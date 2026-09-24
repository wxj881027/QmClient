# DDNet 上游同步：PR 草案

本文件给出可直接使用的 PR 标题/正文与推送命令。分支都是**线性堆叠**、基线为 `6b9a41fd21`
（`origin/master`，2026-09-24 从 `aea1453cc7` 前移），因此「推哪个分支 = 提哪一段」。

已推送并创建（2026-09-24）：

| PR | 分支 | 基线 | 链接 |
|----|------|------|------|
| PR-1 | `sync/slice-1-base-engine`（`0956e8bebb`） | `master` | [#259](https://github.com/wxj881027/QmClient/pull/259) |
| PR-2 | `sync/slice-9-ghost-cull`（`6e1072062a`） | PR-1 分支 | [#260](https://github.com/wxj881027/QmClient/pull/260) |
| PR-3 | `sync/slice-23-official-semantics`（`3b3d3265af`） | PR-2 分支 | [#261](https://github.com/wxj881027/QmClient/pull/261) |

统一验证口径（2026-09-24 在新基线上逐段实测，全部零失败）：

| 状态 | 运行用例 | 通过 | 跳过 | 失败 | 基线外新增 |
|------|----------|------|------|------|------------|
| 新基线 `6b9a41fd21`（`origin/master`） | — | — | — | 0 | — |
| 旧基线 `aea1453cc7`（已弃用） | 3329 | — | — | 2 | — |
| PR-1 边界 `0956e8bebb` | 3329 | 3328 | 1 | **0** | 无 |
| PR-2 边界 `6e1072062a` | 3330 | 3329 | 1 | **0** | 无 |
| PR-3 边界 `3b3d3265af`（`sync/slice-23-official-semantics`，链尾） | 3348 | 3347 | 1 | **0** | 无 |

1 例跳过是环境相关的 `QmWebSocketLive.ConnectsAndEchoesWhenServerConfigured`（未配置测试服务器）。
旧基线上那 2 例（`BaseSettingsStableTextCandidateAuditIsEmptyExceptAllowlist`、
`TeeRestoresCardContentsAndKeepsDoubleClickActions`）随基线前移一起消失 —— 它们期待的源侧实现
（`8b286754a2`，PR #258）此前不在本地基线里，属本地落后，不是同步引入的回归。

---

## PR-1 地基：`src/base` 布局对齐

- 分支：`sync/slice-1-base-engine`（`0956e8bebb`）
- 规模：4 个提交 / 36 个文件 / +3184 −2834
- 标题：`refactor(sync): 对齐 DDNet 上游 src/base 布局并保留自有 QoS/统计行为`
- 正文要点：
  - 新增上游 `io/aio/net/os/process/bytes` 六个模块（`net.cpp`/`net.h`/`os.cpp` 取 **master** 版，
    顺带带入 `44e7f84fa`、`9931d2dc9`、`d3810ba51` 三条缺失窗口修复），删除 `system.cpp`；
  - `system.h` 改为**聚合 shim**（仓库有 272 处 `#include <base/system.h>`，本 PR 不动调用方）；
  - 保留 QmClient 自有 **Windows qWave QoS**、`send_errors` 统计、「任一 socket 成功即成功」语义；
  - `types.h` 对齐上游（`ASYNCIO` / `EIoSeekOrigin` 等），改名 `shell_execute→process_execute` 等 24 处、
    `IOSEEK_*→EIoSeekOrigin::*` 30 处；`CMakeLists.txt` 的 BASE 源列表同步更新；
  - 另含首批 3 条零重叠 cherry-pick（`c6757b72e` 空数据报不停止接收、`adac08c6c` inline varint、`c63b1b944` `std::size`）。
- 评审要点：shim 的存在理由与后续清理计划；QoS 并入 `net.cpp` 而非独立文件的原因（需要 `NETSOCKET` 内部结构）。
- 推送：`git push -u origin sync/slice-1-base-engine`

## PR-2 客户端与引擎修复批次（切片 2–9）

- 分支：`sync/slice-9-ghost-cull`（`6e1072062a`）
- 规模：35 个提交（含 PR-1）/ 60 个文件 / 累计 +3492 −2945
- 标题：`fix(sync): 客户端与引擎修复批次（demo/预测/裁剪/健壮性/nightly/PNG）`
- 正文要点：
  - demo 三处防御修复（marker UB、delta 早于 full、ScanFile 无关键帧崩溃）；
  - 低风险吸收：聊天草稿复位、表情抖动公式、`community_icons` 竞态、返回键断言、绑定比较器、0.7 跳过时长；
  - 预测初始化 3 条（tune zone、`m_LastSnapWeapon`/`ActiveWeapon`、`CLaser`/`CPlasma` 成员）；
  - **屏幕外实体裁剪**（投射物/激光/拾取物，边距对齐上游）与 **ghost 屏外裁剪**；
  - 健壮性 9 条（JSON 解析/转义、控制台与 rcon 粘贴、demo 跳过时长、服务器 demo 文件名清洗）；
  - 20.0 之后 nightly 7 条；PNG 读写 2 条；`CScreenRect` 基建 + 裁剪改写为上游同形。
- 评审要点：`CScreenRect` 是新增基建（纯增量、后端未动）；裁剪与 PNG 属「少画/影响图像」类改动，
  需按 `docs/development/upstream-sync-verification.md` 实机确认。
- 推送：`git push -u origin sync/slice-9-ghost-cull`（PR-1 合并后 rebase 到 master）

## PR-3 服务器、工具、数据、验证补强与四项官方语义（切片 10–23，链尾）

- 分支：`sync/slice-23-official-semantics`（`3b3d3265af`）
- 规模：约 85+ 个提交（含 PR-1/2）/ 累计 +3900 −3200 量级
- 标题：`fix(sync): 服务器、地图工具、Unicode 数据与小修批次（含 use-after-free/double free 修复）`
- 正文要点：
  - 服务器/控制台 3 条（`/map` 优化、`Chain` 断言、`GetOptionalFloat` 用 `GetFloat`）；
  - `src/tools` 7 条（`map_convert_07` **use-after-free**、`map_replace_area` **double free**、`dummy_map` 数据复用等）；
  - **Unicode 15.0.0 → 17.0.0** 数据表与生成脚本（5 个文件与上游逐字节一致）；
  - 扫描五批共 26 条：图形/quad 裁剪、`hot_reload` 泄漏、`sv_ipv4only`、`add_map_votes` 崩溃、
    shader log **OOB**、libpng 版本（**已按上游在 5 天后的回退一并处理**）、Huffman 断言与 OOB、
    `str_comp_filenames`、Android 服务器 UAF、freeze bar 崩溃、lineinput 反向选择、censorlist、switch 钳制、mapbugs SHA256 等；
  - **验证补强**：PNG 读写往返 3 例（`image_test.cpp`）、裁剪边距与判定 6 例
    （`qm_item_culling_logic.h` + 测试）、钩子提示线可见性 7 例（`qm_hook_coll_visibility_test.cpp`）、
    DB 浮点语义 2 例（`db_connection_test.cpp`，且做过「改回旧实现 → 测试必失败」的反向验证）；
  - **自审与门禁**：61 条 `-x` 溯源逐条核对上游是否回退过（`upstream_revert_audit.py`，0 条待处理）；
    整链跑过准发布门禁 `--mode full`，非零项逐条归属到「上游代码 / 本地既有问题 / 环境缺失」。
  - **四项官方语义（2026-09-24 维护者拍板「改用官方」）**：
    `821d5ae4b4` 控制台引号参数参与校验（同步本地 `qm_modes_test.cpp` 断言）、
    `478781ad65` windowed fullscreen 改回有边框（改写本地 `QmWindowModes` 两条断言）、
    `b9c39900a9` rescue 不覆盖 `m_DDRaceState`、`ed2b08f5d9` 超时恢复连接时重置 snapshot；
    其中 windowed fullscreen 会**改变外观**（有边框、换取 Windows 截图工具可用），需实机确认。
  - **门禁**：`src/base` 采纳文件上的 13 条 MSVC `/analyze` 告警按「文件+警告码」精确豁免，
    理由随报告列出（`strict_build.py` 的 `_ANALYZE_UPSTREAM_BASE_ALLOWLIST`）。
- 评审要点：本 PR 已按维护者拍板**采用**上述四项官方语义（此前曾按「保留 fork 行为」挂起）。
- 推送：`git push -u origin sync/slice-23-official-semantics`

---

## 未包含在本批次的可选项（等维护者拍板）

| 上游改动 | 冲突点 | 取舍 |
|----------|--------|------|
| MySQL SSL（`9f59dcb1f6`+`78d8f82c52`）、rejoin 状态（`4e4536bdae`+`4e25f792ea`） | 本地完全没有对应特性 | 属**新增功能**，先讨论再加 |

已拍板并入 PR-3 的四项（控制台引号参数、windowed fullscreen 边框、rescue `m_DDRaceState`、
snapshot 超时重置）不再列在此表。

## 一次合完的替代做法

若不想分三段：在 `sync/slice-23-official-semantics`（链尾）上把提交按「地基 / 客户端 / 服务器与工具」压成 3 个提交，
再从该分支提一个 PR（历史更干净，但会丢失逐条上游提交的 `-x` 溯源信息）。
