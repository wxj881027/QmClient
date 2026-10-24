/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "friends.h"

#include <base/dbg.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/shared/config.h>

#include <algorithm>

CFriends::CFriends()
{
	mem_zero(m_aFriends, sizeof(m_aFriends));
	mem_zero(m_aaCategories, sizeof(m_aaCategories));
	m_NumFriends = 0;
	m_NumCategories = 0;
	m_Foes = false;

	str_copy(m_aaCategories[m_NumCategories++], DefaultCategory(), sizeof(m_aaCategories[0]));
	str_copy(m_aaCategories[m_NumCategories++], IFriends::CLAN_MEMBERS_CATEGORY, sizeof(m_aaCategories[0]));
	str_copy(m_aaCategories[m_NumCategories++], IFriends::OFFLINE_CATEGORY, sizeof(m_aaCategories[0]));
}

bool CFriends::NormalizeCategoryName(const char *pCategory, char (&aCategory)[MAX_FRIEND_CATEGORY_LENGTH])
{
	if(pCategory == nullptr)
	{
		aCategory[0] = '\0';
		return false;
	}

	str_copy(aCategory, str_utf8_skip_whitespaces(pCategory), sizeof(aCategory));
	str_utf8_trim_right(aCategory);
	return aCategory[0] != '\0';
}

bool CFriends::IsClanMembersCategory(const char *pCategory)
{
	return pCategory != nullptr && str_comp_nocase(pCategory, IFriends::CLAN_MEMBERS_CATEGORY) == 0;
}

static bool IsOfflineCategory(const char *pCategory)
{
	return pCategory != nullptr && str_comp_nocase(pCategory, IFriends::OFFLINE_CATEGORY) == 0;
}

bool CFriends::IsProtectedCategory(const char *pCategory)
{
	return pCategory != nullptr && (str_comp_nocase(pCategory, IFriends::DEFAULT_CATEGORY) == 0 || IsClanMembersCategory(pCategory) || IsOfflineCategory(pCategory));
}

int CFriends::FindFriend(const char *pName, const char *pClan) const
{
	const unsigned NameHash = str_quickhash(pName);
	const unsigned ClanHash = str_quickhash(pClan);
	for(int i = 0; i < m_NumFriends; ++i)
	{
		if((m_aFriends[i].m_NameHash == NameHash && !str_comp(m_aFriends[i].m_aName, pName)) &&
			((g_Config.m_ClFriendsIgnoreClan && m_aFriends[i].m_aName[0]) || (m_aFriends[i].m_ClanHash == ClanHash && !str_comp(m_aFriends[i].m_aClan, pClan))))
		{
			return i;
		}
	}
	return -1;
}

void CFriends::ConAddFriend(IConsole::IResult *pResult, void *pUserData)
{
	CFriends *pSelf = (CFriends *)pUserData;
	const char *pCategory = pResult->NumArguments() > 2 ? pResult->GetString(2) : nullptr;
	pSelf->AddFriend(pResult->GetString(0), pResult->GetString(1), pCategory);
}

void CFriends::ConRemoveFriend(IConsole::IResult *pResult, void *pUserData)
{
	CFriends *pSelf = (CFriends *)pUserData;
	pSelf->RemoveFriend(pResult->GetString(0), pResult->GetString(1));
}

void CFriends::ConAddFriendCategory(IConsole::IResult *pResult, void *pUserData)
{
	CFriends *pSelf = (CFriends *)pUserData;
	pSelf->AddCategory(pResult->GetString(0));
}

void CFriends::ConMoveFriendCategory(IConsole::IResult *pResult, void *pUserData)
{
	CFriends *pSelf = (CFriends *)pUserData;
	pSelf->MoveCategory(pSelf->FindCategory(pResult->GetString(0)), pResult->GetInteger(1));
}

void CFriends::ConRenameFriendCategory(IConsole::IResult *pResult, void *pUserData)
{
	CFriends *pSelf = (CFriends *)pUserData;
	pSelf->RenameCategory(pResult->GetString(0), pResult->GetString(1));
}

void CFriends::ConRemoveFriendCategory(IConsole::IResult *pResult, void *pUserData)
{
	CFriends *pSelf = (CFriends *)pUserData;
	pSelf->RemoveCategory(pResult->GetString(0));
}

void CFriends::ConSetFriendCategory(IConsole::IResult *pResult, void *pUserData)
{
	CFriends *pSelf = (CFriends *)pUserData;
	pSelf->SetFriendCategory(pResult->GetString(0), pResult->GetString(1), pResult->GetString(2));
}

void CFriends::ConSetFriendPinned(IConsole::IResult *pResult, void *pUserData)
{
	CFriends *pSelf = (CFriends *)pUserData;
	pSelf->SetFriendPinned(pResult->GetString(0), pResult->GetString(1), pResult->GetInteger(2) != 0);
}

void CFriends::ConSetFriendNote(IConsole::IResult *pResult, void *pUserData)
{
	CFriends *pSelf = (CFriends *)pUserData;
	pSelf->SetFriendNote(pResult->GetString(0), pResult->GetString(1), pResult->GetString(2));
}

void CFriends::ConClearFriendNote(IConsole::IResult *pResult, void *pUserData)
{
	CFriends *pSelf = (CFriends *)pUserData;
	pSelf->ClearFriendNote(pResult->GetString(0), pResult->GetString(1));
}

void CFriends::ConFriends(IConsole::IResult *pResult, void *pUserData)
{
	CFriends *pSelf = (CFriends *)pUserData;
	pSelf->Friends();
}

void CFriends::Init(bool Foes)
{
	m_Foes = Foes;

	IConfigManager *pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	if(pConfigManager)
		pConfigManager->RegisterCallback(ConfigSaveCallback, this);

	IConsole *pConsole = Kernel()->RequestInterface<IConsole>();
	if(!pConsole)
		return;

	if(Foes)
	{
		pConsole->Register("add_foe", "s[name] ?s[clan]", CFGFLAG_CLIENT, ConAddFriend, this, "Add a foe");
		pConsole->Register("remove_foe", "s[name] ?s[clan]", CFGFLAG_CLIENT, ConRemoveFriend, this, "Remove a foe");
		pConsole->Register("foes", "", CFGFLAG_CLIENT, ConFriends, this, "List foes");
		return;
	}

	pConsole->Register("add_friend", "s[name] ?s[clan] ?s[category]", CFGFLAG_CLIENT, ConAddFriend, this, "Add a friend");
	pConsole->Register("remove_friend", "s[name] ?s[clan]", CFGFLAG_CLIENT, ConRemoveFriend, this, "Remove a friend");
	pConsole->Register("friend_category_add", "s[category]", CFGFLAG_CLIENT, ConAddFriendCategory, this, "Add a friend category");
	pConsole->Register("qm_friend_category_move", "s[category] i[position]", CFGFLAG_CLIENT, ConMoveFriendCategory, this, "Move a friend category to a position");
	pConsole->Register("friend_category_rename", "s[old_category] s[new_category]", CFGFLAG_CLIENT, ConRenameFriendCategory, this, "Rename a friend category");
	pConsole->Register("friend_category_remove", "s[category]", CFGFLAG_CLIENT, ConRemoveFriendCategory, this, "Remove a friend category");
	pConsole->Register("set_friend_category", "s[name] s[clan] s[category]", CFGFLAG_CLIENT, ConSetFriendCategory, this, "Set friend category");
	pConsole->Register("qm_friend_pin", "s[name] s[clan] i[pinned]", CFGFLAG_CLIENT, ConSetFriendPinned, this, "Set whether a friend is pinned");
	pConsole->Register("set_friend_note", "s[name] s[clan] r[note]", CFGFLAG_CLIENT, ConSetFriendNote, this, "Set friend note");
	pConsole->Register("clear_friend_note", "s[name] s[clan]", CFGFLAG_CLIENT, ConClearFriendNote, this, "Clear friend note");
	pConsole->Register("friends", "", CFGFLAG_CLIENT, ConFriends, this, "List friends");
}

const CFriendInfo *CFriends::GetFriend(int Index) const
{
	dbg_assert(Index >= 0 && Index < m_NumFriends, "Invalid Index: %d", Index);
	return &m_aFriends[Index];
}

int CFriends::GetFriendState(const char *pName, const char *pClan) const
{
	int Result = FRIEND_NO;
	const unsigned NameHash = str_quickhash(pName);
	const unsigned ClanHash = str_quickhash(pClan);
	for(int i = 0; i < m_NumFriends; ++i)
	{
		if((g_Config.m_ClFriendsIgnoreClan && m_aFriends[i].m_aName[0]) || (m_aFriends[i].m_ClanHash == ClanHash && !str_comp(m_aFriends[i].m_aClan, pClan)))
		{
			if(m_aFriends[i].m_aName[0] == 0)
			{
				Result = FRIEND_CLAN;
			}
			else if(m_aFriends[i].m_NameHash == NameHash && !str_comp(m_aFriends[i].m_aName, pName))
			{
				Result = FRIEND_PLAYER;
				break;
			}
		}
	}
	return Result;
}

bool CFriends::IsFriend(const char *pName, const char *pClan, bool PlayersOnly) const
{
	const unsigned NameHash = str_quickhash(pName);
	const unsigned ClanHash = str_quickhash(pClan);
	for(int i = 0; i < m_NumFriends; ++i)
	{
		if(((g_Config.m_ClFriendsIgnoreClan && m_aFriends[i].m_aName[0]) || (m_aFriends[i].m_ClanHash == ClanHash && !str_comp(m_aFriends[i].m_aClan, pClan))) &&
			((!PlayersOnly && m_aFriends[i].m_aName[0] == 0) || (m_aFriends[i].m_NameHash == NameHash && !str_comp(m_aFriends[i].m_aName, pName))))
			return true;
	}
	return false;
}

bool CFriends::IsFriendPinned(const char *pName, const char *pClan) const
{
	const int FriendIndex = FindFriend(pName, pClan);
	return FriendIndex >= 0 && m_aFriends[FriendIndex].m_aName[0] != '\0' && m_aFriends[FriendIndex].m_Pinned;
}

bool CFriends::SetFriendPinned(const char *pName, const char *pClan, bool Pinned)
{
	const int FriendIndex = FindFriend(pName, pClan);
	if(FriendIndex < 0 || m_aFriends[FriendIndex].m_aName[0] == '\0' || m_aFriends[FriendIndex].m_Pinned == Pinned)
		return false;

	m_aFriends[FriendIndex].m_Pinned = Pinned;
	++m_Revision;
	return true;
}

const char *CFriends::GetFriendCategory(const char *pName, const char *pClan) const
{
	const unsigned NameHash = str_quickhash(pName);
	const unsigned ClanHash = str_quickhash(pClan);
	for(int i = 0; i < m_NumFriends; ++i)
	{
		if(m_aFriends[i].m_aName[0] == 0)
			continue;
		if(m_aFriends[i].m_NameHash != NameHash || str_comp(m_aFriends[i].m_aName, pName) != 0)
			continue;
		if(!g_Config.m_ClFriendsIgnoreClan && (m_aFriends[i].m_ClanHash != ClanHash || str_comp(m_aFriends[i].m_aClan, pClan) != 0))
			continue;

		const char *pCategory = m_aFriends[i].m_aCategory[0] != '\0' ? m_aFriends[i].m_aCategory : DefaultCategory();
		const int CategoryIndex = FindCategory(pCategory);
		return CategoryIndex >= 0 ? GetCategory(CategoryIndex) : DefaultCategory();
	}

	for(int i = 0; i < m_NumFriends; ++i)
	{
		if(m_aFriends[i].m_aName[0] != 0)
			continue;
		if(m_aFriends[i].m_ClanHash == ClanHash && !str_comp(m_aFriends[i].m_aClan, pClan))
			return IFriends::CLAN_MEMBERS_CATEGORY;
	}
	return IFriends::OFFLINE_CATEGORY;
}

const char *CFriends::GetFriendNote(const char *pName, const char *pClan) const
{
	const int FriendIndex = FindFriend(pName, pClan);
	if(FriendIndex < 0)
		return "";

	return m_aFriends[FriendIndex].m_aNote;
}

bool CFriends::SetFriendNote(const char *pName, const char *pClan, const char *pNote)
{
	const int FriendIndex = FindFriend(pName, pClan);
	if(FriendIndex < 0 || m_aFriends[FriendIndex].m_aName[0] == '\0')
		return false;

	char aNote[IFriends::MAX_FRIEND_NOTE_LENGTH];
	if(pNote == nullptr)
	{
		aNote[0] = '\0';
	}
	else
	{
		str_copy(aNote, pNote, sizeof(aNote));
		str_utf8_trim_right(aNote);
	}

	if(str_comp(m_aFriends[FriendIndex].m_aNote, aNote) == 0)
		return false;

	str_copy(m_aFriends[FriendIndex].m_aNote, aNote, sizeof(m_aFriends[FriendIndex].m_aNote));
	return true;
}

bool CFriends::ClearFriendNote(const char *pName, const char *pClan)
{
	return SetFriendNote(pName, pClan, "");
}

const char *CFriends::GetCategory(int Index) const
{
	if(Index < 0 || Index >= m_NumCategories)
		return DefaultCategory();
	return m_aaCategories[Index];
}

int CFriends::FindCategory(const char *pCategory) const
{
	char aCategory[MAX_FRIEND_CATEGORY_LENGTH];
	if(!NormalizeCategoryName(pCategory, aCategory))
		return -1;

	for(int i = 0; i < m_NumCategories; ++i)
	{
		if(str_comp_nocase(m_aaCategories[i], aCategory) == 0)
			return i;
	}
	return -1;
}

bool CFriends::AddCategory(const char *pCategory)
{
	char aCategory[MAX_FRIEND_CATEGORY_LENGTH];
	if(!NormalizeCategoryName(pCategory, aCategory) || IsProtectedCategory(aCategory) || FindCategory(aCategory) >= 0 || m_NumCategories >= MAX_FRIEND_CATEGORIES)
		return false;

	int InsertIndex = m_NumCategories;
	const int OfflineCategoryIndex = FindCategory(IFriends::OFFLINE_CATEGORY);
	if(OfflineCategoryIndex >= 0)
		InsertIndex = OfflineCategoryIndex;

	if(InsertIndex < m_NumCategories)
		mem_move(&m_aaCategories[InsertIndex + 1], &m_aaCategories[InsertIndex], sizeof(m_aaCategories[0]) * (m_NumCategories - InsertIndex));

	str_copy(m_aaCategories[InsertIndex], aCategory, sizeof(m_aaCategories[0]));
	++m_NumCategories;
	++m_Revision;
	return true;
}

bool CFriends::MoveCategory(int FromIndex, int ToIndex)
{
	if(FromIndex < 0 || ToIndex < 0 || FromIndex >= m_NumCategories || ToIndex >= m_NumCategories || FromIndex == ToIndex)
		return false;

	char aCategory[MAX_FRIEND_CATEGORY_LENGTH];
	str_copy(aCategory, m_aaCategories[FromIndex], sizeof(aCategory));

	if(FromIndex < ToIndex)
	{
		mem_move(&m_aaCategories[FromIndex], &m_aaCategories[FromIndex + 1], sizeof(m_aaCategories[0]) * (ToIndex - FromIndex));
	}
	else
	{
		mem_move(&m_aaCategories[ToIndex + 1], &m_aaCategories[ToIndex], sizeof(m_aaCategories[0]) * (FromIndex - ToIndex));
	}

	str_copy(m_aaCategories[ToIndex], aCategory, sizeof(m_aaCategories[0]));
	++m_Revision;
	return true;
}

bool CFriends::RenameCategory(const char *pOldCategory, const char *pNewCategory)
{
	char aOldCategory[MAX_FRIEND_CATEGORY_LENGTH];
	char aNewCategory[MAX_FRIEND_CATEGORY_LENGTH];
	if(!NormalizeCategoryName(pOldCategory, aOldCategory) || !NormalizeCategoryName(pNewCategory, aNewCategory) || str_comp_nocase(aOldCategory, aNewCategory) == 0)
		return false;

	const int OldCategoryIndex = FindCategory(aOldCategory);
	if(OldCategoryIndex < 0 || IsProtectedCategory(m_aaCategories[OldCategoryIndex]))
		return false;

	if(IsProtectedCategory(aNewCategory))
		return false;

	const int ExistingCategoryIndex = FindCategory(aNewCategory);
	if(ExistingCategoryIndex >= 0 && ExistingCategoryIndex != OldCategoryIndex)
		return false;

	char aOldCategoryStored[MAX_FRIEND_CATEGORY_LENGTH];
	str_copy(aOldCategoryStored, m_aaCategories[OldCategoryIndex], sizeof(aOldCategoryStored));
	str_copy(m_aaCategories[OldCategoryIndex], aNewCategory, sizeof(m_aaCategories[OldCategoryIndex]));

	for(int i = 0; i < m_NumFriends; ++i)
	{
		if(m_aFriends[i].m_aName[0] == '\0')
			continue;
		if(str_comp_nocase(m_aFriends[i].m_aCategory, aOldCategoryStored) == 0)
			str_copy(m_aFriends[i].m_aCategory, m_aaCategories[OldCategoryIndex], sizeof(m_aFriends[i].m_aCategory));
	}

	++m_Revision;
	return true;
}

bool CFriends::RemoveCategory(const char *pCategory)
{
	char aCategory[MAX_FRIEND_CATEGORY_LENGTH];
	if(!NormalizeCategoryName(pCategory, aCategory))
		return false;

	const int CategoryIndex = FindCategory(aCategory);
	if(CategoryIndex < 0 || IsProtectedCategory(m_aaCategories[CategoryIndex]))
		return false;

	for(int i = 0; i < m_NumFriends; ++i)
	{
		if(m_aFriends[i].m_aName[0] == '\0')
			continue;
		if(str_comp_nocase(m_aFriends[i].m_aCategory, m_aaCategories[CategoryIndex]) == 0)
			str_copy(m_aFriends[i].m_aCategory, DefaultCategory(), sizeof(m_aFriends[i].m_aCategory));
	}

	if(CategoryIndex < m_NumCategories - 1)
		mem_move(&m_aaCategories[CategoryIndex], &m_aaCategories[CategoryIndex + 1], sizeof(m_aaCategories[0]) * (m_NumCategories - CategoryIndex - 1));
	--m_NumCategories;
	++m_Revision;
	return true;
}

bool CFriends::SetFriendCategory(const char *pName, const char *pClan, const char *pCategory)
{
	char aCategory[MAX_FRIEND_CATEGORY_LENGTH];
	if(!NormalizeCategoryName(pCategory, aCategory))
		return false;

	const int CategoryIndex = FindCategory(aCategory);
	if(CategoryIndex < 0 || IsClanMembersCategory(m_aaCategories[CategoryIndex]))
		return false;

	const int FriendIndex = FindFriend(pName, pClan);
	if(FriendIndex < 0 || m_aFriends[FriendIndex].m_aName[0] == '\0')
		return false;

	if(str_comp_nocase(m_aFriends[FriendIndex].m_aCategory, m_aaCategories[CategoryIndex]) == 0)
		return false;

	str_copy(m_aFriends[FriendIndex].m_aCategory, m_aaCategories[CategoryIndex], sizeof(m_aFriends[FriendIndex].m_aCategory));
	++m_Revision;
	return true;
}

void CFriends::AddFriend(const char *pName, const char *pClan, const char *pCategory)
{
	if(m_NumFriends == MAX_FRIENDS)
		return;

	// 查重和哈希使用最终保存的字节，避免固定字段截断后身份不一致。
	char aName[MAX_NAME_LENGTH];
	char aClan[MAX_CLAN_LENGTH];
	str_copy(aName, pName);
	str_copy(aClan, pClan);
	pName = aName;
	pClan = aClan;
	if(pName[0] == 0 && pClan[0] == 0)
		return;

	const bool IsClanFriend = pName[0] == '\0';
	char aCategory[MAX_FRIEND_CATEGORY_LENGTH];
	if(IsClanFriend)
	{
		str_copy(aCategory, IFriends::CLAN_MEMBERS_CATEGORY, sizeof(aCategory));
	}
	else
	{
		str_copy(aCategory, DefaultCategory(), sizeof(aCategory));
		if(pCategory != nullptr)
		{
			const int CategoryIndex = FindCategory(pCategory);
			if(CategoryIndex >= 0 && !IsClanMembersCategory(m_aaCategories[CategoryIndex]))
				str_copy(aCategory, m_aaCategories[CategoryIndex], sizeof(aCategory));
		}
	}

	const int ExistingFriend = FindFriend(pName, pClan);
	if(ExistingFriend >= 0)
	{
		const bool ExistingClanFriend = m_aFriends[ExistingFriend].m_aName[0] == '\0';
		const char *pNewCategory = ExistingClanFriend ? IFriends::CLAN_MEMBERS_CATEGORY : aCategory;
		if(str_comp(m_aFriends[ExistingFriend].m_aCategory, pNewCategory) != 0)
		{
			str_copy(m_aFriends[ExistingFriend].m_aCategory, pNewCategory, sizeof(m_aFriends[ExistingFriend].m_aCategory));
			++m_Revision;
		}
		return;
	}

	const unsigned NameHash = str_quickhash(pName);
	const unsigned ClanHash = str_quickhash(pClan);

	str_copy(m_aFriends[m_NumFriends].m_aName, pName, sizeof(m_aFriends[m_NumFriends].m_aName));
	str_copy(m_aFriends[m_NumFriends].m_aClan, pClan, sizeof(m_aFriends[m_NumFriends].m_aClan));
	str_copy(m_aFriends[m_NumFriends].m_aCategory, aCategory, sizeof(m_aFriends[m_NumFriends].m_aCategory));
	m_aFriends[m_NumFriends].m_aNote[0] = '\0';
	m_aFriends[m_NumFriends].m_NameHash = NameHash;
	m_aFriends[m_NumFriends].m_ClanHash = ClanHash;
	m_aFriends[m_NumFriends].m_Pinned = false;
	++m_NumFriends;
	++m_Revision;
}

void CFriends::RemoveFriend(const char *pName, const char *pClan)
{
	const int FriendIndex = FindFriend(pName, pClan);
	if(FriendIndex >= 0)
		RemoveFriend(FriendIndex);
}

void CFriends::RemoveFriend(int Index)
{
	dbg_assert(Index >= 0 && Index < m_NumFriends, "Invalid Index: %d", Index);
	mem_move(&m_aFriends[Index], &m_aFriends[Index + 1], sizeof(CFriendInfo) * (m_NumFriends - (Index + 1)));
	--m_NumFriends;
	++m_Revision;
}

void CFriends::Friends()
{
	char aBuf[256];
	IConsole *pConsole = Kernel()->RequestInterface<IConsole>();
	if(pConsole)
	{
		for(int i = 0; i < m_NumFriends; ++i)
		{
			if(m_Foes)
				str_format(aBuf, sizeof(aBuf), "Name: %s, Clan: %s", m_aFriends[i].m_aName, m_aFriends[i].m_aClan);
			else
				str_format(aBuf, sizeof(aBuf), "Name: %s, Clan: %s, Category: %s", m_aFriends[i].m_aName, m_aFriends[i].m_aClan, m_aFriends[i].m_aCategory);

			pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, m_Foes ? "foes" : "friends", aBuf, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor)));
		}
	}
}

void CFriends::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	CFriends *pSelf = (CFriends *)pUserData;
	// 字段转义最多翻倍，另为命令、引号、分隔符和分组序号预留空间。
	constexpr int MaxFieldLength = MAX_NAME_LENGTH + MAX_CLAN_LENGTH + std::max(MAX_FRIEND_CATEGORY_LENGTH, MAX_FRIEND_NOTE_LENGTH);
	char aBuf[2 * MaxFieldLength + 64];
	const char *pEnd = aBuf + sizeof(aBuf) - 4;

	if(!pSelf->m_Foes)
	{
		for(int CategoryIndex = 0; CategoryIndex < pSelf->m_NumCategories; ++CategoryIndex)
		{
			if(IsProtectedCategory(pSelf->m_aaCategories[CategoryIndex]))
				continue;

			str_copy(aBuf, "friend_category_add \"");
			char *pDst = aBuf + str_length(aBuf);
			str_escape(&pDst, pSelf->m_aaCategories[CategoryIndex], pEnd);
			str_append(aBuf, "\"");
			pConfigManager->WriteLine(aBuf);
		}

		// 先创建自定义分组，再按名称恢复包括内置分组在内的完整顺序。
		for(int CategoryIndex = 0; CategoryIndex < pSelf->m_NumCategories; ++CategoryIndex)
		{
			str_copy(aBuf, "qm_friend_category_move \"");
			char *pDst = aBuf + str_length(aBuf);
			str_escape(&pDst, pSelf->m_aaCategories[CategoryIndex], pEnd);
			char aPosition[16];
			str_format(aPosition, sizeof(aPosition), "\" %d", CategoryIndex);
			str_append(aBuf, aPosition);
			pConfigManager->WriteLine(aBuf);
		}
	}

	for(int i = 0; i < pSelf->m_NumFriends; ++i)
	{
		str_copy(aBuf, pSelf->m_Foes ? "add_foe " : "add_friend ");

		str_append(aBuf, "\"");
		char *pDst = aBuf + str_length(aBuf);
		str_escape(&pDst, pSelf->m_aFriends[i].m_aName, pEnd);
		str_append(aBuf, "\" \"");
		pDst = aBuf + str_length(aBuf);
		str_escape(&pDst, pSelf->m_aFriends[i].m_aClan, pEnd);
		str_append(aBuf, "\"");

		if(!pSelf->m_Foes)
		{
			str_append(aBuf, " \"");
			pDst = aBuf + str_length(aBuf);
			const char *pCategory = pSelf->m_aFriends[i].m_aCategory[0] != '\0' ? pSelf->m_aFriends[i].m_aCategory : pSelf->DefaultCategory();
			str_escape(&pDst, pCategory, pEnd);
			str_append(aBuf, "\"");
		}

		pConfigManager->WriteLine(aBuf);

		if(!pSelf->m_Foes && pSelf->m_aFriends[i].m_Pinned)
		{
			str_copy(aBuf, "qm_friend_pin \"");
			char *pPinDst = aBuf + str_length(aBuf);
			str_escape(&pPinDst, pSelf->m_aFriends[i].m_aName, pEnd);
			str_append(aBuf, "\" \"");
			pPinDst = aBuf + str_length(aBuf);
			str_escape(&pPinDst, pSelf->m_aFriends[i].m_aClan, pEnd);
			str_append(aBuf, "\" 1");
			pConfigManager->WriteLine(aBuf);
		}

		if(!pSelf->m_Foes && pSelf->m_aFriends[i].m_aNote[0] != '\0')
		{
			str_copy(aBuf, "set_friend_note \"");
			char *pNoteDst = aBuf + str_length(aBuf);
			str_escape(&pNoteDst, pSelf->m_aFriends[i].m_aName, pEnd);
			str_append(aBuf, "\" \"");
			pNoteDst = aBuf + str_length(aBuf);
			str_escape(&pNoteDst, pSelf->m_aFriends[i].m_aClan, pEnd);
			str_append(aBuf, "\" \"");
			pNoteDst = aBuf + str_length(aBuf);
			str_escape(&pNoteDst, pSelf->m_aFriends[i].m_aNote, pEnd);
			str_append(aBuf, "\"");
			pConfigManager->WriteLine(aBuf);
		}
	}
}
