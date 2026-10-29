"""Bridge normalized Gaussian assets to the 3DGS passive viewer.

Accepts mjlab-style arrays without importing mjlab or changing collision models.
"""

from pathlib import Path
import tempfile
import numpy as np


def write_gaussian_ply(data, path: Path, *, asset_id: int = 0) -> None:
    """Write one normalized mjlab asset as binary Graphdeco PLY, using DC color.

    Positions/rotations/scales are already in world coordinates. No additional
    viewer transform should be applied. Endpoint alpha is clamped to float32
    epsilon because the file stores finite logit opacity.
    """
    adr = np.asarray(data.adr)
    if asset_id < 0 or asset_id >= len(adr) - 1:
        raise ValueError("asset_id is outside the Gaussian asset table")
    start, end = int(adr[asset_id]), int(adr[asset_id + 1])
    if not 0 <= start < end <= len(data.position):
        raise ValueError("Invalid or empty Gaussian asset range")
    selection = slice(start, end)
    arrays = [np.asarray(value[selection], dtype=np.float64) for value in
              (data.position, data.rotation, data.scale, data.rgba)]
    position, rotation, scale, rgba = arrays
    count = end - start
    if any(a.shape != (count, width) for a, width in
           zip(arrays, (3, 4, 3, 4))):
        raise ValueError("Invalid Gaussian array shapes")
    if not all(np.all(np.isfinite(a)) for a in arrays):
        raise ValueError("Gaussian arrays must be finite")
    if np.any(scale <= 0) or np.any((rgba < 0) | (rgba > 1)):
        raise ValueError("Scales must be positive and RGBA must be in [0, 1]")
    norms = np.linalg.norm(rotation, axis=1)
    if not np.allclose(norms, 1, rtol=1e-6, atol=1e-6):
        raise ValueError("Gaussian rotations must be unit wxyz quaternions")
    epsilon = np.finfo(np.float32).eps
    alpha = np.clip(rgba[:, 3], epsilon, 1 - epsilon)
    rows = np.column_stack((
        position, np.log(alpha) - np.log1p(-alpha), np.log(scale),
        rotation, (rgba[:, :3] - 0.5) / 0.28209479177387814,
    )).astype("<f4")
    if not np.all(np.isfinite(rows)):
        raise ValueError("Gaussian values overflow float32 PLY storage")
    fields = ("x", "y", "z", "opacity", "scale_0", "scale_1", "scale_2",
              "rot_0", "rot_1", "rot_2", "rot_3", "f_dc_0", "f_dc_1", "f_dc_2")
    header = ("ply\nformat binary_little_endian 1.0\n"
              f"element vertex {count}\n" +
              "".join(f"property float {field}\n" for field in fields) +
              "end_header\n")
    with Path(path).open("wb") as stream:
        stream.write(header.encode("ascii"))
        stream.write(rows.tobytes())


class GaussianAssets:
    """Cache world-coordinate PLYs until the associated viewer has closed.

    The data must provide position, rotation (wxyz), scale, rgba, adr and
    group_id arrays, as produced by mjlab's GaussianSplatData loader.
    """

    def __init__(self, data):
        self.data = data
        self._directory = tempfile.TemporaryDirectory(prefix='mujoco-gaussians-')
        self._paths = {}

    def path_for_world(self, env_idx):
        groups = np.asarray(self.data.group_id)
        if not 0 <= env_idx < len(groups):
            raise ValueError('env_idx is outside the Gaussian world table')
        asset_id = int(groups[env_idx])
        if asset_id not in self._paths:
            path = Path(self._directory.name) / f'asset_{asset_id}.ply'
            write_gaussian_ply(self.data, path, asset_id=asset_id)
            self._paths[asset_id] = str(path)
        return self._paths[asset_id]

    def close(self):
        self._directory.cleanup()
