#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_TIMELINE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_TIMELINE_H

#include <game/client/components/qmclient/local_world.h>
#include <game/client/components/qmclient/rank_view_timeline.h>

#include <algorithm>
#include <iterator>
#include <vector>

// 练习录制层与查看模式磁盘格式之间的唯一适配点。
// local_world 刻意不认识 .qmevt；由这一层把中性的轨迹事件/开关快照转成
// Rank 1 与练习组共用的时间线结构，写出逻辑只保留在 rank_view_timeline。
inline qmclient::rank_ghost::SViewTimeline QmBuildPracticeViewTimeline(
	const std::vector<SLocalActorEvent> &vEvents,
	const std::vector<SLocalSwitchState> &vSwitchStates)
{
	qmclient::rank_ghost::SViewTimeline Timeline;
	Timeline.m_vEvents.reserve(vEvents.size());
	for(const SLocalActorEvent &Event : vEvents)
	{
		qmclient::rank_ghost::SViewEvent View = {};
		View.m_RelTick = Event.m_Tick;
		View.m_Type = Event.m_Type;
		View.m_aData[0] = Event.m_aData[0];
		View.m_aData[1] = Event.m_aData[1];
		View.m_aData[2] = Event.m_aData[2];
		Timeline.m_vEvents.push_back(View);
	}
	Timeline.m_vSwitchStates.reserve(vSwitchStates.size());
	for(const SLocalSwitchState &State : vSwitchStates)
	{
		qmclient::rank_ghost::SViewSwitchState View = {};
		View.m_RelTick = State.m_Tick;
		View.m_HighestSwitchNumber = State.m_HighestSwitchNumber;
		View.m_Team = State.m_Team;
		std::copy(std::begin(State.m_aStatus), std::end(State.m_aStatus), std::begin(View.m_aStatus));
		Timeline.m_vSwitchStates.push_back(View);
	}
	return Timeline;
}

#endif
