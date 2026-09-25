# QmClient 许可声明 / Licensing

QmClient(Q1menG Client)是基于 DDNet 与 TaterClient 的第三方定制客户端。
本项目实施**分层许可**:每一部分适用与其来源相符的条款。本文件界定各层的范围;
各层条款全文的存放位置见第 6 节。

QmClient (Q1menG Client) is a third-party customised client built on DDNet and
TaterClient. This project uses **layered licensing**: each part is covered by the
terms matching its origin. This file defines the scope of each layer; the
locations of the full licence texts are listed in section 6.

**本声明不改变也不替代任何上游许可。** 上游授予的权利不因本文件而被收窄。

**This notice does not modify or replace any upstream licence.** No right granted
by an upstream licence is narrowed by this file.

---

## 1. 继承自上游的部分 / Upstream-derived parts

**适用条款:zlib/libpng 许可(代码);CC BY-SA 3.0(上游 `data` 内容)**
**Terms: zlib/libpng licence (code); CC BY-SA 3.0 (upstream `data` content)**

除第 2、3 节明确列出的范围之外,本仓库的全部内容均属此类,包括:

- 继承自 Teeworlds、DDRace、DDNet、TaterClient 的源代码,以及 QmClient 对这些文件所作的修改;
- `data/` 下除 `data/qmclient/` 以外的全部内容;
- `ddnet-libs/`、`src/engine/external/` 等第三方依赖。

- All source code inherited from Teeworlds, DDRace, DDNet and TaterClient,
  including QmClient's modifications to those files;
- everything under `data/` except `data/qmclient/`;
- third-party dependencies such as `ddnet-libs` and `src/engine/external`.

对上游文件的修改不改变该文件的许可。此类文件继续完整适用上游条款,并须按
zlib 许可第 2、3 条标明其为修改版本、保留原有版权声明。

Modifying an upstream file does not relicense it. Such files remain fully subject
to the upstream terms and must be marked as altered with their original copyright
notices retained, as required by clauses 2 and 3 of the zlib licence.

## 2. QmClient 自有代码 / QmClient original code

**适用条款:保留所有权利(专有)**
**Terms: All rights reserved (proprietary)**

以下范围由 QmClient 独立创作,不含上游代码,**保留所有权利**:

The following scope was independently created by QmClient, contains no upstream
code, and is **All rights reserved**:

- `src/game/client/components/qmclient/**`
- `src/game/client/QmUi/**`
- `src/game/client/` 下以 `qm_` 前缀命名的新增文件(`qm_icon_manager`、
  `qm_ime_manager`、`qm_ime_candidate_popup`、`qm_title_effect` 等)
- `src/game/client/components/qm_console_log_filter.h`
- `src/engine/shared/config_variables_qmclient.h`、`qm_removed_config.h`
- `src/test/` 下以 `qm_` 或 `Qm` 开头的新增测试文件
- `qmclient_scripts/**`
- `docs/**`
- `data/qmclient/builtinscripts/**`

未经版权持有人事先书面许可,不得复制、修改、分发、再许可或用于商业用途。
源码公开可见不代表授权使用。

No copying, modification, distribution, sublicensing or commercial use is
permitted without the prior written permission of the copyright holder. Source
visibility does not grant a licence.

### 2.1 含第三方移植代码的文件 / Files containing ported third-party code

以下文件含从第三方项目移植的代码。**移植部分继续适用其原始许可**,文件其余部分
适用本节;各原始版权声明与许可文本见 `license.txt`。

The following files contain code ported from third-party projects. **The ported
portions remain under their original licences**; the rest of each file falls
under this section. The original copyright notices and licence texts are in
`license.txt`.

| 文件 / File | 移植来源 / Ported from | 许可 / Licence |
| --- | --- | --- |
| `src/game/client/components/qmclient/music_lyrics/music_lyrics_qrc.{h,cpp}` | WXRIW/QQMusicDecoder(`DESHelper.cs`) | MIT |
| `src/game/client/components/qmclient/music_lyrics/qm_spotify_token.{h,cpp}` | WXRIW/Lyricify-Lyrics-Helper | Apache-2.0 |

以下文件参考了第三方实现或采用其公开实测数据。实测偏移、版本指纹等事实数据不受
版权保护;若其中含受保护的表达性代码,该部分同样适用原许可。

The following reference third-party implementations or use their published
measurement data. Measured offsets and version fingerprints are factual data and
are not copyrightable; any protected expressive code within them is likewise
subject to the original licence.

| 文件 / File | 参考来源 / Reference | 许可 / Licence |
| --- | --- | --- |
| `src/qm-music-hook/**`(酷狗、QQ 音乐采集) | VTB-LINK/Metabox-Nexus-PlayerCap | MIT |
| `src/qm-soda-hook/**`(汽水音乐数据链路) | VTB-LINK/Metabox-Nexus-PlayerCap | MIT |

`README.md` 致谢列表中出现的 jayfunc/BetterLyrics 与 ElliottSilence/LyricCapture
(GPL-3.0)经逐文件核查**未被移植或引用**,不影响本项目的许可分层。

jayfunc/BetterLyrics and ElliottSilence/LyricCapture (both GPL-3.0), listed in the
`README.md` credits, were checked file by file and are **not ported or referenced**
anywhere in the source; they do not affect the licensing layers of this project.

`music_lyrics_krc.{h,cpp}`(酷狗 KRC 解密)、`netease/**` 与 `qm-nmt-hook/**`
(网易云)、`music_lyrics/qm_soda_lyric_file.{h,cpp}`(汽水歌词文件解析)经作者确认
为原创实现,不含第三方移植代码。

`music_lyrics_krc.{h,cpp}` (Kugou KRC decoding), `netease/**` and `qm-nmt-hook/**`
(Netease Cloud Music), and `music_lyrics/qm_soda_lyric_file.{h,cpp}` (Soda lyric file
parsing) are confirmed by the author to be original implementations containing no
ported third-party code.

### 2.2 音乐平台互操作部分的使用意图 / Intent for the music-platform interoperability code

`src/qm-music-hook/`、`src/qm-nmt-hook/`、`src/qm-soda-hook/` 以及
`src/game/client/components/qmclient/music_lyrics/`、`netease/` 下的音乐平台互操作
代码,其编写与发布意图如下:

The music-platform interoperability code under `src/qm-music-hook/`,
`src/qm-nmt-hook/`, `src/qm-soda-hook/`, `.../qmclient/music_lyrics/` and
`.../qmclient/netease/` is written and published with the following intent:

- **个人学习与研究** —— 用于研究歌词格式解析、本机进程间通信与客户端渲染技术。
- **非商业、不盈利** —— 不收费、不含广告、不提供任何付费解锁或增值服务。
- **不提供音乐内容** —— 不提供、不托管、不下载、不再分发任何音乐作品或歌词内容。
  歌词与播放状态仅从使用者本机已安装并正在运行的音乐客户端读取,且只在本机显示。
- **不绕过付费或订阅** —— 不用于规避任何平台的付费、订阅或版权保护机制。
- **权利归属** —— 音乐作品、歌词文本及各平台商标的权利归各权利人所有。

- **Personal study and research** — for studying lyric format parsing, local
  inter-process communication and client-side rendering techniques.
- **Non-commercial, not for profit** — no fees, no advertising, no paid unlocks
  or value-added services of any kind.
- **No music content provided** — no music work or lyric content is provided,
  hosted, downloaded or redistributed. Lyrics and playback state are read only
  from a music client already installed and running on the user's own machine,
  and are displayed locally only.
- **No circumvention of payment or subscriptions** — not used to evade any
  platform's payment, subscription or copy-protection mechanisms.
- **Rights remain with their owners** — music works, lyric texts and platform
  trademarks belong to their respective rights holders.

若任何权利人认为本项目中的相关代码不当,请通过仓库 issue 联系,我们会在核实后
移除对应功能。

If any rights holder considers the related code inappropriate, please contact us
via a repository issue; the functionality will be removed after verification.

本节为**使用意图声明**,用于说明项目立场。它**不构成**对任何第三方软件许可协议
或服务条款的豁免,也不改变使用者与各平台之间既有的协议关系。

This section is a **statement of intent**. It **does not** waive or alter any
third-party software licence or terms of service, nor the agreements users
already hold with those platforms.

## 3. `data/qmclient` 自制素材 / QmClient original assets

**适用条款:CC BY-NC-ND 4.0(署名 — 非商业性使用 — 禁止演绎)**
**Terms: CC BY-NC-ND 4.0 (Attribution-NonCommercial-NoDerivatives)**

以下素材由 QmClient 作者原创绘制,适用
[CC BY-NC-ND 4.0](https://creativecommons.org/licenses/by-nc-nd/4.0/):

The following assets were created by the QmClient author and are licensed under
[CC BY-NC-ND 4.0](https://creativecommons.org/licenses/by-nc-nd/4.0/):

- `data/qmclient/chat_emojis/*.png`(16 张聊天表情,全部原创)
- `data/qmclient/gui_logo.png`

该许可允许在**署名、非商业、不作任何改动**的前提下原样转载;禁止修改、
二次创作、商用,亦不得以本项目名义重新分发。

This licence permits verbatim redistribution with **attribution, for
non-commercial purposes, without any modification**. Alteration, derivative
works, commercial use and redistribution under another project's name are not
permitted.

`data/qmclient/` 下的其余内容不属于本节:**字体**与**图标**见第 4 节。

The remaining content under `data/qmclient/` is not covered by this section:
**fonts** and **icons** are addressed in section 4.

## 4. 第三方组件 / Third-party components

下列内容的版权归各自作者所有,**不适用本项目的任何条款**,各自的原始许可继续有效:

The following remain the property of their respective authors. **No QmClient
licence applies to them**, and their original licences continue in force:

| 范围 / Scope | 许可 / Licence |
| --- | --- |
| `data/qmclient/icons/**`、`datasrc/qm_icons/**` | MIT(Phosphor Icons,© 2020 Phosphor Icons);全文见 `datasrc/qm_icons/LICENSE_PHOSPHOR.txt` |
| `data/qmclient/fonts/Inter_*.ttf`、`Montserrat-*.ttf`、`Nunito-*.ttf`、`Rubik-*.ttf`、`Cabin-*.ttf` | SIL Open Font License 1.1 |
| `data/qmclient/fonts/FreeSansBold.ttf` | GNU FreeFont(GPLv3+ 含字体嵌入例外) |
| `data/qmclient/fonts/GoogleSans-Regular.ttf` | SIL Open Font License 1.1 |
| `data/qmclient/fonts/minecraft_font.ttf` | CC BY-SA 3.0(署名 — 相同方式共享) |
| `src/game/client/components/tclient/fast_practice.{h,cpp}`、`src/game/client/components/menus_assets_editor.cpp` | © 2026 BestProject Team |
| `src/engine/external/**`(zlib、wavpack、GLEW、rnnoise、md5、json-parser、tinyexpr、KCP 等) | 各自原始许可 |
| `ddnet-libs/**` | 各自原始许可 |
| 歌词与音乐采集相关第三方组件 | 见 `README.md` 致谢列表及 `license.txt` |

`minecraft_font.ttf` 适用 CC BY-SA 3.0,其「相同方式共享」条件适用于该字体文件
本身的演绎作品,不影响本项目的代码许可。

`minecraft_font.ttf` is under CC BY-SA 3.0; its ShareAlike condition applies to
adaptations of the font file itself and does not affect the code licensing of this
project.

`data/qmclient/icons/**` 派生自 Phosphor Icons。依 MIT 许可要求,派生作品中
必须保留其版权声明与许可全文,MIT 声明不得被替换为本项目的任何条款。

`data/qmclient/icons/**` is derived from Phosphor Icons. The MIT licence requires
that its copyright notice and permission notice be retained in the derived work;
it must not be replaced by any QmClient term.

## 5. 贡献 / Contributions

向本项目提交贡献即表示:贡献者确认其贡献为原创或已获得充分授权,并同意该贡献
按其所修改部分对应的层(第 1、2 或 3 节)授权给本项目。

By contributing to this project, a contributor confirms that the contribution is
original or otherwise sufficiently licensed, and agrees that it is licensed to
this project under the layer (section 1, 2 or 3) matching the part it modifies.

## 6. 条款全文位置 / Where the full texts are

- 上游 zlib 许可、`data` 的 CC BY-SA 3.0 声明、以及全部第三方归属:`license.txt`
- 本项目的分层范围界定:本文件
- `data/qmclient` 素材声明:`data/qmclient/LICENSE.txt`
- Phosphor Icons MIT 全文:`datasrc/qm_icons/LICENSE_PHOSPHOR.txt`
- CC BY-NC-ND 4.0 全文:<https://creativecommons.org/licenses/by-nc-nd/4.0/legalcode>
- Apache-2.0 全文:见 `license.txt`(已附完整正文)

## 7. 范围冲突的处理 / Resolving overlaps

若某个文件同时包含上游代码与 QmClient 新增内容,该文件整体继续适用第 1 节,
**不得**收窄上游授予的权利。范围归属存疑时,一律以上游许可为准。

Where a file contains both upstream code and QmClient additions, the file as a
whole remains subject to section 1, and no right granted upstream may be
narrowed. Where the applicable layer is uncertain, the upstream licence governs.

---

QmClient 为个人定制版本,不代表 DDNet 或 TaterClient 的官方立场。

QmClient is a personal customisation and does not represent the official stance
of DDNet or TaterClient.
