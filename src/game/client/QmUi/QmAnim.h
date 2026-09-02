// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMANIM_H
#define GAME_CLIENT_QMUI_QMANIM_H

#include <base/color.h>

#include <game/client/ui_rect.h>

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

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
	COLOR_MIX,
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
	EASE_OUT_QUART,
	EASE_OUT_BACK,
	EASE_IN_OUT_CUBIC,
	CUBIC_BEZIER,
	CUSTOM,
};

enum class EUiAnimDriver
{
	TWEEN,
	SPRING,
};

struct SUiSpringConfig
{
	float m_Mass = 1.0f;
	float m_Stiffness = 170.0f;
	float m_Damping = 26.0f;
	float m_RestEpsilon = 0.01f;
	float m_RestVelocity = 0.05f;
};

struct SUiBezier
{
	float m_X1 = 0.42f;
	float m_Y1 = 0.0f;
	float m_X2 = 0.58f;
	float m_Y2 = 1.0f;
};

struct SUiAnimTransition
{
	float m_DurationSec = 0.18f;
	float m_DelaySec = 0.0f;
	int m_Priority = 0;
	EUiAnimInterruptPolicy m_Interrupt = EUiAnimInterruptPolicy::REPLACE;
	EEasing m_Easing = EEasing::EASE_OUT;
	EUiAnimDriver m_Driver = EUiAnimDriver::TWEEN;
	SUiSpringConfig m_Spring;
	SUiBezier m_Bezier;
	uint32_t m_CustomEasingId = 0;
	bool m_RespectMotionLevel = true;
};

struct SUiAnimCompleteEvent
{
	uint64_t m_NodeKey = 0;
	EUiAnimProperty m_Property = EUiAnimProperty::POS_X;
	uint32_t m_TrackId = 0;
};

struct SUiAnimGroupCompleteEvent
{
	uint32_t m_GroupId = 0;
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
	using FCustomEasing = float (*)(float Progress, void *pUser);

	void Reset();
	void Advance(float Dt);

	void SetValue(uint64_t NodeKey, EUiAnimProperty Property, float Value);
	float GetValue(uint64_t NodeKey, EUiAnimProperty Property, float DefaultValue = 0.0f) const;

	bool RequestAnimation(const SUiAnimRequest &Request);
	bool HasActiveAnimation(uint64_t NodeKey, EUiAnimProperty Property) const;
	int ActiveTrackCount() const;
	int QueuedTrackCount() const;
	// pUser 会被已启动轨道快照；注销只影响未来轨道，调用方必须保证 pUser 存活到这些轨道结束。
	void RegisterCustomEasing(uint32_t EasingId, FCustomEasing pfnEasing, void *pUser = nullptr);
	void UnregisterCustomEasing(uint32_t EasingId);
	float ResolveTargetValue(uint64_t NodeKey, EUiAnimProperty Property, float Target, const SUiAnimTransition &Transition);
	ColorRGBA ResolveColorFromValue(uint64_t NodeKey, const ColorRGBA &Current, const ColorRGBA &Target);
	float ResolveColorMixValue(uint64_t NodeKey, const ColorRGBA &Target, float DurationSec, EEasing Easing);

	bool PollCompletedEvent(SUiAnimCompleteEvent &EventOut);
	uint32_t AwaitTracks(const uint32_t *pTrackIds, int NumTrackIds);
	bool PollGroupCompletedEvent(SUiAnimGroupCompleteEvent &EventOut);
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
	struct SCustomEasing
	{
		FCustomEasing m_pfnEasing = nullptr;
		void *m_pUser = nullptr;
	};

	struct SActiveTrack
	{
		float m_Start = 0.0f;
		float m_Target = 0.0f;
		float m_Current = 0.0f;
		float m_ElapsedSec = 0.0f;
		float m_Velocity = 0.0f;
		float m_RestTimerSec = 0.0f;
		SUiAnimTransition m_Transition;
		uint32_t m_TrackId = 0;
		FCustomEasing m_pfnCustomEasing = nullptr;
		void *m_pCustomEasingUser = nullptr;
	};

	float ApplyEasing(float t, const SUiAnimTransition &Transition) const;
	float ApplyTrackEasing(float t, const SActiveTrack &Track) const;
	float TrackProgress(const SActiveTrack &Track) const;
	void AdvanceSpring(SActiveTrack &Track, float Dt) const;
	// 统一打断语义：任何打断都以 (当前值, 当前速度) 为初值落到弹簧继续运动
	// （请求本身是弹簧用其参数，是 tween 按时长映射接管弹簧；0 时长 tween 仍瞬移）。
	bool StartTrackInterrupt(const STrackKey &Key, const SUiAnimRequest &Request, const SActiveTrack &Active);
	bool StartTrack(const STrackKey &Key, const SUiAnimRequest &Request, float StartValue, float StartVelocity = 0.0f);
	void StartQueuedTracks(const STrackKey &Key, float StartValue);
	void CompleteTrack(const STrackKey &Key, const SActiveTrack &Track);
	void CancelAwaitedTrack(uint32_t TrackId);
	void CancelAwaitGroup(uint32_t GroupId);
	void CancelQueuedTracksForKey(const STrackKey &Key);
	void CompleteAwaitedTrack(uint32_t TrackId);
	bool IsTrackPending(uint32_t TrackId) const;
	float CurrentValueFor(const STrackKey &Key, float DefaultValue) const;

	float m_TimeSec = 0.0f;
	std::deque<SUiAnimCompleteEvent> m_CompletedEvents;
	std::deque<SUiAnimGroupCompleteEvent> m_GroupCompletedEvents;
	uint32_t m_NextTrackId = 1;
	uint32_t m_NextGroupId = 1;
	std::unordered_map<STrackKey, float, STrackKeyHasher> m_Values;
	std::unordered_map<STrackKey, SActiveTrack, STrackKeyHasher> m_ActiveTracks;
	std::unordered_map<STrackKey, std::deque<SUiAnimRequest>, STrackKeyHasher> m_QueuedTracks;
	std::unordered_map<uint32_t, SCustomEasing> m_CustomEasings;
	struct SResolveTargetState
	{
		float m_Target = 0.0f;
		EUiAnimDriver m_Driver = EUiAnimDriver::TWEEN;
		uint64_t m_LastUseCounter = 0;
	};
	void PruneResolveTargetCache(uint64_t CurrentUseCounter);
	static constexpr size_t MAX_LAST_TARGETS_SOFT = 4096;
	static constexpr size_t MAX_LAST_TARGETS_HARD = 8192;
	std::unordered_map<STrackKey, SResolveTargetState, STrackKeyHasher> m_LastTargets;
	uint64_t m_ResolveUseCounter = 0;
	struct SColorTargetState
	{
		ColorRGBA m_From;
		ColorRGBA m_Target;
		uint64_t m_LastUseCounter = 0;
	};
	void PruneColorTargetCache(uint64_t CurrentUseCounter);
	std::unordered_map<uint64_t, SColorTargetState> m_ColorTargets;
	uint64_t m_ColorUseCounter = 0;
	struct SAwaitGroup
	{
		int m_Remaining = 0;
	};
	std::unordered_map<uint32_t, std::vector<uint32_t>> m_TrackAwaitGroups;
	std::unordered_map<uint32_t, SAwaitGroup> m_AwaitGroups;
};

#endif
