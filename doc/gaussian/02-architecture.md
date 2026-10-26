# 集成架构与渲染设计

> 更新：用户已于 2026-10-20 授权实施，首版现状与实测结果见 [实现记录](05-implementation.md)。下文保留原始设计/审计语境；“待授权”“拟议”不代表当前停止实施。

## 模块责任

保留两份上游源码边界，采用构建目标和小型适配层连接，不将两个 app 主循环拼在一起。

| 模块（拟议） | 职责 |
|---|---|
| GaussianAsset | PLY 校验、CPU 高斯数据与元信息 |
| GaussianRenderer | GPU buffer、排序、shader、高斯绘制；不创建窗口或推进物理 |
| GaussianScene | 可见性、场景相似变换、资产状态 |
| ViewerGaussianBridge | MuJoCo 实际相机/视口适配、FBO 与深度、状态恢复 |
| simulate UI | 文件选择、显隐、配准参数、错误提示 |

上述名字仅描述责任，最终接口在实施时确定。

## 物理与渲染线程

物理仍由原 simulate 管理；不能添加第二次 mj_step。所有 GL 资源创建、更新、绘制、释放均在拥有 viewer 上下文的线程完成。PLY 可在后台解析，完成后提交待上传请求；上传、场景替换和销毁在帧边界执行。不要在 UI/物理线程直接操作 GL。重载失败保留上一有效场景；关闭窗口前先释放高斯 GL 资源，再销毁上下文。

## 相机必须来自最终可视化相机

读取 mjvScene 实际渲染相机，而不只读取命名相机 cam_xpos。这样自由相机、跟踪相机和固定相机共用同一路径。使用与 MuJoCo 完全一致的非对称视锥、near/far、主视口 rect。将相机方向/向上向量构建正交 c2w，并适配 Splatapult 的 OpenGL -Z forward；矩阵存储布局、转置、单位和手性都必须用标定图验收。

不能用一个固定 FOV 替代真实投影；UI 侧栏、HiDPI、窗口 resize 后要重算实际 framebuffer 像素。第一版仅普通单目；检测到 stereo 时明确禁用高斯或提示，不错误叠加单目结果。

## 推荐帧流程

1. 处理待加载资产、上下文重建及 resize。
2. 原 MuJoCo 绘制物理几何的颜色与深度。
3. 在主场景 rect 中排序高斯，以原几何深度裁剪被几何遮住的片元。
4. 按正确顺序把高斯透明混合到已有颜色上；高斯不写入普通 opaque depth。
5. 恢复 GL 状态；绘制原 viewer UI 和约定的 debug 覆盖层；交换缓冲。

## 两种深度集成路径

**候选 A：复用当前 framebuffer 深度。** 不清屏，打开 depth test、关闭 depth write，使用匹配 MuJoCo 的投影和深度比较。实现最小，但要求确认深度缓冲仍可用、MSAA/反向深度和状态一致。先做可行性探针，不预先保证可用。

**候选 B：自有颜色/深度 FBO。** MuJoCo 几何渲染到可采样 depth attachment，必要时 resolve MSAA；高斯 shader 将其片元深度转换为相同定义并与场景深度比较。再将合成结果输出主视口。控制更明确，但需处理天空背景、FBO 切换和 viewport offset。禁止将同一 texture 同时作为当前写入 attachment 和采样输入形成反馈。

若 A 无法稳定取得正确遮挡，转 B；不要用 CPU readPixels→上传作为正式每帧方案。透明 MuJoCo geom、反射、阴影深度和 debug 标记有各自语义，第一版只验收不透明实体，不宣称体积精确遮挡。

## 透明度与 GL 状态

沿用经核实的高斯排序方向和预乘 alpha；通常对应 ONE / ONE_MINUS_SRC_ALPHA，但先用合成测试证明。高斯深度是所用 splat 近似的深度，并非真实封闭表面。

隔离 framebuffer/read framebuffer、viewport、scissor、shader、VAO、active texture 和 bindings、SSBO/compute binding、depth func/test/mask、blend equation/factors、color mask、cull、sRGB 等实际触及状态。compute→draw 内存屏障必须匹配读写资源；MuJoCo classic 与现代 shader 代码交错时尤其注意状态缓存。

## 交互与碰撞

保留原 viewer 相机/选择/施力。高斯无碰撞 ID，因此点击可见高斯不等于选中实体；第一版选择仍基于 MuJoCo 几何，必须提示可能点到被高斯遮住的对象。可在调试模式显示碰撞代理。可视化分组与 contype/conaffinity 分开管理，隐藏代理不应关闭碰撞。

高斯接收物理遮挡不等于高斯受到光照/投影/接触力。第一版不承诺这些功能。
