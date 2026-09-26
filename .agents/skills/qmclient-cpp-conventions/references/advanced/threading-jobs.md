# 线程与 Jobs 工作流

本文件用于后台任务、资源 jobs、队列、预算、取消、主线程发布和 GPU context 边界。

## 线程边界

- 业务/UI 状态在所属线程发布；图形 API 的提交与 GPU 执行分别遵循后端线程合同。`backend_sdl.cpp` 的 `CGraphicsBackend_Threaded` 使用 Graphics thread 执行命令，普通资源 worker 不得绕过该路径直接操作图形对象。
- 文件 I/O、图片 decode、列表 plan build 可以进入后台。
- 后台 job 输出必须是可验证的数据，不是直接修改 UI 的副作用。
- 共享状态需要 owner 或同步策略，不要临时加锁掩盖设计不清。

## Jobs 生命周期

按 job 实际职责明确适用项：

- 请求 key 和去重策略。
- 可见资源加载需区分 visible、prefetch、background；其他 job 沿用其调度模型。
- 取消条件：页面离开、筛选变化、资源版本变化。
- 发布条件：目标仍有效、版本仍匹配、预算允许。
- fallback：结果缺失时 UI 仍能显示 source/live 路径。

## 主线程预算

交互帧中 drain job 结果必须有预算：

- 限制 count、bytes 或 duration。
- 已使用 `work_drain` 的路径保持其字段合同；新增观测仅在诊断需要时引入，不要求每类 job 都输出该事件。
- `stop` 不能只写 success；预算耗尽、队列空、目标失效都要区分。
- 大量结果不能在页面切换帧集中发布。

## 验收

线程/jobs 改动至少验证：

- 取消后不发布旧结果。
- 同 key 重复请求不会产生错误覆盖。
- 页面离开再回来仍能恢复。
- perf 日志能解释 drain 是否被预算截断。
