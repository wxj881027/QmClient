# QmClient UI 和动画效果现代化报告

## 一、UI 组件合并 — QmUi 目录

### 1.1 目录结构与文件清单

QmUi 目录位于 `src/game/client/QmUi/`，包含 6 个模块（12 个文件）：

| 文件 | 核心类/结构体 | 职责 |
|------|-------------|------|
| QmAnim.h / QmAnim.cpp | `CUiV2AnimationRuntime` | UI 动画运行时引擎 |
| QmLayout.h / QmLayout.cpp | `CUiV2LayoutEngine` | Flexbox 风格布局引擎 |
| QmTree.h / QmTree.cpp | `CUiV2Tree` | UI 节点树追踪 |
| QmRender.h / QmRender.cpp | `CUiV2RenderBridge` | 渲染统计桥接 |
| QmLegacy.h / QmLegacy.cpp | `CUiV2LegacyAdapter` | 新旧 UI 矩形类型适配 |
| QmRt.h / QmRt.cpp | `CUiRuntimeV2` | UI 运行时总管（整合以上所有模块） |

### 1.2 核心组件详解

#### CUiV2LayoutEngine — Flexbox 布局引擎

类 CSS Flexbox 的布局系统，定义在 `QmLayout.h`：

```cpp
enum class EUiAxis { ROW, COLUMN };         // 主轴方向
enum class EUiAlign { START, CENTER, END, STRETCH }; // 对齐方式
enum class EUiLengthType { AUTO, PX, PERCENT, FLEX }; // 尺寸类型

struct SUiLength {
    EUiLengthType m_Type; float m_Value;
    static SUiLength Auto();
    static SUiLength Px(float Value);
    static SUiLength Percent(float Value);
    static SUiLength Flex(float Value);
};

struct SUiStyle {
    EUiAxis m_Axis = EUiAxis::COLUMN;
    SUiLength m_Width, m_Height, m_MinWidth, m_MinHeight, m_MaxWidth, m_MaxHeight;
    SUiEdges m_Padding;
    float m_Gap = 0.0f;
    EUiAlign m_AlignItems = EUiAlign::START;      // 交叉轴对齐
    EUiAlign m_JustifyContent = EUiAlign::START;   // 主轴对齐
    bool m_Clip = false;
};
```

`ComputeChildren()` 方法实现了完整的 Flexbox 算法：解析 AUTO/PX/PERCENT/FLEX 长度，计算 Flex 分配，处理 Gap、MinMax 约束、对齐偏移等。

#### CUiV2LegacyAdapter — 新旧桥接

提供 `SUiLayoutBox` 与旧版 `CUIRect` 之间的双向转换：

```cpp
class CUiV2LegacyAdapter {
    static CUIRect ToCUIRect(const SUiLayoutBox &Box);
    static SUiLayoutBox FromCUIRect(const CUIRect &Rect);
};
```

#### CUiRuntimeV2 — 运行时总管

整合所有子系统，在 `gameclient.h:308` 中作为成员变量持有：

```cpp
CUiRuntimeV2 m_UiRuntimeV2;
```

### 1.3 QmUi 的实际使用情况

| 使用位置 | 使用的 QmUi 类 | 用途 |
|---------|--------------|------|
| scoreboard.cpp | `CUiV2LayoutEngine`, `CUiV2LegacyAdapter`, `CUiV2AnimationRuntime` | 计分板 Flexbox 布局 + 动画 |
| hud.cpp | `CUiV2LayoutEngine`, `CUiV2LegacyAdapter`, `CUiV2AnimationRuntime` | HUD 布局 + 动画值 |
| menus.cpp | `CUiV2AnimationRuntime` | 菜单动画 |
| menus_start.cpp | `CUiV2LayoutEngine`, `CUiV2LegacyAdapter` | 开始菜单布局 |
| nameplates.cpp | `CUiV2AnimationRuntime` | 名字板动画 |

### 1.4 测试覆盖

`src/test/QmAnimTest.cpp` 包含 6 个 GTest 测试用例：
- `ReplacePolicyReplacesCurrentTrack` — 替换策略
- `QueuePolicyRunsInOrder` — 队列策略
- `KeepHigherPriorityRejectsLowerPriorityRequest` — 优先级策略
- `MergeTargetKeepsContinuity` — 合并目标策略（连续性）
- `DelayDefersAnimationStart` — 延迟启动
- `ZeroDurationCompletesImmediately` — 零时长立即完成

---

## 二、动画和 UI 风格统一

### 2.1 两套动画系统并存

#### 系统 A：CUiV2AnimationRuntime（QmUi 模块，现代）

定义在 `QmAnim.h`，基于 Track 的动画系统：

```cpp
enum class EEasing { LINEAR, EASE_IN, EASE_OUT, EASE_IN_OUT };
enum class EUiAnimInterruptPolicy { REPLACE, QUEUE, KEEP_HIGHER_PRIORITY, MERGE_TARGET };

struct SUiAnimTransition {
    float m_DurationSec = 0.18f;
    float m_DelaySec = 0.0f;
    int m_Priority = 0;
    EUiAnimInterruptPolicy m_Interrupt = EUiAnimInterruptPolicy::REPLACE;
    EEasing m_Easing = EEasing::EASE_OUT;
};
```

特点：支持优先级、中断策略、队列、延迟启动、完成事件轮询、属性驱动（POS_X/Y, WIDTH, HEIGHT, ALPHA, COLOR_R/G/B/A, SCALE）。被 HUD、计分板、菜单、名字板广泛使用。

#### 系统 B：CUiEffects（旧版组件）

定义在 `ui_effects.h` / `ui_effects.cpp`，基于 Index 的简单插值系统：

```cpp
class CUiEffects : public CComponent {
    enum ETransitionType {
        TRANSITION_LINEAR, TRANSITION_EASE_IN, TRANSITION_EASE_OUT,
        TRANSITION_EASE_IN_OUT, TRANSITION_BOUNCE
    };
    int CreateSmoothValue(float Initial, float Speed, ETransitionType Type);
    void SetSmoothValue(int Index, float Target);
    float GetSmoothValue(int Index);
};
```

特点：无优先级、无中断策略，额外支持 Bounce 缓动，受 `g_Config.m_ClHudAnimations` 和 `g_Config.m_ClHudAnimationSpeed` 控制。

### 2.2 三个重复的辅助函数

| 函数 | 文件 | 功能 |
|------|------|------|
| `ResolveAnimatedLayoutValueEx()` | hud.cpp:637 | HUD 动画值解析 |
| `ResolveUiAnimValue()` | menus.cpp:74 | 菜单动画值解析 |
| `ResolveAnimatedValue()` | scoreboard.cpp:41 | 计分板动画值解析 |

这三个函数功能几乎相同，是重复代码，**应统一为一个**。

### 2.3 动画相关配置变量

| 配置变量 | 默认值 | 说明 |
|---------|--------|------|
| `ClHudAnimations` | 1 | 启用 HUD 动画 |
| `ClHudAnimationSpeed` | 100 | HUD 动画速度百分比 |
| `TcAnimateWheelTime` | 350 | 表情轮/绑定轮动画时长(ms) |
| `QmScoreboardAnimOptim` | 1 | 计分板动画优化 |
| `QmChatFadeOutAnim` | 1 | 聊天框淡出动画 |
| `QmEmoticonSelectAnim` | 1 | 表情选择动画 |
| `QmChatBubbleAnimation` | 0 | 聊天气泡消失动画(0=淡出,1=缩小,2=上滑) |
| `QmLaserPulseSpeed` | 100 | 激光脉冲动画速度 |
| `QmLaserPulseAmplitude` | 50 | 激光脉冲振幅 |
| `QmUiRuntimeV2Debug` | 0 | UI 运行时 v2 调试日志 |

### 2.4 UI 风格/主题系统

目前**没有统一的 UI 主题/样式系统**。各组件的颜色和样式通过分散的配置变量控制：

- 状态栏颜色：`TcStatusBarColor`, `TcStatusBarTextColor`, `TcStatusBarAlpha`
- HUD 灵动岛：`QmHudIslandBgColor`, `QmHudIslandBgOpacity`
- 聊天气泡：`QmChatBubbleBgColor`, `QmChatBubbleTextColor`, `QmChatBubbleAlpha`
- 饼菜单：`QmPieMenuColorFriend`, `QmPieMenuColorWhisper` 等
- 激光：`QmLaserGlowIntensity`, `QmLaserAlpha`

`SUiStyle` 结构体（在 QmLayout.h 中）仅控制布局属性，不包含视觉样式（颜色、圆角、阴影等）。

---

## 三、Qimeng / TaterClient 残留引用

### 3.1 "Qimeng" 残留引用（需重命名）

| 文件 | 行号 | 内容 | 优先级 |
|------|------|------|--------|
| config_variables_qimeng.h | 10 | `// Qimeng Client Specific Variables` | 中 |
| config_domains.h | 9 | `CONFIG_DOMAIN(QIMENG, "settings_qmclient.cfg", true)` | 中 |
| config_includes.h | 11,14 | `SET_CONFIG_DOMAIN(ConfigDomain::QIMENG)` | 中 |
| config_variables_qmclient.h | 331 | `TcUiShowQimeng` / `tc_ui_show_qimeng` | 中 |
| menus_qmclient.cpp | 2974 | `// Domain 筛选 - DDNet / Qimeng` | 低 |
| menus_qmclient.cpp | 3211 | `case ConfigDomain::QIMENG: return "Qimeng";` | 中 |
| menus_qmclient.cpp | 3429 | `void CMenus::RenderSettingsQiMeng(...)` | 低 |
| menus_qmclient.cpp | 4556 | `Localize("Qimeng's assorted daily-use tools")` | 中 |
| menus.h | 895 | `SETTINGS_QIMENG,` | 中 |
| menus.h | 1059 | `void RenderSettingsQiMeng(...)` | 低 |
| lyrics_component.cpp | 170 | `"QimenGClient"` (User-Agent) | **高** |

### 3.2 "TClient" / "CTClient" 类名残留

`CTClient` 是 QmClient 的核心组件类，包含大量以 `TClient` 命名的成员函数和 `tc_` 前缀的配置变量。重命名涉及 100+ 处引用，**成本高且可能影响配置文件兼容性**，需谨慎评估。

### 3.3 "栖梦" 引用

"栖梦" 是 QmClient 的中文名，以下位置使用了该名称：

| 文件 | 行号 | 内容 |
|------|------|------|
| config_variables_qimeng.h | 256-258 | `"栖梦侧栏模块排序/折叠状态/使用频率"` |
| config_variables_qmclient.h | 331 | `"在配置界面显示栖梦配置项"` |
| menus_qmclient.cpp | 4849 | `"栖梦(璇梦),夏日,DYL"` |
| explanations.cpp | 8 | `// DDNet entity Translation by 栖梦` |

---

## 四、总结与建议

### UI 组件合并
- QmUi 目录已建立了完整的 V2 UI 基础设施（布局引擎 + 动画运行时 + 节点树 + 渲染桥接 + 旧版适配），但尚未形成统一的组件库（如按钮、列表、卡片等可复用组件）。
- `CUiV2LegacyAdapter` 的广泛使用说明目前仍处于"新旧混合"阶段，新布局引擎的结果需要转回 `CUIRect` 才能使用旧版渲染 API。

### 动画和 UI 风格统一
- **两套动画系统并存**：`CUiV2AnimationRuntime`（现代、Track-based）和 `CUiEffects`（旧版、Index-based），功能有重叠，应逐步迁移到 V2 系统。
- **三个重复的辅助函数**应统一为一个。
- **无统一主题系统**：颜色/样式分散在数十个配置变量中，缺少集中管理的 Theme/Style 系统。`SUiStyle` 仅覆盖布局，不含视觉属性。

### Qimeng / TaterClient 重命名
- **高优先级**：`lyrics_component.cpp` 中的 User-Agent `"QimenGClient"` 应立即改为 `"QmClient"`。
- **中优先级**：`ConfigDomain::QIMENG` 枚举名、`config_variables_qimeng.h` 文件名、`SETTINGS_QIMENG` 枚举、`RenderSettingsQiMeng` 函数名等代码标识符。
- **低优先级/需谨慎**：`CTClient` 类名和 `tc_` 配置前缀涉及 100+ 处引用，重命名成本高且可能影响配置文件兼容性。
