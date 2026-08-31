# 多音乐客户端歌词获取链路调研

> 状态:调研已完成(酷狗/汽水/QQ 收口;Spotify 调研收口,实现待排期)
> 目标:为 QmClient 增加酷狗音乐、汽水音乐、QQ 音乐、Spotify 四客户端的歌词获取能力,复用网易云歌词模块的架构与交付格式:统一的歌曲信息、完整歌词、逐字时间轴、翻译数据,不优先做 UI 层 Hook。

## 总评估(四方案结论)

| 客户端 | 用户方案 | 评估 | 落地建议 |
|---|---|---|---|
| 酷狗 | hash + KRC 本地缓存,不足则进程内对象 | 合理 | 本地 `<hash>.krc` 缓存 + 解密(密钥表已确认)+ API 兜底;hash 来源用注入/本地库 |
| 汽水 | 确认本地缓存,无则 DLL 注入观察已解析数据 | 方向合理,手段修正 | 无本地缓存;社区成熟路径是 **CDP(Node inspector 9229)** 读 sharedState 一次拿全,比 DLL 注入稳 |
| QQ 音乐 | .qm.qrc 本地缓存解码解析 + 歌曲匹配,不足则 DLL 注入 | 合理,需调整顺序 | QRC 解密链已实测;本地缓存 + 网络 API 兜底,先做纯数据链路,注入作最后手段 |
| Spotify | 自提方案:官方内置 color-lyrics 接口 + sp_dc 鉴权 | 合理,已有开源实现可移植 | 以 Lyricify-Lyrics-Helper 开源实现为参考:sp_dc + TOTP → color-lyrics 主源,LRCLIB 兜底;翻译取 alternatives[];SMTC 身份 + OAuth 预留 |

统一交付:`QmMusicLyrics::SLyricsData`(歌曲信息 + STimeline 行/词时间轴 + 翻译轨),复用 `NeteaseLyrics` 的解析/时间轴模型。

## 实现进度

### 已完成(2026-08-30)

1. **KRC 模块**(`src/game/client/components/qmclient/music_lyrics/music_lyrics_krc.{h,cpp}`):
   - `DecryptKrc`:魔数校验 → 标准 64 字节密钥表 / 旧版 16 字节密钥表(双尝试)→ zlib inflate → BOM 剥离。
   - `ParseKrcText`:兼容 `[mm:ss.xx]`(LRC 风格)与 `[startMs,durationMs]`(毫秒对,PlayerCap 实证格式)两种行头;词标记 `<start,dur>` / `<start,dur,0>`;词偏移相对行起点。
   - `ExtractKrcTranslation`:`[language:base64json]` 内嵌翻译轨(PlayerCap 实证结构:type=1 中文、type=0 罗马音,按行序号对齐)。
   - `ParseKrcData`:组合入口,填充 SLyricsData。
2. **QRC 模块**(`music_lyrics_qrc.{h,cpp}`):
   - 11 字节魔数 → QMC1 XOR(128 字节密钥,全局偏移)→ 自定义 3DES-EDE(密钥 `!@#)(*$%123ZXC!@!@#)(NHL`)→ zlib inflate。
   - `ExtractQrcLyricContent`:XML 属性提取 + 实体反转义。
   - `ParseQrcRlrc`:rlrc 逐字文本(`[行起始ms,行时长ms]字(绝对偏移ms,时长ms)`),词偏移为**绝对时间戳**。
   - 算法已用真实样本(tmp/sample_qm.qrc / sample_qmts.qrc)实测复现验证。
3. **汽水音乐 Hook**(`src/qm-soda-hook/` + `music_lyrics/qm_soda_*`):
   - `qm_soda_watchdog`:SodaMusic.exe 主进程发现(无 `--type=`)+ 复刻 `process._debugProcess` 激活 Node inspector(9229),绕过原生反调试。
   - `qm_soda_probe`:rendererMain 探针脚本(MessagePort 抓取 sharedState.get('player'))+ 主进程 executeJavaScript 桥表达式 + 结果解析。
   - `qm_soda_protocol`:共享内存 seqlock 协议(歌曲身份/进度/歌词文件路径)。
   - `qm_soda_writer`:Helper 侧共享内存发布。
   - `qm-soda-helper.exe`:发现 → 激活 → CDP 连接 → 300ms 轮询提取 → JSON 歌词文件(原子写)→ 共享内存发布。
   - 客户端侧 `CQmSodaHookProvider`(共享内存读取)+ `QmSodaLyricFile`(JSON 歌词文件 → SLyricsData,翻译轨按时间戳对齐)。
   - 复用网易云 `CCdpSession`,新增 `EvaluateAwaitPromise`(awaitPromise=true 用于 executeJavaScript 桥)。
4. **配置**:`qm_soda_hook_enable` / `qm_soda_hook_timeout_ms` / `qm_soda_hook_helper_path`。
5. **测试**:36+ 用例全部通过(music_lyrics_krc / music_lyrics_qrc / qm_soda_probe / qm_soda_protocol / qm_soda_lyric_file)。

### 待办

- 客户端侧集成:在 CSystemMediaControls 或独立组件中管理 soda provider 生命周期(启停/读取),并接入 HUD 歌词岛。
- 酷狗/QQ 歌曲身份获取(当前播放 hash / songmid)与统一数据通道接入。
- 酷狗/QQ 的缓存扫描 + 网络 API 兜底链路。
- Spotify:已实现(qm_spotify_{crypto,token,parser,integration} + 21 单测 + gate quick 通过;TOTP 链路已线上实证 401=通过);设置页 UI 已接入(QmMusicHookRegistry 注册 + sp_dc 输入框),客户端完整构建链接通过。
- `qmclient_scripts/tests/test_netease_package_contract.py` 扩展覆盖 qm-soda-helper 打包。
- gate 验证(quick/default)。

## 背景:网易云现有架构(模板)

网易云走「启动注入 + CDP 前端桥 + 共享内存 v5」路线,代码位于 `src/qm-nmt-hook/` 与 `src/game/client/components/qmclient/netease/`:

1. `qm-nmt-bootstrap.dll`(version.dll 代理)部署到网易云安装目录,启动期改 PEB 命令行加 `--remote-debugging-address=127.0.0.1 --remote-debugging-port=<动态端口>`,并 IAT hook GetCommandLineW/A。
2. `qm-nmt-helper.exe --watch` 发现 cloudmusic.exe 主进程 → 注入 `qm-nmt-hook64.dll`(CreateRemoteThread + LoadLibraryW)→ 启动 CDP WebSocket 前端桥。
3. hook64 DLL:GDI+ 桌面歌词窗口 hook(仅当前句,fallback)、waveOut 播放进度 hook、本地 `%LOCALAPPDATA%\NetEase\CloudMusic\webdata\file\playingList` / `lastTimePlayingList` 曲库解析、SMTC 兜底、封面下载缓存。
4. CDP 前端桥(`qm_netease_cdp_client.cpp` 的 JS hook):webpack require 捕获 → store.getStore() → playing / async:lyric → 序列化整首 LRC → 报告 JSON {kind, songId, positionMs, playing, rawLyrics{lrc,yrc}}。
5. `CFrontendLyricBridge`:解析报告 → YRC/LRC 时间轴 → 100ms tick 选当前句 → 共享内存 v5 发布(songId/generation/position/当前句/行边界/来源)。
6. QmClient 侧:`CQmNeteaseHookProvider`(共享内存读取)→ `CNeteaseIntegration`(状态机)→ HUD 歌词岛显示。

关键设计:双槽 seqlock 共享内存、校验和 + 版本校验、generation 防旧快照、GDI fallback 不能覆盖 Frontend 高优先级、UTF-8 边界截断。

### 网易云各文件职责(新客户端实现模板)

| 文件 | 职责 |
|---|---|
| `qm_netease_bootstrap.{h,cpp}` | version.dll 代理的部署/升级/冲突检测;PEB 命令行补丁(只加 `--remote-debugging-port`,只接受 loopback);动态端口候选序列 |
| `qm_netease_version_proxy.{cpp,def}` | version.dll 导出转发(GetFileVersionInfo* / VerQueryValue* 等),启动期 IAT hook GetCommandLineW/A |
| `qm_netease_hook_helper.cpp` | `qm-nmt-helper.exe --watch`:进程发现(ExeFile 匹配 + 非 renderer + 父进程判断)、注入(CreateRemoteThread+LoadLibraryW)、bootstrap 安装/提权重试、FrontendBridge 生命周期 |
| `qm_netease_hook_dll.cpp` | `qm-nmt-hook64.dll`:GDI+ 桌面歌词文本捕获(fallback,仅当前句)、waveOut 播放进度、本地曲库文件解析(playingList/lastTimePlayingList JSON)、SMTC 兜底、封面下载缓存、v4/v5 共享内存发布 |
| `qm_netease_cdp.{h,cpp}` | CDP target 解析(仅 loopback Orpheus target,json-parser)、端口/命令行解析、trusted target 校验(监听端口 owner = 目标 PID) |
| `qm_netease_cdp_client.{h,cpp}` | WinHTTP 发现 /json、Winsock WebSocket(RFC6455)、CCdpSession(Runtime.evaluate/addBinding/addScriptToEvaluateOnNewDocument)、CFrontendBridgeWorker(连接循环 + JS hook 注入 + 2s 轮询)、BuildInstallHookScript(webpack require → store → playing/lyric → 报告 JSON) |
| `qm_netease_frontend_bridge.{h,cpp}` | 报告 JSON 解析(kind=progress/lyrics, songId, positionMs, playing, rawLyrics{lrc,yrc})、YRC/LRC 时间轴、SwitchSongLocked generation 推进、100ms tick 选当前句、v5 发布(含 GDI fallback 保护) |
| `netease_lyric_parser.{h,cpp}` | LRC(行级 [mm:ss.xx])与 YRC(行级 [start,duration] + 词级 (offset,duration))解析 → STimeline;UTF-8 严格校验 |
| `netease_lyric_timeline.{h,cpp}` | SelectCurrentLine(binary search)、AreTimelinesEquivalent、SPlaybackAnchor 时钟外推、SGenerationState |
| `netease_lyric_state.{h,cpp}` | CLyricState 状态机:切歌清空、来源优先级、暂停保留窗口、Bridge 身份阻断、progress 新鲜度 |
| `netease_integration.{h,cpp}` | CNeteaseIntegration 组件:消费 SMTC + v5 快照,维护展示状态,GetCurrentLyric 供 HUD |
| `netease_shared_memory.{h,cpp}` | 客户端侧 v5 共享内存 seqlock 读取 |
| `netease_hook/qm_netease_hook_protocol.{h,cpp}` | 固定布局 ABI:SSnapshot(v4)/SSnapshotV5/v5 共享块、CRC32 校验、Validate/Finalize、IsStale、UTF-8 截断 |
| `netease_hook/qm_netease_hook_v5_writer.{h,cpp}` | Helper 侧 v5 writer:CreateFileMapping/OpenFileMapping、writer mutex、seqlock 发布 |
| `netease_hook/qm_netease_hook_provider.{h,cpp}` | 客户端侧 provider:启动 helper 子进程、v4/v5 共享内存读取、协作式停止 |
| `netease_hook/qm_netease_hook_metadata.h` | 窗口标题归一化匹配、本地曲库元数据填充 snapshot、WaveOut 位置换算 |

v5 快照字段:Magic/SchemaVersion/SnapshotSize/Sequence/CloudMusicPid/Flags/SongId/Generation/LyricSource/PositionMs/LineStartMs/LineEndMs/UpdatedAtTick/CurrentLyric[1024]/Checksum。Flags: HAS_SONG/LYRIC_VALID/POSITION_VALID/PLAYING_HINT/POSITION_ANCHORED/LYRIC_TIMELINE_VALID。

## 酷狗音乐(KuGou)

### 结论

- 本地歌词缓存:**存在**,文件名 = `<歌曲hash>.krc`,随播放/下载落盘;路径随大版本变化(`%APPDATA%\KuGou7|8|9\Temp\` 等,用户可改缓存目录),不能硬编码。
- KRC 格式:**已明确**。
  - 魔数 `krc1`(偏移 0);偏移 4 为小端长度(可选);偏移 8 为密钥标志(0=旧版 64 字节密钥表,1=新版前 1024 字节用 1024 字节密钥表)。
  - XOR 解密(64 字节密钥表已给出,见下)→ zlib inflate → UTF-8 LRC 风格文本。
  - 逐字时间轴:行 `[mm:ss.xx]`,行内 `<start,dur>text`,`start` 为相对行起始的毫秒偏移。
- 当前歌曲 hash 获取:**无官方本地文件**。社区主路径为内存读取(精易论坛易语言)或注入(TaskbarLyrics);本地数据库(播放历史)可作为备选;窗口标题 + 搜索 API 为兜底。
- 翻译:独立 lyric id 的 KRC(另一条记录),或新版内嵌 `[language:base64json]` 轨(PlayerCap 实证:base64 解码后 JSON `{"content":[{"lyricContent":[["译文片段"...]],"type":1}]}`,type=1 为中文翻译、type=0 为罗马音,按行序号与主歌词对齐);需实测目标版本。
- 网络兜底:hash → `lyrics.kugou.com/search?hash=` → id/accesskey → `lyrics.kugou.com/download?fmt=krc` → base64 content。端点版本相关。
- **格式佐证(PlayerCap 真机验证)**:KRC 明文行格式为 `[行起始ms,行时长ms]`(毫秒对),词标记 `<相对偏移ms,时长ms,0>`(三字段,第三字段固定 0);汽水音乐也直接给这份明文 KRC。这与本仓库旧实现一致,与部分资料说的 `[mm:ss.xx]` 不同——解析器需两种行头兼容(已实现)。
- **历史残留**:仓库旧版 `qm_lyrics_source_kugou.cpp`(已删除)用 16 字节 XOR 密钥 `0x40,0x47,0x61,0x77,0x5E,0x32,0x74,0x47,0x51,0x36,0x31,0x2D,0xCE,0xD2,0x6E,0x69`,从偏移 4 开始 XOR,行格式 `[start,dur]` + `<offset,dur,0>词`,与标准 64 字节表不同 → 落地时需两种变体兼容。

### 64 字节密钥表(标准)

```text
0x40, 0x57, 0x7D, 0x24, 0x30, 0x16, 0x0B, 0x35, 0x13, 0x11, 0x09, 0x00, 0x1C, 0x30, 0x3B, 0x3A,
0x18, 0x27, 0x03, 0x1D, 0x2D, 0x2B, 0x37, 0x05, 0x11, 0x04, 0x2E, 0x37, 0x2D, 0x1A, 0x0B, 0x11,
0x2A, 0x01, 0x0B, 0x35, 0x08, 0x32, 0x0E, 0x0A, 0x2D, 0x36, 0x05, 0x07, 0x06, 0x0A, 0x1A, 0x25,
0x02, 0x31, 0x39, 0x3B, 0x04, 0x08, 0x19, 0x37, 0x3A, 0x08, 0x2D, 0x32, 0x03, 0x22, 0x20, 0x06
```

### 方案评估

用户方案「hash + KRC 缓存 → 不足则进程内数据对象」:**基本合理**。补充:
- 纯缓存无法覆盖首次播放的新歌 → 需 API 兜底(或注入拿内存已解析数据)。
- hash 来源是最大不确定点:内存读取/本地库/注入三者都要版本兼容;若走注入,可同时拿到内存中已解析的歌词对象,一次满足缓存未命中场景。

## 汽水音乐(SodaMusic)

### 结论

- Windows 客户端 = **Electron + Vue3**(`SodaMusic.exe`),NSIS 安装。
- **无稳定本地歌词缓存**(已解析歌词只在渲染进程内存 `sharedState`,切歌即替换;音频下载目录 `C:\Users\<用户>\Music\SodaPlayer` 非歌词)。
- 最优链路(社区成熟、有开源 + 真机验证):主进程 **Node inspector 端口 9229**(用复刻 `process._debugProcess` 的方式绕过原生反调试,不碰 argv)→ 桥进 rendererMain → 读 `sharedState.get('player')` → 一次拿全:`mediaId`(稳定歌曲 ID)、标题/歌手/专辑、时长、进度、**整首明文 KRC(含逐字)**、**独立翻译轨 `translations.cn`**。参考实现:VTB-LINK/Metabox-Nexus-PlayerCap。
- 备选网络链路(无需本地客户端、无需签名,已实测 200):`GET https://beta-luna.douyin.com/luna/h5/seo_track?track_id=<id>&device_platform=web` → `lyric.content` 明文 KRC;或分享页 HTML 内嵌 `__ROUTER_DATA__` 的 `sentences[{startMs,endMs,text,words[]}]` JSON。**均不含翻译**,且需要 track_id。
- track_id 来源:SMTC 有标题/歌手/封面/时长/进度但**无 track_id**;只能从客户端状态(sharedState.mediaId)或分享链接获得。
- 反调试:`--remote-debugging-port` argv 会在 ~2s 内自杀;`NODE_OPTIONS=--inspect` 被过滤;绕过 = `process._debugProcess` 方式(读 `node-debug-handler-<pid>` 映射 → CreateRemoteThread 让目标自启 inspector)。inspector 激活过程非破坏性,比 DLL 注入稳。
- 已知坑:进度源 1Hz、窗口最小化后台节流(需本地时钟外推 + 重申 setBackgroundThrottling(false))、换歌时 mediaDetail 异步晚到。

### 方案评估

用户方案「确认本地缓存 → 无则 DLL 注入观察已解析数据」:**方向合理,手段需修正**。
- 本地缓存确认不存在 → 直接进入注入阶段,这点与用户方案一致。
- 但社区验证过的路径是 **CDP(Node inspector)而非 DLL 注入**:激活非破坏性、反调试不检测 9229、读的正是「客户端已解析完成的完整歌词数据」。建议采用 CDP 路线,复用网易云已有的 CDP/WebSocket 基础设施。

## QQ 音乐(QQMusic)

### 结论(调研已收口,算法已用真实样本实测复现)

- `.qrc` 本地文件解密链路:**已实测复现**(样本 `tmp/sample_qm.qrc` / `tmp/sample_qmts.qrc`,脚本 `tmp/verify_qrc.py` / `tmp/verify_qmts.py`):
  1. 文件头 11 字节魔数 `98 25 B0 AC E3 02 83 68 E8 FC 6C`(对应 ASCII "QRC1" 变体)。
  2. 全文件做 **QMC1 XOR**(128 字节 PRIVKEY,与 qmc 音频解密同一密钥,`i > 0x7FFF ? PRIVKEY[(i % 0x7FFF) & 0x7F] : PRIVKEY[i & 0x7F]`)。
  3. 去掉 11 字节魔数后,对剩余数据按 8 字节分组做**自定义 3DES-EDE 解密**(标准库不兼容,需移植 QQMusicDecoder DESHelper.cs 的位运算实现;密钥 `!@#)(*$%123ZXC!@!@#)(NHL`,24 字节,K1/K2/K3 拆分已与 QRCD_M 等价性验证)。
  4. **zlib inflate** → UTF-8。
  - 主歌词 `.qrc` 解密后是 XML:`<QrcInfos><LyricInfo LyricCount="1"><Lyric_1 LyricType="1" LyricContent="..."/></LyricInfo></QrcInfos>`,`LyricContent` 属性内为 **rlrc 逐字文本**:`[行起始ms,行时长ms]字(字起始ms,字时长ms)…`(字起始为相对行起点的偏移;另有 `[ti:]/[ar:]/[al:]/[offset:]/[kana:]` 元数据行)。
  - 翻译 `_qmts.qrc` 解密后是纯文本 **LRC**(`[mm:ss.xx]` 行级时间戳,无逐字)。
- 本地缓存:`.qrc` 文件确实存在于本地(下载/离线缓存场景),但**在线播放实时落盘无公开证据**;主流工具(Lyricify、lxmusic、QQMusicApi)走网络 API。
- 可靠公开路径:SMTC / 窗口标题 → songmid → 歌词 API(`crypt=1&qrc=1`)→ 解密。歌词接口格式:`{"retcode":0,"lyric":"<hex>","trans":"<hex>","qrc":"<hex>","qrc_trans":"<hex>"}` 等字段(版本相关)。
- 进程内逆向:基本不公开;可借 `QQMusicCommon.dll` 导出函数或 Metabox-Nexus-PlayerCap 版本偏移表,不依赖固定地址。
- 仓库 `tmp/qrc_research/` 有完整参考:Lyricify Decrypter/Parser/Generator、qrc_decrypt.go、tripledes.py。

### 方案评估

用户方案「.qm.qrc 本地缓存识别/解码/解析 + 当前歌曲匹配 → 不足则 DLL 注入 QRC 加载/解析数据层」:**基本合理,需调整顺序**。
- 本地 `.qrc` 解密链路完整且已实测,可作为主路径之一;但在线播放时不一定实时落盘,需要网络 API 兜底(与酷狗同构:本地缓存 → API 兜底)。
- 当前歌曲 songmid 来源:SMTC 只有标题/歌手;窗口标题匹配或本地缓存目录文件名(含 songmid)可作为来源。
- 优先实现「缓存读取 + 网络 API + 解密解析」纯数据链路(可单测),DLL 注入观察进程内数据仅作最后手段。

## Spotify 音乐(Spotify)

### 结论

- **Lyricify 已实现完整 Spotify 歌词链路**:其 App 本体闭源(仓库仅留文档),但歌词处理库 `WXRIW/Lyricify-Lyrics-Helper` **完全开源**且即 App 实际所用库,包含 Spotify 全部链路,可直接参考移植,无需从零逆向。
- 歌词源 = **Spotify 官方内置歌词接口 color-lyrics**(`spclient.wg.spotify.com/color-lyrics/v2/track/{trackId}`),与 Spotify App 内显示一致,非第三方源。
- 鉴权 = **sp_dc 登录 cookie → `open.spotify.com/api/token`(TOTP 挑战)换 Bearer token**(约 1 小时有效,自动刷新);免费账号可用,无需注册 Developer app、无 client_id/secret。
- 数据能力:**行级 + 音节级时间轴 + 官方翻译轨**;`syncType` 三档:UNSYNCED / LINE_SYNCED / SYLLABLE_SYNCED。
- 桌面本地 API(spotilocal:4380)新版已不稳定/有停用报告(2025) → 不作基础,与前述三家不同,Spotify **不需要注入/CDP/共享内存**,纯网络链路。

### 歌词获取链路(Lyricify-Lyrics-Helper 开源实现,已读源码)

1. **sp_dc 获取**:浏览器 DevTools 复制登录 cookie(长期有效,失效需重配;Lyricify App 做法为内嵌登录自动抓,可作后续增强)。
2. **Token 换取**(`Providers/Web/Spotify/Api.cs`):
   - `GET https://open.spotify.com/api/token?reason=init&productType=web-player&totp=<TOTP>&totpVer=<ver>&totpServer=<TOTP>`,请求头 `Cookie: sp_dc=<spDc>`、`App-Platform: WebPlayer`、`Origin: https://open.spotify.com`。
   - TOTP = HMAC-SHA1(secret, `serverTime/30` 计数器)截断取 6 位;serverTime 取自 `open.spotify.com/api/server-time`。
   - secret 从公开 spotify-secrets 镜像拉取(3 个 URL 轮询)+ 内置 3 组兜底;还原公式 `value[i] ^ ((i % 33) + 9)`,版本取最大 key。
   - 失败降级:旧参数 `reason=transport` + `ts=<unix秒>`。
3. **歌词获取**:`GET https://spclient.wg.spotify.com/color-lyrics/v2/track/{trackId}?format=json&market=from_token`,`Authorization: Bearer <token>`;响应 `{lyrics{...}, colors{...}, hasVocalRemoval}`。
4. **歌曲搜索(拿 trackId)**:pathfinder `searchDesktop` persisted query(两个 sha256 hash 轮换)→ 降级官方 `GET /v1/search?q=&type=track&market=from_token`。
5. **歌词数据结构**(对应 `Parsers/Models/Spotify.cs`):

| 字段 | 含义 |
|---|---|
| `lyrics.syncType` | `UNSYNCED` / `LINE_SYNCED` / `SYLLABLE_SYNCED` |
| `lines[]` | 行级:`startTimeMs` / `endTimeMs` / `words` |
| `lines[].syllables[]` | 音节级:`startTimeMs` / `endTimeMs` / `numChars`,按 `words[i..i+numChars]` 切词 → 可映射词级时间轴 |
| `alternatives[]` | `{language, lines[]}` —— 官方翻译轨 |
| `provider` | 歌词提供方(如 musixmatch) |

### 方案评估(修正自初版方案)

- **主源**:color-lyrics 官方内置接口(初版曾建议 LRCLIB 主源,现修正)—— 覆盖率/质量/逐字能力全面优于 LRCLIB,且最难点(TOTP 动态 secret 管理、token 刷新、双参数降级)已有开源实现可移植。
- **兜底(已定)**:LRCLIB(ISRC 精确匹配),用于 color-lyrics 未收录歌曲;不引入网易云搜索(保持链路最简,翻译由 alternatives[] 覆盖)。
- **鉴权(已定)**:sp_dc 粘贴配置为第一版;无 client_id/secret、无 PKCE。
- **身份层(已定)**:SMTC 当前曲目为主 → pathfinder 搜索 → trackId(与 Lyricify 同构,免配置);**OAuth 预留接口**,后续增强可直拿 trackId/ISRC、支持 Connect 跨设备。
- **范围(已定)**:只做歌词显示链路(身份+进度+歌词 → 歌词岛),不做播放控制热键(需 Premium)。
- **翻译轨**:解析 `alternatives[]` 即得官方翻译。
- **逐字能力**:`SYLLABLE_SYNCED` → 按 numChars 切分映射 STimeline 词级;`LINE_SYNCED` → 行级;`UNSYNCED` → 无时间轴。
- **已知坑**:sp_dc 失效需重配;TOTP secret 仓库为社区维护(需多镜像 + 内置兜底);`market=from_token` 下部分区域歌词可用性有差异。

## 统一交付格式(目标)

复用 `NeteaseLyrics` 的 STimeline/SLine/SWord 模型(行级 + 词级时间轴),扩展翻译轨;输出:歌曲信息(标题/歌手/专辑/时长/封面)、完整歌词、逐字时间轴、翻译。

## 参考来源

- 酷狗:KRC 格式 https://www.jianshu.com/p/dfae11a9599b 、https://github.com/emako/KRCLib 、歌词接口 https://www.cnblogs.com/mmm/p/18144203/kugou_krc 、TaskbarLyrics https://github.com/ANYNC/TaskbarLyrics
- 汽水:PlayerCap https://github.com/VTB-LINK/Metabox-Nexus-PlayerCap 、qishui-api https://github.com/guowenye/qishui-api 、qishui-music-parser https://github.com/MiraHikari/qishui-music-parser
- QQ:QQMusicDecoder https://github.com/WXRIW/QQMusicDecoder 、Lyricify
- Spotify:Lyricify-Lyrics-Helper https://github.com/WXRIW/Lyricify-Lyrics-Helper 、Lyricify-App https://github.com/WXRIW/Lyricify-App 、DeepWiki 歌词源分析 https://deepwiki.com/WXRIW/Lyricify-App/8-lyrics-sources-and-processing 、spotify-secrets(镜像示例)https://github.com/xyloflake/spot-secrets-go 、本地 API 停用讨论 https://community.latenode.com/t/has-spotifys-local-web-server-api-been-discontinued/32664
