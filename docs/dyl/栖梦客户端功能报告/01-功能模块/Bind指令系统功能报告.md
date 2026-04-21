# QmClient Bind 指令系统功能报告

## 一、系统架构总览

QmClient 的 Bind 系统由三个核心组件构成：

| 组件 | 类名 | 文件路径 | 职责 |
|------|------|----------|------|
| 键盘绑定 | `CBinds` | binds.h / binds.cpp | 物理按键到控制台命令的映射 |
| 聊天绑定 | `CBindChat` | bindchat.h / bindchat.cpp | 聊天输入框中的快捷指令（如 `!shrug`） |
| 绑定轮盘 | `CBindWheel` | bindwheel.h / bindwheel.cpp | 鼠标轮盘式快捷指令选择器 |

---

## 二、CBinds — 键盘绑定系统（上游 DDNet 原生）

### 核心数据结构

```cpp
char *m_aapKeyBindings[KeyModifier::COMBINATION_COUNT][KEY_LAST];
std::vector<CBindSlot> m_vActiveBinds;
```

使用二维数组存储按键绑定，第一维是修饰键组合（最多 32 种：Ctrl/Alt/Shift/GUI 的排列），第二维是键码。

### 控制台命令

| 命令 | 用法 | 说明 |
|------|------|------|
| `bind` | `bind <key> [command]` | 绑定按键到命令 |
| `binds` | `binds [key]` | 查看绑定 |
| `unbind` | `unbind <key>` | 解绑按键 |
| `unbindall` | `unbindall` | 解绑所有按键 |

### 修饰键解析

支持 `shift+ctrl+alt+gui+key` 格式，例如 `ctrl+shift+a`。

---

## 三、CBindChat — 聊天绑定系统（QmClient 特有）

### 核心数据结构

```cpp
class CBind {
    char m_aName[BINDCHAT_MAX_NAME];   // 触发名称，如 "!shrug"
    char m_aCommand[BINDCHAT_MAX_CMD];  // 执行的命令
};

std::vector<CBind> m_vBinds; // TODO use map（代码中有 TODO 注释）
```

常量：`BINDCHAT_MAX_NAME = 64`，`BINDCHAT_MAX_CMD = 1024`，`BINDCHAT_MAX_BINDS = 256`

### 控制台命令

| 命令 | 用法 | 说明 |
|------|------|------|
| `bindchat` | `bindchat <name> <command>` | 添加聊天绑定 |
| `bindchats` | `bindchats [name]` | 列出聊天绑定 |
| `unbindchat` | `unbindchat <name>` | 移除聊天绑定 |
| `unbindchatall` | `unbindchatall` | 移除所有聊天绑定 |
| `bindchatdefaults` | `bindchatdefaults` | 恢复默认聊天绑定 |

### 默认绑定（BIND_DEFAULTS）

分为三组：

1. **Kaomoji（颜文字）**：`!shrug`、`!flip`、`!unflip`、`!cute`、`!lenny` — 通过 ChaiScript 脚本 `sayemoticon.chai` 实现
2. **Warlist（黑名单）**：`!war`、`!warclan`、`!team`、`!teamclan` 等 — 操作黑名单系统
3. **Other（其他）**：`!translate`、`!translateid`、`!mute`、`!unmute`

### 触发机制 — ChatDoBinds

```cpp
bool CBindChat::ChatDoBinds(const char *pText)
{
    if(pText[0] == ' ' || pText[0] == '\0' || pText[1] == '\0')
        return false;

    const char *pSpace = str_find(pText, " ");
    size_t SpaceIndex = pSpace ? pSpace - pText : strlen(pText);
    for(const CBind &Bind : m_vBinds)
    {
        if(str_startswith_nocase(pText, Bind.m_aName) &&
            str_comp_nocase_num(pText, Bind.m_aName, SpaceIndex) == 0)
        {
            ExecuteBind(Bind, pSpace ? pSpace + 1 : nullptr);
            return true;
        }
    }
    return false;
}
```

触发点在 `chat.cpp:588`：当用户在聊天框按回车发送时，**优先检查**是否匹配 bindchat，如果匹配则执行命令而非发送聊天消息。

### 自动补全 — ChatDoAutocomplete

`ChatDoAutocomplete` 方法已实现但**未被调用**。聊天框的 Tab 补全逻辑（chat.cpp）只处理了服务器命令和玩家名称，未集成 bindchat 补全。

### 配置持久化

聊天绑定保存到独立配置文件 `qmclient_chatbinds.cfg`（ConfigDomain::TCLIENTCHATBINDS），会跳过与默认绑定内容完全相同的条目，对于被用户删除的默认绑定则写入 `unbindchat` 命令。

---

## 四、CBindWheel — 绑定轮盘系统（QmClient 特有）

### 核心数据结构

```cpp
class CBind {
    char m_aName[BINDWHEEL_MAX_NAME] = "EMPTY";
    char m_aCommand[BINDWHEEL_MAX_CMD] = "";
};
```

常量：`BINDWHEEL_MAX_NAME = 64`，`BINDWHEEL_MAX_CMD = 1024`，`BINDWHEEL_MAX_BINDS = 64`

### 控制台命令

| 命令 | 用法 | 说明 |
|------|------|------|
| `+bindwheel` | `+bindwheel` | 打开/关闭轮盘（按住式） |
| `+bindwheel_execute_hover` | | 执行悬停的绑定 |
| `add_bindwheel` | `add_bindwheel <name> <command>` | 添加绑定 |
| `remove_bindwheel` | `remove_bindwheel <name> <command>` | 移除绑定 |
| `delete_all_bindwheel_binds` | | 删除所有绑定 |

### 渲染与交互

圆形轮盘 UI：
- 屏幕中心显示圆形菜单
- 鼠标位置决定选中项（角度+距离判断）
- 松开按键时执行选中的绑定
- 支持动画效果（QuadEaseInOut 缓动）
- 复用了 `m_Emoticon.m_SelectorMouse` 来追踪鼠标位置

默认绑定键为 `Q`。

---

## 五、UI 管理界面 — "指令工坊"

### 1. Chat Binds Tab（聊天绑定管理）

位于 `menus_qmclient.cpp` 的 `RenderSettingsTClientChatBinds`：

- 显示 `BIND_DEFAULTS` 中定义的所有默认绑定组（Kaomoji、Warlist、Other）
- 每个绑定有一个编辑框，可以修改触发名称
- **局限**：只能编辑默认绑定的名称，无法添加自定义绑定、无法修改命令内容、无法删除单个绑定

### 2. Bind Wheel Tab（绑定轮盘管理）

位于 `menus_qmclient.cpp` 的 `RenderSettingsTClientBindWheel`：

- 左侧：名称输入框 + 命令输入框 + 添加/覆盖/删除按钮
- 右侧：圆形轮盘预览，支持鼠标交互
- 左键选择绑定、右键交换位置、中键选择不复制
- 底部：绑定轮盘快捷键设置 + 鼠标重置选项

---

## 六、配置变量

| 变量名 | 脚本名 | 默认值 | 说明 |
|--------|--------|--------|------|
| `TcResetBindWheelMouse` | `tc_reset_bindwheel_mouse` | 0 | 打开绑定轮时重置鼠标位置 |
| `TcAnimateWheelTime` | `tc_animate_wheel_time` | 350 | 表情轮和绑定轮动画时长（毫秒） |

---

## 七、指令识别精准度分析

### 当前解析逻辑

`ChatDoBinds` 的匹配逻辑：
1. 找到输入文本中第一个空格的位置 `SpaceIndex`
2. 遍历所有绑定，检查输入文本是否以绑定名称开头（`str_startswith_nocase`）
3. 同时检查输入文本的前 `SpaceIndex` 个字符是否与绑定名称完全匹配（`str_comp_nocase_num`）

### 潜在的歧义问题

1. **`!` 前缀约定**：所有默认绑定都以 `!` 开头，但代码中**并未强制要求**此前缀。用户可以创建任意名称的绑定，例如 `hello`，这会导致每次在聊天框输入 `hello` 都会触发绑定而非发送消息。

2. **与普通聊天的冲突**：`ChatDoBinds` 在 `SendChatQueued` 之前执行，所以任何匹配到绑定名称的聊天消息都不会被发送。

3. **线性搜索**：`m_vBinds` 使用 `std::vector` 线性遍历，代码中有 `// TODO use map` 注释，表明开发者已意识到性能问题。

---

## 八、改进方向

### 1. 指令识别更精准
- **强制前缀约定**：建议在 `ChatDoBinds` 中只匹配以特定前缀（如 `!`）开头的输入，避免与普通聊天冲突
- **使用有序容器**：将 `m_vBinds` 从 `std::vector` 改为 `std::map` 或有序结构，提升查找效率并消除 TODO
- **最长匹配优先**：当多个绑定名称是同一输入的前缀时，应优先匹配最长的那个（当前实现通过 `SpaceIndex` 已经做到了这一点）

### 2. Chat Binds UI 增强
- 当前 UI 只能编辑默认绑定的名称，**无法添加自定义绑定、修改命令内容、删除绑定**
- 缺少自定义绑定（非默认绑定）的显示和管理
- 建议增加：添加新绑定按钮、命令编辑框、删除按钮、搜索/过滤功能

### 3. Tab 自动补全集成
- `CBindChat::ChatDoAutocomplete` 方法已实现但**未被调用**
- 聊天框的 Tab 补全逻辑（chat.cpp）只处理了服务器命令和玩家名称，未集成 bindchat 补全
- 建议在 chat.cpp 的 Tab 处理逻辑中加入 bindchat 自动补全支持

### 4. 绑定轮盘改进
- 轮盘与 Emoticon 组件共享 `m_SelectorMouse` 状态，耦合度较高
- 最大绑定数限制为 64，对于重度用户可能不够
- 缺少绑定分组/分类功能

### 5. 配置管理
- Chat Binds 和 Bind Wheel 使用不同的配置文件和 ConfigDomain，管理分散
- 缺少导入/导出绑定配置的功能
