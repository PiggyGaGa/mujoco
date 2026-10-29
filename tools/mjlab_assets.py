"""Export mjlab manifest assets for the standalone 3DGS simulate viewer.

Run in an environment with mjlab installed. Manifest validation and collision
construction deliberately stay with mjlab; this module only adapts its output.
"""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import json
from pathlib import Path
import tempfile

import numpy as np


@contextmanager
def launch_mjlab_viewer(model, data, gaussian_data, *, env_idx=0, **viewer_options):
    """Open the custom passive viewer with one mjlab world's normalized asset.

    The caller owns stepping/state synchronization and supplies the model already
    assembled by mjlab, including collisions. Asset selection is fixed for this
    viewer's lifetime; reopen to select another asset. No manifest is reapplied.
    """
    import mujoco.viewer

    groups = np.asarray(gaussian_data.group_id)
    if not 0 <= env_idx < len(groups):
        raise ValueError("env_idx is outside the Gaussian world table")
    with tempfile.TemporaryDirectory(prefix="mjlab-simulate-") as directory:
        path = Path(directory) / "visual.ply"
        write_gaussian_ply(gaussian_data, path, asset_id=int(groups[env_idx]))
        with mujoco.viewer.launch_passive(
            model, data, gaussian_ply=str(path), **viewer_options
        ) as viewer:
            yield viewer


def write_gaussian_ply(data, path: Path, *, asset_id: int = 0) -> None:
    """Use the same serializer as the installed viewer bridge."""
    import importlib.util

    source = Path(__file__).resolve().parents[1] / 'python/mujoco/gaussian.py'
    spec = importlib.util.spec_from_file_location('_mjgs_gaussian', source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.write_gaussian_ply(data, path, asset_id=asset_id)


def export_manifest(manifest_path: Path, output_dir: Path, *,
                    spawn_point: str | None = None) -> dict:
    """Export a collision-only scene and visual asset using mjlab's semantics.

    Does not initialize a robot: alignment.json records the selected world pose for
    a future policy runner. The destination must be new to prevent stale assets.
    """
    from mjlab.scene import Scene, SceneCfg, apply_scene_manifest
    from mjlab.scene.gaussian_splat import load_gaussian_splat_scene
    import mujoco

    cfg = SceneCfg()
    manifest = apply_scene_manifest(cfg, manifest_path, spawn_point=spawn_point)
    data = load_gaussian_splat_scene(cfg.gaussian_splats, nworld=1)
    scene = Scene(cfg, device="cpu")
    scene.compile()  # Validate collision construction before creating output.
    output_dir = Path(output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=False)
    scene.write(output_dir)
    # Catch missing external meshes/textures in the portable exported scene.
    mujoco.MjModel.from_xml_path(str(output_dir / "scene.xml"))
    write_gaussian_ply(data, output_dir / "visual.ply")
    metadata = {
        "source_manifest": str(manifest.source_path),
        "source_schema_version": manifest.schema_version,
        "source_visual_sha256": manifest.visual.sha256,
        "source_collision_sha256": manifest.collision.sha256,
        "coordinate_system": "right-handed Z-up meters",
        "transforms_baked": True,
        "collision_geom_group": manifest.collision.geom_group,
        "spawn_position": list(cfg.spawn_position),
        "spawn_quaternion_wxyz": list(cfg.spawn_quaternion_wxyz),
        "visual": "visual.ply",
        "collision_scene": "scene.xml",
    }
    (output_dir / "alignment.json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return metadata


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--spawn-point")
    args = parser.parse_args()
    export_manifest(args.manifest, args.output_dir, spawn_point=args.spawn_point)
    print(f"Exported aligned assets to {args.output_dir.resolve()}")


if __name__ == "__main__":
    main()
