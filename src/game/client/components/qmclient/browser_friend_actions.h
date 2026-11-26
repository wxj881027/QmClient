#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_FRIEND_ACTIONS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_FRIEND_ACTIONS_H

#include <game/client/ui.h>

#include <algorithm>
#include <optional>

struct SQmBrowserFriendActions
{
	// 有矩形的动作同时参与绘制和输入，离开行后不保留禁用态。
	std::optional<CUIRect> m_Pin;
	std::optional<CUIRect> m_Follow;
	std::optional<CUIRect> m_Copy;
	std::optional<CUIRect> m_Remove;
	float m_LabelRight;

	float LabelMaxWidth(float LabelX) const { return std::max(0.0f, m_LabelRight - LabelX); }

	static constexpr unsigned ACTION_BUTTON_FLAGS = BUTTONFLAG_LEFT | BUTTONFLAG_CURRENT_HIT;

	// 只加高热区，保留相邻图标间的横向空隙。
	static CUIRect HitRect(const CUIRect &Icon)
	{
		CUIRect Hit = Icon;
		Hit.h = std::max(Icon.h, 20.0f);
		Hit.y -= (Hit.h - Icon.h) * 0.5f;
		return Hit;
	}

	unsigned RowButtonFlags(vec2 Mouse) const
	{
		const auto Hovered = [Mouse](const std::optional<CUIRect> &Icon) {
			return Icon && HitRect(*Icon).Inside(Mouse);
		};
		if(Hovered(m_Pin) || Hovered(m_Follow) || Hovered(m_Copy) || Hovered(m_Remove))
			return BUTTONFLAG_RIGHT | BUTTONFLAG_CURRENT_HIT;
		return BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT;
	}
};

// Content 已扣除跟随胶囊的当前槽宽。按条目支持的动作预留文字空间，悬停不改变截断位置。
inline SQmBrowserFriendActions QmBrowserFriendActions(const CUIRect &Content, bool Hovered, bool IsPlayerFriend, bool HasName, bool Pinned)
{
	constexpr float IconSize = 13.0f;
	constexpr float Gap = 2.0f;
	constexpr float RowHeight = 16.0f;
	float Right = Content.x + Content.w;
	const auto TakeSlot = [&]() {
		const CUIRect Icon{Right - IconSize, Content.y + (RowHeight - IconSize) * 0.5f, IconSize, IconSize};
		Right = Icon.x - Gap;
		return Icon;
	};

	SQmBrowserFriendActions Actions{};
	const CUIRect Remove = TakeSlot();
	const CUIRect Copy = TakeSlot();
	if(Hovered)
	{
		Actions.m_Remove = Remove;
		Actions.m_Copy = Copy;
	}
	if(HasName)
	{
		const CUIRect Follow = TakeSlot();
		if(Hovered)
			Actions.m_Follow = Follow;
		if(IsPlayerFriend)
		{
			const CUIRect Pin = TakeSlot();
			if(Hovered || Pinned)
				Actions.m_Pin = Pin;
		}
	}
	Actions.m_LabelRight = Right;
	return Actions;
}

#endif
