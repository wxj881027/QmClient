/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_FRIENDS_H
#define ENGINE_FRIENDS_H

#include "kernel.h"

#include <engine/shared/protocol.h>

struct CFriendInfo
{
	char m_aName[MAX_NAME_LENGTH];
	char m_aClan[MAX_CLAN_LENGTH];
	char m_aCategory[64];
	char m_aNote[128];
	unsigned m_NameHash;
	unsigned m_ClanHash;
};

class IFriends : public IInterface
{
	MACRO_INTERFACE("friends")
public:
	enum
	{
		FRIEND_NO = 0,
		FRIEND_CLAN,
		FRIEND_PLAYER,
	};
	static constexpr auto MAX_FRIENDS = 4096;
	static constexpr int MAX_FRIEND_CATEGORIES = 64;
	static constexpr int MAX_FRIEND_CATEGORY_LENGTH = 64;
	static constexpr int MAX_FRIEND_NOTE_LENGTH = 128;
	static constexpr const char *DEFAULT_CATEGORY = "好友";
	static constexpr const char *CLAN_MEMBERS_CATEGORY = "战队成员";
	static constexpr const char *OFFLINE_CATEGORY = "离线";

	virtual void Init(bool Foes = false) = 0;

	virtual int NumFriends() const = 0;
	virtual const CFriendInfo *GetFriend(int Index) const = 0;
	virtual int GetFriendState(const char *pName, const char *pClan) const = 0;
	virtual bool IsFriend(const char *pName, const char *pClan, bool PlayersOnly) const = 0;
	virtual const char *GetFriendCategory(const char *pName, const char *pClan) const = 0;
	virtual const char *GetFriendNote(const char *pName, const char *pClan) const = 0;
	virtual bool SetFriendNote(const char *pName, const char *pClan, const char *pNote) = 0;
	virtual bool ClearFriendNote(const char *pName, const char *pClan) = 0;

	virtual const char *DefaultCategory() const = 0;
	virtual int NumCategories() const = 0;
	virtual const char *GetCategory(int Index) const = 0;
	virtual int FindCategory(const char *pCategory) const = 0;
	virtual bool AddCategory(const char *pCategory) = 0;
	virtual bool MoveCategory(int FromIndex, int ToIndex) = 0;
	virtual bool RenameCategory(const char *pOldCategory, const char *pNewCategory) = 0;
	virtual bool RemoveCategory(const char *pCategory) = 0;
	virtual bool SetFriendCategory(const char *pName, const char *pClan, const char *pCategory) = 0;

	virtual void AddFriend(const char *pName, const char *pClan, const char *pCategory = nullptr) = 0;
	virtual void RemoveFriend(const char *pName, const char *pClan) = 0;
};

#endif
