# 内置输入数据

Windows 内置输入支持中文拼音和英文单词补全，使用 US 键盘布局；默认仍为系统输入（`qm_ime_engine 0`）。设置卡片中的“内置拼音”启用后，中文模式提供拼音候选，英文模式提供英文前缀补全。其他输入方式和键盘布局使用系统输入。

## 输入与学习

- 默认保留 Shift 单击切换，并提供 Ctrl+Space。可在输入法设置卡片重新绑定切换键或关闭 Shift；Ctrl+Shift 等修饰键组合也可绑定。中文组合期间的 Shift 单击保持原行为；显式切换快捷键保留未选拼音原文。
- 英文模式下，Tab 接受当前高亮建议，上下方向键选择建议；空格保留已输入原文并加空格。数字保留为原文，Enter 先提交当前英文原文，再交给原输入框处理。
- 中文组合中，Enter / 小键盘 Enter 原样提交键入的字母，不选首候选；例如 `onlyfans + Enter` 输出 `onlyfans`。这一按键只结束组合，下一次空闲 Enter 才交给输入框发送/执行；中文候选由空格或数字键确认。
- 中文选词后可接数字单位，例如“他”选定后输入 `5kfenle` 并选词，得到“他5k分了”。中文组合中的 1–9 继续用于选词；没有组合时的数字及其后的 k/K、w/W 等单位保持 ASCII。
- 数字后的可能单位字母暂存在组合中；后续元音允许继续拼音，因此 `5ge`、`5tian`、`5wan`、`5ke` 仍能选择“5个、5天、5万、5颗”。有歧义的英文片段可显式切到英文模式。
- 选取部分候选时，按单字多音表匹配已消费的拼音前缀，通过 Rime `set_input` 保留剩余拼音。例如 `caishi` 选“才”提交后保留 `shi`，随后可以选“是”。无法匹配前缀时保留 Rime 原生选择。
- 中文组合中用 `-` 或 PageUp 翻上一页，`=` 或 PageDown 翻下一页；首页和末页不循环。不增加界面提示，Shift 组合产生的 `_` / `+` 不作为翻页键。
- Rime 只做通用解码，关闭自身 user dict。学习统一由 Qm 词库管理；Rime 首选保留在第一位，领域词和学习词从第二位开始，重复项保留原生选择下标。连续单字在原有 2 秒窗口内可以组合学习。
- `qm_ime_learning 0` 停止新增学习；`qm_ime_clear_learned` 清理 Qm 的持久化学习文件。静态通用词、网络用语和 DDNet 词条保留。

## 数据来源与生成

`qm_pinyin.dict.yaml`、`qm_game_aliases.dict.yaml`、`qm_pinyin_chars.txt`、`qm_english_words.tsv`、`LICENSES.txt` 为生成物。普通客户端构建复制这些文件，不下载词库或调用 Python。

中文基础改为 amzxyz 的万象拼音数据，固定提交 `6ea60e43339de4257bf815529e0fccac0704670a`（2026-10-29），只采用 `jichu`、`zi`、`duoyin` 三份数据，按 CC BY 4.0 分发衍生中文词典和补充后的读音表。上游说明词频来自社区问答、博文互动、评论、百科、新闻等多领域语料；这是上游统计，Qm 没有自行训练其 48 GB 语料。保留上游 LICENSE、LICENSE-SCOPE 和 NOTICE，未采用万象的程序代码、Lua、语法模型或插件。

补充来源为 jieba v0.42.1、phrase-pinyin-data v0.19.0、pinyin-data v0.15.0，以及 en-wl/wordlist 固定提交中 Alan Beale 的 12dicts 3esl。完整地址、提交、SHA-256 和许可说明见 `LICENSES.txt` 与生成器常量。前三个项目使用 MIT；单字数据另保留 Unihan 的 Unicode License V3 和 python-pinyin/Hotoo 署名。3esl 按作者声明为 public domain，原始 README 一并保留。未采用 GPL 的输入法词库、插件或来源不明的搜狗词包。

生成器对整批语料词条去声调、合并同词同音重复行，并保留各读音原有的相对权重，不再次取对数。当前生成 1602660 条中文词/读音组合、43836 个汉字的读音；其中 1547546 条来自语料数据，54773 条为旧库独有词的低权重补充。非法文本和权重会被排除并计数。中文词典约 46.4 MB，不再用 20 万条的新闻/分词词频作为主库。

日常和 DDNet 人工优先项作为补充；同级优先项保留语料频次差异，避免同音词被抹成相同权重后按字形排列。人工数值是输入优先级，不代表实际互联网或玩家聊天统计频次。明确标注的游戏读音可以覆盖基础读音；独立 m/n/ng 鼻音展开为完整音节，避免与简拼竞争。前后鼻音采用 Rime 的 correction 容错，正确完整拼写优先。

人工来源位于 `qmclient_scripts/ime/`：

- `curated_phrases.txt`：原有常用口语。
- `chat_phrases.tsv`：日常、网络表达和常见语法组合。
- `ddnet_phrases.tsv`：游戏术语、动作、队伍及沟通词。
- `user_chat_supplement.tsv`：用户授权的本地备份中逐条复核的 207 条通用聊天和游戏短句。排除地址、姓名、学校、课程、联系方式等个人信息，本批不补入明显地域口语；原始备份和未入选记录只保留在本地 `tmp/`，不随客户端分发。补充项采用较低输入优先级，不改变已有词的权重。
- `english_phrases.tsv`：英语常用词及游戏用词补全优先级。
- `game_aliases.tsv`：已确认的完整输入快捷词，包括 `hj → HJ` 和 `zg/bj/xbf/md` 的固定约定。歧义简称不通过整体抬高“中国/北京”等词频处理；独立表只匹配完整输入，不引入新的汉语单字母音节。

主方案通过依赖部署 `qm_game_aliases.schema.yaml`，该依赖不加入用户方案列表。动态玩家名、地图名、服务器名和学习词仍由 provider 合并，不写入源词典或触发重新部署。

准备固定输入并生成：

```powershell
python qmclient_scripts/ime/build_rime_dict.py --download
```

准备后离线重建：

```powershell
python qmclient_scripts/ime/build_rime_dict.py
python -m unittest discover -s qmclient_scripts/ime -p test_build_rime_dict.py -v
```

缓存默认为 `tmp/ime/downloads/data/`。输出不包含生成时间，重复生成可复现；`ü` 使用 `v`。生成器只读取 phrase-pinyin-data 的 `pinyin.txt`，不使用 `large_pinyin.txt`、`cc_cedict.txt` 或其他未核实的词库。

## 固定核心运行时

`qmclient_scripts/ime/rime_core_sources.json` 与 `prepare_rime_core.py` 固定 librime 1.17.0 及其依赖源码、提交和 SHA-256。只构建纯核心，清空插件，关闭 glog。leveldb、Marisa、OpenCC、yaml-cpp 和 CRT 静态链接；Boost 仅使用头文件。完整通知随 `RIME-LICENSES.txt` 分发。

```powershell
python qmclient_scripts/ime/prepare_rime_core.py --download
python qmclient_scripts/ime/prepare_rime_core.py --build
```

源码和构建位于 `tmp/ime/rime-core/`，产物为 `install/rime.dll`、头文件、来源清单与回执。FindRime 校验固定配方、平台、版本和产物后启用客户端分支，不接受含 octagram 的官方整包。准备核心不会更新客户端，需要另行通过 Windows 封装增量构建：

```powershell
cmd /c qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target game-client -j 6
```

首次部署写入用户目录的 `ime/build/`；后台部署期间继续由系统路径处理输入。全库首次编译可能超过原来的 10 秒，当前有界等待为 60 秒；部署失败或超时后交还系统输入。正式客户端不主动下载词库。

## 独立真实 provider 场景

此工具链接生产 engine、词库、排序与关闭实现，动态加载同一纯核心 DLL；只替换存储路径，数据保存在工作区 `tmp/ime/gpt-provider-user/`。它验证真实 provider 与 Rime 协作，不启动完整客户端，不能替代 SDL、输入框或视觉验收。

```powershell
cmd /c qmclient_scripts/cmake-windows.cmd -G Ninja -S qmclient_scripts/ime/engine_probe -B tmp/ime/engine-probe-build -DCMAKE_BUILD_TYPE=Release
cmd /c qmclient_scripts/cmake-windows.cmd --build tmp/ime/engine-probe-build --target qm-ime-engine-probe -j 6
$imeRepo = (Get-Location).Path
foreach ($imePhase in @('input', 'matrix', 'restore', 'clear', 'after-clear', 'failure')) {
    & ./tmp/ime/engine-probe-build/qm-ime-engine-probe.exe $imeRepo $imePhase
    if ($LASTEXITCODE -ne 0) { throw "IME scenario failed: $imePhase" }
}
```

`matrix` 读取 `engine_probe/pinyin_cases.tsv`，逐项核对候选顺序和数字提交；其他阶段验证分段、混输、英语补全、快捷键、学习跨进程恢复/清除及真实部署失败回退。失败数据单独留在 `tmp/ime/gpt-provider-failure-*`。当次验证记录及未完成验收见 `tmp/ime/gpt-optimize-status.md`，不把场景数量称作覆盖率或端到端覆盖。

## 独立语料对比

`prepare_corpus_eval.py` 从固定 SHA-256 的 Tatoeba 普通话导出中，选取简体文本和有署名的人工注音。按内容 hash 取样并划分调试集、保留集；评测文本不写入生产词典。Tatoeba 数据为 CC BY 2.0 FR，样本作者、注音作者和原句链接保存在评测输出的 `attribution.tsv`。wordfreq 3.1.1 仅为该工具提供简繁筛选映射，未写入生产数据；其数据为 CC BY-SA 4.0，署名 Robyn Speer。

`engine-probe evaluate <cases.tsv> <results.tsv>` 对同一输入逐键调用真实 provider，输出首项、前九候选及整串输入耗时。可用环境变量 `QM_IME_PROBE_DATA_DIR`（包含 ime 的数据目录）和 `QM_IME_PROBE_USER_DIR` 指定旧/新词库及独立用户目录；默认路径保持不变。

```powershell
python qmclient_scripts/ime/prepare_corpus_eval.py
# 旧库、新库分别调用 evaluate，必须使用独立用户目录并保留两份输出。
python qmclient_scripts/ime/compare_corpus_eval.py tmp/ime/slice/corpus-baseline-heldout.tsv tmp/ime/slice/corpus-final-heldout.tsv --output tmp/ime/slice/corpus-heldout-comparison.md
```

统计表示给定拼音能否还原原始文本，简拼有歧义；不能解释成玩家实际正确率或全互联网覆盖率。Qm 没有用这些保留样本调权，无法确认上游大语料是否曾包含同文句子。全量改善和退化样本同时留存。
