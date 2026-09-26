# 内存与生命周期工作流

本文件用于缓存、纹理、UI element、指针、引用、异步发布、内存/显存预算相关任务。

## 风险识别

修改前先判断是否涉及：

- 指针或引用跨帧保存。
- `CUIElement`、text container、纹理句柄和注册表。
- `std::vector` 扩容导致指针失效。
- cache key、cache eviction、disk restore、memory restore。
- 后台 job 结果发布到主线程。
- 显存或内存预算。

涉及这些内容时，不要只看 happy path。

## 生命周期规则

- UI/text/texture 资源的 owner 必须明确。
- 注册项在 owner 失效前解除；有意跨 reset 保留的注册按实际生命周期处理，避免重复注册。
- 缓存条目不能保存会被 vector reallocation 失效的裸指针。
- 普通资源 worker 不得直接修改 UI 或绕过图形命令队列操作 GPU context；专属渲染线程按后端合同执行。
- 发布结果前必须确认目标对象仍然有效。
- cache miss、cache disabled、cache corrupt 都必须安全回退。

## 预算规则

- 数量上限不等于内存/显存预算。
- 大图、preview、纹理 atlas 和 derived cache 要估算字节成本。
- 替换当前项时不要 double count 当前 resident 资源。
- evict 不能破坏当前可见项或稳定 pin。

## 验收

内存/生命周期改动应覆盖：

- 创建、复用、释放、重建路径。
- cache hit、miss、corrupt、disabled 路径。
- 页面切换、语言切换、UI scale 变化。
- 长时间运行或反复进入退出页面的 smoke。
