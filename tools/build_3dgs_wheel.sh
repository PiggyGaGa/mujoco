#!/usr/bin/env bash
# Build from the sdist, then bundle non-driver native dependencies on Linux.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
python=${MJGS_PYTHON:-python3}
python=$("$python" -c 'import sys; print(sys.executable)')
native_build=${MJGS_BUILD_DIR:-"$repo/build"}
stage=${MJGS_STAGE_DIR:-$(mktemp -d /tmp/mjgs-wheel.XXXXXX)}
version=$("$python" -c 'import sys,tomllib; print(tomllib.load(open(sys.argv[1],"rb"))["project"]["version"])' "$repo/python/pyproject.toml")
export PATH="$(dirname "$python"):$PATH"
export VIRTUAL_ENV=$("$python" -c 'import sys; print(sys.prefix)')
export MUJOCO_SKIP_BUILD_DEPS=1
export CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-4}
mkdir -p "$stage/source" "$stage/runtime/empty_plugins" "$repo/dist"
cmake --install "$native_build" --prefix "$stage/runtime"
bash "$repo/python/make_sdist.sh"
tar -xf "$repo/python/dist/mujoco-$version.tar.gz" -C "$stage/source"
export MUJOCO_PATH="$stage/runtime"
export MUJOCO_PLUGIN_PATH="$stage/runtime/empty_plugins"
export MUJOCO_CMAKE_ARGS="${MUJOCO_CMAKE_ARGS:-} -DMUJOCO_ENABLE_3DGS=ON -DMUJOCO_BUILD_3DGS_TESTS=OFF"
"$python" -m build --wheel --no-isolation "$stage/source/mujoco-$version"
# OpenGL/GLX dispatch belongs to the host GPU stack, not the Python wheel.
"$python" -m auditwheel repair --plat "${MJGS_WHEEL_PLATFORM:-manylinux_2_35_x86_64}" \
  --exclude libOpenGL.so.0 --exclude libGLdispatch.so.0 --exclude libGLX.so.0 \
  -w "$repo/dist" "$stage/source/mujoco-$version/dist/"*.whl
printf 'Wheel directory: %s\nBuild stage: %s\n' "$repo/dist" "$stage"
