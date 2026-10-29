"""Smoke-test the installed custom wheel, including GL reload and reopening."""

import argparse
from importlib.metadata import version
from pathlib import Path
import time
from types import SimpleNamespace

import mujoco
from mujoco.gaussian import GaussianAssets
import mujoco.viewer
import numpy as np


def wait_ready(viewer):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        viewer.sync()
        status = viewer.gaussian_status
        if status == "ready":
            return
        if status.startswith("error:") or not viewer.is_running():
            raise AssertionError(f"Gaussian renderer failed: {status}")
        time.sleep(.02)
    raise AssertionError(f"Timed out waiting for renderer: {viewer.gaussian_status}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gpu", action="store_true")
    args = parser.parse_args()
    assert "+3dgs" in version("mujoco"), version("mujoco")
    data = SimpleNamespace(
        position=np.array([[0., 0., 1.], [1., 0., 1.]]),
        rotation=np.array([[1., 0., 0., 0.], [1., 0., 0., 0.]]),
        scale=np.full((2, 3), .1), rgba=np.array([[1., 0., 0., .9], [0., 1., 0., .9]]),
        adr=np.array([0, 1, 2]), group_id=np.array([0, 1, 0]),
    )
    assets = GaussianAssets(data)
    try:
        first, second = assets.path_for_world(0), assets.path_for_world(1)
        assert first == assets.path_for_world(2) and first != second
        mujoco._simulate.validate_gaussian_ply(first)
        mujoco._simulate.validate_gaussian_ply(second)
        model = mujoco.MjModel.from_xml_string(
            '<mujoco><worldbody><geom type="sphere" size=".1" group="3"/></worldbody></mujoco>')
        state = mujoco.MjData(model)
        if args.gpu:
            for index in range(2):
                with mujoco.viewer.launch_passive(model, state, gaussian_ply=first) as viewer:
                    wait_ready(viewer)
                    viewer.load_gaussian_ply(second)
                    wait_ready(viewer)
                print(f"PASS window {index + 1}: hidden collision geoms, load and reload")
    finally:
        assets.close()
    assert not Path(first).exists()
    print(f"PASS installed wheel {version('mujoco')}: asset cache and cleanup")


if __name__ == "__main__":
    main()
