# OpenGL / Vulkan Qm 管线拆分

状态：实现和静态审阅已完成。用户已授权保持行为的拆分、本地 master 合入与 PR 交付；未进行构建或运行验收。

## 范围与基线

- 工作分支：`QimenG/qm-render-pipeline-split`。
- 工作目录：`C:/Users/QimenG/.codex/worktrees/qm-render-pipeline-split/QmClient`。主工作区发生并行切分支后，本任务改动已转移至此；原 `QimenG/split-qm-render-pipelines` 分支保留原状。
- 起点：`ab0c7d9673`，与任务开始时的本地及远端 master 一致。
- 官方边界：对照 DDNet 共同祖先 `de609e845ee1deea0a707c5507b4288a381b4e66`；分组 Quad、Sprite Multi Push 属于官方实现。
- 拆分 Qm 新增的 Media Island SDF、Rounded Rect SDF、Textured MSDF、Gaussian / Dual 模糊，以及离屏目标创建、回绘、捕获与读回。
- 保留 shader 文件、命令格式、配置默认值、功能开关、能力发布、失败回退、GPU 调用顺序与线程归属。
- 工作区原有 `src/game/client/components/menus_ingame.cpp` 改动不属于本任务，不纳入提交。

## 实现边界

- OpenGL：Qm shader program 类型与管线实现独立维护；离屏目标基础实现单独维护；GLES 继续使用现有类名映射与源码复用方式。
- Vulkan：保留后端私有设备、描述符、分配器及命令缓冲所有权；Qm 管线与离屏目标方法移入类定义后包含的私有实现文件，不增加运行时接口或状态副本。
- 核心初始化、销毁和命令注册保留必要接入，并维持原先执行位置。
- CMake 同步登记客户端、GLES 与 `map_render` 的新增源码。
- 只处理受本次拆分影响的测试。源码字符串不能证明 GPU 运行时行为，不把失效的实现镜像搬到新路径继续保留。

## 协作与验证

- Gemini：已完成只读清单整理，辅助定位新增管线、调用点、官方边界与测试引用；结论已结合当前源码核对。
- Claude：已完成实现、生命周期及平台接入的只读复核，未发现确定问题。
- 静态搬迁对照：46 个方法体，以及展开新接入函数后的 6 处初始化、清理和命令注册路径，与拆分前一致。
- 已补充 OpenGL / GLES program 类型的编译期合同测试，删除受拆分影响的 16 项源码实现镜像测试及 2 处局部断言；现有行为测试保留。已补充测试，未执行。
- `git diff --check` 通过。本次仅进行静态审阅，未编译，未运行测试或 gate，未启动客户端。
- 运行时效果、跨平台编译及视觉一致性需另外执行对应验证后才能确认。

## 交付顺序

1. 完成拆分及必要配套，静态复核最终差异。
2. 仅提交本任务文件，将提交合入本地 master。
3. 推送工作分支，向远端 master 发起 PR。
4. 最终切换并停留在本地 master。
