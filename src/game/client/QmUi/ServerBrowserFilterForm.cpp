/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "SecondaryPanel.h"
#include "ServerBrowserFilterLayout.h"
#include "SettingsPageLayout.h"
#include "UiForms.h"

#include <engine/client.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/countryflags.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/browser_status_layout.h>
#include <game/client/components/qmclient/map_vote_difficulty.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>

void CMenus::RenderServerbrowserFilterForm(CUIRect &View)
{
	SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(Ui()->Screen()->w);
	// 字号、行高、组间距与胶囊度量统一来自 ui_token::server_filter，
	// 面板里不再出现 18 / 14 / 3 / 6 / 24 / 26 这类字面量。
	Metrics.m_LineHeight = ui_token::server_filter::LINE_HEIGHT;
	Metrics.m_BodySize = ui_token::server_filter::BODY_FONT_SIZE * CUi::ms_FontmodHeight;
	Metrics.m_LineSpacing = ui_token::server_filter::LINE_SPACING;
	Metrics.m_InputHeight = Metrics.m_LineHeight;
	Metrics.m_SectionGap = ui_token::server_filter::SECTION_GAP;
	const auto Entry = [](int *pValue, const char *pTextId) {
		SSettingsToggleEntry Result{pValue, pTextId, Localize(pTextId)};
		Result.m_ShowCheckmark = true;
		return Result;
	};
	const auto LabelHeight = [&](const char *pText, float Size) {
		return std::max(Size, TextRender()->TextBoundingBox(Size, pText, -1, std::max(1.0f, View.w)).m_H);
	};
	// 标签只负责分组，不再和内容抢注意力：标题用主文本色 + 略大字号，小节标题降一档用次文本色。
	const auto DrawLabel = [&](const char *pText, float Size, const ColorRGBA &Color) {
		CUIRect Label;
		View.HSplitTop(LabelHeight(pText, Size), &Label, &View);
		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_EnableWidthCheck = false;
		Props.m_MinimumFontSize = std::min(Size, Metrics.m_BodySize);
		Props.SetColor(Color);
		Ui()->DoLabel(&Label, pText, Size, TEXTALIGN_ML, Props);
	};
	// 组间距必须明显大于组内间距（内容行 4px），否则整页读起来是同一层信息。
	// 分割线两端渐隐、不拉满宽：贴在标题下面就成下划线，所以线本身要离开标题一行。
	const auto DrawFadeDivider = [&]() {
		CUIRect Line;
		View.HSplitTop(ui_token::server_filter::DIVIDER_HEIGHT, &Line, &View);
		const float Inset = std::min(Line.w * ui_token::server_filter::DIVIDER_INSET_RATIO, ui_token::server_filter::DIVIDER_INSET_MAX);
		CUIRect Bar{Line.x + Inset, Line.y + 0.5f, std::max(0.0f, Line.w - 2.0f * Inset), ui_token::server_filter::DIVIDER_THICKNESS};
		const ColorRGBA Peak = ui_token::color::TEXT_SECONDARY.WithAlpha(0.18f);
		const ColorRGBA Edge = Peak.WithAlpha(0.0f);
		const float Half = Bar.w * 0.5f;
		Graphics()->DrawRect4(Bar.x, Bar.y, Half, Bar.h, Edge, Peak, Edge, Peak, IGraphics::CORNER_NONE, 0.0f);
		Graphics()->DrawRect4(Bar.x + Half, Bar.y, Bar.w - Half, Bar.h, Peak, Edge, Peak, Edge, IGraphics::CORNER_NONE, 0.0f);
	};
	const auto DrawSection = [&](const char *pText) {
		View.HSplitTop(ui_token::spacing::MD, nullptr, &View);
		DrawLabel(pText, Metrics.m_BodySize * ui_token::server_filter::SECTION_TITLE_SCALE, ui_token::color::TEXT_PRIMARY);
		View.HSplitTop(ui_token::spacing::XS, nullptr, &View);
		DrawFadeDivider();
		View.HSplitTop(ui_token::spacing::XS, nullptr, &View);
	};
	// 小节标题用于模式里的 DDmaX （比组间距小、比行间距大），让「子类」读起来属于上一组。
	const auto DrawSubsection = [&](const char *pText) {
		View.HSplitTop(ui_token::spacing::SM, nullptr, &View);
		DrawLabel(pText, Metrics.m_BodySize, ui_token::color::TEXT_SECONDARY);
		View.HSplitTop(ui_token::spacing::XS, nullptr, &View);
	};

	DrawSection(Localize("Common filters"));
	DoSettingsToggleGroup(-1, -1, -1, View, {
							Entry(&g_Config.m_BrFilterEmpty, Localizable("Has people playing")),
							Entry(&g_Config.m_BrFilterFull, Localizable("Server not full")),
							Entry(&g_Config.m_BrFilterFriends, Localizable("Show friends only")),
							Entry(&g_Config.m_BrFilterPw, Localizable("No password")),
							Entry(&g_Config.m_BrFilterLogin, Localizable("No login required")),
						},
		Metrics);

	// 按地址筛选列表。注意与底部「服务器地址」输入框区分：那个是要连接的地址，这个只过滤列表，
	// 不复用 m_UiServerAddress；它是引擎里 m_BrFilterServerAddress 的唯一入口。
	{
		View.HSplitTop(ui_token::spacing::XS, nullptr, &View);
		CUIRect Input;
		View.HSplitTop(Metrics.m_InputHeight, &Input, &View);
		static CLineInput s_ServerAddressFilterInput(g_Config.m_BrFilterServerAddress, sizeof(g_Config.m_BrFilterServerAddress));
		ui_widget::SInputFieldOptions AddressFilterOptions;
		AddressFilterOptions.m_FontSize = Metrics.m_BodySize;
		AddressFilterOptions.m_Clearable = true;
		AddressFilterOptions.m_pPlaceholder = Localize("Server address:");
		if(ui_widget::InputField(SettingsUiContext("server_browser_filter_address"), &s_ServerAddressFilterInput, Input, AddressFilterOptions).m_Changed)
			Client()->ServerBrowserUpdate();
	}

	DrawSection(Localize("Player count"));
	DoSettingsToggleGroup(-1, -1, -1, View, {
							Entry(&g_Config.m_BrFilterSpectators, Localizable("Count players only")),
							Entry(&g_Config.m_BrFilterConnectingPlayers, Localizable("Filter connecting players")),
						},
		Metrics);

	if(ServerBrowser()->CommunityCache().AnyRanksAvailable())
	{
		DrawSection(Localize("Map progress"));
		const bool WasIndicatingFinished = g_Config.m_BrIndicateFinished != 0;
		const SSettingsToggleEntry Finished = Entry(&g_Config.m_BrIndicateFinished, Localizable("Indicate map finish"));
		if(WasIndicatingFinished)
			DoSettingsToggleGroup(-1, -1, -1, View, {Finished, Entry(&g_Config.m_BrFilterUnfinishedMap, Localizable("Unfinished map"))}, Metrics);
		else
			DoSettingsToggleGroup(-1, -1, -1, View, {Finished}, Metrics);
		if(!g_Config.m_BrIndicateFinished)
			g_Config.m_BrFilterUnfinishedMap = 0;
		else if(!WasIndicatingFinished)
			ServerBrowser()->Refresh(ServerBrowser()->GetCurrentType());
	}

	// 星级与类别写入同一份浏览器筛选，与底部快捷条共用状态：任一入口改动另一处立即同步。
	DrawSection(Localize("Difficulty"));
	{
		static int s_aStarChipIds[6];
		const IUiContext ChipContext = SettingsUiContext("server_browser_filter_stars");
		const SQmBrowserChipGrid Chips = QmBrowserChipGrid(View, 6, ui_token::server_filter::CHIP_STAR_MIN_WIDTH, ui_token::server_filter::CHIP_HEIGHT, ui_token::server_filter::CHIP_GAP);
		int StarMask = g_Config.m_QmMapBrowserStarMask;
		bool StarMaskChanged = false;
		if(ui_widget::FilterChip(ChipContext, &s_aStarChipIds[0], Chips.m_aRects[0], Localize("All"), StarMask == 0))
		{
			StarMask = 0;
			StarMaskChanged = true;
		}
		for(int Star = 1; Star <= 5; ++Star)
		{
			char aLabel[16];
			str_format(aLabel, sizeof(aLabel), "%d★", Star);
			if(ui_widget::FilterChip(ChipContext, &s_aStarChipIds[Star], Chips.m_aRects[Star], aLabel, QmMapVotes::MapBrowserStarSelected(StarMask, Star)))
			{
				StarMask = QmMapVotes::ToggleMapBrowserStar(StarMask, Star);
				StarMaskChanged = true;
			}
		}
		if(StarMaskChanged)
		{
			g_Config.m_QmMapBrowserStarMask = StarMask;
			Client()->ServerBrowserUpdate();
		}
		View.HSplitTop(Chips.m_ContentHeight, nullptr, &View);
	}

	DrawSection(Localize("Types"));
	{
		// 常用类别为普通模式，标签走翻译；DDmaX 族单独成小节，见下方。
		static const char *const s_apCategoryKeys[] = {"Novice", "Moderate", "Brutal", "Insane", "Dummy", "Solo", "Oldschool", "Event", "Fun", "Race"};
		// DDmaX 的四个子系列名是专有名，不参与翻译：「Next」这类键会命中别的界面文案（下一个）。
		static const char *const s_apDdmxKeys[] = {"DDmaX Easy", "DDmaX Next", "DDmaX Pro", "DDmaX Nut"};
		static const char *const s_apDdmxLabels[] = {"Easy", "Next", "Pro", "Nut"};
		constexpr int NumCategories = (int)(sizeof(s_apCategoryKeys) / sizeof(s_apCategoryKeys[0]));
		constexpr int NumDdmx = (int)(sizeof(s_apDdmxKeys) / sizeof(s_apDdmxKeys[0]));
		static int s_aCategoryChipIds[NumCategories + NumDdmx];
		const IUiContext ChipContext = SettingsUiContext("server_browser_filter_categories");
		int CategoryMask = g_Config.m_QmMapBrowserCategoryMask;
		bool CategoryMaskChanged = false;
		const auto DrawChipGroup = [&](const char *const *ppKeys, const char *const *ppLabels, int Count, int IdOffset, bool Translate) {
			const SQmBrowserChipGrid Chips = QmBrowserChipGrid(View, Count, ui_token::server_filter::CHIP_LABEL_MIN_WIDTH, ui_token::server_filter::CHIP_HEIGHT, ui_token::server_filter::CHIP_GAP);
			for(int Index = 0; Index < Chips.m_Count; ++Index)
			{
				const int Bit = QmMapVotes::MapBrowserCategoryBit(ppKeys[Index]);
				if(ui_widget::FilterChip(ChipContext, &s_aCategoryChipIds[IdOffset + Index], Chips.m_aRects[Index], Translate ? Localize(ppLabels[Index]) : ppLabels[Index], QmMapVotes::MapBrowserCategorySelected(CategoryMask, Bit)))
				{
					CategoryMask = QmMapVotes::ToggleMapBrowserCategory(CategoryMask, Bit);
					CategoryMaskChanged = true;
				}
			}
			View.HSplitTop(Chips.m_ContentHeight, nullptr, &View);
		};
		DrawChipGroup(s_apCategoryKeys, s_apCategoryKeys, NumCategories, 0, true);
		// DDmaX 子系列：标题即族名，四个子类只留短名，避免长标签被省略成同一个前缀。
		char aDdmxTitle[64];
		str_format(aDdmxTitle, sizeof(aDdmxTitle), "%s · %s", Localize("DDmaX"), Localize("Classic"));
		DrawSubsection(aDdmxTitle);
		DrawChipGroup(s_apDdmxKeys, s_apDdmxLabels, NumDdmx, NumCategories, false);
		if(CategoryMaskChanged)
		{
			g_Config.m_QmMapBrowserCategoryMask = CategoryMask;
			Client()->ServerBrowserUpdate();
		}
	}
}
