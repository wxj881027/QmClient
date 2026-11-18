#include "local_world.h"

#include <base/str.h>
#include <base/system.h>

#include <algorithm>
#include <utility>

void CLocalWorld::BeginTrack(int StartTick)
{
	m_TrackActive = true;
	m_TrackStartTick = StartTick;
	m_CompletedStartTick = 0;
	for(STrackBuffer &Track : m_aTracks)
	{
		Track.m_ClientId = -1;
		Track.m_aName[0] = '\0';
		Track.m_vFrames.clear();
		Track.m_vCompleted.clear();
	}
	m_vEvents.clear();
	m_vCompletedEvents.clear();
	m_vSwitchStates.clear();
	m_vCompletedSwitchStates.clear();
}

void CLocalWorld::EndTrack()
{
	m_TrackActive = false;
	for(STrackBuffer &Track : m_aTracks)
	{
		Track.m_vFrames.clear();
		Track.m_vCompleted.clear();
	}
	m_vEvents.clear();
	m_vCompletedEvents.clear();
	m_vSwitchStates.clear();
	m_vCompletedSwitchStates.clear();
}

void CLocalWorld::SetTrackIdentity(int TrackIndex, int ClientId, const char *pName)
{
	if(TrackIndex < 0 || TrackIndex >= MAX_TRACKS)
		return;
	m_aTracks[TrackIndex].m_ClientId = ClientId;
	str_copy(m_aTracks[TrackIndex].m_aName, pName ? pName : "", sizeof(m_aTracks[TrackIndex].m_aName));
}

void CLocalWorld::AddTrackFrame(int TrackIndex, const SLocalActorFrame &Frame)
{
	if(!m_TrackActive || TrackIndex < 0 || TrackIndex >= MAX_TRACKS)
		return;
	m_aTracks[TrackIndex].m_vFrames.push_back(Frame);
}

void CLocalWorld::AddTrackEvent(const SLocalActorEvent &Event)
{
	if(!m_TrackActive)
		return;
	// .qmevt 的读取器要求 tick 非递减；预测事件按 tick 顺序到达，
	// 偶有乱序时插到正确位置，避免整份时间线被判为损坏。
	auto It = std::lower_bound(m_vEvents.begin(), m_vEvents.end(), Event.m_Tick,
		[](const SLocalActorEvent &Candidate, int Tick) { return Candidate.m_Tick < Tick; });
	m_vEvents.insert(It, Event);
}

void CLocalWorld::AddTrackSwitchState(const SLocalSwitchState &State)
{
	if(!m_TrackActive)
		return;
	// 每 tick 全量采样，只在状态真的变化时留一条：否则一条几分钟的线路
	// 会写成上万条完全相同的记录。每段开头缓冲是空的，所以初始状态必被记录。
	if(!m_vSwitchStates.empty())
	{
		const SLocalSwitchState &Last = m_vSwitchStates.back();
		if(Last.m_HighestSwitchNumber == State.m_HighestSwitchNumber &&
			Last.m_Team == State.m_Team &&
			mem_comp(Last.m_aStatus, State.m_aStatus, sizeof(State.m_aStatus)) == 0)
			return;
	}
	m_vSwitchStates.push_back(State);
}

void CLocalWorld::MarkSegmentBoundary()
{
	if(!m_TrackActive)
		return;

	// 以主轨判定这一段是否够长；其它轨（分身）跟着一起封存，保证多轨同起点。
	const bool Keep = (int)m_aTracks[0].m_vFrames.size() >= MIN_SEGMENT_FRAMES;
	if(Keep)
	{
		for(STrackBuffer &Track : m_aTracks)
			Track.m_vCompleted = Track.m_vFrames;
		m_vCompletedEvents = m_vEvents;
		m_vCompletedSwitchStates = m_vSwitchStates;
		m_CompletedStartTick = m_aTracks[0].m_vFrames.front().m_Tick;
	}
	// 完成段已经复制出来，当前段缓冲可以整个丢弃——练习可能连跑几十分钟，
	// 不能让轨迹无界增长。下一帧起就是一段新的连续线路。
	for(STrackBuffer &Track : m_aTracks)
		Track.m_vFrames.clear();
	m_vEvents.clear();
	m_vSwitchStates.clear();
}

bool CLocalWorld::BestSegment(SLocalSegment *pOut) const
{
	if(!pOut)
		return false;
	*pOut = {};

	const int CurrentSize = m_TrackActive ? (int)m_aTracks[0].m_vFrames.size() : 0;
	const int CompletedSize = (int)m_aTracks[0].m_vCompleted.size();
	// 短于 MIN_SEGMENT_FRAMES 的当前段只有在“已经有一段完成段、且不比它短”时才被接受；
	// 完全没有完成段时仍然按最小时长拒绝，否则刚按开始就停也能存出一条无效线路。
	const bool UseCurrent = CurrentSize > 0 && (CurrentSize >= MIN_SEGMENT_FRAMES || (CompletedSize > 0 && CurrentSize >= CompletedSize));
	const bool UseCompleted = !UseCurrent && CompletedSize > 0;
	if(!UseCurrent && !UseCompleted)
		return false;

	const std::vector<SLocalActorFrame> &vMain = UseCurrent ? m_aTracks[0].m_vFrames : m_aTracks[0].m_vCompleted;
	pOut->m_StartTick = vMain.front().m_Tick;

	for(const STrackBuffer &Track : m_aTracks)
	{
		const std::vector<SLocalActorFrame> &vFrames = UseCurrent ? Track.m_vFrames : Track.m_vCompleted;
		if(vFrames.empty())
			continue;
		SLocalTrack Out;
		Out.m_ClientId = Track.m_ClientId;
		str_copy(Out.m_aName, Track.m_aName, sizeof(Out.m_aName));
		Out.m_vFrames = vFrames;
		pOut->m_vTracks.push_back(std::move(Out));
	}

	// 事件按段起点重定基；段外的事件属于别的世界线片段，不能带进回放。
	const std::vector<SLocalActorEvent> &vEvents = UseCurrent ? m_vEvents : m_vCompletedEvents;
	const int EndTick = vMain.back().m_Tick;
	for(const SLocalActorEvent &Event : vEvents)
	{
		if(Event.m_Tick < pOut->m_StartTick || Event.m_Tick > EndTick)
			continue;
		SLocalActorEvent Relative = Event;
		Relative.m_Tick -= pOut->m_StartTick;
		pOut->m_vEvents.push_back(Relative);
	}

	// 机关开关状态同理：段内第一条记录是该段的初始状态，后续每条是一次变化。
	// 没有这一段，解密型地图回放时门会一直停在服务器当前状态上。
	const std::vector<SLocalSwitchState> &vSwitchStates = UseCurrent ? m_vSwitchStates : m_vCompletedSwitchStates;
	for(const SLocalSwitchState &Switch : vSwitchStates)
	{
		if(Switch.m_Tick < pOut->m_StartTick || Switch.m_Tick > EndTick)
			continue;
		SLocalSwitchState Relative = Switch;
		Relative.m_Tick -= pOut->m_StartTick;
		pOut->m_vSwitchStates.push_back(Relative);
	}
	return true;
}

bool CLocalWorld::BestSegment(std::vector<SLocalActorFrame> *pOut, int *pStartTick) const
{
	if(!pOut || !pStartTick)
		return false;
	SLocalSegment Segment;
	if(!BestSegment(&Segment))
		return false;
	*pOut = Segment.m_vTracks.empty() ? std::vector<SLocalActorFrame>{} : Segment.m_vTracks.front().m_vFrames;
	*pStartTick = Segment.m_StartTick;
	return true;
}

void CLocalWorld::BeginViewSession(int SlotCount)
{
	m_ViewSessionActive = true;
	m_ViewSessionSlotCount = SlotCount;
}

void CLocalWorld::EndViewSession()
{
	m_ViewSessionActive = false;
	m_ViewSessionSlotCount = 0;
}

void CLocalWorld::ShiftTrackTicks(int TickDelta)
{
	if(TickDelta <= 0)
		return;
	// 世界 tick 被平移后，轨迹里的绝对时间量必须同步，否则存成幽灵后
	// 播放头会与当前世界错开一截（起跑、攻击动画都会偏）。
	for(STrackBuffer &Track : m_aTracks)
	{
		for(SLocalActorFrame &Frame : Track.m_vFrames)
		{
			Frame.m_Tick += TickDelta;
			Frame.m_AttackTick += TickDelta;
		}
		for(SLocalActorFrame &Frame : Track.m_vCompleted)
		{
			Frame.m_Tick += TickDelta;
			Frame.m_AttackTick += TickDelta;
		}
	}
	for(SLocalActorEvent &Event : m_vEvents)
		Event.m_Tick += TickDelta;
	for(SLocalActorEvent &Event : m_vCompletedEvents)
		Event.m_Tick += TickDelta;
	for(SLocalSwitchState &Switch : m_vSwitchStates)
		Switch.m_Tick += TickDelta;
	for(SLocalSwitchState &Switch : m_vCompletedSwitchStates)
		Switch.m_Tick += TickDelta;
	m_TrackStartTick += TickDelta;
	if(m_CompletedStartTick != 0)
		m_CompletedStartTick += TickDelta;
}
