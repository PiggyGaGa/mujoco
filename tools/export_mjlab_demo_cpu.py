"""Export the bundled mjlab tracking demo for standalone MuJoCo CPU playback.

Run once in the mjlab environment. Playback uses play_mjlab_cpu.py without mjlab.
"""

import argparse
import json
from dataclasses import asdict
from pathlib import Path

import mujoco
import numpy as np
import torch


def array(value):
    return value.detach().cpu().numpy() if isinstance(value, torch.Tensor) else np.asarray(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--scene-manifest", type=Path)
    parser.add_argument("--scene-spawn-point")
    parser.add_argument("--checkpoint", type=Path)
    parser.add_argument("--motion", type=Path)
    parser.add_argument("--reference-steps", type=int, default=100)
    parser.add_argument("--device", default="cuda:0")
    args = parser.parse_args()
    if args.reference_steps <= 0:
        parser.error("--reference-steps must be positive")
    if args.output.exists():
        parser.error("Output must be a new directory")

    from mujoco.gaussian import write_gaussian_ply

    import mjlab.tasks  # noqa: F401 -- Register task configurations.
    from mjlab.actuator import BuiltinPositionActuatorCfg
    from mjlab.envs import ManagerBasedRlEnv
    from mjlab.rl import RslRlVecEnvWrapper
    from mjlab.scene import apply_scene_manifest
    from mjlab.scripts.gcs import ensure_default_checkpoint, ensure_default_motion
    from mjlab.tasks.registry import load_env_cfg, load_rl_cfg, load_runner_cls

    task = "Mjlab-Tracking-Flat-Unitree-G1"
    checkpoint = args.checkpoint or Path(ensure_default_checkpoint())
    motion_path = args.motion or Path(ensure_default_motion())
    cfg = load_env_cfg(task, play=True)
    cfg.scene.num_envs = 1
    # Deterministic transfer benchmark: no pushes, encoder bias or randomized COM.
    cfg.events = {}
    cfg.terminations = {}
    command_cfg = cfg.commands["motion"]
    command_cfg.motion_file = str(motion_path)
    command_cfg.sampling_mode = "start"
    command_cfg.joint_position_range = (0., 0.)
    if args.scene_manifest:
        apply_scene_manifest(cfg.scene, args.scene_manifest,
                             spawn_point=args.scene_spawn_point)
    robot_cfg = cfg.scene.entities["robot"]
    for actuator in robot_cfg.articulation.actuators:
        if not isinstance(actuator, BuiltinPositionActuatorCfg) or actuator.delay_max_lag:
            raise ValueError("CPU export supports non-delayed builtin position actuators only")
    agent_cfg = load_rl_cfg(task)
    env = ManagerBasedRlEnv(cfg=cfg, device=args.device)
    try:
        wrapped = RslRlVecEnvWrapper(env, clip_actions=agent_cfg.clip_actions)
        runner = load_runner_cls(task)(wrapped, asdict(agent_cfg), device=args.device)
        runner.load(str(checkpoint), load_cfg={"actor": True}, strict=True,
                    map_location=args.device)
        policy = runner.get_inference_policy(device=args.device)
        env.seed(0)
        obs, _ = wrapped.reset()
        cmd = env.command_manager.get_term("motion")
        robot = env.scene["robot"]
        action_term = env.action_manager.get_term("joint_pos")
        if action_term.cfg.clip is not None:
            raise ValueError("Per-joint processed-action clipping is not supported")
        if args.reference_steps >= cmd.motion.time_step_total - int(cmd.time_steps[0]):
            raise ValueError("Reference sequence must not wrap the motion")
        output = args.output.resolve()
        output.mkdir(parents=True)
        runner.export_policy_to_onnx(str(output))
        model = env.sim.mj_model
        if env.sim.expanded_fields:
            raise ValueError(f"Unexpected per-world model fields: {env.sim.expanded_fields}")
        mujoco.mj_saveModel(model, str(output / "scene.mjb"), None)
        state_fields = ("qpos", "qvel", "ctrl", "act", "mocap_pos", "mocap_quat")
        np.savez(output / "initial.npz", **{
            n: array(getattr(env.sim.data, n)[0]).copy() for n in state_fields})
        np.savez(output / "motion.npz", **{
            n: array(getattr(cmd.motion, n)) for n in (
                "joint_pos", "joint_vel", "body_pos_w", "body_quat_w")})
        if env.scene.gaussian_splat_data is not None:
            write_gaussian_ply(env.scene.gaussian_splat_data, output / "visual.ply")
        joint_names = [f"robot/{n}" for n in robot.joint_names]
        target_names = [f"robot/{n}" for n in action_term.target_names]
        ctrl_ids = []
        for name in target_names:
            joint_id = model.joint(name).id
            ids = np.flatnonzero(model.actuator_trnid[:, 0] == joint_id)
            if len(ids) != 1:
                raise ValueError(f"Expected one actuator for {name}, found {ids}")
            ctrl_ids.append(int(ids[0]))
        observations = []
        for name in env.observation_manager.active_terms["actor"]:
            term = env.observation_manager.get_term_cfg("actor", name)
            if term.history_length or term.delay_max_lag:
                raise ValueError("Observation history/delay is not supported")
            observations.append(dict(name=name, clip=term.clip,
                                     scale=array(term.scale).tolist() if term.scale is not None else 1.))
        config = dict(
            schema_version=1, task=task, checkpoint=str(checkpoint),
            source_manifest=str(args.scene_manifest) if args.scene_manifest else None,
            mujoco_version=mujoco.mj_versionString(), decimation=cfg.decimation,
            initial_frame=int(cmd.time_steps[0]),
            env_origin=array(env.scene.env_origins[0]).tolist(),
            joint_names=joint_names, action_joint_names=target_names,
            ctrl_ids=ctrl_ids, action_scale=array(action_term.scale).squeeze().tolist(),
            action_offset=array(action_term.offset).squeeze().tolist(),
            clip_actions=agent_cfg.clip_actions,
            default_joint_pos=array(robot.data.default_joint_pos[0]).tolist(),
            default_joint_vel=array(robot.data.default_joint_vel[0]).tolist(),
            observations=observations, anchor_body=f"robot/{cmd.cfg.anchor_body_name}",
            motion_anchor_index=cmd.motion_anchor_body_index,
            linear_velocity_sensor="robot/imu_lin_vel",
            angular_velocity_sensor="robot/imu_ang_vel",
            normalization="embedded in policy.onnx; do not normalize twice",
            deterministic_overrides=["events disabled", "terminations disabled",
                                     "start frame sampling", "zero joint reset noise"],
        )
        (output / "config.json").write_text(json.dumps(config, indent=2) + "\n")
        reference = {n: [] for n in ("qpos", "qvel", "frame", "last_action", "obs", "action", "ctrl")}
        anchor_errors, root_heights = [], []
        with torch.inference_mode():
            for _ in range(args.reference_steps):
                values = dict(qpos=env.sim.data.qpos[0], qvel=env.sim.data.qvel[0],
                              frame=cmd.time_steps[0], last_action=env.action_manager.action[0],
                              obs=obs["actor"][0])
                action = policy(obs)
                values["action"] = action[0]
                for n, value in values.items():
                    reference[n].append(array(value).copy())
                obs, *_ = wrapped.step(action)
                reference["ctrl"].append(array(env.sim.data.ctrl[0])[ctrl_ids].copy())
                anchor_errors.append(float(torch.linalg.vector_norm(
                    cmd.robot_anchor_pos_w[0] - cmd.anchor_pos_w[0])))
                root_heights.append(float(env.sim.data.qpos[0, 2]))
        np.savez(output / "reference.npz", **reference)
        (output / "warp-report.json").write_text(json.dumps(dict(
            steps=args.reference_steps,
            root_height_min=min(root_heights), root_height_final=root_heights[-1],
            anchor_error_mean=float(np.mean(anchor_errors)),
            anchor_error_max=max(anchor_errors),
        ), indent=2) + "\n")
        print(f"Exported CPU bundle: {output}")
    finally:
        env.close()


if __name__ == "__main__":
    main()
