// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmAnimResolve.h"

#include <engine/shared/config.h>

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	uint64_t HashAnimNode(uint64_t Value)
	{
		// Mix bits to keep generated animation keys stable and well distributed.
		Value ^= Value >> 33;
		Value *= 0xff51afd7ed558ccdULL;
		Value ^= Value >> 33;
		Value *= 0xc4ceb9fe1a85ec53ULL;
		Value ^= Value >> 33;
		return Value;
	}

	float Clamp01(float Value)
	{
		return std::clamp(Value, 0.0f, 1.0f);
	}

	float SrgbToLinear(float Value)
	{
		Value = Clamp01(Value);
		if(Value <= 0.04045f)
			return Value / 12.92f;
		return std::pow((Value + 0.055f) / 1.055f, 2.4f);
	}

	float LinearToSrgb(float Value)
	{
		Value = Clamp01(Value);
		if(Value <= 0.0031308f)
			return Value * 12.92f;
		return 1.055f * std::pow(Value, 1.0f / 2.4f) - 0.055f;
	}

	struct SOklab
	{
		float m_L = 0.0f;
		float m_A = 0.0f;
		float m_B = 0.0f;
	};

	SOklab RgbToOklab(const ColorRGBA &Color)
	{
		const float R = SrgbToLinear(Color.r);
		const float G = SrgbToLinear(Color.g);
		const float B = SrgbToLinear(Color.b);

		const float L = std::cbrt(0.4122214708f * R + 0.5363325363f * G + 0.0514459929f * B);
		const float M = std::cbrt(0.2119034982f * R + 0.6806995451f * G + 0.1073969566f * B);
		const float S = std::cbrt(0.0883024619f * R + 0.2817188376f * G + 0.6299787005f * B);

		SOklab Out;
		Out.m_L = 0.2104542553f * L + 0.7936177850f * M - 0.0040720468f * S;
		Out.m_A = 1.9779984951f * L - 2.4285922050f * M + 0.4505937099f * S;
		Out.m_B = 0.0259040371f * L + 0.7827717662f * M - 0.8086757660f * S;
		return Out;
	}

	ColorRGBA OklabToRgb(const SOklab &Color, float Alpha)
	{
		const float L = Color.m_L + 0.3963377774f * Color.m_A + 0.2158037573f * Color.m_B;
		const float M = Color.m_L - 0.1055613458f * Color.m_A - 0.0638541728f * Color.m_B;
		const float S = Color.m_L - 0.0894841775f * Color.m_A - 1.2914855480f * Color.m_B;

		const float L3 = L * L * L;
		const float M3 = M * M * M;
		const float S3 = S * S * S;

		ColorRGBA Out;
		Out.r = LinearToSrgb(+4.0767416621f * L3 - 3.3077115913f * M3 + 0.2309699292f * S3);
		Out.g = LinearToSrgb(-1.2684380046f * L3 + 2.6097574011f * M3 - 0.3413193965f * S3);
		Out.b = LinearToSrgb(-0.0041960863f * L3 - 0.7034186147f * M3 + 1.7076147010f * S3);
		Out.a = Clamp01(Alpha);
		return Out;
	}

	uint64_t PropertyTargetCacheKey(uint64_t NodeKey, EUiAnimProperty Property)
	{
		return HashAnimNode(NodeKey ^ (0x9e3779b97f4a7c15ULL * (static_cast<uint64_t>(Property) + 1ULL)));
	}
} // namespace

uint64_t BuildUiAnimNodeKey(const uint64_t ScopeHash, const uint64_t Id)
{
	return HashAnimNode((ScopeHash << 32) ^ HashAnimNode(Id));
}

float ResolveUiAnimValue(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, EUiAnimProperty Property, float Target, float DurationSec, EEasing Easing)
{
	SUiAnimTransition Transition;
	Transition.m_DurationSec = DurationSec;
	Transition.m_DelaySec = 0.0f;
	Transition.m_Priority = 1;
	Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
	Transition.m_Easing = Easing;
	Transition.m_Driver = EUiAnimDriver::TWEEN;
	return AnimRuntime.ResolveTargetValue(NodeKey, Property, Target, Transition);
}

float ResolveUiAnimSpringValue(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, EUiAnimProperty Property, float Target, const SUiSpringConfig &Spring, int Priority)
{
	// 首次出现从控件当前目标开始，避免从屏幕原点飞入；关闭动效立即停止旧轨道。
	const float Current = AnimRuntime.GetValue(NodeKey, Property, std::numeric_limits<float>::quiet_NaN());
	if(std::isnan(Current))
		AnimRuntime.SetValue(NodeKey, Property, Target);
	if(g_Config.m_QmUiMotionLevel == 0)
	{
		if(Current != Target || AnimRuntime.HasActiveAnimation(NodeKey, Property))
			AnimRuntime.SetValue(NodeKey, Property, Target);
		return Target;
	}

	SUiAnimTransition Transition;
	Transition.m_Driver = EUiAnimDriver::SPRING;
	Transition.m_Spring = Spring;
	Transition.m_Priority = Priority;
	Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
	return AnimRuntime.ResolveTargetValue(NodeKey, Property, Target, Transition);
}

CUIRect ResolveUiAnimValueRect(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, const CUIRect &Target, float DurationSec, EEasing Easing)
{
	CUIRect Out;
	Out.x = ResolveUiAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::POS_X, Target.x, DurationSec, Easing);
	Out.y = ResolveUiAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::POS_Y, Target.y, DurationSec, Easing);
	Out.w = ResolveUiAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::WIDTH, Target.w, DurationSec, Easing);
	Out.h = ResolveUiAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::HEIGHT, Target.h, DurationSec, Easing);
	return Out;
}

CUIRect ResolveUiAnimSpringRectXY(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, const CUIRect &Target, const SUiSpringConfig &Spring, int Priority)
{
	CUIRect Out = Target;
	Out.x = ResolveUiAnimSpringValue(AnimRuntime, NodeKey, EUiAnimProperty::POS_X, Target.x, Spring, Priority);
	Out.y = ResolveUiAnimSpringValue(AnimRuntime, NodeKey, EUiAnimProperty::POS_Y, Target.y, Spring, Priority);
	return Out;
}

ColorRGBA ResolveUiAnimValueColor(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, const ColorRGBA &Target, float DurationSec, EEasing Easing)
{
	if(g_Config.m_QmUiColorInterpolation != 0)
	{
		// 后台不会回写 RGBA；换目标时从当前 mix 的可见颜色接续。
		const ColorRGBA Current = ReadUiAnimValueColor(AnimRuntime, NodeKey, Target);
		const ColorRGBA From = AnimRuntime.ResolveColorFromValue(NodeKey, Current, Target);
		const float Mix = AnimRuntime.ResolveColorMixValue(NodeKey, Target, DurationSec, Easing);
		// 关闭动效直接保留配置色，避免前后台分别取目标值与 Oklab 往返值。
		const ColorRGBA Out = g_Config.m_QmUiMotionLevel == 0 ? Target : ResolveUiAnimInterpolatedColor(From, Target, Mix);
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::COLOR_R, Out.r);
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::COLOR_G, Out.g);
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::COLOR_B, Out.b);
		AnimRuntime.SetValue(NodeKey, EUiAnimProperty::COLOR_A, Out.a);
		return Out;
	}

	ColorRGBA Out;
	Out.r = ResolveUiAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::COLOR_R, Target.r, DurationSec, Easing);
	Out.g = ResolveUiAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::COLOR_G, Target.g, DurationSec, Easing);
	Out.b = ResolveUiAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::COLOR_B, Target.b, DurationSec, Easing);
	Out.a = ResolveUiAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::COLOR_A, Target.a, DurationSec, Easing);
	return Out;
}

ColorRGBA ReadUiAnimValueColor(const CUiV2AnimationRuntime &AnimRuntime, const uint64_t NodeKey, const ColorRGBA &Target)
{
	if(g_Config.m_QmUiMotionLevel == 0)
		return Target;
	if(g_Config.m_QmUiColorInterpolation != 0)
	{
		ColorRGBA From;
		ColorRGBA StoredTarget;
		if(AnimRuntime.ReadColorTransition(NodeKey, From, StoredTarget))
			return ResolveUiAnimInterpolatedColor(From, StoredTarget, AnimRuntime.GetValue(NodeKey, EUiAnimProperty::COLOR_MIX, 1.0f));
	}
	return ColorRGBA(
		AnimRuntime.GetValue(NodeKey, EUiAnimProperty::COLOR_R, Target.r),
		AnimRuntime.GetValue(NodeKey, EUiAnimProperty::COLOR_G, Target.g),
		AnimRuntime.GetValue(NodeKey, EUiAnimProperty::COLOR_B, Target.b),
		AnimRuntime.GetValue(NodeKey, EUiAnimProperty::COLOR_A, Target.a));
}

ColorRGBA ResolveUiAnimInterpolatedColor(const ColorRGBA &From, const ColorRGBA &To, float Amount)
{
	Amount = Clamp01(Amount);
	const float Alpha = From.a + (To.a - From.a) * Amount;
	if(g_Config.m_QmUiColorInterpolation == 0)
	{
		ColorRGBA Out;
		Out.r = From.r + (To.r - From.r) * Amount;
		Out.g = From.g + (To.g - From.g) * Amount;
		Out.b = From.b + (To.b - From.b) * Amount;
		Out.a = Alpha;
		return Out;
	}

	const SOklab FromLab = RgbToOklab(From);
	const SOklab ToLab = RgbToOklab(To);
	SOklab Mixed;
	Mixed.m_L = FromLab.m_L + (ToLab.m_L - FromLab.m_L) * Amount;
	Mixed.m_A = FromLab.m_A + (ToLab.m_A - FromLab.m_A) * Amount;
	Mixed.m_B = FromLab.m_B + (ToLab.m_B - FromLab.m_B) * Amount;
	return OklabToRgb(Mixed, Alpha);
}

float ResolveUiPresentationStateValue(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, EUiAnimProperty Property, float Target, const SUiSpringConfig &Spring, int Priority, float Epsilon)
{
	struct SPresentationTargetState
	{
		float m_Target = 0.0f;
		uint64_t m_LastUseCounter = 0;
	};

	static std::unordered_map<uint64_t, SPresentationTargetState> s_aLastTargets;
	static uint64_t s_UseCounter = 0;
	constexpr size_t TARGET_CACHE_SOFT_LIMIT = 4096;
	constexpr uint64_t TARGET_CACHE_PRUNE_INTERVAL = 1024;
	constexpr uint64_t TARGET_CACHE_STALE_WINDOW = 8192;
	const uint64_t LastTargetKey = PropertyTargetCacheKey(NodeKey, Property);
	const float CurrentValue = AnimRuntime.GetValue(NodeKey, Property, Target);
	const uint64_t CurrentUseCounter = ++s_UseCounter;

	if(s_aLastTargets.empty())
		s_aLastTargets.reserve(TARGET_CACHE_SOFT_LIMIT);
	if((CurrentUseCounter % TARGET_CACHE_PRUNE_INTERVAL) == 0 && s_aLastTargets.size() > TARGET_CACHE_SOFT_LIMIT)
	{
		for(auto It = s_aLastTargets.begin(); It != s_aLastTargets.end();)
		{
			if(CurrentUseCounter - It->second.m_LastUseCounter > TARGET_CACHE_STALE_WINDOW)
				It = s_aLastTargets.erase(It);
			else
				++It;
		}
		if(s_aLastTargets.size() > TARGET_CACHE_SOFT_LIMIT * 2)
			s_aLastTargets.clear();
	}

	auto [ItLastTarget, Inserted] = s_aLastTargets.try_emplace(LastTargetKey, SPresentationTargetState{Target, CurrentUseCounter});
	const float LastTarget = ItLastTarget->second.m_Target;
	const float Threshold = std::max(Epsilon, 0.0001f);
	const bool TargetChanged = Inserted || std::abs(Target - LastTarget) > Threshold;
	const bool NeedsSync = !AnimRuntime.HasActiveAnimation(NodeKey, Property) && std::abs(Target - CurrentValue) > Threshold;
	if(TargetChanged || NeedsSync)
	{
		SUiAnimRequest Request;
		Request.m_NodeKey = NodeKey;
		Request.m_Property = Property;
		Request.m_Target = Target;
		Request.m_Transition.m_Priority = Priority;
		Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
		Request.m_Transition.m_Driver = EUiAnimDriver::SPRING;
		Request.m_Transition.m_Spring = Spring;
		AnimRuntime.RequestAnimation(Request);
		ItLastTarget->second.m_Target = Target;
	}
	ItLastTarget->second.m_LastUseCounter = CurrentUseCounter;
	return AnimRuntime.GetValue(NodeKey, Property, Target);
}

void SetUiPresentationStateValue(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, EUiAnimProperty Property, float Value)
{
	AnimRuntime.SetValue(NodeKey, Property, Value);
}
