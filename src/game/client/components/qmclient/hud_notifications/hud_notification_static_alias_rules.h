// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_HUD_NOTIFICATIONS_HUD_NOTIFICATION_STATIC_ALIAS_RULES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_HUD_NOTIFICATIONS_HUD_NOTIFICATION_STATIC_ALIAS_RULES_H

#define QM_HUD_NOTIFICATION_STATIC_ALIAS_RULES(X) \
	X("你现在会收到私聊消息", WhispersOn) \
	X("你将不再收到私聊消息", WhispersOff) \
	X("你现在可以看到本服所有 tee，不受距离限制", ShowAllOn) \
	X("你将不再看到本服所有 tee", ShowAllOff) \
	X("本服务器未开启救援功能，而你所在的队伍也没有开启 /practice。注意：练习模式下无法获得排名。", RescueDisabled) \
	X("Unknown emote... Say /emote", UnknownEmote) \
	X("未知表情。输入 /emote 查看帮助", UnknownEmote) \
	X("你的超时保护码已设置。0.7 客户端在超时后无法重新认领自己的 tee；不过 0.6 客户端可以认领你的 tee ", TimeoutCodeSet) \
	X("队伍存档已在进行中", TeamSaveInProgress) \
	X("队伍存档尚未完成", TeamSaveInProgress)

#endif
