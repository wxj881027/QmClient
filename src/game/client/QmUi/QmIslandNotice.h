// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMISLANDNOTICE_H
#define GAME_CLIENT_QMUI_QMISLANDNOTICE_H

#include "QmIslandSurface.h"

#include <base/color.h>
#include <base/types.h>

#include <game/client/components/hud_media_island_logic.h>

#include <algorithm>

// 灵动岛式通知：一颗黑球从屏幕顶部滑下 → 液体形变成胶囊 → 外轮廓环绕一圈倒计时
// → 时间到先收缩回黑球、再上滑离场。
//
// 入场动画直接复用 HUD 动态岛那套（QmHudMediaIslandEntrancePose +
// Drop/Expand 双弹簧），所以观感与 HUD 动态岛同源：掉落≈0.18s 临界阻尼，
// 展开≈0.66s 轻微欠阻尼；出场沿用同两条弹簧，只是目标值顺序反过来。
//
// 生命周期状态由调用方持有（qm_island::SNoticeState），因为「何时出现」属于业务，
// 而「怎么画」属于表现，两者分开后同一个组件能同时服务赞助提醒与关闭挽留提示。
namespace qm_island
{
	// 弹簧静止判据：与 QmHudMediaIslandResolveEntranceSprings 的角度量共用同一量级。
	inline constexpr float NOTICE_SETTLED_EPSILON = 0.001f;

	// ==== 生命周期 ====
	// 两段式：入场「掉落 → 展开」，出场「收成球 → 上滑」。
	// 两条通道各自一条弹簧，调用方每帧把进度写回这里，下一帧据此做阶段门控：
	// 出场一定是「先收成球再往上滑」，而不是一边缩一边飞。
	struct SNoticeState
	{
		// 完整展开后的停留时长（秒），也就是倒计时总长。
		float m_DurationSeconds = 5.0f;
		// 完整展开后已停留时长（秒）。
		float m_ElapsedSeconds = 0.0f;
		// 上一帧掉落通道进度：0 = 黑球在屏幕上方之外，1 = 黑球落定。
		float m_DropProgress = 0.0f;
		// 上一帧展开通道进度：0 = 黑球，1 = 完整展开的胶囊。
		float m_ExpandProgress = 0.0f;
	};

	// 两条弹簧通道的目标值。
	struct SNoticeTargets
	{
		float m_Drop = 0.0f;
		// 是否应该展开；真正开始还要等掉落落定（见 ResolveSprings）。
		bool m_Expand = false;
	};

	// 阶段门控（纯逻辑，可单测）：
	// - 显示：掉落目标立刻为 1；展开目标置位（落定后再真正开始）。
	// - 收起：展开目标立刻归零（先收成球）；掉落目标等收拢完成后才归零（再上滑）。
	inline SNoticeTargets ResolveTargets(const SNoticeState &State, bool Visible, float SettledEpsilon = NOTICE_SETTLED_EPSILON)
	{
		SNoticeTargets Targets;
		Targets.m_Drop = !Visible && State.m_ExpandProgress <= SettledEpsilon ? 0.0f : 1.0f;
		Targets.m_Expand = Visible;
		return Targets;
	}

	// 是否还要继续绘制：业务上要显示，或者出场动画还没收完。
	inline bool NeedsRender(const SNoticeState &State, bool Visible, float SettledEpsilon = NOTICE_SETTLED_EPSILON)
	{
		return Visible || State.m_DropProgress > SettledEpsilon || State.m_ExpandProgress > SettledEpsilon;
	}

	// 复位成「从未出现」；只在业务与动画都结束后调用。
	inline void Reset(SNoticeState &State)
	{
		State.m_ElapsedSeconds = 0.0f;
		State.m_DropProgress = 0.0f;
		State.m_ExpandProgress = 0.0f;
	}

	// 倒计时只在完整展开后推进（形变阶段不吃时间）。返回 true 表示本轮到时。
	inline bool AdvanceCountdown(SNoticeState &State, bool Visible, float DeltaSeconds, float SettledEpsilon = NOTICE_SETTLED_EPSILON)
	{
		if(!Visible || State.m_ExpandProgress < 1.0f - SettledEpsilon)
			return false;
		State.m_ElapsedSeconds += DeltaSeconds;
		return State.m_ElapsedSeconds >= State.m_DurationSeconds;
	}

	// 剩余比例 1 → 0，用于环绕倒计时条。
	inline float RemainingFraction(const SNoticeState &State)
	{
		if(State.m_DurationSeconds <= 0.0f)
			return 0.0f;
		return std::clamp(1.0f - State.m_ElapsedSeconds / State.m_DurationSeconds, 0.0f, 1.0f);
	}

	// 把两通道目标交给 HUD 动态岛同一套入场弹簧，返回本帧进度。
	// 展开通道以「本帧掉落进度」为门：落定后才展开，所以形变不会和掉落叠在一起。
	inline SHudMediaIslandEntranceSpringResult ResolveSprings(
		CUiV2AnimationRuntime &AnimRuntime,
		uint64_t DropNode,
		uint64_t ExpandNode,
		const SNoticeState &State,
		bool Visible,
		float SettledEpsilon = NOTICE_SETTLED_EPSILON)
	{
		const SNoticeTargets Targets = ResolveTargets(State, Visible, SettledEpsilon);
		const float DropProgress = ResolveUiPresentationStateValue(AnimRuntime, DropNode, EUiAnimProperty::ALPHA, Targets.m_Drop, QmHudMediaIslandEntranceDropSpring(), 3, SettledEpsilon);
		const float ExpandTarget = Targets.m_Expand && DropProgress >= 1.0f - SettledEpsilon ? 1.0f : 0.0f;
		const float ExpandProgress = ResolveUiPresentationStateValue(AnimRuntime, ExpandNode, EUiAnimProperty::ALPHA, ExpandTarget, QmHudMediaIslandEntranceExpandSpring(), 3, SettledEpsilon);
		return {DropProgress, ExpandProgress};
	}

	// 环绕倒计时条配色：绿 → 红随剩余时间插值。
	// 不复用 HUD 的 MediaIslandCountdownColor（那套按倒计时类型固定取色），
	// 因为这里要的是「时间压力」的连续表达。
	inline ColorRGBA CountdownRingColor(float Remaining)
	{
		const ColorRGBA Green(0.35f, 0.86f, 0.42f, 1.0f);
		const ColorRGBA Red(0.93f, 0.30f, 0.26f, 1.0f);
		const float T = 1.0f - std::clamp(Remaining, 0.0f, 1.0f);
		return ColorRGBA(
			Green.r + (Red.r - Green.r) * T,
			Green.g + (Red.g - Green.g) * T,
			Green.b + (Red.b - Green.b) * T,
			1.0f);
	}

	struct SNoticeLayout
	{
		// 胶囊主体矩形（不含轮廓环）。
		CUIRect m_Body{};
		// 轮廓环厚度。
		float m_RingThickness = 2.0f;
		// 轮廓环中心线相对主体外沿外扩的距离；环整条都在主体外侧。
		float m_RingOffset = 0.0f;
	};

	// 顶部居中布局：与 HUD 动态岛同在屏幕顶部中间，让玩家一眼认出「这是灵动岛」。
	inline SNoticeLayout ResolveLayout(const CUIRect &Screen, float BodyWidth, float BodyHeight, float TopMargin, float RingThickness, float RingGap)
	{
		SNoticeLayout Layout;
		Layout.m_Body.w = std::clamp(BodyWidth, 40.0f, std::max(80.0f, Screen.w - 32.0f));
		Layout.m_Body.h = std::max(12.0f, BodyHeight);
		Layout.m_Body.x = Screen.x + (Screen.w - Layout.m_Body.w) * 0.5f;
		Layout.m_Body.y = Screen.y + TopMargin;
		Layout.m_RingThickness = std::max(1.0f, RingThickness);
		Layout.m_RingOffset = Layout.m_RingThickness * 0.5f + std::max(0.0f, RingGap);
		return Layout;
	}
} // namespace qm_island

#endif
