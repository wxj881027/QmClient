## 它是什么

把计分板、观战、控制台、截图、表情、练习开关和主动断开这些杂项动作挂到键上，属于输入层，卡片本身只维护 Bind 表。

## 机制

每个动作对应一条命令：+scoreboard、+statboard、+spectate、+emote、+pie_menu、toggle_local_console、screenshot、fast_practice_toggle、say /lock、toggle cl_overlay_entities 0 100、toggle cl_showhud 0 1、qm_timeout_disconnect。两类命令混在一起：加号开头的是按住生效、松开结束；toggle 和 say 在按下沿触发一次，按一下切一次或发一条聊天。「主动断开」执行 qm_timeout_disconnect，会立刻断开主连接和分身连接，断线原因记为异常断开，客户端本身不退出。所有改动只写本地 Bind 表，保存时写成 bind 行，下次启动继续生效。

## 调到合适

| 动作 | 类型 | 放在哪 |
|---|---|---|
| 计分板、统计板 | 按住 | 顺手位置，随时松 |
| 观战模式 | 按住 | 和切换分身分开放 |
| 控制台 | 按一下开或关 | 远离移动键 |
| 主动断开 | 按一下 | 靠里，避免误触 |

## 注意

- 主动断开没有二次确认，按一下就掉线，别把它放在移动键旁边。
- 「锁定队伍」是 say /lock，会真的往服务器发一条聊天命令，不是本地开关。
- 一个键只能挂一条命令，覆盖后原动作立刻失效，改完回头看别的卡片。

## 相关

- [自定义按键](deck:controls-custom)
- [绑定一览](qm:bind_overview)
