# Google Benchmark 编写入口

本目录继承 `src/test/AGENTS.md`。新增或修改 case 前必须读取 `.agents/skills/qmclient-verification-gate/references/performance.md` 的“Case 书写”与“Case 与可比性”，运行与证据统一按该参考执行。

- 直接测量生产接口，禁止在 case 中复制算法或重新实现待测职责。
- 每项 case 明确输入规模、状态、计时范围和工作量单位；冷路径与稳态路径分别注册。
- 每轮迭代必须执行所命名的工作；需要复位的状态应明确复位成本是否计时，避免耗尽队列、缓存命中或动画收敛后只测空操作。
- fixture 显式准备与恢复全局状态，允许随机交错与重复运行；不依赖真实用户目录、互联网或其他 case 留下的数据。
- case 文件按功能域放置并显式注册到 `qm-benchmarks`，辅助入口不能增加第二个 main。新增 case 用统一 runner 确认过滤命中并完成 smoke；smoke 通过只证明可以运行。

测量条件、优化屏障、正确性预检和性能结论边界由性能参考维护。benchmark 不替代行为回归测试，测试通过也不代表性能通过。
