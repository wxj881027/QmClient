#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FRIENDS_CATEGORY_DRAG_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FRIENDS_CATEGORY_DRAG_H

#include <base/system.h>
#include <base/vmath.h>

#include <engine/friends.h>

#include <game/client/ui_rect.h>

struct SFriendsCategoryDragState
{
	int m_PressedIndex = -1;
	int m_DraggingIndex = -1;
	vec2 m_PressMouse = vec2(0.0f, 0.0f);
	vec2 m_GrabOffset = vec2(0.0f, 0.0f);
	float m_DraggedWidth = 0.0f;
	float m_DraggedHeight = 0.0f;
	bool m_HasDragRect = false;

	void Begin(int CategoryIndex, const CUIRect &Header, vec2 Mouse)
	{
		m_PressedIndex = CategoryIndex;
		m_DraggingIndex = -1;
		m_PressMouse = Mouse;
		m_GrabOffset = Mouse - vec2(Header.x, Header.y);
		m_DraggedWidth = Header.w;
		m_DraggedHeight = Header.h;
		m_HasDragRect = true;
	}

	bool Update(vec2 Mouse, bool MouseDown)
	{
		// 小幅抖动仍按点击处理；越过阈值后离开标题也能继续拖动。
		if(!MouseDown || m_PressedIndex < 0 || m_DraggingIndex >= 0 || distance(Mouse, m_PressMouse) < 5.0f)
			return false;
		m_DraggingIndex = m_PressedIndex;
		return true;
	}
};

struct SFriendsPlayerDragState
{
	const void *m_pPressedItem = nullptr;
	bool m_Dragging = false;
	vec2 m_PressMouse = vec2(0.0f, 0.0f);
	vec2 m_GrabOffset = vec2(0.0f, 0.0f);
	float m_DraggedWidth = 0.0f;
	float m_DraggedHeight = 0.0f;
	char m_aName[MAX_NAME_LENGTH] = {0};
	char m_aClan[MAX_CLAN_LENGTH] = {0};
	char m_aCategory[IFriends::MAX_FRIEND_CATEGORY_LENGTH] = {0};

	void Begin(const void *pItemId, int FriendState, const char *pName, const char *pClan, const char *pCategory, const CUIRect &Rect, vec2 Mouse)
	{
		*this = {};
		if(pItemId == nullptr || FriendState != IFriends::FRIEND_PLAYER || pName == nullptr || pName[0] == '\0')
			return;
		m_pPressedItem = pItemId;
		m_PressMouse = Mouse;
		m_GrabOffset = Mouse - vec2(Rect.x, Rect.y);
		m_DraggedWidth = Rect.w;
		m_DraggedHeight = Rect.h;
		str_copy(m_aName, pName);
		str_copy(m_aClan, pClan);
		str_copy(m_aCategory, pCategory);
	}

	bool Update(vec2 Mouse, bool MouseDown)
	{
		if(!MouseDown || m_pPressedItem == nullptr || m_Dragging || distance(Mouse, m_PressMouse) < 5.0f)
			return false;
		m_Dragging = true;
		return true;
	}

	bool CanDropTo(const char *pCategory) const
	{
		return m_Dragging && pCategory != nullptr && pCategory[0] != '\0' &&
		       str_comp_nocase(pCategory, IFriends::CLAN_MEMBERS_CATEGORY) != 0 &&
		       str_comp_nocase(pCategory, IFriends::OFFLINE_CATEGORY) != 0 &&
		       str_comp_nocase(pCategory, m_aCategory) != 0;
	}
};

#endif
