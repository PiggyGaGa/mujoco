# MuJoCo 3DGS 编译与运行说明

本文记录当前 `feature/3dgs` 分支中 MuJoCo `simulate` 与 Splatapult 3D Gaussian Splatting（3DGS）的编译、测试和运行方法。


## 路径约定

文中的示例路径需要按自己的目录设置。涉及两个仓库的命令执行前，在同一终端设置：

```bash
export MUJOCO_REPO=/path/to/mujoco
export MJLAB_REPO=/path/to/mjlab
```

`MUJOCO_REPO` 指向本仓库，`MJLAB_REPO` 指向上层 mjlab-3dgs 仓库；请使用绝对路径。
涉及已有 Unitree 策略的示例还需设置原训练仓库和 checkpoint 目录：

```bash
export UNITREE_TASK_REPO=/path/to/unitree_rl_mjlab
export CHECKPOINT_DIR=/path/to/g1_velocity/run_directory
```

这些是路径占位示例，不是需要创建的固定目录。仅编译独立查看器时不需要 mjlab 和策略目录。

## 1. Requirements

### 半透明 mesh 与 3DGS 的绘制顺序（2026-11-10）

经典 OpenGL native viewer 的顺序调整为：不透明几何、阴影和背景 → 3DGS →
半透明 geom/mesh → 标签和 UI。半透明几何继续使用 MuJoCo 原有混合与排序，
不写深度；不修改 alpha=0.995 的透明分类阈值。

`mjr_renderWithCallback` 是经典渲染器的可选扩展，在透明阶段前调用外部绘制，
原 `mjr_render` 使用空回调，保持原流程。回调必须恢复改动的 GL 状态。
`mjgs::RenderScene` 集成该回调并在完整合成后截图。未加载 PLY 或隐藏 3DGS
时应与原图一致；segmentation 不调用外部颜色绘制。

显式运行需要桌面 OpenGL 4.6 compatibility context 的回归：

```bash
cmake -S . -B build -DMUJOCO_BUILD_3DGS_TESTS=ON
cmake --build build --target simulate mjgs_render_test mjgs_transparency_test -j4
./build/bin/mjgs_render_test
./build/bin/mjgs_transparency_test
```

新增回归检查真实 mesh 的 alpha=0.1/0.5/1 像素混合、不透明遮挡、隐藏/无 PLY
图像一致性、segmentation 隔离及截图包含透明阶段。它不证明 Gaussian 与
透明 mesh 的逐片元联合排序：透明 mesh 作为调试覆盖层，位于 Gaussian 墙后
仍可能显示。原有立体、正交和 scene transform 限制仍保留。

修改本仓库源码后，上层 mjlab 使用的已安装 wheel 不会自动更新。需要重新构建
并安装当前 Python 版本的自定义 wheel，或运行这里重新编译的 simulate。

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
  build-essential gcc-11 g++-11 cmake git \
  libgl1-mesa-dev libglew-dev libglm-dev \
  libeigen3-dev libsdl2-dev libglfw3-dev \
  nlohmann-json3-dev libssl-dev
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


下面沿用本机验证过的 GCC 11（`gcc-11` / `g++-11`）。GCC 11 是编译器版本，
不是 C++11 语言标准；语言标准由项目 CMake 配置决定。更换编译器时使用新的
构建目录，避免沿用旧的 CMakeCache。

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DMUJOCO_BUILD_SIMULATE=ON \
  -DMUJOCO_ENABLE_3DGS=ON \
  -DMUJOCO_BUILD_3DGS_TESTS=ON \
  -DMUJOCO_USE_FILAMENT=OFF \
  -DMUJOCO_USE_FILAMENT_MJR_COMPAT=OFF \
  -DCMAKE_C_COMPILER=gcc-11 \
  -DCMAKE_CXX_COMPILER=g++-11

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

## 13. 与 mjlab manifest 的资产对齐

`tools/mjlab_assets.py` 提供资产导出桥，复用 mjlab 自身的
`apply_scene_manifest`、Gaussian 加载器和 `Scene` 碰撞构建流程。
它不维护第二份 manifest 解析器：版本、相对路径、SHA-256、依赖文件、
右手 Z-up 米制坐标和出生点选择规则均由 mjlab 校验。

在已安装 mjlab 及其依赖的环境中运行（输出目录必须尚不存在）：

```bash
cd "$MJLAB_REPO"
uv run python "$MUJOCO_REPO"/tools/mjlab_assets.py \
  src/mjlab/asset_zoo/3dgs/room1/manifest.json \
  /tmp/mjlab-room1-aligned
```

可以增加 `--spawn-point NAME` 选择 manifest 中的出生点。
导出内容：

- `scene.xml` 和 `assets/`：由 mjlab 构建并导出的静态碰撞场景，保留
  manifest 的碰撞分组，应用 `world_from_scene`，不额外添加平面。
- `visual.ply`：由 mjlab 标准化数据生成的 binary little-endian Gaussian PLY；
  位置、旋转和尺度已经转换到世界坐标。
- `alignment.json`：记录来源、源文件哈希、碰撞分组和所选出生点的世界位姿。
  这是导出元数据，不是可替代原 manifest 的场景配置。

查看对齐结果：

```bash
cd "$MUJOCO_REPO"
./build/bin/simulate /tmp/mjlab-room1-aligned/scene.xml \
  --gs-ply /tmp/mjlab-room1-aligned/visual.ply --gs-no-sh
```

**不要再传 `--gs-scale`、`--gs-translate` 或 `--gs-rotate`，否则会重复变换。**
碰撞分组默认是 3，可在 simulate 的可视化选项中切换其显示；显示状态不会
关闭物理碰撞。导出场景没有机器人，出生点只记录在元数据中，不会自动生成
机器人或执行策略。

当前桥接使用 mjlab 已解码的 RGBA，与其 Viser 数据语义一致，不保留原始高阶 SH；
透明度 0/1 在导出有限 logit 时夹到 float32 epsilon / (1-epsilon)。
`write_gaussian_ply(data, path, asset_id=...)` 也可供上层代码直接导出已过滤的
`GaussianSplatData`，保留其裁剪和资产选择结果。

资产适配测试（不要求安装 MuJoCo，也不需要显示器）：

```bash
MJLAB_SOURCE="$MJLAB_REPO" \
  python3 -m unittest discover -s tools/tests -v
```

`MJLAB_SOURCE` 用于对照本地 mjlab 的原始 Gaussian 加载器进行往返验证。
完整 manifest/碰撞导出必须在 mjlab 环境验证；脚本会先编译场景，并在导出后
重新加载 `scene.xml`，检查外部资源是否可用。
Python passive viewer 的运行时资产加载、策略播放和 CPU sim2sim 尚需后续开发。

## 14. Python passive viewer 接入入口（源码接口已实现）

本分支的 `mujoco.viewer.launch_passive` 新增可选参数：

```python
with mujoco.viewer.launch_passive(
    model, data, gaussian_ply="/tmp/mjlab-room1-aligned/visual.ply"
) as viewer:
    while viewer.is_running():
        # 由上层执行策略/物理，并更新 data。
        viewer.sync()
```

`model` 必须已经包含与 PLY 对齐的碰撞体。PLY 坐标已经是世界坐标，
viewer 不再应用 manifest 变换。文件在调用线程校验，GL 资源在渲染线程创建。
无 3DGS 的构建会明确拒绝此参数；不传参数保持原接口行为。
当前入口限定 Linux + classic OpenGL 4.6 compatibility profile。
GL 初始化失败仍采用现有行为：终端报告错误，继续显示普通 MuJoCo 场景。

`tools/mjlab_assets.py` 同时提供上下文管理器 `launch_mjlab_viewer`，
可在上层 Python 中导入后使用：

```python
with launch_mjlab_viewer(
    env.unwrapped.sim.mj_model,
    env.unwrapped.sim.mj_data,
    env.unwrapped.scene.gaussian_splat_data,
    env_idx=0,
) as viewer:
    # 在此复用 mjlab native viewer 的 GPU→CPU 状态同步及 viewer.sync()。
    # 该管理器本身不执行策略或物理步进。
    ...
```

该管理器按 `group_id[env_idx]` 选择资产，临时 PLY 保留到窗口上下文退出。
当前资产选择在窗口存续期间固定；动态切换资产仍需后续接口。
它只传递视觉资产，碰撞与出生点仍由 mjlab 初始化，避免二次导入。

3DGS 配置与资源状态按渲染线程隔离，关闭时复位。对应无显示器测试：

```bash
cmake -S . -B build
cmake --build build --target simulate mjgs_asset_test mjgs_lifecycle_test -j4
ctest --test-dir build -R '^mjgs_(asset|lifecycle)_test$' --output-on-failure
```

注意：以上是新增的源码 API，必须构建本分支的 Python 扩展才能使用；
仅重新编译独立 `simulate` 不会更新环境中的 Python 包。
Python wheel 的源码/依赖打包、shader 嵌入和实际策略播放现已验证，见第 16 节。

## 15. C++ simulate 直接加载 mjlab manifest

独立 C++ `simulate` 现在可以直接读取 mjlab manifest v1，无需先运行 Python
资产导出桥。新增编译依赖：

```bash
sudo apt install nlohmann-json3-dev libssl-dev
cmake -S . -B build
cmake --build build --target simulate mjgs_asset_test mjgs_lifecycle_test -j4
```

以 mjlab 的 room1 为例，仅加载场景外观和碰撞体：

```bash
./build/bin/simulate \
  --scene-manifest "$MJLAB_REPO"/src/mjlab/asset_zoo/3dgs/room1/manifest.json
```

也可以保留机器人 XML，再附加 manifest 中的碰撞场景：

```bash
./build/bin/simulate /path/to/robot.xml \
  --scene-manifest "$MJLAB_REPO"/src/mjlab/asset_zoo/3dgs/room1/manifest.json \
  --scene-spawn-point default
```

原有 XML + PLY 加载方式保持可用：

```bash
./build/bin/simulate model/gaussian/viewer_demo.xml \
  --gs-ply third_party/splatapult/data/test.ply
```

manifest 加载规则：

- 校验 schema v1、右手 Z-up 米制坐标、资产类型、必需字段和未知字段。
- 相对于 manifest 目录解析资产路径，拒绝越界路径，并校验 PLY、碰撞 MJCF
  及声明依赖文件的 SHA-256；核对碰撞 XML 的文件引用与依赖列表。
- 与 mjlab 碰撞接口一致，拒绝 include、关节、actuator、sensor、mocap
  和没有启用碰撞的场景。
- `world_from_scene` 的位置及 wxyz 四元数同时作用于 PLY 和碰撞场景。
- 碰撞 geom 使用 manifest 指定分组（默认 3）；附加对象加 `manifest/` 前缀，
  避免与基础机器人模型重名。
- 机器人 XML 中已有的地面和碰撞体会保留；请使用不包含重复场景的机器人 XML。
- 出生点默认选择 `default`，或唯一出生点；多个出生点没有默认值时必须指定名称。
  **当前只校验并输出出生点的世界位姿，不自动修改机器人初态。**
- manifest 不可与 `--gs-ply/scale/translate/rotate` 混用；仍可使用 `--gs-no-sh`、
  截图参数以及 F8 显示开关。
- PLY 仍须符合第 9 节的 C++ 加载格式；manifest 加载不会自动转换其他 PLY 编码。

无窗口检查（不要求 DISPLAY，也不创建 OpenGL context）：

```bash
./build/bin/simulate \
  --scene-manifest "$MJLAB_REPO"/src/mjlab/asset_zoo/3dgs/room1/manifest.json \
  --scene-check
```

检查会编译模型，输出出生点、几何体名称、分组和初态世界位置；失败返回非零状态。
它验证加载和编译，不验证 GPU 渲染。

测试脚本仅依赖 Python 标准库：

```bash
python3 tools/test_simulate_manifest.py
# 有图形会话时，额外验证 room1 和原 XML + PLY 的渲染截图：
python3 tools/test_simulate_manifest.py --gpu
# 可指定其他构建产物及真实 manifest：
python3 tools/test_simulate_manifest.py \
  --simulate ./build/bin/simulate --manifest /path/to/manifest.json
```

脚本覆盖真实 manifest 加载、非零四元数变换、碰撞分组、出生点世界坐标、
机器人 XML 合并，以及错误版本、哈希、路径、位姿、分组、出生点、动态碰撞和
参数冲突的拒绝行为。GPU 测试通过临时 PPM 截图验证加载，结束后清理临时文件。
独立 C++ manifest 入口不会代替 mjlab Python play 的场景构建流程。

## 16. 构建 3DGS Python wheel 并接入 mjlab native

Python 包使用版本 `3.16.0+3dgs`，底层 MuJoCo 引擎版本仍是 3.16.0。
适配后的 shader 已嵌入 C++ 扩展，安装后不依赖源码或构建目录；sdist 包含
Splatapult 源码、shader 和许可证。Linux wheel 通过 auditwheel 打包 GLEW。
OpenGL/GLX 驱动及其 dispatch 库由宿主系统提供，不随 wheel 携带；SDL2、GLM、
Eigen 等仍需在编译时安装。

### 16.1 准备构建环境

以下命令在本仓库根目录执行，先完成第 2、3 节的系统依赖安装和子模块初始化。
需要已安装 `uv`；也可以用目标环境的 `python -m pip install` 安装下列 Python 依赖。
当前脚本使用 `tomllib` 读取版本，要求构建解释器为 **Python 3.11 或以上**。
wheel 与 Python 小版本和平台绑定：Python 3.13 对应 `cp313`，3.12 对应 `cp312`，
不能交叉安装。先确认最终运行 mjlab 的解释器版本：

```bash
cd "$MUJOCO_REPO"
"$MJLAB_REPO/.env/bin/python" --version
```

下面两种环境任选一种。后续命令通过 `MJGS_PYTHON` 使用选中的解释器；请在同一
终端依次执行。如果换了终端，需要重新设置该变量。

**方式 A：使用已有 mjlab 环境。** 会在该环境中安装构建工具；生成 wheel 本身
不会替换环境中已安装的 MuJoCo，第 16.3 节才执行安装。

```bash
export MJGS_PYTHON="$MJLAB_REPO/.env/bin/python"
uv pip install --python "$MJGS_PYTHON" \
  build setuptools wheel pybind11 absl-py auditwheel patchelf
```

**方式 B：创建独立打包环境。** 下面以 Python 3.13 为例；构建 3.12 包时，将环境
创建命令中的 `3.13` 和目录名中的 `313` 分别换为 `3.12`、`312`，并使用对应解释器。
打包环境放在仓库外，避免源码包收集时混入环境文件；`/tmp` 中的环境可能被系统清理。

```bash
uv env "$HOME/.envs/mjgs-wheel-env313" --python 3.13
export MJGS_PYTHON="$HOME/.envs/mjgs-wheel-env313/bin/python"
uv pip install --python "$MJGS_PYTHON" \
  build setuptools wheel pybind11 absl-py auditwheel patchelf
```

构建前检查解释器和工具是否可用：

```bash
"$MJGS_PYTHON" --version
"$MJGS_PYTHON" -c 'import tomllib, build, setuptools, absl; print("Python build dependencies OK")'
"$MJGS_PYTHON" -m build --version
"$MJGS_PYTHON" -m auditwheel --version
patchelf --version
```

若 `patchelf` 不在当前 PATH，可使用 `"$(dirname "$MJGS_PYTHON")/patchelf" --version`
检查。打包脚本会把解释器所在目录加入 PATH。脚本设置了
`MUJOCO_SKIP_BUILD_DEPS=1` 并使用 `--no-isolation`，因此这些依赖必须提前装好。

### 16.2 编译原生库并生成 wheel

整个流程沿用原来的 **原生库 → sdist → wheel**，最后增加 Linux 动态依赖修复。
原生 CMake 构建和 Python 扩展构建是两个阶段；编译好 `build/bin/simulate` 不会
更新 Python 包中的 `_simulate`。

**第一步：编译原生库。** 以下统一使用 `build-wheel/`。它与打包环境目录是不同
目录，不要混用。若已经按第 4 节完整构建了 `build/`，可以跳过此步，在下一步
将 `MJGS_BUILD_DIR` 指向 `"$PWD/build"`。若只编译过部分 target，应先完成全量构建，
避免安装时缺少其他配置启用的产物。

```bash
cmake -S . -B build-wheel \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc-11 \
  -DCMAKE_CXX_COMPILER=g++-11 \
  -DMUJOCO_BUILD_SIMULATE=ON \
  -DMUJOCO_ENABLE_3DGS=ON \
  -DMUJOCO_BUILD_TESTS=OFF \
  -DMUJOCO_BUILD_3DGS_TESTS=OFF \
  -DMUJOCO_TEST_PYTHON_UTIL=OFF \
  -DMUJOCO_USE_FILAMENT=OFF \
  -DMUJOCO_USE_FILAMENT_MJR_COMPAT=OFF


cmake --build build-wheel -j4
```

**第二步：生成源码包和 wheel。** 确保第 16.1 节的 `MJGS_PYTHON` 已 export：

```bash
MJGS_BUILD_DIR="$PWD/build-wheel" \
CMAKE_BUILD_PARALLEL_LEVEL=4 \
MUJOCO_CMAKE_ARGS='-DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11' \
bash tools/build_3dgs_wheel.sh
```

如果使用已有的 `build/` 和 mjlab 环境，也可用一条完整命令指定所有参数：

```bash
MJGS_PYTHON="$MJLAB_REPO/.env/bin/python" \
MJGS_BUILD_DIR="$PWD/build" \
CMAKE_BUILD_PARALLEL_LEVEL=4 \
MUJOCO_CMAKE_ARGS='-DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11' \
bash tools/build_3dgs_wheel.sh
```

每行末尾的 `\` 后不能有空格。多行赋值若不使用续行或 `export`，变量不会传给
脚本，脚本可能退回使用系统的 `python3`。不要在 `build/` 或 `python/` 子目录中
直接照抄这些命令，否则 `$PWD/build` 和脚本相对路径会指向错误位置。

脚本实际执行顺序：

1. 将 `MJGS_BUILD_DIR` 的原生构建产物安装到临时工作目录的 `runtime/`。
2. 调用 `python/make_sdist.sh`，生成绑定代码并收集 simulate、Splatapult 源码、
   shader、CMake 脚本和许可证，生成 `python/dist/mujoco-<版本>.tar.gz`。
3. 解压 sdist，通过选中的 Python 执行 `python -m build --wheel --no-isolation`，
   启用 3DGS，重新编译 Python 扩展并收集 MuJoCo 动态库和头文件。
4. 执行 `auditwheel repair`，收集 GLEW 等非驱动动态依赖，将最终 wheel 写入根目录 `dist/`。
   OpenGL/GLX dispatch 库由宿主系统提供。

这里仍然使用原来的 sdist 构建机制，手动用 `pip wheel` 构建同一 sdist 也可以，
但仍须设置原生库路径、3DGS CMake 参数并处理动态依赖。不能只运行
`make_sdist.sh` 就把生成的 tar.gz 当作已编译 wheel。

可调整的参数：

| 变量 | 当前行为 |
| --- | --- |
| `MJGS_PYTHON` | 构建解释器；默认 `python3`，建议明确指定绝对路径 |
| `MJGS_BUILD_DIR` | 已完成的原生构建目录；默认仓库下 `build/` |
| `CMAKE_BUILD_PARALLEL_LEVEL` | Python 扩展构建并发数，默认 4；内存不足时调低 |
| `MUJOCO_CMAKE_ARGS` | 传给 Python 扩展构建的额外 CMake 参数，不会重新配置原生构建目录 |
| `MJGS_STAGE_DIR` | 临时工作目录；默认 `/tmp/mjgs-wheel.XXXXXX`，建议每次使用新目录 |
| `MJGS_WHEEL_PLATFORM` | 默认 `manylinux_2_35_x86_64` |

当前脚本针对 Linux x86_64，默认产物要求 glibc >= 2.35；修改平台标签不会降低
实际编译产物的系统要求。可用 `uname -m`、`ldd --version` 检查本机平台。
脚本当前使用空插件目录，**不会携带标准 MuJoCo 插件动态库**；需要插件的任务
应先调整脚本中的 `MUJOCO_PLUGIN_PATH` 指向实际插件目录。

CMake 的 Abseil、Eigen、GLFW 等依赖首次构建可能需要下载，可以通过
`MUJOCO_CMAKE_ARGS` 的 `FETCHCONTENT_SOURCE_DIR_*` 指向已有源码缓存。
脚本成功后打印 `Wheel directory` 和 `Build stage`；临时目录保留，便于排错。
以 Python 3.13 为例，最终产物为：

- `dist/mujoco-3.16.0+3dgs-cp313-cp313-manylinux_2_35_x86_64.whl`：修复后的可安装包。
- `python/dist/mujoco-3.16.0+3dgs.tar.gz`：源码包。

Python 3.12 的 wheel 标签相应为 `cp312`。安装前核对实际文件名：

```bash
ls -lh dist/*.whl python/dist/*.tar.gz
```

**构建失败时按首个错误排查：**

| 报错或现象 | 原因与处理 |
| --- | --- |
| 指定的 Python `No such file or directory` | 环境不存在或被清理；重新创建环境，或改为已有解释器的绝对路径 |
| `No module named tomllib` | 实际解释器低于 3.11；检查 `MJGS_PYTHON` 是否正确传入及其版本 |
| `No module named build` / `build.__main__` | 在选中的解释器环境安装上面的构建依赖，并用 `python -m build --version` 验证 |
| `tar ... Not found` | 先检查前面 sdist 生成是否失败，不要直接使用旧的 tar.gz 继续构建 |
| CMake 安装提示文件缺失 | 确认 `MJGS_BUILD_DIR` 正确，先完成该目录的 `cmake --build` |
| 缺少 GLEW、SDL2、JSON 或 OpenSSL | 补齐第 2 节系统开发包，然后重试 |
| 编译被 `Killed` | 可能内存不足；降低原生构建和 Python 扩展构建的并发数 |
| auditwheel 提示平台不兼容 | 检查实际依赖的 glibc/符号版本，使用匹配目标平台的构建环境 |

`make_sdist.sh` 当前未启用遇错立即退出，前面的失败可能伴随后续 tar 报错；
应从日志中最早的失败位置处理。补齐 Python 工具后可以重跑打包脚本，
无需仅因缺少 `build` 就重新编译原生 MuJoCo。

### 16.3 安装到 mjlab 并验证

安装到 mjlab 所使用的环境。`--no-deps` 保留已经准备好的训练依赖，仅替换 MuJoCo：

```bash
uv pip install --python "$MJLAB_REPO/.env/bin/python" \
  --reinstall --no-deps \
  ./dist/mujoco-3.16.0+3dgs-cp313-cp313-manylinux_2_35_x86_64.whl

"$MJLAB_REPO/.env/bin/python" -c \
  'from importlib.metadata import version; import mujoco.gaussian; print(version("mujoco"))'
```

应输出 `3.16.0+3dgs`，且能导入 `mujoco.gaussian`。版本查询使用包元数据；
底层引擎的版本仍是 `3.16.0`。安装包用于已有 mjlab 环境；新环境应先安装 mjlab
及其运行依赖。

mjlab native viewer 已增加资产桥接：由 mjlab 解析 manifest、构建碰撞模型，
将标准化 Gaussian 数据交给 `mujoco.gaussian.GaussianAssets`，在临时 PLY 中
缓存当前资产。切换到使用其他资产的环境时，通过 `Handle.load_gaussian_ply()`
提交渲染线程更新。窗口关闭后清理缓存，已有 GPU→CPU 状态同步和扰动力流程继续使用。
没有 Gaussian 场景时不加载资产桥，可继续使用普通 MuJoCo native viewer。

在 mjlab 环境运行默认 demo 的 3DGS native 版本：

```bash
cd "$MJLAB_REPO"
uv run --no-sync demo --use-3dgs True --viewer native --num-envs 1 \
  --scene-manifest src/mjlab/asset_zoo/3dgs/room1/manifest.json
```

**手工安装自定义 wheel 后使用 `--no-sync`，避免 uv 按原锁文件换回官方包。**
使用独立环境时可以设置 `UV_PROJECT_ENVIRONMENT`，并通过 `uv run --python`
指定该环境的 Python。demo 使用其默认 G1 tracking checkpoint 和 motion，
复用 mjlab 的下载缓存及 SHA-256 校验。

验证已安装 wheel（从其他工作目录运行也应成功）：

```bash
"$MJLAB_REPO/.env/bin/python" \
  "$MUJOCO_REPO"/tools/test_python_3dgs.py --gpu
```

`--gpu` 会打开 native viewer，需要可用的桌面显示及 OpenGL 4.6 compatibility
上下文；去掉此参数可只验证资产序列化、缓存和清理。

该脚本检查序列化、资产缓存、碰撞几何隐藏时的渲染、运行时切换、窗口重开和清理。
`Handle.gaussian_status` 返回 `empty/pending/ready/error: ...` 等状态，便于集成测试
发现 GL 初始化失败，避免只依赖终端日志。

有限步真实策略播放测试：

```bash
cd "$MJLAB_REPO"
uv run --no-sync python "$MUJOCO_REPO"/tools/smoke_mjlab_native.py --steps 100
```

测试调用实际 demo CLI，固定 `--viewer native` 和 room1 manifest，完成指定数量的
策略步进后自动关闭，并检查 3DGS 渲染器是否就绪；不修改策略、物理步进或碰撞模型。

### 使用已有 Unitree G1 velocity 策略

指定训练目录时，测试脚本按数字迭代号选择最新 `model_<iteration>.pt`。
此前验证所用目录的最新文件为 `model_10000.pt`，网络输入 98 维、动作输出 29 维。
该策略包含 phase 观测，应加载原训练仓库的 `Unitree-G1-Flat` 配置：

```bash
cd "$MJLAB_REPO"
uv run --no-sync python "$MUJOCO_REPO"/tools/smoke_mjlab_native.py \
  --checkpoint-dir "$CHECKPOINT_DIR" \
  --task-source "$UNITREE_TASK_REPO" \
  --task-id Unitree-G1-Flat --steps 100
```

也可以通过 `--checkpoint /path/to/model_10000.pt` 显式指定文件。
脚本读取原训练任务源码，不改写训练仓库或 checkpoint；room1 manifest 由当前
mjlab 的 play 流程应用。原训练工程依赖 mjlab 1.2.0，而此处目标 mjlab 为 1.6.0，
任务 API、观测和 checkpoint 的兼容性需要实际播放验证；不能仅凭网络维度一致认定迁移成功。

### 2026-10-29 实测结果（已通过）

在当时的 mjlab 虚拟环境中验证：Python 3.13.17、Torch 2.14.0、
mjlab 1.6.0、MuJoCo/Warp 3.16.0，安装的自定义 Python 包版本为 `3.16.0+3dgs`。
对应 wheel：`dist/mujoco-3.16.0+3dgs-cp313-cp313-manylinux_2_35_x86_64.whl`。

- checkpoint：`2026-08-25_12-42-10/model_10000.pt`。
- task：原训练代码的 `Unitree-G1-Flat`；actor 98 维、critic 113 维、动作 29 维。
- scene：room1 原始 manifest；成功加载 799,083 个 Gaussian。
- native viewer 渲染状态 `ready`，完成 100 个真实策略步并正常关闭。
- 8 项 native 桥接、动作和 G1 场景配置测试通过；修改文件的 Ruff、Ty 检查通过。

完整日志：`build/validation/room1-native-model10000.log`。
这验证了策略加载、步进、资产渲染及窗口生命周期；100 步测试不代表长期运动稳定性评估。
仿真仍由 mjlab 的 MuJoCo Warp 执行，native viewer 同步显示其状态。

为适配原训练工程使用的 mjlab 1.2 API，`tools/unitree_g1_compat.py` 仅加载所需的
G1 velocity 任务，在进程内适配旧资产加载接口和碰撞默认值；原训练源码及 checkpoint
保持原样。当前 mjlab 的任务公共模块也补齐了既有注册代码引用的 3DGS 场景函数和常量。

重复有限步测试：

```bash
cd "$MJLAB_REPO"
uv run --no-sync python "$MUJOCO_REPO"/tools/smoke_mjlab_native.py \
  --checkpoint-dir "$CHECKPOINT_DIR" \
  --task-source "$UNITREE_TASK_REPO" \
  --task-id Unitree-G1-Flat --steps 100
```

正常交互播放（窗口由用户关闭）：

```bash
cd "$MJLAB_REPO"
uv run --no-sync python "$MUJOCO_REPO"/tools/play_unitree_native.py \
  --task-source "$UNITREE_TASK_REPO" \
  Unitree-G1-Flat \
  --checkpoint-file "$CHECKPOINT_DIR/model_10000.pt" \
  --viewer native --num-envs 1 \
  --scene-manifest "$MJLAB_REPO"/src/mjlab/asset_zoo/3dgs/room1/manifest.json
```

若终端加载了 ROS 的 Python 路径或其他自定义动态库路径，测试时可在上述命令前使用
`env -u PYTHONPATH -u LD_LIBRARY_PATH`；运行 pytest 时还可设置
`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1`，避免 ROS pytest 插件干扰。

## 17. 使用 mjlab demo ONNX 做独立 CPU sim2sim

`tools/export_mjlab_demo_cpu.py` 将 mjlab 默认 tracking demo 的 checkpoint 导出为
ONNX，并打包 MuJoCo 模型、参考动作和接口配置；`tools/play_mjlab_cpu.py` 使用
**MuJoCo CPU `mj_step` + ONNX Runtime CPU** 独立运行。播放时不导入 mjlab、Torch
或 Warp；native 窗口由本仓库 Python 包中的 simulate 渲染。

目前支持 demo 对应的 `Mjlab-Tracking-Flat-Unitree-G1`，不是任意 ONNX 策略的
通用播放器。该策略输入 160 维、输出 29 维，ONNX 内已包含训练观测归一化。

### 17.1 导出测试包

先完成第 16 节自定义 wheel 的安装。导出环境还需要 `onnx`、`onnxruntime`；
若缺少，可用 `uv pip install --python .env/bin/python onnx onnxruntime` 安装。
在已准备好的 mjlab 环境执行：

```bash
cd "$MJLAB_REPO"
uv run --no-sync python "$MUJOCO_REPO"/tools/export_mjlab_demo_cpu.py \
  "$MUJOCO_REPO"/build/demo-cpu-room1 \
  --scene-manifest src/mjlab/asset_zoo/3dgs/room1/manifest.json \
  --reference-steps 100
```

输出目录必须尚不存在。默认使用 CUDA 在 mjlab 中生成对照数据，官方 demo
checkpoint 和 motion 首次运行会下载并校验 SHA-256；已有文件可通过
`--checkpoint /path/demo_ckpt.pt --motion /path/motion.npz` 指定。
不传 `--scene-manifest` 时保留任务默认平面；manifest 的路径、变换、碰撞和可选
`--scene-spawn-point` 由 mjlab 的资产接口解析后写入导出结果。

为便于复现，测试关闭 startup/interval 随机事件与终止重置，使用起始帧采样及
零关节重置噪声。参考序列结束前停止，不做自动重采样；这些配置记录在
`config.json`。机器人执行器增益、阻尼、力矩限幅、armature、接触参数与场景碰撞
由编译模型保留，不在播放器中另写一套 PD 参数。

测试包内容：

| 文件 | 用途 |
| --- | --- |
| `policy.onnx` | 含归一化的策略及 mjlab exporter 附带的参考动作输出 |
| `scene.mjb` | 包含机器人、执行器、传感器和 manifest 碰撞的编译模型 |
| `visual.ply` | 变换后的 3DGS 资产；没有 Gaussian 场景时不生成 |
| `motion.npz` | 播放器构建 tracking 指令所需的参考关节和刚体姿态 |
| `initial.npz` | 与 mjlab 对照一致的初始仿真状态 |
| `config.json` | 关节/执行器映射、动作缩放、观测顺序与控制频率 |
| `reference.npz` | mjlab GPU 生成的状态、观测、策略动作和控制目标 |
| `warp-report.json` | 对照序列的根部高度与参考躯干位置误差 |

`scene.mjb` 要求使用导出时相同的 MuJoCo 引擎版本；升级后应重新导出。

### 17.2 无窗口验证和 native 播放

无窗口测试可在没有桌面显示的环境运行：

```bash
cd "$MJLAB_REPO"
uv run --no-sync python "$MUJOCO_REPO"/tools/play_mjlab_cpu.py \
  "$MUJOCO_REPO"/build/demo-cpu-room1 \
  --validate --steps 1000 --max-anchor-error 0.5 \
  --report "$MUJOCO_REPO"/build/demo-cpu-room1/cpu-report.json
```

`--validate` 将同一组 GPU 状态放入 CPU 模型，比较观测、ONNX 动作和控制目标；
最大绝对误差上限分别为 `2e-4`、`2e-3`、`2e-3`，超限返回失败。
独立 rollout 的物理步长为 0.005 秒、decimation 为 4，即每 0.02 秒推理一次。
`--max-anchor-error 0.5` 要求参考躯干位置误差不超过 0.5 米；不传时仅检查有限数值
并报告误差，不能据此判断动作跟踪成功。

打开 native 3DGS 窗口：

```bash
uv run --no-sync python "$MUJOCO_REPO"/tools/play_mjlab_cpu.py \
  "$MUJOCO_REPO"/build/demo-cpu-room1 \
  --viewer native --steps 1000 --max-anchor-error 0.5 \
  --report "$MUJOCO_REPO"/build/demo-cpu-room1/native-report.json
```

播放 1000 个控制步后自动退出。运行时只需要自定义 MuJoCo 包、NumPy 和
ONNX Runtime；可在另一个装有这些依赖的 Python 环境直接运行同一脚本与测试包。
native 模式仍需要 OpenGL 4.6 compatibility 和桌面显示。

### 17.3 验证范围

2026-10-29 使用 room1 和官方 demo checkpoint 实测：100 组状态的观测最大绝对
误差 `3.64e-7`，ONNX 动作误差 `2.86e-6`，控制目标误差 `7.84e-7`。
CPU 无窗口和 native 两种模式均完成 1000 步（20 秒），平均参考躯干位置误差
约 0.071 米、最大约 0.191 米；native 加载 799,083 个 Gaussian，状态为 `ready`。

进一步的完整序列验证结果：

| 场景与检查 | 结果 |
| --- | --- |
| 默认平面，CPU 独立 6572 步 / 131.44 秒 | 通过 0.5 米阈值；平均误差 0.068 米，最大 0.260 米 |
| room1，完整 6572 组相同状态的接口对比 | 通过；观测/动作最大误差均约 `5.72e-6`，控制目标约 `3.13e-6` |
| room1，CPU 完整独立序列 | 未通过；发生跌倒，最大误差约 3.93 米 |
| room1，mjlab Warp 完整对照序列 | 同样未通过；最大误差约 3.36 米 |

room1 的 CPU 与 GPU 均约在第 1605 步首次超过 0.5 米误差。默认平面成功、
room1 两端均失败，说明场景变更后的动作可执行性需要单独验证，不能只按接口
对齐来保证；具体导致失稳的接触尚未定位。当前已验证独立 CPU sim2sim 链路，
**不能宣称该 demo 策略在 room1 的完整动作序列稳定通过**。

复现默认平面的完整序列测试（输出目录需尚不存在）：

```bash
cd "$MJLAB_REPO"
uv run --no-sync python "$MUJOCO_REPO"/tools/export_mjlab_demo_cpu.py \
  "$MUJOCO_REPO"/build/demo-cpu-plane --reference-steps 100
uv run --no-sync python "$MUJOCO_REPO"/tools/play_mjlab_cpu.py \
  "$MUJOCO_REPO"/build/demo-cpu-plane \
  --validate --steps 6572 --max-anchor-error 0.5 \
  --report "$MUJOCO_REPO"/build/demo-cpu-plane/cpu-report.json
```

完整 room1 对照数据的导出参数为 `--reference-steps 6572`，本机测试包位于
`build/demo-cpu-room1-full/`，其中 `warp-report.json` 和 `cpu-report.json` 保留
完整结果；这些大型测试产物位于忽略的 `build/` 目录，不随源码提交。

## 18. TODO

### 大规模 PLY 的渲染资源控制（尚未实现）

当前会加载全部 Gaussian，并按窗口视口尺寸渲染。点数过多时可能占用较多显存，
增加排序和绘制开销，进而影响仿真播放流畅度。计划增加以下功能；参数名称为
拟议接口，当前版本不能使用：

- [ ] 点数上限：`--gs-max-points 300000`，加载时进行确定性降采样，在上传 GPU
  前限制点数，减少显存、排序和绘制开销；输出原始和保留点数。
- [ ] 渲染分辨率比例：`--gs-render-scale 0.5`，降低 3DGS 内部渲染分辨率，正确
  处理与机器人画面的深度遮挡及合成。该选项主要减少像素开销，不替代点数限制。
- [ ] 显示刷新率上限：`--gs-max-fps 30`，限制渲染刷新频率，减少资源争用，
  保持物理步进与策略控制频率独立。
- [ ] 将上述选项统一提供给 C++ simulate、Python viewer 和 mjlab native 资产桥，
  支持 XML + PLY 与 manifest 两种资产入口。
- [ ] 使用不同点数和分辨率测试显存占用、帧率、策略步进耗时及画质，验证默认
  不降采样时的兼容性，以及运行时切换资产后的资源释放。

这些优化只作用于视觉资产和渲染，不修改 manifest 碰撞模型、物理步长或策略
控制参数。点数限制应优先实现；降采样和低分辨率会损失视觉细节，需要实测取舍。
