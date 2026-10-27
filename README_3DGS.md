# MuJoCo 3DGS 编译与运行说明

本文记录当前 `feature/3dgs` 分支中 MuJoCo `simulate` 与 Splatapult 3D Gaussian Splatting（3DGS）的编译、测试和运行方法。

## 1. Requirements

本项目需要满足以下功能需求：

1. **`simulate` 支持 3DGS PLY 文件渲染**
   - 在 MuJoCo 原生 `simulate` 查看器中加载并渲染 Gaussian PLY。
   - MuJoCo/MJCF 负责刚体、关节、动力学和碰撞；Gaussian PLY 负责视觉外观。
   - 运行时可按 `F8` 显示或隐藏 3DGS。
   - 当前使用经典 OpenGL 渲染器，要求 OpenGL 4.6 compatibility profile，不支持 Filament 后端。

2. **支持生成 Python 包**
   - 支持从当前源码构建 MuJoCo Python 包。
   - Python 包可供上层 Python 仿真、控制和训练代码调用。

3. **支持与 `mjlab-3dgs` 仓库联合仿真**
   - 当前 MuJoCo 3DGS 分支提供底层物理仿真与 Gaussian 渲染能力。
   - `mjlab-3dgs` 作为上层工程，通过生成的 Python 包连接本仓库，实现控制、场景管理和 3DGS 可视化联合运行。

> 注意：Gaussian PLY 不会自动生成物理碰撞几何，联合仿真所需的碰撞体仍应在 MJCF 中定义。

## 2. 环境与依赖

以下命令适用于 Ubuntu/Debian：

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake git \
  libgl1-mesa-dev libglew-dev libglm-dev \
  libeigen3-dev libsdl2-dev libglfw3-dev
```

使用 NVIDIA 显卡时，先确认驱动与 OpenGL：

```bash
glxinfo -B
```

如果没有 `glxinfo`：

```bash
sudo apt install mesa-utils
```

需要看到 OpenGL 4.6，并确保程序获得 compatibility profile。当前机器验证使用：

```text
OpenGL 4.6.0 NVIDIA 580.82.07
```

## 3. 获取子模块

Splatapult 位于 `third_party/splatapult`，首次编译前执行：

```bash
git submodule sync --recursive
git submodule update --init --recursive
```

可用下面的命令确认子模块已就绪：

```bash
git submodule status
```

正常情况下会显示 `third_party/splatapult` 的提交号，而不是空目录。

## 4. 配置与编译

在仓库根目录执行：

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DMUJOCO_BUILD_SIMULATE=ON \
  -DMUJOCO_ENABLE_3DGS=ON \
  -DMUJOCO_BUILD_3DGS_TESTS=ON \
  -DMUJOCO_USE_FILAMENT=OFF \
  -DMUJOCO_USE_FILAMENT_MJR_COMPAT=OFF

cmake --build build -j"$(nproc)"
```

如果只需要 MuJoCo 与 3DGS，不需要 MuJoCo 的 Python utility 测试，建议在配置时增加：

```bash
-DMUJOCO_TEST_PYTHON_UTIL=OFF
```

这可以避免该测试通过 `FetchContent` 完整克隆 Eigen 仓库。3DGS 本身仍通过系统安装的 `libeigen3-dev` 使用 Eigen。

也可以只编译查看器和 3DGS 测试：

```bash
cmake --build build --target simulate mjgs_asset_test mjgs_render_test -j"$(nproc)"
```

成功后主要文件为：

```text
build/bin/simulate
build/bin/mjgs_asset_test
build/bin/mjgs_render_test
build/lib/libmujoco_3dgs_renderer.a
build/lib/libmujoco_3dgs_splat.a
```

## 5. 测试

### 5.1 PLY 与变换测试（不需要显示器）

```bash
ctest --test-dir build -R '^mjgs_asset_test$' --output-on-failure
```

成功结果应包含：

```text
100% tests passed, 0 tests failed
```

### 5.2 GPU/OpenGL 集成测试

该测试会创建隐藏窗口，验证：

- Gaussian 加载和渲染；
- Gaussian 与 MuJoCo 几何体之间的深度遮挡；
- 非零视口偏移；
- 显示/隐藏切换；
- OpenGL 状态恢复；
- 空视锥处理。

运行：

```bash
./build/bin/mjgs_render_test
```

成功结果末尾应为：

```text
Depth, viewport offset, visibility, native state restoration and empty-frustum tests passed
```

测试还会生成：

```text
/tmp/mjgs-depth-test.ppm
```

如果提示 `glfw init failed` 或 `window creation failed`，确认当前终端有图形会话：

```bash
echo "$DISPLAY"
```

## 6. 运行自带演示

仓库提供：

- MJCF：[model/gaussian/viewer_demo.xml](model/gaussian/viewer_demo.xml)
- 示例 PLY：[third_party/splatapult/data/test.ply](third_party/splatapult/data/test.ply)

从仓库根目录运行：

```bash
./build/bin/simulate \
  model/gaussian/viewer_demo.xml \
  --gs-ply third_party/splatapult/data/test.ply
```

终端出现类似以下输出即表示加载成功：

```text
[INFO] using multi_radixsort.glsl
[3DGS] Loaded 16 Gaussians, GL 4.6.0 NVIDIA 580.82.07
```

窗口中可以通过以下任一方式切换 Gaussian 显示/隐藏：

- 按 `F8` 快捷键。
- 展开左侧 `3DGS` 功能栏，通过 `Show | Hide (F8)` 双选按钮切换。

两种操作使用相同的显示状态和切换逻辑，终端会打印：

```text
[3DGS] Hidden
[3DGS] Visible
```

## 7. 运行自己的 3DGS 模型

```bash
./build/bin/simulate \
  model/gaussian/viewer_demo.xml \
  --gs-ply /path/to/your/point_cloud.ply
```

如果模型坐标、朝向或大小不合适，可使用：

```bash
./build/bin/simulate \
  model/gaussian/viewer_demo.xml \
  --gs-ply /path/to/your/point_cloud.ply \
  --gs-scale 0.5 \
  --gs-translate 0 0 1 \
  --gs-rotate 90 0 0
```

参数说明：

- `--gs-scale S`：统一缩放，必须大于 0。
- `--gs-translate X Y Z`：在 MuJoCo 世界坐标中平移。
- `--gs-rotate RX RY RZ`：以角度指定旋转，组合顺序为 `Rz * Ry * Rx`。
- `--gs-no-sh`：关闭高阶球谐，只使用基础颜色。
- `--gs-help`：显示 3DGS 命令行帮助。

注意：立体视觉、正交相机和 MuJoCo scene transform 当前不受支持；遇到这些模式时 splats 会隐藏并输出警告。

## 8. 自动截图

下面的命令会在第 10 帧保存截图，然后自动退出：

```bash
./build/bin/simulate \
  model/gaussian/viewer_demo.xml \
  --gs-ply third_party/splatapult/data/test.ply \
  --gs-screenshot /tmp/mjgs-smoke.ppm \
  --gs-capture-frame 10 \
  --gs-exit-after-capture
```

成功时输出：

```text
[3DGS] Screenshot: /tmp/mjgs-smoke.ppm
```

## 9. PLY 格式要求

输入必须是非空的 binary little-endian、仅包含 vertex element 的 Gaussian PLY，并至少包含以下 `float` 属性：

```text
x y z
opacity
scale_0 scale_1 scale_2
rot_0 rot_1 rot_2 rot_3
f_dc_0 f_dc_1 f_dc_2
```

约束：

- 所有 Gaussian 参数必须是有限数值，不能包含 NaN 或 Inf。
- `rot_0..3` 使用归一化的 `wxyz` 四元数，允许约 0.02 的范数误差。
- `scale_0..2` 保存对数尺度，取指数后必须为有限正数。
- 可以包含 `f_rest_*` 高阶球谐属性；若没有，渲染器可能给出 warning，但基础颜色仍可工作。

普通 ASCII PLY、网格 PLY或包含额外 element（例如 face）的文件不会被此加载器接受。

## 10. MJCF 与碰撞体

Gaussian PLY 只增加外观，不参与 MuJoCo 物理计算。需要在 MJCF 中单独添加碰撞几何，例如：

```xml
<worldbody>
  <geom name="ground" type="plane" size="0 0 0.1"/>
  <geom name="obstacle" type="box" pos="0 0 0.5" size="0.5 0.5 0.5"/>
</worldbody>
```

若不希望这些碰撞体遮挡 3DGS，可通过材质、透明度或几何设计进行调整；透明视觉几何与实际碰撞几何也可以分开定义。

## 11. 常见问题

### 11.1 CMakeCache 指向旧目录

如果移动或复制过仓库，可能出现：

```text
The current CMakeCache.txt directory is different than the directory where CMakeCache.txt was created
CMake step for lodepng failed
```

最稳妥的处理方式是保留旧构建目录作为备份，再重新配置：

```bash
mv build build.old
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DMUJOCO_ENABLE_3DGS=ON \
  -DMUJOCO_BUILD_3DGS_TESTS=ON \
  -DMUJOCO_TEST_PYTHON_UTIL=OFF
cmake --build build -j"$(nproc)"
```

CMake 构建目录不可在不同源码路径之间直接复用。

### 11.2 Eigen 下载长时间停在 `Cloning into 'eigen3-src'`

MuJoCo 的 Python utility 测试会通过 CMake `FetchContent` 完整克隆 Eigen，GitLab 网络慢时看起来会卡住。若不需要该测试，重新配置时关闭它：

```bash
cmake -S . -B build -DMUJOCO_TEST_PYTHON_UTIL=OFF
```

若必须启用，可尝试让 Git 使用 HTTP/1.1：

```bash
GIT_CONFIG_COUNT=1 \
GIT_CONFIG_KEY_0=http.version \
GIT_CONFIG_VALUE_0=HTTP/1.1 \
cmake -S . -B build
```

### 11.3 找不到 GLEW、GLM、Eigen3 或 SDL2

重新安装开发包：

```bash
sudo apt install -y libglew-dev libglm-dev libeigen3-dev libsdl2-dev
```

当前成功配置对应的包路径包括：

```text
GLEW:   /usr/include, /usr/lib/x86_64-linux-gnu/libGLEW.so
GLM:    /usr/lib/cmake/glm
Eigen3: /usr/share/eigen3/cmake
SDL2:   /usr/lib/x86_64-linux-gnu/cmake/SDL2
```

### 11.4 3DGS 被禁用

若出现：

```text
[3DGS] Disabled: Need OpenGL 4.6 compatibility profile
```

检查显卡驱动、远程桌面/虚拟机的 OpenGL 转发以及 `glxinfo -B`。软件 OpenGL、某些 Wayland/X11 转发环境或 core-only context 可能不满足要求。

### 11.5 PLY 加载失败

根据终端错误检查：

- 文件是否为 binary little-endian；
- 是否只包含 vertex element；
- 必需属性是否为 `float`；
- 四元数是否为归一化 `wxyz`；
- 文件是否截断；
- 是否包含 NaN/Inf。

## 12. 快速检查清单

```bash
# 1. 确认构建选项
grep -E 'MUJOCO_(ENABLE_3DGS|BUILD_3DGS_TESTS|BUILD_SIMULATE)' build/CMakeCache.txt

# 2. 资源测试
ctest --test-dir build -R '^mjgs_asset_test$' --output-on-failure

# 3. GPU 测试
./build/bin/mjgs_render_test

# 4. 交互演示
./build/bin/simulate \
  model/gaussian/viewer_demo.xml \
  --gs-ply third_party/splatapult/data/test.ply
```
