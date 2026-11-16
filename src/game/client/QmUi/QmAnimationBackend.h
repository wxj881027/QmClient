// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMANIMATIONBACKEND_H
#define GAME_CLIENT_QMUI_QMANIMATIONBACKEND_H

#include <base/color.h>

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_map>
#include <vector>

// UI 动画的唯一底层契约。曲线、补间、弹簧、打断、队列和 motion level
// 都从这里进入，页面层只提交目标值，不再自行决定动画驱动。
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

	// 设计师直觉参数构造（Apple Fluid Interfaces 范式）：ResponseSec = 无阻尼
	// 固有响应周期（秒），DampingRatio = 阻尼比（<1 欠阻尼回弹、=1 临界、>1 过阻尼）。
	// 换算 k=(2π/Response)²、c=4π·DampingRatio/Response（m=1），与解析解积分器闭合。
	// 调手感只动这两个直觉量，不再试错物理量。
	static constexpr SUiSpringConfig FromResponse(float ResponseSec, float DampingRatio)
	{
		const float Response = ResponseSec > 0.0001f ? ResponseSec : 0.0001f;
		const float Omega = 6.283185307179586f / Response;
		SUiSpringConfig Config;
		Config.m_Stiffness = Omega * Omega;
		Config.m_Damping = 2.0f * DampingRatio * Omega;
		return Config;
	}
};

// 阻尼谐振子的解析解积分器：UI 弹簧的唯一实现。以 (Value, Velocity) 为初值在
// [0, Dt] 上推进到 Target——线性常系数 ODE 下与全局闭式解等价，因此帧率无关，
// 且无条件稳定，调用方不需要为数值稳定性截断 Dt。弹簧轨道与滚动越界回弹共用
// 它，避免两处各自演化出手感差异。
inline void QmSpringAdvance(float &Value, float &Velocity, const float Target, const SUiSpringConfig &Config, const float Dt)
{
	if(!(Dt > 0.0f))
		return;

	const float Mass = std::max(Config.m_Mass, 1e-4f);
	const float Stiffness = std::max(Config.m_Stiffness, 0.0f);
	const float Damping = std::max(Config.m_Damping, 0.0f);

	const float Omega0 = std::sqrt(Stiffness / Mass);
	if(Omega0 <= 1e-6f)
	{
		// 无刚度退化：自由阻尼运动（指数衰减）或匀速直线运动。
		const float Lambda = Damping / Mass;
		if(Lambda <= 1e-9f)
		{
			Value += Velocity * Dt;
		}
		else
		{
			const float Decay = std::exp(-Lambda * Dt);
			Value += Velocity * (1.0f - Decay) / Lambda;
			Velocity *= Decay;
		}
		return;
	}

	const float Zeta = Damping / (2.0f * Mass * Omega0);
	const float Displacement = Value - Target;
	const float InitialVelocity = Velocity;
	if(Zeta < 1.0f)
	{
		// 欠阻尼：x(t) = e^(-ζω0 t)·(A·cos(ωd·t) + B·sin(ωd·t))，A=d0，B=(v0+ζω0·d0)/ωd
		const float OmegaD = Omega0 * std::sqrt(std::max(0.0f, 1.0f - Zeta * Zeta));
		const float A = Displacement;
		const float B = (InitialVelocity + Zeta * Omega0 * Displacement) / OmegaD;
		const float Decay = std::exp(-Zeta * Omega0 * Dt);
		const float CosDt = std::cos(OmegaD * Dt);
		const float SinDt = std::sin(OmegaD * Dt);
		Value = Target + Decay * (A * CosDt + B * SinDt);
		Velocity = Decay * ((B * OmegaD - A * Zeta * Omega0) * CosDt - (A * OmegaD + B * Zeta * Omega0) * SinDt);
	}
	else if(Zeta > 1.0f)
	{
		// 过阻尼：x(t) = A·e^(s1·t) + B·e^(s2·t)，s1/s2 为两个负实根
		const float Root = Omega0 * std::sqrt(std::max(0.0f, Zeta * Zeta - 1.0f));
		const float S1 = -Zeta * Omega0 + Root;
		const float S2 = -Zeta * Omega0 - Root;
		const float A = (InitialVelocity - S2 * Displacement) / (S1 - S2);
		const float B = (S1 * Displacement - InitialVelocity) / (S1 - S2);
		const float Exp1 = std::exp(S1 * Dt);
		const float Exp2 = std::exp(S2 * Dt);
		Value = Target + A * Exp1 + B * Exp2;
		Velocity = A * S1 * Exp1 + B * S2 * Exp2;
	}
	else
	{
		// 临界阻尼：x(t) = (d0 + (v0 + ω0·d0)·t)·e^(-ω0 t)
		const float A = Displacement;
		const float B = InitialVelocity + Omega0 * Displacement;
		const float Decay = std::exp(-Omega0 * Dt);
		Value = Target + (A + B * Dt) * Decay;
		Velocity = (InitialVelocity - Omega0 * B * Dt) * Decay;
	}
}

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

namespace qm_animation
{
	int NormalizeMotionLevel(int MotionLevel);
	SUiAnimTransition ApplyMotionLevel(SUiAnimTransition Transition, int MotionLevel);
}

class CQmAnimationBackend
{
public:
	using FCustomEasing = float (*)(float Progress, void *pUser);

	void Reset();
	void Advance(float Dt);

	void SetValue(uint64_t NodeKey, EUiAnimProperty Property, float Value);
	float GetValue(uint64_t NodeKey, EUiAnimProperty Property, float DefaultValue = 0.0f) const;
	// 该轨道是否已有显式值：首见预置必须用它判断，不能用 GetValue 的
	// 默认值哨兵（resolver 层无值默认取 Target，哨兵永不命中）。
	bool HasValue(uint64_t NodeKey, EUiAnimProperty Property) const;

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
	void AdvanceSpring(SActiveTrack &Track, float Dt, EUiAnimProperty Property) const;
	void StoreValue(const STrackKey &Key, float Value);
	void PruneValueCache();
	// 统一打断语义：任何打断都以 (当前值, 当前速度) 为初值落到弹簧继续运动。
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
	struct SValueEntry
	{
		float m_Value = 0.0f;
		uint64_t m_LastUseCounter = 0;
	};
	// 值缓存是"最后一次已知值"，不是权威状态：条目丢失后 resolver 会把首见当作
	// 已在目标位（不会补一段进场动画），因此可以安全淘汰。NodeKey 大量来自指针
	// 地址，界面进出会不断产生新键，不淘汰会让这张表在整个会话里只增不减。
	mutable std::unordered_map<STrackKey, SValueEntry, STrackKeyHasher> m_Values;
	mutable uint64_t m_ValueUseCounter = 0;
	// 硬上限之外的条目按"最近使用帧"淘汰；活跃/排队中的轨道永不淘汰。
	static constexpr size_t MAX_VALUES_SOFT = 4096;
	static constexpr size_t MAX_VALUES_HARD = 8192;
	static constexpr uint64_t MAX_VALUE_IDLE_FRAMES = 1024;
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

// 旧名称保留一个类型别名，确保既有页面和组件可以增量迁移；实际实现只有
// CQmAnimationBackend 一个底层类。
using CUiV2AnimationRuntime = CQmAnimationBackend;

#endif
