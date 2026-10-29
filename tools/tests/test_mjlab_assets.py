"""Array adapter tests; optional round-trip against a local mjlab checkout."""

import importlib.util
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mjlab_assets import write_gaussian_ply


class AssetAlignmentTest(unittest.TestCase):
    def setUp(self):
        self.data = SimpleNamespace(
            position=np.array([[1., 2., 3.], [4., 5., 6.]]),
            rotation=np.array([[1., 0., 0., 0.], [0., 0., 0., 1.]]),
            scale=np.array([[.1, .2, .3], [1., 2., 3.]]),
            rgba=np.array([[.2, .5, .8, .25], [.1, .3, .9, .75]]),
            adr=np.array([0, 1, 2]),
        )

    def test_binary_encoding_and_asset_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "visual.ply"
            write_gaussian_ply(self.data, path, asset_id=1)
            header, payload = path.read_bytes().split(b"end_header\n")
        self.assertIn(b"element vertex 1", header)
        row = np.frombuffer(payload, dtype="<f4")
        np.testing.assert_allclose(row[:3], [4, 5, 6])
        np.testing.assert_allclose(np.exp(row[4:7]), [1, 2, 3], rtol=1e-6)
        np.testing.assert_allclose(row[7:11], [0, 0, 0, 1])
        self.assertAlmostEqual(float(1 / (1 + np.exp(-row[3]))), .75, places=6)

    def test_invalid_arrays_do_not_create_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "visual.ply"
            self.data.scale[0, 0] = 0
            with self.assertRaises(ValueError):
                write_gaussian_ply(self.data, path)
            self.assertFalse(path.exists())

    def test_roundtrip_with_mjlab_loader(self):
        root = os.environ.get("MJLAB_SOURCE")
        if not root:
            self.skipTest("Set MJLAB_SOURCE to run the upstream loader round-trip")
        source = Path(root) / "src/mjlab/scene/gaussian_splat.py"
        spec = importlib.util.spec_from_file_location("alignment_upstream", source)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        # Use upstream transforms, then ensure export/reload does not apply them twice.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "visual.ply"
            write_gaussian_ply(self.data, path)
            transformed = module.load_gaussian_splat_asset(
                module.GaussianSplatAssetCfg(
                    path=str(path), pos=(3, 4, 5),
                    quat=(2 ** -.5, 0, 0, 2 ** -.5), scale=2))
            np.testing.assert_allclose(transformed.position, [[-1, 6, 11]], atol=1e-6)
            write_gaussian_ply(transformed, path)
            loaded = module.load_gaussian_splat_asset(
                module.GaussianSplatAssetCfg(path=str(path)))
        for field in ("position", "rotation", "scale", "rgba"):
            np.testing.assert_allclose(getattr(loaded, field),
                                       getattr(transformed, field), atol=1e-6)


if __name__ == "__main__":
    unittest.main()
