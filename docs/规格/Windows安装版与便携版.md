# Windows 安装版与便携版

状态：已实现，并完成本地验证。依据当前用户要求，客户端存储模式由专门构建决定。

## 存储与构建

普通 Windows 客户端及 Setup 安装版固定使用系统用户存档目录，通常是 C 盘的 `%APPDATA%/DDNet`。安装程序可以选择程序位置，但不提供玩家存档位置选项。

便携客户端必须单独使用 `QMCLIENT_PORTABLE=ON` 编译。所有自动读取和保存的玩家配置、皮肤、截图、地图缓存、回放等归入程序旁的 `profile/`。资源从程序旁的 `data/` 读取。便携版不查找系统用户存档，不读取程序根目录、工作目录或资源目录中的玩家配置，不通过 `storage.cfg` 切换存储模式。缺少 profile 时创建它；profile 被文件占用或无法创建时初始化失败，不能退回系统用户目录或工作目录。移动整个程序目录时 profile 一起移动。玩家主动使用命令导入指定文件仍按原命令语义执行。

普通包与便携包不能靠复制配置文件互相转换。已有本地存档不会自动跨目录迁移；需要便携存档时，关闭客户端后自行复制到 profile。

`storage.cfg` 仍随包供服务端等非客户端工具使用；客户端忽略它。便携包中的 `config_directory.bat` 打开旁边的 profile。便携承诺针对客户端玩家数据，不代表 Windows、驱动或第三方程序不会使用系统临时目录。

Windows 构建通过 `qmclient_scripts/cmake-windows.cmd`。普通与便携使用不同构建目录，避免 CMake 缓存和二进制混用：

```text
cmd /c qmclient_scripts/cmake-windows.cmd -G Ninja -S . -B cmake-build-portable -DCMAKE_BUILD_TYPE=Release -DQMCLIENT_PORTABLE=ON -DPREFER_BUNDLED_LIBS=ON -DDOWNLOAD_GTEST=ON
cmd /c qmclient_scripts/cmake-windows.cmd --build cmake-build-portable --target package_default -j 14
```

## 安装与自动更新

采用现有 Inno Setup EXE 安装器。固定 AppId、复用安装目录、覆盖程序文件并保留用户文件，卸载不删除用户存档。当前项目已有 Setup 启动和签名验证链，维持 EXE 能复用这些接口；MSI 的修复及企业部署模型暂没有对应需求。

CI 的普通 Windows 行构建正常 ZIP、7z 和 Setup；Windows Portable 行编译独立便携客户端，只生成便携 ZIP 与 7z，不生成 Setup。Setup 载荷准备器拒绝从已标记的便携或隔离测试构建目录取文件。

普通 ZIP 更新资产保持旧名称与 schema 1，保留旧客户端兼容性。便携资产为 `QmClient-windows-portable.zip`、其签名和 `QmClient-windows-portable-update.json` 及其签名。便携版只选择这些附件，不因附件缺失回退普通包，也不因残留安装标记改用 Setup。Setup 安装版使用单独签名的 Setup 清单；旧发布缺少 Setup 资产时保留正常 ZIP 更新回退。

签名脚本和更新器拒绝更新载荷中的 profile 文件；更新保留既有 storage.cfg，避免影响服务端。自动更新成功后客户端构建类型必须保持一致。

## 验证入口

便携真实进程场景：`python qmclient_scripts/integration/e2e_storage_modes.py cmake-build-portable --normal-build cmake-build-release`。在 tmp 内复制程序、创建冲突配置、保存玩家名、搬家恢复，再验证 profile 被文件占用时失败。

通用 Windows 进程回归使用 `DEV=ON、QMCLIENT_TEST_STORAGE=ON` 的独立测试客户端，由 runner 注入临时存档目录。它与便携选项互斥，禁止发布打包；普通客户端遇到测试隔离环境变量直接拒绝启动，防止测试污染实际存档。

自动测试不能代替真实 GitHub 发布：发布上传、实际远程下载和玩家安装目录上的升级仍需要发布环境验收。

## 本地验证记录

- `default` gate 为 PASS，14 项通过、零失败。C++ 主入口 3686 项（包含单元、集成及静态合同），音乐入口 170 项；Python 三组 168、60、37 项；Rust 单元和文档入口通过。报告：`tmp/strict-storage-final-gate.json`。
- 实际普通版、便携版客户端及更新器构建通过；两种 ZIP 分开生成，便携版另生成 7z。普通 ZIP 的三个更新可执行文件及配置目录脚本亦与普通构建匹配。便携 ZIP 与本地便携客户端、服务端、更新器逐一匹配哈希，配置目录脚本正确，未包含 profile 数据，中文路径未损坏。
- 便携配置保存、目录搬家恢复、忽略目录外自动配置、profile 被文件占用时失败、普通版拒绝测试存储覆盖，均由隔离真实进程验证。日志：`tmp/strict-storage-portable-normal-e2e.log`。
- 实际便携 ZIP 使用隔离测试密钥验证了规范化、独立清单生成和签名链；这些测试签名不属于发布密钥，不用于玩家自动更新。
- 当前普通构建载荷编译 Setup 成功。使用独立测试 AppId，在工作区临时目录完成安装、同版本覆盖重装（替换旧程序及资源，清除指定旧字体）和卸载，保留未知用户文件。日志：`tmp/strict-storage-setup-smoke.log`。安装器的精简中文资源仍有系统消息使用默认英文；本轮未做安装器视觉和完整语言验收。
- 移除了两项只依赖调用源码文本的旧测试；配置迁移的生产接口测试、更新清单和 Rust 校验测试及真实进程持久化场景保留。没有以匹配新的调用文本代替行为验证。

本轮没有运行真实 GitHub 上传和远程自动更新，也没有验证其他操作系统的运行时路径；便携专用构建当前仅用于 Windows。
