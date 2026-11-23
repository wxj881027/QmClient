#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_RECORDER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_RECORDER_H

#include <game/client/components/qmclient/local_world.h>

// 快速练习录制会话的生命周期：开始、暂停、继续、位置突变切段、结束。
// 采样（从分支世界取帧/事件/开关）仍由 CFastPractice 负责；这一层只决定
// 「这段线路现在是否在录、暂停后继续要不要另起一段」——这些规则以前散在
// 组件的布尔量和调用顺序里，出过“暂停恢复把空洞连成直线”的实机问题。
class CPracticeRecorder
{
public:
	void Reset() { m_Paused = false; }
	bool Paused() const { return m_Paused; }
	bool Active(const CLocalWorld &LocalWorld) const { return LocalWorld.TrackActive(); }

	// 开始一条新会话；尚未接入的参与者（id < 0）不登记轨迹。
	void Begin(CLocalWorld &LocalWorld, int StartTick, int LocalClientId, int DummyClientId, const char *pLocalName, const char *pDummyName)
	{
		LocalWorld.BeginTrack(StartTick);
		m_Paused = false;
		if(LocalClientId >= 0)
			LocalWorld.SetTrackIdentity(0, LocalClientId, pLocalName);
		if(DummyClientId >= 0)
			LocalWorld.SetTrackIdentity(1, DummyClientId, pDummyName);
	}

	void Pause() { m_Paused = true; }

	// 暂停后继续必须另起一段：暂停期间 tee 照常跑，直接接上会把空洞连成直线。
	void Resume(CLocalWorld &LocalWorld)
	{
		m_Paused = false;
		LocalWorld.MarkSegmentBoundary();
	}

	void MarkBoundary(CLocalWorld &LocalWorld) const { LocalWorld.MarkSegmentBoundary(); }

	void End(CLocalWorld &LocalWorld)
	{
		m_Paused = false;
		LocalWorld.EndTrack();
	}

private:
	bool m_Paused = false;
};

#endif
