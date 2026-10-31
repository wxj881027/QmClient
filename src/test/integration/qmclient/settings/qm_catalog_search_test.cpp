#include <base/str.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmCardSearchRegistry.h>
#include <game/client/QmUi/SettingsSearchHighlight.h>
#include <game/client/QmUi/SettingsSearchReveal.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogSearch.h>
#include <game/localization.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

TEST(QmCatalogSearch, EveryDefaultCardResolvesDeclaredSearchTable)
{
	// 预览/输入框/信息页没有设置行；其余默认卡都必须能找到声明表，
	// 以兜住漏 include、漏 REGISTER 或 stableId 拼错。
	const char *s_apTablelessCards[] = {
		"deck:credits-friend-links",
		"deck:global-search-input",
		"deck:global-search-results",
		"deck:qmclient-contributors-community",
		"deck:qmclient-contributors-ddnet",
		"deck:qmclient-contributors-sponsors",
		"deck:tclient-bind-wheel-preview",
		"deck:tclient-info-developers",
		"qm:info",
		"qm:laser",
		"qm:nameplate_text",
		"tclient:visual-font-cursor",
	};
	for(const qm_card_registry::SCardDefault &Card : qm_card_registry::Defaults())
	{
		const qm_card_registry::SCardSettingSearch *pSettings = nullptr;
		size_t SettingCount = 0;
		const bool Found = qm_card_catalog::FindSearchMetadata(Card.m_pStableId, &pSettings, &SettingCount);
		const bool Tableless = std::any_of(std::begin(s_apTablelessCards), std::end(s_apTablelessCards), [&Card](const char *pStableId) {
			return str_comp(Card.m_pStableId, pStableId) == 0;
		});
		if(Tableless)
		{
			EXPECT_FALSE(Found) << Card.m_pStableId;
			continue;
		}
		EXPECT_TRUE(Found) << Card.m_pStableId;
		EXPECT_GT(SettingCount, 0u) << Card.m_pStableId;
		EXPECT_NE(pSettings, nullptr) << Card.m_pStableId;
	}
}

TEST(QmCatalogSearch, DeclaredSearchTablesRegisterBeforeSearch)
{
	EXPECT_FALSE(qm_card_search::Registrations().empty());
	const qm_card_search::SRegistration *pRegistration = qm_card_search::Find("qm:lyrics");
	ASSERT_NE(pRegistration, nullptr);
	EXPECT_GT(pRegistration->m_Count, 0u);
}

TEST(QmCatalogSearch, InternalLabelsAliasesAndConfigsResolveWithoutOpeningTheirPages)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	struct SCase
	{
		const char *m_pQuery;
		const char *m_pCard;
	};
	const SCase aCases[] = {
		{"inp_mousesens", "deck:controls-mouse"},
		{"Maximum cursor distance", "deck:controls-mouse"},
		{"inp_controller_tolerance", "deck:controls-controller"},
		{"player_name", "deck:player-identity"},
		{"gfx_fsaa_samples", "deck:graphics-display"},
		{"gfx_screen_refresh_rate", "deck:graphics-modes"},
		{"snd_volume", "deck:sound-volume"},
		{"Sound volume", "deck:sound-volume"},
		{"cl_predict_events", "deck:ddnet-gameplay"},
		{"Axiom dummy password", "qm:gores"},
		{"qm_ime_auto_manage", "qm:ime"},
		{"cl_replay_length", "deck:ddnet-demo"},
		{"预测余量", "deck:ddnet-gameplay"},
		{"cl_background_entities", "deck:ddnet-background"},
		{"cl_chat_size", "deck:appearance-chat-settings"},
		{"qm_custom_font_cjk", "tclient:font"},
		{"CJK font weight:", "tclient:font"},
		{"中日韩字体字重", "tclient:font"},
		{"qm_statusbar_height", "deck:tclient-status-bar-settings"},
		{"qm_cycle_tee_hue_dummy", "deck:tee7-editor"},
		{"地图点状调试路径", "qm:player_stats"},
		{"qm_player_stats_map_progress_dbg_route", "qm:player_stats"},
		// 四张曾缺索引/索引措辞过期的卡片：界面上的原文必须能直接命中。
		{"Lyrics language", "qm:lyrics"},
		{"Separate Dynamic Island", "qm:lyrics"},
		{"Show lyrics in Dynamic Island", "qm:lyrics"},
		{"Enable Netease music Hook", "qm:lyrics"},
		{"Infinity symbol", "qm:dynamic_island"},
		{"Hook countdown size", "qm:dynamic_island"},
		{"Console color scheme", "qm:console"},
		{"Console font size", "qm:console"},
		{"Selection background color", "qm:chat_completion"},
		{"Reset chat completion appearance", "qm:chat_completion"},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pQuery);
		const auto Results = qm_card_registry::SearchCards(Case.m_pQuery, Model);
		const auto It = std::find_if(Results.begin(), Results.end(), [&](const auto &Result) {
			return std::string(Result.m_pStableId) == Case.m_pCard;
		});
		ASSERT_NE(It, Results.end());
		EXPECT_TRUE(qm_card_catalog::HasCardModule(It->m_pStableId));
		EXPECT_STREQ(It->m_Target.m_pStableId, Case.m_pCard);
	}
}

TEST(QmCatalogSearch, ChineseQueryKeepsActualRowAndDropsLongHelp)
{
	struct CLocalizationCleanup
	{
		~CLocalizationCleanup() { g_Localization.Load("", nullptr, nullptr); }
	} Cleanup;
	g_Localization.Load("", nullptr, nullptr);
	g_Localization.AddString("Blank asset auto fallback", "空白资源自动回退", "");
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	const auto Results = qm_card_registry::SearchCards("回退", Model);
	const auto It = std::find_if(Results.begin(), Results.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "qm:mini_features";
	});
	ASSERT_NE(It, Results.end());
	EXPECT_NE(std::find(It->m_vMatchKeys.begin(), It->m_vMatchKeys.end(), "空白资源自动回退"), It->m_vMatchKeys.end());
	EXPECT_TRUE(std::none_of(It->m_vMatchKeys.begin(), It->m_vMatchKeys.end(), [](const std::string &Key) {
		return Key.find("自定义资源的贴图全透明") != std::string::npos;
	}));
}

TEST(QmCatalogSearch, PreviewHighlightsEveryResultCard)
{
	qm_settings_reveal::Clear();
	qm_settings_reveal::SetPreviews({
		{"qm:lyrics", {"Lyrics language"}},
		{"qm:mini_features", {"Blank asset auto fallback"}},
	});
	qm_settings_reveal::BeginCard("qm:mini_features", nullptr, true);
	EXPECT_TRUE(qm_settings_reveal::ShouldReveal("Blank asset auto fallback", nullptr));
	qm_settings_reveal::EndCard();
	qm_settings_reveal::BeginCard("qm:lyrics", nullptr, true);
	EXPECT_TRUE(qm_settings_reveal::ShouldReveal(nullptr, "Lyrics language"));
	qm_settings_reveal::EndCard();
	qm_settings_reveal::BeginCard("qm:other", nullptr, true);
	EXPECT_FALSE(qm_settings_reveal::ShouldReveal(nullptr, "Lyrics language"));
	qm_settings_reveal::EndCard();
	qm_settings_reveal::Clear();
}

TEST(QmCatalogSearch, HighlightStyleUsesThemeAccentAndClampsRadius)
{
	SUiTheme Theme;
	Theme.m_Accent = ColorRGBA(0.2f, 0.4f, 0.6f, 1.0f);
	CUIRect Row;
	Row.w = 200.0f;
	Row.h = 20.0f;
	qm_settings_highlight::SStyle Style = qm_settings_highlight::Resolve(Theme, Row);
	EXPECT_FLOAT_EQ(Style.m_Text.r, 0.2f);
	EXPECT_FLOAT_EQ(Style.m_Text.g, 0.4f);
	EXPECT_FLOAT_EQ(Style.m_Text.b, 0.6f);
	EXPECT_FLOAT_EQ(Style.m_Fill.a, Theme.m_Selected.a);
	EXPECT_FLOAT_EQ(Style.m_Border.r, Theme.m_Accent.r);
	EXPECT_FLOAT_EQ(Style.m_Radius, ui_token::radius::BASE);

	Row.h = 6.0f;
	Style = qm_settings_highlight::Resolve(Theme, Row);
	EXPECT_FLOAT_EQ(Style.m_Radius, 3.0f);
}

TEST(QmCatalogSearch, RevealRequestMatchesOnlyTargetRowAndExpires)
{
	qm_settings_reveal::Clear();
	qm_settings_reveal::Request("qm:lyrics", {"Lyrics language", "qm_lyrics_language"});
	ASSERT_TRUE(qm_settings_reveal::Pending());
	qm_settings_reveal::BeginCard("qm:other", nullptr, true);
	EXPECT_FALSE(qm_settings_reveal::ShouldReveal("Lyrics language", nullptr));
	qm_settings_reveal::EndCard();

	qm_settings_reveal::BeginCard("qm:lyrics", nullptr, true);
	EXPECT_TRUE(qm_settings_reveal::ShouldReveal("qm_lyrics_language", nullptr));
	EXPECT_TRUE(qm_settings_reveal::ShouldReveal(nullptr, "Lyrics language"));
	qm_settings_reveal::EndCard();
	EXPECT_FALSE(qm_settings_reveal::ShouldReveal(nullptr, "Lyrics language"));

	// 墙钟到期：不依赖目标卡是否绘制。
	qm_settings_reveal::State().m_ExpireSeconds = qm_settings_reveal::NowSeconds() - 1.0;
	EXPECT_FALSE(qm_settings_reveal::Pending());
	EXPECT_FALSE(qm_settings_reveal::ShouldReveal(nullptr, "Lyrics language"));

	qm_settings_reveal::Request("qm:lyrics", {"Lyrics language"});
	qm_settings_reveal::Clear();
	EXPECT_FALSE(qm_settings_reveal::Pending());
}

TEST(QmCatalogSearch, LongHelpEntryDoesNotBecomeRowMatch)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	const auto Results = qm_card_registry::SearchCards("fallback", Model);
	const auto It = std::find_if(Results.begin(), Results.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "qm:mini_features";
	});
	ASSERT_NE(It, Results.end());
	EXPECT_NE(std::find(It->m_vMatchKeys.begin(), It->m_vMatchKeys.end(), "Blank asset auto fallback"), It->m_vMatchKeys.end());
	EXPECT_TRUE(std::none_of(It->m_vMatchKeys.begin(), It->m_vMatchKeys.end(), [](const std::string &Key) {
		return Key.find("Automatically fall back") != std::string::npos;
	}));
}

TEST(QmCatalogSearch, MatchKeysCoverDisplayedMatches)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	const auto Results = qm_card_registry::SearchCards("lyrics", Model);
	const auto It = std::find_if(Results.begin(), Results.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "qm:lyrics";
	});
	ASSERT_NE(It, Results.end());
	ASSERT_FALSE(It->m_vMatches.empty());
	for(const std::string &Display : It->m_vMatches)
		EXPECT_NE(std::find(It->m_vMatchKeys.begin(), It->m_vMatchKeys.end(), Display), It->m_vMatchKeys.end()) << Display;
}

TEST(QmCatalogSearch, EveryQueryTokenMustMatchTheSameCard)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	const auto Multi = qm_card_registry::SearchCards("console font", Model);
	EXPECT_TRUE(std::any_of(Multi.begin(), Multi.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "qm:console";
	}));
	// 全角空格与半角空格同义。
	const auto FullWidth = qm_card_registry::SearchCards("console　font", Model);
	EXPECT_TRUE(std::any_of(FullWidth.begin(), FullWidth.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "qm:console";
	}));
	// 任一查询词没有命中时整张卡不返回。
	EXPECT_TRUE(qm_card_registry::SearchCards("console zzzznotfound", Model).empty());
}

TEST(QmCatalogSearch, TitleMatchesRankAheadOfSettingText)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	const auto Results = qm_card_registry::SearchCards("Voice", Model);
	ASSERT_FALSE(Results.empty());
	EXPECT_STREQ(Results.front().m_pStableId, "qm:voice");
}

TEST(QmCatalogSearch, ResultCarriesMatchedSettingText)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	const auto LabelResults = qm_card_registry::SearchCards("Lyrics language", Model);
	const auto LabelIt = std::find_if(LabelResults.begin(), LabelResults.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "qm:lyrics";
	});
	ASSERT_NE(LabelIt, LabelResults.end());
	ASSERT_FALSE(LabelIt->m_vMatches.empty());
	const char *pExpectedLabel = Localize("Lyrics language");
	EXPECT_NE(std::find(LabelIt->m_vMatches.begin(), LabelIt->m_vMatches.end(), pExpectedLabel), LabelIt->m_vMatches.end());
	EXPECT_NE(std::find(LabelIt->m_vMatchKeys.begin(), LabelIt->m_vMatchKeys.end(), std::string("Lyrics language")), LabelIt->m_vMatchKeys.end());
	EXPECT_NE(std::find(LabelIt->m_vMatchKeys.begin(), LabelIt->m_vMatchKeys.end(), std::string(pExpectedLabel)), LabelIt->m_vMatchKeys.end());

	// 预计算小写预筛不能改变大小写不敏感的匹配。
	const auto UpperResults = qm_card_registry::SearchCards("LYRICS LANGUAGE", Model);
	EXPECT_NE(std::find_if(UpperResults.begin(), UpperResults.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "qm:lyrics";
	}),
		UpperResults.end());

	const auto ConfigResults = qm_card_registry::SearchCards("qm_lyrics_language", Model);
	const auto ConfigIt = std::find_if(ConfigResults.begin(), ConfigResults.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "qm:lyrics";
	});
	ASSERT_NE(ConfigIt, ConfigResults.end());
	ASSERT_FALSE(ConfigIt->m_vMatches.empty());
	EXPECT_NE(std::find(ConfigIt->m_vMatches.begin(), ConfigIt->m_vMatches.end(), "qm_lyrics_language"), ConfigIt->m_vMatches.end());
	EXPECT_NE(std::find(ConfigIt->m_vMatchKeys.begin(), ConfigIt->m_vMatchKeys.end(), std::string("qm_lyrics_language")), ConfigIt->m_vMatchKeys.end());
	EXPECT_LE(ConfigIt->m_vMatches.size(), 3u);
}

TEST(QmCatalogSearch, RevealKeysMatchLabelOrLocalizedText)
{
	const std::vector<std::string> vKeys = {"Lyrics language", "qm_lyrics_language", "歌词语言"};
	EXPECT_TRUE(qm_settings_reveal::KeyMatches(vKeys, "qm_lyrics_language", nullptr));
	EXPECT_TRUE(qm_settings_reveal::KeyMatches(vKeys, nullptr, "歌词语言"));
	EXPECT_FALSE(qm_settings_reveal::KeyMatches(vKeys, "other-config", "其他文案"));
	EXPECT_FALSE(qm_settings_reveal::KeyMatches({}, "qm_lyrics_language", nullptr));
}

TEST(QmCatalogSearch, AliasesAndCardLabelsProduceOneEditableResult)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	const auto Results = qm_card_registry::SearchCards("激光", Model);
	EXPECT_EQ(std::count_if(Results.begin(), Results.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "deck:appearance-laser-enhanced";
	}),
		1);
	EXPECT_TRUE(std::none_of(Results.begin(), Results.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "qm:laser";
	}));
}

TEST(QmCatalogSearch, AliasNavigationUsesTheSavedLocationOfItsEditableCard)
{
	const auto Defaults = qm_card_registry::BuildDefaultEntries();
	qm_card_order::CModel Model;
	Model.SetEntries(Defaults);
	Model.MoveToTab("deck:appearance-laser-enhanced", "function", 2, 0);
	char aSaved[32768];
	ASSERT_TRUE(Model.Serialize(aSaved, sizeof(aSaved)));
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSaved, Defaults));
	const auto *pAlias = qm_card_registry::FindByStableId("qm:laser");
	ASSERT_NE(pAlias, nullptr);
	const auto Target = qm_card_registry::ResolveCardNavigationTarget(*pAlias, Reloaded);
	EXPECT_STREQ(Target.m_pTab, "function");
	EXPECT_STREQ(Target.m_pStableId, "deck:appearance-laser-enhanced");
	const auto Results = qm_card_registry::SearchCards("激光", Reloaded);
	const auto It = std::find_if(Results.begin(), Results.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "deck:appearance-laser-enhanced";
	});
	ASSERT_NE(It, Results.end());
	EXPECT_STREQ(It->m_Target.m_pTab, "function");
}

TEST(QmCatalogSearch, LyricsUseTheirOwnControlsAndNavigationTarget)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	const auto Results = qm_card_registry::SearchCards("歌词", Model);
	const auto It = std::find_if(Results.begin(), Results.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "qm:lyrics";
	});
	ASSERT_NE(It, Results.end());
	EXPECT_TRUE(qm_card_catalog::HasCardModule(It->m_pStableId));
	EXPECT_STREQ(It->m_Target.m_pStableId, "qm:lyrics");
}
