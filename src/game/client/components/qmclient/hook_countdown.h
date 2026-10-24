#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_HOOK_COUNTDOWN_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_HOOK_COUNTDOWN_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/client/QmUi/QmNeonRainbow.h>

#include <algorithm>
#include <array>
#include <cmath>

// 彩虹沿无限符号流动一圈和光晕呼吸一轮的秒数，与游戏钩子计数无关。
// 节奏对齐好友跟随光环（3.2 秒呼吸一轮）：两个指示器同时出现时鼓点一致。
constexpr float QM_HOOK_COUNTDOWN_FLOW_PERIOD_SECONDS = 6.0f;
constexpr float QM_HOOK_COUNTDOWN_BREATH_PERIOD_SECONDS = 3.2f;
// 相位每前进一圈时高光波扫过的圈数。必须是整数，相位回到 0 时光波也正好回到原位，
// 否则每绕完一圈色相都会看到一次高光"回弹"。
constexpr float QM_HOOK_COUNTDOWN_SHEEN_RATE = 3.0f;

// 沿路径移动的亮度波（0..1）：两轮高光绕一圈，方向与色相流动相反。
// 色相在动、高光也在动，且两者不同步，看到的是流光而不是整圈同时换色。
inline float QmHookCountdownSheen(float Phase, float PathPosition)
{
	return 0.5f + 0.5f * std::sin(2.0f * pi * (PathPosition * 2.0f + Phase * QM_HOOK_COUNTDOWN_SHEEN_RATE));
}

// 彩虹流光色：色相按沿曲线的距离循环并由相位推动，亮度随高光波起伏。
// 调色板与好友跟随光环共用 QmNeonRainbowColor，两处彩虹才一致。
inline ColorRGBA QmHookCountdownFlowColor(float Phase, float PathPosition)
{
	return QmNeonRainbowColor(PathPosition - Phase, QmHookCountdownSheen(Phase, PathPosition), 1.0f);
}

inline float QmHookCountdownBreath(float Phase)
{
	return 0.5f - 0.5f * std::cos(Phase * 2.0f * pi);
}

// ∞ 曲线（双纽线）的包围尺寸与采样点数：渲染、光晕和 HUD 裁剪半径都从这些值推出来。
constexpr float QM_HOOK_COUNTDOWN_INFINITY_HALF_WIDTH = 10.5f;
constexpr float QM_HOOK_COUNTDOWN_INFINITY_HALF_HEIGHT = 5.0f;
constexpr size_t QM_HOOK_COUNTDOWN_INFINITY_POINTS = 145;

struct SQmHookCountdownInfinityPoint
{
	vec2 m_Position;
	vec2 m_Normal;
	float m_PathPosition = 0.0f;
};

// 只在首次使用时计算曲线和法线；每帧复用几何，按弧长推进颜色，交叉处也不中断。
inline const std::array<SQmHookCountdownInfinityPoint, QM_HOOK_COUNTDOWN_INFINITY_POINTS> &QmHookCountdownInfinityPath()
{
	static const auto s_aPath = [] {
		std::array<SQmHookCountdownInfinityPoint, QM_HOOK_COUNTDOWN_INFINITY_POINTS> aPath{};
		float TotalLength = 0.0f;
		for(size_t i = 0; i < aPath.size(); ++i)
		{
			const float Angle = static_cast<float>(i) / static_cast<float>(aPath.size() - 1) * 2.0f * pi;
			aPath[i].m_Position = vec2(QM_HOOK_COUNTDOWN_INFINITY_HALF_WIDTH * std::cos(Angle), QM_HOOK_COUNTDOWN_INFINITY_HALF_HEIGHT * std::sin(Angle * 2.0f));
			const vec2 Tangent(-QM_HOOK_COUNTDOWN_INFINITY_HALF_WIDTH * std::sin(Angle), 2.0f * QM_HOOK_COUNTDOWN_INFINITY_HALF_HEIGHT * std::cos(Angle * 2.0f));
			aPath[i].m_Normal = normalize(vec2(-Tangent.y, Tangent.x));
			if(i != 0)
				TotalLength += distance(aPath[i - 1].m_Position, aPath[i].m_Position);
			aPath[i].m_PathPosition = TotalLength;
		}
		for(auto &Point : aPath)
			Point.m_PathPosition /= TotalLength;
		aPath.back() = aPath.front();
		aPath.back().m_PathPosition = 1.0f;
		return aPath;
	}();
	return s_aPath;
}

inline float QmHookCountdownProgress(float HookTick, float HookDurationSeconds, bool EndlessHook)
{
	if(EndlessHook)
		return 1.0f;
	// 与钩子核心使用同一个计数窗口；hook_duration 改变出钩初值，不是收回动画时长。
	constexpr float MaxHookTicks = SERVER_TICK_SPEED + SERVER_TICK_SPEED / 5;
	const int StartTick = static_cast<int>(SERVER_TICK_SPEED * (1.25f - HookDurationSeconds));
	const float DurationTicks = std::max(MaxHookTicks - StartTick, 1.0f);
	return std::clamp((MaxHookTicks - HookTick) / DurationTicks, 0.0f, 1.0f);
}

inline float QmHookCountdownInterpolatedTick(int PreviousTick, int CurrentTick, float Intra, bool SameGrab)
{
	// 重钩或换目标时不能把上一钩的计数插进这一钩。
	if(!SameGrab || CurrentTick < PreviousTick)
		return static_cast<float>(CurrentTick);
	return mix(static_cast<float>(PreviousTick), static_cast<float>(CurrentTick), std::clamp(Intra, 0.0f, 1.0f));
}

// 非无限钩的环色分三段：剩余 45% 以上是普通色，20%~45% 过渡到警告色，最后 20%
// 过渡到紧急色。三段颜色各自可配，默认就是原先固定的蓝/琥珀/红。
inline ColorRGBA QmHookCountdownColor(float Progress)
{
	const ColorRGBA Normal = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmHookCountdownColor));
	const ColorRGBA Warning = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmHookCountdownWarningColor));
	const ColorRGBA Urgent = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmHookCountdownUrgentColor));
	const auto MixColor = [](ColorRGBA From, ColorRGBA To, float Amount) {
		return ColorRGBA(mix(From.r, To.r, Amount), mix(From.g, To.g, Amount), mix(From.b, To.b, Amount), 1.0f);
	};
	if(Progress >= 0.45f)
		return Normal;
	if(Progress >= 0.20f)
		return MixColor(Normal, Warning, (0.45f - Progress) / 0.25f);
	return MixColor(Warning, Urgent, std::clamp((0.20f - Progress) / 0.20f, 0.0f, 1.0f));
}

struct SQmHookCountdownInput
{
	int m_ClientId = -1;
	int m_Connection = 0;
	int m_HookedPlayer = -1;
	bool m_HookAttached = false;
	bool m_EndlessHook = false;
	float m_HookTick = 0.0f;
	float m_HookDurationSeconds = 1.25f;
	vec2 m_SourcePosition{};
	vec2 m_TargetPosition{};
};

struct SQmHookCountdownVisual
{
	vec2 m_Position{};
	float m_Progress = 0.0f;
	float m_Alpha = 0.0f;
	float m_Scale = 0.72f;
	ColorRGBA m_Color{0.20f, 0.65f, 1.0f, 1.0f};
	// 无限钩的彩虹流动与呼吸相位（0..1 循环）；普通钩子仍使用倒计时圆环。
	bool m_EndlessHook = false;
	float m_FlowPhase = 0.0f;
	float m_BreathPhase = 0.0f;
};

class CQmHookCountdown
{
	enum class EPhase
	{
		HIDDEN,
		ENTERING,
		ACTIVE,
		EXITING,
	};

	SQmHookCountdownVisual m_Visual;
	EPhase m_Phase = EPhase::HIDDEN;
	int m_ClientId = -1;
	int m_Connection = -1;
	int m_HookedPlayer = -1;
	float m_TransitionTime = 0.0f;
	float m_FromAlpha = 0.0f;
	float m_FromScale = 0.72f;
	bool m_Reentering = false;

	static float EaseOutCubic(float Progress)
	{
		const float Remaining = 1.0f - Progress;
		return 1.0f - Remaining * Remaining * Remaining;
	}

public:
	void Reset() { *this = CQmHookCountdown{}; }
	const SQmHookCountdownVisual &Visual() const { return m_Visual; }

	void Update(const SQmHookCountdownInput &Input, float Delta)
	{
		// 无效帧时长不能污染动画时间与循环相位。
		Delta = std::isfinite(Delta) && Delta > 0.0f ? Delta : 0.0f;
		if(Input.m_ClientId < 0 || Input.m_Connection < 0)
		{
			Reset();
			return;
		}
		if(Input.m_ClientId != m_ClientId || Input.m_Connection != m_Connection)
		{
			Reset();
			m_ClientId = Input.m_ClientId;
			m_Connection = Input.m_Connection;
		}

		const bool Tracking = m_Phase == EPhase::ENTERING || m_Phase == EPhase::ACTIVE;
		if(Input.m_HookAttached && Input.m_HookedPlayer >= 0)
		{
			if(!Tracking || Input.m_HookedPlayer != m_HookedPlayer)
			{
				m_Reentering = m_Visual.m_Alpha > 0.0f;
				// 首帧就能看见短钩；重钩则从退场中的实际透明度、尺寸接回。
				m_FromAlpha = m_Reentering ? m_Visual.m_Alpha : 0.35f;
				m_FromScale = m_Visual.m_Scale;
				m_TransitionTime = 0.0f;
				m_Phase = EPhase::ENTERING;
			}
			m_HookedPlayer = Input.m_HookedPlayer;
			m_Visual.m_Position = mix(Input.m_SourcePosition, Input.m_TargetPosition, 0.5f);
			m_Visual.m_Progress = QmHookCountdownProgress(Input.m_HookTick, Input.m_HookDurationSeconds, Input.m_EndlessHook);
			m_Visual.m_Color = QmHookCountdownColor(m_Visual.m_Progress);
			m_Visual.m_EndlessHook = Input.m_EndlessHook;
		}
		else if(Tracking)
		{
			// 松钩保留最后的弧长、颜色和位置，只收拢这一枚环。
			m_FromAlpha = m_Visual.m_Alpha;
			m_FromScale = m_Visual.m_Scale;
			m_TransitionTime = 0.0f;
			m_Phase = EPhase::EXITING;
		}

		m_TransitionTime = std::min(m_TransitionTime + std::max(Delta, 0.0f), 0.18f);
		if(m_Phase == EPhase::ENTERING)
		{
			const float Duration = m_Reentering ? 0.07f : 0.14f;
			const float Progress = std::clamp(m_TransitionTime / Duration, 0.0f, 1.0f);
			const float Back = Progress - 1.0f;
			const float ScaleEase = m_Reentering ? EaseOutCubic(Progress) : 1.0f + 2.70158f * Back * Back * Back + 1.70158f * Back * Back;
			m_Visual.m_Scale = mix(m_FromScale, 1.0f, ScaleEase);
			m_Visual.m_Alpha = mix(m_FromAlpha, 1.0f, EaseOutCubic(std::clamp(m_TransitionTime / 0.06f, 0.0f, 1.0f)));
			if(Progress >= 1.0f)
			{
				m_Phase = EPhase::ACTIVE;
				m_Visual.m_Scale = 1.0f;
				m_Visual.m_Alpha = 1.0f;
			}
		}
		else if(m_Phase == EPhase::EXITING)
		{
			const float Progress = std::clamp(m_TransitionTime / 0.18f, 0.0f, 1.0f);
			m_Visual.m_Scale = mix(m_FromScale, 0.70f, Progress * Progress);
			m_Visual.m_Alpha = m_FromAlpha * (1.0f - Progress * Progress);
			if(Progress >= 1.0f)
				Reset();
		}

		// 松钩收拢期间仍保持彩虹流动与呼吸，完整退场或切换连接后随 Visual 清零。
		if(m_Visual.m_EndlessHook && (m_Phase == EPhase::ENTERING || m_Phase == EPhase::ACTIVE || m_Phase == EPhase::EXITING))
		{
			m_Visual.m_FlowPhase = std::fmod(m_Visual.m_FlowPhase + std::max(Delta, 0.0f) / QM_HOOK_COUNTDOWN_FLOW_PERIOD_SECONDS, 1.0f);
			m_Visual.m_BreathPhase = std::fmod(m_Visual.m_BreathPhase + std::max(Delta, 0.0f) / QM_HOOK_COUNTDOWN_BREATH_PERIOD_SECONDS, 1.0f);
		}
	}
};

#endif
