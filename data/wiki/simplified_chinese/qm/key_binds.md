## 它是什么

把 8 个常用功能做成一行一个的键位：每行右侧按键捕获，按下想用的键就写入绑定，替你敲 bind；目标键原有的绑定会被顶掉。

## 在对局里

| 行 | 命令 | 按住发生什么 |
|---|---|---|
| HDF | `+toggle cl_dummy_hammer 1 0` | 切分身锤，松手收回 |
| DF | `+fire; +toggle cl_dummy_hammer 1 0` | 开火同时切分身锤 |
| 45°瞄准 | `echo You are using 45-degree aim; +toggle cl_mouse_max_distance 2 400; +toggle_restore inp_mousesens 1` | 按下先在控制台提示一句，再把瞄准距离压到 2、灵敏度临时设 1 |
| 瞄缝救人 | `+toggle_restore inp_mousesens 1` | 灵敏度临时设 1 |
| 左跳 / 右跳 | `+jump; +left` / `+jump; +right` | 同一帧跳加方向 |
| 武器辅助线 | `+showweapontrajectory` | 按住显示弹道 |
| 主动断开 | `qm_timeout_disconnect` | 按下静默断线 |

## 注意

- `+` 开头的命令按住生效、松手复原：`+toggle 变量 值A 值B` 按下写 A、松手写 B，`+toggle_restore` 松手恢复按下前的值。45°瞄准松手把 `cl_mouse_max_distance` 写回 400。
- 武器辅助线键只在显示模式为「按键显示」时有反应；「始终显示」用不着，「关闭」时没反应。
- 主动断开同时断开本体和分身，保留 Tee 防超时保护。

## 相关

- [绑定](qm:bind_editor)
- [武器辅助线](qm:weapon_trajectory)
