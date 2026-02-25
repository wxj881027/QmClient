/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmAnim.h"

#include <algorithm>
#include <cmath>

void CUiV2AnimationRuntime::Reset()
{
	m_TimeSec = 0.0f;
	m_CompletedEvents.clear();
	m_NextTrackId = 1;
	m_Values.clear();
	m_ActiveTracks.clear();
	m_QueuedTracks.clear();
}

float CUiV2AnimationRuntime::ApplyEasing(float t, EEasing Easing) const
{
	const float Clamped = std::clamp(t, 0.0f, 1.0f);
	switch(Easing)
	{
	case EEasing::LINEAR:
		return Clamped;
	case EEasing::EASE_IN:
		return Clamped * Clamped;
	case EEasing::EASE_OUT:
		return Clamped * (2.0f - Clamped);
	case EEasing::EASE_IN_OUT:
		return Clamped < 0.5f ? 2.0f * Clamped * Clamped : -1.0f + (4.0f - 2.0f * Clamped) * Clamped;
	}
	return Clamped;
}

float CUiV2AnimationRuntime::TrackProgress(const SActiveTrack &Track) const
{
	const float Duration = std::max(0.0f, Track.m_Transition.m_DurationSec);
	const float Delay = std::max(0.0f, Track.m_Transition.m_DelaySec);
	const float LocalElapsed = std::max(0.0f, Track.m_ElapsedSec - Delay);
	if(Duration <= 0.0f)
		return LocalElapsed > 0.0f ? 1.0f : 0.0f;
	return std::clamp(LocalElapsed / Duration, 0.0f, 1.0f);
}

bool CUiV2AnimationRuntime::StartTrack(const STrackKey &Key, const SUiAnimRequest &Request, float StartValue)
{
	SActiveTrack Track;
	Track.m_Start = StartValue;
	Track.m_Target = Request.m_Target;
	Track.m_Current = StartValue;
	Track.m_ElapsedSec = 0.0f;
	Track.m_Transition = Request.m_Transition;
	if(Track.m_Transition.m_DurationSec < 0.0f)
		Track.m_Transition.m_DurationSec = 0.0f;
	if(Track.m_Transition.m_DelaySec < 0.0f)
		Track.m_Transition.m_DelaySec = 0.0f;
	Track.m_TrackId = Request.m_TrackId != 0 ? Request.m_TrackId : m_NextTrackId++;
	if(Track.m_Transition.m_DurationSec <= 0.0f && Track.m_Transition.m_DelaySec <= 0.0f)
	{
		m_Values[Key] = Track.m_Target;
		m_CompletedEvents.push_back({Key.m_NodeKey, Key.m_Property, Track.m_TrackId});
		return false;
	}

	m_ActiveTracks[Key] = Track;
	m_Values[Key] = StartValue;
	return true;
}

void CUiV2AnimationRuntime::StartQueuedTracks(const STrackKey &Key, float StartValue)
{
	float CurrentStartValue = StartValue;
	while(true)
	{
		auto ItQueue = m_QueuedTracks.find(Key);
		if(ItQueue == m_QueuedTracks.end() || ItQueue->second.empty())
			return;

		SUiAnimRequest Next = ItQueue->second.front();
		ItQueue->second.pop_front();
		if(ItQueue->second.empty())
			m_QueuedTracks.erase(ItQueue);

		if(StartTrack(Key, Next, CurrentStartValue))
			return;

		CurrentStartValue = Next.m_Target;
	}
}

void CUiV2AnimationRuntime::CompleteTrack(const STrackKey &Key, const SActiveTrack &Track)
{
	m_Values[Key] = Track.m_Target;
	m_CompletedEvents.push_back({Key.m_NodeKey, Key.m_Property, Track.m_TrackId});
	m_ActiveTracks.erase(Key);
	StartQueuedTracks(Key, Track.m_Target);
}

float CUiV2AnimationRuntime::CurrentValueFor(const STrackKey &Key, float DefaultValue) const
{
	const auto ItActive = m_ActiveTracks.find(Key);
	if(ItActive != m_ActiveTracks.end())
		return ItActive->second.m_Current;

	const auto ItValue = m_Values.find(Key);
	if(ItValue != m_Values.end())
		return ItValue->second;

	return DefaultValue;
}

void CUiV2AnimationRuntime::SetValue(uint64_t NodeKey, EUiAnimProperty Property, float Value)
{
	const STrackKey Key{NodeKey, Property};
	m_Values[Key] = Value;
	m_ActiveTracks.erase(Key);
	m_QueuedTracks.erase(Key);
}

float CUiV2AnimationRuntime::GetValue(uint64_t NodeKey, EUiAnimProperty Property, float DefaultValue) const
{
	const STrackKey Key{NodeKey, Property};
	return CurrentValueFor(Key, DefaultValue);
}

bool CUiV2AnimationRuntime::RequestAnimation(const SUiAnimRequest &Request)
{
	const STrackKey Key{Request.m_NodeKey, Request.m_Property};
	const float BaseValue = CurrentValueFor(Key, 0.0f);
	auto ItActive = m_ActiveTracks.find(Key);
	if(ItActive == m_ActiveTracks.end())
	{
		StartTrack(Key, Request, BaseValue);
		return true;
	}

	SActiveTrack &Active = ItActive->second;
	switch(Request.m_Transition.m_Interrupt)
	{
	case EUiAnimInterruptPolicy::REPLACE:
	{
		m_QueuedTracks.erase(Key);
		StartTrack(Key, Request, Active.m_Current);
		return true;
	}
	case EUiAnimInterruptPolicy::QUEUE:
	{
		m_QueuedTracks[Key].push_back(Request);
		return true;
	}
	case EUiAnimInterruptPolicy::KEEP_HIGHER_PRIORITY:
	{
		if(Active.m_Transition.m_Priority > Request.m_Transition.m_Priority)
			return false;
		m_QueuedTracks.erase(Key);
		StartTrack(Key, Request, Active.m_Current);
		return true;
	}
	case EUiAnimInterruptPolicy::MERGE_TARGET:
	{
		if(Active.m_Transition.m_Priority > Request.m_Transition.m_Priority)
			return false;
		if(Request.m_Transition.m_DurationSec <= 0.0f && Request.m_Transition.m_DelaySec <= 0.0f)
		{
			const uint32_t TrackId = Request.m_TrackId != 0 ? Request.m_TrackId : Active.m_TrackId;
			m_Values[Key] = Request.m_Target;
			m_CompletedEvents.push_back({Key.m_NodeKey, Key.m_Property, TrackId});
			m_ActiveTracks.erase(Key);
			StartQueuedTracks(Key, Request.m_Target);
			return true;
		}

		const float Progress = ApplyEasing(TrackProgress(Active), Active.m_Transition.m_Easing);
		const float Current = Active.m_Current;
		const float Denominator = 1.0f - Progress;
		float NewStart = Current;
		if(std::abs(Denominator) > 1e-6f)
		{
			NewStart = (Current - Progress * Request.m_Target) / Denominator;
		}
		Active.m_Start = NewStart;
		Active.m_Target = Request.m_Target;
		Active.m_Transition.m_Priority = Request.m_Transition.m_Priority;
		Active.m_TrackId = Request.m_TrackId != 0 ? Request.m_TrackId : Active.m_TrackId;
		return true;
	}
	}

	return false;
}

void CUiV2AnimationRuntime::Advance(float Dt)
{
	if(Dt <= 0.0f)
		return;
	const float ClampedDt = std::min(Dt, 1.0f / 15.0f);
	m_TimeSec += ClampedDt;

	std::deque<STrackKey> vCompleted;
	for(auto &Pair : m_ActiveTracks)
	{
		const STrackKey &Key = Pair.first;
		SActiveTrack &Track = Pair.second;
		Track.m_ElapsedSec += ClampedDt;

		const float RawProgress = TrackProgress(Track);
		const float Progress = ApplyEasing(RawProgress, Track.m_Transition.m_Easing);
		Track.m_Current = Track.m_Start + (Track.m_Target - Track.m_Start) * Progress;
		m_Values[Key] = Track.m_Current;

		if(RawProgress >= 1.0f)
			vCompleted.push_back(Key);
	}

	for(const STrackKey &Key : vCompleted)
	{
		auto ItTrack = m_ActiveTracks.find(Key);
		if(ItTrack != m_ActiveTracks.end())
			CompleteTrack(Key, ItTrack->second);
	}
}

bool CUiV2AnimationRuntime::HasActiveAnimation(uint64_t NodeKey, EUiAnimProperty Property) const
{
	return m_ActiveTracks.find({NodeKey, Property}) != m_ActiveTracks.end();
}

int CUiV2AnimationRuntime::ActiveTrackCount() const
{
	return static_cast<int>(m_ActiveTracks.size());
}

int CUiV2AnimationRuntime::QueuedTrackCount() const
{
	int Count = 0;
	for(const auto &Pair : m_QueuedTracks)
		Count += static_cast<int>(Pair.second.size());
	return Count;
}

bool CUiV2AnimationRuntime::PollCompletedEvent(SUiAnimCompleteEvent &EventOut)
{
	if(m_CompletedEvents.empty())
		return false;
	EventOut = m_CompletedEvents.front();
	m_CompletedEvents.pop_front();
	return true;
}

float CUiV2AnimationRuntime::TimeSec() const
{
	return m_TimeSec;
}
