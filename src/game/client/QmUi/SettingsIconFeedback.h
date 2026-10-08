#ifndef GAME_CLIENT_QMUI_SETTINGSICONFEEDBACK_H
#define GAME_CLIENT_QMUI_SETTINGSICONFEEDBACK_H

#include "QmAnimationBackend.h"

// 只在进入悬浮和按下的瞬间触发一次回弹；持续悬浮不会重启动画。
inline float ResolveSettingsIconScale(CQmAnimationBackend &Anim, uint64_t NodeKey, bool Hovered, bool Pressed, bool Enabled)
{
	const int PreviousState = static_cast<int>(Anim.GetValue(NodeKey, EUiAnimProperty::COLOR_MIX));
	const int State = Enabled ? (Pressed ? 2 : Hovered ? 1 : 0) : 0;
	Anim.SetValue(NodeKey, EUiAnimProperty::COLOR_MIX, static_cast<float>(State));
	if(!Enabled)
	{
		Anim.SetValue(NodeKey, EUiAnimProperty::SCALE, 1.0f);
		return 1.0f;
	}
	if(State > PreviousState)
	{
		Anim.SetValue(NodeKey, EUiAnimProperty::SCALE, Pressed ? 0.90f : 1.12f);
		SUiAnimRequest Request;
		Request.m_NodeKey = NodeKey;
		Request.m_Property = EUiAnimProperty::SCALE;
		Request.m_Target = 1.0f;
		Request.m_Transition.m_DurationSec = 0.18f;
		Request.m_Transition.m_Easing = EEasing::EASE_OUT_BACK;
		Anim.RequestAnimation(Request);
	}
	return Anim.GetValue(NodeKey, EUiAnimProperty::SCALE, 1.0f);
}

#endif
