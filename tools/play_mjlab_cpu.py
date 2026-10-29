"""Run an exported mjlab tracking policy using only NumPy, ONNX Runtime and MuJoCo.

No mjlab, Torch or Warp imports are used during playback.
"""

import argparse
import json
import time
from contextlib import nullcontext
from pathlib import Path

import mujoco
import numpy as np
import onnxruntime as ort


def rotation(quaternion):
    matrix = np.empty(9)
    mujoco.mju_quat2Mat(matrix, np.asarray(quaternion, dtype=np.float64))
    return matrix.reshape(3, 3)


class CpuPlayer:
    def __init__(self, bundle):
        self.bundle = Path(bundle).resolve()
        self.cfg = json.loads((self.bundle / "config.json").read_text())
        if self.cfg["schema_version"] != 1:
            raise ValueError("Unsupported CPU bundle version")
        if self.cfg["mujoco_version"] != mujoco.mj_versionString():
            raise ValueError("scene.mjb requires the MuJoCo version used for export: "
                             + self.cfg["mujoco_version"])
        self.model = mujoco.MjModel.from_binary_path(str(self.bundle / "scene.mjb"))
        self.data = mujoco.MjData(self.model)
        self.motion = np.load(self.bundle / "motion.npz", allow_pickle=False)
        self.initial = np.load(self.bundle / "initial.npz", allow_pickle=False)
        options = ort.SessionOptions()
        options.intra_op_num_threads = 1
        self.policy = ort.InferenceSession(
            str(self.bundle / "policy.onnx"), options,
            providers=["CPUExecutionProvider"])
        self.joints = np.array([self.model.joint(n).id for n in self.cfg["joint_names"]])
        self.qadr = self.model.jnt_qposadr[self.joints]
        self.vadr = self.model.jnt_dofadr[self.joints]
        self.ctrl_ids = np.array(self.cfg["ctrl_ids"])
        self.anchor = self.model.body(self.cfg["anchor_body"]).id
        self.reset()

    def reset(self):
        mujoco.mj_resetData(self.model, self.data)
        for name in self.initial.files:
            getattr(self.data, name)[:] = self.initial[name]
        self.frame = int(self.cfg["initial_frame"])
        self.last_action = np.zeros(len(self.ctrl_ids), dtype=np.float32)
        mujoco.mj_forward(self.model, self.data)

    def observation(self):
        d, c, f = self.data, self.cfg, self.frame
        current = rotation(d.xquat[self.anchor])
        target = rotation(self.motion["body_quat_w"][f, c["motion_anchor_index"]])
        target_pos = (self.motion["body_pos_w"][f, c["motion_anchor_index"]]
                      + np.asarray(c["env_origin"]))
        terms = {
            "command": np.concatenate((self.motion["joint_pos"][f],
                                       self.motion["joint_vel"][f])),
            "motion_anchor_pos_b": current.T @ (target_pos - d.xpos[self.anchor]),
            "motion_anchor_ori_b": (current.T @ target)[:, :2].ravel(),
            "base_lin_vel": d.sensor(c["linear_velocity_sensor"]).data,
            "base_ang_vel": d.sensor(c["angular_velocity_sensor"]).data,
            "joint_pos": d.qpos[self.qadr] - np.asarray(c["default_joint_pos"]),
            "joint_vel": d.qvel[self.vadr] - np.asarray(c["default_joint_vel"]),
            "actions": self.last_action,
        }
        output = []
        for term in c["observations"]:
            value = np.asarray(terms[term["name"]], dtype=np.float32)
            if term["clip"] is not None:
                value = np.clip(value, *term["clip"])
            value = value * np.asarray(term["scale"], dtype=np.float32)
            output.append(value)
        return np.concatenate(output).astype(np.float32)[None]

    def infer(self, observation=None):
        observation = self.observation() if observation is None else observation
        return self.policy.run(["actions"], {
            "obs": observation,
            "time_step": np.array([[self.frame]], dtype=np.float32),
        })[0][0]

    def control(self, action):
        limit = self.cfg["clip_actions"]
        if limit is not None:
            action = np.clip(action, -limit, limit)
        return action, (action * np.asarray(self.cfg["action_scale"])
                        + np.asarray(self.cfg["action_offset"]))

    def step(self):
        action, target = self.control(self.infer())
        if not np.isfinite(target).all():
            raise RuntimeError("Non-finite policy action")
        self.last_action = action.copy()
        self.data.ctrl[self.ctrl_ids] = target
        for _ in range(self.cfg["decimation"]):
            mujoco.mj_step(self.model, self.data)
        # Match mjlab's explicit post-decimation forward before observations.
        mujoco.mj_forward(self.model, self.data)
        self.frame += 1
        if not np.isfinite(self.data.qpos).all() or not np.isfinite(self.data.qvel).all():
            raise RuntimeError("Non-finite CPU simulation state")


def validate(bundle):
    """Compare CPU observations/ONNX actions on identical GPU reference states."""
    player = CpuPlayer(bundle)
    samples = np.load(player.bundle / "reference.npz", allow_pickle=False)
    obs_error = action_error = ctrl_error = 0.0
    for i in range(len(samples["frame"])):
        player.data.qpos[:] = samples["qpos"][i]
        player.data.qvel[:] = samples["qvel"][i]
        player.frame = int(samples["frame"][i])
        player.last_action = samples["last_action"][i]
        mujoco.mj_forward(player.model, player.data)
        obs = player.observation()
        action = player.infer(obs)
        obs_error = max(obs_error, float(np.max(np.abs(obs[0] - samples["obs"][i]))))
        action_error = max(action_error, float(np.max(np.abs(action - samples["action"][i]))))
        _, ctrl = player.control(action)
        ctrl_error = max(ctrl_error, float(np.max(np.abs(ctrl - samples["ctrl"][i]))))
    result = dict(samples=len(samples["frame"]), observation_max_abs=obs_error,
                  action_max_abs=action_error, control_max_abs=ctrl_error)
    if obs_error > 2e-4 or action_error > 2e-3 or ctrl_error > 2e-3:
        raise AssertionError(result)
    print("PASS identical-state alignment:", json.dumps(result))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--steps", type=int, default=1000)
    parser.add_argument("--viewer", choices=("none", "native"), default="none")
    parser.add_argument("--validate", action="store_true")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--max-anchor-error", type=float,
                        help="Fail if reference anchor position error exceeds this many meters")
    args = parser.parse_args()
    if args.steps <= 0:
        parser.error("--steps must be positive")
    if args.max_anchor_error is not None and args.max_anchor_error <= 0:
        parser.error("--max-anchor-error must be positive")
    alignment = validate(args.bundle) if args.validate else None
    player = CpuPlayer(args.bundle)
    available = len(player.motion["joint_pos"]) - player.frame - 1
    if args.steps > available:
        parser.error(f"Only {available} motion steps remain; choose a shorter --steps")
    context = nullcontext(None)
    if args.viewer == "native":
        import mujoco.viewer
        context = mujoco.viewer.launch_passive(
            player.model, player.data,
            gaussian_ply=str(player.bundle / "visual.ply")
            if (player.bundle / "visual.ply").exists() else None)
    heights, errors = [], []
    start = time.monotonic()
    with context as viewer:
        if viewer:
            viewer.cam.lookat[:] = player.data.xpos[player.anchor]
            viewer.cam.distance = 3
            viewer.cam.elevation = -15
        for _ in range(args.steps):
            tick = time.monotonic()
            if viewer and not viewer.is_running():
                raise RuntimeError("Viewer closed before test completed")
            player.step()
            heights.append(float(player.data.qpos[2]))
            ref = player.motion["body_pos_w"][player.frame, player.cfg["motion_anchor_index"]]
            errors.append(float(np.linalg.norm(
                player.data.xpos[player.anchor] - ref - player.cfg["env_origin"])))
            if viewer:
                viewer.sync()
                if viewer.gaussian_status.startswith("error:"):
                    raise RuntimeError(viewer.gaussian_status)
                time.sleep(max(0, player.model.opt.timestep * player.cfg["decimation"]
                               - (time.monotonic() - tick)))
        status = viewer.gaussian_status if viewer else "headless"
        if viewer and (player.bundle / "visual.ply").exists() and status != "ready":
            raise RuntimeError(f"3DGS was not ready: {status}")
    result = dict(steps=args.steps, simulation_seconds=float(player.data.time),
                  wall_seconds=time.monotonic() - start, renderer=status,
                  root_height_min=min(heights), root_height_final=heights[-1],
                  anchor_error_mean=float(np.mean(errors)), anchor_error_max=max(errors),
                  anchor_error_peak_step=int(np.argmax(errors)) + 1,
                  first_anchor_error_over_0_5m_step=next(
                      (i + 1 for i, error in enumerate(errors) if error > .5), None),
                  alignment=alignment)
    if args.report:
        args.report.write_text(json.dumps(result, indent=2) + "\n")
    if args.max_anchor_error is not None and max(errors) > args.max_anchor_error:
        raise AssertionError(f"Anchor error {max(errors):.6f} m exceeds "
                             f"{args.max_anchor_error} m; report: {result}")
    print("PASS CPU rollout (finite state):", json.dumps(result))


if __name__ == "__main__":
    main()
