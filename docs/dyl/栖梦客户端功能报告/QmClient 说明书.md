# QmClient (栖梦客户端) 说明书

## 项目简介

QmClient (栖梦客户端，全称 Q1menG Client) 是基于 DDNet / TaterClient 构建的第三方定制客户端。项目名称 "栖梦" 寓意为玩家提供一个舒适、梦幻的游戏体验环境。

### 主要特色

- **现代化 UI 设计** - QmUi V2 基础设施（Flexbox 布局引擎 + Track-based 动画运行时），采用毛玻璃、渐变、动画等现代视觉元素
- **聊天实时翻译** - 三后端翻译系统（腾讯云 / LibreTranslate / FTAPI），支持自动翻译和手动翻译
- **资源工坊系统** - 在线 Workshop 下载社区贴图资源，支持 6 种资源类型浏览和贴图编辑器
- **配置管理系统** - 三域分离的配置架构，10 个功能标签快速筛选
- **好友分类管理** - 自定义好友分类（增删改查、拖拽排序、好友备注）
- **Bind 指令系统** - 三组件架构（键盘绑定 / 聊天绑定 / 绑定轮盘），含指令工坊 UI
- **代码可移植性** - 参考 BestClient 功能，支持功能移植

---

## 配置系统架构

### 三域分离设计

QmClient 采用三域分离的配置架构，将不同类型的配置项分开管理：

| 配置域 | 前缀 | 说明 | 存储文件 |
|--------|------|------|----------|
| **DDNET** | 无 | 原版 DDNet 设置 | `settings_ddnet.cfg` |
| **TCLIENT** | `tc_*` | TaterClient 特有设置 | `settings_tclient.cfg` |
| **QIMENG** | `qm_*` | 栖梦客户端特有设置 | `settings_qmclient.cfg` |

> **注意**：QIMENG 域的存储文件已更名为 `settings_qmclient.cfg`，但枚举名和部分代码标识符仍保留 "Qimeng" 命名，待后续统一。

### 配置变量命名规范

```cfg
# DDNet 原版配置 (无前缀)
cl_name
cl_language
gfx_fullscreen

# TClient 配置 (tc_ 前缀)
tc_rainbow_mode
tc_skin_preview
tc_deepfly

# 栖梦客户端配置 (qm_ 前缀)
qm_fast_input_mode
qm_delta_input_others
qm_gamma_input_others
```

---

## Tags 标签筛选系统

### 标签分类

Configs 页面支持按 10 个功能标签筛选配置项：

| 标签 | 中文名 | 包含配置类型 |
|------|--------|--------------|
| **Visual** | 视觉效果 | 皮肤、彩虹、激光、Jelly Tee 等 |
| **HUD** | 界面显示 | 计分板、灵动岛、玩家统计等 |
| **Input** | 输入优化 | Fast Input、Deepfly、Snap Tap 等 |
| **Chat** | 聊天相关 | 气泡、复读、关键词回复、翻译等 |
| **Audio** | 语音/音效 | RiVoice 语音聊天、音效设置等 |
| **Auto** | 自动 | 自动化功能、Gores 模式等 |
| **Social** | 社交功能 | 好友、饼菜单、屏蔽词等 |
| **Camera** | 镜头视野 | 漂移、动态 FOV 等 |
| **Gameplay** | 游戏玩法 | 武器轨迹、碰撞盒显示等 |
| **Misc** | 其他杂项 | 其他未分类功能 |

### 智能标签匹配

系统根据配置变量名自动推断所属分类：

- **前缀匹配**: `tc_*` → 匹配所有 TClient 配置 → **Input** 标签
- **后缀匹配**: `*_color` → 匹配颜色相关配置 → **Visual** 标签
- **中间匹配**: `*laser*` → 匹配激光相关配置 → **Visual** 标签

---

## 主要功能模块

### 1. 视觉效果 (Visual)

| 功能 | 配置变量 | 说明 |
|------|----------|------|
| **Q弹 Tee** | `qm_jelly_tee` | Tee 形变效果，落地时产生弹性形变 |
| **彩虹名字** | `qm_rainbow_name` | 自己名字显示为彩虹色 |
| **增强激光** | `qm_laser_enhanced` | 激光辉光+脉冲动画效果 |
| **皮肤队列** | `qm_skin_queue_*` | 自动轮换皮肤，支持从地图玩家获取 |
| **碰撞盒显示** | `qm_show_collision_hitbox` | 显示 Freeze 等碰撞体积边框 |

### 2. 界面显示 (HUD)

| 功能 | 配置变量 | 说明 |
|------|----------|------|
| **灵动岛** | `qm_hud_island_*` | HUD 灵动岛样式自定义 |
| **玩家统计** | `qm_player_stats_hud` | 显示玩家统计数据 |
| **地图进度条** | `qm_player_stats_map_progress` | 显示当前地图进度 |
| **输入显示** | `qm_input_overlay` | 在屏幕上显示当前按键输入 |
| **计分板优化** | `qm_scoreboard_anim_optim` | 计分板动画优化 |
| **状态栏** | `tc_statusbar` / `tc_statusbar_scheme` | 可自定义布局的状态栏，支持 Ping/FPS/Prediction 等指标 |
| **名字板 Ping 圆环** | `tc_nameplate_ping_circle` | 在名字板旁显示 Ping 彩色圆环 |

### 3. 输入优化 (Input)

| 功能 | 配置变量 | 说明 |
|------|----------|------|
| **快速输入** | `qm_fast_input_*` / `tc_fast_input_*` | 减少输入延迟，支持多种模式 |
| **Deepfly** | `qm_deepfly_mode` | 自动 Deepfly 辅助 |
| **Gores 自动切换** | `qm_gores` | Gores 模式自动锤枪切换 |
| **自动解冻切换** | `qm_auto_switch_on_unfreeze` | 自动切换到先解冻的 Tee |
| **AntiPing 增强** | `tc_antiping_improved` 等 | 更激进的延迟补偿和预测 |

### 4. 聊天翻译系统 (Chat / Translate)

QmClient 内置三后端聊天翻译系统，支持将游戏内聊天消息实时翻译为目标语言。

#### 翻译后端

| 后端 | 配置值 | 认证方式 | 自动翻译 | 说明 |
|------|--------|----------|----------|------|
| **腾讯云翻译** | `tencentcloud`（默认） | TC3-HMAC-SHA256 签名 | ✅ 支持 | 默认端点 `tmt.tencentcloudapi.com`，凭证支持配置变量和环境变量 |
| **LibreTranslate** | `libretranslate` | 可选 API Key | ✅ 支持 | 自部署开源翻译引擎，默认 `localhost:5000` |
| **FTAPI** | `ftapi` | 无 | ❌ 禁用 | 公共免费服务，为防过载禁用自动翻译 |

#### 翻译配置

| 配置变量 | 默认值 | 说明 |
|----------|--------|------|
| `tc_translate_backend` | `tencentcloud` | 翻译后端选择 |
| `tc_translate_target` | `zh` | 翻译目标语言代码 |
| `tc_translate_endpoint` | `https://tmt.tencentcloudapi.com/` | API 端点地址 |
| `tc_translate_secret_id` | (空) | 腾讯云 SecretId |
| `tc_translate_secret_key` | (空) | 腾讯云 SecretKey |
| `tc_translate_key` | (空) | 通用 API Key（如 LibreTranslate） |
| `tc_translate_region` | `ap-guangzhou` | 腾讯云地域 |
| `tc_translate_auto` | `1` | 自动翻译开关 |

#### 触发方式

- **自动翻译**：新消息到达时自动翻译（`tc_translate_auto`，自动跳过中文源语言）
- **控制台命令**：`translate [name]` / `translate_id <id>`
- **BindChat 快捷键**：`!translate` / `!translateid`
- **出站翻译**：消息末尾添加 `[ru]`、`[en]` 等语言代码（⚠️ 发送逻辑尚未完成）

#### 聊天其他功能

| 功能 | 配置变量 | 说明 |
|------|----------|------|
| **聊天气泡** | `qm_chat_bubble` | 在玩家头顶显示聊天气泡 |
| **复读功能** | `qm_repeat_enabled` | 快速复读上一条消息 |
| **关键词回复** | `qm_keyword_reply_*` | 自动回复特定关键词 |
| **草稿保存** | `qm_chat_save_draft` | 关闭聊天时保留未发送内容 |
| **屏蔽词** | `qm_block_words_*` | 聊天屏蔽词列表 |
| **聊天框淡出动画** | `qm_chat_fade_out_anim` | 消息淡出动画效果 |
| **聊天气泡消失动画** | `qm_chat_bubble_animation` | 0=淡出, 1=缩小, 2=上滑 |

### 5. 语音/音效 (Audio)

| 功能 | 配置变量 | 说明 |
|------|----------|------|
| **RiVoice 语音** | `ri_voice_*` | 内置语音聊天系统，支持距离衰减、分组、VAD 等 |
| **语音滤波** | `ri_voice_filter_enable` | 高通/压缩/限幅处理 |
| **噪声抑制** | `ri_voice_noise_suppress_*` | 噪声抑制功能 |

### 6. 社交功能 (Social)

| 功能 | 配置变量 | 说明 |
|------|----------|------|
| **饼菜单** | `qm_pie_menu_*` | 快速交互菜单（好友、私聊、复制皮肤等） |
| **好友自定义分类** | `friend_category_add` 等 | 支持自定义好友分类（增删改查、拖拽排序） |
| **好友备注** | `set_friend_note` | 为好友设置备注 |
| **好友上线提醒** | `qm_friend_online_notify` | 好友上线时提醒 |
| **自动打招呼** | `qm_friend_enter_auto_greet` | 好友进图自动打招呼 |
| **屏蔽词** | `qm_block_words_*` | 聊天屏蔽词列表 |
| **QmClient 标识** | `qm_client_show_badge` | 通过中心服识别并标记其他 QmClient 用户 |

#### 好友分类系统

好友系统支持自定义分类，预置三个受保护分类（"好友"、"战队成员"、"离线"），用户可自由添加/重命名/删除自定义分类，支持拖拽排序。右键分类头部可弹出管理菜单，右键好友可移动到指定分类。

### 7. 镜头视野 (Camera)

| 功能 | 配置变量 | 说明 |
|------|----------|------|
| **相机漂移** | `qm_camera_drift` | 镜头根据移动速度产生轻微拖拽 |
| **动态 FOV** | `qm_dynamic_fov` | 移动越快视野越宽 |
| **自定义纵横比** | `qm_aspect_*` | 支持多种画面比例 |

### 8. 游戏玩法 (Gameplay)

| 功能 | 配置变量 | 说明 |
|------|----------|------|
| **武器弹道** | `qm_weapon_trajectory` | 显示武器弹道辅助线 |
| **锤子换皮肤** | `qm_hammer_swap_skin` | 锤人时复制对方皮肤 |
| **随机表情** | `qm_random_emote_on_hit` | 被锤/榴弹击中时随机表情 |
| **自动取消旁观** | `qm_auto_unspec_on_unfreeze` | 被解冻时自动退出旁观模式 |

### 9. 资源工坊 (Assets / Workshop)

QmClient 提供分散式的资源管理，皮肤在 "Tee" 页面，其他贴图资源在 "Assets" 页面。

#### 贴图资源类型

| 资源类型 | 配置变量 | Workshop 在线下载 | 本地路径 |
|----------|----------|-------------------|----------|
| **实体层** | `cl_assets_entities` | ✅ | `assets/entities/` |
| **游戏** | `cl_asset_game` | ✅ | `assets/game/` |
| **表情** | `cl_asset_emoticons` | ✅ | `assets/emoticons/` |
| **粒子** | `cl_asset_particles` | ✅ | `assets/particles/` |
| **HUD** | `cl_asset_hud` | ✅ | `assets/hud/` |
| **额外** | `cl_asset_extras` | ❌ | `assets/extras/` |

#### Workshop 系统

- API 端点：`https://www.ddrace.cn/data/assets.json`
- 缩略图缓存：`qmclient/workshop/thumbs/`
- 列表缓存：`qmclient/workshop/workshop_<type>.json`
- 支持在线浏览、一键下载、删除已安装资源

#### 贴图编辑器

Assets 页面底部的 "Assets editor" 按钮，支持从现有资源拖拽组合新贴图、预览、导出。

### 10. Bind 指令系统

QmClient 的 Bind 系统由三个核心组件构成：

| 组件 | 类名 | 职责 | 配置文件 |
|------|------|------|----------|
| **键盘绑定** | `CBinds` | 物理按键到控制台命令的映射 | `settings_ddnet.cfg` |
| **聊天绑定** | `CBindChat` | 聊天输入框中的快捷指令（如 `!shrug`） | `qmclient_chatbinds.cfg` |
| **绑定轮盘** | `CBindWheel` | 鼠标轮盘式快捷指令选择器（默认 Q 键） | `settings_qmclient.cfg` |

#### 默认聊天绑定

| 分组 | 绑定 | 说明 |
|------|------|------|
| **Kaomoji** | `!shrug` `!flip` `!unflip` `!cute` `!lenny` | 颜文字（通过 ChaiScript 脚本） |
| **Warlist** | `!war` `!warclan` `!team` `!teamclan` 等 | 黑名单操作 |
| **Other** | `!translate` `!translateid` `!mute` `!unmute` | 翻译和静音 |

#### 指令工坊 UI

设置页面 TClient 选项卡中的两个 Tab：
- **Chat Binds**：编辑默认绑定名称（⚠️ 暂不支持添加自定义绑定/修改命令/删除）
- **Bind Wheel**：添加/删除/预览绑定轮盘项，圆形轮盘交互

### 11. 网络状态显示

| 功能 | 位置 | 配置变量 | 说明 |
|------|------|----------|------|
| **Ping 显示** | 计分板 / 状态栏 / 名字板圆环 | `cl_enable_ping_color` / `tc_nameplate_ping_circle` | HSL 色彩映射（0-300ms 绿→红） |
| **FPS 显示** | HUD / 状态栏 | `cl_showfps` | 帧率计数器 |
| **预测时间** | HUD / 状态栏 | `cl_showpred` | 客户端预测与服务器时间的偏差（ms） |
| **连接警告** | HUD | 自动 | "Connection Problems..." 文字提示 |

> **当前局限**：Ping 为 RTT 往返延迟，无法区分上行/下行；无丢包率、Jitter、服务器 Tick Rate 等指标；连接问题检测为二值判断（有/无），无中间状态。

---

## QmUi V2 基础设施

QmClient 建立了一套现代化的 UI 基础设施，位于 `src/game/client/QmUi/`：

| 模块 | 核心类 | 职责 |
|------|--------|------|
| **布局引擎** | `CUiV2LayoutEngine` | 类 CSS Flexbox 布局（ROW/COLUMN、AUTO/PX/PERCENT/FLEX、Gap、对齐） |
| **动画运行时** | `CUiV2AnimationRuntime` | Track-based 动画系统（优先级、中断策略、队列、缓动） |
| **节点树** | `CUiV2Tree` | UI 节点层级追踪 |
| **渲染桥接** | `CUiV2RenderBridge` | 渲染统计 |
| **旧版适配** | `CUiV2LegacyAdapter` | `SUiLayoutBox` ↔ `CUIRect` 双向转换 |
| **运行时总管** | `CUiRuntimeV2` | 整合所有子系统 |

### 已集成 QmUi 的组件

- **计分板** (scoreboard.cpp)：Flexbox 布局 + 动画
- **HUD** (hud.cpp)：布局 + 动画值
- **菜单** (menus.cpp)：菜单动画
- **开始菜单** (menus_start.cpp)：Flexbox 布局
- **名字板** (nameplates.cpp)：动画

### 两套动画系统

| 系统 | 类名 | 特点 | 使用场景 |
|------|------|------|----------|
| **V2 动画** | `CUiV2AnimationRuntime` | Track-based、优先级、中断策略、队列 | HUD、计分板、菜单、名字板 |
| **旧版动画** | `CUiEffects` | Index-based、简单插值、Bounce 缓动 | 受 `cl_hud_animations` 控制 |

> **待统一**：三个功能相同的辅助函数（`ResolveAnimatedLayoutValueEx`/`ResolveUiAnimValue`/`ResolveAnimatedValue`）应合并；无统一主题/样式系统。

---

## BC 端功能移植评估

基于 BestClient 功能的移植可行性分析：

| 功能 | 移植难度 | QmClient 现状 | 优先级 |
|------|---------|--------------|--------|
| **Speedrun Timer** | 低 | 基础计时有，缺分段/对比 | ⭐ 高 |
| **Auto Team Lock** | 低 | 无 | ⭐ 高 |
| **Client Indicator** | 中 | 部分实现（中心服 Badge） | 中 |
| **Media Background** | 中高 | 静态图片/地图背景有，无视频 | 中 |
| **3D Particles** | 高 | 仅有 2D CParticles | 低 |

**推荐移植顺序**：Speedrun Timer → Auto Team Lock → Client Indicator → Media Background → 3D Particles

---

## 待完善功能

| 功能 | 状态 | 说明 |
|------|------|------|
| 聊天框翻译按钮 | ❌ 未实现 | 聊天框无鼠标交互基础设施，需从零添加悬停检测和按钮渲染 |
| 出站翻译发送 | ❌ 未完成 | `TryTranslateOutgoingChat()` 解析逻辑已实现，但发送代码被注释 |
| 全局聊天 | ❌ 未实现 | 受限于单服务器连接架构，需独立聊天服务器或 Discord Lobby |
| 服务器/客户端卡顿区分 | ❌ 未实现 | 当前 Ping 为 RTT，无法区分上下行；无丢包率/Jitter/NetGraph |
| Tab 自动补全 BindChat | ⚠️ 未集成 | `ChatDoAutocomplete` 方法已实现但未被调用 |
| Chat Binds UI 增强 | ⚠️ 不足 | 只能编辑默认绑定名称，无法添加自定义绑定/修改命令/删除 |
| 统一主题系统 | ⚠️ 缺失 | 颜色/样式分散在数十个配置变量中，无集中管理 |
| Qimeng 重命名 | ⚠️ 进行中 | 代码中仍有 "Qimeng" 残留引用需统一为 "QmClient" |

---

## 界面说明

### Configs 页面布局

```
┌─────────────────────────────────────────────────────────────┐
│  客户端配置                              [紧凑列表] [仅已修改] │
├─────────────────────────────────────────────────────────────┤
│  [搜索...]  [DDNet] [TClient] [栖梦]                        │
├─────────────────────────────────────────────────────────────┤
│  标签: [视觉效果] [界面显示] [输入优化] [聊天相关] [语音音效]  │
│        [自动] [社交功能] [镜头视野] [游戏玩法] [其他杂项]     │
├─────────────────────────────────────────────────────────────┤
│  ┌───────────────────────────────────────────────────────┐  │
│  │ 📁 Visual                                              │  │
│  │   ┌─────────────────────────────────────────────────┐ │  │
│  │   │ tc_rainbow_mode    [下拉框]          [默认值]   │ │  │
│  │   └─────────────────────────────────────────────────┘ │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### 筛选功能说明

1. **搜索框** - 按配置变量名或描述文本搜索
2. **域筛选按钮** - 切换显示 DDNet / TClient / 栖梦 的配置
3. **紧凑列表** - 以紧凑模式显示配置项
4. **仅已修改** - 只显示已修改过的配置项
5. **标签筛选** - 按功能分类筛选配置项

---

## 文件结构

```
QmClient/
├── src/
│   ├── engine/shared/
│   │   ├── config_domains.h              # 配置域定义（QIMENG/TCLIENT/DDNET）
│   │   ├── config_includes.h             # 配置包含文件
│   │   ├── config_tags.h/.cpp            # Tags 标签系统
│   │   ├── config_variables_qmclient.h   # TClient 配置变量（翻译、状态栏等）
│   │   └── config_variables_qimeng.h     # QIMENG 域配置（视觉效果、HUD 等）
│   ├── game/client/components/qmclient/
│   │   ├── qmclient.h/.cpp              # 核心模块（CTClient 类）
│   │   ├── translate.h/.cpp             # 翻译系统（三后端）
│   │   ├── bindchat.h/.cpp              # 聊天绑定系统
│   │   ├── bindwheel.h/.cpp             # 绑定轮盘
│   │   ├── statusbar.h/.cpp             # 状态栏
│   │   ├── menus_qmclient.cpp           # 设置页面 UI（含翻译/Bind/侧栏等）
│   │   ├── bg_draw.h/.cpp               # 背景描线
│   │   ├── rainbow.h/.cpp               # 彩虹效果
│   │   ├── player_indicator.h/.cpp      # 玩家位置指示器
│   │   ├── lyrics_component.h/.cpp      # 歌词组件
│   │   └── ...
│   ├── game/client/QmUi/
│   │   ├── QmRt.h/.cpp                  # UI 运行时总管
│   │   ├── QmLayout.h/.cpp              # Flexbox 布局引擎
│   │   ├── QmAnim.h/.cpp                # 动画运行时
│   │   ├── QmTree.h/.cpp                # UI 节点树
│   │   ├── QmRender.h/.cpp              # 渲染桥接
│   │   └── QmLegacy.h/.cpp              # 新旧适配器
│   ├── game/client/components/
│   │   ├── chat.h/.cpp                  # 聊天系统（含翻译渲染）
│   │   ├── hud.h/.cpp                   # HUD（含 FPS/Prediction 显示）
│   │   ├── scoreboard.h/.cpp            # 计分板（含 Ping 显示）
│   │   ├── nameplates.h/.cpp            # 名字板（含 Ping 圆环）
│   │   ├── skins.h/.cpp                 # 皮肤管理
│   │   ├── menus_settings_assets.cpp    # 贴图资源页面 + Workshop
│   │   ├── ui_effects.h/.cpp            # 旧版动画系统
│   │   └── ...
│   └── engine/client/
│       ├── friends.h/.cpp               # 好友系统（含自定义分类）
│       └── ...
├── data/qmclient/
│   └── languages/
│       └── simplified_chinese.txt        # 简体中文翻译
└── dyl/
    ├── 栖梦客户端功能报告/
    │   ├── QmClient 说明书.md            # 本说明书
    │   ├── 翻译功能报告.md
    │   ├── UI和动画效果现代化报告.md
    │   ├── 资源页面功能报告.md
    │   ├── 网络卡顿可视化功能报告.md
    │   ├── 好友分类与全局聊天功能报告.md
    │   ├── Bind指令系统功能报告.md
    │   ├── BC端精华功能移植报告.md
    │   └── 聊天框翻译按钮功能报告.md
    ├── CHANGELOG_QmClient.md             # 更新日志
    └── BestClient (BC端) - 参考文档.md   # BC端参考文档
```

---

## 开发指南

### 添加新配置项

1. **在 `config_variables_qimeng.h` 中定义配置变量**

```cpp
MACRO_CONFIG_INT(QmNewFeatureEnable, qm_new_feature_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "启用新功能")
```

2. **在 `config_tags.cpp` 中添加标签匹配规则**（可选）

```cpp
// 为 qm_new_feature_* 配置添加 Gameplay 标签
RegisterTag("qm_new_feature_*", EConfigTag::GAMEPLAY);
```

3. **在语言文件中添加翻译**

```
qm_new_feature_enable == 新功能开关
New feature == 新功能
Enable new feature == 启用新功能
```

### 添加新标签

1. **在 `config_tags.h` 中添加标签枚举**

```cpp
enum class EConfigTag
{
    // ... 现有标签
    NEW_TAG,    // 新标签
    NUM_TAGS
};
```

2. **在 `config_tags.cpp` 中添加标签信息**

```cpp
{SConfigTagInfo{EConfigTag::NEW_TAG, "NewTag", "新标签", "新功能分类"}}
```

3. **在 `config_variables_qmclient.h` 中添加筛选配置**

```cpp
MACRO_CONFIG_INT(ClConfigTagNewTag, cl_config_tag_new_tag, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Filter by NewTag")
```

4. **在语言文件中添加翻译**

```
NewTag == 新标签
```

---

## 参考资源

- [翻译功能报告](./翻译功能报告.md) - 翻译系统详细分析
- [UI和动画效果现代化报告](./UI和动画效果现代化报告.md) - QmUi V2 和动画系统分析
- [资源页面功能报告](./资源页面功能报告.md) - 资源管理和 Workshop 系统分析
- [网络卡顿可视化功能报告](./网络卡顿可视化功能报告.md) - 网络状态显示分析
- [好友分类与全局聊天功能报告](./好友分类与全局聊天功能报告.md) - 好友系统和全局聊天分析
- [Bind指令系统功能报告](./Bind指令系统功能报告.md) - Bind 三组件系统分析
- [BC端精华功能移植报告](./BC端精华功能移植报告.md) - BC 功能移植评估
- [聊天框翻译按钮功能报告](./聊天框翻译按钮功能报告.md) - 聊天框交互分析
- [地图收藏增强功能报告](./地图收藏增强功能报告.md) - 地图收藏系统和浏览器增强分析
- [配置导入导出系统功能报告](./配置导入导出系统功能报告.md) - 配置系统架构和导入导出方案
- [无障碍功能报告](./无障碍功能报告.md) - 色盲模式/高对比度/键盘导航分析
- [自动更新系统功能报告](./自动更新系统功能报告.md) - 现有更新器分析和增强方案
- [钩子反馈时间进度条功能报告](./钩子反馈时间进度条功能报告.md) - 钩子时长进度条和无限钩符号分析
- [聊天框界面增强功能报告](./聊天框界面增强功能报告.md) - 聊天框现代化和 QmUi 动画迁移分析
- [武器切换甩枪动画功能报告](./武器切换甩枪动画功能报告.md) - 武器切换动画和本地/远程同步分析
- [功能头脑风暴报告](./功能头脑风暴报告.md) - 全功能维度头脑风暴和优先级矩阵
- [BestClient (BC端) 参考文档](../BestClient%20(BC端)%20-%20参考文档.md) - BC端功能移植参考
- [CHANGELOG_QmClient.md](../CHANGELOG_QmClient.md) - 更新日志
- [AGENTS.md](../../AGENTS.md) - 项目开发指南

---

## 许可证

QmClient 基于 DDNet / TaterClient 构建，遵循上游项目的许可证。

---

**版本**: 2.0
**更新日期**: 2026-04-19
