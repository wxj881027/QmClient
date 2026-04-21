# QmClient Demo 播放器增强功能报告

## 一、功能概述

Demo 播放器用于回放游戏录像，支持分析游戏过程、制作视频内容。增强功能包括书签管理、自动高光标记、Ghost 对比 UI 等。

---

## 二、当前实现分析

### 2.1 核心文件

| 文件 | 职责 |
|------|------|
| [race_demo.h](file:///e:/Coding/DDNet/QmClient/src/game/client/components/race_demo.h) | Race Demo 核心数据结构和接口 |
| [race_demo.cpp](file:///e:/Coding/DDNet/QmClient/src/game/client/components/race_demo.cpp) | Demo 录制、播放和管理逻辑 |
| [ghost.h](file:///e:/Coding/DDNet/QmClient/src/game/client/components/ghost.h) | Ghost 核心定义和结构 |
| [ghost.cpp](file:///e:/Coding/DDNet/QmClient/src/game/client/components/ghost.cpp) | Ghost 录制和播放实现 |
| [demo.h](file:///e:/Coding/DDNet/QmClient/src/engine/shared/demo.h) | Demo 系统核心头文件 |
| [demo.cpp](file:///e:/Coding/DDNet/QmClient/src/engine/shared/demo.cpp) | Demo 读写底层功能 |

### 2.2 现有功能

1. **Demo 录制**：`CDemoRecorder` 类实现录制功能
2. **Demo 播放**：`CDemoPlayer` 类实现播放功能
3. **Ghost 系统**：`CGhost` 类实现 Ghost 录制和回放
4. **Race Demo**：竞速专用 Demo 系统

### 2.3 Demo 文件结构

```cpp
// Demo 头文件结构
typedef struct {
    unsigned char m_aMarker[7];  // "DDNETDEMO"
    unsigned char m_Version;
    unsigned char m_aNetversion[4];
    unsigned char m_aMapName[128];
    unsigned char m_aMapSize[4];
    unsigned char m_aMapCrc[4];
    unsigned char m_aType[8];
    unsigned char m_Length[4];
} CDemoHeader;
```

---

## 三、当前问题

1. **缺少书签管理**：无法标记重要时间点
2. **缺少自动高光标记**：完成地图、连杀等事件不会自动标记
3. **Ghost 对比功能有限**：无法同时回放多个 Ghost
4. **播放控制不够精细**：缺少逐帧播放、变速播放等功能
5. **缺少视频导出集成**：无法直接从 Demo 导出视频片段

---

## 四、实现方案

### 4.1 书签管理系统

#### 4.1.1 书签数据结构

```cpp
struct SDemoBookmark
{
    int m_Tick;           // 时间点（tick）
    char m_aName[64];     // 书签名称
    char m_aDescription[256]; // 描述
    int m_Type;           // 类型：手动/自动
};
```

#### 4.1.2 书签存储

书签保存在 Demo 文件同名的 `.bookmarks.json` 文件中：

```json
{
    "demo_file": "race_20240419.demo",
    "bookmarks": [
        {"tick": 1500, "name": "Start", "type": "manual"},
        {"tick": 4500, "name": "Finish", "type": "auto_finish"}
    ]
}
```

### 4.2 自动高光标记

自动检测并标记以下事件：

| 事件类型 | 触发条件 | 标记名称 |
|---------|---------|---------|
| 地图完成 | 玩家到达终点 | "🏁 Finish" |
| 连杀 | 连续击杀 3+ 人 | "🔥 Triple Kill" |
| 旗帜捕获 | CTF 模式夺旗 | "🚩 Flag Capture" |
| 个人最佳 | 打破记录 | "⭐ New Record" |

### 4.3 Ghost 对比 UI

#### 4.3.1 多 Ghost 回放

```cpp
// 配置变量
MACRO_CONFIG_INT(GhostCompareCount, ghost_compare_count, 2, 1, 4, CFGFLAG_CLIENT, "同时回放的 Ghost 数量")
MACRO_CONFIG_INT(GhostCompareSync, ghost_compare_sync, 1, 0, 1, CFGFLAG_CLIENT, "同步回放多个 Ghost")
```

#### 4.3.2 对比显示

- 不同 Ghost 使用不同颜色
- 显示时间差（+0.5s / -0.3s）
- 关键点对比（检查点时间）

### 4.4 播放控制增强

| 功能 | 快捷键 | 说明 |
|------|--------|------|
| 逐帧前进 | `.` | 前进 1 帧 |
| 逐帧后退 | `,` | 后退 1 帧 |
| 0.5x 速度 | `[` | 慢速播放 |
| 2x 速度 | `]` | 快速播放 |
| 跳转书签 | `0-9` | 跳转到对应书签 |

### 4.5 配置变量

| 变量 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `demo_auto_bookmark` | INT | 1 | 自动标记高光事件 |
| `demo_ghost_compare` | INT | 1 | 启用 Ghost 对比模式 |
| `demo_show_timeline` | INT | 1 | 显示时间轴 |

---

## 五、实现路径

### 阶段一：书签管理

1. 设计书签数据结构
2. 实现书签存储/加载
3. 添加书签 UI（时间轴标记）

**修改文件**：

| 文件 | 修改内容 |
|------|----------|
| `race_demo.h` | 添加书签数据结构 |
| `race_demo.cpp` | 实现书签管理逻辑 |
| `menus_demo.cpp` | 添加书签 UI |

### 阶段二：自动高光

1. 检测游戏事件
2. 自动创建书签
3. 高亮显示高光时刻

### 阶段三：Ghost 对比

1. 多 Ghost 加载
2. 同步播放控制
3. 时间差显示

---

## 六、关键文件索引

| 文件 | 职责 |
|------|------|
| `src/game/client/components/race_demo.h` | Race Demo 定义 |
| `src/game/client/components/race_demo.cpp` | Race Demo 实现 |
| `src/game/client/components/ghost.h` | Ghost 定义 |
| `src/game/client/components/ghost.cpp` | Ghost 实现 |
| `src/engine/shared/demo.h` | Demo 核心头文件 |
| `src/engine/shared/demo.cpp` | Demo 核心实现 |
| `src/game/client/components/menus_demo.cpp` | Demo 菜单 UI |
