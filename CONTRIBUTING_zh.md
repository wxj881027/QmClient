# 为 DDNet 贡献代码

> 📄 本文档另有 <a href="CONTRIBUTING.md">English</a> 版本</p>

## 概述

在开始编写代码之前，请先创建一个 Issue 讨论这个想法。
如果花费时间开发的贡献不符合 DDNet 项目的理念，那将是很遗憾的。

通常会被拒绝的事项列表：

- 为分身（dummy）添加影响游戏玩法的新功能。
  https://github.com/ddnet/ddnet/pull/8275
  https://github.com/ddnet/ddnet/pull/5443#issuecomment-1158437505
- 破坏网络协议或文件格式（如皮肤和录像）的向后兼容性。
- 破坏游戏玩法的向后兼容性：
  - 现有排名不应变得不可能。
  - 现有地图不应被破坏。
  - 新玩法不应使已完成地图的跑图变得更容易。

查看[问题列表](https://github.com/ddnet/ddnet/issues)寻找可以处理的问题。
未标记的问题尚未分类，通常不是好的候选者。
此外，标签 https://github.com/ddnet/ddnet/labels/needs-discussion 表示仍需讨论才能实施的问题，标签 https://github.com/ddnet/ddnet/labels/fix-changes-physics 表示对新手贡献者来说过于复杂的问题。
推荐处理带有标签 https://github.com/ddnet/ddnet/labels/good%20first%20issue、https://github.com/ddnet/ddnet/labels/bug 和 https://github.com/ddnet/ddnet/labels/feature-accepted 的问题。
通过检查分配情况和是否有相关的开放 Pull Request，确保该问题没有被其他人处理。
如果您想处理某个问题，请发表评论以便分配给您，或如果您有任何问题。

添加新功能通常需要至少两位维护者的支持，以避免功能蔓延。

## 编程语言

我们目前使用以下语言开发 DDNet：

- C++
- 极少量的 Rust
- Python（用于代码生成和支持工具）
- CMake（用于构建）

不允许添加其他编程语言的代码。

对于平台支持，我们还在其他平台上使用其他编程语言，如 Android 上的 Java 或 macOS 上的 Objective-C++，但这些仅限于平台特定代码。

## 代码风格

有一些风格规则。其中一些由 CI 强制执行，一些由审查者手动检查。
如果您的 GitHub pipeline 显示一些错误，请查看日志并尝试修复它们。

此类修复提交理想情况下应使用 `git commit --amend` 或 `git rebase -i` 压缩为一个大提交。

我们使用 clang-format 20 来格式化 C++ 代码。

### 变量、方法、类名使用大驼峰命名

`src/base` 文件夹中的文件除外。

单词：

- `int length = 0;` ❌
- `int Length = 0;` ✅

多个单词：

- `int maxLength = 0;` ❌
- `int MaxLength = 0;` ✅

### 变量名应具有描述性

❌ 避免：

```cpp
for(int i = 0; i < MAX_CLIENTS; i++)
{
	for(int k = 0; k < NUM_DUMMIES; k++)
	{
		if(k == 0)
			continue;

		m_aClients[i].Foo();
	}
}
```

✅ 应该：

```cpp
for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
{
	for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
	{
		if(Dummy == 0)
			continue;

		m_aClients[ClientId].Foo();
	}
}
```

更多示例请参见[这里](https://github.com/ddnet/ddnet/pull/8288#issuecomment-2094097306)。

### 我们对匈牙利 notation 的解释

DDNet 从 [Teeworlds](https://www.teeworlds.com/?page=docs&wiki=nomenclature) 继承了带前缀的匈牙利 notation。

只使用下面列出的前缀。DDNet 代码库**不完全**遵循严格的匈牙利 notation。

**不要**使用 `c` 表示常量或 `b` 表示布尔值或 `i` 表示整数。

C 风格函数指针是指针，但 `std::function` 不是。

#### 变量前缀

| 前缀    | 用途                                         | 示例                                                                                                                           |
| ------- | -------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| `m_`  | 类成员                                       | `int m_Mode`, `CLine m_aLines[]`                                                                                           |
| `g_`  | 全局成员                                     | `CConfig g_Config`                                                                                                           |
| `s_`  | 静态变量                                     | `static EHistoryType s_HistoryType`, `static char *ms_apSkinNameVariables[NUM_DUMMIES]`                                    |
| `p`   | 原始指针和智能指针                           | `char *pName`, `void **ppUserData`, `std::unique_ptr<IStorage> pStorage`                                                 |
| `a`   | 固定大小数组和 `std::array`                | `float aWeaponInitialOffset[NUM_WEAPONS]`, `std::array<char, 12> aOriginalData`                                            |
| `v`   | 向量（`std::vector`）                      | `std::vector<CLanguage> m_vLanguages`                                                                                        |
| `pfn` | 函数指针（**不是** `std::function`） | `m_pfnUnknownCommandCallback = pfnCallback`                                                                                  |
| `F`   | 函数类型定义                                 | `typedef void (*FCommandCallback)(IResult *pResult, void *pUserData)`, `typedef std::function<int()> FButtonColorCallback` |

适当组合使用。

#### 类前缀

| 前缀  | 用途                      | 示例                                 |
| ----- | ------------------------- | ------------------------------------ |
| `C` | 类                        | `class CTextCursor`                |
| `I` | 接口                      | `class IFavorites`                 |
| `S` | ~~结构体（使用类代替）~~ | ~~`struct STextContainerUsages`~~ |

### 枚举

非作用域枚举（`enum`）和作用域枚举（`enum class`）应以 `E` 开头，使用 CamelCase。枚举值应使用 SCREAMING_SNAKE_CASE。

所有新代码应使用作用域枚举，枚举值的名称不应包含枚举名称。

❌ 避免：

```cpp
enum STATUS
{
	STATUS_PENDING,
	STATUS_OKAY,
	STATUS_ERROR,
};
```

✅ 应该：

```cpp
enum class EStatus
{
	PENDING,
	OKAY,
	ERROR,
};
```

**也不要**使用 `enum` 或 `enum class` 作为标志。参见下一节。
`enum class` 应仅用于枚举。具有类型值的常量应改用 `constexpr`。

### 位标志

当前代码大量使用 `enum` 来处理标志。新代码应使用 `inline constexpr` 来处理位标志。

❌ 避免：

```cpp
enum
{
	CFGFLAG_SAVE = 1,
	CFGFLAG_CLIENT = 2,
	CFGFLAG_SERVER = 4,
	CFGFLAG_STORE = 8,
	CFGFLAG_MASTER = 16,
	CFGFLAG_ECON = 32,
};
```

✅ 应该：

```cpp
namespace ConfigFlag
{
	inline constexpr uint32_t SAVE = 1 << 0;
	inline constexpr uint32_t CLIENT = 1 << 1;
	inline constexpr uint32_t SERVER = 1 << 2;
	inline constexpr uint32_t STORE = 1 << 3;
	inline constexpr uint32_t MASTER = 1 << 4;
	inline constexpr uint32_t ECON = 1 << 5;
}
```

### 应避免使用 C 预处理器

- 避免使用 `#define` 指令定义常量。请改用 `enum class` 或 `inline constexpr`。只有在没有其他选择时（如条件编译）才使用 `#define` 指令。
- 避免使用类似函数的 `#define` 指令。如果可能，将代码提取到函数或 lambda 表达式中而不是宏。
  只有在没有其他更易读的选项时才使用类似函数的 `#define` 指令。

### 不鼓励使用 `goto`

不要在新代码中使用 `goto` 关键字，C++ 中有更好的控制流结构。

### 应避免在 if 语句中赋值

不要在 if 语句中设置变量。

❌

```cpp
int Foo;
if((Foo = 2)) { .. }
```

✅

```cpp
int Foo = 2;
if(Foo) { .. }
```

除非替代代码更复杂且更难阅读。

### 应避免在布尔上下文中使用整数

❌

```cpp
int Foo = 0;
if(!Foo) { .. }
```

✅

```cpp
int Foo = 0;
if(Foo != 0) { .. }
```

### 应避免使用方法默认参数

默认参数很容易出问题，如果您有多个默认参数，即使只想更改最后一个，也必须指定每一个。

### 应避免方法重载

尝试寻找更具描述性的名称。

### 应避免全局/静态变量

使用成员变量或通过参数传递状态，而不是使用全局或静态变量，因为静态变量在类的所有实例之间共享相同的值。

避免静态变量 ❌：

```cpp
int CMyClass::Foo()
{
	static int s_Count = 0;
	s_Count++;
	return s_Count;
}
```

改用成员变量 ✅：

```cpp
class CMyClass
{
	int m_Count = 0;
};
int CMyClass::Foo()
{
	m_Count++;
	return m_Count;
}
```

常量可以是静态的 ✅：

```cpp
static constexpr int ANSWER = 42;
```

### Getter 不应有 `Get` 前缀

虽然代码库中已有许多以 `Get` 前缀开头的方法。但如果添加新的 getter，它们不应包含前缀。

❌

```cpp
int GetMyVariable() { return m_MyVariable; }
```

✅

```cpp
int MyVariable() { return m_MyVariable; }
```

### 类成员变量应在声明时初始化

避免这样做 ❌：

```cpp
class CFoo
{
	int m_Foo;
};
```

如果可能，改这样做 ✅：

```cpp
class CFoo
{
	int m_Foo = 0;
};
```

### 应优先使用 `class` 而不是 `struct`

虽然代码中仍有许多 `struct` 被使用，但所有新代码应仅使用 `class`。

### 应使用现代 C++ 而不是旧的 C 风格代码

DDNet 在便携性（易于在所有常见发行版上编译）和使用现代特性之间取得平衡。
因此，我们鼓励使用所有现代 C++ 特性，只要它们在我们使用的 C++ 版本中得到支持。
仍然要注意，在游戏循环代码中应避免内存分配，因此栈上的静态缓冲区可能更可取。

示例：

- 使用 `nullptr` 而不是 `0` 或 `NULL`。
- 尽可能使用 `std::fill` 初始化数组，而不是使用 `mem_zero` 或循环。

### 成功使用 `true`

不要使用 `int` 作为可以成功或失败的方法的返回类型。
改用 `bool`。`true` 表示成功，`false` 表示失败。

参见 https://github.com/ddnet/ddnet/issues/6436

### 文件名

文件和文件夹名称应全部小写，单词用下划线分隔。

❌

```cpp
src/game/FooBar.cpp
```

✅

```cpp
src/game/foo_bar.cpp
```

## 代码文档

`base` 文件夹中的所有公共函数、类等声明都需要代码文档。
对于其他代码，建议为打算重用的函数、类等添加文档，或在能提高清晰度时添加文档。

我们使用 [doxygen](https://www.doxygen.nl/) 生成代码文档。
文档会定期更新，可在 https://codedoc.ddnet.org/ 获取。

我们使用 [Javadoc 风格的块注释](https://www.doxygen.nl/manual/docblocks.html)，并使用 `@` 而不是 `\` 作为 [doxygen 命令](https://www.doxygen.nl/manual/commands.html)的前缀。

## 提交信息

描述您的贡献为玩家/用户带来的变化，而不是从技术角度谈论您做了什么。您的 PR 信息最好采用可以直接用于[更新日志](https://ddnet.org/downloads/)的格式。
