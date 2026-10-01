# 性能验证参考

新增或修改性能敏感路径、修复性能回退，以及宣称性能改善时读取。本文件维护性能验证命令与证据；实现取舍见 [C++ 性能参考](../../qmclient-cpp-conventions/references/advanced/performance-workflow.md)。

## 选择观察方式

- 可独立调用的 CPU 代码路径优先使用 Google Benchmark，直接调用生产实现，按本次风险选择相关 case。普通文档、翻译内容或不影响热路径的改动不强制跑 benchmark。
- 帧调度、GPU、I/O、设备和玩家交互卡顿需要对应真实场景的日志或 profiler；CPU 微基准不能证明整帧 p95/p99 或实机体验。
- `quick/default/full` gate 当前不执行 `qm-benchmarks`；功能测试和 gate 通过均不代表性能验证通过。

## 当前构建入口

生产基准在 `src/test/benchmark/`，CMake 目标为 `qm-benchmarks`，全量运行目标为 `run_cxx_benchmarks`。当前目标需同时满足 Google Benchmark 依赖、`SERVER=ON` 和 FreeType 可用；系统 `benchmark>=1.9` 或 `DOWNLOAD_BENCHMARK=ON` 均可提供依赖，下载版本以根 CMake 配置为准。

已有 Release build 包含目标时直接构建，不重复配置。缺目标时先核对 CMakeCache、依赖和配置输出；Windows 使用封装入口，以下命令从仓库根执行：

```text
qmclient_scripts/cmake-windows.cmd -G Ninja -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release -DSERVER=ON -DDOWNLOAD_BENCHMARK=ON
qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target qm-benchmarks -j 14
```

Linux/macOS 使用 `cmake` 配置和构建同名目标，跨平台验证使用独立 build 目录；不复用 Windows CMakeCache。Benchmark 的配置、构建和运行与同一目录的测试、打包、gate 必须串行。

## 统一运行与结果留存

推荐从仓库根使用 [benchmark 入口](../../../../qmclient_scripts/benchmark/README.md)，串行构建当前源码、枚举过滤器并运行真实二进制，校验每次重复及错误状态：

```text
python qmclient_scripts/benchmark/run.py --filter "^AnimBenchmark/BM_AnimIdleFrame/"
python qmclient_scripts/benchmark/run.py --filter ".*" --smoke
python qmclient_scripts/benchmark/run.py --filter "BM_FontCategoryClassify|BM_AnimLayoutCacheHits" --min-time 1 --warmup 0.2 --repetitions 7
```

默认至少计时 0.2 秒、预热 0.05 秒、重复 5 次，并使用原生随机交错。样本波动高时延长测量、核对系统负载/电源及本次进程的 CPU 亲和性；这些参数不是统一验收阈值。`--smoke` 只证明 case 能完成，摘要不含性能指标。

产物默认放独立 `tmp/benchmarks/run-<id>/`，可用 `--output-dir` 指定尚不存在的目录；不会覆盖已有基线。原生 `results.json` 保留原始重复和 aggregate，`summary.json` 分别记录 CPU/real time 与 CV，`run.json` 记录 revision/diff、CMake 设置和二进制来源。`MEASURED` 不意味着无回退或样本充分稳定。

过滤未命中、原生 skip/error、非零退出、缺失/非法 JSON 或重复样本、测量期间源码/二进制变化会失败。Windows ANSI 主机名按实际编码记录，原始 JSON 不改写。修改运行或摘要逻辑时运行：

```text
python -m unittest qmclient_scripts.tests.test_benchmark_results qmclient_scripts.integration.test_benchmark_runner
```

`run_cxx_benchmarks` 保留为原生全量入口，它不自动附带上述重复、预热、过滤和结果校验。直接运行原生二进制时从 build 目录执行，并保留命中的 case、完整原始 JSON 与实际参数；字体基准已按源码路径加载数据。

## Case 与可比性

- 用生产接口测量真实职责，不复制算法或只测为基准重写的实现；必要拆分应同时服务模块化和行为测试。
- 按职责覆盖实际相关的空/小/大输入、冷启动或稳态、命中或失效、启用或禁用；不要用不同用户路径混成一个无法归因的结果。
- 明确哪些准备与清理属于计时范围；冷路径和稳态路径分别注册。用 Google Benchmark 的 `DoNotOptimize` / `ClobberMemory` 等现有能力避免目标工作被优化掉。
- 基线与当前结果保持编译配置、工具链、硬件/电源状态、输入、过滤范围、计时语义和重复参数一致，记录源码 revision 及本次相关未提交改动。
- 比较匹配 case 的 CPU/real time、单位、吞吐与重复统计，注明使用哪种时间。没有同条件基线只报告本次测量；单次 ns/us 数值不证明收益，微基准分布不等于帧耗时 p95/p99。
- 本次改动导致回退时调查生产路径和测量范围，不削弱 case 或调宽阈值来放行；无性能收益的改动需要说明其正确性或架构价值。

## 客户端场景与旧 HTML 工具

`qmclient_scripts/perf/` 保留为客户端 `qm_perf_debug` 日志的离线分析工具，输出 HTML 和 JSON；它用于帧耗时、卡顿归因、配置变化和采样偏差，使用方式见 [perf/README.md](../../../../qmclient_scripts/perf/README.md)。常规代码性能验证优先 benchmark，不要求生成 HTML。

场景实测记录操作路径、客户端版本、renderer、窗口模式、刷新率、UI scale 和采样方式；只有同条件操作才能作为严格对比。自动选择上一份日志仅是趋势参考。

仅在修改诊断或分析器时运行其对应测试：在 `qmclient_scripts/perf/` 执行 `bun run test`；TypeScript 逻辑变化补 `npx --no-install tsc --noEmit`（需已安装本地依赖）。跨语言字段用生产日志与解析输出验证，源码字符串合同只能检查真正的构建/架构边界。

未来淘汰 HTML 工具时，先确认真实场景日志有替代消费入口，再同步清理 CLI、解析与报表模块、测试、依赖、忽略项和文档；不能仅因已有 CPU benchmark 就断定场景诊断不再需要。

性能交付记录实际命令、过滤 case、环境、基线和关键结果；未运行的场景、设备或平台验证如实说明，不能用构建、功能测试或 gate 替代。
