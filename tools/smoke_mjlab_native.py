"""Run the real mjlab demo CLI with room1/native, stopping after N policy steps.

Execute with `uv run --no-sync` in the mjlab environment containing the custom
wheel. Uses demo's standard checkpoint/motion retrieval and SHA-256 validation.
"""

import argparse
import importlib
from pathlib import Path
import re
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--steps", type=int, default=100)
    parser.add_argument("--checkpoint-dir", type=Path)
    parser.add_argument("--checkpoint", type=Path)
    parser.add_argument("--task-id", default="Unitree-G1-Flat")
    parser.add_argument("--task-source", type=Path)
    parser.add_argument("--task-module", default="src.tasks.velocity.config.g1")
    parser.add_argument("--manifest", type=Path, default=(
        Path(__file__).resolve().parents[2] /
        "mjlab/src/mjlab/asset_zoo/3dgs/room1/manifest.json"))
    args = parser.parse_args()
    if args.steps < 1:
        parser.error("--steps must be positive")
    if args.checkpoint and args.checkpoint_dir:
        parser.error("Choose --checkpoint or --checkpoint-dir")
    checkpoint = args.checkpoint
    if args.checkpoint_dir:
        candidates = []
        for path in args.checkpoint_dir.glob("model_*.pt"):
            match = re.fullmatch(r"model_(\d+)\.pt", path.name)
            if match:
                candidates.append((int(match[1]), path))
        if not candidates:
            parser.error("No model_<iteration>.pt checkpoints in directory")
        checkpoint = max(candidates)[1]
    if args.task_source:
        if args.task_module == "src.tasks.velocity.config.g1":
            from unitree_g1_compat import load_task
            load_task(args.task_source)
        else:
            sys.path.insert(0, str(args.task_source.resolve()))
            importlib.import_module(args.task_module)

    from mjlab.scripts.demo import main as demo_main
    from mjlab.viewer.native import NativeMujocoViewer

    run = NativeMujocoViewer.run
    sync = NativeMujocoViewer.sync_env_to_viewer
    observed = {"ready": False, "steps": 0}

    def checked_sync(self):
        sync(self)
        status = self.viewer.gaussian_status
        if status.startswith("error:"):
            raise AssertionError(status)
        observed["ready"] |= status == "ready"
        if time.monotonic() - start > 300:
            raise TimeoutError("Native playback exceeded 300 seconds")

    def limited_run(self, *unused, **kwargs):
        run(self, num_steps=args.steps)
        observed["steps"] = self._step_count
        if self._last_error:
            raise AssertionError(self._last_error)

    NativeMujocoViewer.run = limited_run
    NativeMujocoViewer.sync_env_to_viewer = checked_sync
    sys.argv = ["demo", "--use-3dgs", "True", "--viewer", "native",
                "--num-envs", "1", "--scene-manifest", str(args.manifest)]
    start = time.monotonic()
    if checkpoint is None:
        demo_main()
    else:
        from mjlab.scripts.play import PlayConfig, run_play

        print(f"Using task {args.task_id}, checkpoint {checkpoint.resolve()}")
        run_play(args.task_id, PlayConfig(
            checkpoint_file=str(checkpoint.resolve()), viewer="native", num_envs=1,
            scene_manifest=str(args.manifest.resolve()),
        ))
    assert observed["ready"], "Gaussian renderer never reported ready"
    assert observed["steps"] >= args.steps, observed
    print(f"PASS room1 native: {observed['steps']} trained policy steps; 3DGS ready")


if __name__ == "__main__":
    main()
