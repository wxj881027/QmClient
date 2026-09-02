// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmAnim.h"

#include "QmMotion.h"

#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>

static bool ColorChanged(const ColorRGBA &A, const ColorRGBA &B)
{
	constexpr float COLOR_EPSILON = 0.0001f;
	return std::abs(A.r - B.r) > COLOR_EPSILON ||
	       std::abs(A.g - B.g) > COLOR_EPSILON ||
	       std::abs(A.b - B.b) > COLOR_EPSILON ||
	       std::abs(A.a - B.a) > COLOR_EPSILON;
}

void CUiV2AnimationRuntime::Reset()
{
	m_TimeSec = 0.0f;
	m_CompletedEvents.clear();
	m_GroupCompletedEvents.clear();
	m_NextTrackId = 1;
	m_NextGroupId = 1;
	m_Values.clear();
	m_ActiveTracks.clear();
	m_QueuedTracks.clear();
	m_CustomEasings.clear();
	m_LastTargets.clear();
	m_ResolveUseCounter = 0;
	m_ColorTargets.clear();
	m_ColorUseCounter = 0;
	m_TrackAwaitGroups.clear();
	m_AwaitGroups.clear();
}

static float SolveBezierY(float TargetX, const SUiBezier &Bezier)
{
	auto SampleX = [&](float t) {
		const float OneMinusT = 1.0f - t;
		return 3.0f * OneMinusT * OneMinusT * t * Bezier.m_X1 + 3.0f * OneMinusT * t * t * Bezier.m_X2 + t * t * t;
	};
	auto SampleXPrime = [&](float t) {
		const float OneMinusT = 1.0f - t;
		return 3.0f * OneMinusT * OneMinusT * Bezier.m_X1 + 6.0f * OneMinusT * t * (Bezier.m_X2 - Bezier.m_X1) + 3.0f * t * t * (1.0f - Bezier.m_X2);
	};

	float t = TargetX;
	for(int i = 0; i < 8; ++i)
	{
		const float CurrentX = SampleX(t);
		const float Err = CurrentX - TargetX;
		if(std::abs(Err) < 1e-5f)
			break;
		const float Slope = SampleXPrime(t);
		if(std::abs(Slope) < 1e-6f)
			break;
		t -= Err / Slope;
		t = std::clamp(t, 0.0f, 1.0f);
	}

	const float OneMinusT = 1.0f - t;
	return 3.0f * OneMinusT * OneMinusT * t * Bezier.m_Y1 + 3.0f * OneMinusT * t * t * Bezier.m_Y2 + t * t * t;
}

float CUiV2AnimationRuntime::ApplyEasing(float t, const SUiAnimTransition &Transition) const
{
	const float Clamped = std::clamp(t, 0.0f, 1.0f);
	switch(Transition.m_Easing)
	{
	case EEasing::LINEAR:
		return Clamped;
	case EEasing::EASE_IN:
		return Clamped * Clamped;
	case EEasing::EASE_OUT:
		return Clamped * (2.0f - Clamped);
	case EEasing::EASE_IN_OUT:
		return Clamped < 0.5f ? 2.0f * Clamped * Clamped : -1.0f + (4.0f - 2.0f * Clamped) * Clamped;
	case EEasing::EASE_OUT_QUART:
	{
		const float OneMinusT = 1.0f - Clamped;
		return 1.0f - OneMinusT * OneMinusT * OneMinusT * OneMinusT;
	}
	case EEasing::EASE_OUT_BACK:
	{
		constexpr float C1 = 1.70158f;
		constexpr float C3 = C1 + 1.0f;
		const float Shifted = Clamped - 1.0f;
		return 1.0f + C3 * Shifted * Shifted * Shifted + C1 * Shifted * Shifted;
	}
	case EEasing::EASE_IN_OUT_CUBIC:
		return Clamped < 0.5f ? 4.0f * Clamped * Clamped * Clamped : 1.0f - std::pow(-2.0f * Clamped + 2.0f, 3.0f) / 2.0f;
	case EEasing::CUBIC_BEZIER:
		return SolveBezierY(Clamped, Transition.m_Bezier);
	case EEasing::CUSTOM:
	{
		const auto ItCustom = m_CustomEasings.find(Transition.m_CustomEasingId);
		if(ItCustom != m_CustomEasings.end() && ItCustom->second.m_pfnEasing != nullptr)
			return std::clamp(ItCustom->second.m_pfnEasing(Clamped, ItCustom->second.m_pUser), 0.0f, 1.0f);
		return Clamped;
	}
	}
	return Clamped;
}

float CUiV2AnimationRuntime::ApplyTrackEasing(float t, const SActiveTrack &Track) const
{
	const float Clamped = std::clamp(t, 0.0f, 1.0f);
	if(Track.m_Transition.m_Easing == EEasing::CUSTOM)
	{
		if(Track.m_pfnCustomEasing != nullptr)
			return std::clamp(Track.m_pfnCustomEasing(Clamped, Track.m_pCustomEasingUser), 0.0f, 1.0f);
		return Clamped;
	}
	return ApplyEasing(Clamped, Track.m_Transition);
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

bool CUiV2AnimationRuntime::StartTrack(const STrackKey &Key, const SUiAnimRequest &Request, float StartValue, float StartVelocity)
{
	SActiveTrack Track;
	Track.m_Start = StartValue;
	Track.m_Target = Request.m_Target;
	Track.m_Current = StartValue;
	Track.m_ElapsedSec = 0.0f;
	Track.m_Velocity = StartVelocity;
	Track.m_RestTimerSec = 0.0f;
	Track.m_Transition = Request.m_Transition;
	if(Track.m_Transition.m_DurationSec < 0.0f)
		Track.m_Transition.m_DurationSec = 0.0f;
	if(Track.m_Transition.m_DelaySec < 0.0f)
		Track.m_Transition.m_DelaySec = 0.0f;
	Track.m_TrackId = Request.m_TrackId != 0 ? Request.m_TrackId : m_NextTrackId++;
	if(Track.m_Transition.m_Easing == EEasing::CUSTOM)
	{
		const auto ItCustom = m_CustomEasings.find(Track.m_Transition.m_CustomEasingId);
		if(ItCustom != m_CustomEasings.end())
		{
			Track.m_pfnCustomEasing = ItCustom->second.m_pfnEasing;
			Track.m_pCustomEasingUser = ItCustom->second.m_pUser;
		}
	}

	const bool IsSpring = Track.m_Transition.m_Driver == EUiAnimDriver::SPRING;
	if(!IsSpring && Track.m_Transition.m_DurationSec <= 0.0f && Track.m_Transition.m_DelaySec <= 0.0f)
	{
		m_Values[Key] = Track.m_Target;
		m_CompletedEvents.push_back({Key.m_NodeKey, Key.m_Property, Track.m_TrackId});
		CompleteAwaitedTrack(Track.m_TrackId);
		return false;
	}

	if(IsSpring && std::abs(Track.m_Current - Track.m_Target) < Track.m_Transition.m_Spring.m_RestEpsilon && std::abs(Track.m_Velocity) < Track.m_Transition.m_Spring.m_RestVelocity)
	{
		m_Values[Key] = Track.m_Target;
		m_CompletedEvents.push_back({Key.m_NodeKey, Key.m_Property, Track.m_TrackId});
		CompleteAwaitedTrack(Track.m_TrackId);
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

namespace
{
	// 固定时长缓动被打断后转弹簧接管：响应时间≈原时长、轻微欠阻尼，保留速度感。
	// 映射是启发式的（苹果把曲线动画打断后同样落到弹簧语义），集中在此便于统一调参。
	SUiSpringConfig InterruptSpringFromTween(const SUiAnimTransition &Transition)
	{
		constexpr float QM_PI = 3.14159265358979323846f;
		constexpr float TAKEOVER_DAMPING_RATIO = 0.85f;
		constexpr float MIN_RESPONSE_SEC = 0.05f;
		constexpr float MAX_RESPONSE_SEC = 0.75f;

		SUiSpringConfig Spring;
		Spring.m_Mass = 1.0f;
		const float Response = std::clamp(Transition.m_DurationSec, MIN_RESPONSE_SEC, MAX_RESPONSE_SEC);
		const float Omega = 2.0f * QM_PI / std::max(Response, 1e-4f);
		Spring.m_Stiffness = Omega * Omega;
		Spring.m_Damping = 2.0f * TAKEOVER_DAMPING_RATIO * Omega;
		Spring.m_RestEpsilon = 0.001f;
		Spring.m_RestVelocity = 0.05f;
		return Spring;
	}
} // namespace

bool CUiV2AnimationRuntime::StartTrackInterrupt(const STrackKey &Key, const SUiAnimRequest &Request, const SActiveTrack &Active)
{
	// 0 时长 tween = 显式瞬移（如 motion level 0 下 ApplyMotionLevel 产生的请求）：
	// 所有中断策略都直接到位，不转弹簧接管。
	const bool RequestIsTween = Request.m_Transition.m_Driver == EUiAnimDriver::TWEEN;
	if(RequestIsTween && Request.m_Transition.m_DurationSec <= 0.0f && Request.m_Transition.m_DelaySec <= 0.0f)
	{
		const uint32_t TrackId = Request.m_TrackId != 0 ? Request.m_TrackId : Active.m_TrackId;
		m_Values[Key] = Request.m_Target;
		m_CompletedEvents.push_back({Key.m_NodeKey, Key.m_Property, TrackId});
		if(TrackId != Active.m_TrackId)
			CancelAwaitedTrack(Active.m_TrackId);
		m_ActiveTracks.erase(Key);
		StartQueuedTracks(Key, Request.m_Target);
		return true;
	}

	// 统一打断语义（可中断 + 速度继承）：任何打断都以 (当前值, 当前速度) 为初值
	// 落到弹簧继续运动——请求本身是弹簧就用它的参数，是 tween 就按时长映射接管弹簧，
	// 不再“从当前值重放曲线”。
	SUiAnimRequest Takeover = Request;
	Takeover.m_Transition.m_Driver = EUiAnimDriver::SPRING;
	Takeover.m_Transition.m_DurationSec = 0.0f;
	Takeover.m_Transition.m_DelaySec = 0.0f;
	if(Request.m_Transition.m_Driver != EUiAnimDriver::SPRING)
		Takeover.m_Transition.m_Spring = InterruptSpringFromTween(Request.m_Transition);
	if(Takeover.m_TrackId != Active.m_TrackId)
		CancelAwaitedTrack(Active.m_TrackId);
	return StartTrack(Key, Takeover, Active.m_Current, Active.m_Velocity);
}

void CUiV2AnimationRuntime::CompleteTrack(const STrackKey &Key, const SActiveTrack &Track)
{
	const float Target = Track.m_Target;
	const uint32_t TrackId = Track.m_TrackId;
	m_Values[Key] = Target;
	m_CompletedEvents.push_back({Key.m_NodeKey, Key.m_Property, TrackId});
	CompleteAwaitedTrack(TrackId);
	m_ActiveTracks.erase(Key);
	StartQueuedTracks(Key, Target);
}

void CUiV2AnimationRuntime::CompleteAwaitedTrack(uint32_t TrackId)
{
	auto ItTrackGroup = m_TrackAwaitGroups.find(TrackId);
	if(ItTrackGroup != m_TrackAwaitGroups.end())
	{
		const std::vector<uint32_t> vGroupIds = ItTrackGroup->second;
		m_TrackAwaitGroups.erase(ItTrackGroup);
		for(const uint32_t GroupId : vGroupIds)
		{
			auto ItGroup = m_AwaitGroups.find(GroupId);
			if(ItGroup != m_AwaitGroups.end())
			{
				--ItGroup->second.m_Remaining;
				if(ItGroup->second.m_Remaining <= 0)
				{
					m_GroupCompletedEvents.push_back({GroupId});
					m_AwaitGroups.erase(ItGroup);
				}
			}
		}
	}
}

void CUiV2AnimationRuntime::CancelAwaitedTrack(uint32_t TrackId)
{
	auto ItTrackGroup = m_TrackAwaitGroups.find(TrackId);
	if(ItTrackGroup == m_TrackAwaitGroups.end())
		return;
	const std::vector<uint32_t> vGroupIds = ItTrackGroup->second;
	for(const uint32_t GroupId : vGroupIds)
		CancelAwaitGroup(GroupId);
}

void CUiV2AnimationRuntime::CancelAwaitGroup(uint32_t GroupId)
{
	if(m_AwaitGroups.erase(GroupId) == 0)
		return;

	for(auto ItTrackGroup = m_TrackAwaitGroups.begin(); ItTrackGroup != m_TrackAwaitGroups.end();)
	{
		std::vector<uint32_t> &vGroupIds = ItTrackGroup->second;
		vGroupIds.erase(std::remove(vGroupIds.begin(), vGroupIds.end(), GroupId), vGroupIds.end());
		if(vGroupIds.empty())
			ItTrackGroup = m_TrackAwaitGroups.erase(ItTrackGroup);
		else
			++ItTrackGroup;
	}
}

void CUiV2AnimationRuntime::CancelQueuedTracksForKey(const STrackKey &Key)
{
	const auto ItQueued = m_QueuedTracks.find(Key);
	if(ItQueued == m_QueuedTracks.end())
		return;
	for(const SUiAnimRequest &Request : ItQueued->second)
		CancelAwaitedTrack(Request.m_TrackId);
	m_QueuedTracks.erase(ItQueued);
}

bool CUiV2AnimationRuntime::IsTrackPending(uint32_t TrackId) const
{
	if(TrackId == 0)
		return false;
	for(const auto &Pair : m_ActiveTracks)
	{
		if(Pair.second.m_TrackId == TrackId)
			return true;
	}
	for(const auto &Pair : m_QueuedTracks)
	{
		for(const SUiAnimRequest &Request : Pair.second)
		{
			if(Request.m_TrackId == TrackId)
				return true;
		}
	}
	return false;
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
	const auto ItActive = m_ActiveTracks.find(Key);
	if(ItActive != m_ActiveTracks.end())
	{
		CancelAwaitedTrack(ItActive->second.m_TrackId);
		m_ActiveTracks.erase(ItActive);
	}
	const auto ItQueued = m_QueuedTracks.find(Key);
	if(ItQueued != m_QueuedTracks.end())
	{
		CancelQueuedTracksForKey(Key);
	}
}

float CUiV2AnimationRuntime::GetValue(uint64_t NodeKey, EUiAnimProperty Property, float DefaultValue) const
{
	const STrackKey Key{NodeKey, Property};
	return CurrentValueFor(Key, DefaultValue);
}

bool CUiV2AnimationRuntime::RequestAnimation(const SUiAnimRequest &Request)
{
	SUiAnimRequest EffectiveRequest = Request;
	if(Request.m_Transition.m_RespectMotionLevel)
		EffectiveRequest.m_Transition = qm_motion::ApplyMotionLevel(Request.m_Transition, g_Config.m_QmUiMotionLevel);

	const STrackKey Key{EffectiveRequest.m_NodeKey, EffectiveRequest.m_Property};
	const float BaseValue = CurrentValueFor(Key, 0.0f);
	auto ItActive = m_ActiveTracks.find(Key);
	if(ItActive == m_ActiveTracks.end())
	{
		StartTrack(Key, EffectiveRequest, BaseValue);
		return true;
	}

	SActiveTrack &Active = ItActive->second;
	switch(EffectiveRequest.m_Transition.m_Interrupt)
	{
	case EUiAnimInterruptPolicy::REPLACE:
	{
		CancelQueuedTracksForKey(Key);
		return StartTrackInterrupt(Key, EffectiveRequest, Active);
	}
	case EUiAnimInterruptPolicy::QUEUE:
	{
		m_QueuedTracks[Key].push_back(EffectiveRequest);
		return true;
	}
	case EUiAnimInterruptPolicy::KEEP_HIGHER_PRIORITY:
	{
		if(Active.m_Transition.m_Priority > EffectiveRequest.m_Transition.m_Priority)
			return false;
		CancelQueuedTracksForKey(Key);
		return StartTrackInterrupt(Key, EffectiveRequest, Active);
	}
	case EUiAnimInterruptPolicy::MERGE_TARGET:
	{
		if(Active.m_Transition.m_Priority > EffectiveRequest.m_Transition.m_Priority)
			return false;
		// MERGE_TARGET 语义：轨道延续，未显式给 TrackId 时沿用活动轨道 id，
		// 让等待组跟踪不被打断（与打断前行为一致）。
		if(EffectiveRequest.m_TrackId == 0)
			EffectiveRequest.m_TrackId = Active.m_TrackId;
		return StartTrackInterrupt(Key, EffectiveRequest, Active);
	}
	}

	return false;
}

void CUiV2AnimationRuntime::AdvanceSpring(SActiveTrack &Track, float Dt) const
{
	const float Delay = std::max(0.0f, Track.m_Transition.m_DelaySec);
	if(Track.m_ElapsedSec < Delay)
		return;

	const SUiSpringConfig &Cfg = Track.m_Transition.m_Spring;
	const float Mass = std::max(Cfg.m_Mass, 1e-4f);
	const float Stiffness = std::max(Cfg.m_Stiffness, 0.0f);
	const float Damping = std::max(Cfg.m_Damping, 0.0f);

	// 解析解闭式积分：阻尼谐振子。每步以 (当前值, 当前速度) 为初值在 [t, t+Dt] 上
	// 解析推进；线性常系数 ODE 下与全局闭式解完全等价，帧率无关、无条件稳定。
	const float Omega0 = std::sqrt(Stiffness / Mass);
	if(Omega0 <= 1e-6f)
	{
		// 无刚度退化：自由阻尼运动（指数衰减）或匀速直线运动。
		const float Lambda = Damping / Mass;
		if(Lambda <= 1e-9f)
		{
			Track.m_Current += Track.m_Velocity * Dt;
		}
		else
		{
			const float Decay = std::exp(-Lambda * Dt);
			Track.m_Current += Track.m_Velocity * (1.0f - Decay) / Lambda;
			Track.m_Velocity *= Decay;
		}
	}
	else
	{
		const float Zeta = Damping / (2.0f * Mass * Omega0);
		const float Displacement = Track.m_Current - Track.m_Target;
		const float Velocity = Track.m_Velocity;
		if(Zeta < 1.0f)
		{
			// 欠阻尼：x(t) = e^(-ζω0 t)·(A·cos(ωd·t) + B·sin(ωd·t))，A=d0，B=(v0+ζω0·d0)/ωd
			const float OmegaD = Omega0 * std::sqrt(std::max(0.0f, 1.0f - Zeta * Zeta));
			const float A = Displacement;
			const float B = (Velocity + Zeta * Omega0 * Displacement) / OmegaD;
			const float Decay = std::exp(-Zeta * Omega0 * Dt);
			const float CosDt = std::cos(OmegaD * Dt);
			const float SinDt = std::sin(OmegaD * Dt);
			Track.m_Current = Track.m_Target + Decay * (A * CosDt + B * SinDt);
			Track.m_Velocity = Decay * ((B * OmegaD - A * Zeta * Omega0) * CosDt - (A * OmegaD + B * Zeta * Omega0) * SinDt);
		}
		else if(Zeta > 1.0f)
		{
			// 过阻尼：x(t) = A·e^(s1·t) + B·e^(s2·t)，s1/s2 为两个负实根
			const float Root = Omega0 * std::sqrt(std::max(0.0f, Zeta * Zeta - 1.0f));
			const float S1 = -Zeta * Omega0 + Root;
			const float S2 = -Zeta * Omega0 - Root;
			const float A = (Velocity - S2 * Displacement) / (S1 - S2);
			const float B = (S1 * Displacement - Velocity) / (S1 - S2);
			const float Exp1 = std::exp(S1 * Dt);
			const float Exp2 = std::exp(S2 * Dt);
			Track.m_Current = Track.m_Target + A * Exp1 + B * Exp2;
			Track.m_Velocity = A * S1 * Exp1 + B * S2 * Exp2;
		}
		else
		{
			// 临界阻尼：x(t) = (d0 + (v0 + ω0·d0)·t)·e^(-ω0 t)
			const float A = Displacement;
			const float B = Velocity + Omega0 * Displacement;
			const float Decay = std::exp(-Omega0 * Dt);
			Track.m_Current = Track.m_Target + (A + B * Dt) * Decay;
			Track.m_Velocity = (Velocity - Omega0 * B * Dt) * Decay;
		}
	}

	const bool AtRest = std::abs(Track.m_Current - Track.m_Target) < Cfg.m_RestEpsilon && std::abs(Track.m_Velocity) < Cfg.m_RestVelocity;
	if(AtRest)
		Track.m_RestTimerSec += Dt;
	else
		Track.m_RestTimerSec = 0.0f;
}

void CUiV2AnimationRuntime::Advance(float Dt)
{
	if(Dt <= 0.0f)
		return;
	const float ClampedDt = std::min(Dt, 1.0f / 15.0f);
	m_TimeSec += ClampedDt;

	constexpr float KSpringRestHoldSec = 0.033f;

	std::deque<STrackKey> vCompleted;
	for(auto &Pair : m_ActiveTracks)
	{
		const STrackKey &Key = Pair.first;
		SActiveTrack &Track = Pair.second;
		Track.m_ElapsedSec += ClampedDt;

		if(Track.m_Transition.m_Driver == EUiAnimDriver::SPRING)
		{
			AdvanceSpring(Track, ClampedDt);
			if(Track.m_RestTimerSec >= KSpringRestHoldSec)
			{
				Track.m_Current = Track.m_Target;
				Track.m_Velocity = 0.0f;
				m_Values[Key] = Track.m_Current;
				vCompleted.push_back(Key);
			}
			else
			{
				m_Values[Key] = Track.m_Current;
			}
		}
		else
		{
			const float Previous = Track.m_Current;
			const float RawProgress = TrackProgress(Track);
			const float Progress = ApplyTrackEasing(RawProgress, Track);
			Track.m_Current = Track.m_Start + (Track.m_Target - Track.m_Start) * Progress;
			// 有限差分速度：供打断时做 tween→弹簧接管的初速度（速度继承）。
			if(ClampedDt > 0.0f)
				Track.m_Velocity = (Track.m_Current - Previous) / ClampedDt;
			m_Values[Key] = Track.m_Current;

			if(RawProgress >= 1.0f)
				vCompleted.push_back(Key);
		}
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
	return m_ActiveTracks.contains({NodeKey, Property});
}

void CUiV2AnimationRuntime::PruneResolveTargetCache(uint64_t CurrentUseCounter)
{
	if(m_LastTargets.empty())
		m_LastTargets.reserve(MAX_LAST_TARGETS_SOFT);

	if((CurrentUseCounter % 1024) != 0 || m_LastTargets.size() <= MAX_LAST_TARGETS_SOFT)
		return;

	for(auto It = m_LastTargets.begin(); It != m_LastTargets.end();)
	{
		if(CurrentUseCounter - It->second.m_LastUseCounter > MAX_LAST_TARGETS_HARD)
			It = m_LastTargets.erase(It);
		else
			++It;
	}
	if(m_LastTargets.size() > MAX_LAST_TARGETS_HARD)
		m_LastTargets.clear();
}

float CUiV2AnimationRuntime::ResolveTargetValue(uint64_t NodeKey, EUiAnimProperty Property, float Target, const SUiAnimTransition &Transition)
{
	constexpr float ANIM_EPSILON = 0.0001f;
	const STrackKey Key{NodeKey, Property};
	const float CurrentValue = GetValue(NodeKey, Property, Target);
	const uint64_t CurrentUseCounter = ++m_ResolveUseCounter;

	PruneResolveTargetCache(CurrentUseCounter);

	auto [ItLastTarget, Inserted] = m_LastTargets.try_emplace(Key, SResolveTargetState{Target, Transition.m_Driver, CurrentUseCounter});
	const bool HasLastTarget = !Inserted;
	const bool TargetChanged = !HasLastTarget || std::abs(Target - ItLastTarget->second.m_Target) > ANIM_EPSILON;
	const bool DriverChanged = !HasLastTarget || ItLastTarget->second.m_Driver != Transition.m_Driver;
	const bool NeedsSync = !HasActiveAnimation(NodeKey, Property) && std::abs(Target - CurrentValue) > ANIM_EPSILON;
	if(TargetChanged || DriverChanged || NeedsSync)
	{
		SUiAnimRequest Request;
		Request.m_NodeKey = NodeKey;
		Request.m_Property = Property;
		Request.m_Target = Target;
		Request.m_Transition = Transition;
		RequestAnimation(Request);
		ItLastTarget->second.m_Target = Target;
		ItLastTarget->second.m_Driver = Transition.m_Driver;
	}
	ItLastTarget->second.m_LastUseCounter = CurrentUseCounter;

	return GetValue(NodeKey, Property, Target);
}

void CUiV2AnimationRuntime::PruneColorTargetCache(uint64_t CurrentUseCounter)
{
	if(m_ColorTargets.empty())
		m_ColorTargets.reserve(4096);

	if((CurrentUseCounter % 1024) != 0 || m_ColorTargets.size() <= 4096)
		return;

	for(auto It = m_ColorTargets.begin(); It != m_ColorTargets.end();)
	{
		if(CurrentUseCounter - It->second.m_LastUseCounter > 8192)
			It = m_ColorTargets.erase(It);
		else
			++It;
	}
	if(m_ColorTargets.size() > 4096 * 2)
		m_ColorTargets.clear();
}

ColorRGBA CUiV2AnimationRuntime::ResolveColorFromValue(uint64_t NodeKey, const ColorRGBA &Current, const ColorRGBA &Target)
{
	const uint64_t CurrentUseCounter = ++m_ColorUseCounter;
	PruneColorTargetCache(CurrentUseCounter);

	auto [ItTarget, Inserted] = m_ColorTargets.try_emplace(NodeKey, SColorTargetState{Current, Target, CurrentUseCounter});
	if(Inserted || ColorChanged(ItTarget->second.m_Target, Target))
	{
		SetValue(NodeKey, EUiAnimProperty::COLOR_MIX, 0.0f);
		ItTarget->second.m_From = Current;
		ItTarget->second.m_Target = Target;
	}
	ItTarget->second.m_LastUseCounter = CurrentUseCounter;
	return ItTarget->second.m_From;
}

float CUiV2AnimationRuntime::ResolveColorMixValue(uint64_t NodeKey, const ColorRGBA &Target, float DurationSec, EEasing Easing)
{
	SUiAnimTransition Transition;
	Transition.m_DurationSec = DurationSec;
	Transition.m_DelaySec = 0.0f;
	Transition.m_Priority = 1;
	Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
	Transition.m_Easing = Easing;
	Transition.m_Driver = EUiAnimDriver::TWEEN;
	return ResolveTargetValue(NodeKey, EUiAnimProperty::COLOR_MIX, 1.0f, Transition);
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

void CUiV2AnimationRuntime::RegisterCustomEasing(uint32_t EasingId, FCustomEasing pfnEasing, void *pUser)
{
	if(EasingId == 0 || pfnEasing == nullptr)
		return;
	m_CustomEasings[EasingId] = SCustomEasing{pfnEasing, pUser};
}

void CUiV2AnimationRuntime::UnregisterCustomEasing(uint32_t EasingId)
{
	m_CustomEasings.erase(EasingId);
}

bool CUiV2AnimationRuntime::PollCompletedEvent(SUiAnimCompleteEvent &EventOut)
{
	if(m_CompletedEvents.empty())
		return false;
	EventOut = m_CompletedEvents.front();
	m_CompletedEvents.pop_front();
	return true;
}

uint32_t CUiV2AnimationRuntime::AwaitTracks(const uint32_t *pTrackIds, int NumTrackIds)
{
	if(pTrackIds == nullptr || NumTrackIds <= 0)
		return 0;

	const uint32_t GroupId = m_NextGroupId++;
	int Registered = 0;
	std::unordered_set<uint32_t> vSeenTrackIds;
	vSeenTrackIds.reserve(static_cast<size_t>(NumTrackIds));
	for(int i = 0; i < NumTrackIds; ++i)
	{
		const uint32_t TrackId = pTrackIds[i];
		if(TrackId == 0)
			continue;
		if(!vSeenTrackIds.insert(TrackId).second)
			continue;
		if(!IsTrackPending(TrackId))
			continue;
		m_TrackAwaitGroups[TrackId].push_back(GroupId);
		++Registered;
	}
	if(Registered <= 0)
		return 0;

	m_AwaitGroups[GroupId] = SAwaitGroup{Registered};
	return GroupId;
}

bool CUiV2AnimationRuntime::PollGroupCompletedEvent(SUiAnimGroupCompleteEvent &EventOut)
{
	if(m_GroupCompletedEvents.empty())
		return false;
	EventOut = m_GroupCompletedEvents.front();
	m_GroupCompletedEvents.pop_front();
	return true;
}

float CUiV2AnimationRuntime::TimeSec() const
{
	return m_TimeSec;
}
