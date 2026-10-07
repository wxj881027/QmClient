# Google Benchmark 运行与结果

QmClient 代码性能的专用入口；生产 case 在 `src/test/benchmark/`，构建目标为 `qm-benchmarks`。本入口串行构建当前源码、枚举过滤范围、运行真实二进制并校验原始 JSON。客户端帧、GPU、I/O 和玩家交互诊断仍按 [perf 说明](../perf/README.md) 选择场景。

## 日常运行

从仓库根执行；Windows 需要可用 Python，构建自动使用 `qmclient_scripts/cmake-windows.cmd`。构建目录已有 Google Benchmark、SERVER 和 FreeType 条件即可使用；首次配置见 [性能验证参考](../../.agents/skills/qmclient-verification-gate/references/performance.md)。同一 build 的构建、测试、运行与 gate 由调用者串行安排，入口不管理其他任务的进程。

```text
python qmclient_scripts/benchmark/run.py --filter "^AnimBenchmark/BM_AnimIdleFrame/"
python qmclient_scripts/benchmark/run.py --filter ".*" --smoke
python qmclient_scripts/benchmark/run.py --filter "BM_FontCategoryClassify|BM_AnimLayoutCacheHits" --min-time 1 --warmup 0.2 --repetitions 7
```

过滤器必须显式给出，全量使用 `.*`。默认每个 case 至少计时 0.2 秒、预热 0.05 秒、重复 5 次，并开启原生随机交错；使用 `--no-interleave` 可显式关闭。短 case 有较高波动时延长时间并核对环境，默认参数不是性能质量阈值。

`--smoke` 使用原生 dry-run，只证明每个 case 能完成且无原生错误；摘要不输出耗时/吞吐指标。完整测量要求 Release 或 RelWithDebInfo，以及 Google Benchmark 库本身为 release。`--timeout` 控制枚举和测量进程，默认 600 秒；构建按正常入口完成，不因计时到期留下后台编译子进程。

## 产物和失败

默认创建独立 `tmp/benchmarks/run-<id>/`；`--output-dir tmp/benchmarks/<场景>-<版本>` 可以命名结果，已有目录会拒绝覆盖。

| 文件 | 内容 |
| --- | --- |
| `build.log`、`list.log`、`benchmark.log` | 构建、注册项和测量进程输出 |
| `command.json` | 实际命令、工作目录与命中的 case |
| `results.json` | 原生结果，保留每次重复和 aggregate，不改写字节 |
| `summary.json` | 逐 case 的 CPU/real 中位数、均值、范围及 CV；统一为 ns |
| `run.json` | 状态、源码 revision/diff 指纹、CMake 编译设置和二进制 SHA-256 |

`MEASURED` 表示有效测量已完成，不表示相对基线没有回退或样本足够稳定。CV 是重复耗时的样本标准差/均值，无量纲；摘要不将原生 CV aggregate 当耗时样本，也不把单次重复伪装成有波动统计。

空过滤、重复注册、原生错误或跳过、非零退出、超时、无 JSON、缺失重复、重复索引冲突、零迭代、非法单位/计时/吞吐或标签漂移都会失败。构建与测量期间源码或二进制发生改变时，保留原始证据并拒绝发布成功摘要。Windows 原生主机名可能使用 ANSI：优先读 UTF-8，必要时使用本机 `mbcs` 并在摘要记录 `json_encoding`。

## Case 的实际职责

| 领域 | 当前输入和边界 |
| --- | --- |
| 格式化 | 固定日志文案，`str_format` 与 `snprintf`，每次输出可观察 |
| 动画 | 16/64/256 节点的 idle、活跃弹簧、presence 与可见性切换 |
| 布局缓存 | 256/4096 固定键命中；4096/8192 的两组不相交键交替 churn |
| 卡片测量 | 搜索聚合/翻译单卡，稳定配置与 advanced 配置交替 |
| 文本 | 混合码点分类、UTF-8 解码/导航/验证 |
| 网络 | 16 KiB 快照样式 payload 的 Huffman 压缩/解压，校验真实往返 |
| 快照 | 128/512/1024 个 10-int 实体，全部字段 +1 的 diff/unpack |
| 字形 | 256 个不重复 CJK 码点，16/24/36 px，查表 + FreeType 光栅化 |

字形用例从当前源码加载字体，每次重复初始化并释放 Face/Library，测量不含文件加载、应用字形缓存命中或 GPU 上传；其新名字 `BM_GlyphRasterizeBatch` 避免把重复批次当完整冷启动。缓存命中与 churn 分开注册，`v2` 标签明确修正后的计时语义。修改场景后重新建立基线，不沿用旧语义的数字算性能收益。

## 基线和噪声

比较时匹配输入、case、标签、编译设置、硬件/电源、时间类型、时间长度、预热、重复次数和 CPU 亲和性。使用真实同条件 baseline/current，不用其他 revision 的未关联结果或 UI 帧 p95/p99 替代微基准。可按需使用 Google Benchmark 上游 `tools/compare.py`，不另造全仓统一百分比阻断规则；本工具不自动选择历史基线。

当前 Windows 实测中，短 case 的 CV 可超过 25%，延长运行后有些降到约 3%；4096 键用例在固定单 CPU 的局部测量中约 2.7%。不同 case 集合与时间顺序也会影响这些数字，不能单凭这几次结果证明某个环境选项的通用收益。需要固定 CPU 时仅对本次测量进程或调用 shell 设置并恢复，记录实际 mask；工具不改系统电源方案或其他任务进程。

## 工具验证

```text
python -m unittest qmclient_scripts.tests.test_benchmark_results qmclient_scripts.integration.test_benchmark_runner
```

单元测试调用生产摘要接口；集成测试用真实 fake 子进程验证参数、日志、错误、超时、编码和输出保留。fake 只实现外部进程协议，不复制测量统计实现。真实 `qm-benchmarks` 全量 smoke 与过滤重复测量作为独立证据，不能称客户端玩家端到端测试。

## Phosphor 图集与字体 CPU 对照

入口调用真实 `CQmIconManager` 与 `CTextRender`，设备接口仅计数，不执行 `graphics_threaded` 命令构造或 GPU 工作。每批使用注册表前 24 个相同图标，像素映射 1:1；字号 16／24／36，Regular 样式。不得把本结果称 GPU 或整帧性能。

```text
python qmclient_scripts/benchmark/run.py --filter "BM_Icon(RenderCpuBoundary|FreshGlyphsCpuBoundary|AtlasReloadCpuBoundary|WarmSizeSwitchCpuBoundary)" --smoke
python qmclient_scripts/benchmark/run.py --filter "BM_Icon(RenderCpuBoundary|FreshGlyphsCpuBoundary|AtlasReloadCpuBoundary|WarmSizeSwitchCpuBoundary)" --min-time 0.2 --warmup 0 --repetitions 5
```

| case | 范围 | 参数与单位 |
|---|---|---|
| BM_IconRenderCpuBoundary | 字形／图集已准备后的绘制 CPU 逻辑；TTF 缓存容器与即时容器分别报告 | path 0=MTSDF、1=缓存TTF、2=即时TTF；第二参数字号；第三参数0=高对比、1=低对比；每迭代24图标 |
| BM_IconFreshGlyphsCpuBoundary | 新生产 renderer 首次生成24个字形及即时绘制；初始化、字体文件加载和销毁不计时 | 字号；每重复固定16个真实fresh批次；不能与AtlasReload横比为完整冷启动 |
| BM_IconAtlasReloadCpuBoundary | OS文件缓存预热的完整Regular图集读取、JSON解析、PNG解码和CPU资源替换 | 每迭代一次整张图集；设备上传耗时不计，只记字节 |
| BM_IconWarmSizeSwitchCpuBoundary | 三种字号预热后按16→24→36循环切换 | 0=MTSDF、1=即时TTF；每迭代24图标，无新字形上传 |

Warm结果在结束时检查实际图标／绘制数与零上传；首次字形生成强制检查实际上传。设备生命周期验证放在测量外，避免TTF设备替身的哈希表维护影响对照。原生结果中的 `upload_bytes_per_batch`、`container_bytes_per_batch` 和绘制计数属于接口边界观察，不等于实测显存或GPU带宽。按实际batch耗时除以24可计算ns/icon，不能把batch直接标成单图标。

此矩阵应使用 `--warmup 0`：各稳态 case 已自行准备并预热真实缓存；固定首批字形／重载 case 不需要框架额外预热。Google Benchmark 1.9.4 会把全局非零预热应用到固定批次，计时外字体初始化可能因此放大到很长的实际运行时间。短时首批字形 case 使用 `UseRealTime`，吞吐率基于实际经过时间，不能把Windows舍入为零的CPU累计时间解读为零成本。
