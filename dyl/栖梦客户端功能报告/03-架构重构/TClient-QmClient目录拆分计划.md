# TClient / QmClient 目录拆分计划

> **状态**: 待用户确认文件分类  
> **目标**: 将 `src/game/client/components/qmclient/` 中混合的 TClient 上游代码和 QmClient 本客户端代码分离  
> **原则**: TClient 上游代码保持不动（便于后续合并上游更新），QmClient 新增代码独立存放

---

## 一、参考：BestClient (BC端) 的目录结构

BC端已经完美实现了 TClient 和 BestClient 的分离：

```
src/game/client/components/
├── tclient/                    # TClient 上游功能
│   ├── tclient.cpp/h           # TClient 核心组件
│   ├── menus_tclient.cpp       # TClient 设置菜单
│   ├── bg_draw.cpp/h
│   ├── bindchat.cpp/h
│   ├── bindwheel.cpp/h
│   ├── custom_communities.cpp/h
│   ├── mod.cpp/h
│   ├── moving_tiles.cpp/h
│   ├── mumble.cpp/h            # BC端特有，QmClient没有
│   ├── outlines.cpp/h
│   ├── pet.cpp/h
│   ├── player_indicator.cpp/h
│   ├── rainbow.cpp/h
│   ├── scripting.cpp/h
│   ├── scripting/impl.cpp/h
│   ├── skinprofiles.cpp/h
│   ├── statusbar.cpp/h
│   ├── trails.cpp/h
│   └── warlist.cpp/h
│
└── bestclient/                 # BestClient 本客户端功能
    ├── bestclient.cpp/h        # BestClient 核心组件
    ├── menus_bestclient.cpp
    ├── menus_bestclient_shop.cpp
    ├── translate.cpp/h         # BC端翻译功能
    ├── voice/                  # BC端语音（与QmClient不同实现）
    │   ├── voice.cpp/h
    │   ├── server.cpp
    │   └── protocol.h
    ├── clientindicator/        # BC端客户端指示器
    ├── visualizer/             # BC端音频可视化
    ├── 3d_particles.cpp/h
    ├── admin_panel.cpp/h
    ├── afterimage.cpp/h
    ├── chat_bubbles.cpp/h
    ├── fast_actions.cpp/h
    ├── fast_practice.cpp/h
    ├── hud_editor.cpp/h
    ├── magic_particles.cpp/h
    ├── music_player.cpp/h
    ├── orbit_aura.cpp/h
    ├── r_jelly.cpp/h           # BC端果冻效果（重命名）
    ├── r_trail.cpp/h           # BC端拖尾效果（重命名）
    └── ...
```

BC端 `gameclient.h` 的 include 方式：
```cpp
#include "components/tclient/bg_draw.h"
#include "components/tclient/bindchat.h"
#include "components/tclient/tclient.h"
#include "components/bestclient/bestclient.h"
#include "components/bestclient/voice/voice.h"
```

---

## 二、QmClient 目标目录结构（参考BC端）

```
src/game/client/components/
├── tclient/                    # TClient 上游功能（保持与上游兼容）
│   ├── tclient.cpp/h           # TClient 核心组件（原qmclient.cpp/h中的TClient部分）
│   ├── menus_tclient.cpp       # TClient 设置菜单（从menus_qmclient.cpp拆分）
│   ├── bg_draw.cpp/h
│   ├── bg_draw_file.cpp/h
│   ├── bindchat.cpp/h
│   ├── bindwheel.cpp/h
│   ├── colored_parts.h
│   ├── custom_communities.cpp/h
│   ├── data_version.h
│   ├── mod.cpp/h
│   ├── moving_tiles.cpp/h
│   ├── outlines.cpp/h
│   ├── pet.cpp/h
│   ├── player_indicator.cpp/h
│   ├── rainbow.cpp/h
│   ├── scripting.cpp/h
│   ├── scripting/impl.cpp/h
│   ├── skinprofiles.cpp/h
│   ├── statusbar.cpp/h
│   ├── trails.cpp/h
│   └── warlist.cpp/h
│
└── qmclient/                   # QmClient 本客户端功能
    ├── qmclient.cpp/h          # QmClient 核心组件（原qmclient.cpp/h中的QmClient部分）
    ├── menus_qmclient.cpp      # QmClient 设置菜单
    ├── translate.cpp/h         # QmClient翻译功能（扩展了TClient的翻译）
    ├── voice_callback.h
    ├── voice_component.cpp/h
    ├── voice_core.cpp/h
    ├── voice_rust.cpp/h
    ├── voice_utils.cpp/h
    ├── collision_hitbox.cpp/h  # QmClient碰撞框（扩展版）
    ├── input_overlay.cpp/h     # QmClient输入叠加
    ├── jelly_tee.cpp/h         # QmClient果冻Tee（注意：BC端叫r_jelly）
    ├── lyrics_component.cpp/h  # QmClient歌词组件
    └── ...                     # 其他QmClient新增功能
```

---

## 三、文件分类表（参考BC端，待用户确认）

### 3.1 TClient 上游功能（移动到 `tclient/`）

| 文件名 | QmClient当前路径 | BC端路径 | 判断依据 |
|--------|-----------------|---------|---------|
| `bg_draw.cpp/h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `bg_draw_file.cpp/h` | `qmclient/` | - | QmClient特有，但功能类似bg_draw |
| `bindchat.cpp/h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `bindwheel.cpp/h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `colored_parts.h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `custom_communities.cpp/h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `data_version.h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `mod.cpp/h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `moving_tiles.cpp/h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `outlines.cpp/h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `pet.cpp/h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `player_indicator.cpp/h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `rainbow.cpp/h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `scripting.cpp/h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `scripting/impl.cpp/h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `skinprofiles.cpp/h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `statusbar.cpp/h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `trails.cpp/h` | `qmclient/` | `tclient/` | BC端在tclient目录 |
| `warlist.cpp/h` | `qmclient/` | `tclient/` | BC端在tclient目录 |

### 3.2 QmClient 本客户端功能（保留在 `qmclient/`）

| 文件名 | QmClient当前路径 | BC端对应 | 判断依据 |
|--------|-----------------|---------|---------|
| `translate.cpp/h` | `qmclient/` | `bestclient/translate.cpp/h` | BC端在bestclient目录，QmClient扩展了TClient翻译 |
| `voice_*.cpp/h` | `qmclient/` | `bestclient/voice/` | BC端在bestclient目录，QmClient有独立语音实现 |
| `collision_hitbox.cpp/h` | `qmclient/` | - | QmClient特有 |
| `fast_practice.cpp/h` | `qmclient/` | `bestclient/fast_practice.cpp/h` | BC端在bestclient目录 |
| `input_overlay.cpp/h` | `qmclient/` | - | QmClient特有 |
| `jelly_tee.cpp/h` | `qmclient/` | `bestclient/r_jelly.cpp/h` | BC端在bestclient目录（重命名为r_jelly） |
| `lyrics_component.cpp/h` | `qmclient/` | - | QmClient特有 |

### 3.3 混合功能（需要拆分）

| 文件名 | 当前路径 | 建议处理 |
|--------|---------|---------|
| `qmclient.cpp/h` | `qmclient/` | 拆分为 `tclient/tclient.cpp/h`（TClient更新检查）和 `qmclient/qmclient.cpp/h`（QmClient用户识别） |
| `menus_qmclient.cpp` | `qmclient/` | 拆分为 `tclient/menus_tclient.cpp`（TClient设置标签页）和 `qmclient/menus_qmclient.cpp`（QmClient设置） |

---

## 四、与BC端的差异说明

| 功能 | QmClient | BC端 | 说明 |
|------|---------|------|------|
| 语音 | `qmclient/voice_*.cpp` | `bestclient/voice/` | 不同实现 |
| 翻译 | `qmclient/translate.cpp` | `bestclient/translate.cpp` | 都在本客户端目录 |
| 果冻Tee | `qmclient/jelly_tee.cpp` | `bestclient/r_jelly.cpp` | BC端重命名为r_jelly |
| 拖尾 | `qmclient/trails.cpp` | `tclient/trails.cpp` | QmClient可能扩展了TClient的trails |
| 快速练习 | `qmclient/fast_practice.cpp` | `bestclient/fast_practice.cpp` | BC端移到本客户端目录 |
| Mumble语音 | 无 | `tclient/mumble.cpp` | QmClient没有Mumble |

---

## 五、需要修改的引用文件

执行拆分时，以下文件需要更新 `#include` 路径：

| 文件 | 当前引用 | 修改后 |
|------|---------|--------|
| `src/game/client/gameclient.h` | `#include "components/qmclient/xxx.h"` | 根据归属改为 `components/tclient/xxx.h` 或 `components/qmclient/xxx.h` |
| `src/game/client/gameclient.cpp` | `#include "components/qmclient/xxx.h"` | 同上 |
| `src/game/client/components/menus.h` | `#include <game/client/components/qmclient/warlist.h>` | 改为 `components/tclient/warlist.h` |
| `src/game/client/components/chat.cpp` | `#include <game/client/components/qmclient/colored_parts.h>` | 改为 `components/tclient/colored_parts.h` |
| `src/game/client/components/emoticon.h` | `#include <game/client/components/qmclient/bindwheel.h>` | 改为 `components/tclient/bindwheel.h` |
| `src/game/client/components/players.cpp` | `#include <game/client/components/qmclient/jelly_tee.h>` 等 | 改为 `components/qmclient/jelly_tee.h` |
| `src/game/client/components/console.cpp` | `#include <game/client/components/qmclient/colored_parts.h>` | 改为 `components/tclient/colored_parts.h` |
| `src/test/voice_core_test.cpp` | `#include <game/client/components/qmclient/voice_utils.h>` | 保持 `components/qmclient/voice_utils.h` |
| `CMakeLists.txt` | `components/qmclient/*.cpp` | 根据新目录结构更新 |

---

## 六、执行步骤（确认分类后执行）

### 阶段1：创建目录结构
```bash
# 创建 tclient 目录
mkdir -p src/game/client/components/tclient/scripting

# 移动纯TClient文件到 tclient/
# （具体文件列表根据用户确认的分类表）
```

### 阶段2：更新 CMakeLists.txt
- 修改 `components/qmclient/xxx.cpp` 为 `components/tclient/xxx.cpp` 或 `components/qmclient/xxx.cpp`

### 阶段3：更新头文件引用
- 修改所有 `#include <game/client/components/qmclient/xxx.h>` 为正确的路径

### 阶段4：编译验证
```bash
qmclient_scripts/cmake-windows.cmd --build build-ninja --target game-client -j 10
```

---

## 七、用户确认区

请确认以下分类（参考BC端结构）：

### 确认1：TClient 目录文件列表
以下文件是否应移动到 `tclient/`？

- [ ] `bg_draw.cpp/h`
- [ ] `bg_draw_file.cpp/h`
- [ ] `bindchat.cpp/h`
- [ ] `bindwheel.cpp/h`
- [ ] `colored_parts.h`
- [ ] `custom_communities.cpp/h`
- [ ] `data_version.h`
- [ ] `mod.cpp/h`
- [ ] `moving_tiles.cpp/h`
- [ ] `outlines.cpp/h`
- [ ] `pet.cpp/h`
- [ ] `player_indicator.cpp/h`
- [ ] `rainbow.cpp/h`
- [ ] `scripting.cpp/h`
- [ ] `scripting/impl.cpp/h`
- [ ] `skinprofiles.cpp/h`
- [ ] `statusbar.cpp/h`
- [ ] `trails.cpp/h`
- [ ] `warlist.cpp/h`

### 确认2：QmClient 目录文件列表
以下文件是否保留在 `qmclient/`？

- [ ] `translate.cpp/h`
- [ ] `voice_*.cpp/h`
- [ ] `collision_hitbox.cpp/h`
- [ ] `fast_practice.cpp/h`
- [ ] `input_overlay.cpp/h`
- [ ] `jelly_tee.cpp/h`
- [ ] `lyrics_component.cpp/h`

### 确认3：混合文件处理
- `qmclient.cpp/h` 是否拆分为 `tclient/tclient.cpp/h` + `qmclient/qmclient.cpp/h`？
- `menus_qmclient.cpp` 是否拆分为 `tclient/menus_tclient.cpp` + `qmclient/menus_qmclient.cpp`？

---

## 八、注意事项

1. **TClient 上游代码保持不动**：便于后续合并 TClient 上游更新
2. **QmClient 新增代码独立存放**：清晰标识本客户端功能
3. **参考BC端结构**：BC端已经实现了完美的分离，QmClient可以参考
4. **编译验证**：每步修改后都要编译验证，确保不破坏构建
5. **Git 提交**：建议分阶段提交，便于回滚
