# QmClient HUD 编辑器完善功能报告

## 一、功能概述

HUD 编辑器允许玩家自定义游戏界面元素的位置和大小，通过拖拽方式重新排列 HUD 组件。当前已有基础实现，需要完善以提供更好的用户体验。

---

## 二、当前实现分析

### 2.1 核心文件

| 文件 | 职责 |
|------|------|
| [hud_editor.h](file:///e:/Coding/DDNet/QmClient/src/game/client/components/hud_editor.h) | HUD 编辑器头文件，定义接口和结构体 |
| [hud_editor.cpp](file:///e:/Coding/DDNet/QmClient/src/game/client/components/hud_editor.cpp) | 编辑器主要实现，包含核心逻辑和状态管理 |

### 2.2 现有功能

根据代码分析，当前 HUD 编辑器已实现：

1. **基础编辑模式**：进入/退出编辑模式
2. **元素选择**：点击选择 HUD 元素
3. **拖拽移动**：拖动元素改变位置
4. **尺寸调整**：调整元素大小

### 2.3 布局系统

项目已有 Flexbox 布局引擎，可用于更灵活的布局管理。

---

## 三、当前问题

1. **编辑体验不够直观**：用户可能难以精确定位元素
2. **缺少预设布局**：没有提供常用布局模板
3. **缺少元素对齐辅助**：没有网格吸附或对齐线
4. **缺少重置功能**：无法一键恢复默认布局
5. **缺少导入/导出**：无法分享自定义布局

---

## 四、实现方案

### 4.1 增强编辑体验

#### 4.1.1 网格吸附

```cpp
// 配置变量
MACRO_CONFIG_INT(HudEditorGridSize, hud_editor_grid_size, 10, 5, 50, CFGFLAG_CLIENT, "网格吸附大小（像素）")
MACRO_CONFIG_INT(HudEditorGridSnap, hud_editor_grid_snap, 1, 0, 1, CFGFLAG_CLIENT, "启用网格吸附")
```

#### 4.1.2 对齐辅助线

当元素边缘与其他元素对齐时显示辅助线：
- 水平对齐线（红色虚线）
- 垂直对齐线（绿色虚线）

### 4.2 预设布局系统

提供常用布局预设：

| 预设名称 | 描述 |
|---------|------|
| 默认 | DDNet 标准布局 |
| 竞速 | 精简布局，强调计时器 |
| 竞技 | 强调生命值和武器 |
| 自定义 | 用户保存的布局 |

### 4.3 布局导入/导出

```cpp
// 布局文件格式 (JSON)
{
    "version": 1,
    "name": "My Custom Layout",
    "elements": [
        {
            "id": "health_bar",
            "x": 100,
            "y": 50,
            "width": 200,
            "height": 30
        }
    ]
}
```

### 4.4 配置变量

| 变量 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `hud_editor_grid_size` | INT | 10 | 网格大小（像素） |
| `hud_editor_grid_snap` | INT | 1 | 启用网格吸附 |
| `hud_editor_show_guides` | INT | 1 | 显示对齐辅助线 |
| `hud_editor_layout` | STR | "default" | 当前布局名称 |

---

## 五、实现路径

### 阶段一：基础增强

1. 添加网格吸附功能
2. 添加对齐辅助线
3. 添加重置默认布局按钮

**修改文件**：

| 文件 | 修改内容 |
|------|----------|
| `hud_editor.cpp` | 实现网格吸附和对齐辅助线 |
| `hud_editor.h` | 添加相关数据结构 |
| `config_variables.h` | 添加配置变量 |

### 阶段二：预设布局

1. 设计预设布局数据结构
2. 实现布局切换功能
3. 添加布局选择 UI

### 阶段三：导入/导出

1. 实现布局序列化（JSON）
2. 添加导入/导出 UI
3. 支持布局分享

---

## 六、关键文件索引

| 文件 | 职责 |
|------|------|
| `src/game/client/components/hud_editor.h` | HUD 编辑器头文件 |
| `src/game/client/components/hud_editor.cpp` | HUD 编辑器实现 |
| `src/game/client/components/hud.cpp` | HUD 组件渲染 |
| `src/game/client/components/hud.h` | HUD 组件定义 |
| `src/engine/shared/config_variables.h` | 配置变量定义 |
