#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_COMMANDS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_COMMANDS_H

#include <game/client/components/qmclient/practice_rescue.h>

// /back 与 /lasttp 的两份记录，对应服务器 CPlayer::m_LastDeath 与 m_LastTeleTee。
// 每份记录只有一个写入来源：死亡写 /back 记录，斜杠传送写 /lasttp 记录。
// 地图传送 tile 两份都不写——服务器把它当成世界事件，不更新这两份 tee，
// 只有 /tp、/tpxy、/tc、/totele、/totelecp 这类命令才会存档。
struct SQmPracticeCommandRecords
{
	bool m_HasLastTeleport = false;
	bool m_HasLastDeath = false;
	SQmPracticeSavedTee m_LastTeleportTee;
	SQmPracticeSavedTee m_LastDeathTee;
};

// 服务器 CCharacter::Die 只在自动救援点已采集时写 m_LastDeath，写的是死亡前
// 最近一次自动救援快照，不是踩到死亡 tile 的坐标。没有自动救援点时 /back 无目标。
inline bool QmRecordPracticeDeath(SQmPracticeCommandRecords &Records, const SQmPracticeSavedTee &AutoTee)
{
	if(!AutoTee.m_Valid)
		return false;
	Records.m_LastDeathTee = AutoTee;
	Records.m_HasLastDeath = true;
	return true;
}

// 服务器在传送落地、解冻、清速度、重置跳跃之后才把整份 tee 存进 m_LastTeleTee，
// 因此调用方必须在角色状态整理完之后再记录。
inline void QmRecordPracticeCommandTeleport(SQmPracticeCommandRecords &Records, const SQmPracticeSavedTee &Tee)
{
	if(!Tee.m_Valid)
		return;
	Records.m_LastTeleportTee = Tee;
	Records.m_HasLastTeleport = true;
}

// 服务器 ConBack：把 m_LastDeath 塞进当前救援模式的救援槽，再调用 Rescue()。
// 赋值本身不置位 m_aSetSavePos，所以手动模式没有手动救援点时 Rescue() 仍然失败；
// Rescue() 在 super/invincible 下同样拒绝。两种情况都只赋值、不传送。
struct SQmPracticeBackDecision
{
	bool m_AssignRescueTee = false;
	bool m_Teleport = false;
};

inline SQmPracticeBackDecision QmEvaluatePracticeBack(bool HasLastDeath, bool ModeSavePointExists, bool TeleportBlocked)
{
	SQmPracticeBackDecision Decision;
	Decision.m_AssignRescueTee = HasLastDeath;
	Decision.m_Teleport = HasLastDeath && ModeSavePointExists && !TeleportBlocked;
	return Decision;
}

// 服务器 ConToTeleporter 用 CWorldCore::RandomOr0 在多出口传送器里随机取一个，
// 只有一个出口时固定第 0 个。返回 -1 表示没有可用出口。
inline int QmPracticeTeleOutIndex(int Count, int RandomBelowCount)
{
	if(Count <= 0)
		return -1;
	if(Count == 1)
		return 0;
	if(RandomBelowCount < 0)
		RandomBelowCount = 0;
	return RandomBelowCount % Count;
}

#endif
