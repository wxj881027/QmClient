#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_WORLD_EVENT_GATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_WORLD_EVENT_GATE_H

#include <generated/protocol.h>

// 世界音效播放的唯一门控。三处必须共用同一份判定，否则同一条线路在
// 「实际游玩 / 快速练习 / demo 式回放」里会播出不同的声音：
//   * CGameClient::HandlePredictedEvents（预测侧播放）
//   * CRankGhost::DispatchViewEvent（查看模式回放）
//   * CFastPractice::RecordPracticeEvents（录制时过滤，避免录进当时没播的音）
//
// 参数全部是标量，避免这层反过来依赖禅模式或配置结构。
inline bool QmShouldPlayWorldSound(int SoundId, bool SndGame, bool PlayJumpSound,
	bool PlayDeathOrSpawnSound, bool RaceSounds, bool SndGun, bool SndLongPain)
{
	if(!SndGame)
		return false;
	if(SoundId == SOUND_PLAYER_JUMP && !PlayJumpSound)
		return false;
	if(SoundId == SOUND_PLAYER_DIE && !PlayDeathOrSpawnSound)
		return false;
	// 赛道音效：枪声与长痛音有独立开关，仅在该地图启用 race sounds 时生效。
	if(RaceSounds && ((SoundId == SOUND_GUN_FIRE && !SndGun) ||
				 (SoundId == SOUND_PLAYER_PAIN_LONG && !SndLongPain)))
		return false;
	return true;
}

#endif
