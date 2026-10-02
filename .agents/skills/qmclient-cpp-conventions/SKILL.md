---
name: qmclient-cpp-conventions
description: 修改或调试 QmClient C++ 时使用；提供 DDNet 命名、热路径、资源与线程约定，以及按风险读取的专项参考。
---

# QmClient C++ 约定

先读当前实现、直接调用点和相关测试。授权与兼容性边界遵循根 `AGENTS.md`。

## 风格与配置

- 遵循所在模块的 DDNet 风格，`src/base` 等区域沿用自身约定；不因通用现代 C++ 偏好改写已有模式。
- 局部变量、方法和类名沿用大驼峰；前缀包括 `m_` 成员、`g_` 全局、`s_` 静态、`p` 指针、`a` 固定数组、`v` 向量、`C` 类、`I` 接口。
- 枚举类按附近模式使用 `E...` 名称及大写枚举值；所有权和错误传播方式与模块一致。
- Qm 配置用 `qm_` / `Qm`；新增可翻译文本或配置说明时读取 `qmclient-i18n-workflow`，维护源与运行时产物分开。
- 抽函数、模板或 RAII 只在能解决当前问题且保持局部风格时引入；不要隐藏所有权转移。

## 实时路径

识别每帧、tick、玩家、实体、snapshot 和文本布局路径。检查新增的分配、字符串构造、排序扫描、`TextWidth`、配置写入、序列化和网络成本是否必要。

性能敏感职责尽可能保持模块化和可 benchmark 化，直接测量生产实现；相关路径优先复用 Google Benchmark，具体命令与证据由 `qmclient-verification-gate` 的 [性能参考](../qmclient-verification-gate/references/performance.md) 维护。静态可确认的正确性问题可以先修复，缺少可比实测时不宣称性能提升。

## UI 组件与共享职责

Qm 卡片和组件沿现有共享入口管理布局、状态与生命周期。检查长译文、缩放、窄视口和无内容时的尺寸适应与兜底；交互改动同时覆盖外部点击、ESC、嵌套关闭、离页失效和重开。用生产状态转换约束回归，不为最小化 diff 分散同类策略或牺牲架构统一性。

## 资源与线程

- 校验外部输入、大小和索引；开发者不变量用断言，外部失败沿用运行时错误处理。
- 涉及缓存、纹理、`CUIElement`、text container、`string_view` 或跨帧引用时，核对 owner、失效和释放路径。
- 异步结果发布前验证对象与请求版本仍有效；GPU/UI 操作遵循实际后端的线程归属。
- 音频、图形、HTTP、存储或后台任务改动先识别共享状态；不靠临时加锁替代生命周期设计。

## 专项参考

仅在改动触及表中风险时读取，参考中的验收项只适用于实际改变的行为。

| 风险 | 本 skill 下的参考 |
| --- | --- |
| 性能实现、长帧或诊断链路 | [performance-workflow.md](references/advanced/performance-workflow.md) |
| 行为保持型重构 | [refactor-workflow.md](references/advanced/refactor-workflow.md) |
| 新功能或新配置 | [feature-introduction.md](references/advanced/feature-introduction.md) |
| 下载、文件、外部输入 | [safety-security.md](references/advanced/safety-security.md) |
| 缓存、纹理、对象生命周期 | [memory-lifetime.md](references/advanced/memory-lifetime.md) |
| 后台任务与结果发布 | [threading-jobs.md](references/advanced/threading-jobs.md) |
| 诊断与反馈包 | [observability-debugging.md](references/advanced/observability-debugging.md) |
| 回归场景设计 | [regression-prevention.md](references/advanced/regression-prevention.md) |

验证集合由 `qmclient-verification-gate` 统一选择；审查使用 `qmclient-code-review`，不因加载本 skill 自动触发全量审计。
