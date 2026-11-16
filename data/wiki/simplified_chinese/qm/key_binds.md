## 它是什么

把 8 个高频动作做成一键上绑：每行右侧是按键捕获，按下想用的键就直接写入绑定，省掉手敲 bind。目标键原有的绑定会被顶掉。

## 八条分别是什么

| 行 | 命令 | 按住时发生什么 |
|---|---|---|
| HDF | `+toggle cl_dummy_hammer 1 0` | 切分身锤，松手收回 |
| DF | `+fire; +toggle cl_dummy_hammer 1 0` | 开火同时切分身锤 |
| 45°瞄准 | `echo You are using 45-degree aim; +toggle cl_mouse_max_distance 2 400; +toggle_restore inp_mousesens 1` | 提示一句，把瞄准距离压到 2，灵敏度临时设 1 |
| 瞄缝救人 | `+toggle_restore inp_mousesens 1` | 灵敏度临时设 1，松手恢复原值 |
| 左跳 / 右跳 | `+jump; +left` / `+jump; +right` | 同一帧跳加方向 |
| 武器辅助线 | `+showweapontrajectory` | 按住显示弹道 |
| 主动断开 | `qm_timeout_disconnect` | 按下静默断线 |

## 为什么 HDF 用 +toggle 而不是 toggle

`toggle` 按一下开、再按一下才关，中途被打断就会停留在开着的状态。`+toggle` 松手立刻回到关，失误成本低。

官方 deepfly 教程用的是同一条思路：`bind mouse1 "+fire; +toggle cl_dummy_hammer 1 0"`。那里还专门提醒——**关闭的一侧必须把 `cl_dummy_hammer` 归零**，否则假人会卡在挥锤节奏里停不下来。本卡片生成的命令已经避开了这个坑。

## 45° 瞄准要改成你自己的值

官方示例把灵敏度写死成固定值（例如 `+toggle inp_mousesens 1 200`），松手就写回 200——如果你本来不是 200，一次 45° 就把灵敏度改掉了。

这里改用 `+toggle_restore`：松手恢复**按下前**的值，不管你是多少。`cl_mouse_max_distance` 仍然是固定 2/400，因为它决定的是 45° 的几何约束，不是手感。

想改成自己的数，先到控制台输入 `cl_mouse_max_distance` 和 `inp_mousesens` 看当前值，再写进命令串。

## 注意

- 武器辅助线键只在显示模式为「按键显示」时有反应；「始终显示」用不着它，「关闭」时按了也没反应。
- 主动断开同时断开本体和分身，保留 Tee 防超时保护。
- 这 8 条都写单键，不占修饰键槽位；要组合键请到「绑定」卡片。
- 写入后建议去「绑定一览」搜一下，确认没有顶掉你常用的键。

## 延伸阅读

- DDNet Wiki · Binds：`+toggle` 与深飞（deep fly）的原始写法与坑

## 相关

- [绑定](qm:bind_editor)
- [绑定一览](qm:bind_overview)
- [武器辅助线](qm:weapon_trajectory)
