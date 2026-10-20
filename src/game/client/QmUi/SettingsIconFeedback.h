#ifndef GAME_CLIENT_QMUI_SETTINGSICONFEEDBACK_H
#define GAME_CLIENT_QMUI_SETTINGSICONFEEDBACK_H

#include "QmAnimationBackend.h"
#include "UiTokens.h"

// 只在进入悬浮和按下的瞬间触发一次回弹；离开悬浮或松开按键时收回至原尺寸。
inline float ResolveSettingsIconScale(CQmAnimationBackend &Anim, uint64_t NodeKey, bool Hovered, bool Pressed, bool Enabled)
{
	const int PreviousState = static_cast<int>(Anim.GetValue(NodeKey, EUiAnimProperty::COLOR_MIX));
	const int State = Enabled ? (Pressed ? 2 : Hovered ? 1 :
							     0) :
				    0;
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
	else if(State < PreviousState)
	{
		if(PreviousState == 2 && State == 1)
		{
			// 指针在图标上松开：轻柔弹回原尺寸，承接按压释放。
			SUiAnimRequest Request;
			Request.m_NodeKey = NodeKey;
			Request.m_Property = EUiAnimProperty::SCALE;
			Request.m_Target = 1.0f;
			Request.m_Transition.m_Driver = EUiAnimDriver::SPRING;
			Request.m_Transition.m_Spring = ui_token::motion::TWITCH_SPRING;
			Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
			Anim.RequestAnimation(Request);
		}
		else if(Anim.HasActiveAnimation(NodeKey, EUiAnimProperty::SCALE) || std::abs(Anim.GetValue(NodeKey, EUiAnimProperty::SCALE, 1.0f) - 1.0f) > 0.001f)
		{
			// 鼠标快速划过并离开：以竞技快击速度收回，消除残余脉冲。
			SUiAnimRequest Request;
			Request.m_NodeKey = NodeKey;
			Request.m_Property = EUiAnimProperty::SCALE;
			Request.m_Target = 1.0f;
			Request.m_Transition = ui_token::motion::TWITCH_SNAP;
			Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
			Anim.RequestAnimation(Request);
		}
	}
	return Anim.GetValue(NodeKey, EUiAnimProperty::SCALE, 1.0f);
}

#endif
