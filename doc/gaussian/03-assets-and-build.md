# 资产、坐标与构建方案

> 更新：用户已于 2026-10-20 授权实施，首版现状与实测结果见 [实现记录](05-implementation.md)。下文保留原始设计/审计语境；“待授权”“拟议”不代表当前停止实施。

## PLY 契约

优先标准 3DGS PLY，检查 positions、rot_0..3、scale_0..2、opacity、f_dc_0..2，并按实际文件判断 SH 阶数。位置 float32、尺度 exp(log-scale)、opacity sigmoid、四元数 wxyz 归一化。不能把普通 XYZRGB 点云当高斯。检查空文件、缺字段、非有限值、零四元数、非正尺度和明显溢出；报告错误并保持旧场景。原始 PLY 永不自动覆写。

高斯文件大时先完整保真，再评估 opacity/ROI/LOD 筛选；不能无提示随机降采样。预算同时统计 CPU/GPU 数据、排序临时空间和上传峰值。不要承诺未实测的点数/FPS。

## 场景坐标

统一目标：MuJoCo world、米、右手系、Z-up。LCC 等导出说明仅作参考，必须用已知地面和长度核对实际资产。窗口角落坐标轴的屏幕倾斜不证明地面错位。

定义 `world_T_gs` 将高斯坐标映射到 MuJoCo world，避免变量名与变换方向相反：

```
p_world = s R p_gs + t，s > 0
Σ_world = s² R Σ_gs Rᵀ
q_world = q_R ⊗ q_gs
```

第一版只支持正的统一缩放和刚体旋转平移。仅改 XYZ 会使各向异性高斯错误。推荐保留高斯局部资产，通过相机变换/模型变换渲染；确保投影协方差和 SH 观察方向均在正确局部坐标中。若离线旋转资产，须同步方向与高阶 SH；不只保留旧 SH 系数后宣称外观不变。

设置新原点 c 与绕 c 旋转有区别：`R(p-c)` 与 `R(p-c)+c`。地面对齐应从实测平面法线求 R，不默认旋转 90°。碰撞代理和视觉场景采用同一变换，不隐式改变机器人世界。

## 构建边界（拟议）

目标 Linux + NVIDIA/支持所需 OpenGL 的桌面 GPU；首版不引入 CUDA/Torch/gsplat/OpenXR。MuJoCo 与高斯共同使用 MuJoCo 的 GLFW 窗口，不链接 Splatapult SDL app 主程序。

复用候选：GaussianCloud、Ply、SplatRenderer 及确需的 program/buffer/util；逐项检查链接闭包，排除 flycam、SDL main、OpenXR、Tracy 和独立 UI。SDL2/libpng/JSON 是否仍被 util 等间接引用，应以抽取后的源码验证，不能提前保证移除。

| 依赖 | 集成角色 |
|---|---|
| MuJoCo / GLFW | 物理、原生 viewer、窗口与交互 |
| OpenGL 4.6 + 函数加载器 | shader/compute 与 classic renderer；不能强制 core profile 破坏 legacy 绘制 |
| GLM / Eigen3 | 保留实际使用的数学实现 |
| JSON / libpng | 仅在相应 loader/资源仍使用时保留 |
| SDL2 / OpenXR / Tracy | 原独立 app 能力；不作为首版集成目标要求 |
| vcpkg | 可选依赖管理工具，不是运行时功能 |

MuJoCo 自带符号加载与 GLEW 的头文件/宏/链接冲突必须隔离在模块边界。资源路径基于安装/构建资源目录，支持从任意 cwd 启动。保留原版权与许可证声明。

## 待实施的配置设计

考虑 `MUJOCO_ENABLE_3DGS` 构建开关；运行时独立输入 MJCF 与 PLY，额外指定场景变换。上述名称仅为提案。不开高斯功能时应保持 MuJoCo 原行为。缺 OpenGL 特性时明确拒绝开启高斯并保留普通 viewer 能力。

当前不提供合并工程命令：标准名 Splatapult CMake 缺失，尚未创建集成构建目标。未来授权后先确定修复/适配方案，再写可验证命令。
