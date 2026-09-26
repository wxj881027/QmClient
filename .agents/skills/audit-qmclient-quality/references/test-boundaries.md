# 测试能力参考

用于选择验证方法，不要求执行所有检查。具体测试范围与证据复用遵循 `qmclient-verification-gate`。

## C++ 可以稳定覆盖的内容

- 纯函数：clamp、排序、匹配、路由、cache key、预算和几何计算。
- parser/formatter：LRC、TTML、sidecar、配置、版本、网络指标和 UTF 转换。
- 状态机：prewarm/read-only、实际存在的观察者状态、IME candidate、touch finger、Gores、main/dummy/spectator/demo。
- 生命周期决策：generation、取消、发布条件、reset、cache invalidation。
- 索引与边界：空列表、最大列表、失效 selection、非法 client id、畸形 count。
- 调用顺序合同：先更新状态再渲染、weapon body 与 laser endpoint 顺序、主线程发布前验证。
- fake-interface 集成：模拟 storage、HTTP、input、client state、clock 和 job result。
- 配置合同：默认值、旧值迁移、canonicalize、序列化往返和 chain 初始化时机。

优先使用已有可测试接口；确实无法隔离目标行为时，再提取窄 helper 或 fake 接口。不要为满足测试分类重构代码。设备检查可自动化：例如 `src/test/metal_backend_runtime_test.cpp` 需要真实平台和图形环境，不能把此类运行时测试误记为纯逻辑测试。

## C++ 只能部分覆盖的内容

| 领域      | C++ 可覆盖                                 | 仍需其他验证                     |
| --------- | ------------------------------------------ | -------------------------------- |
| UI 布局   | 几何、测量、裁剪、滚轮 owner、focus/reveal | 截图和多分辨率视觉检查           |
| 渲染      | gating、参数、调用顺序、状态恢复约定       | OpenGL/OpenGL ES/Vulkan/Metal 实际像素与驱动     |
| 性能      | 预算、采样、聚合和日志合同                 | 固定场景 p95/p99 实测            |
| 网络      | parser、超时状态机、旧响应丢弃             | 真实延迟、丢包、重连             |
| 音频      | buffer、状态机、格式和生命周期             | 设备、驱动、实时延迟和听感       |
| IME/触控  | offset、状态机、几何和 reset               | 原生候选窗、软键盘和真机多点触控 |
| Demo/预测 | 格式解析、tick 边界和状态转换              | 官方回放、地图和可达性对照       |


构建成功不能证明视觉、设备、性能或兼容性；源码字符串断言不能替代行为测试。时序测试优先可控时钟和确定调度，不靠宽松阈值隐藏不稳定。
