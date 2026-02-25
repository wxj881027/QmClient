/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QM_UI_QM_ANIM_H
#define GAME_CLIENT_QM_UI_QM_ANIM_H

#include <cstdint>
#include <deque>
#include <unordered_map>

enum class EUiAnimProperty
{
	POS_X,
	POS_Y,
	WIDTH,
	HEIGHT,
	ALPHA,
	COLOR_R,
	COLOR_G,
	COLOR_B,
	COLOR_A,
	SCALE,
};

enum class EUiAnimInterruptPolicy
{
	REPLACE,
	QUEUE,
	KEEP_HIGHER_PRIORITY,
	MERGE_TARGET,
};

enum class EEasing
{
	LINEAR,
	EASE_IN,
	EASE_OUT,
	EASE_IN_OUT,
};

struct SUiAnimTransition
{
	float m_DurationSec = 0.18f;
	float m_DelaySec = 0.0f;
	int m_Priority = 0;
	EUiAnimInterruptPolicy m_Interrupt = EUiAnimInterruptPolicy::REPLACE;
	EEasing m_Easing = EEasing::EASE_OUT;
};

struct SUiAnimCompleteEvent
{
	uint64_t m_NodeKey = 0;
	EUiAnimProperty m_Property = EUiAnimProperty::POS_X;
	uint32_t m_TrackId = 0;
};

struct SUiAnimRequest
{
	uint64_t m_NodeKey = 0;
	EUiAnimProperty m_Property = EUiAnimProperty::POS_X;
	float m_Target = 0.0f;
	SUiAnimTransition m_Transition;
	uint32_t m_TrackId = 0;
};

class CUiV2AnimationRuntime
{
public:
	void Reset();
	void Advance(float Dt);

	void SetValue(uint64_t NodeKey, EUiAnimProperty Property, float Value);
	float GetValue(uint64_t NodeKey, EUiAnimProperty Property, float DefaultValue = 0.0f) const;

	bool RequestAnimation(const SUiAnimRequest &Request);
	bool HasActiveAnimation(uint64_t NodeKey, EUiAnimProperty Property) const;
	int ActiveTrackCount() const;
	int QueuedTrackCount() const;

	bool PollCompletedEvent(SUiAnimCompleteEvent &EventOut);
	float TimeSec() const;

private:
	struct STrackKey
	{
		uint64_t m_NodeKey = 0;
		EUiAnimProperty m_Property = EUiAnimProperty::POS_X;

		bool operator==(const STrackKey &Other) const
		{
			return m_NodeKey == Other.m_NodeKey && m_Property == Other.m_Property;
		}
	};

	struct STrackKeyHasher
	{
		size_t operator()(const STrackKey &Key) const
		{
			return std::hash<uint64_t>{}(Key.m_NodeKey) ^ (std::hash<int>{}(static_cast<int>(Key.m_Property)) << 1);
		}
	};

	struct SActiveTrack
	{
		float m_Start = 0.0f;
		float m_Target = 0.0f;
		float m_Current = 0.0f;
		float m_ElapsedSec = 0.0f;
		SUiAnimTransition m_Transition;
		uint32_t m_TrackId = 0;
	};

	float ApplyEasing(float t, EEasing Easing) const;
	float TrackProgress(const SActiveTrack &Track) const;
	bool StartTrack(const STrackKey &Key, const SUiAnimRequest &Request, float StartValue);
	void StartQueuedTracks(const STrackKey &Key, float StartValue);
	void CompleteTrack(const STrackKey &Key, const SActiveTrack &Track);
	float CurrentValueFor(const STrackKey &Key, float DefaultValue) const;

	float m_TimeSec = 0.0f;
	std::deque<SUiAnimCompleteEvent> m_CompletedEvents;
	uint32_t m_NextTrackId = 1;
	std::unordered_map<STrackKey, float, STrackKeyHasher> m_Values;
	std::unordered_map<STrackKey, SActiveTrack, STrackKeyHasher> m_ActiveTracks;
	std::unordered_map<STrackKey, std::deque<SUiAnimRequest>, STrackKeyHasher> m_QueuedTracks;
};

#endif
