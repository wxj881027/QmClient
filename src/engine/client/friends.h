/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CLIENT_FRIENDS_H
#define ENGINE_CLIENT_FRIENDS_H

#include <engine/console.h>
#include <engine/friends.h>

class IConfigManager;

class CFriends : public IFriends
{
	CFriendInfo m_aFriends[MAX_FRIENDS];
	char m_aaCategories[MAX_FRIEND_CATEGORIES][MAX_FRIEND_CATEGORY_LENGTH];
	int m_Foes;
	int m_NumFriends;
	int m_NumCategories;

	static void ConAddFriend(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveFriend(IConsole::IResult *pResult, void *pUserData);
	static void ConAddFriendCategory(IConsole::IResult *pResult, void *pUserData);
	static void ConRenameFriendCategory(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveFriendCategory(IConsole::IResult *pResult, void *pUserData);
	static void ConSetFriendCategory(IConsole::IResult *pResult, void *pUserData);
	static void ConSetFriendNote(IConsole::IResult *pResult, void *pUserData);
	static void ConClearFriendNote(IConsole::IResult *pResult, void *pUserData);
	static void ConFriends(IConsole::IResult *pResult, void *pUserData);

	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);

	static bool NormalizeCategoryName(const char *pCategory, char (&aCategory)[MAX_FRIEND_CATEGORY_LENGTH]);
	static bool IsClanMembersCategory(const char *pCategory);
	static bool IsProtectedCategory(const char *pCategory);
	int FindFriend(const char *pName, const char *pClan) const;

public:
	CFriends();

	void Init(bool Foes = false) override;

	int NumFriends() const override { return m_NumFriends; }
	const CFriendInfo *GetFriend(int Index) const override;
	int GetFriendState(const char *pName, const char *pClan) const override;
	bool IsFriend(const char *pName, const char *pClan, bool PlayersOnly) const override;
	const char *GetFriendCategory(const char *pName, const char *pClan) const override;
	const char *GetFriendNote(const char *pName, const char *pClan) const override;
	bool SetFriendNote(const char *pName, const char *pClan, const char *pNote) override;
	bool ClearFriendNote(const char *pName, const char *pClan) override;

	const char *DefaultCategory() const override { return IFriends::DEFAULT_CATEGORY; }
	int NumCategories() const override { return m_NumCategories; }
	const char *GetCategory(int Index) const override;
	int FindCategory(const char *pCategory) const override;
	bool AddCategory(const char *pCategory) override;
	bool MoveCategory(int FromIndex, int ToIndex) override;
	bool RenameCategory(const char *pOldCategory, const char *pNewCategory) override;
	bool RemoveCategory(const char *pCategory) override;
	bool SetFriendCategory(const char *pName, const char *pClan, const char *pCategory) override;

	void AddFriend(const char *pName, const char *pClan, const char *pCategory = nullptr) override;
	void RemoveFriend(const char *pName, const char *pClan) override;
	void RemoveFriend(int Index);
	void Friends();
};

#endif
