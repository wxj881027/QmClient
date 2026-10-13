# 资料与取舍

以下资料用于解释设计判断。本 skill 的正文为针对 QmClient 编写的工作流程，未复制社区工具的实现或引入其安装器。

## 官方依据

| 资料 | 何时读取 |
| --- | --- |
| [Apple HIG](https://developer.apple.com/design/human-interface-guidelines/) | 确认层次、交互和平台规范；按主题阅读，避免把 iOS 尺寸直接换算成游戏 UI 单位 |
| [Motion](https://developer.apple.com/design/human-interface-guidelines/motion) | 判断运动表达的状态和减少动态效果的替代方式 |
| [Designing Fluid Interfaces，WWDC 2018](https://developer.apple.com/videos/play/wwdc2018/803/) | 分析响应延迟、可中断操作和空间连续性 |
| [Animate with springs，WWDC 2023](https://developer.apple.com/videos/play/wwdc2023/10158/) | 分析位置与速度接续，以及不同回弹程度 |
| [Explore SwiftUI animation，WWDC 2023](https://developer.apple.com/videos/play/wwdc2023/10156/) | 理解状态与画面值的分工；向 C++ 迁移机制时核对现有后端能力 |
| [Materials](https://developer.apple.com/design/human-interface-guidelines/materials) | 涉及材质时检查导航、控制与内容的层次和可读性 |

HIG 页面依赖 JavaScript；只获取到标题时，不把摘要当成已读到的完整规范。引用原文保持简短，优先用自己的语言解释行为和理由。

## 社区方法参考

| 项目 | 吸收的方法 | QmClient 中的取舍 |
| --- | --- | --- |
| [apple-design-skill](https://github.com/tzzs/apple-design-skill) | 按主题查官方资料，并解释建议的原因 | 保留来源意识；平台专属控件、数值和手势需先判断适用性 |
| [Impeccable](https://github.com/pbakaus/impeccable) | 将设计评审、细节打磨和动效设计作为明确任务 | 使用针对玩家路径的具体反馈；网页检测器和浏览器工作流不替代 C++ UI 证据 |
| [UI UX Pro Max](https://github.com/nextlevelbuilder/ui-ux-pro-max-skill) | 先建立可复用设计规则，再处理页面局部差异 | 复用 QmClient 规格与 tokens；本轮不引入网站风格生成、技术栈模板或新增依赖 |

这些项目均为社区参考。其流行度、演示和自述不证明它们已在 QmClient 上得到验证；Apple API 的公开行为也不能作为 QmClient 当前实现的证据。
