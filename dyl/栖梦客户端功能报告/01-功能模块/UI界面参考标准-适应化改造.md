# QmClient UI 界面现代化 — 适应化改造方案

> 本文档基于 `UI界面参考标准.md` 的原始方案，结合 QmClient 实际代码结构进行适应化改造。
> 原始方案提出了"经典 UI + Rust/egui 现代 UI"双轨制架构，方向正确，但与项目实际状况存在多处偏差。
> 本文档修正这些偏差，给出可落地的实施方案。

### 核心定位

- **最终目标**：Rust（egui/wgpu）实现现代 UI，同时**永久保留**原版经典 UI 作为回退选项
- **QmUi 的定位**：QmUi（`CUiRuntimeV2`、`CUiV2LayoutEngine`、`CUiV2AnimationRuntime` 等）是 UI 现代化道路上的**试验品和过渡方案**，不是最终架构。其价值在于验证了布局引擎、动画运行时等概念，为 Rust UI 的设计提供了经验参考，但最终会被 Rust UI 替代
- **双轨共存原则**：经典 UI 和现代 Rust UI 必须可以随时切换，经典 UI 代码路径一行不改

---

## 一、原始方案与实际代码的偏差分析

| # | 原始方案假设 | 实际代码状况 | 影响 |
|---|------------|------------|------|
| 1 | 使用 `SCREEN_MAIN`/`SCREEN_SETTINGS` 等屏幕枚举 | 实际使用 `PAGE_` 枚举 + `m_MenuPage`/`m_GamePage` 双变量 | 分发逻辑需基于 `PAGE_*` |
| 2 | 仅支持 OpenGL 后端 | 支持 OpenGL（多版本）+ Vulkan 双后端，通过 `IGraphicsBackend` 抽象 | 渲染共存方案需兼容双后端 |
| 3 | 在 `CInput::DispatchEvent()` 中做输入分发 | 输入流为 `CInput::Update()` → `CGameClient::OnUpdate()` → 组件 `OnInput()` 链 | 分发点应在 `CGameClient::OnUpdate()` |
| 4 | 提议新建 `CUiManager` 类 | 已有 `CUiRuntimeV2`（QmRt）作为运行时总管 | `CUiRuntimeV2` 是试验品，最终由 Rust UI 接管，分发器仍需新建 |
| 5 | 忽略已有 QmUi 基础设施 | QmUi 已有布局引擎 + 动画运行时 + 节点树 + 渲染桥接 + 旧版适配 | QmUi 是过渡试验品，其经验指导 Rust UI 设计，但不应作为最终基础 |
| 6 | 配置变量写入 `config_variables.h` | QmClient 自定义变量写入 `config_variables_qmclient.h` | 新变量应遵循项目约定 |
| 7 | Rust UI 模块放在 `src/rust_ui/` | Rust 模块统一放在 `src/rust-bridge/` 下 | 遵循现有目录约定 |
| 8 | 使用 `try/catch` 做降级保护 | 项目禁用 C++ 异常，Rust 侧用 `ffi_wrap!` + `catch_unwind` | 降级机制需用返回值 + 标志位 |
| 9 | 未提及两套动画系统并存 | `CUiEffects`（旧版 Index-based）与 `CUiV2AnimationRuntime`（新版 Track-based）并存 | 需规划动画系统统一路径 |
| 10 | 未提及三个重复辅助函数 | `ResolveAnimatedValue`(scoreboard) 与 `ResolveAnimatedLayoutValueEx`(hud) 功能重复 | 需统一为公共工具函数 |

---

## 二、修正后的整体架构

### 2.1 架构图

```
┌──────────────────────────────────────────────────────────────────┐
│                    CGameClient::OnRender()                       │
│                           │                                      │
│              ┌────────────┴────────────┐                         │
│              ▼                         ▼                         │
│     ┌──────────────┐          ┌─────────────────┐               │
│     │  Classic UI  │          │  Rust Modern UI │               │
│     │  (原有代码)   │          │  (egui + wgpu)  │               │
│     │  CUIRect     │          │                  │               │
│     │  CUi IMGUI   │          │  纹理合成输出     │               │
│     │  CUiEffects  │          │  ↓ blit 到屏幕   │               │
│     └──────────────┘          └─────────────────┘               │
│              ▲                         ▲                        │
│              │            ┌────────────┘                        │
│              │            │                                     │
│              │     ┌──────────────────┐                         │
│              │     │ CUiModeDispatcher│                         │
│              │     │ (模式切换/降级)   │                         │
│              │     └──────┬───────────┘                         │
│              │            │                                     │
│              │     ┌──────────────────┐                         │
│              │     │ ui_bridge (FFI)  │                         │
│              │     │ C++ ←→ Rust 数据 │                         │
│              │     └──────────────────┘                         │
│              │                                                    │
│     ┌────────┴─────────┐                                        │
│     │ QmUi (过渡试验品) │ ← 当前在用，最终被 Rust UI 替代        │
│     │ CUiRuntimeV2     │ ← 经验可指导 Rust UI 设计              │
│     │ CUiV2LegacyAdapter│ ← 过渡期桥接                          │
│     └──────────────────┘                                        │
└──────────────────────────────────────────────────────────────────┘
```

### 2.2 与原始方案的核心差异

1. **Rust UI 是最终目标**：不把 QmUi 当作最终基础，而是直接以 Rust/egui + wgpu 为目标架构
2. **QmUi 是过渡试验品**：QmUi 验证了 Flexbox 布局、Track-based 动画等概念，其设计经验（如 `MERGE_TARGET` 中断策略、`SUiStyle` 声明式布局）可直接指导 Rust UI 的 API 设计，但 QmUi 代码本身最终会被替代
3. **双后端兼容**：渲染共存方案同时支持 OpenGL 和 Vulkan 后端
4. **经典 UI 永久保留**：原版 UI 作为回退选项始终可用，不是被替换的对象

---

## 三、配置系统

### 3.1 配置变量定义

在 `src/engine/shared/config_variables_qmclient.h` 中添加（遵循 QmClient 自定义变量约定，前缀 `Qm`）：

```cpp
MACRO_CONFIG_INT(QmModernUi, qm_modern_ui, 0, 0, 1,
    CFGFLAG_CLIENT|CFGFLAG_SAVE, "Enable modern UI V2")

MACRO_CONFIG_INT(QmModernUiParts, qm_modern_ui_parts, 0b1111, 0, 65535,
    CFGFLAG_CLIENT|CFGFLAG_SAVE, "Modern UI parts bitmask")

MACRO_CONFIG_INT(QmModernUiTransition, qm_modern_ui_transition, 1, 0, 1,
    CFGFLAG_CLIENT|CFGFLAG_SAVE, "Enable fade transition when switching UI mode")
```

### 3.2 界面粒度位掩码定义

基于实际的 `PAGE_` 枚举和 `SETTINGS_` 枚举定义位掩码：

```cpp
enum EModernUiPart
{
    MODERN_PART_START_MENU       = 1 << 0,   // 离线主菜单 (STATE_OFFLINE)
    MODERN_PART_SERVER_BROWSER   = 1 << 1,   // 服务器浏览器 (PAGE_INTERNET/LAN/FAVORITES)
    MODERN_PART_SETTINGS         = 1 << 2,   // 设置菜单 (PAGE_SETTINGS)
    MODERN_PART_DEMO_BROWSER     = 1 << 3,   // Demo 浏览器 (PAGE_DEMOS)
    MODERN_PART_HUD              = 1 << 4,   // 游戏内 HUD
    MODERN_PART_SCOREBOARD       = 1 << 5,   // 计分板
    MODERN_PART_CHAT             = 1 << 6,   // 聊天框
    MODERN_PART_NAMEPLATES       = 1 << 7,   // 名字板
};
```

### 3.3 分发判断函数

```cpp
bool ShouldUseModernUi(int PageId, IClient::EClientState State)
{
    if(!g_Config.m_QmModernUi)
        return false;

    int PartMask = g_Config.m_QmModernUiParts;

    if(State == IClient::STATE_OFFLINE)
    {
        switch(PageId)
        {
        case PAGE_NEWS:         return PartMask & MODERN_PART_START_MENU;
        case PAGE_INTERNET:
        case PAGE_LAN:
        case PAGE_FAVORITES:    return PartMask & MODERN_PART_SERVER_BROWSER;
        case PAGE_DEMOS:        return PartMask & MODERN_PART_DEMO_BROWSER;
        case PAGE_SETTINGS:     return PartMask & MODERN_PART_SETTINGS;
        default:                return false;
        }
    }
    return false;
}
```

> **注意**：原始方案使用 `SCREEN_MAIN` 等不存在的枚举。实际代码中，离线菜单使用 `m_MenuPage`（`PAGE_*` 枚举），在线菜单使用 `m_GamePage`（同一 `PAGE_*` 枚举），两者通过 `IClient::EClientState` 区分。

---

## 四、渲染分发

### 4.1 菜单渲染入口修正

原始方案在 `CMenus::OnRender()` 中做分发，但代码结构需调整。实际的 `CMenus::OnRender()` 流程：

```
OnRender()
  → Ui()->StartCheck()
  → UpdateColors()
  → Ui()->Update()
  → Render()           ← 分发点应在此
  → RenderTools()->RenderCursor()
  → Ui()->FinishCheck()
  → Ui()->ClearHotkeys()
```

`Render()` 方法内部根据 `IClient::EClientState` 分发到具体页面渲染函数。分发逻辑应插入 `Render()` 的最前面：

```cpp
void CMenus::Render()
{
    if(ShouldUseModernUi(m_MenuPage, Client()->State()))
    {
        if(RenderModernUiV2(m_MenuPage, Client()->State()))
            return;
    }

    // 原有经典渲染代码，完全不变 ↓↓↓
    CUIRect Screen = *UI()->Screen();
    // ...
}
```

**关键原则不变**：经典渲染代码路径**一行不改**，只在外面包一层 `if` 判断。

### 4.2 HUD 渲染分发

`CHud::OnRender()` 在 `m_vpAll` 组件列表中排第 49 位。分发逻辑类似：

```cpp
void CHud::OnRender()
{
    if(Client()->State() != IClient::STATE_ONLINE &&
       Client()->State() != IClient::STATE_DEMOPLAYBACK)
        return;

    m_Width = 300.0f * Graphics()->ScreenAspect();
    m_Height = 300.0f;
    Graphics()->MapScreen(0.0f, 0.0f, m_Width, m_Height);

    if(g_Config.m_QmModernUi && (g_Config.m_QmModernUiParts & MODERN_PART_HUD))
    {
        if(RenderModernHudV2())
            return;
    }

    // 原有经典 HUD 渲染代码 ↓↓↓
    if(g_Config.m_ClShowhud) { ... }
    RenderCursor();
}
```

### 4.3 渲染管线共存方案修正

原始方案提出 wgpu OpenGL ES 后端或纹理合成。但 QmClient 同时支持 **OpenGL（多版本）和 Vulkan** 后端，需要兼容两种情况：

#### 方案 A：纹理合成（推荐，Rust UI 最终方案）

Rust/egui 通过 wgpu 渲染到离屏纹理，C++ 侧将其 blit 到屏幕：

```
Rust egui → wgpu 渲染到离屏纹理 → 返回纹理句柄 → C++ 侧 blit 到屏幕
```

**纹理共享机制需按后端区分**：

| 后端 | 纹理共享方式 | 实现复杂度 |
|------|------------|-----------|
| OpenGL 3.3+ | 共享 GL Context，Rust 侧用 `wgpu::Backends::GL`，直接共享纹理 ID | 中 |
| Vulkan | 通过 Vulkan 外部内存 (VK_EXT_external_memory) 共享图像 | 高 |
| OpenGL 1.x/2.x | 仅纹理合成方案（Rust 渲染到 FBO → readback → C++ 上传为纹理） | 低效但兼容 |

**优点**：
- 完全隔离，wgpu 可用任何后端（Vulkan/DX/Metal/GL）
- egui 提供完整的即时模式 UI 框架，无需从零构建组件库
- Rust 生态的优势：内存安全、丰富的 crate、热重载潜力

**缺点**：
- 多一次纹理拷贝，有微小性能开销（现代 GPU 可忽略）
- 需要处理 wgpu 与现有 GL/Vulkan Context 的共存

#### 方案 B：QmUi + IGraphics 过渡方案（当前在用）

在 Rust UI 就绪前，QmUi 继续作为过渡方案使用现有 `IGraphics` 接口渲染：

```
QmUi 组件 → CUiV2LayoutEngine(布局) → CUiV2LegacyAdapter(转换) → CUIRect → IGraphics::DrawRect()
                                    → CUiV2AnimationRuntime(动画) → 插值后的属性值
```

**优点**：
- 零渲染管线改动，与 OpenGL/Vulkan 后端天然兼容
- 无 FFI 开销，无纹理拷贝

**缺点**：
- 受限于 `IGraphics` 的渲染能力（无阴影、无模糊等高级效果）
- 这是过渡方案，不应在其上投入过多建设组件库的精力

**推荐策略**：方案 B（QmUi）仅作为过渡期维持现有功能的手段，核心精力投入方案 A（Rust UI）的建设。

---

## 五、输入事件路由修正

### 5.1 原始方案的问题

原始方案提议在 `CInput::DispatchEvent()` 中做分发，但：
1. `CInput` 没有 `DispatchEvent` 方法，SDL 事件在 `CInput::Update()` 中通过 `SDL_PollEvent` 轮询
2. 输入事件通过 `CGameClient::OnUpdate()` → `m_vpInput` 组件链分发
3. 在 `CInput` 层拦截会破坏现有组件优先级机制

### 5.2 修正方案：在组件优先级链中插入分发

在 `m_vpInput` 列表中，`m_Menus` 排第 11 位。现代 UI 的输入处理应在 `m_Menus` 内部完成：

```cpp
// CMenus::OnInput() 中的分发
bool CMenus::OnInput(const IInput::CEvent &Event)
{
    if(ShouldUseModernUi(m_MenuPage, Client()->State()))
    {
        if(m_ModernUiActive)
        {
            bool Consumed = HandleModernUiInput(Event);
            if(Consumed && (Event.m_Flags & ~IInput::FLAG_RELEASE) != 0)
                return true;
        }
    }

    // 原有经典输入处理 ↓↓↓
    return CComponent::OnInput(Event);
}
```

**输入优先级策略**（与原始方案一致）：
- 现代 UI 打开时，优先消费输入（防止点击穿透到经典 UI）
- `FLAG_RELEASE` 事件始终转发给所有组件（现有机制保证）
- 游戏中按 `Escape` 时，如果现代 UI 有打开的子菜单，先关闭子菜单

### 5.3 Rust 扩展层的输入传递

如果引入 Rust 扩展层，输入事件通过 FFI 传递：

```cpp
// 现代UI输入处理（Rust扩展层）
bool HandleModernUiInput(const IInput::CEvent &Event)
{
    if(!m_pModernUiContext)
        return false;

    RustUiInputEvent CabiEvent;
    CabiEvent.m_Type = (Event.m_Flags & IInput::FLAG_PRESS) ? RUST_UI_EVENT_PRESS :
                       (Event.m_Flags & IInput::FLAG_RELEASE) ? RUST_UI_EVENT_RELEASE :
                       (Event.m_Flags & IInput::FLAG_TEXT) ? RUST_UI_EVENT_TEXT :
                       RUST_UI_EVENT_OTHER;
    CabiEvent.m_Key = Event.m_Key;
    CabiEvent.m_Text = Event.m_aText;

    int Result = rust_ui_handle_input(m_pModernUiContext, &CabiEvent);
    return Result != 0;
}
```

---

## 六、状态同步：C++ ↔ Rust 数据桥

### 6.1 FFI 机制选择

项目已有两种 FFI 机制：

| 机制 | 使用场景 | 适用性 |
|------|---------|--------|
| cxx bridge | 上游 DDNet 引擎接口（IConsole 等） | 适合类型安全的接口桥接 |
| 手写 C ABI + `ffi_wrap!` | QmClient 新增模块（voice） | 适合性能敏感、需要不透明句柄的场景 |

**UI 桥接推荐使用手写 C ABI**，原因：
1. 与 voice 模块保持一致（同为 QmClient 新增模块）
2. `ffi_wrap!` 宏提供 panic 安全保护
3. 不透明句柄模式适合 UI 上下文管理
4. 无需 cxx 代码生成步骤

### 6.2 数据桥 API 设计

遵循 voice 模块的约定：`#[repr(C)]` 结构体 + 不透明句柄 + `ffi_wrap!` 宏。

```cpp
// ui_bridge.h - C++ 侧暴露给 Rust 的 API
extern "C" {
    // 服务器列表（只读）
    typedef struct { const char *pName; int Ping; int NumPlayers; int MaxPlayers; } BridgeServerInfo;
    int Bridge_GetServerCount();
    void Bridge_GetServer(int Index, BridgeServerInfo *pOut);

    // 玩家本地状态（只读）
    const char *Bridge_GetLocalPlayerName();
    int Bridge_GetLocalPlayerScore();

    // 配置项（只读访问，修改通过命令发送）
    int Bridge_GetConfigInt(const char *pName);
    float Bridge_GetConfigFloat(const char *pName);
    void Bridge_ExecuteCommand(const char *pCmd);

    // UI 状态（只读）
    int Bridge_GetClientState();
    int Bridge_GetMenuPage();
    float Bridge_GetScreenWidth();
    float Bridge_GetScreenHeight();
}
```

```rust
// bridge.rs - Rust 侧
use crate::ffi_wrap;

#[repr(C)]
pub struct BridgeServerInfo {
    pub name: *const c_char,
    pub ping: i32,
    pub num_players: i32,
    pub max_players: i32,
}

ffi_wrap! {
    fn rust_ui_get_server_count() -> i32 {
        unsafe { Bridge_GetServerCount() }
    }
}

ffi_wrap! {
    fn rust_ui_get_server(index: i32, out: *mut BridgeServerInfo) {
        unsafe { Bridge_GetServer(index, out) };
    }
}
```

**重要**：Rust 侧**只读**访问 C++ 内存，所有修改操作通过 `Bridge_ExecuteCommand()` 发送控制台命令回传给 C++ 引擎执行，避免并发数据竞争。这与原始方案的原则一致。

---

## 七、降级与容错机制修正

### 7.1 原始方案的问题

原始方案使用 `try/catch` 做降级保护，但：
1. QmClient 项目禁用 C++ 异常（`-fno-exceptions`）
2. Rust 侧已通过 `ffi_wrap!` + `catch_unwind` + `panic = "abort"` 三重保护

### 7.2 修正方案：返回值 + 标志位

```cpp
class CUiModeDispatcher
{
    bool m_ModernUiFailed = false;
    bool m_ModernUiInitialized = false;
    size_t m_ModernUiHandle = 0;  // 不透明句柄，与 voice 模块一致

public:
    bool RenderModernUi(int PageId, IClient::EClientState State)
    {
        if(m_ModernUiFailed)
            return false;

        if(!m_ModernUiInitialized)
        {
            m_ModernUiHandle = rust_ui_create();
            if(m_ModernUiHandle == 0)
            {
                m_ModernUiFailed = true;
                Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ui",
                    "Modern UI initialization failed, falling back to classic UI");
                return false;
            }
            m_ModernUiInitialized = true;
        }

        int Result = rust_ui_render(m_ModernUiHandle, PageId, (int)State);
        if(Result < 0)
        {
            m_ModernUiFailed = true;
            Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ui",
                "Modern UI render failed, falling back to classic UI");
            return false;
        }

        return Result > 0;
    }
};
```

Rust 侧的 `ffi_wrap!` 宏确保即使 panic 也会返回 `Default::default()`（即 0），C++ 侧检测到 0 句柄或负返回值即回退。

### 7.3 诊断命令

```
qm_modern_ui_status     // 查看现代 UI 层状态
qm_modern_ui_reload     // 重新初始化（开发调试用）
```

---

## 八、用户体验：平滑切换

### 8.1 切换方式

1. **设置菜单中切换**：在"栖梦"设置页（`SETTINGS_QIMENG`）添加下拉框
2. **控制台命令**：`qm_modern_ui 1`
3. **快捷键**：`Ctrl+Shift+U` 快速切换

### 8.2 切换过渡动画

复用现有 `CUiV2AnimationRuntime` 的 `MERGE_TARGET` 中断策略实现平滑过渡：

```cpp
void CUiModeDispatcher::Update(float Dt)
{
    if(!g_Config.m_QmModernUiTransition)
    {
        m_TransitionAlpha = g_Config.m_QmModernUi ? 1.0f : 0.0f;
        return;
    }

    float TargetAlpha = g_Config.m_QmModernUi ? 1.0f : 0.0f;

    SUiAnimRequest Request;
    Request.m_NodeKey = UI_DISPATCHER_NODE_KEY;
    Request.m_Property = EUiAnimProperty::ALPHA;
    Request.m_Target = TargetAlpha;
    Request.m_Transition.m_DurationSec = 0.20f;
    Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
    Request.m_Transition.m_Easing = EEasing::EASE_OUT;
    m_AnimRuntime.RequestAnimation(Request);

    m_AnimRuntime.Advance(Dt);
    m_TransitionAlpha = m_AnimRuntime.GetValue(UI_DISPATCHER_NODE_KEY, EUiAnimProperty::ALPHA, TargetAlpha);
}
```

> **与原始方案的差异**：原始方案使用简单的指数衰减 `m_TransitionAlpha += (Target - Current) * Dt * 10.0f`。修正方案复用 `CUiV2AnimationRuntime` 的 `MERGE_TARGET` 策略，确保切换过程中目标再次改变时动画无跳变。

---

## 九、动画系统与 QmUi 过渡策略

### 9.1 现状

两套动画系统并存：

| 系统 | 位置 | 特点 | 使用情况 |
|------|------|------|---------|
| `CUiV2AnimationRuntime` | QmAnim.h | Track-based，支持优先级/中断策略/队列/延迟 | HUD、计分板、菜单、名字板 |
| `CUiEffects` | ui_effects.h | Index-based，简单指数衰减 | 受 `cl_hud_animations` 控制 |

### 9.2 QmUi 的过渡角色

QmUi 是试验品，其动画和布局系统在过渡期继续使用，但**不应继续扩展**。最终 Rust UI 会自带完整的动画和布局系统（egui 内置动画 + 自定义动画层）。

**QmUi 经验对 Rust UI 的指导价值**：

| QmUi 设计 | 经验教训 | Rust UI 对应设计 |
|-----------|---------|----------------|
| `MERGE_TARGET` 中断策略 | 目标值变化时保持视觉连续性是核心需求 | egui 动画层需实现类似策略 |
| `SUiStyle` 声明式布局 | Flexbox 风格声明比命令式分割更易维护 | egui 的 Layout 系统已内置类似能力 |
| `CUiV2LegacyAdapter` | 新旧系统需要桥接层 | Rust UI 同样需要 `ui_bridge` 与 C++ 数据对接 |
| `ResolveAnimatedValue` 辅助函数 | 动画值解析是高频操作，需统一接口 | Rust UI 的动画辅助函数应集中管理 |
| 两套动画系统并存 | 并存导致维护负担和代码重复 | Rust UI 应只使用一套统一的动画系统 |

### 9.3 过渡期策略

1. **短期**：保留 `CUiEffects` 和 `CUiV2AnimationRuntime` 不动，维持现有功能
2. **中期**：Rust UI 就绪后，已迁移到 Rust UI 的界面不再使用 QmUi 动画
3. **长期**：QmUi 整体退役，所有 UI 动画由 Rust UI 内置系统处理；`CUiEffects` 仅保留 `GetPulse`/`GetWave` 等辅助函数供游戏特效使用

### 9.4 重复辅助函数统一

将 `ResolveAnimatedValue`（scoreboard.cpp）和 `ResolveAnimatedLayoutValueEx`（hud.cpp）统一为 QmUi 模块的公共工具函数：

```cpp
// QmAnimUtil.h - 新增公共头文件
namespace UiAnimUtil
{
float ResolveAnimatedValue(CUiV2AnimationRuntime &AnimRuntime,
    uint64_t NodeKey, EUiAnimProperty Property,
    float Target, float &LastTarget,
    float DurationSec = 0.10f,
    float DelaySec = 0.0f,
    EEasing Easing = EEasing::EASE_OUT,
    float Epsilon = 0.01f);
}
```

---

## 十、文件结构建议（修正）

```
src/
├── game/client/
│   ├── QmUi/                        # QmUi 过渡试验品（维持现状，不继续扩展）
│   │   ├── QmAnim.h / QmAnim.cpp    # 动画运行时（过渡期继续使用）
│   │   ├── QmLayout.h / QmLayout.cpp # 布局引擎（过渡期继续使用）
│   │   ├── QmTree.h / QmTree.cpp    # 节点追踪
│   │   ├── QmRender.h / QmRender.cpp # 渲染桥接
│   │   ├── QmLegacy.h / QmLegacy.cpp # 旧版适配（过渡期桥接）
│   │   ├── QmRt.h / QmRt.cpp        # 运行时总管
│   │   └── QmAnimUtil.h             # 新增：统一动画辅助函数（过渡期整理）
│   ├── components/
│   │   ├── menus.cpp                 # 添加分发逻辑（约 20 行修改）
│   │   ├── menus.h
│   │   ├── hud.cpp                   # 未来添加 HUD 分发
│   │   ├── scoreboard.cpp            # 替换为 UiAnimUtil 公共函数
│   │   └── ui_effects.h / cpp        # 保留，过渡期后仅保留辅助函数
│   └── modern_ui/                    # 现代 UI 分发层（新增）
│       ├── ui_mode_dispatcher.h      # 模式分发器
│       ├── ui_mode_dispatcher.cpp
│       ├── ui_bridge.h               # C++ FFI 桥接声明
│       └── ui_bridge.cpp             # C++ FFI 桥接实现
├── rust-bridge/
│   ├── voice/                        # 现有语音模块
│   └── ui/                           # 新增：Rust UI 核心（最终目标）
│       ├── Cargo.toml
│       ├── build.rs
│       └── src/
│           ├── lib.rs                # FFI 入口 + ffi_wrap! 宏
│           ├── bridge.rs             # C++ 数据桥接
│           ├── anim.rs               # 动画系统（借鉴 QmUi 经验）
│           ├── screens/              # 各界面实现
│           │   ├── start_menu.rs
│           │   ├── server_browser.rs
│           │   └── settings.rs
│           └── theme.rs              # 主题定义
└── engine/shared/
    └── config_variables_qmclient.h   # 添加 QmModernUi 等配置变量
```

> **与原始方案的差异**：
> - Rust UI 模块放在 `src/rust-bridge/ui/` 而非 `src/rust_ui/`，遵循项目约定
> - QmUi 目录不再计划扩展 `QmWidgets`/`QmTheme`，因为 QmUi 是过渡试验品
> - 新增 `anim.rs`，明确 Rust UI 需要自带动画系统，借鉴 QmUi 的 `MERGE_TARGET` 等经验
> - 主题系统（`theme.rs`）在 Rust UI 内部实现，而非作为 C++ 层的扩展

---

## 十一、迁移顺序（修正）

### 第一阶段：基础分发机制 + Rust UI 骨架

| 步骤 | 内容 | 风险 |
|------|------|------|
| 1 | 统一 `ResolveAnimatedValue` 为 `UiAnimUtil` 公共函数（过渡期整理） | 低 |
| 2 | 创建 `CUiModeDispatcher`，在 `CMenus::Render()` 中添加分发逻辑 | 中 |
| 3 | 创建 `src/rust-bridge/ui/` crate，配置 CMake 集成 | 中 |
| 4 | 实现 `ui_bridge.h/cpp` 数据桥接（C++ 侧） | 中 |
| 5 | 实现 `bridge.rs` 数据桥接（Rust 侧）+ `ffi_wrap!` 宏 | 中 |

### 第二阶段：Rust UI 渲染管线 + 首个界面

| 步骤 | 内容 | 风险 |
|------|------|------|
| 6 | 实现纹理合成渲染（兼容 OpenGL + Vulkan） | 高 |
| 7 | 实现 `anim.rs` 动画系统（借鉴 QmUi `MERGE_TARGET` 经验） | 中 |
| 8 | 试点：用 Rust/egui 重写设置菜单（`PAGE_SETTINGS`） | 高 |

### 第三阶段：逐步扩展 Rust UI 覆盖范围

| 步骤 | 内容 | 风险 |
|------|------|------|
| 9 | 服务器浏览器（收益最大，信息密度高） | 高 |
| 10 | 主菜单 / 开始页面 | 中 |
| 11 | HUD（最复杂，需与游戏状态高频同步） | 高 |
| 12 | 计分板、聊天框、名字板 | 中 |

### 第四阶段：QmUi 退役

| 步骤 | 内容 | 风险 |
|------|------|------|
| 13 | 所有界面迁移到 Rust UI 后，QmUi 布局/动画代码可移除 | 低 |
| 14 | `CUiEffects` 仅保留 `GetPulse`/`GetWave`/`GetBounce` 等游戏特效辅助函数 | 低 |
| 15 | 经典 UI 永久保留作为回退选项 | — |

---

## 十二、实施 Checklist

| # | 原则 | 说明 |
|---|------|------|
| 1 | **Rust UI 是最终目标** | 直接以 Rust/egui + wgpu 为目标架构，不在 QmUi 上过度投入 |
| 2 | **QmUi 是过渡试验品** | 过渡期维持现状，其经验（`MERGE_TARGET`、声明式布局等）指导 Rust UI 设计 |
| 3 | **经典 UI 永久保留** | 原版 UI 作为回退选项始终可用，经典代码路径一行不改 |
| 4 | **配置持久化** | 利用 `config_variables_qmclient.h` 系统，前缀 `Qm` |
| 5 | **降级保护** | Rust 层通过 `ffi_wrap!` + `panic=abort` 保护，C++ 层通过返回值 + 标志位回退 |
| 6 | **用户可控** | 支持按界面粒度开关（`QmModernUiParts` 位掩码），而非一刀切 |
| 7 | **双后端兼容** | 纹理合成方案兼容 OpenGL 和 Vulkan 后端 |
| 8 | **遵循项目约定** | Rust 模块放 `rust-bridge/`，配置变量前缀 `Qm`，FFI 用手写 C ABI + `ffi_wrap!` |
