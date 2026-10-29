"""Load the supplied Unitree G1 task on mjlab 1.6 without editing training code.

Only the selected velocity task is registered. Legacy asset-loader spelling is
adapted; robot assets, observations, actuator parameters and policy stay intact.
"""

import importlib
from pathlib import Path
import sys
from types import ModuleType


def load_task(source: Path):
    import mujoco
    import mjlab.utils.os as os_utils
    import mjlab.utils.spec_config as spec_config
    import mjlab.tasks  # Register the current mjlab tasks before the legacy plugin.

    source = source.resolve()
    sys.path.insert(0, str(source))
    importlib.import_module("src")
    # The training package's src.tasks initializer eagerly imports every robot
    # task. Limit discovery to G1 velocity, avoiding unrelated legacy plugins.
    for name, directory in (
        ("src.tasks", source / "src/tasks"),
        ("src.assets", source / "src/assets"),
        ("src.assets.robots", source / "src/assets/robots"),
    ):
        module = ModuleType(name)
        module.__path__ = [str(directory)]
        module.__package__ = name
        sys.modules[name] = module

    def update_assets(assets, path, meshdir=None, glob="*", recursive=False):
        for file in Path(path).glob(glob):
            if file.is_file():
                assets[f"{meshdir}/{file.name}" if meshdir else file.name] = file.read_bytes()
            elif file.is_dir() and recursive:
                update_assets(assets, file, meshdir, glob, recursive)

    previous = getattr(os_utils, "update_assets", None)
    os_utils.update_assets = update_assets
    collision_cls = spec_config.CollisionCfg

    def legacy_collision(**kwargs):
        # mjlab 1.2 supplied these defaults and used zero for unmatched priorities.
        kwargs.setdefault("contype", 1)
        kwargs.setdefault("conaffinity", 1)
        kwargs.setdefault("condim", 3)
        kwargs.setdefault("priority", 0)
        if isinstance(kwargs["priority"], dict):
            kwargs["priority"] = {**kwargs["priority"], ".*": 0}
        return collision_cls(**kwargs)

    spec_config.CollisionCfg = legacy_collision
    try:
        robot = importlib.import_module("src.assets.robots.unitree_g1.g1_constants")
    finally:
        spec_config.CollisionCfg = collision_cls
        if previous is None:
            del os_utils.update_assets
        else:
            os_utils.update_assets = previous

    def get_spec():
        spec = mujoco.MjSpec.from_file(str(robot.G1_XML))
        spec.assets = robot.get_assets(spec.compiler.meshdir)
        return spec

    robot.get_spec = get_spec
    exports = sys.modules["src.assets.robots"]
    exports.G1_ACTION_SCALE = robot.G1_ACTION_SCALE
    exports.get_g1_robot_cfg = robot.get_g1_robot_cfg
    importlib.import_module("src.tasks.velocity.config.g1")
