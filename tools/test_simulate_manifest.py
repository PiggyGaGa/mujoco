#!/usr/bin/env python3
"""Exercise C++ simulate manifest loading, optionally including GPU screenshots.

No mjlab/Python MuJoCo dependencies are needed; only the Python standard library.
"""

import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = (ROOT.parent / "mjlab/src/mjlab/asset_zoo/3dgs/room1/manifest.json")


def run(binary, args, *, success=True, contains=None):
    try:
        result = subprocess.run([str(binary), *map(str, args)], capture_output=True,
                                text=True, timeout=120)
    except subprocess.TimeoutExpired as exc:
        output = (exc.stdout or b"") + (exc.stderr or b"")
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        raise AssertionError(f"Command timed out: {args}\n{output}") from exc
    output = result.stdout + result.stderr
    if (result.returncode == 0) != success or (contains and contains not in output):
        raise AssertionError(f"Command failed expectation: {args}\n{output}")
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulate", type=Path, default=ROOT / "build/bin/simulate")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--gpu", action="store_true",
                        help="Also test room1 and legacy XML+PLY screenshot rendering")
    args = parser.parse_args()
    binary = args.simulate.resolve()
    manifest = args.manifest.resolve()
    run(binary, ["--scene-manifest", manifest, "--scene-check"], contains="[manifest] OK:")
    print("PASS real manifest: hashes, PLY validation, collision compile")

    with tempfile.TemporaryDirectory(prefix="simulate-manifest-test-") as directory:
        directory = Path(directory)
        ply = directory / "visual.ply"
        shutil.copyfile(ROOT / "third_party/splatapult/data/test.ply", ply)
        collision = directory / "collision.xml"
        collision.write_text('<mujoco><worldbody><geom name="box" type="box" '
                             'pos="1 0 0" size=".1 .1 .1"/></worldbody></mujoco>')
        digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
        document = {
            "schema_version": 1,
            "coordinate_system": {"handedness": "right", "up_axis": "z", "length_unit": "meter"},
            "visual": {"type": "3dgs_ply", "path": ply.name, "sha256": digest(ply)},
            "collision": {"type": "mjcf", "path": collision.name, "sha256": digest(collision),
                          "geom_group": 4, "dependencies": []},
            "world_from_scene": {"position": [3, 4, 5],
                                 "quaternion_wxyz": [math.sqrt(.5), 0, 0, math.sqrt(.5)]},
            "spawn_points": [{"name": "default", "position": [2, 0, 0],
                              "quaternion_wxyz": [1, 0, 0, 0]}],
        }
        path = directory / "manifest.json"

        def check(doc, *, success=True, contains=None, extra=()):
            path.write_text(json.dumps(doc))
            return run(binary, ["--scene-manifest", path, "--scene-check", *extra],
                       success=success, contains=contains)

        output = check(document, contains="geom manifest/box group 4 pos 3 5 5")
        assert "Spawn world position 3 6 5" in output, output
        print("PASS quaternion transform, collision group and world spawn")
        base = directory / "robot.xml"
        base.write_text('<mujoco><worldbody><body name="robot" pos="0 0 2">'
                        '<freejoint/><geom name="robot_geom" type="sphere" size=".1"/>'
                        '</body></worldbody></mujoco>')
        check(document, extra=[base], contains="2 geoms")
        print("PASS base robot XML + manifest collision merge")

        cases = [
            ("schema", lambda j: j.update(schema_version=2), "schema_version"),
            ("hash", lambda j: j["visual"].update(sha256="0" * 64), "SHA256 mismatch"),
            ("traversal", lambda j: j["visual"].update(path="../visual.ply"), "traversal"),
            ("quaternion", lambda j: j["world_from_scene"].update(quaternion_wxyz=[0, 0, 0, 0]), "unit wxyz"),
            ("group", lambda j: j["collision"].update(geom_group=6), "geom_group"),
            ("coordinates", lambda j: j["coordinate_system"].update(up_axis="y"), "Z-up"),
            ("unknown field", lambda j: j.update(unrecognized=True), "Unsupported field"),
        ]
        for name, mutate, expected in cases:
            invalid = copy.deepcopy(document)
            mutate(invalid)
            check(invalid, success=False, contains=expected)
            print(f"PASS reject {name}")
        check(document, extra=["--scene-spawn-point", "missing"], success=False,
              contains="spawn-point")
        check(document, extra=["--gs-ply", ply], success=False, contains="cannot be combined")

        collision.write_text('<mujoco><worldbody><body><freejoint/>'
                             '<geom type="sphere" size="1"/></body></worldbody></mujoco>')
        invalid = copy.deepcopy(document)
        invalid["collision"]["sha256"] = digest(collision)
        check(invalid, success=False, contains="must be static")
        print("PASS reject missing spawn, CLI conflicts and dynamic collision")

        mesh = directory / "tetra.obj"
        mesh.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\n"
                        "f 1 3 2\nf 1 2 4\nf 1 4 3\nf 2 3 4\n")
        collision.write_text('<mujoco><asset><mesh name="tetra" file="tetra.obj"/>'
                             '</asset><worldbody><geom type="mesh" mesh="tetra"/>'
                             '</worldbody></mujoco>')
        external = copy.deepcopy(document)
        external["collision"].update(sha256=digest(collision), dependencies=[
            {"path": mesh.name, "sha256": digest(mesh)}])
        check(external, contains="[manifest] OK:")
        missing = copy.deepcopy(external)
        missing["collision"]["dependencies"] = []
        check(missing, success=False, contains="dependencies does not match")
        corrupt = copy.deepcopy(external)
        corrupt["collision"]["dependencies"][0]["sha256"] = "0" * 64
        check(corrupt, success=False, contains="SHA256 mismatch")
        print("PASS external collision mesh and dependency validation")

        if args.gpu:
            for name, launch in [
                ("manifest", ["--scene-manifest", manifest]),
                ("legacy", [ROOT / "model/gaussian/viewer_demo.xml", "--gs-ply",
                            ROOT / "third_party/splatapult/data/test.ply"]),
            ]:
                screenshot = directory / f"{name}.ppm"
                output = run(binary, [*launch, "--gs-screenshot", screenshot,
                                      "--gs-capture-frame", "10", "--gs-exit-after-capture"],
                             contains="[3DGS] Loaded")
                assert screenshot.is_file() and screenshot.stat().st_size > 100, output
                print(f"PASS {name} GPU load and screenshot")
    print("All manifest tests passed")


if __name__ == "__main__":
    main()
