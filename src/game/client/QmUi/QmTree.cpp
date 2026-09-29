// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmTree.h"

#include "QmAnim.h"

#include <algorithm>
#include <cmath>

void CUiV2Tree::Reset()
{
	m_FrameNodeKeys.clear();
	m_LayoutTransitions.clear();
	m_PresenceStates.clear();
	m_LastNodeCount = 0;
	m_LayoutUseCounter = 0;
	m_PresenceUseCounter = 0;
}

void CUiV2Tree::BeginFrame()
{
	m_FrameNodeKeys.clear();
}

void CUiV2Tree::TouchNode(uint64_t NodeKey)
{
	m_FrameNodeKeys.insert(NodeKey);
}

CUIRect CUiV2Tree::ResolveLayoutTransition(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, const CUIRect &Target, const SUiSpringConfig &Spring, int Priority, bool Animate)
{
	const uint64_t CurrentUseCounter = ++m_LayoutUseCounter;
	PruneLayoutTransitionCache(CurrentUseCounter);

	auto [It, Inserted] = m_LayoutTransitions.try_emplace(NodeKey, SLayoutTransitionState{Target, true, CurrentUseCounter});
	if(Inserted || !It->second.m_HasLastTarget || !Animate)
	{
		It->second.m_LastTarget = Target;
		It->second.m_HasLastTarget = true;
		It->second.m_LastUseCounter = CurrentUseCounter;
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::POS_X, Target.x);
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::POS_Y, Target.y);
		return Target;
	}

	constexpr float ANIM_EPSILON = 0.0001f;
	const bool Changed = std::abs(Target.x - It->second.m_LastTarget.x) > ANIM_EPSILON || std::abs(Target.y - It->second.m_LastTarget.y) > ANIM_EPSILON;
	const bool HasActivePositionAnim = AnimRuntime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_X) || AnimRuntime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_Y);
	const bool PositionNeedsSync = std::abs(AnimRuntime.GetValue(NodeKey, EUiAnimProperty::POS_X, Target.x) - Target.x) > ANIM_EPSILON || std::abs(AnimRuntime.GetValue(NodeKey, EUiAnimProperty::POS_Y, Target.y) - Target.y) > ANIM_EPSILON;
	CUIRect Out = Target;
	if(Changed || HasActivePositionAnim || PositionNeedsSync)
	{
		SUiAnimTransition Transition;
		Transition.m_Priority = Priority;
		Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
		Transition.m_Driver = EUiAnimDriver::SPRING;
		Transition.m_Spring = Spring;
		Out.x = AnimRuntime.ResolveTargetValue(NodeKey, EUiAnimProperty::POS_X, Target.x, Transition);
		Out.y = AnimRuntime.ResolveTargetValue(NodeKey, EUiAnimProperty::POS_Y, Target.y, Transition);
	}
	It->second.m_LastTarget = Target;
	It->second.m_HasLastTarget = true;
	It->second.m_LastUseCounter = CurrentUseCounter;
	return Out;
}

CUIRect CUiV2Tree::SyncLayoutTransition(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, const CUIRect &Target)
{
	const uint64_t CurrentUseCounter = ++m_LayoutUseCounter;
	PruneLayoutTransitionCache(CurrentUseCounter);

	SLayoutTransitionState &State = m_LayoutTransitions[NodeKey];
	State.m_LastTarget = Target;
	State.m_HasLastTarget = true;
	State.m_LastUseCounter = CurrentUseCounter;
	AnimRuntime.SetValue(NodeKey, EUiAnimProperty::POS_X, Target.x);
	AnimRuntime.SetValue(NodeKey, EUiAnimProperty::POS_Y, Target.y);
	return Target;
}

SUiPresenceResult CUiV2Tree::ResolvePresence(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, bool Visible, const SUiAnimTransition &Transition)
{
	const uint64_t CurrentUseCounter = ++m_PresenceUseCounter;
	if(!Visible)
	{
		auto It = m_PresenceStates.find(NodeKey);
		if(It == m_PresenceStates.end())
			return {};

		SPresenceState &State = It->second;
		State.m_LastUseCounter = CurrentUseCounter;
		if(State.m_Phase != EPresencePhase::EXITING)
		{
			State.m_Phase = EPresencePhase::EXITING;
			SUiAnimTransition ExitTransition = Transition;
			ExitTransition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
			ExitTransition.m_Priority = std::max(ExitTransition.m_Priority, 1000);
			AnimRuntime.ResolveTargetValue(NodeKey, EUiAnimProperty::ALPHA, 0.0f, ExitTransition);
		}

		const float Alpha = AnimRuntime.GetValue(NodeKey, EUiAnimProperty::ALPHA, 0.0f);
		const bool HasExitAnimation = AnimRuntime.HasActiveAnimation(NodeKey, EUiAnimProperty::ALPHA);
		if(!HasExitAnimation && Alpha <= 0.001f)
		{
			m_PresenceStates.erase(It);
			return {};
		}
		return {true, Alpha, false};
	}

	TouchNode(NodeKey);
	auto [It, Inserted] = m_PresenceStates.try_emplace(NodeKey, SPresenceState{EPresencePhase::ENTERING, true, CurrentUseCounter});
	SPresenceState &State = It->second;
	State.m_TouchedThisFrame = true;
	State.m_LastUseCounter = CurrentUseCounter;

	if(Inserted)
	{
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::ALPHA, 0.0f);
	}
	if(Inserted || State.m_Phase == EPresencePhase::EXITING)
	{
		State.m_Phase = EPresencePhase::ENTERING;
		SUiAnimTransition EnterTransition = Transition;
		EnterTransition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
		EnterTransition.m_Priority = std::max(EnterTransition.m_Priority, 1);
		AnimRuntime.ResolveTargetValue(NodeKey, EUiAnimProperty::ALPHA, 1.0f, EnterTransition);
	}
	else if(!AnimRuntime.HasActiveAnimation(NodeKey, EUiAnimProperty::ALPHA))
	{
		State.m_Phase = EPresencePhase::PRESENT;
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::ALPHA, 1.0f);
	}

	return {true, AnimRuntime.GetValue(NodeKey, EUiAnimProperty::ALPHA, 1.0f), Inserted};
}

float CUiV2Tree::ResolvePresenceAlpha(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, const SUiAnimTransition &Transition)
{
	return ResolvePresence(AnimRuntime, NodeKey, true, Transition).m_Alpha;
}

void CUiV2Tree::EndFrame()
{
	ResolveEndFrame(nullptr);
}

void CUiV2Tree::EndFrame(CUiV2AnimationRuntime &AnimRuntime)
{
	ResolveEndFrame(&AnimRuntime);
}

void CUiV2Tree::ResolveEndFrame(CUiV2AnimationRuntime *pAnimRuntime)
{
	m_LastNodeCount = static_cast<int>(m_FrameNodeKeys.size());
	if(pAnimRuntime == nullptr)
		return;

	SUiAnimTransition ExitTransition;
	ExitTransition.m_DurationSec = 0.16f;
	ExitTransition.m_Priority = 1000;
	ExitTransition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
	ExitTransition.m_Easing = EEasing::EASE_OUT;

	for(auto It = m_PresenceStates.begin(); It != m_PresenceStates.end();)
	{
		const uint64_t NodeKey = It->first;
		SPresenceState &State = It->second;
		if(State.m_TouchedThisFrame)
		{
			State.m_TouchedThisFrame = false;
			++It;
			continue;
		}

		if(State.m_Phase != EPresencePhase::EXITING)
		{
			State.m_Phase = EPresencePhase::EXITING;
			pAnimRuntime->ResolveTargetValue(NodeKey, EUiAnimProperty::ALPHA, 0.0f, ExitTransition);
			++It;
			continue;
		}

		const bool HasExitAnimation = pAnimRuntime->HasActiveAnimation(NodeKey, EUiAnimProperty::ALPHA);
		const float Alpha = pAnimRuntime->GetValue(NodeKey, EUiAnimProperty::ALPHA, 0.0f);
		if(!HasExitAnimation && Alpha <= 0.001f)
			It = m_PresenceStates.erase(It);
		else
			++It;
	}
	int ExitingNodeCount = 0;
	for(const auto &Pair : m_PresenceStates)
	{
		if(!m_FrameNodeKeys.contains(Pair.first))
			++ExitingNodeCount;
	}
	m_LastNodeCount += ExitingNodeCount;
}

void CUiV2Tree::PruneLayoutTransitionCache(uint64_t CurrentUseCounter)
{
	// 剪枝按调用次数节流（每 1024 次扫描一次），否则 counter>=4096 后每次
	// Resolve/Sync 都全表扫描，单帧 O(N²)（基准 BM_AnimLayoutCacheChurn 实证，
	// 4096 键下单次 resolve ~10µs）；节流方式对照运行时侧 PruneResolveTargetCache。
	// 未到首个 4096 计数前缓存不可能积累陈旧项，直接跳过。
	if(CurrentUseCounter < 4096 || (CurrentUseCounter % 1024) != 0)
		return;

	const uint64_t StaleThreshold = CurrentUseCounter - 4096;
	for(auto It = m_LayoutTransitions.begin(); It != m_LayoutTransitions.end();)
	{
		if(It->second.m_LastUseCounter < StaleThreshold)
			It = m_LayoutTransitions.erase(It);
		else
			++It;
	}
}

int CUiV2Tree::NodeCount() const
{
	return m_LastNodeCount;
}
