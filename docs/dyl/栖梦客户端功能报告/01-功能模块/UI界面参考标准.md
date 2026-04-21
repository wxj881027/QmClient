实现"经典 UI"与"现代 UI"的兼容切换，核心思路是**在渲染入口点做轻量级分发，保持经典代码路径完全不变**。以下是基于 DDNet 代码结构的具体实现方案：

## 整体架构：双轨制 + 抽象层

```
┌─────────────────────────────────────────────┐
│           CGameClient::OnRender()            │
│                   │                          │
│         ┌─────────┴─────────┐                │
│         ▼                   ▼                │
│  ┌─────────────┐    ┌──────────────┐        │
│  │  Classic UI │    │  Modern UI   │        │
│  │  (原有代码)  │    │  (Rust/egui) │        │
│  │  OpenGL     │    │  wgpu/OpenGL │        │
│  └─────────────┘    └──────────────┘        │
│         ▲                   ▲                │
│         └─────────┬─────────┘                │
│              CUiManager                      │
│         (模式切换/状态同步)                    │
└─────────────────────────────────────────────┘
```

---

## 1. 配置系统：用户偏好存储

在 `src/engine/shared/config_variables.h` 中添加配置项：

```cpp
// 主开关：0=经典, 1=现代
MACRO_CONFIG_INT(ClModernUi, cl_modern_ui, 0, 0, 1, CFGFLAG_CLIENT|CFGFLAG_SAVE, "Enable modern UI")

// 可选：按界面粒度控制（位掩码）
// bit 0: 主菜单, bit 1: 服务器浏览器, bit 2: 设置, bit 3: HUD
MACRO_CONFIG_INT(ClModernUiParts, cl_modern_ui_parts, 0b1111, 0, 15, CFGFLAG_CLIENT|CFGFLAG_SAVE, "Modern UI parts bitmask")
```

这样玩家可以通过控制台随时切换：
```
cl_modern_ui 1        // 开启现代 UI
cl_modern_ui 0        // 切回经典 UI
ui_mode modern        // 可添加便捷命令
```

---

## 2. 渲染分发：最小侵入式改造

DDNet 的菜单渲染入口在 `src/game/client/components/menus.cpp` 的 `CMenus::OnRender()`。这是最关键的分发点：

```cpp
// menus.cpp
void CMenus::OnRender()
{
    // 原有状态更新逻辑保持不变
    UpdateMenuState();
    
    // 在最后一帧渲染前做分发
    if(g_Config.m_ClModernUi && m_ActiveScreen != SCREEN_NONE)
    {
        // 调用 Rust 现代 UI 层
        if(RenderModernUi(m_ActiveScreen))
            return; // 现代 UI 成功渲染，跳过经典路径
        // 失败时回退到经典 UI（降级保护）
    }
    
    // 原有经典渲染代码，完全不变 ↓↓↓
    CUIRect Screen = *UI()->Screen();
    // ... 数百行原有渲染代码 ...
}
```

**关键原则**：经典渲染代码路径**一行不改**，只是在外面包一层 `if` 判断。

---

## 3. 按界面粒度渐进迁移

不要一次性替换所有 UI，而是按**界面级别**逐步切换。这样用户可以选择性启用：

```cpp
// 定义可独立切换的界面模块
enum EModernUiPart
{
    MODERN_PART_MENU = 1 << 0,        // 主菜单
    MODERN_PART_SERVERBROWSER = 1 << 1, // 服务器浏览器
    MODERN_PART_SETTINGS = 1 << 2,    // 设置菜单
    MODERN_PART_HUD = 1 << 3,         // 游戏内 HUD（最后迁移）
};

bool ShouldUseModernUi(int ScreenId)
{
    if(!g_Config.m_ClModernUi)
        return false;
        
    int PartMask = g_Config.m_ClModernUiParts;
    
    switch(ScreenId)
    {
        case SCREEN_MAIN: return PartMask & MODERN_PART_MENU;
        case SCREEN_SERVERBROWSER: return PartMask & MODERN_PART_SERVERBROWSER;
        case SCREEN_SETTINGS: return PartMask & MODERN_PART_SETTINGS;
        case SCREEN_HUD: return PartMask & MODERN_PART_HUD;
        default: return false;
    }
}
```

**迁移顺序建议**：
1. **第一阶段**：设置菜单（风险最低，功能独立）
2. **第二阶段**：服务器浏览器（收益最大，信息密度高）
3. **第三阶段**：主菜单（门面工程）
4. **第四阶段**：HUD（最复杂，需与游戏状态高频同步）

---

## 4. 输入事件路由

DDNet 使用 SDL2 处理输入。在 `src/engine/client/input.cpp` 中做事件分发：

```cpp
void CInput::DispatchEvent(const SDL_Event* pEvent)
{
    // 如果现代 UI 激活且该界面已接管输入
    if(g_Config.m_ClModernUi && IsModernUiActive() && !ShouldPassThroughToGame(pEvent))
    {
        // 通过 FFI 传递给 Rust 层
        RustUi_HandleEvent(pEvent->type, pEvent);
        
        // 现代 UI 可能希望阻止事件继续传递（如点击了按钮）
        if(RustUi_WantsCaptureInput())
            return;
    }
    
    // 继续原有经典输入处理流程
    ProcessClassicEvent(pEvent);
}
```

**输入优先级策略**：
- 现代 UI 打开时，**优先消费输入**（防止点击穿透到经典 UI）
- 游戏中按 `Escape` 时，如果现代 UI 有打开的子菜单（如设置弹窗），先关闭子菜单而不是直接打开经典主菜单

---

## 5. 状态同步：C++ ↔ Rust 数据桥

现代 UI 需要访问游戏数据（服务器列表、玩家信息、设置项）。建立只读数据桥，避免双向耦合：

```cpp
// ui_bridge.h - C++ 侧暴露给 Rust 的 API
extern "C" {
    // 服务器列表（只读）
    struct ServerInfo { const char* Name; int Ping; int NumPlayers; int MaxPlayers; };
    int Bridge_GetServerCount();
    void Bridge_GetServer(int Index, ServerInfo* pOut);
    
    // 玩家本地状态
    const char* Bridge_GetLocalPlayerName();
    int Bridge_GetLocalPlayerScore();
    
    // 设置项（双向，但 Rust 侧只读，修改通过命令发送）
    int Bridge_GetConfigInt(const char* pName);
    float Bridge_GetConfigFloat(const char* pName);
    void Bridge_ExecuteCommand(const char* pCmd); // 如 "zoom 1.5"
}
```

```rust
// bridge.rs - Rust 侧调用 C++ 数据
#[link(name = "ddnet_client")]
extern "C" {
    fn Bridge_GetServerCount() -> i32;
    fn Bridge_GetServer(index: i32, out: *mut ServerInfo);
    fn Bridge_ExecuteCommand(cmd: *const c_char);
}

// 在 Rust 中封装为安全接口
pub fn get_servers() -> Vec<ServerInfo> {
    unsafe {
        let count = Bridge_GetServerCount();
        (0..count).map(|i| {
            let mut info = ServerInfo::default();
            Bridge_GetServer(i, &mut info);
            info
        }).collect()
    }
}
```

**重要**：Rust 侧**只读**访问 C++ 内存，所有修改操作通过发送控制台命令回传给 C++ 引擎执行，避免并发数据竞争。

---

## 6. 渲染管线共存：OpenGL 与 wgpu

DDNet 使用 OpenGL，而 egui 通常配合 wgpu。让它们在同一窗口共存有两种方案：

### 方案 A：wgpu OpenGL ES 后端（推荐）
让 wgpu 使用 OpenGL ES 后端，与 DDNet 共享同一个 OpenGL Context：

```rust
// Rust 侧初始化
let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
    backends: wgpu::Backends::GL, // 强制使用 OpenGL 后端
    ..Default::default()
});

// 从 C++ 传入已有的 OpenGL context/raw window handle
let surface = unsafe { instance.create_surface_unsafe(window_handle) };
```

**优点**：无额外内存开销，纹理共享简单  
**缺点**：wgpu 的 GL 后端成熟度略低于 Vulkan

### 方案 B：纹理合成（更安全）
Rust 侧渲染到离屏纹理，C++ 侧将其作为普通 OpenGL 纹理 blit 到屏幕：

```cpp
// C++ 侧：每帧从 Rust 获取纹理 ID
void RenderModernUi()
{
    GLuint uiTexture = RustUi_RenderToTexture(screenW, screenH); // FFI 调用
    
    // 使用全屏四边形绘制该纹理
    glBindTexture(GL_TEXTURE_2D, uiTexture);
    DrawFullscreenQuad();
}
```

```rust
// Rust 侧：渲染到与 OpenGL 共享的纹理
let texture = device.create_texture(&wgpu::TextureDescriptor {
    label: Some("ui_output"),
    size: wgpu::Extent3d { width, height, depth_or_array_layers: 1 },
    format: wgpu::TextureFormat::Rgba8Unorm,
    usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::TEXTURE_BINDING,
    // ...
});
```

**优点**：完全隔离，wgpu 可用任何后端（Vulkan/DX/Metal/GL）  
**缺点**：多一次纹理拷贝，有微小性能开销（现代 GPU 可忽略）

---

## 7. 降级与容错机制

现代 UI 作为**可选组件**，必须保证在 Rust 层崩溃时游戏不闪退：

```cpp
bool RenderModernUi(int ScreenId)
{
    static bool s_ModernUiFailed = false;
    if(s_ModernUiFailed)
        return false; // 之前失败过，永久回退到经典 UI
    
    try {
        // 延迟初始化：第一次使用时才加载 Rust 库
        if(!m_pModernUi)
            m_pModernUi = RustUi_CreateContext();
            
        return RustUi_Render(m_pModernUi, ScreenId);
    }
    catch(...) {
        // 捕获任何异常（包括 Rust panic 通过 FFI 转换的异常）
        s_ModernUiFailed = true;
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ui", 
            "Modern UI failed, falling back to classic UI");
        return false;
    }
}
```

同时提供诊断命令：
```
modernui_status     // 查看 Rust 层状态
modernui_reload     // 重新初始化（开发调试用）
```

---

## 8. 用户体验：平滑切换

### 切换方式
1. **设置菜单中切换**：在"界面"设置页添加下拉框：
   - 经典 UI
   - 现代 UI（仅菜单）
   - 现代 UI（全部）
   
2. **控制台命令**：`cl_modern_ui 1`

3. **快捷键**：`Ctrl+Shift+U` 快速切换（方便对比效果）

### 切换时的过渡
避免瞬间跳变，添加 200ms 淡入淡出：
```cpp
void CUiManager::Update(float DeltaTime)
{
    float TargetAlpha = g_Config.m_ClModernUi ? 1.0f : 0.0f;
    m_TransitionAlpha += (TargetAlpha - m_TransitionAlpha) * DeltaTime * 10.0f;
    
    if(m_TransitionAlpha > 0.01f)
        RenderModernUiWithAlpha(m_TransitionAlpha);
        
    if(m_TransitionAlpha < 0.99f)
        RenderClassicUiWithAlpha(1.0f - m_TransitionAlpha);
}
```

---

## 9. 文件结构建议

```
src/
├── game/client/
│   ├── ui/                    # 经典 UI（原有代码，不动）
│   │   ├── ui.cpp
│   │   └── ui.h
│   ├── components/
│   │   ├── menus.cpp          # 添加分发逻辑（约 20 行修改）
│   │   ├── menus.h
│   │   └── hud.cpp            # 未来添加 HUD 分发
│   └── modern_ui/             # 现代 UI 桥接层（新增）
│       ├── bridge.cpp         # C++ FFI 实现
│       ├── bridge.h
│       └── dispatcher.cpp     # 渲染/输入分发
└── rust_ui/                   # Rust 项目（独立目录）
    ├── Cargo.toml
    ├── src/
    │   ├── lib.rs             # FFI 入口
    │   ├── screens/           # 各界面实现
    │   │   ├── main_menu.rs
    │   │   ├── server_browser.rs
    │   │   └── settings.rs
    │   └── theme.rs           # Discord 风格主题
    └── build.rs               # 生成 C 头文件
```

---

## 总结：实施 checklist

1. **零风险起步**：先只替换"设置菜单"一个界面，验证全链路
2. **经典代码零修改**：所有分发逻辑写在独立的 `modern_ui/` 目录
3. **配置持久化**：利用 DDNet 现有的 `config_variables.h` 系统
4. **降级保护**：Rust 层任何异常都自动回退到经典 UI
5. **用户可控**：支持按界面粒度开关，而非一刀切

这样既能推进 UI 现代化，又完全不会破坏现有玩家的游戏体验。你想先从哪个界面开始试点？我可以提供更具体的代码模板。