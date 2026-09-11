/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/log.h>
#include <base/perf_timer.h>

#include <engine/engine.h>
#include <engine/favorites.h>
#include <engine/friends.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/qmclient/map_history_ui.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>
#include <game/voting.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <unordered_map>

using namespace FontIcons;

static constexpr ColorRGBA gs_HighlightedTextColor = ColorRGBA(0.4f, 0.4f, 1.0f, 1.0f);

static ColorRGBA BrowserOpacityColor(ColorRGBA Color, float AlphaScale = 1.0f)
{
	if(Color.r == Color.g && Color.g == Color.b)
	{
		const ColorRGBA Base = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmMapBrowserColor));
		Color.r = std::clamp(Base.r * (0.35f + Color.r * 0.65f), 0.0f, 1.0f);
		Color.g = std::clamp(Base.g * (0.35f + Color.g * 0.65f), 0.0f, 1.0f);
		Color.b = std::clamp(Base.b * (0.35f + Color.b * 0.65f), 0.0f, 1.0f);
	}
	Color.a = std::clamp(Color.a * (g_Config.m_QmMapBrowserOpacity / 100.0f) * AlphaScale, 0.0f, 1.0f);
	return Color;
}

class CUiBackgroundAlphaScaleScope
{
	CUi *m_pUi;
	float m_SavedAlphaScale;

public:
	CUiBackgroundAlphaScaleScope(CUi *pUi, float AlphaScale) :
		m_pUi(pUi),
		m_SavedAlphaScale(pUi->BackgroundAlphaScale())
	{
		m_pUi->SetBackgroundAlphaScale(AlphaScale);
	}

	~CUiBackgroundAlphaScaleScope()
	{
		m_pUi->SetBackgroundAlphaScale(m_SavedAlphaScale);
	}
};

static const char *FavoriteMapCategoryKeyFromText(const char *pText)
{
	if(!pText || pText[0] == '\0')
		return nullptr;
	if(str_find_nocase(pText, "DDmaX"))
	{
		if(str_find_nocase(pText, "Easy"))
			return "DDmaX Easy";
		if(str_find_nocase(pText, "Next"))
			return "DDmaX Next";
		if(str_find_nocase(pText, "Pro"))
			return "DDmaX Pro";
		if(str_find_nocase(pText, "Nut"))
			return "DDmaX Nut";
		return "DDmaX";
	}
	if(str_find_nocase(pText, "Oldschool"))
		return "Oldschool";
	if(str_find_nocase(pText, "Novice"))
		return "Novice";
	if(str_find_nocase(pText, "Moderate"))
		return "Moderate";
	if(str_find_nocase(pText, "Brutal"))
		return "Brutal";
	if(str_find_nocase(pText, "Insane"))
		return "Insane";
	if(str_find_nocase(pText, "Dummy"))
		return "Dummy";
	if(str_find_nocase(pText, "Solo"))
		return "Solo";
	if(str_find_nocase(pText, "Race"))
		return "Race";
	if(str_find_nocase(pText, "Fun"))
		return "Fun";
	if(str_find_nocase(pText, "Event"))
		return "Event";
	return nullptr;
}

static const char *FavoriteMapCategoryDisplayName(const char *pType)
{
	if(!pType || pType[0] == '\0')
		return Localize("Unknown");
	if(str_comp_nocase(pType, "DDmaX Easy") == 0)
		return Localize("DDmaX Easy");
	if(str_comp_nocase(pType, "DDmaX Next") == 0)
		return Localize("DDmaX Next");
	if(str_comp_nocase(pType, "DDmaX Pro") == 0)
		return Localize("DDmaX Pro");
	if(str_comp_nocase(pType, "DDmaX Nut") == 0)
		return Localize("DDmaX Nut");
	if(str_comp_nocase(pType, "DDmaX") == 0)
		return Localize("DDmaX");
	if(str_comp_nocase(pType, "Novice") == 0)
		return Localize("Novice");
	if(str_comp_nocase(pType, "Moderate") == 0)
		return Localize("Moderate");
	if(str_comp_nocase(pType, "Brutal") == 0)
		return Localize("Brutal");
	if(str_comp_nocase(pType, "Insane") == 0)
		return Localize("Insane");
	if(str_comp_nocase(pType, "Dummy") == 0)
		return Localize("Dummy");
	if(str_comp_nocase(pType, "Solo") == 0)
		return Localize("Solo");
	if(str_comp_nocase(pType, "Oldschool") == 0)
		return Localize("Oldschool");
	if(str_comp_nocase(pType, "Race") == 0)
		return Localize("Race");
	if(str_comp_nocase(pType, "Fun") == 0)
		return Localize("Fun");
	if(str_comp_nocase(pType, "Event") == 0)
		return Localize("Event");
	return Localize("Unknown");
}

static bool TryParseVoteMapDifficulty(const char *pDescription, const char *pMapName, char *pOut, int OutSize)
{
	if(!pDescription || !pMapName || pMapName[0] == '\0')
		return false;

	const char *pBy = str_find_nocase(pDescription, " by ");
	const char *pStars = str_find(pDescription, "/5");
	if(!pBy || !pStars || pStars <= pDescription)
		return false;

	const int MapNameLength = (int)(pBy - pDescription);
	if((int)str_length(pMapName) != MapNameLength || str_comp_nocase_num(pDescription, pMapName, MapNameLength) != 0)
		return false;

	const char *pStarNumber = pStars;
	while(pStarNumber > pDescription && pStarNumber[-1] >= '0' && pStarNumber[-1] <= '9')
		--pStarNumber;
	if(pStarNumber == pStars)
		return false;

	char aStars[8];
	const int StarNumberLength = minimum((int)(pStars - pStarNumber), (int)sizeof(aStars) - 1);
	str_copy(aStars, pStarNumber, StarNumberLength + 1);
	const int Stars = str_toint(aStars);
	if(Stars < 0 || Stars > 5)
		return false;

	str_format(pOut, OutSize, "%d/5 ★", Stars);
	return true;
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct SLocalSaveDisplayEntry
{
	std::string m_Time;
	std::string m_Players;
	std::string m_Map;
	std::string m_Code;
	std::string m_RawLine;
};

static void TrimDisplayField(std::string &Field)
{
	while(!Field.empty() && str_isspace(Field.front()))
		Field.erase(Field.begin());
	while(!Field.empty() && str_isspace(Field.back()))
		Field.pop_back();
}

static std::array<std::string, 4> ParseSaveCsvFields(const char *pLine)
{
	std::array<std::string, 4> aFields;
	int FieldIndex = 0;
	bool InQuotes = false;

	for(int CharIndex = 0; pLine[CharIndex] != '\0' && FieldIndex < (int)aFields.size(); ++CharIndex)
	{
		if(pLine[CharIndex] == '"')
		{
			if(InQuotes && pLine[CharIndex + 1] == '"')
			{
				aFields[FieldIndex].push_back('"');
				++CharIndex;
			}
			else
			{
				InQuotes = !InQuotes;
			}
		}
		else if(pLine[CharIndex] == ',' && !InQuotes)
		{
			++FieldIndex;
		}
		else
		{
			aFields[FieldIndex].push_back(pLine[CharIndex]);
		}
	}

	for(std::string &Field : aFields)
		TrimDisplayField(Field);
	return aFields;
}

static std::vector<SLocalSaveDisplayEntry> LoadLocalSaveDisplayEntries(IStorage *pStorage, bool &FileExists)
{
	FileExists = false;
	std::vector<SLocalSaveDisplayEntry> vEntries;
	IOHANDLE File = pStorage->OpenFile(SAVES_FILE, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return vEntries;
	FileExists = true;

	char *pFileContent = io_read_all_str(File);
	io_close(File);
	if(!pFileContent)
		return vEntries;

	const char *pCursor = pFileContent;
	char aLine[2048];
	bool FirstLine = true;
	while((pCursor = str_next_token(pCursor, "\n", aLine, sizeof(aLine))))
	{
		str_utf8_trim_right(aLine);
		if(aLine[0] == '\0')
			continue;
		if(FirstLine)
		{
			FirstLine = false;
			if(str_startswith(aLine, "Time"))
				continue;
		}

		std::array<std::string, 4> aFields = ParseSaveCsvFields(aLine);
		SLocalSaveDisplayEntry Entry;
		Entry.m_Time = aFields[0];
		Entry.m_Players = aFields[1];
		Entry.m_Map = aFields[2];
		Entry.m_Code = aFields[3];
		Entry.m_RawLine = aLine;
		vEntries.push_back(std::move(Entry));
	}

	free(pFileContent);
	return vEntries;
}

static const CServerInfo *FindSortedServerByAddress(IServerBrowser *pServerBrowser, const char *pAddress, int *pIndex = nullptr)
{
	if(pIndex != nullptr)
		*pIndex = -1;
	if(pServerBrowser == nullptr || pAddress == nullptr || pAddress[0] == '\0')
		return nullptr;

	for(int i = 0; i < pServerBrowser->NumSortedServers(); ++i)
	{
		const CServerInfo *pServerInfo = pServerBrowser->SortedGet(i);
		if(pServerInfo != nullptr && str_comp(pServerInfo->m_aAddress, pAddress) == 0)
		{
			if(pIndex != nullptr)
				*pIndex = i;
			return pServerInfo;
		}
	}

	return nullptr;
}

static const CServerInfo *FindServerByAddress(IServerBrowser *pServerBrowser, const char *pAddress)
{
	if(pServerBrowser == nullptr || pAddress == nullptr || pAddress[0] == '\0')
		return nullptr;

	for(int i = 0; i < pServerBrowser->NumServers(); ++i)
	{
		const CServerInfo *pServerInfo = pServerBrowser->Get(i);
		if(pServerInfo != nullptr && str_comp(pServerInfo->m_aAddress, pAddress) == 0)
			return pServerInfo;
	}

	return nullptr;
}

static bool IsClanMembersCategory(const char *pCategory)
{
	return pCategory != nullptr && str_comp_nocase(pCategory, IFriends::CLAN_MEMBERS_CATEGORY) == 0;
}

static bool IsOfflineFriendsCategory(const char *pCategory)
{
	return pCategory != nullptr && str_comp_nocase(pCategory, IFriends::OFFLINE_CATEGORY) == 0;
}

static bool IsProtectedFriendsCategory(const char *pCategory)
{
	return pCategory != nullptr && (str_comp_nocase(pCategory, IFriends::DEFAULT_CATEGORY) == 0 || IsClanMembersCategory(pCategory) || IsOfflineFriendsCategory(pCategory));
}

static const char *LocalizeFriendsCategory(const char *pCategory)
{
	if(pCategory == nullptr || pCategory[0] == '\0')
		return "";
	if(str_comp_nocase(pCategory, IFriends::DEFAULT_CATEGORY) == 0)
		return Localize("Friends");
	if(IsClanMembersCategory(pCategory))
		return Localize("Clan Members");
	if(IsOfflineFriendsCategory(pCategory))
		return Localize("Offline");
	return pCategory;
}

static ColorRGBA PlayerBackgroundColor(bool Friend, bool Clan, bool Afk, bool InSelectedServer, bool Inside)
{
	const ColorRGBA FriendsColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClFriendsListFriendColor));
	const ColorRGBA ClanColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClFriendsListClanColor));
	const ColorRGBA NeutralColor = ColorRGBA(0.75f, 0.75f, 0.75f);
	const ColorRGBA COLORS[] = {FriendsColor, ClanColor, NeutralColor};
	static const ColorRGBA COLORS_AFK[] = {ColorRGBA(1.0f, 1.0f, 0.5f), ColorRGBA(1.0f, 0.98f, 0.65f), ColorRGBA(0.6f, 0.6f, 0.6f)};
	int i;
	if(Friend)
		i = 0;
	else if(Clan)
		i = 1;
	else
		i = 2;
	return (Afk ? COLORS_AFK[i] : COLORS[i]).WithAlpha(0.3f + (Inside ? 0.15f : 0.0f) + (InSelectedServer ? 0.12f : 0.0f));
}

template<size_t N>
static void FormatServerbrowserPing(char (&aBuffer)[N], const CServerInfo *pInfo)
{
	if(!pInfo->m_LatencyIsEstimated)
	{
		str_format(aBuffer, sizeof(aBuffer), "%d", pInfo->m_Latency);
		return;
	}
	static const char *const LOCATION_NAMES[CServerInfo::NUM_LOCS] = {
		"", // LOC_UNKNOWN
		Localizable("AFR"), // LOC_AFRICA
		Localizable("ASI"), // LOC_ASIA
		Localizable("AUS"), // LOC_AUSTRALIA
		Localizable("EUR"), // LOC_EUROPE
		Localizable("NA"), // LOC_NORTH_AMERICA
		Localizable("SA"), // LOC_SOUTH_AMERICA
		Localizable("CHN"), // LOC_CHINA
	};
	if(pInfo->m_Location < 0 || pInfo->m_Location >= CServerInfo::NUM_LOCS)
	{
		log_warn("serverbrowser", "Invalid estimated ping location %d", pInfo->m_Location);
		str_copy(aBuffer, Localize(LOCATION_NAMES[CServerInfo::LOC_UNKNOWN]));
		return;
	}
	str_copy(aBuffer, Localize(LOCATION_NAMES[pInfo->m_Location]));
}

static ColorRGBA GetPingTextColor(int Latency)
{
	return color_cast<ColorRGBA>(ColorHSLA((300.0f - std::clamp(Latency, 0, 300)) / 1000.0f, 1.0f, 0.5f));
}

void CMenus::RenderServerbrowserServerList(CUIRect View, bool &WasListboxItemActivated)
{
	static CListBox s_ListBox;

	CUIRect Headers;
	View.HSplitTop(ms_ListheaderHeight, &Headers, &View);
	const CUIRect ListView = View;
	Headers.Draw(BrowserOpacityColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f)), IGraphics::CORNER_T, 5.0f);
	Headers.VSplitRight(s_ListBox.ScrollbarWidthMax(), &Headers, nullptr);
	View.Draw(BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f)), IGraphics::CORNER_NONE, 0.0f);

	{
		CUIRect ResetBtn;
		Headers.VSplitLeft(ms_ListheaderHeight, &ResetBtn, &Headers);
		ResetBtn.Margin(3.0f, &ResetBtn);
		static CButtonContainer s_ResetColsButton;
		if(Ui()->DoButton_FontIcon(&s_ResetColsButton, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &ResetBtn, BUTTONFLAG_LEFT))
		{
			g_Config.m_BrColWidthName = 120;
			g_Config.m_BrColWidthGametype = 50;
			g_Config.m_BrColWidthMap = 120;
			g_Config.m_BrColWidthFriends = 20;
			g_Config.m_BrColWidthPlayers = 60;
			g_Config.m_BrColWidthPing = 40;
			ConfigManager()->Save();
		}
		if(Ui()->HotItem() == &s_ResetColsButton)
			GameClient()->m_Tooltips.DoToolTip(&s_ResetColsButton, &ResetBtn, Localize("Reset column widths"));
	}

	struct SColumn
	{
		int m_Id;
		int m_Sort;
		const char *m_pCaption;
		int m_Direction;
		float m_Width;
		CUIRect m_Rect;
	};

	enum
	{
		COL_FLAG_LOCK = 0,
		COL_FLAG_FAV,
		COL_COMMUNITY,
		COL_NAME,
		COL_GAMETYPE,
		COL_MAP,
		COL_FRIENDS,
		COL_PLAYERS,
		COL_PING,
	};

	enum
	{
		UI_ELEM_LOCK_ICON = 0,
		UI_ELEM_FAVORITE_ICON,
		UI_ELEM_NAME_1,
		UI_ELEM_NAME_2,
		UI_ELEM_NAME_3,
		UI_ELEM_GAMETYPE,
		UI_ELEM_MAP_1,
		UI_ELEM_MAP_2,
		UI_ELEM_MAP_3,
		UI_ELEM_FINISH_ICON,
		UI_ELEM_PLAYERS,
		UI_ELEM_FRIEND_ICON,
		UI_ELEM_PING,
		UI_ELEM_KEY_ICON,
		NUM_UI_ELEMS,
	};

	constexpr float ClickableIconSpace = 20.0f;

	static SColumn s_aCols[] = {
		{-1, -1, "", -1, 2.0f, {0}},
		{COL_FLAG_LOCK, -1, "", -1, 14.0f, {0}},
		{COL_FLAG_FAV, IServerBrowser::SORT_FAVORITES, "", -1, ClickableIconSpace, {0}},
		{COL_COMMUNITY, -1, "", -1, 28.0f, {0}},
		{COL_NAME, IServerBrowser::SORT_NAME, Localizable("Name"), 0, 50.0f, {0}},
		{COL_GAMETYPE, IServerBrowser::SORT_GAMETYPE, Localizable("Type"), 1, 50.0f, {0}},
		{COL_MAP, IServerBrowser::SORT_MAP, Localizable("Map"), 1, 120.0f + (Headers.w - 480) / 8, {0}},
		{COL_FRIENDS, IServerBrowser::SORT_NUMFRIENDS, "", 1, ClickableIconSpace, {0}},
		{COL_PLAYERS, IServerBrowser::SORT_NUMPLAYERS, Localizable("Players"), 1, 60.0f, {0}},
		{-1, -1, "", 1, 4.0f, {0}},
		{COL_PING, IServerBrowser::SORT_PING, Localizable("Ping"), 1, 40.0f, {0}},
	};

	auto ClampConfigWidth = [](int Value, int MinWidth, int MaxWidth) {
		return std::clamp(Value, MinWidth, MaxWidth);
	};

	s_aCols[5].m_Width = (float)ClampConfigWidth(g_Config.m_BrColWidthGametype, 36, 300);
	s_aCols[6].m_Width = (float)ClampConfigWidth(g_Config.m_BrColWidthMap, 60, 800);
	s_aCols[7].m_Width = (float)ClampConfigWidth(g_Config.m_BrColWidthFriends, 18, 120);
	s_aCols[8].m_Width = (float)ClampConfigWidth(g_Config.m_BrColWidthPlayers, 48, 240);
	s_aCols[10].m_Width = (float)ClampConfigWidth(g_Config.m_BrColWidthPing, 32, 180);

	const int NumCols = std::size(s_aCols);

	auto GetColWidthConfig = [](int ColId) -> int * {
		switch(ColId)
		{
		case COL_GAMETYPE: return &g_Config.m_BrColWidthGametype;
		case COL_MAP: return &g_Config.m_BrColWidthMap;
		case COL_FRIENDS: return &g_Config.m_BrColWidthFriends;
		case COL_PLAYERS: return &g_Config.m_BrColWidthPlayers;
		case COL_PING: return &g_Config.m_BrColWidthPing;
		default: return nullptr;
		}
	};

	auto GetColMinWidth = [](int ColId) {
		switch(ColId)
		{
		case COL_GAMETYPE: return 36.0f;
		case COL_MAP: return 60.0f;
		case COL_FRIENDS: return 18.0f;
		case COL_PLAYERS: return 48.0f;
		case COL_PING: return 32.0f;
		default: return 10.0f;
		}
	};

	auto GetColMaxWidth = [](int ColId) {
		switch(ColId)
		{
		case COL_GAMETYPE: return 300.0f;
		case COL_MAP: return 800.0f;
		case COL_FRIENDS: return 120.0f;
		case COL_PLAYERS: return 240.0f;
		case COL_PING: return 180.0f;
		default: return 1000.0f;
		}
	};

	auto SetColWidthConfig = [](int *pConfig, float Width, float MinWidth, float MaxWidth) {
		*pConfig = std::clamp((int)(Width + 0.5f), (int)MinWidth, (int)MaxWidth);
	};

	struct SResizeHandle
	{
		CUIRect m_Rect;
		int m_ColIndex;
		int *m_pWidthConfig;
		float m_MinWidth;
		float m_MaxWidth;
	};

	static std::vector<SResizeHandle> s_vResizeHandles;
	s_vResizeHandles.clear();

	// do layout - left columns
	for(int i = 0; i < NumCols; i++)
	{
		if(s_aCols[i].m_Direction == -1)
		{
			Headers.VSplitLeft(s_aCols[i].m_Width, &s_aCols[i].m_Rect, &Headers);

			if(i + 1 < NumCols)
			{
				CUIRect Gap;
				Headers.VSplitLeft(2.0f, &Gap, &Headers);
				int *pConfig = GetColWidthConfig(s_aCols[i].m_Id);
				if(pConfig)
				{
					Gap.x -= 3.0f;
					Gap.w = 8.0f;
					s_vResizeHandles.push_back({Gap, i, pConfig, GetColMinWidth(s_aCols[i].m_Id), GetColMaxWidth(s_aCols[i].m_Id)});
				}
			}
		}
	}

	for(int i = NumCols - 1; i >= 0; i--)
	{
		if(s_aCols[i].m_Direction == 1)
		{
			Headers.VSplitRight(s_aCols[i].m_Width, &Headers, &s_aCols[i].m_Rect);
			CUIRect Gap;
			Headers.VSplitRight(2.0f, &Headers, &Gap);
			int *pConfig = GetColWidthConfig(s_aCols[i].m_Id);
			if(pConfig)
			{
				Gap.x -= 3.0f;
				Gap.w = 8.0f;
				s_vResizeHandles.push_back({Gap, i, pConfig, GetColMinWidth(s_aCols[i].m_Id), GetColMaxWidth(s_aCols[i].m_Id)});
			}
		}
	}

	for(auto &Col : s_aCols)
	{
		if(Col.m_Direction == 0)
			Col.m_Rect = Headers;
	}

	const bool PlayersOrPing = (g_Config.m_BrSort == IServerBrowser::SORT_NUMPLAYERS || g_Config.m_BrSort == IServerBrowser::SORT_PING);

	// do headers
	for(const auto &Col : s_aCols)
	{
		int Checked = g_Config.m_BrSort == Col.m_Sort;
		if(PlayersOrPing && g_Config.m_BrSortOrder == 2 && (Col.m_Sort == IServerBrowser::SORT_NUMPLAYERS || Col.m_Sort == IServerBrowser::SORT_PING))
			Checked = 2;

		if(DoButton_GridHeader(&Col.m_Id, Localize(Col.m_pCaption), Checked, &Col.m_Rect))
		{
			if(Col.m_Sort != -1)
			{
				if(g_Config.m_BrSort == Col.m_Sort)
					g_Config.m_BrSortOrder = (g_Config.m_BrSortOrder + 1) % (PlayersOrPing ? 3 : 2);
				else
					g_Config.m_BrSortOrder = 0;
				g_Config.m_BrSort = Col.m_Sort;
			}
		}

		if(Col.m_Id == COL_FRIENDS)
		{
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			Ui()->DoLabel(&Col.m_Rect, FONT_ICON_HEART, 14.0f, TEXTALIGN_MC);
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
		else if(Col.m_Id == COL_FLAG_FAV)
		{
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			Ui()->DoLabel(&Col.m_Rect, FONT_ICON_STAR, 14.0f, TEXTALIGN_MC);
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
	}

	static int s_ResizeDragColIndex = -1;
	static float s_ResizeDragStartMouseX = 0.0f;
	static float s_ResizeDragStartWidth = 0.0f;
	static float s_ResizeDragStartFlexWidth = 0.0f;
	static float s_ResizeDragCurrentWidth = 0.0f;

	const float MinNameWidth = (float)ClampConfigWidth(g_Config.m_BrColWidthName, 60, 1000);

	const int FlexColIndex = [&]() {
		for(int i = 0; i < NumCols; i++)
			if(s_aCols[i].m_Direction == 0)
				return i;
		return -1;
	}();

	for(const auto &Handle : s_vResizeHandles)
	{
		const void *pHandleId = &s_aCols[Handle.m_ColIndex].m_Width;
		const int ColIdx = Handle.m_ColIndex;
		const bool IsRightCol = s_aCols[ColIdx].m_Direction == 1;

		if(s_ResizeDragColIndex == ColIdx)
		{
			if(!Ui()->MouseButton(0))
			{
				SetColWidthConfig(Handle.m_pWidthConfig, s_ResizeDragCurrentWidth, Handle.m_MinWidth, Handle.m_MaxWidth);
				s_ResizeDragColIndex = -1;
				Ui()->SetActiveItem(nullptr);
				ConfigManager()->Save();
			}
			else
			{
				float DeltaX = Ui()->MouseX() - s_ResizeDragStartMouseX;
				float NewWidth = s_ResizeDragStartWidth + (IsRightCol ? -DeltaX : DeltaX);
				NewWidth = std::clamp(NewWidth, Handle.m_MinWidth, Handle.m_MaxWidth);

				if(IsRightCol && FlexColIndex >= 0)
				{
					float MaxWidth = s_ResizeDragStartWidth + s_ResizeDragStartFlexWidth - MinNameWidth;
					NewWidth = minimum(NewWidth, MaxWidth);
				}
				NewWidth = maximum(NewWidth, Handle.m_MinWidth);
				s_ResizeDragCurrentWidth = NewWidth;
				SetColWidthConfig(Handle.m_pWidthConfig, NewWidth, Handle.m_MinWidth, Handle.m_MaxWidth);
			}
		}
		else if(Ui()->MouseHovered(&Handle.m_Rect))
		{
			Ui()->SetHotItem(pHandleId);
			if(Ui()->MouseButtonClicked(0))
			{
				s_ResizeDragColIndex = ColIdx;
				s_ResizeDragStartMouseX = Ui()->MouseX();
				s_ResizeDragStartWidth = s_aCols[ColIdx].m_Width;
				s_ResizeDragStartFlexWidth = FlexColIndex >= 0 ? s_aCols[FlexColIndex].m_Rect.w : 0.0f;
				s_ResizeDragCurrentWidth = s_ResizeDragStartWidth;
				Ui()->SetActiveItem(pHandleId);
			}
		}
	}

	for(const auto &Handle : s_vResizeHandles)
	{
		const int ColIdx = Handle.m_ColIndex;
		const bool IsRightCol = s_aCols[ColIdx].m_Direction == 1;
		bool Hovered = Ui()->HotItem() == &s_aCols[ColIdx].m_Width;
		bool Dragging = s_ResizeDragColIndex == ColIdx;

		float LineX = IsRightCol ? s_aCols[ColIdx].m_Rect.x - 1.0f : s_aCols[ColIdx].m_Rect.x + s_aCols[ColIdx].m_Rect.w + 1.0f;

		float Alpha = 0.15f;
		float LineW = 1.0f;
		if(Hovered)
		{
			Alpha = 0.5f;
			LineW = 2.0f;
		}
		if(Dragging)
		{
			Alpha = 1.0f;
			LineW = 2.0f;
		}

		CUIRect Line;
		Line.x = LineX;
		Line.y = s_aCols[ColIdx].m_Rect.y;
		Line.w = LineW;
		Line.h = s_aCols[ColIdx].m_Rect.h;
		Line.Draw(BrowserOpacityColor(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha)), IGraphics::CORNER_NONE, 0.0f);
	}

	const int NumServers = ServerBrowser()->NumSortedServers();

	// display important messages in the middle of the screen so no
	// users misses it
	{
		if(!ServerBrowser()->NumServers() && ServerBrowser()->IsGettingServerlist())
		{
			char aLoadingLabel[256];
			const int LoadingDotsCount = static_cast<int>(Client()->GlobalTime() * 3.0f) % 7;
			str_format(aLoadingLabel, sizeof(aLoadingLabel), "%s%.*s", Localize("Getting server list from master server"), LoadingDotsCount, "......");
			Ui()->DoLabel(&View, aLoadingLabel, 16.0f, TEXTALIGN_MC);
		}
		else if(!ServerBrowser()->NumServers())
		{
			if(ServerBrowser()->GetCurrentType() == IServerBrowser::TYPE_LAN)
			{
				CUIRect Label, Button;
				View.HMargin((View.h - (16.0f + 18.0f + 8.0f)) / 2.0f, &Label);
				Label.HSplitTop(16.0f, &Label, &Button);
				Button.HSplitTop(8.0f, nullptr, &Button);
				Button.VMargin((Button.w - 320.0f) / 2.0f, &Button);
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), Localize("No local servers found (ports %d-%d)"), IServerBrowser::LAN_PORT_BEGIN, IServerBrowser::LAN_PORT_END);
				Ui()->DoLabel(&Label, aBuf, 16.0f, TEXTALIGN_MC);
				static CButtonContainer s_StartLocalServerButton;
				if(DoButton_Menu(&s_StartLocalServerButton, Localize("Start and connect to local server"), 0, &Button))
				{
					if(GameClient()->m_LocalServer.IsServerRunning())
					{
						RefreshBrowserTab(true);
						Connect("localhost");
					}
					else if(GameClient()->m_LocalServer.RunServer({}))
					{
						Connect("localhost");
					}
				}
			}
			else if(ServerBrowser()->IsServerlistError())
			{
				Ui()->DoLabel(&View, Localize("Could not get server list from master server"), 16.0f, TEXTALIGN_MC);
			}
			else
			{
				Ui()->DoLabel(&View, Localize("No servers found"), 16.0f, TEXTALIGN_MC);
			}
		}
		else if(ServerBrowser()->NumServers() && !NumServers)
		{
			CUIRect Label, ResetButton;
			View.HMargin((View.h - (16.0f + 18.0f + 8.0f)) / 2.0f, &Label);
			Label.HSplitTop(16.0f, &Label, &ResetButton);
			ResetButton.HSplitTop(8.0f, nullptr, &ResetButton);
			ResetButton.VMargin((ResetButton.w - 200.0f) / 2.0f, &ResetButton);
			Ui()->DoLabel(&Label, Localize("No servers match your filter criteria"), 16.0f, TEXTALIGN_MC);
			static CButtonContainer s_ResetButton;
			if(DoButton_Menu(&s_ResetButton, Localize("Reset filter"), 0, &ResetButton))
			{
				ResetServerbrowserFilters();
			}
		}
	}

	s_ListBox.SetActive(!Ui()->IsPopupOpen());
	const bool PerfListFrameEnabled = QmPerfEnabled();
	const auto ListFrameStartTime = PerfListFrameEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
	s_ListBox.DoStart(ms_ListheaderHeight, NumServers, 1, 3, -1, &View, false);

	if(m_ServerBrowserShouldRevealSelection)
	{
		s_ListBox.ScrollToSelected();
		m_ServerBrowserShouldRevealSelection = false;
	}
	m_SelectedIndex = -1;

	const auto &&RenderBrowserIcons = [this](CUIElement::SUIElementRect &UIRect, CUIRect *pRect, const ColorRGBA &TextColor, const ColorRGBA &TextOutlineColor, const char *pText, int TextAlign, bool SmallFont = false) {
		const float FontSize = SmallFont ? 6.0f : 14.0f;
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		TextRender()->TextColor(TextColor);
		TextRender()->TextOutlineColor(TextOutlineColor);
		Ui()->DoLabelStreamed(UIRect, pRect, pText, FontSize, TextAlign);
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	};

	std::vector<CUIElement *> &vpServerBrowserUiElements = m_avpServerBrowserUiElements[ServerBrowser()->GetCurrentType()];
	if(vpServerBrowserUiElements.size() < (size_t)NumServers)
		vpServerBrowserUiElements.resize(NumServers, nullptr);

	int RowsVisible = 0;
	int RowsRendered = 0;
	int RowsIterated = 0;
	for(int i = 0; i < NumServers; i++)
	{
		const CServerInfo *pItem = ServerBrowser()->SortedGet(i);
		RowsIterated += PerfListFrameEnabled ? 1 : 0;
		const CCommunity *pCommunity = ServerBrowser()->Community(pItem->m_aCommunityId);

		if(vpServerBrowserUiElements[i] == nullptr)
		{
			vpServerBrowserUiElements[i] = Ui()->GetNewUIElement(NUM_UI_ELEMS);
		}
		CUIElement *pUiElement = vpServerBrowserUiElements[i];

		const CListboxItem ListItem = s_ListBox.DoNextItem(pItem, str_comp(pItem->m_aAddress, g_Config.m_UiServerAddress) == 0);
		if(ListItem.m_Selected)
			m_SelectedIndex = i;

		if(!ListItem.m_Visible)
		{
			// reset active item, if not visible
			if(Ui()->CheckActiveItem(pItem))
				Ui()->SetActiveItem(nullptr);

			// don't render invisible items
			continue;
		}
		if(PerfListFrameEnabled)
		{
			RowsVisible++;
			RowsRendered++;
		}

		const float FontSize = 12.0f;
		char aTemp[64];
		for(const auto &Col : s_aCols)
		{
			CUIRect Button;
			Button.x = Col.m_Rect.x;
			Button.y = ListItem.m_Rect.y;
			Button.h = ListItem.m_Rect.h;
			Button.w = Col.m_Rect.w;

			const int Id = Col.m_Id;
			if(Id == COL_FLAG_LOCK)
			{
				if(pItem->m_Flags & SERVER_FLAG_PASSWORD)
				{
					RenderBrowserIcons(*pUiElement->Rect(UI_ELEM_LOCK_ICON), &Button, ColorRGBA(0.75f, 0.75f, 0.75f, 1.0f), TextRender()->DefaultTextOutlineColor(), FONT_ICON_LOCK, TEXTALIGN_MC);
				}
				else if(pItem->m_RequiresLogin)
				{
					RenderBrowserIcons(*pUiElement->Rect(UI_ELEM_KEY_ICON), &Button, ColorRGBA(0.75f, 0.75f, 0.75f, 1.0f), TextRender()->DefaultTextOutlineColor(), FONT_ICON_KEY, TEXTALIGN_MC);
				}
			}
			else if(Id == COL_FLAG_FAV)
			{
				if(pItem->m_Favorite != TRISTATE::NONE)
				{
					RenderBrowserIcons(*pUiElement->Rect(UI_ELEM_FAVORITE_ICON), &Button, ColorRGBA(1.0f, 0.85f, 0.3f, 1.0f), TextRender()->DefaultTextOutlineColor(), FONT_ICON_STAR, TEXTALIGN_MC);
				}
			}
			else if(Id == COL_COMMUNITY)
			{
				if(pCommunity != nullptr)
				{
					const CCommunityIcon *pIcon = m_CommunityIcons.Find(pCommunity->Id());
					if(pIcon != nullptr)
					{
						CUIRect CommunityIcon;
						Button.Margin(2.0f, &CommunityIcon);
						m_CommunityIcons.Render(pIcon, CommunityIcon, true);
						Ui()->DoButtonLogic(&pItem->m_aCommunityId, 0, &CommunityIcon, BUTTONFLAG_NONE);
						GameClient()->m_Tooltips.DoToolTip(&pItem->m_aCommunityId, &CommunityIcon, pCommunity->Name());
					}
				}
			}
			else if(Id == COL_NAME)
			{
				SLabelProperties Props;
				Props.m_MaxWidth = Button.w;
				Props.m_StopAtEnd = true;
				Props.m_EnableWidthCheck = false;
				char aDisplayServerName[sizeof(pItem->m_aName)];
				const char *pDisplayServerName = g_Config.m_QmShortServerNames ? CMenus::GetServerbrowserDisplayName(pItem, aDisplayServerName, sizeof(aDisplayServerName)) : pItem->m_aName;
				bool Printed = false;
				if(g_Config.m_BrFilterString[0] && (g_Config.m_QmShortServerNames || (pItem->m_QuickSearchHit & IServerBrowser::QUICK_SERVERNAME)))
					Printed = PrintHighlighted(pDisplayServerName, [&](const char *pFilteredStr, const int FilterLen) {
						Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_NAME_1), &Button, pDisplayServerName, FontSize, TEXTALIGN_ML, Props, (int)(pFilteredStr - pDisplayServerName));
						TextRender()->TextColor(gs_HighlightedTextColor);
						Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_NAME_2), &Button, pFilteredStr, FontSize, TEXTALIGN_ML, Props, FilterLen, &pUiElement->Rect(UI_ELEM_NAME_1)->m_Cursor);
						TextRender()->TextColor(TextRender()->DefaultTextColor());
						Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_NAME_3), &Button, pFilteredStr + FilterLen, FontSize, TEXTALIGN_ML, Props, -1, &pUiElement->Rect(UI_ELEM_NAME_2)->m_Cursor);
					});
				if(!Printed)
					Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_NAME_1), &Button, pDisplayServerName, FontSize, TEXTALIGN_ML, Props);
			}
			else if(Id == COL_GAMETYPE)
			{
				SLabelProperties Props;
				Props.m_MaxWidth = Button.w;
				Props.m_StopAtEnd = true;
				Props.m_EnableWidthCheck = false;
				if(g_Config.m_UiColorizeGametype)
				{
					TextRender()->TextColor(pItem->m_GametypeColor);
				}
				Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_GAMETYPE), &Button, pItem->m_aGameType, FontSize, TEXTALIGN_ML, Props);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			else if(Id == COL_MAP)
			{
				{
					CUIRect Icon;
					Button.VMargin(4.0f, &Button);
					Button.VSplitLeft(Button.h, &Icon, &Button);
					if(g_Config.m_BrIndicateFinished && pItem->m_HasRank == CServerInfo::RANK_RANKED)
					{
						Icon.Margin(2.0f, &Icon);
						RenderBrowserIcons(*pUiElement->Rect(UI_ELEM_FINISH_ICON), &Icon, TextRender()->DefaultTextColor(), TextRender()->DefaultTextOutlineColor(), FONT_ICON_FLAG_CHECKERED, TEXTALIGN_MC);
					}
				}

				// 检查是否是收藏地图
				const bool IsFavoriteMap = GameClient()->m_TClient.IsFavoriteMap(pItem->m_aMap);
				if(IsFavoriteMap)
					TextRender()->TextColor(1.0f, 0.85f, 0.0f, 1.0f); // 金色

				SLabelProperties Props;
				Props.m_MaxWidth = Button.w;
				Props.m_StopAtEnd = true;
				Props.m_EnableWidthCheck = false;
				bool Printed = false;
				if(g_Config.m_BrFilterString[0] && (pItem->m_QuickSearchHit & IServerBrowser::QUICK_MAPNAME))
					Printed = PrintHighlighted(pItem->m_aMap, [&](const char *pFilteredStr, const int FilterLen) {
						Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_MAP_1), &Button, pItem->m_aMap, FontSize, TEXTALIGN_ML, Props, (int)(pFilteredStr - pItem->m_aMap));
						TextRender()->TextColor(gs_HighlightedTextColor);
						Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_MAP_2), &Button, pFilteredStr, FontSize, TEXTALIGN_ML, Props, FilterLen, &pUiElement->Rect(UI_ELEM_MAP_1)->m_Cursor);
						TextRender()->TextColor(TextRender()->DefaultTextColor());
						Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_MAP_3), &Button, pFilteredStr + FilterLen, FontSize, TEXTALIGN_ML, Props, -1, &pUiElement->Rect(UI_ELEM_MAP_2)->m_Cursor);
					});
				if(!Printed)
					Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_MAP_1), &Button, pItem->m_aMap, FontSize, TEXTALIGN_ML, Props);

				const char *pMapNote = GameClient()->m_TClient.GetMapNote(pItem->m_aMap);
				if(pMapNote && pMapNote[0] != '\0' && Ui()->MouseHovered(&Button))
				{
					static char s_aMapNoteTooltip[512];
					str_format(s_aMapNoteTooltip, sizeof(s_aMapNoteTooltip), "%s: %s", Localize("Note"), pMapNote);
					Ui()->DoButtonLogic(&pItem->m_aMap, 0, &Button, BUTTONFLAG_NONE);
					GameClient()->m_Tooltips.DoToolTip(&pItem->m_aMap, &Button, s_aMapNoteTooltip, 320.0f);
				}

				if(IsFavoriteMap)
					TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			else if(Id == COL_FRIENDS)
			{
				if(pItem->m_FriendState != IFriends::FRIEND_NO)
				{
					RenderBrowserIcons(*pUiElement->Rect(UI_ELEM_FRIEND_ICON), &Button, ColorRGBA(0.94f, 0.4f, 0.4f, 1.0f), TextRender()->DefaultTextOutlineColor(), FONT_ICON_HEART, TEXTALIGN_MC);

					if(pItem->m_FriendNum > 1)
					{
						str_format(aTemp, sizeof(aTemp), "%d", pItem->m_FriendNum);
						TextRender()->TextColor(0.94f, 0.8f, 0.8f, 1.0f);
						Ui()->DoLabel(&Button, aTemp, 9.0f, TEXTALIGN_MC);
						TextRender()->TextColor(TextRender()->DefaultTextColor());
					}
				}
			}
			else if(Id == COL_PLAYERS)
			{
				str_format(aTemp, sizeof(aTemp), "%i/%i", pItem->m_NumFilteredPlayers, ServerBrowser()->Max(*pItem));
				if(g_Config.m_BrFilterString[0] && (pItem->m_QuickSearchHit & IServerBrowser::QUICK_PLAYER))
				{
					TextRender()->TextColor(gs_HighlightedTextColor);
				}
				Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_PLAYERS), &Button, aTemp, FontSize, TEXTALIGN_MR);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			else if(Id == COL_PING)
			{
				Button.VMargin(4.0f, &Button);
				FormatServerbrowserPing(aTemp, pItem);
				if(g_Config.m_UiColorizePing)
				{
					TextRender()->TextColor(GetPingTextColor(pItem->m_Latency));
				}
				Ui()->DoLabelStreamed(*pUiElement->Rect(UI_ELEM_PING), &Button, aTemp, FontSize, TEXTALIGN_MR);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
		}
	}

	const int NewSelected = s_ListBox.DoEnd();
	const bool ListScrollActive = QmMenuUiScrollPerfActive(s_ListBox.WheelConsumedThisFrame(), s_ListBox.ScrollbarActive(), s_ListBox.ScrollbarAnimating());
	const double ListFrameDurationMs = PerfListFrameEnabled ? std::chrono::duration<double, std::milli>(time_get_nanoseconds() - ListFrameStartTime).count() : -1.0;
	if(PerfListFrameEnabled)
	{
		if(QmPerfShouldLogDuration(ListFrameDurationMs, false))
		{
			char aPayload[256];
			const int ItemsTotal = ServerBrowser()->NumSortedServers();
			const int RowsSkipped = maximum(0, ItemsTotal - RowsRendered);
			str_format(aPayload, sizeof(aPayload),
				"event=list_frame page=server_browser items_total=%d rows_visible=%d rows_rendered=%d rows_iterated=%d rows_skipped=%d dur_ms=%.3f source=server_browser",
				ItemsTotal, RowsVisible, RowsRendered, RowsIterated, RowsSkipped, ListFrameDurationMs);
			QmPerfLogPayload("perf/interaction", aPayload, Client(), "server_browser");
		}
	}
	if(ListScrollActive)
	{
		StartSettingsPerfScrollWindow("server_browser_scroll", SettingsPerfContextName(), "server_browser", "none");
		SQmMenuUiFramePerf MenuUiPerf;
		MenuUiPerf.m_pPage = "server_browser";
		MenuUiPerf.m_pOperation = "server_browser_scroll";
		MenuUiPerf.m_ItemsTotal = NumServers;
		MenuUiPerf.m_ItemsVisible = RowsVisible;
		MenuUiPerf.m_ItemsProcessed = RowsIterated;
		MenuUiPerf.m_ItemsSkipped = maximum(0, NumServers - RowsRendered);
		MenuUiPerf.m_UiMs = (float)ListFrameDurationMs;
		QmLogMenuUiFramePerf(MenuUiPerf, Client());
	}
	if(NumServers * ms_ListheaderHeight > ListView.h)
	{
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
		CUIRect Fade = ListView;
		Fade.VSplitRight(s_ListBox.ScrollbarWidthMax(), &Fade, nullptr);
		constexpr float FadeHeight = 44.0f;
		Fade.y += maximum(Fade.h - FadeHeight, 0.0f);
		Fade.h = minimum(Fade.h, FadeHeight);
		Fade.Draw4(
			BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)),
			BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f)),
			BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.38f)),
			BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.38f)),
			IGraphics::CORNER_NONE, 0.0f);
	}
	if(NewSelected != m_SelectedIndex)
	{
		m_SelectedIndex = NewSelected;
		if(m_SelectedIndex >= 0)
		{
			// select the new server
			const CServerInfo *pItem = ServerBrowser()->SortedGet(NewSelected);
			if(pItem)
			{
				str_copy(g_Config.m_UiServerAddress, pItem->m_aAddress);
				m_ServerBrowserShouldRevealSelection = true;
			}
		}
	}

	WasListboxItemActivated = s_ListBox.WasItemActivated();
}

void CMenus::RenderServerbrowserStatusBox(CUIRect StatusBox, bool WasListboxItemActivated)
{
	// Render bar that shows the loading progression.
	// The bar is only shown while loading and fades out when it's done.
	CUIRect RefreshBar;
	StatusBox.HSplitTop(5.0f, &RefreshBar, &StatusBox);
	static float s_LoadingProgressionFadeEnd = 0.0f;
	if(ServerBrowser()->IsRefreshing() && ServerBrowser()->LoadingProgression() < 100)
	{
		s_LoadingProgressionFadeEnd = Client()->GlobalTime() + 2.0f;
	}
	const float LoadingProgressionTimeDiff = s_LoadingProgressionFadeEnd - Client()->GlobalTime();
	if(LoadingProgressionTimeDiff > 0.0f)
	{
		const float RefreshBarAlpha = minimum(LoadingProgressionTimeDiff, 0.8f);
		RefreshBar.h = 2.0f;
		RefreshBar.w *= ServerBrowser()->LoadingProgression() / 100.0f;
		RefreshBar.Draw(BrowserOpacityColor(ColorRGBA(1.0f, 1.0f, 1.0f, RefreshBarAlpha)), IGraphics::CORNER_NONE, 0.0f);
	}

	const float SearchExcludeAddrStrMax = 130.0f;
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	const float ExcludeIconWidth = TextRender()->TextWidth(16.0f, FONT_ICON_BAN);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	const float SearchExcludeAddrInputOffset = SearchExcludeAddrStrMax + 5.0f + ExcludeIconWidth + 5.0f;

	CUIRect SearchInfoAndAddr, ServersAndConnect, ServersPlayersOnline, SearchAndInfo, ServerAddr, ConnectButtons;
	StatusBox.VSplitRight(135.0f, &SearchInfoAndAddr, &ServersAndConnect);
	if(SearchInfoAndAddr.w > 350.0f)
		SearchInfoAndAddr.VSplitLeft(350.0f, &SearchInfoAndAddr, nullptr);
	SearchInfoAndAddr.HSplitTop(40.0f, &SearchAndInfo, &ServerAddr);
	ServersAndConnect.HSplitTop(35.0f, &ServersPlayersOnline, &ConnectButtons);
	ConnectButtons.HSplitTop(5.0f, nullptr, &ConnectButtons);

	CUIRect QuickSearch, QuickExclude;
	SearchAndInfo.HSplitTop(20.0f, &QuickSearch, &QuickExclude);
	QuickSearch.Margin(2.0f, &QuickSearch);
	QuickExclude.Margin(2.0f, &QuickExclude);

	// render quick search
	{
		char aBufSearch[64];
		str_format(aBufSearch, sizeof(aBufSearch), "%s:", Localize("Search"));
		CUIRect SearchLabel;
		QuickSearch.VSplitLeft(SearchExcludeAddrStrMax, &SearchLabel, nullptr);
		QuickSearch.VSplitLeft(SearchExcludeAddrInputOffset, nullptr, &QuickSearch);
		Ui()->DoLabel(&SearchLabel, aBufSearch, 14.0f, TEXTALIGN_ML);

		static CLineInput s_FilterInput(g_Config.m_BrFilterString, sizeof(g_Config.m_BrFilterString));
		static char s_aTooltipText[64];
		str_format(s_aTooltipText, sizeof(s_aTooltipText), "%s: \"solo; nameless tee; kobra 2\"", Localize("Example of usage"));
		GameClient()->m_Tooltips.DoToolTip(&s_FilterInput, &QuickSearch, s_aTooltipText);
		if(!Ui()->IsPopupOpen() && Input()->KeyPress(KEY_F) && Input()->ModifierIsPressed())
		{
			Ui()->SetActiveItem(&s_FilterInput);
			s_FilterInput.SelectAll();
		}
		const IUiContext ServerBrowserSearchCtx = SettingsUiContext("server_browser_search");
		ui_widget::SInputFieldOptions SearchOptions;
		SearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
		SearchOptions.m_Clearable = true;
		SearchOptions.m_FontSize = 12.0f;
		if(ui_widget::InputField(ServerBrowserSearchCtx, &s_FilterInput, QuickSearch, SearchOptions).m_Changed)
			Client()->ServerBrowserUpdate();
	}

	// render quick exclude
	{
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		Ui()->DoLabel(&QuickExclude, FONT_ICON_BAN, 16.0f, TEXTALIGN_ML);
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		CUIRect ExcludeLabel;
		QuickExclude.VSplitLeft(ExcludeIconWidth + 5.0f, nullptr, &ExcludeLabel);
		QuickExclude.VSplitLeft(SearchExcludeAddrInputOffset, nullptr, &QuickExclude);

		char aBufExclude[64];
		str_format(aBufExclude, sizeof(aBufExclude), "%s:", Localize("Exclude"));
		Ui()->DoLabel(&ExcludeLabel, aBufExclude, 14.0f, TEXTALIGN_ML);

		static CLineInput s_ExcludeInput(g_Config.m_BrExcludeString, sizeof(g_Config.m_BrExcludeString));
		static char s_aTooltipText[64];
		str_format(s_aTooltipText, sizeof(s_aTooltipText), "%s: \"CHN; [A]\"", Localize("Example of usage"));
		GameClient()->m_Tooltips.DoToolTip(&s_ExcludeInput, &QuickExclude, s_aTooltipText);
		if(!Ui()->IsPopupOpen() && Input()->KeyPress(KEY_X) && Input()->ShiftIsPressed() && Input()->ModifierIsPressed())
		{
			Ui()->SetActiveItem(&s_ExcludeInput);
			s_ExcludeInput.SelectAll();
		}
		const IUiContext ServerBrowserExcludeCtx = SettingsUiContext("server_browser_exclude");
		ui_widget::SInputFieldOptions SearchOptions;
		SearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
		SearchOptions.m_Clearable = true;
		SearchOptions.m_FontSize = 12.0f;
		if(ui_widget::InputField(ServerBrowserExcludeCtx, &s_ExcludeInput, QuickExclude, SearchOptions).m_Changed)
			Client()->ServerBrowserUpdate();
	}

	// render status
	{
		CUIRect ServersOnline, PlayersOnline;
		ServersPlayersOnline.HSplitMid(&PlayersOnline, &ServersOnline);

		char aBuf[128];
		if(ServerBrowser()->NumServers() != 1)
			str_format(aBuf, sizeof(aBuf), Localize("%d of %d servers"), ServerBrowser()->NumSortedServers(), ServerBrowser()->NumServers());
		else
			str_format(aBuf, sizeof(aBuf), Localize("%d of %d server"), ServerBrowser()->NumSortedServers(), ServerBrowser()->NumServers());
		Ui()->DoLabel(&ServersOnline, aBuf, 12.0f, TEXTALIGN_MR);

		if(ServerBrowser()->NumSortedPlayers() != 1)
			str_format(aBuf, sizeof(aBuf), Localize("%d players"), ServerBrowser()->NumSortedPlayers());
		else
			str_format(aBuf, sizeof(aBuf), Localize("%d player"), ServerBrowser()->NumSortedPlayers());
		Ui()->DoLabel(&PlayersOnline, aBuf, 12.0f, TEXTALIGN_MR);
	}

	// status box
	{
		CUIRect ServersOnline, PlayersOnline;
		ServersPlayersOnline.HSplitMid(&PlayersOnline, &ServersOnline);

		char aBuf[128];
		if(ServerBrowser()->NumServers() != 1)
			str_format(aBuf, sizeof(aBuf), Localize("%d of %d servers"), ServerBrowser()->NumSortedServers(), ServerBrowser()->NumServers());
		else
			str_format(aBuf, sizeof(aBuf), Localize("%d of %d server"), ServerBrowser()->NumSortedServers(), ServerBrowser()->NumServers());
		Ui()->DoLabel(&ServersOnline, aBuf, 12.0f, TEXTALIGN_MR);

		int NumPlayers = 0;
		for(int i = 0; i < ServerBrowser()->NumSortedServers(); i++)
			NumPlayers += ServerBrowser()->SortedGet(i)->m_NumFilteredPlayers;

		if(NumPlayers != 1)
			str_format(aBuf, sizeof(aBuf), Localize("%d players"), NumPlayers);
		else
			str_format(aBuf, sizeof(aBuf), Localize("%d player"), NumPlayers);
		Ui()->DoLabel(&PlayersOnline, aBuf, 12.0f, TEXTALIGN_MR);
	}

	// address info
	{
		CUIRect ServerAddrLabel, ServerAddrEditBox;
		ServerAddr.Margin(2.0f, &ServerAddr);
		ServerAddr.VSplitLeft(SearchExcludeAddrInputOffset, &ServerAddrLabel, &ServerAddrEditBox);

		Ui()->DoLabel(&ServerAddrLabel, Localize("Server address:"), 14.0f, TEXTALIGN_ML);
		static CLineInput s_ServerAddressInput(g_Config.m_UiServerAddress, sizeof(g_Config.m_UiServerAddress));
		const IUiContext ServerBrowserAddressCtx = SettingsUiContext("server_browser_address");
		ui_widget::SInputFieldOptions AddressOptions;
		AddressOptions.m_Clearable = true;
		AddressOptions.m_FontSize = 12.0f;
		if(ui_widget::InputField(ServerBrowserAddressCtx, &s_ServerAddressInput, ServerAddrEditBox, AddressOptions).m_Changed)
			m_ServerBrowserShouldRevealSelection = true;
	}

	// buttons
	{
		CUIRect ButtonRefresh, ButtonConnect;
		ConnectButtons.VSplitMid(&ButtonRefresh, &ButtonConnect, 10.0f);

		// refresh button
		{
			char aLabelBuf[32] = {0};
			const auto &&RefreshLabelFunc = [this, aLabelBuf]() mutable {
				if(ServerBrowser()->IsRefreshing() || ServerBrowser()->IsGettingServerlist())
					str_format(aLabelBuf, sizeof(aLabelBuf), "%s%s", FONT_ICON_ARROW_ROTATE_RIGHT, FONT_ICON_ELLIPSIS);
				else
					str_copy(aLabelBuf, FONT_ICON_ARROW_ROTATE_RIGHT);
				return aLabelBuf;
			};

			SMenuButtonProperties Props;
			Props.m_HintRequiresStringCheck = true;
			Props.m_HintCanChangePositionOrSize = true;
			Props.m_UseIconFont = true;

			static CButtonContainer s_RefreshButton;
			if(Ui()->DoButton_Menu(m_RefreshButton, &s_RefreshButton, RefreshLabelFunc, &ButtonRefresh, Props) || (!Ui()->IsPopupOpen() && (Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))))
			{
				RefreshBrowserTab(true);
			}
		}

		// connect button
		{
			const auto &&ConnectLabelFunc = []() { return FONT_ICON_RIGHT_TO_BRACKET; };

			SMenuButtonProperties Props;
			Props.m_UseIconFont = true;
			Props.m_HintCanChangePositionOrSize = true;
			Props.m_Color = ColorRGBA(0.5f, 1.0f, 0.5f, 0.5f);

			static CButtonContainer s_ConnectButton;
			if(Ui()->DoButton_Menu(m_ConnectButton, &s_ConnectButton, ConnectLabelFunc, &ButtonConnect, Props) || WasListboxItemActivated || (!Ui()->IsPopupOpen() && Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER)))
			{
				Connect(g_Config.m_UiServerAddress);
			}
		}
	}
}

void CMenus::Connect(const char *pAddress, EConnectIntent Intent)
{
	if(Intent == EConnectIntent::Manual)
		StopFriendAutoFollow(m_FriendAutoFollowState);
	if(Client()->State() == IClient::STATE_ONLINE && GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmDisconnectTime && g_Config.m_ClConfirmDisconnectTime >= 0)
	{
		str_copy(m_aNextServer, pAddress);
		PopupConfirm(Localize("Disconnect"), Localize("Are you sure that you want to disconnect and switch to a different server?"), Localize("Yes"), Localize("No"), &CMenus::PopupConfirmSwitchServer);
	}
	else
	{
		Client()->Connect(pAddress);
	}
}

void CMenus::PopupConfirmSwitchServer()
{
	Client()->Connect(m_aNextServer);
}

void CMenus::RenderServerbrowserFilters(CUIRect View)
{
	const float RowHeight = 18.0f;
	const float FontSize = (RowHeight - 4.0f) * CUi::ms_FontmodHeight; // based on DoButton_CheckBox

	View.Margin(5.0f, &View);

	CUIRect Button, ResetButton;
	View.HSplitBottom(RowHeight, &View, &ResetButton);
	View.HSplitBottom(3.0f, &View, nullptr);

	View.HSplitTop(RowHeight, &Button, &View);
	if(DoButton_CheckBox(&g_Config.m_BrFilterEmpty, Localize("Has people playing"), g_Config.m_BrFilterEmpty, &Button))
		g_Config.m_BrFilterEmpty ^= 1;

	View.HSplitTop(RowHeight, &Button, &View);
	if(DoButton_CheckBox(&g_Config.m_BrFilterSpectators, Localize("Count players only"), g_Config.m_BrFilterSpectators, &Button))
		g_Config.m_BrFilterSpectators ^= 1;

	View.HSplitTop(RowHeight, &Button, &View);
	if(DoButton_CheckBox(&g_Config.m_BrFilterFull, Localize("Server not full"), g_Config.m_BrFilterFull, &Button))
		g_Config.m_BrFilterFull ^= 1;

	View.HSplitTop(RowHeight, &Button, &View);
	if(DoButton_CheckBox(&g_Config.m_BrFilterFriends, Localize("Show friends only"), g_Config.m_BrFilterFriends, &Button))
		g_Config.m_BrFilterFriends ^= 1;

	View.HSplitTop(RowHeight, &Button, &View);
	if(DoButton_CheckBox(&g_Config.m_BrFilterPw, Localize("No password"), g_Config.m_BrFilterPw, &Button))
		g_Config.m_BrFilterPw ^= 1;

	View.HSplitTop(RowHeight, &Button, &View);
	if(DoButton_CheckBox(&g_Config.m_BrFilterLogin, Localize("No login required"), g_Config.m_BrFilterLogin, &Button))
		g_Config.m_BrFilterLogin ^= 1;

	View.HSplitTop(RowHeight, &Button, &View);
	if(DoButton_CheckBox(&g_Config.m_BrFilterGametypeStrict, Localize("Strict gametype filter"), g_Config.m_BrFilterGametypeStrict, &Button))
		g_Config.m_BrFilterGametypeStrict ^= 1;

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(RowHeight, &Button, &View);
	Ui()->DoLabel(&Button, Localize("Game types:"), FontSize, TEXTALIGN_ML);
	Button.VSplitRight(60.0f, nullptr, &Button);
	static CLineInput s_GametypeInput(g_Config.m_BrFilterGametype, sizeof(g_Config.m_BrFilterGametype));
	const IUiContext ServerBrowserGameTypeCtx = SettingsUiContext("server_browser_filter_game_type");
	ui_widget::SInputFieldOptions GameTypeOptions;
	GameTypeOptions.m_FontSize = FontSize;
	if(ui_widget::InputField(ServerBrowserGameTypeCtx, &s_GametypeInput, Button, GameTypeOptions).m_Changed)
		Client()->ServerBrowserUpdate();

	// server address
	View.HSplitTop(6.0f, nullptr, &View);
	View.HSplitTop(RowHeight, &Button, &View);
	View.HSplitTop(6.0f, nullptr, &View);
	Ui()->DoLabel(&Button, Localize("Server address:"), FontSize, TEXTALIGN_ML);
	Button.VSplitRight(60.0f, nullptr, &Button);
	static CLineInput s_FilterServerAddressInput(g_Config.m_BrFilterServerAddress, sizeof(g_Config.m_BrFilterServerAddress));
	const IUiContext ServerBrowserFilterAddressCtx = SettingsUiContext("server_browser_filter_address");
	ui_widget::SInputFieldOptions FilterAddressOptions;
	FilterAddressOptions.m_FontSize = FontSize;
	if(ui_widget::InputField(ServerBrowserFilterAddressCtx, &s_FilterServerAddressInput, Button, FilterAddressOptions).m_Changed)
		Client()->ServerBrowserUpdate();

	// player country
	{
		CUIRect Flag;
		View.HSplitTop(RowHeight, &Button, &View);
		Button.VSplitRight(60.0f, &Button, &Flag);
		if(DoButton_CheckBox(&g_Config.m_BrFilterCountry, Localize("Player country:"), g_Config.m_BrFilterCountry, &Button))
			g_Config.m_BrFilterCountry ^= 1;

		const float OldWidth = Flag.w;
		Flag.w = Flag.h * 2.0f;
		Flag.x += (OldWidth - Flag.w) / 2.0f;
		GameClient()->m_CountryFlags.Render(g_Config.m_BrFilterCountryIndex, ColorRGBA(1.0f, 1.0f, 1.0f, Ui()->HotItem() == &g_Config.m_BrFilterCountryIndex ? 1.0f : (g_Config.m_BrFilterCountry ? 0.9f : 0.5f)), Flag.x, Flag.y, Flag.w, Flag.h);

		if(Ui()->DoButtonLogic(&g_Config.m_BrFilterCountryIndex, 0, &Flag, BUTTONFLAG_LEFT))
		{
			static SPopupMenuId s_PopupCountryId;
			static SPopupCountrySelectionContext s_PopupCountryContext;
			s_PopupCountryContext.m_pMenus = this;
			s_PopupCountryContext.m_Selection = g_Config.m_BrFilterCountryIndex;
			s_PopupCountryContext.m_New = true;
			Ui()->DoPopupMenu(&s_PopupCountryId, Flag.x, Flag.y + Flag.h, 490, 210, &s_PopupCountryContext, PopupCountrySelection);
		}
	}

	View.HSplitTop(RowHeight, &Button, &View);
	if(DoButton_CheckBox(&g_Config.m_BrFilterConnectingPlayers, Localize("Filter connecting players"), g_Config.m_BrFilterConnectingPlayers, &Button))
		g_Config.m_BrFilterConnectingPlayers ^= 1;

	// map finish filters
	if(ServerBrowser()->CommunityCache().AnyRanksAvailable())
	{
		View.HSplitTop(RowHeight, &Button, &View);
		if(DoButton_CheckBox(&g_Config.m_BrIndicateFinished, Localize("Indicate map finish"), g_Config.m_BrIndicateFinished, &Button))
		{
			g_Config.m_BrIndicateFinished ^= 1;
			if(g_Config.m_BrIndicateFinished)
				ServerBrowser()->Refresh(ServerBrowser()->GetCurrentType());
		}

		if(g_Config.m_BrIndicateFinished)
		{
			View.HSplitTop(RowHeight, &Button, &View);
			if(DoButton_CheckBox(&g_Config.m_BrFilterUnfinishedMap, Localize("Unfinished map"), g_Config.m_BrFilterUnfinishedMap, &Button))
				g_Config.m_BrFilterUnfinishedMap ^= 1;
		}
		else
		{
			g_Config.m_BrFilterUnfinishedMap = 0;
		}
	}

	// countries and types filters
	if(ServerBrowser()->CommunityCache().CountriesTypesFilterAvailable())
	{
		const ColorRGBA ColorActive = BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f));
		const ColorRGBA ColorInactive = BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f));

		CUIRect TabContents, CountriesTab, TypesTab;
		View.HSplitTop(6.0f, nullptr, &View);
		View.HSplitTop(19.0f, &Button, &View);
		View.HSplitTop(minimum(4.0f * 22.0f, View.h), &TabContents, &View);
		Button.VSplitMid(&CountriesTab, &TypesTab);
		TabContents.Draw(ColorActive, IGraphics::CORNER_B, 4.0f);

		enum EFilterTab
		{
			FILTERTAB_COUNTRIES = 0,
			FILTERTAB_TYPES,
		};
		static EFilterTab s_ActiveTab = FILTERTAB_COUNTRIES;
		static EFilterTab s_PrevFilterTab = FILTERTAB_COUNTRIES;
		static float s_FilterTabDirection = 0.0f;

		static CButtonContainer s_CountriesButton;
		if(DoButton_MenuTab(&s_CountriesButton, Localize("Countries"), s_ActiveTab == FILTERTAB_COUNTRIES, &CountriesTab, IGraphics::CORNER_TL, nullptr, &ColorInactive, &ColorActive, nullptr, 4.0f))
		{
			s_ActiveTab = FILTERTAB_COUNTRIES;
		}

		static CButtonContainer s_TypesButton;
		if(DoButton_MenuTab(&s_TypesButton, Localize("Types"), s_ActiveTab == FILTERTAB_TYPES, &TypesTab, IGraphics::CORNER_TR, nullptr, &ColorInactive, &ColorActive, nullptr, 4.0f))
		{
			s_ActiveTab = FILTERTAB_TYPES;
		}

		if(s_ActiveTab != s_PrevFilterTab)
		{
			s_FilterTabDirection = s_ActiveTab > s_PrevFilterTab ? 1.0f : -1.0f;
			TriggerUiSwitchAnimation(UiAnimNodeKey("browser_filter_tab_switch"), 0.18f);
			s_PrevFilterTab = s_ActiveTab;
		}
		const float TransitionStrength = ReadUiSwitchAnimation(UiAnimNodeKey("browser_filter_tab_switch"));
		const bool TransitionActive = TransitionStrength > 0.0f && s_FilterTabDirection != 0.0f;
		const float TransitionAlpha = UiSwitchAnimationAlpha(TransitionStrength);
		CUIRect AnimatedTabContents = TabContents;
		if(TransitionActive)
		{
			Ui()->ClipEnable(&TabContents);
			ApplyUiSwitchOffset(AnimatedTabContents, TransitionStrength, s_FilterTabDirection, false, 0.08f, 24.0f, 120.0f);
		}

		if(s_ActiveTab == FILTERTAB_COUNTRIES)
		{
			RenderServerbrowserCountriesFilter(AnimatedTabContents);
		}
		else if(s_ActiveTab == FILTERTAB_TYPES)
		{
			RenderServerbrowserTypesFilter(AnimatedTabContents);
		}

		if(TransitionActive)
		{
			if(TransitionAlpha > 0.0f)
				DrawUiSwitchTransitionOverlay(TabContents, BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha)));
			Ui()->ClipDisable();
		}
	}

	static CButtonContainer s_ResetButton;
	if(DoButton_Menu(&s_ResetButton, Localize("Reset filter"), 0, &ResetButton))
	{
		ResetServerbrowserFilters();
	}
}

void CMenus::ResetServerbrowserFilters()
{
	g_Config.m_BrFilterString[0] = '\0';
	g_Config.m_BrExcludeString[0] = '\0';
	g_Config.m_BrFilterFull = 0;
	g_Config.m_BrFilterEmpty = 0;
	g_Config.m_BrFilterSpectators = 0;
	g_Config.m_BrFilterFriends = 0;
	g_Config.m_BrFilterCountry = 0;
	g_Config.m_BrFilterCountryIndex = -1;
	g_Config.m_BrFilterPw = 0;
	g_Config.m_BrFilterGametype[0] = '\0';
	g_Config.m_BrFilterGametypeStrict = 0;
	g_Config.m_BrFilterConnectingPlayers = 1;
	g_Config.m_BrFilterServerAddress[0] = '\0';
	g_Config.m_BrFilterLogin = false; // TClient

	if(g_Config.m_UiPage != PAGE_LAN)
	{
		if(ServerBrowser()->CommunityCache().AnyRanksAvailable())
		{
			g_Config.m_BrFilterUnfinishedMap = 0;
		}
		if(g_Config.m_UiPage == PAGE_INTERNET || g_Config.m_UiPage == PAGE_FAVORITES)
		{
			ServerBrowser()->CommunitiesFilter().Clear();
		}
		ServerBrowser()->CountriesFilter().Clear();
		ServerBrowser()->TypesFilter().Clear();
		UpdateCommunityCache(true);
	}

	Client()->ServerBrowserUpdate();
}

void CMenus::RenderServerbrowserDDNetFilter(CUIRect View,
	IFilterList &Filter,
	float ItemHeight, int MaxItems, int ItemsPerRow,
	CScrollRegion &ScrollRegion, std::vector<unsigned char> &vItemIds,
	bool UpdateCommunityCacheOnChange,
	const std::function<const char *(int ItemIndex)> &GetItemName,
	const std::function<void(int ItemIndex, CUIRect Item, const void *pItemId, bool Active)> &RenderItem)
{
	vItemIds.resize(MaxItems);

	vec2 ScrollOffset(0.0f, 0.0f);
	SQmScrollRequest ScrollRequest;
	ScrollRequest.m_Profile = EQmScrollProfile::FILTER_GRID;
	ScrollRequest.m_RowExtent = ItemHeight;
	ScrollRequest.m_RowsPerStep = 2;
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	ScrollRegion.Begin(&View, &ScrollOffset, &ScrollParams);
	View.y += ScrollOffset.y;

	CUIRect Row;
	int ColumnIndex = 0;
	for(int ItemIndex = 0; ItemIndex < MaxItems; ++ItemIndex)
	{
		CUIRect Item;
		if(ColumnIndex == 0)
			View.HSplitTop(ItemHeight, &Row, &View);
		Row.VSplitLeft(View.w / ItemsPerRow, &Item, &Row);
		ColumnIndex = (ColumnIndex + 1) % ItemsPerRow;
		if(!ScrollRegion.AddRect(Item))
			continue;

		const void *pItemId = &vItemIds[ItemIndex];
		const char *pName = GetItemName(ItemIndex);
		const bool Active = !Filter.Filtered(pName);
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());

		const int Click = Ui()->DoButtonLogic(pItemId, 0, &Item, BUTTONFLAG_ALL);
		if(Click == 1 || Click == 2)
		{
			// left/right click to toggle filter
			if(Filter.Empty())
			{
				if(Click == 1)
				{
					// Left click: when all are active, only activate one and none
					for(int j = 0; j < MaxItems; ++j)
					{
						if(const char *pItemName = GetItemName(j);
							j != ItemIndex &&
							!((&Filter == &ServerBrowser()->CountriesFilter() && str_comp(pItemName, IServerBrowser::COMMUNITY_COUNTRY_NONE) == 0) ||
								(&Filter == &ServerBrowser()->TypesFilter() && str_comp(pItemName, IServerBrowser::COMMUNITY_TYPE_NONE) == 0)))
							Filter.Add(pItemName);
					}
				}
				else if(Click == 2)
				{
					// Right click: when all are active, only deactivate one
					if(MaxItems >= 2)
					{
						Filter.Add(GetItemName(ItemIndex));
					}
				}
			}
			else
			{
				bool AllFilteredExceptUs = true;
				for(int j = 0; j < MaxItems; ++j)
				{
					if(const char *pItemName = GetItemName(j);
						j != ItemIndex && !Filter.Filtered(pItemName) &&
						!((&Filter == &ServerBrowser()->CountriesFilter() && str_comp(pItemName, IServerBrowser::COMMUNITY_COUNTRY_NONE) == 0) ||
							(&Filter == &ServerBrowser()->TypesFilter() && str_comp(pItemName, IServerBrowser::COMMUNITY_TYPE_NONE) == 0)))
					{
						AllFilteredExceptUs = false;
						break;
					}
				}
				// When last one is removed, re-enable all currently selectable items.
				// Don't use Clear, to avoid enabling also currently unselectable items.
				if(AllFilteredExceptUs && Active)
				{
					for(int j = 0; j < MaxItems; ++j)
					{
						Filter.Remove(GetItemName(j));
					}
				}
				else if(Active)
				{
					Filter.Add(pName);
				}
				else
				{
					Filter.Remove(pName);
				}
			}

			Client()->ServerBrowserUpdate();
			if(UpdateCommunityCacheOnChange)
				UpdateCommunityCache(true);
		}
		else if(Click == 3)
		{
			// middle click to reset (re-enable all currently selectable items)
			for(int j = 0; j < MaxItems; ++j)
			{
				Filter.Remove(GetItemName(j));
			}
			Client()->ServerBrowserUpdate();
			if(UpdateCommunityCacheOnChange)
				UpdateCommunityCache(true);
		}

		if(Ui()->HotItem() == pItemId && !ScrollRegion.Animating())
			Item.Draw(BrowserOpacityColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f)), IGraphics::CORNER_ALL, 2.0f);
		RenderItem(ItemIndex, Item, pItemId, Active);
	}

	ScrollRegion.End();
}

void CMenus::RenderServerbrowserCommunitiesFilter(CUIRect View)
{
	CUIRect Tab;
	View.HSplitTop(19.0f, &Tab, &View);
	Tab.Draw(BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f)), IGraphics::CORNER_T, 4.0f);
	Ui()->DoLabel(&Tab, Localize("Communities"), 12.0f, TEXTALIGN_MC);
	View.Draw(BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f)), IGraphics::CORNER_B, 4.0f);

	const int MaxEntries = ServerBrowser()->Communities().size();
	if(MaxEntries == 0)
	{
		CUIRect ErrorLabel;
		View.Margin(5.0f, &ErrorLabel);
		SLabelProperties ErrorLabelProps;
		ErrorLabelProps.m_MaxWidth = ErrorLabel.w;
		ErrorLabelProps.SetColor(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
		Ui()->DoLabel(&ErrorLabel, Localize("Error loading communities"), 10.0f, TEXTALIGN_MC, ErrorLabelProps);
		return;
	}

	const int EntriesPerRow = 1;

	static CScrollRegion s_ScrollRegion;
	static std::vector<unsigned char> s_vItemIds;
	static std::vector<unsigned char> s_vFavoriteButtonIds;

	const float ItemHeight = 13.0f;
	const float Spacing = 2.0f;

	const auto &&GetItemName = [&](int ItemIndex) {
		return ServerBrowser()->Communities()[ItemIndex].Id();
	};
	const auto &&RenderItem = [&](int ItemIndex, CUIRect Item, const void *pItemId, bool Active) {
		const auto &Community = ServerBrowser()->Communities()[ItemIndex];
		const float Alpha = (Active ? 0.9f : 0.2f) + (Ui()->HotItem() == pItemId ? 0.1f : 0.0f);

		CUIRect Icon, NameLabel, PlayerCountIcon, PlayerCountLabel, FavoriteButton;
		Item.VSplitRight(Item.h, &Item, &FavoriteButton);
		Item.HMargin(Spacing, &Item);
		Item.VSplitLeft(Spacing, nullptr, &Item);
		Item.VSplitRight(1.0f, &Item, nullptr);
		Item.VSplitLeft(Item.h * 2.0f, &Icon, &NameLabel);
		NameLabel.VSplitLeft(Spacing, nullptr, &NameLabel);
		NameLabel.VSplitRight(8.0f, &NameLabel, &PlayerCountIcon);
		NameLabel.VSplitRight(25.0f, &NameLabel, &PlayerCountLabel);

		const char *pItemName = Community.Id();
		const CCommunityIcon *pIcon = m_CommunityIcons.Find(pItemName);
		if(pIcon != nullptr)
		{
			m_CommunityIcons.Render(pIcon, Icon, Active);
		}

		TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
		Ui()->DoLabel(&NameLabel, Community.Name(), NameLabel.h * CUi::ms_FontmodHeight, TEXTALIGN_ML);
		char aNumPlayersLabel[8];
		str_format(aNumPlayersLabel, sizeof(aNumPlayersLabel), "%d", Community.NumPlayers());
		Ui()->DoLabel(&PlayerCountLabel, aNumPlayersLabel, 7.0f, TEXTALIGN_MR);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		Ui()->DoLabel(&PlayerCountIcon, FONT_ICON_USER, 7.0f, TEXTALIGN_MC);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		const bool Favorite = ServerBrowser()->FavoriteCommunitiesFilter().Filtered(pItemName);
		if(DoButton_Favorite(&s_vFavoriteButtonIds[ItemIndex], pItemId, Favorite, &FavoriteButton))
		{
			if(Favorite)
			{
				ServerBrowser()->FavoriteCommunitiesFilter().Remove(pItemName);
			}
			else
			{
				ServerBrowser()->FavoriteCommunitiesFilter().Add(pItemName);
			}
		}
		GameClient()->m_Tooltips.DoToolTip(&s_vFavoriteButtonIds[ItemIndex], &FavoriteButton,
			Favorite ? Localize("Click to remove this community from favorites") : Localize("Click to add this community to favorites"));
	};

	s_vFavoriteButtonIds.resize(MaxEntries);
	RenderServerbrowserDDNetFilter(View, ServerBrowser()->CommunitiesFilter(), ItemHeight + 2.0f * Spacing, MaxEntries, EntriesPerRow, s_ScrollRegion, s_vItemIds, true, GetItemName, RenderItem);
}

void CMenus::RenderServerbrowserCountriesFilter(CUIRect View)
{
	const int MaxEntries = ServerBrowser()->CommunityCache().SelectableCountries().size();
	const int EntriesPerRow = MaxEntries > 8 ? 5 : 4;

	static CScrollRegion s_ScrollRegion;
	static std::vector<unsigned char> s_vItemIds;

	const float ItemHeight = 18.0f;
	const float Spacing = 2.0f;

	const auto &&GetItemName = [&](int ItemIndex) {
		return ServerBrowser()->CommunityCache().SelectableCountries()[ItemIndex]->Name();
	};
	const auto &&RenderItem = [&](int ItemIndex, CUIRect Item, const void *pItemId, bool Active) {
		Item.Margin(Spacing, &Item);
		const float OldWidth = Item.w;
		Item.w = Item.h * 2.0f;
		Item.x += (OldWidth - Item.w) / 2.0f;
		GameClient()->m_CountryFlags.Render(ServerBrowser()->CommunityCache().SelectableCountries()[ItemIndex]->FlagId(), ColorRGBA(1.0f, 1.0f, 1.0f, (Active ? 0.9f : 0.2f) + (Ui()->HotItem() == pItemId ? 0.1f : 0.0f)), Item.x, Item.y, Item.w, Item.h);
	};

	RenderServerbrowserDDNetFilter(View, ServerBrowser()->CountriesFilter(), ItemHeight + 2.0f * Spacing, MaxEntries, EntriesPerRow, s_ScrollRegion, s_vItemIds, false, GetItemName, RenderItem);
}

void CMenus::RenderServerbrowserTypesFilter(CUIRect View)
{
	const int MaxEntries = ServerBrowser()->CommunityCache().SelectableTypes().size();
	const int EntriesPerRow = 3;

	static CScrollRegion s_ScrollRegion;
	static std::vector<unsigned char> s_vItemIds;

	const float ItemHeight = 13.0f;
	const float Spacing = 2.0f;

	const auto &&GetItemName = [&](int ItemIndex) {
		return ServerBrowser()->CommunityCache().SelectableTypes()[ItemIndex]->Name();
	};
	const auto &&RenderItem = [&](int ItemIndex, CUIRect Item, const void *pItemId, bool Active) {
		Item.Margin(Spacing, &Item);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, (Active ? 0.9f : 0.2f) + (Ui()->HotItem() == pItemId ? 0.1f : 0.0f));
		Ui()->DoLabel(&Item, GetItemName(ItemIndex), Item.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	};

	RenderServerbrowserDDNetFilter(View, ServerBrowser()->TypesFilter(), ItemHeight + 2.0f * Spacing, MaxEntries, EntriesPerRow, s_ScrollRegion, s_vItemIds, false, GetItemName, RenderItem);
}

CUi::EPopupMenuFunctionResult CMenus::PopupCountrySelection(void *pContext, CUIRect View, bool Active)
{
	SPopupCountrySelectionContext *pPopupContext = static_cast<SPopupCountrySelectionContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;

	static CListBox s_ListBox;
	s_ListBox.SetActive(Active);
	s_ListBox.DoStart(50.0f, pMenus->GameClient()->m_CountryFlags.Num(), 8, 1, -1, &View, false);

	if(pPopupContext->m_New)
	{
		pPopupContext->m_New = false;
		s_ListBox.ScrollToSelected();
	}

	for(size_t i = 0; i < pMenus->GameClient()->m_CountryFlags.Num(); ++i)
	{
		const CCountryFlags::CCountryFlag &Entry = pMenus->GameClient()->m_CountryFlags.GetByIndex(i);

		const CListboxItem Item = s_ListBox.DoNextItem(&Entry, Entry.m_CountryCode == pPopupContext->m_Selection);
		if(!Item.m_Visible)
			continue;

		CUIRect FlagRect, Label;
		Item.m_Rect.Margin(5.0f, &FlagRect);
		FlagRect.HSplitBottom(12.0f, &FlagRect, &Label);
		Label.HSplitTop(2.0f, nullptr, &Label);
		const float OldWidth = FlagRect.w;
		FlagRect.w = FlagRect.h * 2.0f;
		FlagRect.x += (OldWidth - FlagRect.w) / 2.0f;
		pMenus->GameClient()->m_CountryFlags.Render(Entry.m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);

		pMenus->Ui()->DoLabel(&Label, Entry.m_aCountryCodeString, 10.0f, TEXTALIGN_MC);
	}

	const int NewSelected = s_ListBox.DoEnd();
	pPopupContext->m_Selection = NewSelected >= 0 ? pMenus->GameClient()->m_CountryFlags.GetByIndex(NewSelected).m_CountryCode : -1;
	if(s_ListBox.WasItemSelected() || s_ListBox.WasItemActivated())
	{
		g_Config.m_BrFilterCountry = 1;
		g_Config.m_BrFilterCountryIndex = pPopupContext->m_Selection;
		pMenus->Client()->ServerBrowserUpdate();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

void CMenus::RenderServerbrowserInfo(CUIRect View)
{
	const CServerInfo *pSelectedServer = ServerBrowser()->SortedGet(m_SelectedIndex);

	const float RowHeight = 18.0f;
	const float FontSize = (RowHeight - 4.0f) * CUi::ms_FontmodHeight; // based on DoButton_CheckBox

	CUIRect ServerDetails, Scoreboard;
	View.HSplitTop(4.0f * 15.0f + RowHeight + 2.0f * 5.0f + 2.0f * 2.0f, &ServerDetails, &Scoreboard);

	if(pSelectedServer)
	{
		ServerDetails.Margin(5.0f, &ServerDetails);

		// copy info button
		{
			CUIRect Button;
			ServerDetails.HSplitBottom(15.0f, &ServerDetails, &Button);
			static CButtonContainer s_CopyButton;
			if(DoButton_Menu(&s_CopyButton, Localize("Copy info"), 0, &Button))
			{
				char aInfo[512];
				str_format(
					aInfo,
					sizeof(aInfo),
					"%s\n"
					"Address: ddnet://%s\n"
					"Map: %s\n",
					pSelectedServer->m_aName,
					pSelectedServer->m_aAddress,
					pSelectedServer->m_aMap);
				Input()->SetClipboardText(aInfo);
			}
		}

		// favorite checkbox
		{
			CUIRect ButtonAddFav, ButtonLeakIp;
			ServerDetails.HSplitBottom(2.0f, &ServerDetails, nullptr);
			ServerDetails.HSplitBottom(RowHeight, &ServerDetails, &ButtonAddFav);
			ServerDetails.HSplitBottom(2.0f, &ServerDetails, nullptr);
			ButtonAddFav.VSplitMid(&ButtonAddFav, &ButtonLeakIp);
			static int s_AddFavButton = 0;
			if(DoButton_CheckBox_Tristate(&s_AddFavButton, Localize("Favorite"), pSelectedServer->m_Favorite, &ButtonAddFav))
			{
				if(pSelectedServer->m_Favorite != TRISTATE::NONE)
				{
					Favorites()->Remove(pSelectedServer->m_aAddresses, pSelectedServer->m_NumAddresses);
				}
				else
				{
					Favorites()->Add(pSelectedServer->m_aAddresses, pSelectedServer->m_NumAddresses);
					if(g_Config.m_UiPage == PAGE_LAN)
					{
						Favorites()->AllowPing(pSelectedServer->m_aAddresses, pSelectedServer->m_NumAddresses, true);
					}
				}
				Client()->ServerBrowserUpdate();
			}
			if(pSelectedServer->m_Favorite != TRISTATE::NONE)
			{
				static int s_LeakIpButton = 0;
				if(DoButton_CheckBox_Tristate(&s_LeakIpButton, Localize("Leak IP"), pSelectedServer->m_FavoriteAllowPing, &ButtonLeakIp))
				{
					Favorites()->AllowPing(pSelectedServer->m_aAddresses, pSelectedServer->m_NumAddresses, pSelectedServer->m_FavoriteAllowPing == TRISTATE::NONE);
					Client()->ServerBrowserUpdate();
				}
			}
		}

		CUIRect LeftColumn, RightColumn, Row;
		ServerDetails.VSplitLeft(80.0f, &LeftColumn, &RightColumn);

		LeftColumn.HSplitTop(15.0f, &Row, &LeftColumn);
		Ui()->DoLabel(&Row, Localize("Version"), FontSize, TEXTALIGN_ML);

		RightColumn.HSplitTop(15.0f, &Row, &RightColumn);
		Ui()->DoLabel(&Row, pSelectedServer->m_aVersion, FontSize, TEXTALIGN_ML);

		LeftColumn.HSplitTop(15.0f, &Row, &LeftColumn);
		Ui()->DoLabel(&Row, Localize("Game type"), FontSize, TEXTALIGN_ML);

		SLabelProperties GameTypeLabelProps;
		if(g_Config.m_UiColorizeGametype)
		{
			GameTypeLabelProps.SetColor(pSelectedServer->m_GametypeColor);
		}
		RightColumn.HSplitTop(15.0f, &Row, &RightColumn);
		Ui()->DoLabel(&Row, pSelectedServer->m_aGameType, FontSize, TEXTALIGN_ML, GameTypeLabelProps);

		LeftColumn.HSplitTop(15.0f, &Row, &LeftColumn);
		Ui()->DoLabel(&Row, Localize("Ping"), FontSize, TEXTALIGN_ML);

		SLabelProperties PingLabelProps;
		if(g_Config.m_UiColorizePing)
		{
			PingLabelProps.SetColor(GetPingTextColor(pSelectedServer->m_Latency));
		}
		char aPingLabel[8];
		FormatServerbrowserPing(aPingLabel, pSelectedServer);
		RightColumn.HSplitTop(15.0f, &Row, &RightColumn);
		Ui()->DoLabel(&Row, aPingLabel, FontSize, TEXTALIGN_ML, PingLabelProps);

		RenderServerbrowserInfoScoreboard(Scoreboard, pSelectedServer);
	}
	else
	{
		Ui()->DoLabel(&ServerDetails, Localize("No server selected"), FontSize, TEXTALIGN_MC);
	}
}

void CMenus::RenderServerbrowserInfoScoreboard(CUIRect View, const CServerInfo *pSelectedServer)
{
	const float FontSize = 10.0f;

	static CListBox s_ListBox;
	View.VSplitLeft(5.0f, nullptr, &View);
	s_ListBox.DoAutoSpacing(2.0f);
	s_ListBox.DoStart(25.0f, pSelectedServer->m_NumReceivedClients, 1, 3, -1, &View, false, IGraphics::CORNER_NONE);

	for(int i = 0; i < pSelectedServer->m_NumReceivedClients; i++)
	{
		const CServerInfo::CClient &CurrentClient = pSelectedServer->m_aClients[i];
		const CListboxItem Item = s_ListBox.DoNextItem(&CurrentClient);
		if(!Item.m_Visible)
			continue;
		auto GaussianBlurSuppression = Item.SuppressGaussianBlur();

		CUIRect Skin, Name, Clan, Score, Flag;
		Name = Item.m_Rect;

		const ColorRGBA Color = PlayerBackgroundColor(CurrentClient.m_FriendState == IFriends::FRIEND_PLAYER, CurrentClient.m_FriendState == IFriends::FRIEND_CLAN, CurrentClient.m_Afk, false, false);
		DrawRoundedSurface(Ui(), Name, Color, ColorRGBA(), 4.0f);
		Name.VSplitLeft(1.0f, nullptr, &Name);
		Name.VSplitLeft(34.0f, &Score, &Name);
		Name.VSplitLeft(18.0f, &Skin, &Name);
		Name.VSplitRight(26.0f, &Name, &Flag);
		Flag.HMargin(6.0f, &Flag);
		Name.HSplitTop(12.0f, &Name, &Clan);

		// score
		char aTemp[16];
		if(!CurrentClient.m_Player)
		{
			str_copy(aTemp, "SPEC");
		}
		else if(pSelectedServer->m_ClientScoreKind == CServerInfo::CLIENT_SCORE_KIND_POINTS)
		{
			str_format(aTemp, sizeof(aTemp), "%d", CurrentClient.m_Score);
		}
		else
		{
			std::optional<int> Time = {};

			if(pSelectedServer->m_ClientScoreKind == CServerInfo::CLIENT_SCORE_KIND_TIME_BACKCOMPAT)
			{
				const int TempTime = absolute(CurrentClient.m_Score);
				if(TempTime != 0 && TempTime != 9999)
					Time = TempTime;
			}
			else
			{
				// CServerInfo::CLIENT_SCORE_KIND_POINTS
				if(CurrentClient.m_Score >= 0)
					Time = CurrentClient.m_Score;
			}

			if(Time.has_value())
			{
				str_time((int64_t)Time.value() * 100, TIME_HOURS, aTemp, sizeof(aTemp));
			}
			else
			{
				aTemp[0] = '\0';
			}
		}

		Ui()->DoLabel(&Score, aTemp, FontSize, TEXTALIGN_ML);

		// render tee if available
		if(CurrentClient.m_aSkin[0] != '\0')
		{
			const CTeeRenderInfo TeeInfo = GetTeeRenderInfo(vec2(Skin.w, Skin.h), CurrentClient.m_aSkin, CurrentClient.m_CustomSkinColors, CurrentClient.m_CustomSkinColorBody, CurrentClient.m_CustomSkinColorFeet);
			const CAnimState *pIdleState = CAnimState::GetIdle();
			vec2 OffsetToMid;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
			const vec2 TeeRenderPos = vec2(Skin.x + TeeInfo.m_Size / 2.0f, Skin.y + Skin.h / 2.0f + OffsetToMid.y);
			RenderTools()->RenderTee(pIdleState, &TeeInfo, CurrentClient.m_Afk ? EMOTE_BLINK : EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
			Ui()->DoButtonLogic(&CurrentClient.m_aSkin, 0, &Skin, BUTTONFLAG_NONE);
			GameClient()->m_Tooltips.DoToolTip(&CurrentClient.m_aSkin, &Skin, CurrentClient.m_aSkin);
		}
		else if(CurrentClient.m_aaSkin7[protocol7::SKINPART_BODY][0] != '\0')
		{
			CTeeRenderInfo TeeInfo;
			TeeInfo.m_Size = minimum(Skin.w, Skin.h);
			for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
			{
				GameClient()->m_Skins7.FindSkinPart(Part, CurrentClient.m_aaSkin7[Part], true)->ApplyTo(TeeInfo.m_aSixup[g_Config.m_ClDummy]);
				GameClient()->m_Skins7.ApplyColorTo(TeeInfo.m_aSixup[g_Config.m_ClDummy], CurrentClient.m_aUseCustomSkinColor7[Part], CurrentClient.m_aCustomSkinColor7[Part], Part);
			}
			const CAnimState *pIdleState = CAnimState::GetIdle();
			vec2 OffsetToMid;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
			const vec2 TeeRenderPos = vec2(Skin.x + TeeInfo.m_Size / 2.0f, Skin.y + Skin.h / 2.0f + OffsetToMid.y);
			RenderTools()->RenderTee(pIdleState, &TeeInfo, CurrentClient.m_Afk ? EMOTE_BLINK : EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
		}

		// name
		CTextCursor NameCursor;
		NameCursor.SetPosition(vec2(Name.x, Name.y + (Name.h - (FontSize - 1.0f)) / 2.0f));
		NameCursor.m_FontSize = FontSize - 1.0f;
		NameCursor.m_Flags |= TEXTFLAG_STOP_AT_END;
		NameCursor.m_LineWidth = Name.w;
		const char *pName = CurrentClient.m_aName;
		bool Printed = false;
		if(g_Config.m_BrFilterString[0])
			Printed = PrintHighlighted(pName, [&](const char *pFilteredStr, const int FilterLen) {
				TextRender()->TextEx(&NameCursor, pName, (int)(pFilteredStr - pName));
				TextRender()->TextColor(gs_HighlightedTextColor);
				TextRender()->TextEx(&NameCursor, pFilteredStr, FilterLen);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				TextRender()->TextEx(&NameCursor, pFilteredStr + FilterLen, -1);
			});
		if(!Printed)
			TextRender()->TextEx(&NameCursor, pName, -1);

		// clan
		CTextCursor ClanCursor;
		ClanCursor.SetPosition(vec2(Clan.x, Clan.y + (Clan.h - (FontSize - 2.0f)) / 2.0f));
		ClanCursor.m_FontSize = FontSize - 2.0f;
		ClanCursor.m_Flags |= TEXTFLAG_STOP_AT_END;
		ClanCursor.m_LineWidth = Clan.w;
		const char *pClan = CurrentClient.m_aClan;
		Printed = false;
		if(g_Config.m_BrFilterString[0])
			Printed = PrintHighlighted(pClan, [&](const char *pFilteredStr, const int FilterLen) {
				TextRender()->TextEx(&ClanCursor, pClan, (int)(pFilteredStr - pClan));
				TextRender()->TextColor(0.4f, 0.4f, 1.0f, 1.0f);
				TextRender()->TextEx(&ClanCursor, pFilteredStr, FilterLen);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				TextRender()->TextEx(&ClanCursor, pFilteredStr + FilterLen, -1);
			});
		if(!Printed)
			TextRender()->TextEx(&ClanCursor, pClan, -1);

		// flag
		GameClient()->m_CountryFlags.Render(CurrentClient.m_Country, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), Flag.x, Flag.y, Flag.w, Flag.h);
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(s_ListBox.WasItemSelected())
	{
		const CServerInfo::CClient &SelectedClient = pSelectedServer->m_aClients[NewSelected];
		if(SelectedClient.m_FriendState == IFriends::FRIEND_PLAYER)
		{
			GameClient()->Friends()->RemoveFriend(SelectedClient.m_aName, SelectedClient.m_aClan);
		}
		else
		{
			const int DefaultCategoryIndex = maximum(0, GameClient()->Friends()->FindCategory(GameClient()->Friends()->DefaultCategory()));
			if(m_FriendAddCategoryIndex < 0 || m_FriendAddCategoryIndex >= GameClient()->Friends()->NumCategories() || IsClanMembersCategory(GameClient()->Friends()->GetCategory(m_FriendAddCategoryIndex)))
				m_FriendAddCategoryIndex = DefaultCategoryIndex;
			GameClient()->Friends()->AddFriend(SelectedClient.m_aName, SelectedClient.m_aClan, GameClient()->Friends()->GetCategory(m_FriendAddCategoryIndex));
		}
		FriendlistOnUpdate();
		Client()->ServerBrowserUpdate();
	}
}

void CMenus::RenderServerbrowserFriends(CUIRect View)
{
	const bool MenuUiPerfEnabled = QmPerfEnabled();
	const auto MenuUiStartTime = MenuUiPerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
	const float FontSize = 10.0f;
	const float SpacingH = 2.0f;

	CUIRect List, ServerFriends;
	View.HSplitBottom(92.0f, &List, &ServerFriends);
	List.HSplitTop(5.0f, nullptr, &List);
	List.VSplitLeft(5.0f, nullptr, &List);

	FriendlistOnUpdate();
	const int NumCategories = maximum(1, GameClient()->Friends()->NumCategories());

	std::vector<std::vector<CFriendItem>> vvFriends(NumCategories);
	const int OfflineCategoryIndex = maximum(0, GameClient()->Friends()->FindCategory(IFriends::OFFLINE_CATEGORY));

	// calculate friends
	bool OpenRemovePopup = false;
	static CScrollRegion s_FriendsMoveCategoryPopupScrollRegion;
	static CScrollRegion s_FriendsActionPopupScrollRegion;
	for(int FriendIndex = 0; FriendIndex < GameClient()->Friends()->NumFriends(); ++FriendIndex)
	{
		const CFriendInfo *pFriendInfo = GameClient()->Friends()->GetFriend(FriendIndex);
		if(pFriendInfo->m_aName[0] == '\0')
			continue;

		vvFriends[OfflineCategoryIndex].emplace_back(pFriendInfo);
	}

	for(int ServerIndex = 0; ServerIndex < ServerBrowser()->NumServers(); ++ServerIndex)
	{
		const CServerInfo *pEntry = ServerBrowser()->Get(ServerIndex);
		if(pEntry->m_FriendState == IFriends::FRIEND_NO)
			continue;

		for(int ClientIndex = 0; ClientIndex < pEntry->m_NumClients; ++ClientIndex)
		{
			const CServerInfo::CClient &CurrentClient = pEntry->m_aClients[ClientIndex];
			if(CurrentClient.m_FriendState == IFriends::FRIEND_NO)
				continue;

			const bool ClanOnlyMatch = CurrentClient.m_FriendState == IFriends::FRIEND_CLAN;
			const char *pCategory = ClanOnlyMatch ? IFriends::CLAN_MEMBERS_CATEGORY : GameClient()->Friends()->GetFriendCategory(CurrentClient.m_aName, CurrentClient.m_aClan);
			if(!ClanOnlyMatch && IsOfflineFriendsCategory(pCategory))
				pCategory = GameClient()->Friends()->DefaultCategory();

			int CategoryIndex = GameClient()->Friends()->FindCategory(pCategory);
			if(CategoryIndex < 0 || CategoryIndex >= NumCategories)
				CategoryIndex = 0;

			vvFriends[CategoryIndex].emplace_back(CurrentClient, pEntry, pCategory);

			if(!ClanOnlyMatch)
			{
				auto &vOfflineFriends = vvFriends[OfflineCategoryIndex];
				vOfflineFriends.erase(std::remove_if(vOfflineFriends.begin(), vOfflineFriends.end(), [&](const CFriendItem &Friend) {
					return Friend.ServerInfo() == nullptr && Friend.Name()[0] != '\0' && str_comp(Friend.Name(), CurrentClient.m_aName) == 0 && (g_Config.m_ClFriendsIgnoreClan || str_comp(Friend.Clan(), CurrentClient.m_aClan) == 0);
				}),
					vOfflineFriends.end());
			}
		}
	}
	for(auto &vFriends : vvFriends)
	{
		std::sort(vFriends.begin(), vFriends.end(), [](const CFriendItem &Left, const CFriendItem &Right) {
			const bool LeftOnline = Left.ServerInfo() != nullptr;
			const bool RightOnline = Right.ServerInfo() != nullptr;
			if(LeftOnline != RightOnline)
				return LeftOnline;
			return Left < Right;
		});
	}

	bool FollowTargetOnline = false;
	const char *pFollowTargetAddress = "";
	for(const auto &vFriends : vvFriends)
	{
		for(const CFriendItem &Friend : vFriends)
		{
			if(Friend.ServerInfo() == nullptr)
				continue;
			if(str_comp(Friend.Name(), m_FriendAutoFollowState.m_aName) == 0 && str_comp(Friend.Clan(), m_FriendAutoFollowState.m_aClan) == 0)
			{
				FollowTargetOnline = true;
				pFollowTargetAddress = Friend.ServerInfo()->m_aAddress;
				break;
			}
		}
		if(FollowTargetOnline)
			break;
	}
	char aFollowConnectAddress[NETADDR_MAXSTRSIZE];
	if(FriendAutoFollowStep(m_FriendAutoFollowState, FollowTargetOnline, pFollowTargetAddress, Client()->GlobalTime(), g_Config.m_QmFriendAutoFollowDelay, 2, aFollowConnectAddress, sizeof(aFollowConnectAddress)))
	{
		str_copy(g_Config.m_UiServerAddress, aFollowConnectAddress);
		Connect(g_Config.m_UiServerAddress, EConnectIntent::AutoFollow);
	}

	int TotalFriendItems = 0;
	for(const auto &vFriends : vvFriends)
		TotalFriendItems += (int)vFriends.size();
	m_vFriendTooltipText.clear();
	m_vFriendTooltipText.resize(TotalFriendItems);
	int VisibleFriendItems = 0;

	// friends list
	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	SQmScrollRequest ScrollRequest;
	ScrollRequest.m_Profile = EQmScrollProfile::MENU_LIST;
	ScrollRequest.m_RowExtent = 24.0f;
	ScrollRequest.m_RowsPerStep = 3;
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	s_ScrollRegion.Begin(&List, &ScrollOffset, &ScrollParams);
	List.y += ScrollOffset.y;

	const CUIRect ListViewport = *s_ScrollRegion.ClipRect();
	const float UiScale = 1.0f;
	const float CategoryDropPreviewThickness = std::clamp(3.0f * UiScale, 2.0f, 4.0f);
	const float CategoryDragOutlineThickness = std::clamp(2.0f * UiScale, 1.0f, 2.0f);
	const ColorRGBA CategoryDropPreviewColor(0.2f, 0.9f, 0.4f, 0.9f);
	const ColorRGBA CategoryDragOutlineColor(1.0f, 0.85f, 0.2f, 0.9f);
	const ColorRGBA CategoryDragGhostColor(0.08f, 0.09f, 0.12f, 0.55f);

	struct SFriendsCategoryDragState
	{
		int m_PressedIndex = -1;
		int m_DraggingIndex = -1;
		vec2 m_GrabOffset = vec2(0.0f, 0.0f);
		float m_DraggedWidth = 0.0f;
		float m_DraggedHeight = 0.0f;
		bool m_HasDragRect = false;
	};
	static SFriendsCategoryDragState s_CategoryDragState;

	struct SFriendsCategoryDropPreview
	{
		bool m_Active = false;
		bool m_Valid = false;
		int m_DraggedIndex = -1;
		int m_InsertIndex = 0;
		CUIRect m_LineRect = {};
	};
	static SFriendsCategoryDropPreview s_CategoryDropPreview;

	struct SFriendsCategoryHeaderInfo
	{
		int m_CategoryIndex = -1;
		CUIRect m_Rect = {};
	};

	auto ResetCategoryDragState = [&]() {
		s_CategoryDragState = SFriendsCategoryDragState();
		s_CategoryDropPreview = SFriendsCategoryDropPreview();
	};

	auto DrawCategoryDragOutline = [&](const CUIRect &Rect) {
		CUIRect Line = Rect;
		Line.HSplitTop(CategoryDragOutlineThickness, &Line, nullptr);
		Line.Draw(CategoryDragOutlineColor, IGraphics::CORNER_NONE, 0.0f);

		Line = Rect;
		Line.HSplitBottom(CategoryDragOutlineThickness, nullptr, &Line);
		Line.Draw(CategoryDragOutlineColor, IGraphics::CORNER_NONE, 0.0f);

		Line = Rect;
		Line.VSplitLeft(CategoryDragOutlineThickness, &Line, nullptr);
		Line.Draw(CategoryDragOutlineColor, IGraphics::CORNER_NONE, 0.0f);

		Line = Rect;
		Line.VSplitRight(CategoryDragOutlineThickness, nullptr, &Line);
		Line.Draw(CategoryDragOutlineColor, IGraphics::CORNER_NONE, 0.0f);
	};

	auto MoveCategoryExpandedState = [&](int FromIndex, int ToIndex) {
		if(FromIndex < 0 || ToIndex < 0 || FromIndex == ToIndex || FromIndex >= (int)m_vFriendsCategoryExpanded.size() || ToIndex >= (int)m_vFriendsCategoryExpanded.size())
			return;

		const unsigned char Expanded = m_vFriendsCategoryExpanded[FromIndex];
		if(FromIndex < ToIndex)
		{
			for(int Index = FromIndex; Index < ToIndex; ++Index)
				m_vFriendsCategoryExpanded[Index] = m_vFriendsCategoryExpanded[Index + 1];
		}
		else
		{
			for(int Index = FromIndex; Index > ToIndex; --Index)
				m_vFriendsCategoryExpanded[Index] = m_vFriendsCategoryExpanded[Index - 1];
		}
		m_vFriendsCategoryExpanded[ToIndex] = Expanded;
	};

	if(s_CategoryDragState.m_DraggingIndex >= NumCategories || s_CategoryDragState.m_PressedIndex >= NumCategories)
		ResetCategoryDragState();

	std::vector<SFriendsCategoryHeaderInfo> vCategoryHeaders;
	vCategoryHeaders.reserve(NumCategories);
	m_vFriendsCategoryManageButtons.resize(NumCategories);

	char aBuf[256];
	int FriendTooltipIndex = 0;
	for(int CategoryIndex = 0; CategoryIndex < NumCategories; ++CategoryIndex)
	{
		// header
		CUIRect Header, GroupIcon, GroupLabel, ManageButton;
		List.HSplitTop(ms_ListheaderHeight, &Header, &List);
		s_ScrollRegion.AddRect(Header);
		vCategoryHeaders.push_back({CategoryIndex, Header});
		const char *pCategoryName = GameClient()->Friends()->GetCategory(CategoryIndex);
		CUIRect HeaderAction;
		SplitFriendsCategoryHeaderRects(Header, &HeaderAction, nullptr);
		const bool HeaderInside = Ui()->MouseHovered(&HeaderAction);
		if(Input()->ModifierIsPressed() && Ui()->MouseButtonClicked(0) && HeaderInside && Ui()->ActiveItem() == nullptr)
		{
			s_CategoryDragState.m_PressedIndex = CategoryIndex;
			s_CategoryDragState.m_DraggingIndex = -1;
		}

		if(s_CategoryDragState.m_PressedIndex == CategoryIndex && Ui()->MouseButton(0) && Input()->ModifierIsPressed() && s_CategoryDragState.m_DraggingIndex < 0)
		{
			if(HeaderInside)
			{
				s_CategoryDragState.m_DraggingIndex = CategoryIndex;
				s_CategoryDragState.m_GrabOffset = vec2(Ui()->MouseX() - Header.x, Ui()->MouseY() - Header.y);
				s_CategoryDragState.m_DraggedWidth = Header.w;
				s_CategoryDragState.m_DraggedHeight = Header.h;
				s_CategoryDragState.m_HasDragRect = true;

				bool ExpandedChanged = false;
				for(int CollapseIndex = 0; CollapseIndex < NumCategories && CollapseIndex < (int)m_vFriendsCategoryExpanded.size(); ++CollapseIndex)
				{
					if(m_vFriendsCategoryExpanded[CollapseIndex])
					{
						m_vFriendsCategoryExpanded[CollapseIndex] = false;
						ExpandedChanged = true;
					}
				}
				if(ExpandedChanged)
					SaveFriendsCategoryExpandedState();
			}
		}
		else if(s_CategoryDragState.m_PressedIndex == CategoryIndex && !Input()->ModifierIsPressed() && s_CategoryDragState.m_DraggingIndex < 0)
		{
			ResetCategoryDragState();
		}

		const bool DraggingThisHeader = s_CategoryDragState.m_DraggingIndex == CategoryIndex;
		const bool DraggingAnyHeader = s_CategoryDragState.m_DraggingIndex >= 0;
		const bool HeaderHovered = HeaderInside || DraggingThisHeader;
		const bool PopupOpen = Ui()->IsPopupOpen(&m_FriendsCategoryPopupContext) && m_FriendsCategoryPopupContext.m_CategoryIndex == CategoryIndex;
		ColorRGBA HeaderColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		if(str_comp_nocase(pCategoryName, IFriends::DEFAULT_CATEGORY) == 0)
			HeaderColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClFriendsListFriendColor));
		else if(IsClanMembersCategory(pCategoryName))
			HeaderColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClFriendsListClanColor));
		HeaderColor.a = HeaderHovered || PopupOpen ? 0.4f : 0.25f;
		DrawRoundedSurface(Ui(), Header, HeaderColor, ColorRGBA(), 5.0f);
		Header.VSplitLeft(Header.h, &GroupIcon, &GroupLabel);
		GroupIcon.Margin(2.0f, &GroupIcon);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->TextColor(HeaderHovered ? TextRender()->DefaultTextColor() : ColorRGBA(0.6f, 0.6f, 0.6f, 1.0f));
		Ui()->DoLabel(&GroupIcon, m_vFriendsCategoryExpanded[CategoryIndex] ? FONT_ICON_SQUARE_MINUS : FONT_ICON_SQUARE_PLUS, GroupIcon.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		SplitFriendsCategoryHeaderRects(Header, nullptr, &ManageButton);
		GroupLabel.w = maximum(ManageButton.x - GroupLabel.x, 0.0f);
		str_format(aBuf, sizeof(aBuf), "%s (%d)", LocalizeFriendsCategory(pCategoryName), (int)vvFriends[CategoryIndex].size());
		Ui()->DoLabel(&GroupLabel, aBuf, FontSize, TEXTALIGN_ML);
		if(DraggingThisHeader)
			DrawCategoryDragOutline(Header);

		auto OpenCategoryManagePopup = [&]() {
			m_FriendsCategoryPopupContext.m_pMenus = this;
			m_FriendsCategoryPopupContext.m_CategoryIndex = CategoryIndex;
			m_FriendsCategoryPopupContext.m_Mode = CFriendsCategoryPopupContext::MODE_ACTIONS;
			m_FriendsCategoryPopupContext.m_NameInput.Clear();
			const CUIRect Panel = CMenus::SecondaryPanelRect(Ui()->MouseX(), Ui()->MouseY(), 300.0f, CMenus::FriendsCategoryActionsPopupHeight(), *Ui()->Screen());
			Ui()->DoPopupMenu(&m_FriendsCategoryPopupContext, Panel.x, Panel.y, Panel.w, Panel.h, &m_FriendsCategoryPopupContext, PopupFriendsCategory);
		};
		if(Ui()->DoButton_FontIcon(&m_vFriendsCategoryManageButtons[CategoryIndex], FONT_ICON_GEAR, 0, &ManageButton, BUTTONFLAG_LEFT))
		{
			OpenCategoryManagePopup();
		}
		GameClient()->m_Tooltips.DoToolTip(&m_vFriendsCategoryManageButtons[CategoryIndex], &ManageButton, Localize("Manage categories"));
		GameClient()->m_Tooltips.DoToolTip(&m_vFriendsCategoryExpanded[CategoryIndex], &HeaderAction, Localize("Right-click or use the gear to manage categories"));

		const int HeaderResult = Ui()->DoButtonLogic(&m_vFriendsCategoryExpanded[CategoryIndex], 0, &HeaderAction, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
		if(!DraggingAnyHeader && HeaderResult == 2)
		{
			OpenCategoryManagePopup();
		}
		else if(!DraggingAnyHeader && HeaderResult == 1)
		{
			m_vFriendsCategoryExpanded[CategoryIndex] = !m_vFriendsCategoryExpanded[CategoryIndex];
			SaveFriendsCategoryExpandedState();
		}

		// entries
		if(m_vFriendsCategoryExpanded[CategoryIndex])
		{
			for(size_t FriendIndex = 0; FriendIndex < vvFriends[CategoryIndex].size(); ++FriendIndex)
			{
				// space
				{
					CUIRect Space;
					List.HSplitTop(SpacingH, &Space, &List);
					s_ScrollRegion.AddRect(Space);
				}

				CUIRect Rect;
				const auto &Friend = vvFriends[CategoryIndex][FriendIndex];
				const bool IsPlayerFriend = Friend.FriendState() == IFriends::FRIEND_PLAYER;
				const char *pNote = IsPlayerFriend ? GameClient()->Friends()->GetFriendNote(Friend.Name(), Friend.Clan()) : "";
				const bool HasNote = pNote[0] != '\0';
				const unsigned NameHash = str_quickhash(Friend.Name());
				const unsigned ClanHash = str_quickhash(Friend.Clan());
				const unsigned AddrHash = Friend.ServerInfo() != nullptr ? str_quickhash(Friend.ServerInfo()->m_aAddress) : 0;
				uintptr_t FriendUiIdBase = ((uintptr_t)NameHash << 32) ^ (uintptr_t)ClanHash ^ ((uintptr_t)AddrHash << 1) ^ (uintptr_t)(Friend.FriendState() << 2);
				if(FriendUiIdBase == 0)
					FriendUiIdBase = 1;
				FriendUiIdBase <<= 4;
				const void *pListItemId = reinterpret_cast<const void *>(FriendUiIdBase | 0x1);
				const void *pRemoveButtonId = reinterpret_cast<const void *>(FriendUiIdBase | 0x3);
				const void *pCopyButtonId = reinterpret_cast<const void *>(FriendUiIdBase | 0x9);
				const void *pFollowButtonId = reinterpret_cast<const void *>(FriendUiIdBase | 0xb);
				const void *pCommunityTooltipId = reinterpret_cast<const void *>(FriendUiIdBase | 0x5);
				const void *pSkinTooltipId = reinterpret_cast<const void *>(FriendUiIdBase | 0x7);
				List.HSplitTop(11.0f + 10.0f + 2 * 2.0f + 1.0f + (Friend.ServerInfo() == nullptr ? 0.0f : 10.0f), &Rect, &List);
				s_ScrollRegion.AddRect(Rect);
				if(s_ScrollRegion.RectClipped(Rect))
					continue;
				++VisibleFriendItems;

				const bool Inside = Ui()->MouseHovered(&Rect);
				int ButtonResult = Ui()->DoButtonLogic(pListItemId, 0, &Rect, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);

				if(Friend.ServerInfo() || HasNote)
				{
					std::string &TooltipText = m_vFriendTooltipText[FriendTooltipIndex];
					TooltipText.clear();
					if(Friend.ServerInfo() && HasNote)
					{
						TooltipText = Localize("Click to select server. Double click to join your friend.");
						TooltipText.append("\n");
						TooltipText.append(Localize("Note:"));
						TooltipText.append(" ");
						TooltipText.append(pNote);
					}
					else if(Friend.ServerInfo())
					{
						TooltipText = Localize("Click to select server. Double click to join your friend.");
					}
					else
					{
						TooltipText = Localize("Note:");
						TooltipText.append(" ");
						TooltipText.append(pNote);
					}
					GameClient()->m_Tooltips.DoToolTip(pListItemId, &Rect, TooltipText.c_str(), 320.0f);
				}
				++FriendTooltipIndex;
				const bool IsOffline = Friend.ServerInfo() == nullptr;
				const bool InSelectedServer = m_SelectedIndex >= 0 && Friend.ServerInfo() && Friend.ServerInfo()->m_ServerIndex == ServerBrowser()->SortedGet(m_SelectedIndex)->m_ServerIndex;
				const ColorRGBA Color = PlayerBackgroundColor(Friend.FriendState() == IFriends::FRIEND_PLAYER, Friend.FriendState() == IFriends::FRIEND_CLAN, IsOffline ? true : Friend.IsAfk(), InSelectedServer, Inside);
				DrawRoundedSurface(Ui(), Rect, Color, ColorRGBA(), 5.0f);
				Rect.Margin(2.0f, &Rect);

				CUIRect ButtonsRow, FollowButton, CopyButton, RemoveButton, NameLabel, ClanLabel, InfoLabel;
				Rect.HSplitTop(16.0f, &ButtonsRow, nullptr);
				ButtonsRow.VSplitRight(13.0f, nullptr, &RemoveButton);
				ButtonsRow.VSplitRight(15.0f, nullptr, &CopyButton);
				ButtonsRow.VSplitRight(15.0f, nullptr, &FollowButton);
				FollowButton.VSplitLeft(2.0f, nullptr, &FollowButton);
				CopyButton.VSplitLeft(2.0f, nullptr, &CopyButton);
				FollowButton.HMargin((FollowButton.h - FollowButton.w) / 2.0f, &FollowButton);
				CopyButton.HMargin((CopyButton.h - CopyButton.w) / 2.0f, &CopyButton);
				RemoveButton.HMargin((RemoveButton.h - RemoveButton.w) / 2.0f, &RemoveButton);
				Rect.VSplitLeft(2.0f, nullptr, &Rect);

				if(Friend.ServerInfo())
					Rect.HSplitBottom(10.0f, &Rect, &InfoLabel);
				Rect.HSplitTop(11.0f + 10.0f, &Rect, nullptr);

				// tee
				CUIRect Skin;
				Rect.VSplitLeft(Rect.h, &Skin, &Rect);
				Rect.VSplitLeft(2.0f, nullptr, &Rect);
				if(Friend.Skin()[0] != '\0')
				{
					const CTeeRenderInfo TeeInfo = GetTeeRenderInfo(vec2(Skin.w, Skin.h), Friend.Skin(), Friend.CustomSkinColors(), Friend.CustomSkinColorBody(), Friend.CustomSkinColorFeet());
					const CAnimState *pIdleState = CAnimState::GetIdle();
					vec2 OffsetToMid;
					CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
					const vec2 TeeRenderPos = vec2(Skin.x + Skin.w / 2.0f, Skin.y + Skin.h * 0.55f + OffsetToMid.y);
					RenderTools()->RenderTee(pIdleState, &TeeInfo, Friend.IsAfk() ? EMOTE_BLINK : EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
					Ui()->DoButtonLogic(pSkinTooltipId, 0, &Skin, BUTTONFLAG_NONE);
					GameClient()->m_Tooltips.DoToolTip(pSkinTooltipId, &Skin, Friend.Skin());
				}
				else if(Friend.Skin7(protocol7::SKINPART_BODY)[0] != '\0')
				{
					CTeeRenderInfo TeeInfo;
					TeeInfo.m_Size = minimum(Skin.w, Skin.h);
					for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
					{
						GameClient()->m_Skins7.FindSkinPart(Part, Friend.Skin7(Part), true)->ApplyTo(TeeInfo.m_aSixup[g_Config.m_ClDummy]);
						GameClient()->m_Skins7.ApplyColorTo(TeeInfo.m_aSixup[g_Config.m_ClDummy], Friend.UseCustomSkinColor7(Part), Friend.CustomSkinColor7(Part), Part);
					}
					const CAnimState *pIdleState = CAnimState::GetIdle();
					vec2 OffsetToMid;
					CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
					const vec2 TeeRenderPos = vec2(Skin.x + Skin.w / 2.0f, Skin.y + Skin.h * 0.55f + OffsetToMid.y);
					RenderTools()->RenderTee(pIdleState, &TeeInfo, Friend.IsAfk() ? EMOTE_BLINK : EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
				}
				Rect.HSplitTop(11.0f, &NameLabel, &ClanLabel);

				// name
				Ui()->DoLabel(&NameLabel, Friend.Name(), FontSize - 1.0f, TEXTALIGN_ML);

				// clan
				Ui()->DoLabel(&ClanLabel, Friend.Clan(), FontSize - 2.0f, TEXTALIGN_ML);

				// server info
				if(Friend.ServerInfo())
				{
					// community icon
					const CCommunity *pCommunity = ServerBrowser()->Community(Friend.ServerInfo()->m_aCommunityId);
					if(pCommunity != nullptr)
					{
						const CCommunityIcon *pIcon = m_CommunityIcons.Find(pCommunity->Id());
						if(pIcon != nullptr)
						{
							CUIRect CommunityIcon;
							InfoLabel.VSplitLeft(21.0f, &CommunityIcon, &InfoLabel);
							InfoLabel.VSplitLeft(2.0f, nullptr, &InfoLabel);
							m_CommunityIcons.Render(pIcon, CommunityIcon, true);
							Ui()->DoButtonLogic(pCommunityTooltipId, 0, &CommunityIcon, BUTTONFLAG_NONE);
							GameClient()->m_Tooltips.DoToolTip(pCommunityTooltipId, &CommunityIcon, pCommunity->Name());
						}
					}

					// server info text
					char aLatency[16];
					FormatServerbrowserPing(aLatency, Friend.ServerInfo());
					if(aLatency[0] != '\0')
						str_format(aBuf, sizeof(aBuf), "%s | %s | %s", Friend.ServerInfo()->m_aMap, Friend.ServerInfo()->m_aGameType, aLatency);
					else
						str_format(aBuf, sizeof(aBuf), "%s | %s", Friend.ServerInfo()->m_aMap, Friend.ServerInfo()->m_aGameType);
					Ui()->DoLabel(&InfoLabel, aBuf, FontSize - 2.0f, TEXTALIGN_ML);
				}

				// remove button
				if(Inside)
				{
					const ColorRGBA InactiveIconColor = ColorRGBA(0.4f, 0.4f, 0.4f, 1.0f);
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
					const bool FollowingThisFriend = m_FriendAutoFollowState.m_Active && str_comp(m_FriendAutoFollowState.m_aName, Friend.Name()) == 0 && str_comp(m_FriendAutoFollowState.m_aClan, Friend.Clan()) == 0;
					if(Friend.ServerInfo())
					{
						TextRender()->TextColor(FollowingThisFriend || Ui()->HotItem() == pFollowButtonId ? TextRender()->DefaultTextColor() : InactiveIconColor);
						Ui()->DoLabel(&FollowButton, FollowingThisFriend ? FONT_ICON_STOP : FONT_ICON_PLAY, FollowButton.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
					}
					TextRender()->TextColor(Ui()->HotItem() == pCopyButtonId ? TextRender()->DefaultTextColor() : InactiveIconColor);
					Ui()->DoLabel(&CopyButton, FONT_ICON_COPY, CopyButton.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
					TextRender()->TextColor(Ui()->HotItem() == pRemoveButtonId ? TextRender()->DefaultTextColor() : InactiveIconColor);
					Ui()->DoLabel(&RemoveButton, FONT_ICON_TRASH, RemoveButton.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
					TextRender()->SetRenderFlags(0);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					if(Ui()->DoButtonLogic(pCopyButtonId, 0, &CopyButton, BUTTONFLAG_LEFT))
					{
						Input()->SetClipboardText(Friend.Name()[0] != '\0' ? Friend.Name() : Friend.Clan());
						ButtonResult = 0;
					}
					GameClient()->m_Tooltips.DoToolTip(pCopyButtonId, &CopyButton, Friend.FriendState() == IFriends::FRIEND_PLAYER ? Localize("Click to copy this player's name to clipboard") : Localize("Click to copy this clan's name to clipboard"));
					if(Friend.ServerInfo() && Ui()->DoButtonLogic(pFollowButtonId, 0, &FollowButton, BUTTONFLAG_LEFT))
					{
						if(FollowingThisFriend)
							StopFriendAutoFollow(m_FriendAutoFollowState);
						else
							StartFriendAutoFollow(m_FriendAutoFollowState, Friend.Name(), Friend.Clan(), Friend.ServerInfo()->m_aAddress);
						ButtonResult = 0;
					}
					if(Friend.ServerInfo())
						GameClient()->m_Tooltips.DoToolTip(pFollowButtonId, &FollowButton, FollowingThisFriend ? Localize("Stop following this friend", "Friend auto follow") : Localize("Follow this friend across servers", "Friend auto follow"));
					if(Ui()->DoButtonLogic(pRemoveButtonId, 0, &RemoveButton, BUTTONFLAG_LEFT))
					{
						str_copy(m_aRemoveFriendName, Friend.Name(), sizeof(m_aRemoveFriendName));
						str_copy(m_aRemoveFriendClan, Friend.Clan(), sizeof(m_aRemoveFriendClan));
						m_RemoveFriendState = Friend.FriendState();
						m_HasRemoveFriend = true;
						OpenRemovePopup = true;
						ButtonResult = 0;
					}
					GameClient()->m_Tooltips.DoToolTip(pRemoveButtonId, &RemoveButton, Friend.FriendState() == IFriends::FRIEND_PLAYER ? Localize("Click to remove this player from your friends list.") : Localize("Click to remove this clan from your friends list."));
				}

				if(ButtonResult == 2)
				{
					const bool CanMoveCategory = Friend.FriendState() == IFriends::FRIEND_PLAYER && !IsClanMembersCategory(Friend.Category());
					const bool CanEditNote = Friend.FriendState() == IFriends::FRIEND_PLAYER;

					m_FriendsActionPopupContext.Reset();
					m_FriendsActionPopupContext.m_pScrollRegion = &s_FriendsActionPopupScrollRegion;
					m_vFriendsActionEntries.clear();
					m_FriendsActionPopupContext.m_EntryHeight = 18.0f;
					m_FriendsActionPopupContext.m_EntryPadding = 1.0f;
					m_FriendsActionPopupContext.m_FontSize = (m_FriendsActionPopupContext.m_EntryHeight - 2 * m_FriendsActionPopupContext.m_EntryPadding) * CUi::ms_FontmodHeight;
					m_FriendsActionPopupContext.m_Width = 180.0f;

					if(CanMoveCategory)
					{
						m_FriendsActionPopupContext.m_vEntries.emplace_back(Localize("Move to category"));
						m_vFriendsActionEntries.push_back(FRIEND_ACTION_MOVE_CATEGORY);
					}

					if(CanEditNote)
					{
						m_FriendsActionPopupContext.m_vEntries.emplace_back(Localize("Edit note"));
						m_vFriendsActionEntries.push_back(FRIEND_ACTION_EDIT_NOTE);
						if(HasNote)
						{
							m_FriendsActionPopupContext.m_vEntries.emplace_back(Localize("Clear note"));
							m_vFriendsActionEntries.push_back(FRIEND_ACTION_CLEAR_NOTE);
						}
					}

					if(Friend.ServerInfo())
					{
						const bool FollowingThisFriend = m_FriendAutoFollowState.m_Active && str_comp(m_FriendAutoFollowState.m_aName, Friend.Name()) == 0 && str_comp(m_FriendAutoFollowState.m_aClan, Friend.Clan()) == 0;
						m_FriendsActionPopupContext.m_vEntries.emplace_back(FollowingThisFriend ? Localize("Stop following this friend", "Friend auto follow") : Localize("Follow this friend across servers", "Friend auto follow"));
						m_vFriendsActionEntries.push_back(FollowingThisFriend ? FRIEND_ACTION_STOP_FOLLOW : FRIEND_ACTION_FOLLOW);
					}

					m_FriendsActionPopupContext.m_vEntries.emplace_back(Localize("Remove friend"));
					m_vFriendsActionEntries.push_back(FRIEND_ACTION_REMOVE);

					if(!m_FriendsActionPopupContext.m_vEntries.empty())
					{
						str_copy(m_aFriendActionName, Friend.Name(), sizeof(m_aFriendActionName));
						str_copy(m_aFriendActionClan, Friend.Clan(), sizeof(m_aFriendActionClan));
						str_copy(m_aFriendActionAddress, Friend.ServerInfo() != nullptr ? Friend.ServerInfo()->m_aAddress : "", sizeof(m_aFriendActionAddress));
						m_FriendActionState = Friend.FriendState();
						m_HasFriendAction = true;
						Ui()->ShowPopupSelection(Ui()->MouseX(), Ui()->MouseY(), &m_FriendsActionPopupContext);
					}

					ButtonResult = 0;
				}

				// handle click and double click on item
				if(ButtonResult == 1 && Friend.ServerInfo())
				{
					str_copy(g_Config.m_UiServerAddress, Friend.ServerInfo()->m_aAddress);
					m_ServerBrowserShouldRevealSelection = true;
					if(Ui()->DoDoubleClickLogic(pListItemId))
					{
						Connect(g_Config.m_UiServerAddress);
					}
				}
			}

			if(GameClient()->Friends()->NumFriends() == 0 && CategoryIndex == 0)
			{
				const char *pText = Localize("Add friends by entering their name below or by clicking their name in the player list.");
				const float DescriptionMargin = 2.0f;
				const STextBoundingBox BoundingBox = TextRender()->TextBoundingBox(FontSize, pText, -1, List.w - 2 * DescriptionMargin);
				CUIRect EmptyDescription;
				List.HSplitTop(BoundingBox.m_H + 2 * DescriptionMargin, &EmptyDescription, &List);
				s_ScrollRegion.AddRect(EmptyDescription);
				EmptyDescription.Margin(DescriptionMargin, &EmptyDescription);
				SLabelProperties DescriptionProps;
				DescriptionProps.m_MaxWidth = EmptyDescription.w;
				Ui()->DoLabel(&EmptyDescription, pText, FontSize, TEXTALIGN_ML, DescriptionProps);
			}
		}

		// space
		{
			CUIRect Space;
			List.HSplitTop(SpacingH, &Space, &List);
			s_ScrollRegion.AddRect(Space);
		}
	}

	auto UpdateCategoryDropPreview = [&]() {
		s_CategoryDropPreview = SFriendsCategoryDropPreview();

		if(s_CategoryDragState.m_DraggingIndex < 0 || NumCategories <= 1)
			return;

		if(!Ui()->MouseHovered(&ListViewport))
			return;

		std::vector<const SFriendsCategoryHeaderInfo *> vFilteredHeaders;
		vFilteredHeaders.reserve(vCategoryHeaders.size());
		for(const auto &HeaderInfo : vCategoryHeaders)
		{
			if(HeaderInfo.m_CategoryIndex != s_CategoryDragState.m_DraggingIndex)
				vFilteredHeaders.push_back(&HeaderInfo);
		}

		int InsertIndex = 0;
		const float MouseY = Ui()->MouseY();
		for(const auto *pHeaderInfo : vFilteredHeaders)
		{
			const float MidY = pHeaderInfo->m_Rect.y + pHeaderInfo->m_Rect.h * 0.5f;
			if(MouseY > MidY)
				++InsertIndex;
		}

		float LineY = ListViewport.y + ms_ListheaderHeight * 0.5f;
		if(!vFilteredHeaders.empty())
		{
			if(InsertIndex <= 0)
			{
				LineY = maximum(ListViewport.y, vFilteredHeaders.front()->m_Rect.y - SpacingH * 0.5f);
			}
			else if(InsertIndex >= (int)vFilteredHeaders.size())
			{
				const SFriendsCategoryHeaderInfo *pLast = vFilteredHeaders.back();
				LineY = pLast->m_Rect.y + pLast->m_Rect.h + SpacingH * 0.5f;
			}
			else
			{
				const SFriendsCategoryHeaderInfo *pPrev = vFilteredHeaders[InsertIndex - 1];
				const SFriendsCategoryHeaderInfo *pNext = vFilteredHeaders[InsertIndex];
				LineY = (pPrev->m_Rect.y + pPrev->m_Rect.h + pNext->m_Rect.y) * 0.5f;
			}
		}

		const float LinePadding = std::clamp(6.0f * UiScale, 3.0f, 8.0f);
		CUIRect LineRect;
		LineRect.x = ListViewport.x + LinePadding;
		LineRect.w = maximum(0.0f, ListViewport.w - LinePadding * 2.0f);
		LineRect.y = LineY - CategoryDropPreviewThickness * 0.5f;
		LineRect.h = CategoryDropPreviewThickness;

		s_CategoryDropPreview.m_Active = true;
		s_CategoryDropPreview.m_Valid = true;
		s_CategoryDropPreview.m_DraggedIndex = s_CategoryDragState.m_DraggingIndex;
		s_CategoryDropPreview.m_InsertIndex = InsertIndex;
		s_CategoryDropPreview.m_LineRect = LineRect;
	};

	auto RenderCategoryDragGhost = [&]() {
		if(s_CategoryDragState.m_DraggingIndex < 0 || !s_CategoryDragState.m_HasDragRect)
			return;

		CUIRect Ghost;
		Ghost.x = Ui()->MouseX() - s_CategoryDragState.m_GrabOffset.x;
		Ghost.y = Ui()->MouseY() - s_CategoryDragState.m_GrabOffset.y;
		Ghost.w = s_CategoryDragState.m_DraggedWidth;
		Ghost.h = s_CategoryDragState.m_DraggedHeight;

		CUIRect Shadow = Ghost;
		Shadow.x += 1.5f;
		Shadow.y += 2.0f;
		Shadow.Draw(BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.45f)), IGraphics::CORNER_ALL, 5.0f);

		Ghost.Draw(CategoryDragGhostColor, IGraphics::CORNER_ALL, 5.0f);
		DrawCategoryDragOutline(Ghost);
	};

	auto CommitCategoryDropPreview = [&]() {
		if(!s_CategoryDropPreview.m_Active || !s_CategoryDropPreview.m_Valid || s_CategoryDropPreview.m_DraggedIndex < 0)
			return false;

		const int FromIndex = s_CategoryDropPreview.m_DraggedIndex;
		const int ToIndex = std::clamp(s_CategoryDropPreview.m_InsertIndex, 0, NumCategories - 1);
		if(FromIndex == ToIndex)
			return false;

		char aSelectedCategory[IFriends::MAX_FRIEND_CATEGORY_LENGTH] = {};
		const bool HasSelectedCategory = m_FriendAddCategoryIndex >= 0 && m_FriendAddCategoryIndex < NumCategories;
		if(HasSelectedCategory)
			str_copy(aSelectedCategory, GameClient()->Friends()->GetCategory(m_FriendAddCategoryIndex), sizeof(aSelectedCategory));

		if(!GameClient()->Friends()->MoveCategory(FromIndex, ToIndex))
			return false;

		MoveCategoryExpandedState(FromIndex, ToIndex);
		SaveFriendsCategoryExpandedState();
		FriendlistOnUpdate();

		if(HasSelectedCategory)
		{
			const int SelectedCategoryIndex = GameClient()->Friends()->FindCategory(aSelectedCategory);
			if(SelectedCategoryIndex >= 0)
				m_FriendAddCategoryIndex = SelectedCategoryIndex;
		}

		return true;
	};

	if(s_CategoryDragState.m_DraggingIndex >= 0)
	{
		UpdateCategoryDropPreview();
		if(s_CategoryDropPreview.m_Active && s_CategoryDropPreview.m_Valid)
			s_CategoryDropPreview.m_LineRect.Draw(CategoryDropPreviewColor, IGraphics::CORNER_ALL, CategoryDropPreviewThickness);
		RenderCategoryDragGhost();
	}

	const bool MouseReleased = !Ui()->MouseButton(0) && Ui()->LastMouseButton(0);
	if(MouseReleased)
	{
		if(s_CategoryDragState.m_DraggingIndex >= 0)
			CommitCategoryDropPreview();
		ResetCategoryDragState();
	}

	s_ScrollRegion.End();
	const bool FriendsScrollActive = QmMenuUiScrollPerfActive(s_ScrollRegion.WheelConsumedThisFrame(), s_ScrollRegion.Active(), s_ScrollRegion.Animating());
	if(FriendsScrollActive)
	{
		StartSettingsPerfScrollWindow("friends_scroll", SettingsPerfContextName(), "friends", "none");
		SQmMenuUiFramePerf MenuUiPerf;
		MenuUiPerf.m_pPage = "friends";
		MenuUiPerf.m_pOperation = "friends_scroll";
		MenuUiPerf.m_ItemsTotal = TotalFriendItems;
		MenuUiPerf.m_ItemsVisible = VisibleFriendItems;
		MenuUiPerf.m_ItemsProcessed = VisibleFriendItems;
		MenuUiPerf.m_ItemsSkipped = maximum(0, TotalFriendItems - VisibleFriendItems);
		MenuUiPerf.m_UiMs = MenuUiPerfEnabled ? (float)std::chrono::duration<double, std::milli>(time_get_nanoseconds() - MenuUiStartTime).count() : -1.0f;
		QmLogMenuUiFramePerf(MenuUiPerf, Client());
	}

	if(m_HasFriendAction && m_FriendsActionPopupContext.m_pSelection != nullptr)
	{
		const int SelectionIndex = m_FriendsActionPopupContext.m_SelectionIndex;
		if(SelectionIndex >= 0 && SelectionIndex < (int)m_vFriendsActionEntries.size())
		{
			const EFriendAction Action = m_vFriendsActionEntries[SelectionIndex];
			if(Action == FRIEND_ACTION_MOVE_CATEGORY)
			{
				m_FriendsMoveCategoryPopupContext.Reset();
				m_FriendsMoveCategoryPopupContext.m_pScrollRegion = &s_FriendsMoveCategoryPopupScrollRegion;
				str_copy(m_FriendsMoveCategoryPopupContext.m_aMessage, Localize("Move to category"));
				m_FriendsMoveCategoryPopupContext.m_EntryHeight = 18.0f;
				m_FriendsMoveCategoryPopupContext.m_EntryPadding = 1.0f;
				m_FriendsMoveCategoryPopupContext.m_FontSize = (m_FriendsMoveCategoryPopupContext.m_EntryHeight - 2 * m_FriendsMoveCategoryPopupContext.m_EntryPadding) * CUi::ms_FontmodHeight;
				m_FriendsMoveCategoryPopupContext.m_Width = 180.0f;
				for(int MoveCategoryIndex = 0; MoveCategoryIndex < NumCategories; ++MoveCategoryIndex)
				{
					const char *pMoveCategory = GameClient()->Friends()->GetCategory(MoveCategoryIndex);
					if(IsClanMembersCategory(pMoveCategory))
						continue;
					m_FriendsMoveCategoryPopupContext.m_vEntries.emplace_back(pMoveCategory);
				}

				if(!m_FriendsMoveCategoryPopupContext.m_vEntries.empty())
				{
					str_copy(m_aMoveCategoryFriendName, m_aFriendActionName, sizeof(m_aMoveCategoryFriendName));
					str_copy(m_aMoveCategoryFriendClan, m_aFriendActionClan, sizeof(m_aMoveCategoryFriendClan));
					m_HasMoveCategoryFriend = true;
					Ui()->ShowPopupSelection(Ui()->MouseX(), Ui()->MouseY(), &m_FriendsMoveCategoryPopupContext);
				}
			}
			else if(Action == FRIEND_ACTION_EDIT_NOTE)
			{
				m_FriendNotePopupContext.m_pMenus = this;
				str_copy(m_FriendNotePopupContext.m_aName, m_aFriendActionName, sizeof(m_FriendNotePopupContext.m_aName));
				str_copy(m_FriendNotePopupContext.m_aClan, m_aFriendActionClan, sizeof(m_FriendNotePopupContext.m_aClan));
				m_FriendNotePopupContext.m_NoteInput.Set(GameClient()->Friends()->GetFriendNote(m_aFriendActionName, m_aFriendActionClan));
				m_FriendNotePopupContext.m_NoteInput.SelectAll();
				Ui()->DoPopupMenu(&m_FriendNotePopupContext, Ui()->MouseX(), Ui()->MouseY(), 320.0f, 70.0f, &m_FriendNotePopupContext, PopupFriendNote);
			}
			else if(Action == FRIEND_ACTION_CLEAR_NOTE)
			{
				GameClient()->Friends()->ClearFriendNote(m_aFriendActionName, m_aFriendActionClan);
			}
			else if(Action == FRIEND_ACTION_FOLLOW)
			{
				StartFriendAutoFollow(m_FriendAutoFollowState, m_aFriendActionName, m_aFriendActionClan, m_aFriendActionAddress);
			}
			else if(Action == FRIEND_ACTION_STOP_FOLLOW)
			{
				StopFriendAutoFollow(m_FriendAutoFollowState);
			}
			else if(Action == FRIEND_ACTION_REMOVE)
			{
				str_copy(m_aRemoveFriendName, m_aFriendActionName, sizeof(m_aRemoveFriendName));
				str_copy(m_aRemoveFriendClan, m_aFriendActionClan, sizeof(m_aRemoveFriendClan));
				m_RemoveFriendState = m_FriendActionState;
				m_HasRemoveFriend = true;
				OpenRemovePopup = true;
			}
		}

		m_FriendsActionPopupContext.Reset();
		m_vFriendsActionEntries.clear();
		m_HasFriendAction = false;
		m_aFriendActionName[0] = '\0';
		m_aFriendActionClan[0] = '\0';
		m_aFriendActionAddress[0] = '\0';
		m_FriendActionState = IFriends::FRIEND_NO;
	}
	else if(m_HasFriendAction && !Ui()->IsPopupOpen(&m_FriendsActionPopupContext))
	{
		m_FriendsActionPopupContext.Reset();
		m_vFriendsActionEntries.clear();
		m_HasFriendAction = false;
		m_aFriendActionName[0] = '\0';
		m_aFriendActionClan[0] = '\0';
		m_aFriendActionAddress[0] = '\0';
		m_FriendActionState = IFriends::FRIEND_NO;
	}

	if(m_HasMoveCategoryFriend && m_FriendsMoveCategoryPopupContext.m_pSelection != nullptr)
	{
		const char *pCategory = m_FriendsMoveCategoryPopupContext.m_pSelection->c_str();

		if(pCategory != nullptr && GameClient()->Friends()->SetFriendCategory(m_aMoveCategoryFriendName, m_aMoveCategoryFriendClan, pCategory))
		{
			m_FriendAddCategoryIndex = maximum(0, GameClient()->Friends()->FindCategory(pCategory));
			FriendlistOnUpdate();
			Client()->ServerBrowserUpdate();
		}

		m_FriendsMoveCategoryPopupContext.Reset();
		m_HasMoveCategoryFriend = false;
		m_aMoveCategoryFriendName[0] = '\0';
		m_aMoveCategoryFriendClan[0] = '\0';
	}
	else if(m_HasMoveCategoryFriend && !Ui()->IsPopupOpen(&m_FriendsMoveCategoryPopupContext))
	{
		m_HasMoveCategoryFriend = false;
		m_aMoveCategoryFriendName[0] = '\0';
		m_aMoveCategoryFriendClan[0] = '\0';
	}

	if(OpenRemovePopup && m_HasRemoveFriend)
	{
		char aMessage[256];
		str_format(aMessage, sizeof(aMessage),
			m_RemoveFriendState == IFriends::FRIEND_PLAYER ? Localize("Are you sure that you want to remove the player '%s' from your friends list?") : Localize("Are you sure that you want to remove the clan '%s' from your friends list?"),
			m_RemoveFriendState == IFriends::FRIEND_PLAYER ? m_aRemoveFriendName : m_aRemoveFriendClan);
		PopupConfirm(Localize("Remove friend"), aMessage, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmRemoveFriend, POPUP_NONE, &CMenus::PopupCancelRemoveFriend, POPUP_NONE);
	}

	// add friend
	if(GameClient()->Friends()->NumFriends() < IFriends::MAX_FRIENDS)
	{
		CUIRect Button;
		ServerFriends.Margin(5.0f, &ServerFriends);

		if(m_FriendAutoFollowState.m_Active)
		{
			CUIRect Status, StopButton;
			ServerFriends.HSplitTop(18.0f, &Status, &ServerFriends);
			Status.VSplitRight(64.0f, &Status, &StopButton);
			char aStatus[128];
			if(m_FriendAutoFollowState.m_HasPendingAddress)
				str_format(aStatus, sizeof(aStatus), Localize("Following %s: switching in %.0fs", "Friend auto follow"), m_FriendAutoFollowState.m_aName, maximum(0.0f, m_FriendAutoFollowState.m_PendingConnectTime - Client()->GlobalTime()));
			else
				str_format(aStatus, sizeof(aStatus), Localize("Following %s", "Friend auto follow"), m_FriendAutoFollowState.m_aName);
			Ui()->DoLabel(&Status, aStatus, FontSize, TEXTALIGN_ML);
			static CButtonContainer s_StopFollowButton;
			if(DoButton_Menu(&s_StopFollowButton, Localize("Stop", "Friend auto follow"), 0, &StopButton))
				StopFriendAutoFollow(m_FriendAutoFollowState);
			ServerFriends.HSplitTop(3.0f, nullptr, &ServerFriends);
		}

		ServerFriends.HSplitTop(18.0f, &Button, &ServerFriends);
		str_format(aBuf, sizeof(aBuf), "%s:", Localize("Name"));
		Ui()->DoLabel(&Button, aBuf, FontSize + 2.0f, TEXTALIGN_ML);
		Button.VSplitLeft(80.0f, nullptr, &Button);
		static CLineInputBuffered<MAX_NAME_LENGTH> s_NameInput;
		const IUiContext ServerBrowserAddFriendTextInputCtx = SettingsUiContext("server_browser_add_friend_text_inputs");
		ui_widget::SInputFieldOptions AddFriendInputOptions;
		AddFriendInputOptions.m_FontSize = FontSize + 2.0f;
		ui_widget::InputField(ServerBrowserAddFriendTextInputCtx, &s_NameInput, Button, AddFriendInputOptions);

		ServerFriends.HSplitTop(3.0f, nullptr, &ServerFriends);
		ServerFriends.HSplitTop(18.0f, &Button, &ServerFriends);
		str_format(aBuf, sizeof(aBuf), "%s:", Localize("Clan"));
		Ui()->DoLabel(&Button, aBuf, FontSize + 2.0f, TEXTALIGN_ML);
		Button.VSplitLeft(80.0f, nullptr, &Button);
		static CLineInputBuffered<MAX_CLAN_LENGTH> s_ClanInput;
		ui_widget::InputField(ServerBrowserAddFriendTextInputCtx, &s_ClanInput, Button, AddFriendInputOptions);

		ServerFriends.HSplitTop(3.0f, nullptr, &ServerFriends);
		ServerFriends.HSplitTop(18.0f, &Button, &ServerFriends);
		str_format(aBuf, sizeof(aBuf), "%s:", Localize("Add to category"));
		Ui()->DoLabel(&Button, aBuf, FontSize + 2.0f, TEXTALIGN_ML);
		Button.VSplitLeft(80.0f, nullptr, &Button);
		std::vector<std::string> vDisplayedCategories;
		std::vector<const char *> vpCategories;
		std::vector<int> vCategoryIndices;
		vDisplayedCategories.reserve(NumCategories);
		vpCategories.reserve(NumCategories);
		vCategoryIndices.reserve(NumCategories);
		for(int CategoryIndex = 0; CategoryIndex < NumCategories; ++CategoryIndex)
		{
			const char *pCategory = GameClient()->Friends()->GetCategory(CategoryIndex);
			if(IsClanMembersCategory(pCategory))
				continue;
			vDisplayedCategories.emplace_back(LocalizeFriendsCategory(pCategory));
			vpCategories.push_back(vDisplayedCategories.back().c_str());
			vCategoryIndices.push_back(CategoryIndex);
		}

		if(vCategoryIndices.empty())
		{
			const int DefaultCategoryIndex = maximum(0, GameClient()->Friends()->FindCategory(GameClient()->Friends()->DefaultCategory()));
			vCategoryIndices.push_back(DefaultCategoryIndex);
			vDisplayedCategories.emplace_back(LocalizeFriendsCategory(GameClient()->Friends()->GetCategory(DefaultCategoryIndex)));
			vpCategories.push_back(vDisplayedCategories.back().c_str());
		}

		int DropDownSelection = 0;
		for(int SelectionIndex = 0; SelectionIndex < (int)vCategoryIndices.size(); ++SelectionIndex)
		{
			if(vCategoryIndices[SelectionIndex] == m_FriendAddCategoryIndex)
			{
				DropDownSelection = SelectionIndex;
				break;
			}
		}

		if(m_FriendAddCategoryIndex < 0 || m_FriendAddCategoryIndex >= NumCategories || IsClanMembersCategory(GameClient()->Friends()->GetCategory(m_FriendAddCategoryIndex)))
			m_FriendAddCategoryIndex = vCategoryIndices[DropDownSelection];
		if(!vpCategories.empty())
		{
			static CScrollRegion s_FriendsCategoryDropDownScrollRegion;
			m_FriendsAddCategoryDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_FriendsCategoryDropDownScrollRegion;
			CUIRect CategoryDropDown, CreateCategoryButton;
			Button.VSplitRight(Button.h, &CategoryDropDown, &CreateCategoryButton);
			CategoryDropDown.VSplitRight(3.0f, &CategoryDropDown, nullptr);
			DropDownSelection = Ui()->DoDropDown(&CategoryDropDown, DropDownSelection, vpCategories.data(), (int)vpCategories.size(), m_FriendsAddCategoryDropDownState);
			if(DropDownSelection >= 0 && DropDownSelection < (int)vCategoryIndices.size())
				m_FriendAddCategoryIndex = vCategoryIndices[DropDownSelection];
			if(Ui()->DoButton_FontIcon(&m_FriendsAddCategoryCreateButton, FONT_ICON_PLUS, 0, &CreateCategoryButton, BUTTONFLAG_LEFT))
			{
				m_FriendsCategoryPopupContext.m_pMenus = this;
				m_FriendsCategoryPopupContext.m_CategoryIndex = m_FriendAddCategoryIndex;
				m_FriendsCategoryPopupContext.m_Mode = CFriendsCategoryPopupContext::MODE_ADD;
				m_FriendsCategoryPopupContext.m_NameInput.Clear();
				const CUIRect Panel = CMenus::SecondaryPanelRect(Ui()->MouseX(), Ui()->MouseY(), 300.0f, CMenus::FriendsCategoryEditPopupHeight(), *Ui()->Screen());
				Ui()->DoPopupMenu(&m_FriendsCategoryPopupContext, Panel.x, Panel.y, Panel.w, Panel.h, &m_FriendsCategoryPopupContext, PopupFriendsCategory);
			}
			GameClient()->m_Tooltips.DoToolTip(&m_FriendsAddCategoryCreateButton, &CreateCategoryButton, Localize("Create category"));
		}

		ServerFriends.HSplitTop(3.0f, nullptr, &ServerFriends);
		ServerFriends.HSplitTop(18.0f, &Button, &ServerFriends);
		static CButtonContainer s_AddButton;
		char aAddButtonLabel[128];
		if(s_NameInput.IsEmpty() && !s_ClanInput.IsEmpty())
			str_copy(aAddButtonLabel, Localize("Add clan"));
		else
			str_format(aAddButtonLabel, sizeof(aAddButtonLabel), Localize("Add to %s"), LocalizeFriendsCategory(GameClient()->Friends()->GetCategory(m_FriendAddCategoryIndex)));
		if(DoButton_Menu(&s_AddButton, aAddButtonLabel, 0, &Button))
		{
			const char *pCategory = GameClient()->Friends()->GetCategory(m_FriendAddCategoryIndex);
			GameClient()->Friends()->AddFriend(s_NameInput.GetString(), s_ClanInput.GetString(), pCategory);
			s_NameInput.Clear();
			s_ClanInput.Clear();
			FriendlistOnUpdate();
			Client()->ServerBrowserUpdate();
		}
	}
}

CUi::EPopupMenuFunctionResult CMenus::PopupFriendsCategory(void *pContext, CUIRect View, bool Active)
{
	CFriendsCategoryPopupContext *pPopupContext = static_cast<CFriendsCategoryPopupContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;
	if(pMenus == nullptr)
		return CUi::POPUP_CLOSE_CURRENT;

	IFriends *pFriends = pMenus->GameClient()->Friends();
	if(pPopupContext->m_CategoryIndex < 0 || pPopupContext->m_CategoryIndex >= pFriends->NumCategories())
		return CUi::POPUP_CLOSE_CURRENT;

	const char *pCategory = pFriends->GetCategory(pPopupContext->m_CategoryIndex);
	const bool IsProtectedCategory = IsProtectedFriendsCategory(pCategory);
	const float FontSize = 10.0f;

	View.Margin(5.0f, &View);

	if(pPopupContext->m_Mode == CFriendsCategoryPopupContext::MODE_ACTIONS)
	{
		CUIRect Label, Button;
		View.HSplitTop(12.0f, &Label, &View);
		pMenus->Ui()->DoLabel(&Label, LocalizeFriendsCategory(pCategory), FontSize + 1.0f, TEXTALIGN_ML);

		View.HSplitTop(3.0f, nullptr, &View);
		View.HSplitTop(18.0f, &Button, &View);
		if(pMenus->Ui()->DoButton_PopupMenu(&pPopupContext->m_AddButton, Localize("Add category"), &Button, FontSize, TEXTALIGN_MC))
		{
			pPopupContext->m_Mode = CFriendsCategoryPopupContext::MODE_ADD;
			pPopupContext->m_NameInput.Clear();
			return CUi::POPUP_KEEP_OPEN;
		}

		View.HSplitTop(3.0f, nullptr, &View);
		View.HSplitTop(18.0f, &Button, &View);
		if(pMenus->Ui()->DoButton_PopupMenu(&pPopupContext->m_RenameButton, Localize("Rename"), &Button, FontSize, TEXTALIGN_MC, 0.0f, false, !IsProtectedCategory))
		{
			pPopupContext->m_Mode = CFriendsCategoryPopupContext::MODE_RENAME;
			pPopupContext->m_NameInput.Set(pCategory);
			pPopupContext->m_NameInput.SelectAll();
			return CUi::POPUP_KEEP_OPEN;
		}

		View.HSplitTop(3.0f, nullptr, &View);
		View.HSplitTop(18.0f, &Button, &View);
		if(pMenus->Ui()->DoButton_PopupMenu(&pPopupContext->m_DeleteButton, Localize("Delete category"), &Button, FontSize, TEXTALIGN_MC, 0.0f, false, !IsProtectedCategory))
		{
			if(pFriends->RemoveCategory(pCategory))
				pMenus->FriendlistOnUpdate();
			return CUi::POPUP_CLOSE_CURRENT;
		}

		return CUi::POPUP_KEEP_OPEN;
	}

	CUIRect Label, Input, Buttons, Cancel, Confirm;
	View.HSplitTop(12.0f, &Label, &View);
	pMenus->Ui()->DoLabel(&Label, pPopupContext->m_Mode == CFriendsCategoryPopupContext::MODE_ADD ? Localize("Category name") : Localize("New category name"), FontSize, TEXTALIGN_ML);

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Input, &View);
	const IUiContext FriendsCategoryTextInputCtx = pMenus->SettingsUiContext("server_browser_friends_category_text_input");
	ui_widget::SInputFieldOptions CategoryInputOptions;
	CategoryInputOptions.m_FontSize = FontSize + 1.0f;
	ui_widget::InputField(FriendsCategoryTextInputCtx, &pPopupContext->m_NameInput, Input, CategoryInputOptions);

	View.HSplitTop(6.0f, nullptr, &View);
	View.HSplitTop(20.0f, &Buttons, &View);
	Buttons.VSplitMid(&Cancel, &Confirm, 5.0f);

	const bool CancelPressed = pMenus->Ui()->DoButton_PopupMenu(&pPopupContext->m_CancelButton, Localize("Cancel"), &Cancel, FontSize, TEXTALIGN_MC) || (Active && pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE));
	if(CancelPressed)
		return CUi::POPUP_CLOSE_CURRENT;

	const bool ConfirmPressed = pMenus->Ui()->DoButton_PopupMenu(&pPopupContext->m_ConfirmButton, pPopupContext->m_Mode == CFriendsCategoryPopupContext::MODE_ADD ? Localize("Add") : Localize("Rename"), &Confirm, FontSize, TEXTALIGN_MC) || (Active && pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER));
	if(ConfirmPressed)
	{
		char aCategory[IFriends::MAX_FRIEND_CATEGORY_LENGTH];
		str_copy(aCategory, str_utf8_skip_whitespaces(pPopupContext->m_NameInput.GetString()), sizeof(aCategory));
		str_utf8_trim_right(aCategory);

		if(aCategory[0] != '\0')
		{
			bool Changed = false;
			if(pPopupContext->m_Mode == CFriendsCategoryPopupContext::MODE_ADD)
				Changed = pFriends->AddCategory(aCategory);
			else
				Changed = pFriends->RenameCategory(pCategory, aCategory);

			if(Changed)
			{
				pMenus->FriendlistOnUpdate();
				const int NewCategory = pFriends->FindCategory(aCategory);
				if(NewCategory >= 0)
					pMenus->m_FriendAddCategoryIndex = NewCategory;
			}
		}

		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CMenus::PopupFriendNote(void *pContext, CUIRect View, bool Active)
{
	CFriendNotePopupContext *pPopupContext = static_cast<CFriendNotePopupContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;
	if(pMenus == nullptr)
		return CUi::POPUP_CLOSE_CURRENT;

	const float FontSize = 10.0f;
	View.Margin(5.0f, &View);

	CUIRect Label, Input, Buttons, Cancel, Confirm;
	View.HSplitTop(12.0f, &Label, &View);
	pMenus->Ui()->DoLabel(&Label, Localize("Friend note"), FontSize, TEXTALIGN_ML);

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Input, &View);
	const IUiContext FriendNoteTextInputCtx = pMenus->SettingsUiContext("server_browser_friend_note_text_input");
	ui_widget::SInputFieldOptions NoteInputOptions;
	NoteInputOptions.m_FontSize = FontSize + 1.0f;
	ui_widget::InputField(FriendNoteTextInputCtx, &pPopupContext->m_NoteInput, Input, NoteInputOptions);

	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Buttons, &View);
	Buttons.VSplitMid(&Cancel, &Confirm, 3.0f);

	const bool CancelPressed = pMenus->Ui()->DoButton_PopupMenu(&pPopupContext->m_CancelButton, Localize("Cancel"), &Cancel, FontSize, TEXTALIGN_MC) || (Active && pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE));
	if(CancelPressed)
		return CUi::POPUP_CLOSE_CURRENT;

	const bool ConfirmPressed = pMenus->Ui()->DoButton_PopupMenu(&pPopupContext->m_ConfirmButton, Localize("Save"), &Confirm, FontSize, TEXTALIGN_MC) || (Active && pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER));
	if(ConfirmPressed)
	{
		char aNote[IFriends::MAX_FRIEND_NOTE_LENGTH];
		str_copy(aNote, pPopupContext->m_NoteInput.GetString(), sizeof(aNote));
		str_utf8_trim_right(aNote);

		if(aNote[0] == '\0')
			pMenus->GameClient()->Friends()->ClearFriendNote(pPopupContext->m_aName, pPopupContext->m_aClan);
		else
			pMenus->GameClient()->Friends()->SetFriendNote(pPopupContext->m_aName, pPopupContext->m_aClan, aNote);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

void CMenus::RefreshFriendsCategoryNames()
{
	const int NumCategories = maximum(1, GameClient()->Friends()->NumCategories());
	m_vFriendsCategoryNames.clear();
	m_vFriendsCategoryNames.reserve(NumCategories);
	for(int CategoryIndex = 0; CategoryIndex < NumCategories; ++CategoryIndex)
		m_vFriendsCategoryNames.emplace_back(GameClient()->Friends()->GetCategory(CategoryIndex));
}

void CMenus::ApplyFriendsCategoryExpandedState()
{
	const int NumCategories = maximum(1, GameClient()->Friends()->NumCategories());
	m_vFriendsCategoryExpanded.assign(NumCategories, true);

	const char *pState = g_Config.m_ClFriendsCategoryExpanded;
	const int StateLen = str_length(pState);
	for(int CategoryIndex = 0; CategoryIndex < NumCategories && CategoryIndex < StateLen; ++CategoryIndex)
	{
		if(pState[CategoryIndex] == '0')
			m_vFriendsCategoryExpanded[CategoryIndex] = false;
		else if(pState[CategoryIndex] == '1')
			m_vFriendsCategoryExpanded[CategoryIndex] = true;
	}

	RefreshFriendsCategoryNames();
	m_FriendsCategoryExpandedStateCache = g_Config.m_ClFriendsCategoryExpanded;
	m_FriendsCategoryExpandedLoaded = true;
}

void CMenus::SaveFriendsCategoryExpandedState()
{
	const int NumCategories = maximum(1, GameClient()->Friends()->NumCategories());
	char aState[sizeof(g_Config.m_ClFriendsCategoryExpanded)];
	int Pos = 0;
	for(int CategoryIndex = 0; CategoryIndex < NumCategories && Pos + 1 < (int)sizeof(aState); ++CategoryIndex)
	{
		const bool Expanded = CategoryIndex < (int)m_vFriendsCategoryExpanded.size() && m_vFriendsCategoryExpanded[CategoryIndex] != 0;
		aState[Pos++] = Expanded ? '1' : '0';
	}
	aState[Pos] = '\0';

	while(Pos > 0 && aState[Pos - 1] == '1')
	{
		--Pos;
		aState[Pos] = '\0';
	}

	if(str_comp(aState, g_Config.m_ClFriendsCategoryExpanded) != 0)
		str_copy(g_Config.m_ClFriendsCategoryExpanded, aState, sizeof(g_Config.m_ClFriendsCategoryExpanded));

	m_FriendsCategoryExpandedStateCache = g_Config.m_ClFriendsCategoryExpanded;
	m_FriendsCategoryExpandedLoaded = true;
}

void CMenus::FriendlistOnUpdate()
{
	const int NumCategories = maximum(1, GameClient()->Friends()->NumCategories());
	const bool ConfigChanged = m_FriendsCategoryExpandedLoaded &&
				   str_comp(m_FriendsCategoryExpandedStateCache.c_str(), g_Config.m_ClFriendsCategoryExpanded) != 0;

	if(!m_FriendsCategoryExpandedLoaded || ConfigChanged)
	{
		ApplyFriendsCategoryExpandedState();
	}
	else
	{
		if((int)m_vFriendsCategoryNames.size() != NumCategories)
		{
			std::vector<unsigned char> vExpanded(NumCategories, true);
			for(int CategoryIndex = 0; CategoryIndex < NumCategories; ++CategoryIndex)
			{
				const char *pCategory = GameClient()->Friends()->GetCategory(CategoryIndex);
				for(size_t PrevIndex = 0; PrevIndex < m_vFriendsCategoryNames.size(); ++PrevIndex)
				{
					if(str_comp_nocase(m_vFriendsCategoryNames[PrevIndex].c_str(), pCategory) == 0)
					{
						if(PrevIndex < m_vFriendsCategoryExpanded.size())
							vExpanded[CategoryIndex] = m_vFriendsCategoryExpanded[PrevIndex];
						break;
					}
				}
			}
			m_vFriendsCategoryExpanded.swap(vExpanded);
			RefreshFriendsCategoryNames();
			SaveFriendsCategoryExpandedState();
		}
		else
		{
			bool NamesChanged = false;
			for(int CategoryIndex = 0; CategoryIndex < NumCategories; ++CategoryIndex)
			{
				const char *pCategory = GameClient()->Friends()->GetCategory(CategoryIndex);
				if(str_comp(m_vFriendsCategoryNames[CategoryIndex].c_str(), pCategory) != 0)
				{
					NamesChanged = true;
					break;
				}
			}

			if(NamesChanged)
			{
				RefreshFriendsCategoryNames();
				SaveFriendsCategoryExpandedState();
			}
			else if((int)m_vFriendsCategoryExpanded.size() < NumCategories)
			{
				m_vFriendsCategoryExpanded.resize(NumCategories, true);
			}
			else if((int)m_vFriendsCategoryExpanded.size() > NumCategories)
			{
				m_vFriendsCategoryExpanded.resize(NumCategories);
			}
		}
	}

	if(m_FriendAddCategoryIndex < 0 || m_FriendAddCategoryIndex >= NumCategories)
		m_FriendAddCategoryIndex = 0;

	if(IsClanMembersCategory(GameClient()->Friends()->GetCategory(m_FriendAddCategoryIndex)))
	{
		const int DefaultCategoryIndex = GameClient()->Friends()->FindCategory(GameClient()->Friends()->DefaultCategory());
		m_FriendAddCategoryIndex = DefaultCategoryIndex >= 0 ? DefaultCategoryIndex : 0;
	}
}

void CMenus::PopupConfirmRemoveFriend()
{
	if(!m_HasRemoveFriend)
		return;

	GameClient()->Friends()->RemoveFriend(m_RemoveFriendState == IFriends::FRIEND_PLAYER ? m_aRemoveFriendName : "", m_aRemoveFriendClan);
	FriendlistOnUpdate();
	Client()->ServerBrowserUpdate();
	PopupCancelRemoveFriend();
}

void CMenus::PopupCancelRemoveFriend()
{
	m_HasRemoveFriend = false;
	m_aRemoveFriendName[0] = '\0';
	m_aRemoveFriendClan[0] = '\0';
	m_RemoveFriendState = IFriends::FRIEND_NO;
}

void CMenus::RenderServerbrowserQm(CUIRect View)
{
	const float RowHeight = 18.0f;
	const float FontSize = (RowHeight - 4.0f) * CUi::ms_FontmodHeight;
	const auto &vQmServers = GameClient()->m_QmClient.QmClientServerDistribution();
	const int QmUsers = GameClient()->m_QmClient.QmClientOnlineUserCount();
	const int QmDummies = GameClient()->m_QmClient.QmClientOnlineDummyCount();

	View.Margin(5.0f, &View);

	CUIRect Header, Summary, List, Row;
	View.HSplitTop(15.0f, &Header, &View);
	Ui()->DoLabel(&Header, Localize("QmClient online distribution"), FontSize, TEXTALIGN_ML);
	View.HSplitTop(5.0f, nullptr, &View);
	View.HSplitTop(28.0f, &Summary, &View);

	if(!GameClient()->m_QmClient.HasQmClientRecognitionService())
	{
		Ui()->DoLabel(&Summary, Localize("Set a voice server to enable QmClient distribution"), 9.0f, TEXTALIGN_ML);
		return;
	}

	char aSummary[128];
	if(QmDummies > 0)
		str_format(aSummary, sizeof(aSummary), Localize("%d servers, %d users, %d dummies"), (int)vQmServers.size(), QmUsers, QmDummies);
	else
		str_format(aSummary, sizeof(aSummary), Localize("%d servers, %d users"), (int)vQmServers.size(), QmUsers);
	Ui()->DoLabel(&Summary, aSummary, 9.0f, TEXTALIGN_ML);

	if(vQmServers.empty())
	{
		View.HSplitTop(2.0f, nullptr, &View);
		View.HSplitTop(RowHeight, &Row, &View);
		Ui()->DoLabel(&Row, Localize("No active QmClient reports yet"), FontSize, TEXTALIGN_ML);
		return;
	}

	View.HSplitTop(2.0f, nullptr, &View);
	List = View;

	int SelectedQmIndex = -1;
	for(size_t i = 0; i < vQmServers.size(); ++i)
	{
		if(str_comp(vQmServers[i].m_ServerAddress.c_str(), g_Config.m_UiServerAddress) == 0)
		{
			SelectedQmIndex = (int)i;
			break;
		}
	}

	static CListBox s_QmServerListBox;
	static std::vector<int> s_vQmServerItemIds;
	s_vQmServerItemIds.resize(vQmServers.size());
	s_QmServerListBox.DoAutoSpacing(2.0f);
	s_QmServerListBox.DoStart(40.0f, vQmServers.size(), 1, 3, SelectedQmIndex, &List, false, IGraphics::CORNER_NONE);

	for(size_t i = 0; i < vQmServers.size(); ++i)
	{
		const SQmClientServerDistribution &Entry = vQmServers[i];
		int SortedServerIndex = -1;
		const CServerInfo *pSortedServer = FindSortedServerByAddress(ServerBrowser(), Entry.m_ServerAddress.c_str(), &SortedServerIndex);
		const CServerInfo *pKnownServer = pSortedServer != nullptr ? pSortedServer : FindServerByAddress(ServerBrowser(), Entry.m_ServerAddress.c_str());

		const CListboxItem Item = s_QmServerListBox.DoNextItem(&s_vQmServerItemIds[i], SelectedQmIndex == (int)i);
		if(!Item.m_Visible)
			continue;
		auto GaussianBlurSuppression = Item.SuppressGaussianBlur();

		CUIRect ItemRect, TextRect, CountRect, TitleRect, DetailRect, UsersRect, DummiesRect;
		Item.m_Rect.Margin(4.0f, &ItemRect);
		if(Item.m_Selected)
			ItemRect.Draw(BrowserOpacityColor(ColorRGBA(0.23f, 0.51f, 0.82f, 0.12f)), IGraphics::CORNER_ALL, 6.0f);

		ItemRect.VSplitRight(100.0f, &TextRect, &CountRect);
		TextRect.HSplitTop(18.0f, &TitleRect, &DetailRect);
		CountRect.HSplitTop(18.0f, &UsersRect, &DummiesRect);

		const char *pTitle = pKnownServer != nullptr && pKnownServer->m_aName[0] != '\0' ? pKnownServer->m_aName : Entry.m_ServerAddress.c_str();
		Ui()->DoLabel(&TitleRect, pTitle, FontSize, TEXTALIGN_ML);

		char aDetail[128];
		if(pSortedServer != nullptr && Item.m_Selected)
			str_format(aDetail, sizeof(aDetail), "%s | %s", Localize("Selected server"), pSortedServer->m_aAddress);
		else if(pSortedServer != nullptr)
			str_copy(aDetail, pSortedServer->m_aAddress, sizeof(aDetail));
		else if(pKnownServer != nullptr)
			str_format(aDetail, sizeof(aDetail), "%s | %s", Localize("Not in the current browser list"), pKnownServer->m_aAddress);
		else
			str_copy(aDetail, Localize("Not in the current browser list"), sizeof(aDetail));
		Ui()->DoLabel(&DetailRect, aDetail, 8.0f, TEXTALIGN_ML);

		char aUsers[48];
		str_format(aUsers, sizeof(aUsers), Localize(Entry.m_UserCount == 1 ? "%d user" : "%d users"), Entry.m_UserCount);
		Ui()->DoLabel(&UsersRect, aUsers, 9.0f, TEXTALIGN_MR);

		if(Entry.m_DummyCount > 0)
		{
			char aDummies[48];
			str_format(aDummies, sizeof(aDummies), Localize(Entry.m_DummyCount == 1 ? "%d dummy" : "%d dummies"), Entry.m_DummyCount);
			Ui()->DoLabel(&DummiesRect, aDummies, 8.0f, TEXTALIGN_MR);
		}
	}

	const int NewSelected = s_QmServerListBox.DoEnd();
	if((s_QmServerListBox.WasItemSelected() || s_QmServerListBox.WasItemActivated()) &&
		NewSelected >= 0 &&
		NewSelected < (int)vQmServers.size())
	{
		int SortedServerIndex = -1;
		const CServerInfo *pServerInfo = FindSortedServerByAddress(ServerBrowser(), vQmServers[NewSelected].m_ServerAddress.c_str(), &SortedServerIndex);
		if(pServerInfo != nullptr && SortedServerIndex >= 0)
		{
			m_SelectedIndex = SortedServerIndex;
			str_copy(g_Config.m_UiServerAddress, pServerInfo->m_aAddress);
			m_ServerBrowserShouldRevealSelection = true;
		}
	}
}

void CMenus::RenderServerbrowserFavoriteMaps(CUIRect View)
{
	const QmMapHistoryUi::SWorkspaceMetrics Layout = QmMapHistoryUi::WorkspaceMetrics(View.h);
	View.Margin(Layout.m_OuterMargin, &View);

	static std::vector<SLocalSaveDisplayEntry> s_vSaveEntries;
	static int64_t s_LastSaveReloadTick = 0;
	static bool s_SaveFileExists = false;
	const int64_t Now = time_get();
	if(s_LastSaveReloadTick == 0 || Now - s_LastSaveReloadTick > time_freq() * 2)
	{
		s_vSaveEntries = LoadLocalSaveDisplayEntries(Storage(), s_SaveFileExists);
		s_LastSaveReloadTick = Now;
	}

	char aSavesPath[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, SAVES_FILE, aSavesPath, sizeof(aSavesPath));
	const std::set<std::string> &FavoriteMaps = GameClient()->m_TClient.GetFavoriteMaps();
	const std::vector<QmMapHistory::SMapHistoryRecord> &HistoryEntries = GameClient()->m_TClient.GetMapHistory().Entries();

	static int s_FavoriteMapsWorkspaceTab = 0;
	static CButtonContainer s_aFavoriteMapsWorkspaceTabButtons[3];
	char aaWorkspaceTabLabels[3][128];
	str_format(aaWorkspaceTabLabels[0], sizeof(aaWorkspaceTabLabels[0]), "%s (%d)", Localize("Favorite map"), (int)FavoriteMaps.size());
	str_format(aaWorkspaceTabLabels[1], sizeof(aaWorkspaceTabLabels[1]), "%s (%d)", Localize("Map play history"), (int)HistoryEntries.size());
	str_format(aaWorkspaceTabLabels[2], sizeof(aaWorkspaceTabLabels[2]), "%s (%d)", Localize("Local saves"), (int)s_vSaveEntries.size());

	CUIRect WorkspaceTabs;
	View.HSplitTop(Layout.m_TabHeight, &WorkspaceTabs, &View);
	const ColorRGBA TabActiveColor = BrowserPanelElevatedColor(0.98f);
	const ColorRGBA TabInactiveColor = BrowserPanelColor(0.68f);
	const ColorRGBA TabHoverColor = BrowserPanelElevatedColor(0.84f);
	for(int TabIndex = 0; TabIndex < 3; ++TabIndex)
	{
		CUIRect TabButton;
		WorkspaceTabs.VSplitLeft(WorkspaceTabs.w / (float)(3 - TabIndex), &TabButton, &WorkspaceTabs);
		const int Corners = TabIndex == 0 ? IGraphics::CORNER_L : (TabIndex == 2 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aFavoriteMapsWorkspaceTabButtons[TabIndex], aaWorkspaceTabLabels[TabIndex], s_FavoriteMapsWorkspaceTab == TabIndex, &TabButton, Corners, nullptr, &TabInactiveColor, &TabActiveColor, &TabHoverColor, Layout.m_TabHeight * 0.20f))
			s_FavoriteMapsWorkspaceTab = TabIndex;
	}
	View.HSplitTop(Layout.m_SectionGap, nullptr, &View);

	View.Draw(BrowserPanelColor(0.82f), IGraphics::CORNER_ALL, Layout.m_PanelMargin);
	View.Margin(Layout.m_PanelMargin, &View);
	const char *apWorkspaceTitles[] = {
		Localize("Favorite map"),
		Localize("Map play history"),
		Localize("Local saves"),
	};
	const char *apWorkspaceSubTitles[] = {
		Localize("Your favorite maps appear here"),
		Localize("Unfinished maps are shown first"),
		aSavesPath,
	};
	CUIRect WorkspaceHeader, WorkspaceTitle, WorkspaceSubTitle;
	View.HSplitTop(Layout.m_HeaderHeight, &WorkspaceHeader, &View);
	WorkspaceHeader.HSplitTop(Layout.m_HeaderHeight * 0.62f, &WorkspaceTitle, &WorkspaceSubTitle);
	const float HeaderTextInset = Layout.m_PanelMargin * 0.50f;
	WorkspaceTitle.VMargin(HeaderTextInset, &WorkspaceTitle);
	WorkspaceSubTitle.VMargin(HeaderTextInset, &WorkspaceSubTitle);
	Ui()->DoLabel(&WorkspaceTitle, apWorkspaceTitles[s_FavoriteMapsWorkspaceTab], std::clamp(WorkspaceTitle.h * 0.72f, 13.0f, 15.0f), TEXTALIGN_ML);
	Ui()->DoLabel(&WorkspaceSubTitle, apWorkspaceSubTitles[s_FavoriteMapsWorkspaceTab], std::clamp(WorkspaceSubTitle.h * 0.78f, 8.0f, 10.0f), TEXTALIGN_ML);
	View.HSplitTop(Layout.m_SectionGap, nullptr, &View);

	CUIRect FavoritePanel, HistoryPanel, SavesPanel;
	if(s_FavoriteMapsWorkspaceTab == 0)
		FavoritePanel = View;
	else if(s_FavoriteMapsWorkspaceTab == 1)
		HistoryPanel = View;
	else
		SavesPanel = View;

	static std::unordered_map<std::string, std::string> s_MapCategories;
	static int s_MapCategoryScanIndex = 0;
	static int s_LastNumServers = -1;
	static float s_NextFullScan = 0.0f;
	IServerBrowser *pServerBrowser = ServerBrowser();
	const int NumServers = pServerBrowser ? pServerBrowser->NumSortedServers() : 0;
	if(NumServers != s_LastNumServers)
	{
		s_LastNumServers = NumServers;
		s_MapCategoryScanIndex = 0;
		s_NextFullScan = 0.0f;
	}
	const float LocalNow = Client()->LocalTime();
	if(s_FavoriteMapsWorkspaceTab == 0 && NumServers > 0 && (LocalNow >= s_NextFullScan || s_MapCategoryScanIndex > 0))
	{
		if(s_MapCategoryScanIndex == 0)
		{
			s_MapCategories.clear();
			s_MapCategories.reserve((size_t)NumServers);
		}
		constexpr int ServersPerFrame = 64;
		int ProcessedServers = 0;
		while(s_MapCategoryScanIndex < NumServers && ProcessedServers < ServersPerFrame)
		{
			const CServerInfo *pInfo = pServerBrowser->SortedGet(s_MapCategoryScanIndex);
			++s_MapCategoryScanIndex;
			++ProcessedServers;
			if(!pInfo || pInfo->m_aMap[0] == '\0')
				continue;
			const char *pCategoryKey = FavoriteMapCategoryKeyFromText(pInfo->m_aCommunityType);
			if(!pCategoryKey)
				pCategoryKey = FavoriteMapCategoryKeyFromText(pInfo->m_aName);
			if(!pCategoryKey)
				continue;
			auto It = s_MapCategories.find(pInfo->m_aMap);
			if(It == s_MapCategories.end() || It->second != pCategoryKey)
			{
				s_MapCategories[pInfo->m_aMap] = pCategoryKey;
				GameClient()->m_TClient.UpdateMapCategoryCache(pInfo->m_aMap, pCategoryKey);
			}
		}
		if(s_MapCategoryScanIndex >= NumServers)
		{
			s_MapCategoryScanIndex = 0;
			s_NextFullScan = LocalNow + 2.0f;
		}
	}

	auto GetFavoriteMapCategory = [&](const char *pMapName) -> const char * {
		if(!pMapName || pMapName[0] == '\0')
			return Localize("Unknown");
		const auto It = s_MapCategories.find(pMapName);
		if(It != s_MapCategories.end() && !It->second.empty())
			return FavoriteMapCategoryDisplayName(It->second.c_str());
		const char *pCachedCategory = GameClient()->m_TClient.GetCachedMapCategoryKey(pMapName);
		if(pCachedCategory)
			return FavoriteMapCategoryDisplayName(pCachedCategory);
		return Localize("Unknown");
	};

	auto GetFavoriteMapDifficulty = [&](const char *pMapName, char *pOut, int OutSize) {
		for(const CVoteOptionClient *pOption = GameClient()->m_Voting.FirstOption(); pOption; pOption = pOption->m_pNext)
		{
			if(TryParseVoteMapDifficulty(pOption->m_aDescription, pMapName, pOut, OutSize))
				return;
		}
		str_copy(pOut, Localize("Unknown"), OutSize);
	};

	auto HasLocalSaveForMap = [&](const char *pMapName) {
		if(!s_SaveFileExists || !pMapName || pMapName[0] == '\0')
			return false;
		for(const SLocalSaveDisplayEntry &Entry : s_vSaveEntries)
		{
			if(!Entry.m_Map.empty() && str_comp(Entry.m_Map.c_str(), pMapName) == 0)
				return true;
		}
		return false;
	};

	auto DoFavoriteMapColumnLabel = [this](CUIRect Rect, const char *pText, float FontSize, int Align = TEXTALIGN_ML) {
		Rect.VMargin(4.0f, &Rect);
		SLabelProperties Props;
		Props.m_MaxWidth = Rect.w;
		Props.m_StopAtEnd = true;
		Ui()->DoLabel(&Rect, pText, FontSize, Align, Props);
	};

	auto SplitFavoriteMapColumns = [](CUIRect Row, CUIRect *pMap, CUIRect *pCategory, CUIRect *pDifficulty, CUIRect *pNote, CUIRect *pSaved) {
		const float MapWidth = std::clamp(Row.w * 0.24f, 130.0f, 230.0f);
		const float CategoryWidth = std::clamp(Row.w * 0.15f, 86.0f, 130.0f);
		const float DifficultyWidth = std::clamp(Row.w * 0.13f, 78.0f, 120.0f);
		const float SavedWidth = std::clamp(Row.w * 0.10f, 62.0f, 86.0f);
		Row.VSplitLeft(MapWidth, pMap, &Row);
		Row.VSplitLeft(CategoryWidth, pCategory, &Row);
		Row.VSplitLeft(DifficultyWidth, pDifficulty, &Row);
		Row.VSplitRight(SavedWidth, pNote, pSaved);
	};

	if(s_FavoriteMapsWorkspaceTab == 0)
	{
		if(FavoriteMaps.empty())
		{
			Ui()->DoLabel(&FavoritePanel, Localize("No favorite maps yet"), 13.0f, TEXTALIGN_MC);
		}
		else
		{
			const int NumFavoriteMaps = (int)FavoriteMaps.size();
			CUIRect HeaderRow;
			FavoritePanel.HSplitTop(20.0f, &HeaderRow, &FavoritePanel);
			CUIRect HeaderMap, HeaderCategory, HeaderDifficulty, HeaderNote, HeaderSaved;
			SplitFavoriteMapColumns(HeaderRow, &HeaderMap, &HeaderCategory, &HeaderDifficulty, &HeaderNote, &HeaderSaved);
			TextRender()->TextColor(0.75f, 0.75f, 0.75f, 1.0f);
			DoFavoriteMapColumnLabel(HeaderMap, Localize("Map"), 10.0f);
			DoFavoriteMapColumnLabel(HeaderCategory, Localize("Category"), 10.0f);
			DoFavoriteMapColumnLabel(HeaderDifficulty, Localize("Difficulty stars"), 10.0f);
			DoFavoriteMapColumnLabel(HeaderNote, Localize("Note"), 10.0f);
			DoFavoriteMapColumnLabel(HeaderSaved, Localize("Has save"), 10.0f, TEXTALIGN_MR);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			static CListBox s_FavoriteMapsListBox;
			static std::vector<int> s_vFavoriteMapItemIds;
			s_vFavoriteMapItemIds.resize(NumFavoriteMaps);
			s_FavoriteMapsListBox.DoStart(34.0f, NumFavoriteMaps, 1, 3, -1, &FavoritePanel, false, IGraphics::CORNER_NONE);

			size_t FavoriteMapIndex = 0;
			for(const std::string &MapName : FavoriteMaps)
			{
				const CListboxItem Item = s_FavoriteMapsListBox.DoNextItem(&s_vFavoriteMapItemIds[FavoriteMapIndex], false);
				if(Item.m_Visible)
				{
					auto GaussianBlurSuppression = Item.SuppressGaussianBlur();
					CUIRect Row = Item.m_Rect;
					Row.Margin(4.0f, &Row);
					Row.Draw(BrowserOpacityColor(ColorRGBA(1.0f, 0.85f, 0.0f, 0.08f)), IGraphics::CORNER_ALL, 5.0f);
					Row.Margin(6.0f, &Row);
					CUIRect MapColumn, CategoryColumn, DifficultyColumn, NoteColumn, SavedColumn;
					SplitFavoriteMapColumns(Row, &MapColumn, &CategoryColumn, &DifficultyColumn, &NoteColumn, &SavedColumn);

					char aDifficulty[32];
					GetFavoriteMapDifficulty(MapName.c_str(), aDifficulty, sizeof(aDifficulty));
					const char *pNote = GameClient()->m_TClient.GetMapNote(MapName.c_str());
					const char *pSaved = HasLocalSaveForMap(MapName.c_str()) ? Localize("Yes") : Localize("No");

					TextRender()->TextColor(1.0f, 0.85f, 0.0f, 1.0f);
					DoFavoriteMapColumnLabel(MapColumn, MapName.c_str(), 12.0f);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					DoFavoriteMapColumnLabel(CategoryColumn, GetFavoriteMapCategory(MapName.c_str()), 11.0f);
					DoFavoriteMapColumnLabel(DifficultyColumn, aDifficulty, 11.0f);
					DoFavoriteMapColumnLabel(NoteColumn, pNote && pNote[0] != '\0' ? pNote : Localize("None"), 11.0f);
					DoFavoriteMapColumnLabel(SavedColumn, pSaved, 11.0f, TEXTALIGN_MR);
				}
				++FavoriteMapIndex;
			}
			s_FavoriteMapsListBox.DoEnd();
		}
		return;
	}

	if(s_FavoriteMapsWorkspaceTab == 1)
	{
		static int s_MapHistoryFilter = 0;
		static CButtonContainer s_aMapHistoryFilterButtons[3];
		static CButtonContainer s_ClearFinishedHistoryButton;
		static CButtonContainer s_ClearAllHistoryButton;
		static int s_ClearHistoryConfirmMode = 0;
		static float s_ClearHistoryConfirmUntil = 0.0f;

		if(s_ClearHistoryConfirmMode != 0 && Client()->LocalTime() > s_ClearHistoryConfirmUntil)
			s_ClearHistoryConfirmMode = 0;

		CUIRect FilterArea, ClearArea;
		if(QmMapHistoryUi::StackControls(HistoryPanel.w, Layout.m_ControlHeight))
		{
			CUIRect ControlsArea;
			HistoryPanel.HSplitTop(Layout.m_ControlHeight * 2.0f + Layout.m_SectionGap, &ControlsArea, &HistoryPanel);
			ControlsArea.HSplitTop(Layout.m_ControlHeight, &FilterArea, &ControlsArea);
			ControlsArea.HSplitTop(Layout.m_SectionGap, nullptr, &ControlsArea);
			ControlsArea.HSplitTop(Layout.m_ControlHeight, &ClearArea, nullptr);
		}
		else
		{
			CUIRect ControlsRow;
			HistoryPanel.HSplitTop(Layout.m_ControlHeight, &ControlsRow, &HistoryPanel);
			const float ClearAreaWidth = ControlsRow.w * 0.40f;
			ControlsRow.VSplitRight(ClearAreaWidth, &FilterArea, &ClearArea);
			FilterArea.VSplitRight(Layout.m_SectionGap, &FilterArea, nullptr);
		}

		int UnfinishedCount = 0;
		int FinishedCount = 0;
		for(const QmMapHistory::SMapHistoryRecord &Record : HistoryEntries)
		{
			if(Record.m_Finished)
				++FinishedCount;
			else
				++UnfinishedCount;
		}
		char aaFilterLabels[3][64];
		str_format(aaFilterLabels[0], sizeof(aaFilterLabels[0]), "%s (%d)", Localize("Unfinished"), UnfinishedCount);
		str_format(aaFilterLabels[1], sizeof(aaFilterLabels[1]), "%s (%d)", Localize("Finished"), FinishedCount);
		str_format(aaFilterLabels[2], sizeof(aaFilterLabels[2]), "%s (%d)", Localize("Recent"), (int)HistoryEntries.size());
		for(int i = 0; i < 3; ++i)
		{
			CUIRect Button;
			FilterArea.VSplitLeft(FilterArea.w / (float)(3 - i), &Button, &FilterArea);
			const int Corners = i == 0 ? IGraphics::CORNER_L : (i == 2 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
			if(DoButton_MenuTab(&s_aMapHistoryFilterButtons[i], aaFilterLabels[i], s_MapHistoryFilter == i, &Button, Corners, nullptr, &TabInactiveColor, &TabActiveColor, &TabHoverColor, 5.0f))
				s_MapHistoryFilter = i;
		}

		CUIRect ClearFinishedButton, ClearAllButton;
		ClearArea.VSplitMid(&ClearFinishedButton, &ClearAllButton, 4.0f);
		ClearFinishedButton.Margin(2.0f, &ClearFinishedButton);
		ClearAllButton.Margin(2.0f, &ClearAllButton);

		const bool ConfirmFinished = s_ClearHistoryConfirmMode == 1;
		const bool ConfirmAll = s_ClearHistoryConfirmMode == 2;
		if(DoButton_Menu(&s_ClearFinishedHistoryButton, ConfirmFinished ? Localize("Confirm clear finished") : Localize("Clear finished"), 0, &ClearFinishedButton))
		{
			if(ConfirmFinished)
			{
				GameClient()->m_TClient.ClearFinishedMapHistory();
				s_ClearHistoryConfirmMode = 0;
			}
			else
			{
				s_ClearHistoryConfirmMode = 1;
				s_ClearHistoryConfirmUntil = Client()->LocalTime() + 3.0f;
			}
		}
		if(DoButton_Menu(&s_ClearAllHistoryButton, ConfirmAll ? Localize("Confirm clear all") : Localize("Clear all history"), 0, &ClearAllButton))
		{
			if(ConfirmAll)
			{
				GameClient()->m_TClient.ClearAllMapHistory();
				s_ClearHistoryConfirmMode = 0;
			}
			else
			{
				s_ClearHistoryConfirmMode = 2;
				s_ClearHistoryConfirmUntil = Client()->LocalTime() + 3.0f;
			}
		}

		HistoryPanel.HSplitTop(Layout.m_SectionGap, nullptr, &HistoryPanel);

		QmMapHistory::EMapHistoryFilter Filter = QmMapHistory::EMapHistoryFilter::UNFINISHED;
		if(s_MapHistoryFilter == 1)
			Filter = QmMapHistory::EMapHistoryFilter::FINISHED;
		else if(s_MapHistoryFilter == 2)
			Filter = QmMapHistory::EMapHistoryFilter::RECENT;
		const std::vector<QmMapHistory::SMapHistoryRecord> vRecords = GameClient()->m_TClient.GetMapHistoryRecords(Filter);
		if(vRecords.empty())
		{
			Ui()->DoLabel(&HistoryPanel, HistoryEntries.empty() ? Localize("No map play history yet") : Localize("No results"), 13.0f, TEXTALIGN_MC);
		}
		else
		{
			static CListBox s_MapHistoryListBox;
			static std::vector<int> s_vMapHistoryItemIds;
			static std::vector<CButtonContainer> s_vMapHistoryRemoveButtons;
			s_vMapHistoryItemIds.resize(vRecords.size());
			s_vMapHistoryRemoveButtons.resize(vRecords.size());
			const float CardRowHeight = QmMapHistoryUi::CardRowHeight(HistoryPanel.h);
			const int HistoryGridColumns = QmMapHistoryUi::GridColumns(HistoryPanel.w - QmMapHistoryUi::LIST_SCROLLBAR_WIDTH, CardRowHeight);
			s_MapHistoryListBox.SetScrollbarAlwaysReserved(true);
			s_MapHistoryListBox.DoStart(CardRowHeight, (int)vRecords.size(), HistoryGridColumns, 1, -1, &HistoryPanel, false, IGraphics::CORNER_NONE);

			auto DoHistoryCardLabel = [this](CUIRect Rect, const char *pText, float FontSize, int Align) {
				SLabelProperties Props;
				Props.m_MaxWidth = Rect.w;
				Props.m_StopAtEnd = true;
				Props.m_EllipsisAtEnd = true;
				Ui()->DoLabel(&Rect, pText, FontSize, Align, Props);
			};

			std::string RemoveMapId;
			for(size_t HistoryIndex = 0; HistoryIndex < vRecords.size(); ++HistoryIndex)
			{
				const QmMapHistory::SMapHistoryRecord &Record = vRecords[HistoryIndex];
				const CListboxItem Item = s_MapHistoryListBox.DoNextItem(&s_vMapHistoryItemIds[HistoryIndex], false);
				if(!Item.m_Visible)
					continue;
				auto GaussianBlurSuppression = Item.SuppressGaussianBlur();

				char aDeaths[32];
				str_format(aDeaths, sizeof(aDeaths), "%d", Record.m_DeathCount);
				char aLastEntered[64];
				if(Record.m_LastEnteredAt > 0)
					str_timestamp_ex((time_t)Record.m_LastEnteredAt, aLastEntered, sizeof(aLastEntered), FORMAT_SPACE);
				else if(!Record.m_LastPlayedDate.empty())
					str_copy(aLastEntered, Record.m_LastPlayedDate.c_str(), sizeof(aLastEntered));
				else
					str_copy(aLastEntered, Localize("Unknown"), sizeof(aLastEntered));

				char aTime[64];
				const int64_t TimeMs = Record.m_Finished ? Record.m_FinishTimeMs : Record.m_PlayTimeMs;
				str_time(TimeMs / 10, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));

				const ColorRGBA StatusColor = Record.m_Finished ? ColorRGBA(0.36f, 0.80f, 0.55f, 1.0f) : ColorRGBA(1.0f, 0.78f, 0.18f, 1.0f);
				const ColorRGBA CardColor = Record.m_Finished ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.055f) : ColorRGBA(1.0f, 0.78f, 0.18f, 0.09f);
				const float CardGap = QmMapHistoryUi::CardGap(CardRowHeight);
				CUIRect Card = Item.m_Rect;
				Card.Margin(CardGap * 0.5f, &Card);
				Card.Draw(BrowserOpacityColor(CardColor), IGraphics::CORNER_ALL, CardGap * 0.80f);

				CUIRect Accent, CardContent;
				const float AccentWidth = std::clamp(CardRowHeight * 0.04f, 2.0f, 3.0f);
				Card.VSplitLeft(AccentWidth, &Accent, &CardContent);
				Accent.Draw(BrowserOpacityColor(StatusColor, 2.0f), IGraphics::CORNER_L, CardGap * 0.80f);
				CardContent.Margin(QmMapHistoryUi::CardPadding(CardRowHeight), &CardContent);
				CUIRect CardHeader, MetricsRow;
				CardContent.HSplitTop(CardContent.h * 0.43f, &CardHeader, &MetricsRow);
				MetricsRow.HSplitTop(CardGap * 0.20f, nullptr, &MetricsRow);

				CUIRect MapName, Status, RemoveButton;
				CardHeader.VSplitRight(CardHeader.h, &CardHeader, &RemoveButton);
				const float StatusWidth = std::clamp(CardHeader.w * 0.28f, 48.0f, 68.0f);
				CardHeader.VSplitRight(StatusWidth, &MapName, &Status);
				MapName.VSplitRight(CardGap * 0.40f, &MapName, nullptr);
				DoHistoryCardLabel(MapName, Record.m_MapName.c_str(), std::clamp(CardHeader.h * 0.62f, 10.5f, 12.0f), TEXTALIGN_ML);
				TextRender()->TextColor(StatusColor);
				DoHistoryCardLabel(Status, Record.m_Finished ? Localize("Finished") : Localize("Unfinished"), std::clamp(CardHeader.h * 0.48f, 8.0f, 9.5f), TEXTALIGN_MR);
				TextRender()->TextColor(TextRender()->DefaultTextColor());

				CUIRect PrimaryMetrics, TimeMetric, DeathMetric, LastEnteredMetric;
				MetricsRow.VSplitRight(MetricsRow.w * 0.48f, &PrimaryMetrics, &LastEnteredMetric);
				PrimaryMetrics.VSplitRight(PrimaryMetrics.w * 0.38f, &TimeMetric, &DeathMetric);
				TimeMetric.VSplitRight(CardGap * 0.20f, &TimeMetric, nullptr);
				DeathMetric.VMargin(CardGap * 0.10f, &DeathMetric);
				LastEnteredMetric.VSplitLeft(CardGap * 0.20f, nullptr, &LastEnteredMetric);
				auto RenderMetric = [&](CUIRect Metric, const char *pLabel, const char *pValue, int Align) {
					CUIRect Label, Value;
					Metric.HSplitTop(Metric.h * 0.42f, &Label, &Value);
					TextRender()->TextColor(0.68f, 0.68f, 0.68f, 1.0f);
					DoHistoryCardLabel(Label, pLabel, std::clamp(Label.h * 0.72f, 6.5f, 8.0f), Align);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					DoHistoryCardLabel(Value, pValue, std::clamp(Value.h * 0.72f, 8.0f, 9.5f), Align);
				};
				RenderMetric(TimeMetric, Localize("Time"), aTime, TEXTALIGN_ML);
				RenderMetric(DeathMetric, Localize("Deaths"), aDeaths, TEXTALIGN_MC);
				RenderMetric(LastEnteredMetric, Localize("Last entered"), aLastEntered, TEXTALIGN_MR);

				if(Ui()->DoButton_FontIcon(&s_vMapHistoryRemoveButtons[HistoryIndex], FONT_ICON_XMARK, 0, &RemoveButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL))
					RemoveMapId = Record.m_MapId;
				if(Ui()->HotItem() == &s_vMapHistoryRemoveButtons[HistoryIndex])
					GameClient()->m_Tooltips.DoToolTip(&s_vMapHistoryRemoveButtons[HistoryIndex], &RemoveButton, Localize("Remove from history"));
			}
			s_MapHistoryListBox.DoEnd();
			if(!RemoveMapId.empty())
				GameClient()->m_TClient.RemoveMapHistoryRecord(RemoveMapId.c_str());
		}
		return;
	}

	if(!s_SaveFileExists)
	{
		Ui()->DoLabel(&SavesPanel, Localize("ddnet-saves.txt not found"), 13.0f, TEXTALIGN_MC);
		return;
	}
	if(s_vSaveEntries.empty())
	{
		Ui()->DoLabel(&SavesPanel, Localize("ddnet-saves.txt is empty"), 13.0f, TEXTALIGN_MC);
		return;
	}

	const int NumSaveEntries = (int)s_vSaveEntries.size();
	static CListBox s_SavesListBox;
	static std::vector<int> s_vSaveItemIds;
	s_vSaveItemIds.resize(NumSaveEntries);
	s_SavesListBox.DoStart(42.0f, NumSaveEntries, 1, 3, -1, &SavesPanel, false, IGraphics::CORNER_NONE);

	for(size_t SaveIndex = 0; SaveIndex < s_vSaveEntries.size(); ++SaveIndex)
	{
		const SLocalSaveDisplayEntry &Entry = s_vSaveEntries[SaveIndex];
		const CListboxItem Item = s_SavesListBox.DoNextItem(&s_vSaveItemIds[SaveIndex], false);
		if(!Item.m_Visible)
			continue;
		auto GaussianBlurSuppression = Item.SuppressGaussianBlur();

		CUIRect Row, TopLine, BottomLine, TimeLabel;
		Item.m_Rect.Margin(4.0f, &Row);
		Row.Draw(BrowserOpacityColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f)), IGraphics::CORNER_ALL, 5.0f);
		Row.Margin(6.0f, &Row);
		Row.HSplitTop(17.0f, &TopLine, &BottomLine);
		BottomLine.VSplitRight(105.0f, &BottomLine, &TimeLabel);

		const char *pMap = Entry.m_Map.empty() ? Entry.m_RawLine.c_str() : Entry.m_Map.c_str();
		SLabelProperties Props;
		Props.m_MaxWidth = TopLine.w;
		Props.m_StopAtEnd = true;
		Ui()->DoLabel(&TopLine, pMap, 12.0f, TEXTALIGN_ML, Props);

		char aDetail[512];
		str_format(aDetail, sizeof(aDetail), "[%s] %s",
			Entry.m_Players.empty() ? "Unknown" : Entry.m_Players.c_str(),
			Entry.m_Code.empty() ? "no-code" : Entry.m_Code.c_str());
		Props.m_MaxWidth = BottomLine.w;
		Ui()->DoLabel(&BottomLine, aDetail, 10.0f, TEXTALIGN_ML, Props);
		Ui()->DoLabel(&TimeLabel, Entry.m_Time.empty() ? "-" : Entry.m_Time.c_str(), 9.0f, TEXTALIGN_MR);
	}
	s_SavesListBox.DoEnd();
}

enum
{
	UI_TOOLBOX_PAGE_FILTERS = 0,
	UI_TOOLBOX_PAGE_INFO,
	UI_TOOLBOX_PAGE_FRIENDS,
	UI_TOOLBOX_PAGE_QM,
	NUM_UI_TOOLBOX_PAGES,
};

void CMenus::RenderServerbrowserTabBar(CUIRect TabBar)
{
	CUIRect FilterTabButton, InfoTabButton, FriendsTabButton, QmTabButton;
	const bool UseNewUi = g_Config.m_QmNewUi != 0;
	TabBar.VSplitLeft(TabBar.w / 4.0f, &FilterTabButton, &TabBar);
	TabBar.VSplitLeft(TabBar.w / 3.0f, &InfoTabButton, &TabBar);
	TabBar.VSplitLeft(TabBar.w / 2.0f, &FriendsTabButton, &QmTabButton);
	FilterTabButton.VSplitRight(3.0f, &FilterTabButton, nullptr);
	InfoTabButton.VSplitLeft(3.0f, nullptr, &InfoTabButton);
	InfoTabButton.VSplitRight(3.0f, &InfoTabButton, nullptr);
	FriendsTabButton.VSplitLeft(3.0f, nullptr, &FriendsTabButton);
	FriendsTabButton.VSplitRight(3.0f, &FriendsTabButton, nullptr);
	QmTabButton.VSplitLeft(3.0f, nullptr, &QmTabButton);

	const ColorRGBA ColorActive = UseNewUi ? BrowserPanelElevatedColor(0.92f) : ms_ColorTabbarActive;
	const ColorRGBA ColorInactive = UseNewUi ? BrowserPanelColor(0.70f) : ms_ColorTabbarInactive;
	const ColorRGBA ColorHover = UseNewUi ? BrowserPanelElevatedColor(0.82f) : ms_ColorTabbarHover;

	if(!Ui()->IsPopupOpen() && Ui()->ConsumeHotkey(CUi::HOTKEY_TAB))
	{
		const int Direction = Input()->ShiftIsPressed() ? -1 : 1;
		g_Config.m_UiToolboxPage = (g_Config.m_UiToolboxPage + NUM_UI_TOOLBOX_PAGES + Direction) % NUM_UI_TOOLBOX_PAGES;
	}

	if(UseNewUi)
	{
		// 胶囊 Tabbar：槽位先算完，再画容器与滑块，最后画页签图标/文字。
		const CUIRect aToolboxTabSlots[] = {FilterTabButton, InfoTabButton, FriendsTabButton, QmTabButton};
		const int aToolboxTabPages[] = {UI_TOOLBOX_PAGE_FILTERS, UI_TOOLBOX_PAGE_INFO, UI_TOOLBOX_PAGE_FRIENDS, UI_TOOLBOX_PAGE_QM};
		int ActiveToolboxTab = -1;
		for(size_t Tab = 0; Tab < std::size(aToolboxTabSlots); ++Tab)
		{
			if(g_Config.m_UiToolboxPage == aToolboxTabPages[Tab])
				ActiveToolboxTab = (int)Tab;
		}
		const IUiContext ToolboxTabBarCtx = TabBarUiContext();
		ui_widget::CapsuleTabBarChrome(ToolboxTabBarCtx, MakeUiScopeHash("browser_toolbox_tabs_capsule"), ui_widget::CapsuleTabBarRowRect(aToolboxTabSlots, std::size(aToolboxTabSlots)), ActiveToolboxTab >= 0 ? &aToolboxTabSlots[ActiveToolboxTab] : nullptr, CapsuleTabBarStyleFor(BrowserPanelColor(1.0f)));

		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

		static CButtonContainer s_FilterTabButton;
		if(DoButton_MenuTab(&s_FilterTabButton, FONT_ICON_LIST_UL, g_Config.m_UiToolboxPage == UI_TOOLBOX_PAGE_FILTERS, &FilterTabButton, IGraphics::CORNER_ALL, &m_aAnimatorsSmallPage[SMALL_TAB_BROWSER_FILTER], nullptr, nullptr, nullptr, 10.0f, nullptr, nullptr, -1.0f, true))
			g_Config.m_UiToolboxPage = UI_TOOLBOX_PAGE_FILTERS;
		GameClient()->m_Tooltips.DoToolTip(&s_FilterTabButton, &FilterTabButton, Localize("Server filter"));

		static CButtonContainer s_InfoTabButton;
		if(DoButton_MenuTab(&s_InfoTabButton, FONT_ICON_INFO, g_Config.m_UiToolboxPage == UI_TOOLBOX_PAGE_INFO, &InfoTabButton, IGraphics::CORNER_ALL, &m_aAnimatorsSmallPage[SMALL_TAB_BROWSER_INFO], nullptr, nullptr, nullptr, 10.0f, nullptr, nullptr, -1.0f, true))
			g_Config.m_UiToolboxPage = UI_TOOLBOX_PAGE_INFO;
		GameClient()->m_Tooltips.DoToolTip(&s_InfoTabButton, &InfoTabButton, Localize("Server info"));

		static CButtonContainer s_FriendsTabButton;
		if(DoButton_MenuTab(&s_FriendsTabButton, FONT_ICON_HEART, g_Config.m_UiToolboxPage == UI_TOOLBOX_PAGE_FRIENDS, &FriendsTabButton, IGraphics::CORNER_ALL, &m_aAnimatorsSmallPage[SMALL_TAB_BROWSER_FRIENDS], nullptr, nullptr, nullptr, 10.0f, nullptr, nullptr, -1.0f, true))
			g_Config.m_UiToolboxPage = UI_TOOLBOX_PAGE_FRIENDS;
		GameClient()->m_Tooltips.DoToolTip(&s_FriendsTabButton, &FriendsTabButton, Localize("Friends"));

		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		static CButtonContainer s_QmTabButton;
		if(DoButton_MenuTab(&s_QmTabButton, Localize("Qm"), g_Config.m_UiToolboxPage == UI_TOOLBOX_PAGE_QM, &QmTabButton, IGraphics::CORNER_ALL, &m_aAnimatorsSmallPage[SMALL_TAB_BROWSER_QM], nullptr, nullptr, nullptr, 10.0f, nullptr, nullptr, -1.0f, true))
			g_Config.m_UiToolboxPage = UI_TOOLBOX_PAGE_QM;
		GameClient()->m_Tooltips.DoToolTip(&s_QmTabButton, &QmTabButton, Localize("QmClient"));

		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		return;
	}

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

	static CButtonContainer s_FilterTabButton;
	if(DoButton_MenuTab(&s_FilterTabButton, FONT_ICON_LIST_UL, g_Config.m_UiToolboxPage == UI_TOOLBOX_PAGE_FILTERS, &FilterTabButton, IGraphics::CORNER_ALL, &m_aAnimatorsSmallPage[SMALL_TAB_BROWSER_FILTER], &ColorInactive, &ColorActive, &ColorHover))
	{
		g_Config.m_UiToolboxPage = UI_TOOLBOX_PAGE_FILTERS;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_FilterTabButton, &FilterTabButton, Localize("Server filter"));

	static CButtonContainer s_InfoTabButton;
	if(DoButton_MenuTab(&s_InfoTabButton, FONT_ICON_INFO, g_Config.m_UiToolboxPage == UI_TOOLBOX_PAGE_INFO, &InfoTabButton, IGraphics::CORNER_ALL, &m_aAnimatorsSmallPage[SMALL_TAB_BROWSER_INFO], &ColorInactive, &ColorActive, &ColorHover))
	{
		g_Config.m_UiToolboxPage = UI_TOOLBOX_PAGE_INFO;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_InfoTabButton, &InfoTabButton, Localize("Server info"));

	static CButtonContainer s_FriendsTabButton;
	if(DoButton_MenuTab(&s_FriendsTabButton, FONT_ICON_HEART, g_Config.m_UiToolboxPage == UI_TOOLBOX_PAGE_FRIENDS, &FriendsTabButton, IGraphics::CORNER_ALL, &m_aAnimatorsSmallPage[SMALL_TAB_BROWSER_FRIENDS], &ColorInactive, &ColorActive, &ColorHover))
	{
		g_Config.m_UiToolboxPage = UI_TOOLBOX_PAGE_FRIENDS;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_FriendsTabButton, &FriendsTabButton, Localize("Friends"));

	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	static CButtonContainer s_QmTabButton;
	if(DoButton_MenuTab(&s_QmTabButton, Localize("Qm"), g_Config.m_UiToolboxPage == UI_TOOLBOX_PAGE_QM, &QmTabButton, IGraphics::CORNER_ALL, &m_aAnimatorsSmallPage[SMALL_TAB_BROWSER_QM], &ColorInactive, &ColorActive, &ColorHover))
	{
		g_Config.m_UiToolboxPage = UI_TOOLBOX_PAGE_QM;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_QmTabButton, &QmTabButton, Localize("QmClient"));

	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

void CMenus::RenderServerbrowserToolBox(CUIRect ToolBox)
{
	static int s_PrevToolboxPage = UI_TOOLBOX_PAGE_FILTERS;
	static float s_ToolboxDirection = 0.0f;
	if(g_Config.m_UiToolboxPage != s_PrevToolboxPage)
	{
		s_ToolboxDirection = g_Config.m_UiToolboxPage > s_PrevToolboxPage ? 1.0f : -1.0f;
		TriggerUiSwitchAnimation(UiAnimNodeKey("browser_toolbox_tab_switch"), 0.18f);
		s_PrevToolboxPage = g_Config.m_UiToolboxPage;
	}

	const float TransitionStrength = ReadUiSwitchAnimation(UiAnimNodeKey("browser_toolbox_tab_switch"));
	const bool TransitionActive = TransitionStrength > 0.0f && s_ToolboxDirection != 0.0f;
	const float TransitionAlpha = UiSwitchAnimationAlpha(TransitionStrength);
	const CUIRect ContentClip = ToolBox;
	if(TransitionActive)
	{
		Ui()->ClipEnable(&ContentClip);
		ApplyUiSwitchOffset(ToolBox, TransitionStrength, s_ToolboxDirection, false, 0.08f, 24.0f, 120.0f);
	}

	switch(g_Config.m_UiToolboxPage)
	{
	case UI_TOOLBOX_PAGE_FILTERS:
		RenderServerbrowserFilters(ToolBox);
		break;
	case UI_TOOLBOX_PAGE_INFO:
		RenderServerbrowserInfo(ToolBox);
		break;
	case UI_TOOLBOX_PAGE_FRIENDS:
		RenderServerbrowserFriends(ToolBox);
		break;
	case UI_TOOLBOX_PAGE_QM:
		RenderServerbrowserQm(ToolBox);
		break;
	default:
		dbg_assert_failed("ui_toolbox_page invalid");
		break;
	}

	if(TransitionActive)
	{
		if(TransitionAlpha > 0.0f)
			DrawUiSwitchTransitionOverlay(ContentClip, BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha)));
		Ui()->ClipDisable();
	}
}

void CMenus::RenderServerbrowser(CUIRect MainView, bool DrawBackground)
{
	CUiBackgroundAlphaScaleScope BackgroundAlphaScaleScope(Ui(), g_Config.m_QmMapBrowserOpacity / 100.0f);

	UpdateCommunityCache(false);

	switch(g_Config.m_UiPage)
	{
	case PAGE_INTERNET:
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_BROWSER_INTERNET);
		break;
	case PAGE_LAN:
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_BROWSER_LAN);
		if(m_ForceRefreshLanPage)
		{
			RefreshBrowserTab(true);
			m_ForceRefreshLanPage = false;
		}
		break;
	case PAGE_FAVORITES:
	case PAGE_FAVORITE_MAPS:
		GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_BROWSER_FAVORITES);
		break;
	case PAGE_FAVORITE_COMMUNITY_1:
	case PAGE_FAVORITE_COMMUNITY_2:
	case PAGE_FAVORITE_COMMUNITY_3:
	case PAGE_FAVORITE_COMMUNITY_4:
	case PAGE_FAVORITE_COMMUNITY_5:
		GameClient()->m_MenuBackground.ChangePosition(g_Config.m_UiPage - PAGE_FAVORITE_COMMUNITY_1 + CMenuBackground::POS_BROWSER_CUSTOM0);
		break;
	default:
		dbg_assert_failed("ui_page invalid for RenderServerbrowser: %d", g_Config.m_UiPage);
	}

	// clang-format off
	/*
		+---------------------------+ +---communities---+
		|                           | |                 |
		|                           | +------tabs-------+
		|       server list         | |                 |
		|                           | |      tool       |
		|                           | |      box        |
		+---------------------------+ |                 |
		        status box            +-----------------+
	*/
	// clang-format on

	(void)DrawBackground;
	const bool UseNewUi = g_Config.m_QmNewUi != 0;
	if(g_Config.m_UiPage == PAGE_FAVORITE_MAPS)
	{
		if(UseNewUi)
		{
			CUIRect View = MainView;
			View.Margin(6.0f, &View);
			RenderServerbrowserFavoriteMaps(View);
		}
		else
		{
			RenderServerbrowserFavoriteMaps(MainView);
		}
		return;
	}

	CUIRect View = MainView;
	if(UseNewUi)
		View.Margin(6.0f, &View);
	else
	{
		View.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, ui_token::radius::CARD);
		View.Margin(10.0f, &View);
	}

	CUIRect ServerListBase, StatusBox, ToolBoxBase, TabBar;
	CUIRect ContentLayout = View;
	CUIRect ServerListWithGap;
	const float ToolBoxWidth = UseNewUi ? 205.0f : 188.0f;
	const float ColumnGap = UseNewUi ? 10.0f : 6.0f;
	const float StatusHeight = UseNewUi ? 84.0f : 76.0f;
	ContentLayout.VSplitRight(ToolBoxWidth, &ServerListWithGap, &ToolBoxBase);
	ServerListWithGap.VSplitRight(ColumnGap, &ServerListBase, nullptr);
	CUIRect ServerListStackBase = ServerListBase;
	ServerListStackBase.HSplitBottom(StatusHeight, &ServerListBase, &StatusBox);
	StatusBox.y = ServerListStackBase.y + ServerListStackBase.h - StatusHeight;
	ServerListBase.h = maximum(StatusBox.y - ColumnGap - ServerListBase.y, 0.0f);
	if(UseNewUi)
	{
		ServerListBase.Draw(BrowserPanelColor(), IGraphics::CORNER_ALL, ui_token::radius::CARD);
		StatusBox.Draw(BrowserPanelElevatedColor(), IGraphics::CORNER_ALL, ui_token::radius::CARD);
		ToolBoxBase.Draw(BrowserPanelColor(), IGraphics::CORNER_ALL, ui_token::radius::CARD);
		ServerListBase.Margin(10.0f, &ServerListBase);
		StatusBox.Margin(10.0f, &StatusBox);
		ToolBoxBase.Margin(10.0f, &ToolBoxBase);
	}
	else
	{
		ServerListBase.Margin(std::clamp(ServerListBase.w * 0.006f, 1.0f, 4.0f), &ServerListBase);
		StatusBox.Margin(std::clamp(StatusBox.w * 0.006f, 1.0f, 4.0f), &StatusBox);
		ToolBoxBase.Margin(std::clamp(ToolBoxBase.w * 0.006f, 1.0f, 4.0f), &ToolBoxBase);
	}

	float TransitionOffset = 0.0f;
	const float TransitionStrength = ReadUiSwitchAnimation(UiAnimNodeKey("browser_page_switch"));
	const bool DoClip = TransitionStrength > 0.0f && m_BrowserTabTransitionDirection != 0.0f;
	float TransitionAlpha = UiSwitchAnimationAlpha(TransitionStrength);
	if(DoClip)
	{
		TransitionOffset = TransitionStrength * std::clamp(View.w * 0.08f, 24.0f, 120.0f) * m_BrowserTabTransitionDirection;
	}

	bool WasListboxItemActivated = false;
	{
		CUIRect ServerList = ServerListBase;
		if(DoClip)
		{
			Ui()->ClipEnable(&ServerListBase);
			ServerList.x += TransitionOffset;
		}
		RenderServerbrowserServerList(ServerList, WasListboxItemActivated);
		if(DoClip)
		{
			if(TransitionAlpha > 0.0f)
			{
				DrawUiSwitchTransitionOverlay(ServerListBase, BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha)));
			}
			Ui()->ClipDisable();
		}
	}

	RenderServerbrowserStatusBox(StatusBox, WasListboxItemActivated);

	{
		CUIRect ToolBox = ToolBoxBase;
		if(DoClip)
		{
			Ui()->ClipEnable(&ToolBoxBase);
			ToolBox.x += TransitionOffset;
		}

		if(g_Config.m_UiPage == PAGE_INTERNET || g_Config.m_UiPage == PAGE_FAVORITES)
		{
			CUIRect CommunityFilter;
			ToolBox.HSplitTop(19.0f + 4.0f * 17.0f, &CommunityFilter, &ToolBox);
			ToolBox.HSplitTop(8.0f, nullptr, &ToolBox);
			RenderServerbrowserCommunitiesFilter(CommunityFilter);
		}

		ToolBox.HSplitTop(24.0f, &TabBar, &ToolBox);
		RenderServerbrowserTabBar(TabBar);
		RenderServerbrowserToolBox(ToolBox);

		if(DoClip)
		{
			if(TransitionAlpha > 0.0f)
			{
				DrawUiSwitchTransitionOverlay(ToolBoxBase, BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha)));
			}
			Ui()->ClipDisable();
		}
	}
}

template<typename F>
bool CMenus::PrintHighlighted(const char *pName, F &&PrintFn)
{
	const char *pStr = g_Config.m_BrFilterString;
	char aFilterStr[sizeof(g_Config.m_BrFilterString)];
	char aFilterStrTrimmed[sizeof(g_Config.m_BrFilterString)];
	while((pStr = str_next_token(pStr, IServerBrowser::SEARCH_EXCLUDE_TOKEN, aFilterStr, sizeof(aFilterStr))))
	{
		str_copy(aFilterStrTrimmed, str_utf8_skip_whitespaces(aFilterStr));
		str_utf8_trim_right(aFilterStrTrimmed);
		// highlight the parts that matches
		const char *pFilteredStr;
		int FilterLen = str_length(aFilterStrTrimmed);
		if(aFilterStrTrimmed[0] == '"' && aFilterStrTrimmed[FilterLen - 1] == '"')
		{
			aFilterStrTrimmed[FilterLen - 1] = '\0';
			pFilteredStr = str_comp(pName, &aFilterStrTrimmed[1]) == 0 ? pName : nullptr;
			FilterLen -= 2;
		}
		else
		{
			const char *pFilteredStrEnd;
			pFilteredStr = str_utf8_find_nocase(pName, aFilterStrTrimmed, &pFilteredStrEnd);
			if(pFilteredStr != nullptr && pFilteredStrEnd != nullptr)
				FilterLen = pFilteredStrEnd - pFilteredStr;
		}
		if(pFilteredStr)
		{
			PrintFn(pFilteredStr, FilterLen);
			return true;
		}
	}
	return false;
}

CTeeRenderInfo CMenus::GetTeeRenderInfo(vec2 Size, const char *pSkinName, bool CustomSkinColors, int CustomSkinColorBody, int CustomSkinColorFeet) const
{
	CTeeRenderInfo TeeInfo;
	TeeInfo.Apply(GameClient()->m_Skins.Find(pSkinName));
	TeeInfo.ApplyColors(CustomSkinColors, CustomSkinColorBody, CustomSkinColorFeet);
	TeeInfo.m_Size = minimum(Size.x, Size.y);
	return TeeInfo;
}

void CMenus::ConchainFriendlistUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CMenus *pThis = ((CMenus *)pUserData);
	if(pResult->NumArguments() >= 1 && (pThis->Client()->State() == IClient::STATE_OFFLINE || pThis->Client()->State() == IClient::STATE_ONLINE))
	{
		pThis->FriendlistOnUpdate();
		pThis->Client()->ServerBrowserUpdate();
	}
}

void CMenus::ConchainFavoritesUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() >= 1 && g_Config.m_UiPage == PAGE_FAVORITES)
		((CMenus *)pUserData)->ServerBrowser()->Refresh(IServerBrowser::TYPE_FAVORITES);
}

void CMenus::ConchainCommunitiesUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CMenus *pThis = static_cast<CMenus *>(pUserData);
	if(pResult->NumArguments() >= 1 && (g_Config.m_UiPage == PAGE_INTERNET || g_Config.m_UiPage == PAGE_FAVORITES || (g_Config.m_UiPage >= PAGE_FAVORITE_COMMUNITY_1 && g_Config.m_UiPage <= PAGE_FAVORITE_COMMUNITY_5)))
	{
		pThis->UpdateCommunityCache(true);
		pThis->Client()->ServerBrowserUpdate();
	}
}

void CMenus::ConchainUiPageUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CMenus *pThis = static_cast<CMenus *>(pUserData);
	if(pResult->NumArguments() >= 1)
	{
		if(g_Config.m_UiPage >= PAGE_FAVORITE_COMMUNITY_1 && g_Config.m_UiPage <= PAGE_FAVORITE_COMMUNITY_5 &&
			(size_t)(g_Config.m_UiPage - PAGE_FAVORITE_COMMUNITY_1) >= pThis->ServerBrowser()->FavoriteCommunities().size())
		{
			// Reset page to internet when there is no favorite community for this page.
			g_Config.m_UiPage = PAGE_INTERNET;
		}

		pThis->SetMenuPage(g_Config.m_UiPage);
	}
}

void CMenus::UpdateCommunityCache(bool Force)
{
	if(g_Config.m_UiPage >= PAGE_FAVORITE_COMMUNITY_1 && g_Config.m_UiPage <= PAGE_FAVORITE_COMMUNITY_5 &&
		(size_t)(g_Config.m_UiPage - PAGE_FAVORITE_COMMUNITY_1) >= ServerBrowser()->FavoriteCommunities().size())
	{
		// Reset page to internet when there is no favorite community for this page,
		// i.e. when favorite community is removed via console while the page is open.
		// This also updates the community cache because the page is changed.
		SetMenuPage(PAGE_INTERNET);
	}
	else
	{
		ServerBrowser()->CommunityCache().Update(Force);
	}
}
