// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_HUD_NOTIFICATIONS_HUD_NOTIFICATION_STATIC_RULES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_HUD_NOTIFICATIONS_HUD_NOTIFICATION_STATIC_RULES_H

// Compatibility layer for pre-semantic static categories that are still consumed by
// hud_notification_rules.cpp. The semantic upstream/alias tables are the canonical
// source for the migrated static families; do not reintroduce a mixed total-table macro here.

#define QM_HUD_NOTIFICATION_STATIC_TEAM_RULES(X) \
	X("Team save disabled for teams in practice mode", "Team save disabled for teams in practice mode") \
	X("练习模式下不能保存队伍存档", "Team save disabled for teams in practice mode") \
	X("Team load already in progress", "Team load already in progress") \
	X("队伍读档尚未完成", "Team load already in progress") \
	X("You have to be in a team (from 1-63)", "You have to be in a team (from 1-63)") \
	X("You have to be in a team (from 1-127)", "You have to be in a team (from 1-63)") \
	X("必须处于 1–127 号队伍中", "You have to be in a team (from 1-63)") \
	X("Team can't be loaded while racing", "Team can't be loaded while racing") \
	X("比赛中不能读档", "Team can't be loaded while racing") \
	X("Team can't be loaded while in team 0 mode", "Team can't be loaded while in team 0 mode") \
	X("处于 0 队模式时不能读档", "Team can't be loaded while in team 0 mode") \
	X("Team can't be loaded while practice is enabled", "Team can't be loaded while practice is enabled") \
	X("开启练习模式时不能读档", "Team can't be loaded while practice is enabled") \
	X("Could not find your Team", "Could not find your Team") \
	X("找不到你的队伍", "Could not find your Team") \
	X("To save all players in your team have to be alive and not in '/spec'", "To save all players in your team have to be alive and not in '/spec'") \
	X("要保存队伍，队内所有玩家都必须存活且不能处于 '/spec'", "To save all players in your team have to be alive and not in '/spec'") \
	X("Your team has not started yet", "Your team has not started yet") \
	X("你的队伍还没有开始", "Your team has not started yet") \
	X("Team can't be saved while in team 0 mode", "Team can't be saved while in team 0 mode") \
	X("处于 0 队模式时不能保存队伍存档", "Team can't be saved while in team 0 mode") \
	X("Team can't be saved while a dragger is active", "Team can't be saved while a dragger is active") \
	X("有拖拽器生效时不能保存队伍存档", "Team can't be saved while a dragger is active") \
	X("Your team was killed because it couldn't finish anymore and hasn't entered /practice mode", "Your team was killed because it couldn't finish anymore and hasn't entered /practice mode") \
	X("你的队伍因已无法完赛且未进入 /practice 模式而被处死", "Your team was killed because it couldn't finish anymore and hasn't entered /practice mode") \
	X("This team started already", "This team started already") \
	X("这个队伍已经开始比赛了", "This team started already") \
	X("You are in this team already", "You are in this team already") \
	X("你已经在这个队伍里了", "You are in this team already") \
	X("You can't change teams while you are dead/a spectator.", "You can't change teams while you are dead/a spectator.") \
	X("你死亡或处于旁观状态时，不能切换队伍。", "You can't change teams while you are dead/a spectator.") \
	X("You can't join super team if you don't have super rights", "You can't join super team if you don't have super rights") \
	X("你没有 super 权限，不能加入 super 队伍", "You can't join super team if you don't have super rights") \
	X("You have started racing already", "You have started racing already") \
	X("你已经开始比赛了", "You have started racing already") \
	X("You have used practice mode already", "You have used practice mode already") \
	X("你已经使用过练习模式了", "You have used practice mode already") \
	X("This team is currently saving", "This team is currently saving") \
	X("这个队伍当前正在存档", "This team is currently saving") \
	X("Your team is currently saving", "Your team is currently saving") \
	X("你的队伍当前正在存档", "Your team is currently saving") \
	X("Start holding the hook before loading the savegame to keep the hook", "Start holding the hook before loading the savegame to keep the hook") \
	X("载入存档前先按住钩子，这样可以保留当前钩子状态", "Start holding the hook before loading the savegame to keep the hook") \
	X("Your team has been killed because it contains an invalid tee state", "Your team has been killed because it contains an invalid tee state") \
	X("你的队伍因为包含无效的 tee 状态而被处死", "Your team has been killed because it contains an invalid tee state") \
	X("You died, but will stay in practice until you use kill.", "You died, but will stay in practice until you use kill.") \
	X("你已经死亡，但会继续保持练习模式，直到你输入 kill。", "You died, but will stay in practice until you use kill.") \
	X("This team was disbanded because there are more players than allowed in the team.", "This team was disbanded because there are more players than allowed in the team.") \
	X("这个队伍因人数超过允许上限而被解散。", "This team was disbanded because there are more players than allowed in the team.") \
	X("你的队伍已被解锁队伍图块解除锁定", "Your team was unlocked by an unlock team tile") \
	X("Enter /practice mode or restart to avoid the entire team being killed in 60 seconds", "Enter /practice mode or restart to avoid the entire team being killed in 60 seconds") \
	X("输入 /practice 或重新开始，避免整队在 60 秒后被处死", "Enter /practice mode or restart to avoid the entire team being killed in 60 seconds") \
	X("Join a team to enable practice mode, which means you can use /r, but can't earn a rank.", "Join a team to enable practice mode, which means you can use /r, but can't earn a rank.") \
	X("先加入队伍才能开启练习模式。开启后可以使用 /r，但不会获得排名", "Join a team to enable practice mode, which means you can use /r, but can't earn a rank.") \
	X("Practice mode can't be enabled in team 0 mode.", "Practice mode can't be enabled in team 0 mode.") \
	X("0 队模式下不能开启练习模式", "Practice mode can't be enabled in team 0 mode.") \
	X("Practice mode can't be enabled while team save or load is in progress", "Practice mode can't be enabled while team save or load is in progress") \
	X("队伍正在存档或读档时，不能开启练习模式", "Practice mode can't be enabled while team save or load is in progress") \
	X("Team is already in practice mode", "Team is already in practice mode") \
	X("队伍已经处于练习模式", "Team is already in practice mode") \
	X("Practice mode enabled for your team, happy practicing!", "Practice mode enabled for your team, happy practicing!") \
	X("你的队伍已开启练习模式，祝你练习愉快！", "Practice mode enabled for your team, happy practicing!") \
	X("This team can't be locked", "This team can't be locked") \
	X("这个队伍不能被锁定", "This team can't be locked") \
	X("Teams are disabled", "Teams are disabled") \
	X("队伍功能已禁用", "Teams are disabled") \
	X("Invites are disabled", "Invites are disabled") \
	X("/map is disabled", "/map is disabled") \
	X("本服务器已禁用 /map", "/map is disabled") \
	X("Practice mode is disabled", "Practice mode is disabled") \
	X("本服务器已禁用练习模式", "Practice mode is disabled") \
	X("Save-function is disabled on this server", "Save-function is disabled on this server") \
	X("本服务器已禁用存档功能", "Save-function is disabled on this server") \
	X("You must join a team and play with somebody or else you can't play", "You must join a team and play with somebody or else you can't play") \
	X("你必须加入一个队伍并和其他人一起玩，否则无法开始", "You must join a team and play with somebody or else you can't play") \
	X("No empty team left.", "No empty team left.") \
	X("已经没有空队伍了", "No empty team left.") \
	X("You can't change teams that fast!", "You can't change teams that fast!") \
	X("你切换队伍太快了", "You can't change teams that fast!") \
	X("This team is locked using /lock. Only members of the team can unlock it using /lock.", "This team is locked using /lock. Only members of the team can unlock it using /lock.") \
	X("这个队伍已用 /lock 锁定，只有队伍成员才能用 /lock 解锁", "This team is locked using /lock. Only members of the team can unlock it using /lock.") \
	X("This team is locked using /lock. Only members of the team can invite you or unlock it using /lock.", "This team is locked using /lock. Only members of the team can invite you or unlock it using /lock.") \
	X("这个队伍已用 /lock 锁定，只有队伍成员才能邀请你或用 /lock 解锁", "This team is locked using /lock. Only members of the team can invite you or unlock it using /lock.") \
	X("Player not found", "Player not found") \
	X("未找到该玩家", "Player not found") \
	X("Player already invited", "Player already invited") \
	X("该玩家已经被邀请过了", "Player already invited") \
	X("Can't invite this quickly", "Can't invite this quickly") \
	X("Can't invite players to this team", "Can't invite players to this team") \
	X("这个队伍不能邀请玩家", "Can't invite players to this team") \
	X("Team mode change disabled", "Team mode change disabled") \
	X("队伍模式切换已禁用", "Team mode change disabled") \
	X("Team mode change is disabled on this server.", "Team mode change is disabled on this server.") \
	X("本服务器已禁用队伍模式切换。", "Team mode change is disabled on this server.") \
	X("This team can't have the mode changed", "This team can't have the mode changed") \
	X("这个队伍不能切换模式", "This team can't have the mode changed") \
	X("Team mode can't be changed while racing", "Team mode can't be changed while racing") \
	X("比赛进行中不能切换队伍模式", "Team mode can't be changed while racing")

#define QM_HUD_NOTIFICATION_STATIC_SWAP_RESCUE_RULES(X) \
	X("Unknown argument. Check '/rescuemode list'", "Unknown argument. Check '/rescuemode list'") \
	X("未知救援模式参数", "Unknown argument. Check '/rescuemode list'") \
	X("There is nowhere to go back to.", "There is nowhere to go back to.") \
	X("没有可返回的位置。", "There is nowhere to go back to.") \
	X("You're not in a team with /practice turned on. Note that you can't earn a rank with practice enabled.", "You're not in a team with /practice turned on. Note that you can't earn a rank with practice enabled.") \
	X("你不在开启了 /practice 的队伍里。注意：开启练习模式后无法获得排名。", "You're not in a team with /practice turned on. Note that you can't earn a rank with practice enabled.") \
	X("You haven't previously teleported. Use /tp before using this command.", "You haven't previously teleported. Use /tp before using this command.") \
	X("你之前没有传送过。请先使用 /tp 再执行这个命令。", "You haven't previously teleported. Use /tp before using this command.") \
	X("There is no teleporter with that index on the map.", "There is no teleporter with that index on the map.") \
	X("地图上不存在这个编号的传送器。", "There is no teleporter with that index on the map.") \
	X("There is no checkpoint teleporter with that index on the map.", "There is no checkpoint teleporter with that index on the map.") \
	X("地图上不存在这个编号的检查点传送器。", "There is no checkpoint teleporter with that index on the map.") \
	X("Can't enable team 0 mode with practice mode on.", "Can't enable team 0 mode with practice mode on.") \
	X("练习模式开启时不能启用 team 0 模式。", "Can't enable team 0 mode with practice mode on.") \
	X("Can't swap with yourself", "Can't swap with yourself") \
	X("你不能和自己交换位置", "Can't swap with yourself") \
	X("Player is on a different team", "Player is on a different team") \
	X("目标玩家不在你的队伍里", "Player is on a different team") \
	X("You and other player need to have started the map", "You and other player need to have started the map") \
	X("你和对方都需要先开始地图，才能交换位置", "You and other player need to have started the map") \
	X("Need to have started the map to swap with a player.", "Need to have started the map to swap with a player.") \
	X("你需要先开始地图，才能和其他玩家交换位置", "Need to have started the map to swap with a player.") \
	X("You and the other player must not be paused.", "You and the other player must not be paused.") \
	X("你和对方都不能处于暂停状态，才能交换位置", "You and the other player must not be paused.") \
	X("Swap is disabled on this server.", "Swap is disabled on this server.") \
	X("本服务器已禁用交换功能", "Swap is disabled on this server.") \
	X("Swap is not available on forced solo servers.", "Swap is not available on forced solo servers.") \
	X("强制 solo 服务器上不能使用交换功能", "Swap is not available on forced solo servers.") \
	X("Join a team to use swap feature, which means you can swap positions with each other.", "Join a team to use swap feature, which means you can swap positions with each other.") \
	X("先加入队伍后才能使用交换功能，也就是和队友互换位置", "Join a team to use swap feature, which means you can swap positions with each other.") \
	X("You do not have a pending swap request.", "You do not have a pending swap request.") \
	X("你当前没有待处理的交换请求", "You do not have a pending swap request.")

#define QM_HUD_NOTIFICATION_STATIC_VOTE_MODERATION_RULES(X) \
	X("You are running a vote, please try again after the vote is done!", "You are running a vote, please try again after the vote is done!") \
	X("你正在发起投票，请等当前投票结束后再试", "You are running a vote, please try again after the vote is done!") \
	X("Invalid option", "Invalid option") \
	X("无效的投票选项", "Invalid option") \
	X("Server does not allow voting to kick players", "Server does not allow voting to kick players") \
	X("本服务器不允许发起踢人投票", "Server does not allow voting to kick players") \
	X("Invalid client id to kick", "Invalid client id to kick") \
	X("用于踢人的客户端 ID 无效", "Invalid client id to kick") \
	X("You can't kick yourself", "You can't kick yourself") \
	X("你不能踢自己", "You can't kick yourself") \
	X("You can't kick authorized players", "You can't kick authorized players") \
	X("你不能踢已授权玩家", "You can't kick authorized players") \
	X("You can kick only your team member", "You can kick only your team member") \
	X("你只能踢自己队伍里的成员", "You can kick only your team member") \
	X("Server does not allow voting to move players to spectators", "Server does not allow voting to move players to spectators") \
	X("本服务器不允许发起移至旁观投票", "Server does not allow voting to move players to spectators") \
	X("Invalid client id to move to spectators", "Invalid client id to move to spectators") \
	X("用于移至旁观的客户端 ID 无效", "Invalid client id to move to spectators") \
	X("You can't move yourself to spectators", "You can't move yourself to spectators") \
	X("你不能把自己移到旁观", "You can't move yourself to spectators") \
	X("You can't move authorized players to spectators", "You can't move authorized players to spectators") \
	X("你不能把已授权玩家移到旁观", "You can't move authorized players to spectators") \
	X("You can only move your team member to spectators", "You can only move your team member to spectators") \
	X("你只能把自己队伍里的成员移到旁观", "You can only move your team member to spectators") \
	X("Kill Protection enabled. If you really want to join the spectators, first type /kill", "Kill Protection enabled. If you really want to join the spectators, first type /kill") \
	X("已开启防自杀保护。若确要旁观，请先输入 /kill", "Kill Protection enabled. If you really want to join the spectators, first type /kill") \
	X("You can only vote after logging in.", "You can only vote after logging in.") \
	X("登录后才可以发起投票", "You can only vote after logging in.") \
	X("You are not allowed to vote because we're currently checking for VPNs. Try again in ~30 seconds.", "You are not allowed to vote because we're currently checking for VPNs. Try again in ~30 seconds.") \
	X("当前正在检查你的 VPN 状态，约 30 秒后再尝试发起投票", "You are not allowed to vote because we're currently checking for VPNs. Try again in ~30 seconds.") \
	X("You are not allowed to vote because you appear to be using a VPN. Try connecting without a VPN or contacting an admin if you think this is a mistake.", "You are not allowed to vote because you appear to be using a VPN. Try connecting without a VPN or contacting an admin if you think this is a mistake.") \
	X("你当前看起来正在使用 VPN，暂时不能发起投票。如有误判，请关闭 VPN 或联系管理员", "You are not allowed to vote because you appear to be using a VPN. Try connecting without a VPN or contacting an admin if you think this is a mistake.") \
	X("Wait for current vote to end before calling a new one.", "Wait for current vote to end before calling a new one.") \
	X("请先等待当前投票结束，再发起新的投票", "Wait for current vote to end before calling a new one.")

#define QM_HUD_NOTIFICATION_STATIC_STATUS_RULES(X) \
	X("Unknown parameter. Accepted values: default, gametimer, broadcast, both, none", "Unknown parameter. Accepted values: default, gametimer, broadcast, both, none") \
	X("未知参数。可用值：default、gametimer、broadcast、both、none", "Unknown parameter. Accepted values: default, gametimer, broadcast, both, none") \
	X("Selected timertype is not supported by your client", "Selected timertype is not supported by your client") \
	X("你当前客户端不支持所选计时器类型", "Selected timertype is not supported by your client") \
	X("Timer isn't displayed.", "Timer isn't displayed.") \
	X("计时器不会显示。", "Timer isn't displayed.") \
	X("Active moderator mode enabled for you.", "Active moderator mode enabled for you.") \
	X("已为你开启主动管理员模式", "Active moderator mode enabled for you.") \
	X("Active moderator mode disabled for you.", "Active moderator mode disabled for you.") \
	X("已为你关闭主动管理员模式", "Active moderator mode disabled for you.") \
	X("Server kick/spec votes will now be actively moderated.", "Server kick/spec votes will now be actively moderated.") \
	X("服务器的踢人/旁观投票现在会被主动管理员模式接管", "Server kick/spec votes will now be actively moderated.") \
	X("Server kick/spec votes are no longer actively moderated.", "Server kick/spec votes are no longer actively moderated.") \
	X("服务器踢人/观战投票已不再由管理员主动监管。", "Server kick/spec votes are no longer actively moderated.") \
	X("服务器的踢人/旁观投票已不再由主动管理员模式接管", "Server kick/spec votes are no longer actively moderated.") \
	X("You can see other players. To disable this use DDNet client and type /showothers", "You can see other players. To disable this use DDNet client and type /showothers") \
	X("你当前可以看到其他玩家。要关闭此功能，请使用 DDNet 客户端并输入 /showothers", "You can see other players. To disable this use DDNet client and type /showothers") \
	X("Active moderator mode disabled because you are afk.", "Active moderator mode disabled because you are afk.") \
	X("由于你已挂机，主动管理员模式已关闭", "Active moderator mode disabled because you are afk.") \
	X("The force pause timer is now over, you can exit with /spec", "The force pause timer is now over, you can exit with /spec") \
	X("强制暂停计时已结束，你现在可以用 /spec 退出", "The force pause timer is now over, you can exit with /spec") \
	X("Can't /spec that quickly.", "Can't /spec that quickly.") \
	X("你不能这么快再次 /spec。", "Can't /spec that quickly.") \
	X("Invalid spectator id used", "Invalid spectator id used") \
	X("无效的旁观目标 ID", "Invalid spectator id used") \
	X("Players are not allowed to chat from VPNs at this time", "Players are not allowed to chat from VPNs at this time") \
	X("当前使用 VPN 的玩家不允许发言", "Players are not allowed to chat from VPNs at this time") \
	X("You can't check your team while you are dead/a spectator.", "You can't check your team while you are dead/a spectator.") \
	X("Showing the team top 5 is not allowed on this server.", "Showing the team top 5 is not allowed on this server.") \
	X("本服务器不允许查看队伍前 5 名", "Showing the team top 5 is not allowed on this server.") \
	X("Showing the top is not allowed on this server.", "Showing the top is not allowed on this server.") \
	X("本服务器不允许查看排行榜", "Showing the top is not allowed on this server.") \
	X("Showing the times of others is not allowed on this server.", "Showing the times of others is not allowed on this server.") \
	X("本服务器不允许查看其他玩家的成绩", "Showing the times of others is not allowed on this server.") \
	X("Showing the team rank of other players is not allowed on this server.", "Showing the team rank of other players is not allowed on this server.") \
	X("本服务器不允许查看其他玩家的队伍排名", "Showing the team rank of other players is not allowed on this server.") \
	X("Showing the rank of other players is not allowed on this server.", "Showing the rank of other players is not allowed on this server.") \
	X("本服务器不允许查看其他玩家的排名", "Showing the rank of other players is not allowed on this server.") \
	X("Showing the global points of other players is not allowed on this server.", "Showing the global points of other players is not allowed on this server.") \
	X("本服务器不允许查看其他玩家的全局积分。", "Showing the global points of other players is not allowed on this server.") \
	X("Showing the global top points is not allowed on this server.", "Showing the global top points is not allowed on this server.") \
	X("Showing the checkpoint times is not allowed on this server.", "Showing the checkpoint times is not allowed on this server.") \
	X("Showing players from other teams is disabled", "Showing players from other teams is disabled") \
	X("本服务器已禁用显示其他队伍玩家", "Showing players from other teams is disabled") \
	X("本服务器不允许查看全局积分排行榜", "Showing the global top points is not allowed on this server.") \
	X("本服务器不允许查看 checkpoint 时间", "Showing the checkpoint times is not allowed on this server.") \
	X("Teams are available on this server ；队伍上锁后，队内任意玩家死亡都会导致全队死亡", "Teams are available on this server; if the team is locked, any team member dying will kill the whole team") \
	X("Teams are not available on this server ；队伍上锁后，队内任意玩家死亡都会导致全队死亡", "Teams are not available on this server; if the team is locked, any team member dying will kill the whole team") \
	X("本服务器允许组队；队伍上锁后，队内任意玩家死亡都会导致全队死亡", "Teams are available on this server; if the team is locked, any team member dying will kill the whole team") \
	X("本服务器不允许组队；队伍上锁后，队内任意玩家死亡都会导致全队死亡", "Teams are not available on this server; if the team is locked, any team member dying will kill the whole team") \
	X("You have to be in a team to play on this server and all of your team will die if the team is locked", "You have to be in a team to play on this server; if the team is locked, any team member dying will kill the whole team") \
	X("你必须加入队伍才能在本服务器游玩；队伍上锁后，队内任意玩家死亡都会导致全队死亡", "You have to be in a team to play on this server; if the team is locked, any team member dying will kill the whole team") \
	X("Players can collide on this server", "Players can collide on this server") \
	X("Players can't collide on this server", "Players can't collide on this server") \
	X("Players can hook each other on this server", "Players can hook each other on this server") \
	X("Players can't hook each other on this server", "Players can't hook each other on this server") \
	X("Scores are private on this server", "Scores are private on this server") \
	X("Scores are public on this server", "Scores are public on this server") \
	X("本服务器允许玩家碰撞", "Players can collide on this server") \
	X("本服务器不允许玩家碰撞", "Players can't collide on this server") \
	X("本服务器允许玩家互钩", "Players can hook each other on this server") \
	X("本服务器不允许玩家互钩", "Players can't hook each other on this server") \
	X("本服务器的成绩是私密的", "Scores are private on this server") \
	X("本服务器的成绩是公开的", "Scores are public on this server") \
	X("You will not receive any further global chat and server messages", "You will not receive any further global chat and server messages") \
	X("你将不再接收全局聊天和服务器消息", "You will not receive any further global chat and server messages") \
	X("You will receive global chat and server messages", "You will receive global chat and server messages") \
	X("你将继续接收全局聊天和服务器消息", "You will receive global chat and server messages") \
	X("Command is not available on solo servers", "Command is not available on solo servers") \
	X("该命令在 solo 服务器上不可用", "Command is not available on solo servers") \
	X("Emotes are disabled.", "Emotes are disabled.") \
	X("表情功能已禁用。", "Emotes are disabled.") \
	X("You can now use the preset eye emotes.", "You can now use the preset eye emotes.") \
	X("你现在可以使用预设眼睛表情了。", "You can now use the preset eye emotes.") \
	X("You don't have any eye emotes, remember to bind some.", "You don't have any eye emotes, remember to bind some.") \
	X("你还没有绑定任何眼睛表情，记得先绑定。", "You don't have any eye emotes, remember to bind some.") \
	X("No player with this name found.", "No player with this name found.") \
	X("未找到这个名字的玩家。", "No player with this name found.") \
	X("Invalid X coordinate.", "Invalid X coordinate.") \
	X("无效的 X 坐标。", "Invalid X coordinate.") \
	X("Invalid Y coordinate.", "Invalid Y coordinate.") \
	X("无效的 Y 坐标。", "Invalid Y coordinate.") \
	X("Can't recognize specified arguments. Usage: /tpxy x y, e.g. /tpxy 9 3.", "Can't recognize specified arguments. Usage: /tpxy x y, e.g. /tpxy 9 3.") \
	X("无法识别指定参数。用法：/tpxy x y，例如 /tpxy 9 3。", "Can't recognize specified arguments. Usage: /tpxy x y, e.g. /tpxy 9 3.") \
	X("You can't hit others", "You can't hit others") \
	X("你现在不能攻击其他玩家", "You can't hit others") \
	X("You can hit others", "You can hit others") \
	X("你现在可以攻击其他玩家", "You can hit others") \
	X("You can't collide with others", "You can't collide with others") \
	X("你现在不能与其他玩家碰撞", "You can't collide with others") \
	X("You can collide with others", "You can collide with others") \
	X("你现在可以与其他玩家碰撞", "You can collide with others") \
	X("You can't hook others", "You can't hook others") \
	X("你现在不能钩中其他玩家", "You can't hook others") \
	X("You can hook others", "You can hook others") \
	X("你现在可以钩中其他玩家", "You can hook others") \
	X("You have unlimited air jumps", "You have unlimited air jumps") \
	X("你拥有无限空中跳", "You have unlimited air jumps") \
	X("You don't have unlimited air jumps", "You don't have unlimited air jumps") \
	X("你不再拥有无限空中跳", "You don't have unlimited air jumps") \
	X("You have a jetpack gun", "You have a jetpack gun") \
	X("你现在拥有喷气枪", "You have a jetpack gun") \
	X("You lost your jetpack gun", "You lost your jetpack gun") \
	X("你失去了喷气枪", "You lost your jetpack gun") \
	X("Teleport gun enabled", "Teleport gun enabled") \
	X("传送枪已开启", "Teleport gun enabled") \
	X("Teleport gun disabled", "Teleport gun disabled") \
	X("传送枪已关闭", "Teleport gun disabled") \
	X("Teleport grenade enabled", "Teleport grenade enabled") \
	X("传送榴弹枪已开启", "Teleport grenade enabled") \
	X("Teleport grenade disabled", "Teleport grenade disabled") \
	X("传送榴弹枪已关闭", "Teleport grenade disabled") \
	X("Teleport laser enabled", "Teleport laser enabled") \
	X("传送激光已开启", "Teleport laser enabled") \
	X("Teleport laser disabled", "Teleport laser disabled") \
	X("传送激光已关闭", "Teleport laser disabled") \
	X("You can hammer hit others", "You can hammer hit others") \
	X("你现在可以用锤子攻击其他玩家", "You can hammer hit others") \
	X("You can't hammer hit others", "You can't hammer hit others") \
	X("你现在不能用锤子攻击其他玩家", "You can't hammer hit others") \
	X("You can shoot others with shotgun", "You can shoot others with shotgun") \
	X("你现在可以用散弹枪攻击其他玩家", "You can shoot others with shotgun") \
	X("You can't shoot others with shotgun", "You can't shoot others with shotgun") \
	X("你现在不能用散弹枪攻击其他玩家", "You can't shoot others with shotgun") \
	X("You can shoot others with grenade", "You can shoot others with grenade") \
	X("你现在可以用榴弹枪攻击其他玩家", "You can shoot others with grenade") \
	X("You can't shoot others with grenade", "You can't shoot others with grenade") \
	X("你现在不能用榴弹枪攻击其他玩家", "You can't shoot others with grenade") \
	X("You can shoot others with laser", "You can shoot others with laser") \
	X("你现在可以用激光攻击其他玩家", "You can shoot others with laser") \
	X("You can't shoot others with laser", "You can't shoot others with laser") \
	X("你现在不能用激光攻击其他玩家", "You can't shoot others with laser") \
	X("Endless hook has been activated", "Endless hook has been activated") \
	X("无限钩已开启", "Endless hook has been activated") \
	X("Endless hook has been deactivated", "Endless hook has been deactivated") \
	X("无限钩已关闭", "Endless hook has been deactivated")

#define QM_HUD_NOTIFICATION_LOCALIZATION_ONLY_RULES(X) \
	X("------- 队伍前 5 名 -------", "------- Team Top 5 -------") \
	X("-------- 积分排行 --------", "-------- Top Points --------") \
	X("------------ 全局排行 ------------", "------------ Global Top ------------") \
	X("------------- 最近成绩 -------------", "------------- Last Times -------------") \
	X("/cmdlist 会显示所有聊天命令列表", "/cmdlist will show a list of all chat commands") \
	X("/help + 任意命令 会显示该命令的帮助", "/help + any command will show you the help for this command") \
	X("/times 需要 0、1 或 2 个参数。第 1 个是名字，第 2 个是起始名次", "/times needs 0, 1 or 2 parameter. 1. = name, 2. = start number") \
	X("/top5team 需要 0、1 或 2 个参数。第 1 个是名字，第 2 个是起始名次", "/top5team needs 0, 1 or 2 parameter. 1. = name, 2. = start number") \
	X("你当前没有进行中的私聊会话。先私聊某人即可开始", "You do not have an ongoing conversation. Whisper to someone to start one") \
	X("你必须与其他玩家组队才能开始", "You have to be in a team with other tees to start") \
	X("你必须加入队伍才能在本服务器游玩", "You have to be in a team to play on this server") \
	X("你正在私聊的玩家尚未重连或已离开。请稍等，或改与他人私聊", "The player you were whispering to hasn't reconnected yet or left. Please wait or whisper to someone else") \
	X("你死亡或处于旁观状态时，不能查看自己的队伍。", "You can't check your team while you are dead/a spectator.") \
	X("你现在只有地面跳了", "You only have your ground jump now") \
	X("你现在处于单人区域", "You are now in a solo part") \
	X("你现在已离开单人区域", "You are now out of the solo part") \
	X("你的队伍已开启 team 0 模式。现在会按 team 0 的规则运作。", "Team 0 mode enabled for your team. This will make your team behave like team 0.") \
	X("你老死了", "You died of old age") \
	X("你自己发射的激光不会命中自己，激光会把其他玩家拉向发射者", "Lasers can't hit you if you shot them, and they pull others towards the shooter") \
	X("你自己发射的激光也会命中自己，并把你拉向反弹起点（类似 DDRace Beta）", "Lasers can hit you if you shot them and they pull you towards the bounce origin (Like DDRace Beta)") \
	X("你跑出了自己的最佳成绩。", "You finished with your best time.") \
	X("例如 /help settings 会显示 /settings 的帮助", "Example /help settings will display the help about /settings") \
	X("可用救援模式：auto、manual", "Available rescue modes: auto, manual") \
	X("可用练习命令：", "Available practice commands: ") \
	X("可用表情命令：/emote surprise /emote blink /emote close /emote angry /emote happy /emote pain /emote normal", "Emote commands are: /emote surprise /emote blink /emote close /emote angry /emote happy /emote pain /emote normal") \
	X("在加载/保存队伍的过程中无法开始游戏", "You can't start while loading/saving of team is in progress") \
	X("存档载入成功", "Loading successfully done") \
	X("官方网站: DDNet.org", "Official site: DDNet.org") \
	X("已开启防自杀保护。若确实要自杀，请输入 /kill", "Kill Protection enabled. If you really want to kill, type /kill") \
	X("或访问 DDNet.org", "Or visit DDNet.org") \
	X("投票失败", "Vote failed") \
	X("投票已中止", "Vote aborted") \
	X("投票被否决。请换一个空闲服务器", "Vote failed because of veto. Find an empty server instead") \
	X("投票通过", "Vote passed") \
	X("指定范围内没有成绩记录", "There are no times in the specified range") \
	X("授权玩家强制否决投票", "Vote failed enforced by authorized player") \
	X("授权玩家强制通过投票", "Vote passed enforced by authorized player") \
	X("救援模式已切换为 auto。", "Rescue mode changed to auto.") \
	X("救援模式已切换为 manual。", "Rescue mode changed to manual.") \
	X("无法载入存档：存档编号已损坏", "Unable to load savegame: SaveId corrupted") \
	X("无法载入存档：数据已损坏", "Unable to load savegame: data corrupted") \
	X("无法载入存档：该存档已在其他服务器载入", "Unable to load savegame: loaded on a different server") \
	X("更多命令请查看: /cmdlist", "For more info: /cmdlist") \
	X("未设置服务器规则，请联系管理员。", "No Rules Defined, Kill em all!!") \
	X("本服务器不允许组队", "Teams are not available on this server") \
	X("本服务器允许武器影响其他玩家", "Players weapons affect others") \
	X("本服务器允许组队", "Teams are available on this server") \
	X("本服务器已关闭作弊功能", "Cheats are disabled on this server") \
	X("本服务器已开启作弊功能", "Cheats are enabled on this server") \
	X("本服务器的武器不会影响其他玩家", "Players weapons has no affect on others") \
	X("本服务器的钩子时长不受限制", "Players hook time is unlimited") \
	X("本服务器的钩子时长受限制", "Players hook time is limited") \
	X("没有找到对应设置。输入 /settings 可以查看可用设置", "no matching settings found, type /settings to view them") \
	X("玩家不能通过 Callvote 菜单发起踢人投票", "Players can't use the Callvote menu tab to kick offenders") \
	X("玩家可以通过 Callvote 菜单发起踢人投票", "Players can use Callvote menu tab to kick offenders") \
	X("示例：/emote surprise 10 表示持续 10 秒，或直接 /emote surprise（默认 1 秒）", "Example: /emote surprise 10 for 10 seconds or /emote surprise (default 1 second)") \
	X("示例：/map adr3 可以发起 Adrenaline 3 的换图投票。这表示地图名必须以 'a' 开头，并按顺序包含 'd'、'r'、'3'", "Example: /map adr3 to call vote for Adrenaline 3. This means that the map name must start with 'a' and contain the characters 'd', 'r' and '3' in that order") \
	X("被投票踢出的玩家只会被踢出，不会被封禁", "Players are just kicked and not banned if they get voted off") \
	X("该玩家已关闭接收私聊", "This person has disabled receiving whispers") \
	X("请先使用 /pause，然后才能自杀", "Use /pause first then you can kill") \
	X("请友善交流。", "Be nice.") \
	X("请查看 config_directory 下的 ddnet-saves.txt。", "check ddnet-saves.txt in config_directory.") \
	X("请确认你现在使用的名字与存档时相同。", "Make sure you use the same name as you had when saving. ") \
	X("输入 /practicecmdlist 可以查看所有可用的练习命令。最常用的是 /telecursor、/lasttp 和 /rescue", "See /practicecmdlist for a list of all available practice commands. Most commonly used ones are /telecursor, /lasttp and /rescue") \
	X("输入 /settings 加设置名即可查看服务器设置。可用设置有：", "to check a server setting say /settings and setting's name, setting names are:") \
	X("输入 /spec 后你会暂停，tee 也会消失", "/spec will pause you and your tee will vanish") \
	X("输入 /spec 后你会暂停，但 tee 不会消失", "/spec will pause you but your tee will not vanish") \
	X("这个存档存在，但你不在其中。", "This save exists, but you are not part of it. ") \
	X("这个存档码已存在", "This save-code already exists") \
	X("这个服务器上没有找到符合条件的地图！", "No maps found on this server!") \
	X("这张地图上没有找到存档", "No saves found on this map") \
	X("这张地图没有对应的存档", "No such savegame for this map") \
	X("邀请功能已禁用", "Invites are disabled") \
	X("邀请过于频繁，请稍后再试", "Can't invite this quickly")
#endif
