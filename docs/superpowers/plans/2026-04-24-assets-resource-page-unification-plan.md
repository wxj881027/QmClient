# 资源页面与 Workshop 分类统一实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 将资源页面重构为“本地资源管理 + Workshop 远端预览下载”的统一架构，并在本期接入 `gui_cursor`、`arrow`、`strong_weak`、`entity_bg` 四类新增资源。

**架构：** 先抽出资源分类注册表与路径决策 helper，用测试冻结分类、迁移和回退语义；再让资源页面改为依赖注册表渲染与拉取远端数据；最后补上 `gui_cursor / arrow / strong_weak` 的运行时资源优先级切换，以及 `entity_bg` 对既有 `cl_background_entities` 链路的桥接。

**技术栈：** C++17, gtest, DDNet/QmClient UI system, CMake, Windows Ninja build

---

## 执行策略

本计划按“先冻结纯逻辑，再改 UI 与运行时入口”的顺序执行：

1. 先做注册表 / 路径 / 迁移命名等纯函数与测试，避免把资源页大文件越改越散。
2. 再收敛 `menus_settings_assets.cpp` 的分类与卡片渲染逻辑，完成资源页统一。
3. 最后处理 `gui_cursor / arrow / strong_weak` 的运行时优先级和 `entity_bg` 的特殊桥接。

这样可以把风险拆开：

- 资源页 UI 改动不会和运行时资源加载问题混在一起。
- 运行时链路改动前已经有注册表和迁移规则可依赖。
- `entity_bg` 作为特殊分类，单独收尾，不拖累通用分类改造。

## 文件清单

| 文件 | 职责 |
|------|------|
| `docs/superpowers/specs/2026-04-24-assets-resource-page-unification-design.md` | 本计划对应的设计规格 |
| `docs/superpowers/plans/2026-04-24-assets-resource-page-unification-plan.md` | 本实现计划 |
| `src/game/client/components/assets_resource_registry.h` | 资源分类注册表、分类元数据与路径 helper 声明 |
| `src/game/client/components/assets_resource_registry.cpp` | 分类注册表实现、安装路径 / Workshop 匹配 / 默认资源规则 |
| `src/game/client/components/menus_settings_assets.cpp` | 资源页面主逻辑、远端解析、卡片渲染、下载 / 删除 / 应用 |
| `src/game/client/components/menus_settings.cpp` | 既有 `entity_bg` 设置页链路对接点 |
| `src/game/client/components/background.cpp` | `entity_bg` 对 `.map` / `.png` 的现有加载逻辑 |
| `src/game/client/components/menus.cpp` | `cl_background_entities` console chain |
| `src/game/client/components/nameplates.cpp` | `arrow` 与 `strong_weak` 运行时消费点 |
| `src/game/client/render.cpp` | `gui_cursor` 运行时消费点 |
| `src/game/client/gameclient.cpp` | 通用图片加载、默认图片回退、资源重载入口 |
| `src/game/client/gameclient.h` | 资源重载 helper 声明 |
| `src/engine/shared/config_variables.h` | `entity_bg` 相关既有配置项确认点 |
| `src/test/assets_resource_registry_test.cpp` | 分类注册表、路径、Workshop 映射、default 规则测试 |
| `src/test/assets_resource_migration_test.cpp` | `legacy` 导入命名、旧路径优先级、回退语义测试 |
| `CMakeLists.txt` | 注册新增测试文件 |

---

### 任务 1：冻结资源分类注册表的纯逻辑语义

**文件：**
- 创建：`src/game/client/components/assets_resource_registry.h`
- 创建：`src/game/client/components/assets_resource_registry.cpp`
- 创建：`src/test/assets_resource_registry_test.cpp`
- 修改：`CMakeLists.txt`

- [ ] **步骤 1：定义分类元数据结构与枚举**

在 `src/game/client/components/assets_resource_registry.h` 中声明最小可测试结构，至少覆盖分类类型、安装目录、Workshop 能力和预览类型。

```cpp
enum class EAssetResourceKind
{
	DIRECTORY,
	NAMED_SINGLE_FILE,
	MAP_FILE,
};

enum class EAssetPreviewKind
{
	PNG_TEXTURE,
	MAP_PREVIEW_OR_ICON,
};

struct SAssetResourceCategory
{
	const char *m_pId;
	EAssetResourceKind m_Kind;
	const char *m_pInstallFolder;
	bool m_WorkshopEnabled;
	const char *m_pWorkshopCategory;
	const char *m_pWorkshopCategoryAlt;
	EAssetPreviewKind m_PreviewKind;
	bool m_LocalOnlyBadge;
};
```

- [ ] **步骤 2：把本期 9 个分类写成注册表常量**

在 `src/game/client/components/assets_resource_registry.cpp` 中写出固定注册表，至少包含：

```cpp
static constexpr SAssetResourceCategory s_aAssetCategories[] = {
	{"entities", EAssetResourceKind::DIRECTORY, "assets/entities", true, "entities", "entity", EAssetPreviewKind::PNG_TEXTURE, false},
	{"game", EAssetResourceKind::DIRECTORY, "assets/game", true, "game", nullptr, EAssetPreviewKind::PNG_TEXTURE, false},
	{"hud", EAssetResourceKind::DIRECTORY, "assets/hud", true, "hud", nullptr, EAssetPreviewKind::PNG_TEXTURE, false},
	{"emoticons", EAssetResourceKind::DIRECTORY, "assets/emoticons", true, "emoticons", "emoticon", EAssetPreviewKind::PNG_TEXTURE, false},
	{"particles", EAssetResourceKind::DIRECTORY, "assets/particles", true, "particles", "particle", EAssetPreviewKind::PNG_TEXTURE, false},
	{"gui_cursor", EAssetResourceKind::NAMED_SINGLE_FILE, "assets/gui_cursor", false, nullptr, nullptr, EAssetPreviewKind::PNG_TEXTURE, true},
	{"arrow", EAssetResourceKind::NAMED_SINGLE_FILE, "assets/arrow", false, nullptr, nullptr, EAssetPreviewKind::PNG_TEXTURE, true},
	{"strong_weak", EAssetResourceKind::NAMED_SINGLE_FILE, "assets/strong_weak", false, nullptr, nullptr, EAssetPreviewKind::PNG_TEXTURE, true},
	{"entity_bg", EAssetResourceKind::MAP_FILE, "maps", false, nullptr, nullptr, EAssetPreviewKind::MAP_PREVIEW_OR_ICON, true},
};
```

- [ ] **步骤 3：为注册表写失败测试**

在 `src/test/assets_resource_registry_test.cpp` 中至少覆盖以下断言：

```cpp
TEST(AssetsResourceRegistry, IncludesAllApprovedCategories)
{
	EXPECT_NE(FindAssetResourceCategory("gui_cursor"), nullptr);
	EXPECT_NE(FindAssetResourceCategory("arrow"), nullptr);
	EXPECT_NE(FindAssetResourceCategory("strong_weak"), nullptr);
	EXPECT_NE(FindAssetResourceCategory("entity_bg"), nullptr);
}

TEST(AssetsResourceRegistry, LocalOnlyCategoriesExposeBadgeFlag)
{
	EXPECT_TRUE(FindAssetResourceCategory("gui_cursor")->m_LocalOnlyBadge);
	EXPECT_TRUE(FindAssetResourceCategory("entity_bg")->m_LocalOnlyBadge);
	EXPECT_FALSE(FindAssetResourceCategory("hud")->m_LocalOnlyBadge);
}
```

- [ ] **步骤 4：注册测试并运行一次红灯 / 绿灯闭环**

先在 `CMakeLists.txt` 测试列表中加入：

```cmake
    assets_resource_registry_test.cpp
```

运行：

```powershell
qmclient_scripts\cmake-windows.cmd --build build-ninja --target run_cxx_tests -j 10 | Select-Object -Last 120
```

预期：

- 首次因为符号未实现或测试未接线而失败
- 补齐后重新运行，`assets_resource_registry_test` 通过

- [ ] **步骤 5：提交本任务**

```bash
git add src/game/client/components/assets_resource_registry.h src/game/client/components/assets_resource_registry.cpp src/test/assets_resource_registry_test.cpp CMakeLists.txt
git commit -m "test(assets): lock resource category registry semantics"
```

---

### 任务 2：冻结 default 与 legacy 迁移规则

**文件：**
- 创建：`src/test/assets_resource_migration_test.cpp`
- 修改：`src/game/client/components/assets_resource_registry.h`
- 修改：`src/game/client/components/assets_resource_registry.cpp`
- 修改：`CMakeLists.txt`

- [ ] **步骤 1：声明迁移 helper 接口**

在注册表 helper 中增加最小纯函数接口：

```cpp
bool AssetResourceNeedsLegacyImport(std::string_view CurrentName);
std::string NextLegacyAssetName(std::span<const std::string> ExistingNames);
bool IsProtectedDefaultAsset(std::string_view AssetName);
```

- [ ] **步骤 2：为迁移命名规则写失败测试**

在 `src/test/assets_resource_migration_test.cpp` 中加入：

```cpp
TEST(AssetsResourceMigration, DefaultNeverNeedsLegacyImport)
{
	EXPECT_FALSE(AssetResourceNeedsLegacyImport("default"));
}

TEST(AssetsResourceMigration, FirstImportedLegacyUsesStableName)
{
	std::array<std::string, 1> aNames{"default"};
	EXPECT_EQ(NextLegacyAssetName(aNames), "legacy");
}

TEST(AssetsResourceMigration, NameCollisionUsesIncrementingSuffix)
{
	std::array<std::string, 4> aNames{"default", "legacy", "legacy_2", "custom"};
	EXPECT_EQ(NextLegacyAssetName(aNames), "legacy_3");
}
```

- [ ] **步骤 3：实现 helper 并保持规则只依赖输入，不碰 IO**

在 `assets_resource_registry.cpp` 中实现上述 helper，要求：

- `default` 永远受保护
- `legacy` 是首次导入名
- 冲突时只生成稳定递增后缀

- [ ] **步骤 4：注册测试并运行定向验证**

在 `CMakeLists.txt` 测试列表中加入：

```cmake
    assets_resource_migration_test.cpp
```

运行：

```powershell
.\build-ninja\testrunner.exe --gtest_filter=AssetsResourceMigration*; if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
```

预期：

- 迁移命名与 `default` 保护测试通过

- [ ] **步骤 5：提交本任务**

```bash
git add src/game/client/components/assets_resource_registry.h src/game/client/components/assets_resource_registry.cpp src/test/assets_resource_migration_test.cpp CMakeLists.txt
git commit -m "test(assets): lock default and legacy migration rules"
```

---

### 任务 3：让资源页面改为依赖分类注册表

**文件：**
- 修改：`src/game/client/components/menus_settings_assets.cpp`
- 修改：`src/game/client/components/assets_resource_registry.h`
- 修改：`src/game/client/components/assets_resource_registry.cpp`

- [ ] **步骤 1：把资源页中硬编码分类与目录改为注册表查询**

目标是替换当前散落在 `menus_settings_assets.cpp` 中的：

- tab 到目录字符串的分支
- `WorkshopCategoryMatches` 的分类硬编码
- 本地目录扫描路径硬编码

最小改造目标：

```cpp
const SAssetResourceCategory *pCategory = FindAssetResourceCategory(pCategoryId);
dbg_assert(pCategory != nullptr, "asset category must exist");
const char *pInstallFolder = pCategory->m_pInstallFolder;
```

- [ ] **步骤 2：让 Workshop 解析只读取注册表元数据**

把：

```cpp
ParseWorkshopAssets(pJson, pWorkshopCategory, pWorkshopCategoryAlt, pInstallFolder, ...)
```

收敛为：

```cpp
ParseWorkshopAssets(pJson, *pCategory, vParsedAssets, aError, sizeof(aError));
```

并在 `pCategory->m_WorkshopEnabled == false` 时直接跳过远端拉取。

- [ ] **步骤 3：让本地扫描支持目录型 / 单文件集合型 / 地图型三种分类**

至少明确三条扫描规则：

```cpp
switch(pCategory->m_Kind)
{
case EAssetResourceKind::DIRECTORY:
	// assets/<category>/<name> 或 assets/<category>/<name>.png
	break;
case EAssetResourceKind::NAMED_SINGLE_FILE:
	// assets/<category>/<name>.png
	break;
case EAssetResourceKind::MAP_FILE:
	// maps/<name>.map
	break;
}
```

- [ ] **步骤 4：运行资源页相关回归测试与构建**

运行：

```powershell
qmclient_scripts\cmake-windows.cmd --build build-ninja --target game-client -j 10 | Select-Object -Last 80
```

预期：

- `menus_settings_assets.cpp` 编译通过
- 无因注册表收敛引入的编译错误

- [ ] **步骤 5：提交本任务**

```bash
git add src/game/client/components/assets_resource_registry.h src/game/client/components/assets_resource_registry.cpp src/game/client/components/menus_settings_assets.cpp
git commit -m "refactor(assets): drive resource page categories from registry"
```

---

### 任务 4：统一资源卡片头部样式与本地资源标识

**文件：**
- 修改：`src/game/client/components/menus_settings_assets.cpp`

- [ ] **步骤 1：把“已下载”标签高度与删除按钮对齐**

将当前写死的标签尺寸：

```cpp
const float TagHeight = 16.0f;
const float TagWidth = 58.0f;
```

改成依赖按钮高与文本宽度计算的形式，例如：

```cpp
const float TagHeight = DeleteButtonRect.h;
const float TagWidth = maximum(TagMinWidth, TextRender()->TextWidth(TagFontSize, pLabel, -1, -1.0f) + TagPadding * 2.0f);
```

- [ ] **步骤 2：为 local-only 分类增加“仅本地资源”标记**

在卡片或分类说明区域接入：

```cpp
if(pCategory->m_LocalOnlyBadge)
{
	Ui()->DoLabel(&BadgeRect, Localize("仅本地资源"), BadgeFontSize, TEXTALIGN_CENTER);
}
```

并保证它只出现在无远端源分类中。

- [ ] **步骤 3：让标题区域按剩余空间自适应**

要求：

- 删除按钮在最右
- 状态标签靠右但不抢掉全部标题空间
- 标题优先占满剩余宽度，必要时截断

- [ ] **步骤 4：构建验证**

运行：

```powershell
qmclient_scripts\cmake-windows.cmd --build build-ninja --target game-client -j 10 | Select-Object -Last 80
```

预期：

- 资源页 UI 编译通过
- `RenderAssetStatusTag` 和卡片头部布局无编译告警

- [ ] **步骤 5：提交本任务**

```bash
git add src/game/client/components/menus_settings_assets.cpp
git commit -m "feat(assets): unify resource card status tag and local-only badge"
```

---

### 任务 5：补齐 default 资源播种与 legacy 导入入口

**文件：**
- 修改：`src/game/client/components/menus_settings_assets.cpp`
- 修改：`src/game/client/components/assets_resource_registry.h`
- 修改：`src/game/client/components/assets_resource_registry.cpp`

- [ ] **步骤 1：在资源页初始化时保证每个分类可见 default**

最小规则：

```cpp
EnsureDefaultAssetVisible(*pCategory, vLocalItems);
```

其行为必须满足：

- `default` 永远排第一
- `default` 不可删除
- 即使本地目录为空也能展示默认项

- [ ] **步骤 2：为 `gui_cursor / arrow / strong_weak` 增加 legacy 导入流程**

在进入这些分类或启动资源页时执行一次兼容导入：

```cpp
if(pCategory->m_Kind == EAssetResourceKind::NAMED_SINGLE_FILE)
{
	TryImportLegacySingleFileAsset(*pCategory);
}
```

并严格遵循规格：

- 当前资源是 `default` 时不迁移
- 当前资源不是 `default` 时复制为 `legacy`
- 冲突时使用 `legacy_2`、`legacy_3`

- [ ] **步骤 3：应用成功后同步切换为导入后的命名资源**

至少保证：

```cpp
ApplyNamedSingleFileAsset(*pCategory, pImportedName);
```

不会只复制文件却不刷新当前使用状态。

- [ ] **步骤 4：运行定向测试与构建**

运行：

```powershell
.\build-ninja\testrunner.exe --gtest_filter=AssetsResourceMigration*; if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
qmclient_scripts\cmake-windows.cmd --build build-ninja --target game-client -j 10 | Select-Object -Last 80
```

预期：

- 迁移规则测试仍为绿色
- 客户端编译通过

- [ ] **步骤 5：提交本任务**

```bash
git add src/game/client/components/assets_resource_registry.h src/game/client/components/assets_resource_registry.cpp src/game/client/components/menus_settings_assets.cpp
git commit -m "feat(assets): seed default entries and import legacy single-file assets"
```

---

### 任务 6：接入 gui_cursor、arrow、strong_weak 的运行时资源优先级

**文件：**
- 修改：`src/game/client/gameclient.cpp`
- 修改：`src/game/client/gameclient.h`
- 修改：`src/game/client/render.cpp`
- 修改：`src/game/client/components/nameplates.cpp`
- 修改：`src/game/client/components/assets_resource_registry.h`
- 修改：`src/game/client/components/assets_resource_registry.cpp`

- [ ] **步骤 1：抽出单文件资源的实际加载路径决策 helper**

在 helper 中固定优先级：

```cpp
std::array<std::string, 3> BuildNamedSingleFileAssetCandidates(std::string_view CategoryId, std::string_view ActiveName)
{
	return {
		fmt("assets/{}/{}.png", CategoryId, ActiveName),
		fmt("{}{}.png", LegacyRootPrefix, CategoryId),
		GetBuiltinImageFilename(CategoryId),
	};
}
```

这里的 `LegacyRootPrefix` 需要分别对应：

- `gui_cursor.png`
- `arrow.png`
- `strong_weak.png`

- [ ] **步骤 2：让 `gui_cursor` 走自定义优先级加载，而不是只走 `IMAGE_CURSOR` 默认文件名**

在 `src/game/client/gameclient.cpp` 初始化图片时，为 `IMAGE_CURSOR` 增加专门分支，类似现有 `IMAGE_GAME / IMAGE_HUD`：

```cpp
else if(i == IMAGE_CURSOR)
	LoadNamedSingleFileImage(i, "gui_cursor", g_Config.m_ClAssetGuiCursor);
```

- [ ] **步骤 3：让 `arrow` 与 `strong_weak` 同样走命名资源加载**

至少增加：

```cpp
else if(i == IMAGE_ARROW)
	LoadNamedSingleFileImage(i, "arrow", g_Config.m_ClAssetArrow);
else if(i == IMAGE_STRONGWEAK)
	LoadNamedSingleFileImage(i, "strong_weak", g_Config.m_ClAssetStrongWeak);
```

要求：

- `nameplates.cpp` 与 `render.cpp` 无需理解新路径，只继续消费对应 `IMAGE_*`
- 真正的切换发生在图片加载阶段

- [ ] **步骤 4：为资源切换补刷新入口**

在 `gameclient.h/.cpp` 中补一个最小刷新 helper，使资源页应用时可以触发相应图片重载，而不要求重启客户端：

```cpp
void ReloadNamedSingleFileAssetImage(int ImageId, const char *pCategoryId, const char *pActiveName);
```

- [ ] **步骤 5：构建验证**

运行：

```powershell
qmclient_scripts\cmake-windows.cmd --build build-ninja --target game-client -j 10 | Select-Object -Last 80
```

预期：

- `IMAGE_CURSOR / IMAGE_ARROW / IMAGE_STRONGWEAK` 的加载分支编译通过
- `nameplates.cpp`、`render.cpp` 无需额外适配逻辑即可正常链接

- [ ] **步骤 6：提交本任务**

```bash
git add src/game/client/gameclient.cpp src/game/client/gameclient.h src/game/client/render.cpp src/game/client/components/nameplates.cpp src/game/client/components/assets_resource_registry.h src/game/client/components/assets_resource_registry.cpp
git commit -m "feat(assets): load named single-file resources from assets collection first"
```

---

### 任务 7：把新分类接到资源页面并桥接 entity_bg

**文件：**
- 修改：`src/game/client/components/menus_settings_assets.cpp`
- 修改：`src/game/client/components/menus_settings.cpp`
- 修改：`src/game/client/components/background.cpp`
- 修改：`src/game/client/components/menus.cpp`
- 修改：`src/engine/shared/config_variables.h`

- [ ] **步骤 1：让资源页面显示新增四个分类**

要求：

- `gui_cursor`
- `arrow`
- `strong_weak`
- `entity_bg`

都出现在与现有资源页一致的分类选择区域中。

- [ ] **步骤 2：让 `entity_bg` 直接桥接 `cl_background_entities`**

资源页应用地图型资源时必须写回既有配置：

```cpp
str_copy(g_Config.m_ClBackgroundEntities, pSelectedMapName);
GameClient()->m_pBackground->LoadBackground();
```

如果已有更合适的刷新入口，优先复用已有 DDNet 链路，不要新开平行配置。

- [ ] **步骤 3：为 `entity_bg` 增加“有预览图就显示预览，没有就显示默认图标”的渲染分支**

资源项渲染最小目标：

```cpp
if(HasEntityBgPreview(*pItem))
	RenderPreviewTexture(...);
else
	RenderFallbackMapIcon(...);
```

- [ ] **步骤 4：保持 settings 页与 resource 页状态一致**

要求：

- 在 DDNet 原设置页改 `cl_background_entities` 后，资源页能反映当前选中项
- 在资源页选择 `entity_bg` 后，原设置页输入框也能反映新值

- [ ] **步骤 5：构建验证**

运行：

```powershell
qmclient_scripts\cmake-windows.cmd --build build-ninja --target game-client -j 10 | Select-Object -Last 80
```

预期：

- `entity_bg` 桥接逻辑编译通过
- 未破坏现有 `ConchainBackgroundEntities`

- [ ] **步骤 6：提交本任务**

```bash
git add src/game/client/components/menus_settings_assets.cpp src/game/client/components/menus_settings.cpp src/game/client/components/background.cpp src/game/client/components/menus.cpp src/engine/shared/config_variables.h
git commit -m "feat(assets): add new resource categories and bridge entity background selection"
```

---

### 任务 8：做整体验证与运行态检查

**文件：**
- 修改：`docs/superpowers/plans/2026-04-24-assets-resource-page-unification-plan.md`

- [x] **步骤 1：运行 C++ 测试**

运行：

```powershell
qmclient_scripts\cmake-windows.cmd --build build-ninja --target run_cxx_tests -j 10 | Select-Object -Last 120
```

预期：

- 新增 `assets_resource_registry_test` 与 `assets_resource_migration_test` 通过
- 既有测试无新增回归

- [x] **步骤 2：构建客户端**

运行：

```powershell
qmclient_scripts\cmake-windows.cmd --build build-ninja --target game-client -j 10 | Select-Object -Last 80
```

预期：

- `game-client` 构建成功

- [ ] **步骤 3：做最小运行态检查**

从构建目录运行：

```powershell
Set-Location build-ninja
.\DDNet.exe
```

手工检查以下链路：

1. 资源页新增分类可见
2. `Downloaded` 标签高度与删除按钮一致
3. `gui_cursor / arrow / strong_weak` 选择后无需重启即可生效
4. 根目录旧文件存在时能被兼容导入为 `legacy`
5. `entity_bg` 选择后与原 DDNet 设置页保持一致

- [x] **步骤 4：回写计划勾选状态并补一段验证记录**

在本计划文件底部追加一段简短记录：

```md
## 验证记录

- run_cxx_tests：通过
- game-client：通过
- 运行态检查：完成
```

- [x] **步骤 5：提交收尾**

```bash
git add docs/superpowers/plans/2026-04-24-assets-resource-page-unification-plan.md
git commit -m "docs(plan): record verification for assets resource page unification"
```

## 验证记录

- 任务 5（default 播种与 legacy 导入入口）：
  `.\build-ninja\testrunner.exe --gtest_filter=AssetsResourceMigration*`
  通过
- 任务 5（default 播种与 legacy 导入入口）：
  `qmclient_scripts\cmake-windows.cmd --build build-ninja --target game-client -j 10 | Select-Object -Last 80`
  通过
- 任务 6（命名单文件资源运行时优先级）：
  `.\build-ninja\testrunner.exe --gtest_filter=AssetsResourceRegistry*`
  通过
- 任务 6（命名单文件资源运行时优先级）：
  `qmclient_scripts\cmake-windows.cmd --build build-ninja --target run_cxx_tests -j 10 | Select-Object -Last 80`
  558 tests passed
- 任务 6（命名单文件资源运行时优先级）：
  `qmclient_scripts\cmake-windows.cmd --build build-ninja --target game-client -j 10 | Select-Object -Last 80`
  通过
- 任务 7（新资源分类与 entity_bg 桥接）：
  `qmclient_scripts\cmake-windows.cmd --build build-ninja --target game-client -j 10 | Select-Object -Last 100`
  通过
- 任务 7（新资源分类与 entity_bg 桥接）：
  `qmclient_scripts\cmake-windows.cmd --build build-ninja --target run_cxx_tests -j 10 | Select-Object -Last 60`
  558 tests passed
- 任务 8（整体验证汇总）：
  `qmclient_scripts\cmake-windows.cmd --build build-ninja --target run_cxx_tests -j 10 | Select-Object -Last 60`
  558 tests passed
- 任务 8（整体验证汇总）：
  `qmclient_scripts\cmake-windows.cmd --build build-ninja --target game-client -j 10 | Select-Object -Last 100`
  通过
- 任务 8（运行态检查）：
  尚未做 GUI 人工核验，步骤 3 保持待办

---

## 自检结果

### 规格覆盖度

本计划已覆盖规格中的核心要求：

- 本地资源页与 Workshop 远端预览职责分层
- 新增分类 `gui_cursor / arrow / strong_weak / entity_bg`
- `default` 首位与不可删除语义
- `legacy` 迁移命名与旧路径兼容
- 运行时优先级 `assets -> 根目录兼容 -> data`
- `entity_bg` 与 `cl_background_entities` 既有链路桥接
- `Downloaded` 标签与删除按钮统一
- 无远端源分类显示“仅本地资源”

### 占位符扫描

本计划未保留占位式描述；所有任务都给出了目标文件、最小代码骨架和验证命令。

### 类型一致性

计划内统一使用以下命名，不在任务间切换别名：

- `SAssetResourceCategory`
- `EAssetResourceKind`
- `EAssetPreviewKind`
- `FindAssetResourceCategory`
- `AssetResourceNeedsLegacyImport`
- `NextLegacyAssetName`
- `LoadNamedSingleFileImage`
