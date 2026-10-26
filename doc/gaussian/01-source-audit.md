# 本地源码审计

> 更新：用户已于 2026-10-20 授权实施，首版现状与实测结果见 [实现记录](05-implementation.md)。下文保留原始设计/审计语境；“待授权”“拟议”不代表当前停止实施。

审计日期：2026-10-19。以下结论来自本地检索，不代表已经完成集成、运行或性能测试。

## 基线

| 工程 | 本地证据 | 当前状态 |
|---|---|---|
| MuJoCo | `CMakeLists.txt` 的版本 3.16.0；`include/mujoco/mujoco.h` 的 `mjVERSION_HEADER=3016000` | HEAD `a89275051`，审计时无工作区修改 |
| Splatapult | HEAD `e87960f` | 已有本地修改，不得覆盖 |

Splatapult 审计时：跟踪的 `CMakeLists.txt` 被删除，另有未跟踪的 `splatapult_CMakeLists.txt`；`src/app.cpp`、`src/app.h` 已修改。替代 CMake 中 `ENABLE_OPENXR` 默认 OFF。原始 vcpkg 清单仍声明 openxr-loader/tracy。因此不能直接声称原工程可按标准 CMake 路径重建，也不能自动恢复这些文件。

## MuJoCo 入口

- `simulate/simulate.cc`：`Simulate::Render()` 在 `mjr_render(rect, &scn, &context)` 后绘制提示、UI、figures/images，末尾交换缓冲。拟在场景绘制之后、UI 之前加入高斯合成；覆盖层顺序需整体梳理。
- `simulate/simulate.h`：viewer 状态与生命周期；未来持有集成控制器的候选位置。
- `simulate/glfw_adapter.cc`：GLFW 上下文创建，当前可见代码设置 MSAA=4；不能据此推断已满足 OpenGL 4.6。
- `simulate/CMakeLists.txt`：已有 platform_ui_adapter、simulate 目标，后续只在需要的位置添加目标依赖。
- `src/render/classic/`：classic OpenGL 绘制与 framebuffer；需要继续核对实际深度约定、清屏和投影行为。
- `include/mujoco/mjrender.h`：readDepthMap 包含 ZERONEAR/ZEROFAR。该设置用于 readPixels，不能直接作为硬件 depth buffer 约定的证据。

## Splatapult 可复用部分

- `src/gaussiancloud.h/.cpp`：`GaussianCloud::ImportPly()` 与高斯/SH 属性。
- `src/ply.h/.cpp`：PLY 解析；第一版必须报告不支持的字段和格式。
- `src/splatrenderer.h/.cpp`：`Init()`、`Sort(cameraMat, projMat, viewport, nearFar)`、`Render(...)`。
- `src/core/program.cpp`：桌面 shader 注入 `#version 460`，意味着需要验证 OpenGL 4.6 及相应 shader 功能。
- `shader/splat_frag.glsl`：输出预乘 alpha 的 RGB；低 alpha discard。复用时要保持混合方程相符。
- `src/app.cpp`：独立应用初始化使用 `GL_ONE, GL_ONE_MINUS_SRC_ALPHA`；不要假定 SplatRenderer 自己会建立所有 GL 状态。
- `src/core/util.cpp`：资源路径依赖 GetRootPath；集成后应使用显式资源目录，不依赖启动 cwd。
- `src/flycam.cpp`：是原 Splatapult 相机控制，不应取代 MuJoCo viewer 相机。

## 仍需验证

实际 GPU/驱动能力；compatibility context 与 shader 460 的组合；MuJoCo 实际投影和 stereo 路径；depth 的方向、范围和 MSAA resolve；Splatapult Sort 顺序与 GL 状态副作用；原点变换对高阶 SH 的影响；窗口缩放/HiDPI 与资源释放线程。

版本、源码位置、状态改变时更新本文件，不能依赖旧机器或旧目录的结论。
