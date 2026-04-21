# QmClient BC 端精华功能移植报告

## 一、功能总览

以下是对 QmClient 代码库中 BC (BestClient) 五项功能的详细调研结果，包含当前实现状态、BC 端技术实现细节和移植难度分析。

---

## 二、Client Indicator（客户端指示器）— ✅ 已实现，无需移植

QmClient 已通过中心服实现了客户端识别和标识显示功能，与 BC 的 Client Indicator 功能定位相同，**无需移植**。

- **已有实现**：`QmClientShowBadge`（`qm_client_show_badge`）— 通过中心服识别并标记 QmClient 用户
- **已有实现**：`QmClientMarkTrail`（`qm_client_mark_trail`）— 远程粒子：通过中心服同步并渲染其他玩家

> **注意**：QmClient 的方案基于中心服 WebSocket，BC 基于 UDP Presence + HTTPS，架构不同但功能等价。BC 端技术实现详情保留在下方供参考。

### BC 端技术实现（仅供参考）

BC 的 Client Indicator 是一个**独立的 UDP Presence 系统**，架构复杂，涉及多个子模块：

#### 1. 核心组件：`CClientIndicator`（client_indicator.h/cpp）

继承自 `CComponent`，是整个指示器系统的主控类，负责：

- **UDP Presence Socket 管理**：打开/关闭 UDP 套接字，与 BC Presence Server 通信
- **Presence 状态机**：`DISABLED → ARMED → ACTIVE → COOLDOWN` 四种运行时状态
- **心跳机制**：每 5 秒向 Presence Server 发送心跳包（`PACKET_HEARTBEAT`）
- **本地客户端注册**：跟踪本地玩家（包括 Dummy）的 ClientId，发送 JOIN/LEAVE 包

#### 2. 自定义 UDP 协议：`BestClientIndicator` 命名空间（bestclient_indicator_protocol.h/cpp）

BC 定义了一套完整的二进制 UDP 协议：

| 包类型 | 值 | 方向 | 说明 |
|--------|---|------|------|
| `PACKET_JOIN` | 1 | Client→Server | 玩家加入服务器 |
| `PACKET_HEARTBEAT` | 2 | Client→Server | 定期心跳保活 |
| `PACKET_LEAVE` | 3 | Client→Server | 玩家离开服务器 |
| `PACKET_PEER_STATE` | 4 | Server→Client | 通知某玩家在线 |
| `PACKET_PEER_REMOVE` | 5 | Server→Client | 通知某玩家离线 |
| `PACKET_PEER_LIST` | 6 | Server→Client | 批量推送在线玩家列表 |

**协议细节**：
- Magic: `0x42434931`（"BCI1"）
- Version: 1
- 默认端口: 8778
- 每个包包含：UUID（客户端实例ID）、Nonce、时间戳、服务器地址、玩家名、ClientId
- **SHA-256 Proof 机制**：每个发送包末尾附加 SHA-256 签名，使用 Shared Token 作为密钥，防止伪造

#### 3. Presence Cache：`CPresenceCache`（presence_cache.h）

- 维护当前服务器上 BC 玩家的 ClientId 集合（`std::unordered_set<int>`）
- 按服务器地址分组，切换服务器时自动清空
- 提供 `IsPresent(ClientId)` 查询接口

#### 4. Browser Cache：`CBrowserCache`（browser_cache.h）

- 通过 HTTPS GET 请求获取全局 BC 玩家列表（JSON 格式）
- URL: `https://150.241.70.188:8779/users.json`
- 每 30 秒自动刷新
- 作为 UDP Presence 的降级方案（当 UDP 不可达时）

#### 5. Token Bootstrap

- 通过 HTTPS GET 从 `https://150.241.70.188:8779/token.json` 获取 Shared Token
- 每 60 秒刷新
- Token 用于 UDP 包的 SHA-256 Proof 计算
- 支持配置回退：优先使用 Web Token，回退到 `bc_client_indicator_shared_token` 配置值

#### 6. 名字板渲染：`CNamePlatePartBClientIndicator`（nameplates.cpp）

- 在名字板中渲染 BC 图标（`IMAGE_BCICON`，即 `data/BestClient/bc_icon.png`）
- 支持动态/静态位置模式（`bc_client_indicator_in_name_plate_dynamic`）
- 可配置偏移量（`bc_nameplate_client_indicator_offset_x/y`）
- 可配置大小（`bc_client_indicator_in_name_plate_size`）

#### 7. 配置变量（共 18 个）

```
bc_client_indicator = 1                    # 总开关（强制开启）
bc_client_indicator_in_name_plate = 1      # 名字板显示
bc_client_indicator_in_name_plate_above_self = 0  # 自身上方显示
bc_client_indicator_in_name_plate_size = 30       # 名字板图标大小
bc_client_indicator_in_name_plate_dynamic = 1     # 动态位置
bc_client_indicator_in_scoreboard = 1      # 计分板显示
bc_client_indicator_in_scoreboard_size = 100      # 计分板图标大小
bc_client_indicator_server_address         # UDP Presence 服务器地址
bc_client_indicator_browser_url            # Browser JSON URL
bc_client_indicator_token_url              # Token Bootstrap URL
bc_client_indicator_shared_token           # 手动配置的 Shared Token
bc_nameplate_client_indicator_offset_x/y   # 名字板偏移
dbg_client_indicator = 0                   # 调试日志级别
br_filter_bestclient = 0                   # 服务器浏览器过滤
```

### 移植分析

| 方面 | 分析 |
|------|------|
| **协议层** | 需要完整移植 `bestclient_indicator_protocol.h/cpp`，这是独立的二进制序列化/反序列化库 |
| **网络层** | 需要新增 UDP Socket 管理（`net_udp_create/send/recv`），QmClient 已有这些底层 API |
| **HTTP 层** | 需要新增 HTTPS 请求（Token + Browser Cache），QmClient 已有 `CHttpRequest` |
| **渲染层** | 需要在名字板/计分板中添加图标渲染，QmClient 的名字板系统结构不同 |
| **中心服对比** | QmClient 的 Badge 系统基于中心服 WebSocket，BC 基于 UDP Presence + HTTPS，架构完全不同 |
| **独立部署** | BC 的 Presence Server 是独立部署的（150.241.70.188:8778/8779），移植后需要自建或共用 |

### 结论

Client Indicator 已在 QmClient 中通过中心服 Badge 系统实现，**无需移植**。

---

## 三、3D Particles（3D 粒子效果）

### BC 功能描述

3D 粒子系统，提供更丰富的粒子渲染效果（如深度感、透视变换等）。

### QmClient 当前实现状态：仅有 2D 粒子系统，无 3D 粒子

- **现有粒子系统**：`CParticles`（particles.h/cpp）- 标准 2D 粒子系统
  - 基于精灵图的 2D 粒子渲染，支持 5 个分组，最多 8192 个粒子
  - 渲染方式：使用 `RenderQuadContainerAsSpriteMultiple` 进行批量 2D 精灵渲染
  - 无 3D 透视变换、无深度排序、无 3D 发射器

- **QmClient 脚部粒子**：`QmcFootParticles`（`qmc_foot_particles`）— 仅 2D 脚部粒子效果

### BC 端技术实现详解

BC 的 3D 粒子系统是一个**纯线框 3D 渲染引擎**，不使用精灵图，而是用数学方法绘制 3D 几何体：

#### 1. 核心组件：`C3DParticles`（3d_particles.h/cpp）

继承自 `CComponent`，实现 `OnRender()` 渲染循环。

#### 2. 粒子数据结构：`SParticle`

```cpp
struct SParticle {
    vec3 m_Pos;          // 3D 位置（x, y, z 深度）
    vec3 m_Vel;          // 3D 速度
    vec3 m_Rot;          // 3D 旋转角度（绕 x, y, z 轴）
    vec3 m_RotVel;       // 旋转速度
    ColorRGBA m_Color;   // 颜色
    float m_Size;        // 大小
    vec3 m_SpawnOffset;  // 生成时偏移（用于淡入动画）
    vec3 m_FadeOutOffset;// 消失时偏移（用于淡出动画）
    float m_SpawnTime;   // 生成时间
    float m_FadeOutStart;// 开始淡出时间
    int m_Type;          // 形状类型（Cube/Heart）
    bool m_FadingOut;    // 是否正在淡出
};
```

#### 3. 3D 投影系统

- **透视投影**：`ProjectPoint()` 函数实现简单透视投影
  ```cpp
  Scale = clamp(PROJ_DIST / (PROJ_DIST + Pos.z), 0.5f, 1.6f)
  ```
  `PROJ_DIST = 600.0f`，z 值越大（越远）缩放越小

- **3D 旋转**：`RotateVec3()` 实现绕 Z → X → Y 轴的欧拉角旋转

#### 4. 几何体渲染

**Cube（立方体）**：
- 8 个顶点（单位立方体 ±1）
- 12 条边（线框渲染）
- 使用 `Graphics()->LinesDraw()` 批量绘制线段

**Heart（心形）**：
- 参数方程生成心形曲线：`x = 16sin³(t), y = 13cos(t) - 5cos(2t) - 2cos(3t) - cos(4t)`
- 高分辨率 96 点 + 低分辨率 24 点
- 5 层深度（`HEART_LAYERS = 5`），每层有厚度缩放
- 渲染：环形线 + 层间竖线 + 层间对角线 + 前后面中心辐射线

#### 5. 物理模拟

- **玩家推力**：粒子被玩家位置推开（`PushRadius` + `PushStrength`）
- **粒子碰撞**：O(n²) 碰撞检测，带弹性碰撞响应（`Restitution = 0.6`），仅在粒子数 ≤ 96 时启用
- **速度衰减**：每帧 `Vel *= 0.995`
- **最大速度限制**：`MaxSpeed = Speed * 4.0`

#### 6. 生命周期管理

- **淡入**：生成时从偏移位置滑入 + 缩放弹跳效果（`Pop = 1 + 0.2 * sin(ease * π)`）
- **淡出**：离开视野时触发，向偏移方向滑出 + 缩放衰减
- **配置变更检测**：快照所有配置变量，任何变更触发粒子重置
- **缩放适配**：视野缩放变化时，重新映射粒子位置到新视野

#### 7. 配置变量（共 18 个）

```
bc_3d_particles = 0              # 总开关
bc_3d_particles_type = 1         # 形状（1=Cube, 2=Heart, 3=Mixed）
bc_3d_particles_count = 60       # 粒子数量（1-200）
bc_3d_particles_size_min = 3     # 最小尺寸
bc_3d_particles_size_max = 8     # 最大尺寸
bc_3d_particles_speed = 18       # 基础速度
bc_3d_particles_depth = 300      # 深度范围
bc_3d_particles_alpha = 35       # 透明度（1-100）
bc_3d_particles_fade_in_ms = 400 # 淡入时间
bc_3d_particles_fade_out_ms = 400# 淡出时间
bc_3d_particles_push_radius = 120# 玩家推力半径
bc_3d_particles_push_strength=120# 玩家推力强度
bc_3d_particles_collide = 1      # 粒子碰撞
bc_3d_particles_view_margin = 120# 视野外边距
bc_3d_particles_color_mode = 1   # 颜色模式（1=自定义, 2=随机）
bc_3d_particles_color            # 自定义颜色
bc_3d_particles_glow = 0         # 发光效果
bc_3d_particles_glow_alpha = 35  # 发光透明度
bc_3d_particles_glow_offset = 2  # 发光偏移
```

### 移植分析

| 方面 | 分析 |
|------|------|
| **渲染方式** | 纯线框渲染，使用 `Graphics()->LinesDraw()`，QmClient 的 Graphics API 完全兼容 |
| **数学库** | 使用 `vec3` 和基础三角函数，QmClient 的 `vmath.h` 已有 `vec2`，需扩展 `vec3` |
| **粒子管理** | 使用 `std::vector<SParticle>`，无特殊依赖 |
| **物理系统** | 简单的推力+碰撞，代码量约 100 行，可整体移植 |
| **配置系统** | 18 个配置变量，需添加到 QmClient 的 config 宏定义中 |
| **组件集成** | 需注册为 `CComponent` 子类，QmClient 的组件系统完全兼容 |
| **代码量** | 约 690 行（.cpp）+ 72 行（.h），总计约 760 行 |

### 结论

BC 的 3D 粒子系统**并非真正的 3D 粒子引擎**，而是基于线框几何体 + 简单透视投影的装饰性效果。代码独立性强、依赖少，可以整体移植。主要工作：扩展 `vec3` 类型 + 移植渲染代码 + 添加配置变量。实际难度比预期**低**，因为不需要真正的 3D 引擎。

---

## 四、Speedrun Timer（速通计时器）— ✅ 已移植

### BC 功能描述

独立的速通计时器，提供精确的倒计时功能，在比赛开始时自动启动，时间耗尽时自动杀掉玩家。

### QmClient 当前实现状态：✅ 已移植完成

- **移植版本**：基于 BC 的 `RenderSpeedrunTimer()` 实现，使用 `qm_` 前缀配置变量
- **配置变量**：7 个（`qm_speedrun_timer` 系列），定义在 `config_variables_qimeng.h`
- **设置页面**：栖梦设置 → 速通计时器（独立模块卡片）
- **i18n**：已添加中文翻译

### QmClient 移植实现

#### 1. 修改的文件

| 文件 | 修改内容 |
|------|---------|
| `src/engine/shared/config_variables_qimeng.h` | 添加 7 个 `QmSpeedrunTimer*` 配置变量 |
| `src/game/client/components/hud.h` | 添加 `RenderSpeedrunTimer()` 声明 + `m_SpeedrunTimerExpiredTick` 成员 |
| `src/game/client/components/hud.cpp` | 添加 `FormatSpeedrunTime()` 辅助函数 + `RenderSpeedrunTimer()` 实现 |
| `src/game/client/components/qmclient/menus_qmclient.cpp` | 添加「速通计时器」设置模块卡片 |
| `data/languages/simplified_chinese.txt` | 添加中文翻译 |

#### 2. 与 BC 端的差异

| 方面 | BC 端 | QmClient 端 |
|------|-------|-------------|
| 配置前缀 | `bc_speedrun_timer*` | `qm_speedrun_timer*` |
| 组件禁用检查 | `m_BestClient.IsComponentDisabled(COMPONENT_GAMEPLAY_SPEEDRUN_TIMER)` | 无（QmClient 无组件掩码系统） |
| 等待状态 | 显示 "Waiting..." 灰色文字 | 不显示（简化） |
| 设置 UI | `menus_bestclient.cpp` | 栖梦设置模块卡片系统 |

#### 3. 使用方法

1. 栖梦设置 → 速通计时器 → 勾选「启用速通计时器」
2. 设置目标时间：小时 / 分钟 / 秒 / 毫秒
3. 可选：勾选「时间耗尽后自动禁用」
4. 进入服务器后，比赛开始时自动倒计时，时间耗尽自动 Kill

### BC 端技术实现详解

BC 的 Speedrun Timer 是一个**倒计时器**，嵌入在 `CHud` 组件中，实现非常简洁：

#### 1. 核心逻辑：`CHud::RenderSpeedrunTimer()`（hud.cpp）

**计时原理**：
- 用户通过配置变量设置目标时间（小时+分钟+秒+毫秒）
- 兼容旧版 MMSS 格式（`bc_speedrun_timer_time`）
- 计算总毫秒数 → 转换为 Tick 数 → 用当前 Race Tick 减去得到剩余时间

**关键代码流程**：
```
1. 计算配置的总毫秒数
2. 检查比赛是否开始（GAMESTATEFLAG_RACETIME + WarmupTimer < 0）
3. 比赛未开始 → 显示 "Waiting..." 灰色文字
4. 比赛进行中 → 计算已用 Tick → 计算剩余毫秒数
5. 剩余时间 ≤ 0 → 发送 Kill 命令 + 可选自动禁用
6. 剩余时间 ≤ 60秒 → 红色警告
7. 正常显示 → 格式化时间字符串
```

**时间格式化**：`FormatSpeedrunTime()`
- 有小时：`HH:MM:SS.mmm`
- 无小时：`MM:SS.mmm`

#### 2. 过期处理

- 时间耗尽时调用 `GameClient()->SendKill()` 杀掉玩家
- 设置 `m_SpeedrunTimerExpiredTick` 记录过期时刻
- 显示红色 "TIME EXPIRED!" 文字 5 秒
- 可选自动禁用计时器（`bc_speedrun_timer_auto_disable`）
- 比赛重新开始时重置过期状态

#### 3. 配置变量（共 7 个）

```
bc_speedrun_timer = 0              # 总开关
bc_speedrun_timer_time = 0         # 旧版 MMSS 格式时间（兼容）
bc_speedrun_timer_hours = 0        # 小时
bc_speedrun_timer_minutes = 0      # 分钟
bc_speedrun_timer_seconds = 0      # 秒
bc_speedrun_timer_milliseconds = 0 # 毫秒
bc_speedrun_timer_auto_disable = 0 # 时间耗尽自动禁用
```

#### 4. 关联功能：Finish Prediction（完成预测）

BC 还有一个与速通计时器配合的**完成预测**功能：

```
bc_finish_prediction = 0                    # 总开关
bc_finish_prediction_show_always = 1        # 比赛前也显示
bc_finish_prediction_time_mode = 0          # 0=剩余时间, 1=预测完成时间
bc_finish_prediction_show_time = 1          # 显示时间
bc_finish_prediction_show_percentage = 1    # 显示进度百分比
bc_finish_prediction_show_millis = 1        # 显示毫秒
```

Finish Prediction 基于地图路径分析，预测玩家完成时间，代码较复杂（涉及路径距离计算、计分板时间获取等）。

### 移植分析

| 方面 | 分析 |
|------|------|
| **代码量** | Speedrun Timer 核心约 80 行，极其精简 |
| **依赖** | 仅依赖 `CHud`、`CGameClient::SendKill()`、`GameTick()` 等已有 API |
| **配置** | 7 个配置变量，直接添加即可 |
| **渲染** | 简单的文本渲染，QmClient 的 HUD 系统完全兼容 |
| **Finish Prediction** | 可选移植，代码量较大，建议先只移植 Speedrun Timer |
| **注意** | BC 的速通计时器是**倒计时**而非正计时，与 QmClient 现有的正计时功能互补 |

### 结论

Speedrun Timer 是**最容易移植**的功能，核心代码仅约 80 行，无外部依赖，直接嵌入 `CHud` 即可。建议同时移植 Finish Prediction 以提供更完整的速通体验。

---

## 五、Media Background（媒体背景）

### BC 功能描述

支持视频背景、动画背景或自定义图片背景，替代默认的地图背景。支持菜单背景和游戏内背景两种模式。

### QmClient 当前实现状态：有静态图片/地图背景，无视频/动画背景

- **现有背景系统**：
  1. **CBackground**（background.h/cpp）- 支持 `.map` 地图文件和 `.png` 静态图片作为背景
  2. **CMenuBackground**（menu_background.h/cpp）- 菜单背景，支持主题切换和相机旋转动画
  3. **CBgDraw**（bg_draw.h）- 背景描线功能（非媒体背景）

- **缺少的功能**：
  - 无视频背景播放
  - 无动画 GIF/WebP 背景
  - 无实时媒体流背景

### BC 端技术实现详解

BC 的 Media Background 由两个核心模块组成：

#### 1. 媒体解码器：`MediaDecoder` 命名空间（media_decoder.h/cpp）

**架构设计**：
- `SMediaFrame`：已上传到 GPU 的纹理帧（`CTextureHandle` + 持续时间）
- `SMediaRawFrame`：CPU 侧的原始图像帧（`CImageInfo` + 持续时间）
- `SMediaDecodedFrames`：解码后的帧集合（支持动画/静态）
- `SMediaDecodeLimits`：解码限制（最大尺寸、帧数、内存、时长）

**支持的格式**：
- **静态图片**：PNG（优先使用引擎内置 PNG 加载器）、其他格式通过 FFmpeg 解码
- **动画图片**：GIF、WebP、APNG、AVIF（通过 FFmpeg 解码所有帧）
- **视频**：MP4、WebM、MOV、M4V、MKV、AVI、GIFV、MPG、MPEG、OGV、3GP、3G2

**FFmpeg 集成**：
- 使用 `libavcodec`、`libavformat`、`libswscale` 三个库
- 内存读取模式：通过自定义 `AVIOContext`（`CFfmpegMemoryReader`）从内存缓冲区读取数据
- 像素格式转换：`sws_scale()` 将任意像素格式转为 RGBA
- 动画帧时间计算：优先使用 `AVFrame::duration`，回退到 PTS 差值，再回退到帧率推算
- 内存保护：最大 64MB 动画内存、最大 120 帧、最大 4096px 尺寸
- 帧合并策略：超出限制时合并相邻帧对（时长相加）以减少帧数

**关键函数**：
- `DecodeImageToRgba()`：解码单帧到 RGBA（用于聊天媒体预览）
- `DecodeStaticImage()`：解码静态图片
- `DecodeAnimatedImage()`：解码动画图片（所有帧）
- `DecodeImageWithFfmpegCpu()`：核心 FFmpeg CPU 解码器
- `GetCurrentFrameTexture()`：根据当前时间获取动画帧纹理

#### 2. 菜单媒体背景：`CMenuMediaBackground`（menu_media_background.h/cpp）

**注意**：此类**不继承** `CComponent`，而是作为 `CBackground` 的成员使用。

**两种模式**：

**静态媒体模式**（`LoadStaticMedia`）：
- PNG：优先使用引擎 `LoadPng`，失败则用 FFmpeg
- 动画图片：先尝试 `DecodeAnimatedImage`，失败回退 `DecodeStaticImage`
- 非动画图片：先尝试 `DecodeStaticImage`，失败回退 `DecodeAnimatedImage`
- 所有格式都失败时：尝试用 FFmpeg 直接从文件解码第一帧（`DecodeFirstFrameFromFile`）

**视频模式**（`LoadVideo`）：
- 使用 FFmpeg 打开视频文件
- 查找最佳视频流（`av_find_best_stream`）
- 初始化解码器、缩放器、帧缓冲
- 逐帧解码 + 实时上传纹理

**视频播放流程**：
```
1. avformat_open_input → avformat_find_stream_info
2. av_find_best_stream → avcodec_find_decoder
3. avcodec_alloc_context3 → avcodec_open2
4. 循环：av_read_frame → avcodec_send_packet → avcodec_receive_frame
5. sws_scale（像素格式转换）→ 上传到 GPU 纹理
6. EOF 时 av_seek_frame 回到开头（循环播放）
```

**性能优化**：
- 帧率限制：最高 120 FPS（`MENU_MEDIA_MAX_VIDEO_FRAME_MS = 250`）
- 解码节流：每 tick 最多解码 2 帧
- 非阻塞更新：`CSubsystemTicker::ShouldRunPeriodic()` 控制更新频率
- 性能监控：记录每次更新耗时，支持 debug 输出

**渲染**：
- `RenderTextureCover()`：Cover 模式填充（保持宽高比，裁剪溢出部分）
- 支持世界偏移模式：游戏内背景可部分跟随地图相机（`bc_game_media_background_offset`）

#### 3. 与 CBackground 的集成

`CBackground` 类中包含 `CMenuMediaBackground m_MediaBackground` 成员：
- 菜单背景：直接渲染媒体（无地图偏移）
- 游戏背景：带世界偏移渲染（媒体部分固定到地图位置）

#### 4. 配置变量（共 4 个）

```
bc_menu_media_background = 0          # 菜单媒体背景开关
bc_game_media_background = 0          # 游戏媒体背景开关
bc_menu_media_background_path         # 媒体文件路径
bc_game_media_background_offset = 0   # 游戏背景世界偏移（0-100）
```

### 移植分析

| 方面 | 分析 |
|------|------|
| **FFmpeg 依赖** | QmClient 已有 FFmpeg（`VIDEORECORDER` 选项），链接库已存在 |
| **MediaDecoder** | 约 920 行独立代码，可直接移植，无 QmClient 特定依赖 |
| **CMenuMediaBackground** | 约 730 行，依赖 `IGraphics` 和 `IStorage` 接口，QmClient 兼容 |
| **CBackground 集成** | 需要修改 QmClient 的 `CBackground` 类，添加 `CMenuMediaBackground` 成员 |
| **渲染适配** | Cover 模式渲染逻辑简单，QmClient 的 Graphics API 完全兼容 |
| **总代码量** | MediaDecoder（920行）+ MenuMediaBackground（730行）≈ 1650 行 |
| **风险点** | FFmpeg 版本兼容性、内存管理（大视频帧）、性能影响 |

### 结论

Media Background 的技术方案成熟，BC 已完整实现视频/动画/静态图片的统一解码管线。QmClient 已有 FFmpeg 依赖，移植的主要工作是：移植 `MediaDecoder` + `CMenuMediaBackground` + 修改 `CBackground` 集成。代码量中等（约 1650 行），但功能完整且实用。

---

## 六、Auto Team Lock（自动队伍锁定）— ✅ 已移植

### BC 功能描述

当玩家加入一个可锁定的队伍时，自动在延迟后发送 `/lock 1` 命令锁定队伍，防止其他玩家加入。

### QmClient 当前实现状态：✅ 已移植完成

- **移植版本**：基于 BC 的 `UpdateAutoTeamLock()` 实现，使用 `qm_` 前缀配置变量
- **配置变量**：2 个（`qm_auto_team_lock` + `qm_auto_team_lock_delay`），定义在 `config_variables_qimeng.h`
- **设置页面**：栖梦设置 → HJ 大佬辅助 → 自动锁队
- **i18n**：已添加中文翻译

### QmClient 移植实现

#### 1. 修改的文件

| 文件 | 修改内容 |
|------|---------|
| `src/engine/shared/config_variables_qimeng.h` | 添加 2 个 `QmAutoTeamLock*` 配置变量 |
| `src/game/client/gameclient.h` | 添加 `UpdateAutoTeamLock()` 声明 + 3 个成员变量 |
| `src/game/client/gameclient.cpp` | 添加 `UpdateAutoTeamLock()` 实现 + OnReset/OnNewSnapshot 调用 |
| `src/game/client/components/qmclient/menus_qmclient.cpp` | 在 HJAssist 模块中添加自动锁队 UI |
| `data/languages/simplified_chinese.txt` | 添加中文翻译 |

#### 2. 与 BC 端的差异

| 方面 | BC 端 | QmClient 端 |
|------|-------|-------------|
| 配置前缀 | `bc_auto_team_lock*` | `qm_auto_team_lock*` |
| 组件禁用检查 | `m_BestClient.IsComponentDisabled(COMPONENT_GAMEPLAY_AUTO_TEAM_LOCK)` | 无（QmClient 无组件掩码系统） |
| 设置 UI | `menus_bestclient.cpp` | 栖梦设置 HJAssist 模块卡片 |

#### 3. 使用方法

1. 栖梦设置 → HJ 大佬辅助 → 勾选「自动锁队」
2. 设置「锁队延迟」（默认5秒，范围0-30秒）
3. 加入可锁定队伍（team 1~63）时，延迟到期后自动执行 `/lock 1`

### BC 端技术实现详解

BC 的 Auto Team Lock 实现极其简洁，直接嵌入在 `CGameClient` 中：

#### 1. 核心逻辑：`CGameClient::UpdateAutoTeamLock()`（gameclient.cpp）

**算法**：
```
1. 获取当前 Dummy 的 ClientId 和 Team
2. 检查功能是否启用（bc_auto_teamLock + 组件掩码）
3. 检测队伍变化：
   - 从不可锁定队伍 → 可锁定队伍：设置延迟锁定定时器
   - 从可锁定队伍 → 不可锁定队伍：取消定时器
   - 队伍未变化：保持定时器
4. 定时器到期：发送 "/lock 1" 聊天命令
```

**关键判断**：
- 可锁定队伍：`Team > TEAM_FLOCK && Team < TEAM_SUPER`（即队伍 1-63）
- 不可锁定队伍：`TEAM_FLOCK`（0，公共队）和 `TEAM_SUPER`（64+，超级队）

#### 2. 数据成员（gameclient.h）

```cpp
int m_aAutoTeamLockLastTeam[NUM_DUMMIES];       // 上次记录的队伍号
int64_t m_aAutoTeamLockDeadlineTick[NUM_DUMMIES]; // 锁定截止 Tick
bool m_aAutoTeamLockPending[NUM_DUMMIES];        // 是否有待执行的锁定
```

#### 3. 调用时机

在 `CGameClient::OnNewSnapshot()` 中调用，每次收到服务器快照时检查。

#### 4. 重置时机

- `OnReset()`：重置所有状态
- 客户端离线时：重置所有状态
- ClientId 无效时：重置当前 Dummy 的状态

#### 5. 配置变量（共 2 个）

```
bc_auto_team_lock = 0          # 总开关
bc_auto_team_lock_delay = 5    # 延迟秒数（0-30）
```

### 移植分析

| 方面 | 分析 |
|------|------|
| **代码量** | 核心逻辑约 55 行，是所有功能中最少的 |
| **依赖** | 仅依赖 `m_Teams.Team()`、`m_Chat.SendChat()`、`Client()->GameTick()` |
| **数据** | 3 个数组（每个 Dummy 一份），共 6 个成员变量 |
| **配置** | 2 个配置变量 |
| **集成点** | 在 `OnNewSnapshot()` 中调用，QmClient 有相同的快照处理流程 |
| **注意** | 发送的是 `/lock 1` 聊天命令，需要服务器支持该命令 |

### 结论

Auto Team Lock 是**最简单**的移植功能，核心代码仅约 55 行，3 个数组成员 + 2 个配置变量。可以直接在 QmClient 的 `CGameClient` 中添加相同逻辑。

---

## 七、总结对比表

| 功能 | BC 配置前缀 | QmClient 现状 | BC 代码量 | 移植难度 | 关键依赖 | 优先级建议 |
|------|------------|--------------|----------|---------|---------|-----------|
| **Client Indicator** | `bc_client_indicator_*` | ✅ 已实现（中心服 Badge） | — | — | — | 无需移植 |
| **3D Particles** | `bc_3d_particles` | 无（仅有 2D CParticles） | ~760行 | 中 | vec3 扩展 | 中 |
| **Speedrun Timer** | `bc_speedrun_timer` | ✅ 已移植（`qm_speedrun_timer*`） | ~80行 | **低** | 无 | ~~高~~ 已完成 |
| **Media Background** | `bc_media_background` | 静态图片/地图背景有，无视频 | ~1650行 | 中 | FFmpeg（已有） | 中 |
| **Auto Team Lock** | `bc_auto_team_lock` | ✅ 已移植（`qm_auto_team_lock*`） | ~55行 | **低** | 无 | ~~高~~ 已完成 |

**移植进度**：2/4 完成（Client Indicator 无需移植，不计入）

**推荐移植顺序**：
1. ~~**Auto Team Lock** — 代码量最少（55行），无任何依赖，5 分钟可完成~~ ✅ 已完成
2. ~~**Speedrun Timer** — 代码量极少（80行），无依赖，实用功能~~ ✅ 已完成
3. **3D Particles** — 代码量中等（760行），独立性强，视觉效果好，实际难度比预期低
4. **Media Background** — 代码量最大（1650行），FFmpeg 已有，功能完整但工作量大

---

## 八、附录：BC 端源文件索引

| 功能 | 源文件路径（相对于 BestClient/src/） |
|------|-------------------------------------|
| **Client Indicator** | `game/client/components/bestclient/clientindicator/client_indicator.h/cpp` |
| Client Indicator 协议 | `engine/shared/bestclient_indicator_protocol.h/cpp` |
| Presence Cache | `game/client/components/bestclient/clientindicator/presence_cache.h/cpp` |
| Browser Cache | `game/client/components/bestclient/clientindicator/browser_cache.h/cpp` |
| Sync Helper | `game/client/components/bestclient/clientindicator/client_indicator_sync.h` |
| BC 图标资源 | `data/BestClient/bc_icon.png` |
| **3D Particles** | `game/client/components/bestclient/3d_particles.h/cpp` |
| **Speedrun Timer** | `game/client/components/hud.cpp`（`RenderSpeedrunTimer()` 函数） |
| Finish Prediction | `game/client/components/hud.cpp`（`RenderFinishPrediction()` 函数） |
| **Media Background** | `game/client/components/menu_media_background.h/cpp` |
| Media Decoder | `game/client/components/media_decoder.h/cpp` |
| Background 集成 | `game/client/components/background.h/cpp` |
| **Auto Team Lock** | `game/client/gameclient.cpp`（`UpdateAutoTeamLock()` 函数） |
| **BC 配置变量** | `engine/shared/config_variables_bestclient.h` |
| **BC 组件系统** | `game/client/components/bestclient/bestclient.h/cpp`（`EBestClientComponent` 枚举 + `IsComponentDisabled()` 掩码） |

---

## 九、附录：QmClient 端已移植功能源文件索引

| 功能 | 源文件路径（相对于 QmClient/src/） | 配置变量前缀 |
|------|-------------------------------------|-------------|
| **Auto Team Lock** | `game/client/gameclient.h/cpp`（`UpdateAutoTeamLock()`） | `qm_auto_team_lock*` |
| Auto Team Lock 配置 | `engine/shared/config_variables_qimeng.h` | |
| Auto Team Lock 设置 UI | `game/client/components/qmclient/menus_qmclient.cpp`（HJAssist 模块） | |
| **Speedrun Timer** | `game/client/components/hud.h/cpp`（`RenderSpeedrunTimer()`） | `qm_speedrun_timer*` |
| Speedrun Timer 配置 | `engine/shared/config_variables_qimeng.h` | |
| Speedrun Timer 设置 UI | `game/client/components/qmclient/menus_qmclient.cpp`（SpeedrunTimer 模块） | |
| 中文翻译 | `data/languages/simplified_chinese.txt` | |
