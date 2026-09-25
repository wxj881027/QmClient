# 地图上传目标与搜索

已确认范围：地图上传卡片显示「目标服务器：shengyan北京服」；选择地图弹窗保留文件夹浏览，输入关键字后按地图文件名搜索 `maps`、`downloadedmaps` 的全部子目录，忽略英文大小写，显示完整来源路径以区分同名文件。清空关键字恢复原浏览位置；选择文件不会自动上传。

实现约定：卡片和选择器实现放入 `src/game/client/QmUi/cards/QmMapUpload.cpp`，目录索引与名称匹配放入同目录的 `QmMapUploadSearch.h`。每次打开选择器重建索引，搜索期间逐帧扫描目录，输入变化仅筛选已有索引。上传接口与请求内容沿用现有实现。

测试代码先行覆盖两个根目录的递归搜索、中文与英文大小写匹配、路径保留、排除非地图及范围外文件、空结果与索引重置。遵循本次要求，不运行编译或测试；实现后运行 quick 源码卫生门禁，并做只读审查。

## 实现与验证结果

- 已完成目标服务器显示、跨目录搜索、来源路径显示与清空后恢复浏览。各存储位置分别扫描，避免同名目录被存储层合并；鼠标按下至松开期间暂停索引更新，避免结果排序移动正在点击的行。
- 上传卡片仍通过全局目录与 `QmCardRenderHook` 渲染，高度测量同步增加服务器信息行。相关实现已从 `menus_qmclient.cpp` 移至独立模块，并登记构建源文件与测试源文件。
- 版本由 `3.9.6` 更新为 `3.9.7`。
- 只读审查 findings：本次范围内未发现阻塞问题。检查了搜索结果到上传路径及 StorageType 的传递、索引更新时的选择保留、只读渲染入口及卡片高度。
- `python qmclient_scripts/gate/check_gate.py --mode quick --report-json-path tmp/map-upload-gate.json`：PASS，11 项通过，0 警告，0 失败。日志：`tmp/map-upload-gate.log`。
- 新增模块和测试的 `clang-format --dry-run --Werror`、`git diff --check` 均通过。
- 已执行翻译提取、生成、校验及重复项审查。本次新增 3 个 key，12 种语言均有译文；逐项核对生成文件，原有词条内容保持不变。生成器带出的其他工作区改动未并入运行时语言文件。
- 最终 `python qmclient_scripts/languages_qmclient/validate.py --incremental` 未通过：当前工作区另有 `Locate` 与表情功能缺失译文，以及 3 个表情功能中文源码 key 违规；本次新增 key 无缺失。日志：`tmp/map-upload-i18n-validate-final.log`。重复项审查无重复 key、无空译文，现有同译文与相似词条保留。
- 未编译、未运行 C++/Rust 测试、未启动客户端做 UI 验证。测试代码已补齐，不能将其视为运行通过。
