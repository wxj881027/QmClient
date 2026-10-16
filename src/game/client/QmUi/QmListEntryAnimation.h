#ifndef GAME_CLIENT_QMUI_QMLISTENTRYANIMATION_H
#define GAME_CLIENT_QMUI_QMLISTENTRYANIMATION_H

#include <game/client/QmUi/UiTokens.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class EQmListEntryRenderPass
{
	INTERACTIVE,
	BACKGROUND,
	MEASUREMENT,
};

constexpr bool QmListBoxShouldStartEntryAnimation(bool Enabled, bool MeasurementOnly, int64_t LastRenderTime, int64_t Now, int64_t InactiveGap)
{
	return Enabled && !MeasurementOnly && (LastRenderTime == 0 || Now - LastRenderTime > InactiveGap);
}

constexpr bool QmListBoxEntryAnimationFinished(bool Enabled, bool MeasurementOnly, float ElapsedSeconds, float DurationSeconds)
{
	return !Enabled || (!MeasurementOnly && (DurationSeconds <= 0.0f || ElapsedSeconds >= DurationSeconds));
}

// 依次淡入 + 轻微上浮归位（onetake wordRise 变体）。淡入与上浮同长——
// alpha 全程罩住位移，尾巴不再在满透明度下滑动（实机反馈"尾部生硬"）；
// 无裁剪遮罩：行底部被自身矩形切出的硬边正是"跳出来"感的来源。
// 行间延迟线性铺满可视范围，cascading 一直延伸到底部；整列共享生命周期。
// Full（motion 2）：行间 14ms（28 行封顶）、淡入 0.20s、上浮 0.20s / 8px；
// Reduced（motion 1）：无错开，整体 0.10s 内淡入 + 4px。
inline float QmCriticallyDampedRise(float ElapsedSeconds, float DurationSeconds, float Distance)
{
	if(ElapsedSeconds <= 0.0f)
		return Distance;
	if(ElapsedSeconds >= DurationSeconds)
		return 0.0f;
	const float X = ElapsedSeconds / (DurationSeconds / 6.64f);
	return Distance * (1.0f + X) * std::exp(-X);
}

// 行延迟线性铺满可视范围：可视列表通常 ≤25 行，逐行 14ms 的 cascading
// 一直延伸到底部；渐近式压缩会把深处的行挤成同帧整块出现，读作"跳出来"。
// 28 行封顶只为防御超长发榜场景。RowIndex 必须是相对本帧首个发射行的序号。
// 步长/封顶收口 ui_token::motion::LIST_ENTRY_STAGGER_*。
constexpr float QmListEntryRowDelaySec(int RowIndex, int MotionLevel)
{
	if(MotionLevel <= 1)
		return 0.0f;
	return std::min(RowIndex, ui_token::motion::LIST_ENTRY_STAGGER_MAX_ROWS) * ui_token::motion::LIST_ENTRY_STAGGER_STEP_SEC;
}

struct SQmListEntryVisual
{
	float m_Fade;
	float m_OffsetY;
};

// 时长/距离全部读 ui_token::motion::LIST_ENTRY_*；曲线保持无状态闭式解
// （smoothstep + 临界阻尼），虚拟化交错行不落每行引擎轨道（见 token 注释）。
inline SQmListEntryVisual QmListEntryVisual(float ElapsedSeconds, int RowIndex, int MotionLevel)
{
	if(MotionLevel <= 0 || ElapsedSeconds < 0.0f)
		return {1.0f, 0.0f};
	if(MotionLevel == 1)
	{
		const float Fade = std::clamp(ElapsedSeconds / ui_token::motion::LIST_ENTRY_FADE_REDUCED.m_DurationSec, 0.0f, 1.0f);
		return {Fade, QmCriticallyDampedRise(ElapsedSeconds, ui_token::motion::LIST_ENTRY_RISE_SEC_REDUCED, ui_token::motion::LIST_ENTRY_RISE_DISTANCE_REDUCED)};
	}
	const float Delay = QmListEntryRowDelaySec(RowIndex, MotionLevel);
	const float Local = ElapsedSeconds - Delay;
	// 淡入与上浮同长：smoothstep 收尾斜率为零，与临界阻尼的指数收尾互相掩护。
	const float Fade = std::clamp(Local / ui_token::motion::LIST_ENTRY_FADE.m_DurationSec, 0.0f, 1.0f);
	const float EasedFade = Fade * Fade * (3.0f - 2.0f * Fade);
	return {EasedFade, QmCriticallyDampedRise(Local, ui_token::motion::LIST_ENTRY_RISE_SEC, ui_token::motion::LIST_ENTRY_RISE_DISTANCE)};
}

inline constexpr float QmListEntryVisualTotalSeconds(int MotionLevel)
{
	return MotionLevel == 1 ? ui_token::motion::LIST_ENTRY_FADE_REDUCED.m_DurationSec : ui_token::motion::LIST_ENTRY_STAGGER_MAX_ROWS * ui_token::motion::LIST_ENTRY_STAGGER_STEP_SEC + ui_token::motion::LIST_ENTRY_RISE_SEC;
}

// 可见列表共用同一入场生命周期；测量只读取布局，不延长可见时间。
class CQmListEntryAnimation
{
	int64_t m_LastRenderTime = 0;
	int64_t m_StartTime = 0;

public:
	void Reset()
	{
		m_LastRenderTime = 0;
		m_StartTime = 0;
	}

	// 返回本次入场动画已流逝秒数；未在动画中返回 -1。行级浮现与裁剪由调用方
	// 按 QmListEntryVisual 自行计算，本类只承载生命周期。
	float Resolve(int64_t Now, int64_t Frequency, bool Enabled, int MotionLevel, EQmListEntryRenderPass Pass)
	{
		Frequency = std::max<int64_t>(1, Frequency);
		Enabled = Enabled && MotionLevel > 0;
		const bool MeasurementOnly = Pass == EQmListEntryRenderPass::MEASUREMENT;
		if(!Enabled && !MeasurementOnly)
			m_StartTime = 0;
		else if(QmListBoxShouldStartEntryAnimation(Enabled, MeasurementOnly, m_LastRenderTime, Now, Frequency * 2 / 5))
			m_StartTime = Now;
		float Elapsed = -1.0f;
		if(!MeasurementOnly && Enabled && m_StartTime > 0)
		{
			Elapsed = static_cast<float>(Now - m_StartTime) / static_cast<float>(Frequency);
			if(QmListBoxEntryAnimationFinished(Enabled, MeasurementOnly, Elapsed, QmListEntryVisualTotalSeconds(MotionLevel)))
			{
				m_StartTime = 0;
				Elapsed = -1.0f;
			}
		}
		if(!MeasurementOnly)
			m_LastRenderTime = Now;
		return Elapsed;
	}
};

#endif
