// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMISLANDNOTICE_H
#define GAME_CLIENT_QMUI_QMISLANDNOTICE_H

#include "QmIslandSurface.h"
#include "UiContext.h"

#include <base/color.h>
#include <base/types.h>

#include <game/client/components/hud_media_island_logic.h>

#include <algorithm>

// 灵动岛式通知：从屏幕顶部掉下一颗小球 → 液体形变成胶囊 → 外框环绕一圈倒计时条
// → 时间到自动收回。
//
// 入场动画直接复用 HUD 动态岛那套（QmHudMediaIslandEntrancePose +
// Drop/Expand 双弹簧），所以观感与 HUD 动态岛同源：掉落≈0.18s 临界阻尼，
// 展开≈0.66s 轻微欠阻尼。
//
// 生命周期状态由调用方持有（CQmIslandNotice），因为「何时出现」属于业务，
// 而「怎么画」属于表现，两者分开后同一个组件能同时服务赞助提醒与关闭挽留提示。
namespace qm_island
{
	struct SNoticeState
	{
		// 可见时长（秒）。到时自动进入收回。
		float m_DurationSeconds = 5.0f;
		// 已显示时长（秒），由 Advance 推进。
		float m_ElapsedSeconds = 0.0f;
		// 是否处于「已出现」状态；false 时弹簧回落并允许再次出现。
		bool m_Visible = false;
		// 是否已经完整播放过一轮（用于调用方判定「该收了」）。
		bool m_Finished = false;
	};

	// 推进倒计时。返回 true 表示本轮刚结束（调用方据此把业务状态也置为不可见）。
	inline bool Advance(SNoticeState &State, float DeltaSeconds)
	{
		if(!State.m_Visible || State.m_Finished)
			return false;

		State.m_ElapsedSeconds += DeltaSeconds;
		if(State.m_ElapsedSeconds < State.m_DurationSeconds)
			return false;

		State.m_Finished = true;
		return true;
	}

	// 剩余比例 1 → 0，用于环绕倒计时条。
	inline float RemainingFraction(const SNoticeState &State)
	{
		if(State.m_DurationSeconds <= 0.0f)
			return 0.0f;
		return std::clamp(1.0f - State.m_ElapsedSeconds / State.m_DurationSeconds, 0.0f, 1.0f);
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
		// 胶囊主体矩形（不含外阴影环）。
		CUIRect m_Body{};
		// 环绕倒计时条半径（相对主体中心）。
		float m_RingRadius = 0.0f;
		float m_RingThickness = 1.0f;
	};

	// 顶部居中布局：HUD 动态岛占据屏幕顶部中间，这里保持一致，
	// 让玩家一眼认出「这是灵动岛」。
	inline SNoticeLayout ResolveLayout(const CUIRect &Screen, float BodyWidth, float BodyHeight, float TopMargin, float RingThickness)
	{
		SNoticeLayout Layout;
		Layout.m_Body.w = std::clamp(BodyWidth, 40.0f, std::max(80.0f, Screen.w - 32.0f));
		Layout.m_Body.h = std::max(12.0f, BodyHeight);
		Layout.m_Body.x = Screen.x + (Screen.w - Layout.m_Body.w) * 0.5f;
		Layout.m_Body.y = Screen.y + TopMargin;
		// 环绕条贴着外沿：半径略大于主体半高，厚度留出可见的一圈。
		Layout.m_RingRadius = Layout.m_Body.h * 0.5f + 1.5f;
		Layout.m_RingThickness = std::max(1.0f, RingThickness);
		return Layout;
	}
} // namespace qm_island

#endif
