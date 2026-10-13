/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UIMOTION_H
#define GAME_CLIENT_QMUI_UIMOTION_H

#include "QmAnimResolve.h"
#include "UiContext.h"
#include "UiTokens.h"

#include <engine/shared/config.h>
#include <game/client/ui.h>

#include <algorithm>

// 页面背景绘制沿用当前进度；测量与关闭动效不提交入场位移。
inline float ReadUiSwitchAnimationProgress(const IUiContext &Ctx, uint64_t NodeKey)
{
	if(Ctx.m_pAnim == nullptr || Ctx.m_pUi == nullptr || Ctx.m_pUi->MeasurementOnly())
		return 0.0f;
	if(g_Config.m_QmUiMotionLevel == 0)
	{
		// 前台结束旧轨道，重新启用动效时不重播已经归位的页面。
		if(!Ctx.m_pUi->RenderOnly())
			Ctx.m_pAnim->SetValue(NodeKey, EUiAnimProperty::POS_X, 0.0f);
		return 0.0f;
	}
	return std::clamp(Ctx.m_pAnim->GetValue(NodeKey, EUiAnimProperty::POS_X, 0.0f), 0.0f, 1.0f);
}

// 进行中的页面继续沿原侧归位，避免反向点击把可见内容瞬间翻到另一侧。
inline float ResolveUiSwitchDirection(const CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, float CurrentDirection, float RequestedDirection)
{
	return CurrentDirection != 0.0f && AnimRuntime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_X) ? CurrentDirection : RequestedDirection;
}

// 连续切页只续接当前进度；结束后的下一次切换才重新入场。
inline void BeginUiSwitchAnimation(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, float DurationSec)
{
	if(!AnimRuntime.HasActiveAnimation(NodeKey, EUiAnimProperty::POS_X))
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::POS_X, 1.0f);

	SUiAnimRequest Request;
	Request.m_NodeKey = NodeKey;
	Request.m_Property = EUiAnimProperty::POS_X;
	Request.m_Target = 0.0f;
	Request.m_Transition = ui_token::motion::PAGE_SLIDE;
	Request.m_Transition.m_DurationSec = DurationSec;
	Request.m_Transition.m_Priority = 1;
	Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
	AnimRuntime.RequestAnimation(Request);
}

namespace ui_widget
{

	// 通用 widget 状态值补间：保留传入 transition 的完整运行时语义。
	inline float AnimateStateValue(const IUiContext &Ctx, const void *pId, EUiAnimProperty Property, float Target, const SUiAnimTransition &Transition)
	{
		const uint64_t NodeKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash, reinterpret_cast<uint64_t>(pId));
		if(Ctx.m_pAnim == nullptr)
			return Target;
		return Ctx.m_pAnim->ResolveTargetValue(NodeKey, Property, Target, Transition);
	}

} // namespace ui_widget

#endif
