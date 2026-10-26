# 首版实现记录

日期：2026-10-20。用户已授权从文档进入实现。

## 已实现

- 顶层 CMake 统一构建，`MUJOCO_ENABLE_3DGS` 可关闭。
- 单独构建高斯库，不调用 Splatapult 独立程序或恢复其缺失的 CMakeLists.txt。
- 校验 PLY 后复用 GaussianCloud / SplatRenderer / GPU sort。
- 从最终 `mjvScene.camera` 获取单目平均相机，复现 classic renderer 的视锥；共享原生相机交互。
- 局部高斯资产通过相机映射应用统一缩放、XYZ 旋转和平移，保持高斯方向与 SH 局部观察方向。
- MuJoCo 几何先画，高斯后画，深度比较 GEQUAL、depth write 关闭、预乘 alpha 混合；不清屏。
- 高斯 shader 使用常规 clip 投影，再以 `gl_FragDepth=1-gl_FragCoord.z` 写入与 MuJoCo 反向深度一致的值。
- 原生 viewport 左下偏移、scissor，GL 固定状态及 program/VAO/相关 buffer bindings 保存恢复。
- F8 显隐、PPM 主视口截图及截图后正常退出。
- GL 生命周期与调用均在原 viewer 渲染线程，退出前释放。PLY 参数校验在启动时，GPU 上传同步进行。

## 原生薄接入与第三方边界

MuJoCo fork 在 `simulate/main.cc` 和 `simulate/simulate.cc` 中直接保留少量受 `MJGS_ENABLE_3DGS` 保护的 hook：启动配置、F8 显隐、场景绘制后的 3DGS 回调以及 OpenGL 上下文销毁前的清理。PLY、配准、相机转换、GL 状态和 Splatapult 生命周期全部位于 `integration/viewer_bridge.*`，MuJoCo 物理核心与数据结构不变。这样原生 executable 和后续链接 `mujoco::libmujoco_simulate` 的 Python `_simulate` 可以复用同一 viewer 渲染入口。

Splatapult 保持第三方源码边界。`integration/CMakeLists.txt` 将所需模块构建为 `mjgs_splat` 静态库，不接入其 app、窗口、FlyCam 或 VR 主循环。`cmake/IntegrateViewer.cmake` 只在 `build/generated` 生成少量 shader/source 兼容适配：保留 shader 460，修正 near-plane 过滤与反向深度，避免空视锥分派零大小 radix sort，并修正 viewport.x workaround。`replace_once` 会检查 Splatapult 适配锚点；第三方升级改变锚点时配置直接失败，要求人工审查。

## 实测

| 检查 | 结果 |
|---|---|
| GCC 9 编译上游 MuJoCo | 失败于上游 C++20 初始化语法，改用 GCC 11 |
| GCC 11 + Ninja 完整 simulate/测试构建 | 通过 |
| CPU 变换、有效 PLY、零四元数、截断 PLY | 通过 |
| 无窗口 CLI 帮助 | 通过 |
| NVIDIA OpenGL 能力 | RTX 4070 Laptop，驱动 GL 580.159.03，4.6 |
| 离屏 MSAA4 前后遮挡 | 通过：前方高斯采样 RGB 254,1,0，后方高斯位置保持实体 RGB 47,255,47 |
| 视口偏移/原生状态恢复/F8 显隐/空视锥 | 通过 |
| 实际 LCC PLY | 1,601,052 高斯成功加载；原生窗口截图并正常退出 |

构建验证目录为 `/tmp/mjgs-build11`，不依赖此临时路径作为正式安装。实际扫描截图 `/tmp/mjgs-real-scene.ppm`；示例物体和扫描尚未配准，最终修正 viewport.x 后，扫描房间与动态方块/桌面同视口正常显示，截图见 [实测画面](media/native-viewer-3dgs.png)。这不是完成场景配准或最终视觉质量验收的证明。

GLX 默认 NVIDIA 上下文 profile mask 可能为 0，但仍支持固定管线；因此拒绝明确的 core profile，而不强制 compatibility bit 必须为 1。最初隐藏窗口的默认 framebuffer 像素全黑；遮挡验收改用 MuJoCo 离屏 framebuffer，实际原生窗口另行截图验证。

## 暂未实现

- 运行时 PLY 替换、异步解析与上传进度、GUI 编辑/保存配准。
- 透明 MuJoCo geom 与高斯统一排序；stereo、正交相机、全局 mjvScene transform。
- 高斯碰撞、光照响应、影子、反射、拾取。
- 与 Python passive viewer、MJWarp/mjlab 的同步与批量训练集成。
- 任意安装前缀打包。当前资源指向 build/generated；构建目录需保留。

## 下一步

先用用户实际机器人与场景标定参数检查坐标和尺度，再决定是否补运行时加载与 GUI 配准。继续保留原生 viewer 控制，勿引入第二个相机控制器或物理循环。
