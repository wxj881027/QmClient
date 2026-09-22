#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>
#include <engine/client/plausible_sizes.h>
#include <engine/client/rounded_rect_geometry.h>
#include <engine/storage.h>

#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/camera.h>
#include <game/client/components/controls.h>
#include <game/client/components/menus.h>
#include <game/client/components/nameplate_text_effects.h>
#include <game/client/components/nameplates.h>
#include <game/client/components/qmclient/axiom_auto_login.h>
#include <game/client/components/qmclient/demo_cut.h>
#include <game/client/components/qmclient/demo_display.h>
#include <game/client/components/qmclient/demo_ui.h>
#include <game/client/components/qmclient/friend_heart_icon.h>
#include <game/client/components/qmclient/friends_category_drag.h>
#include <game/client/components/qmclient/nameplate_layout.h>
#include <game/client/components/qmclient/spectator_friend_priority.h>
#include <game/client/components/qmclient/spectator_tele_search.h>
#include <game/client/components/tclient/statusbar.h>
#include <game/client/components/tooltips.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

TEST(QmSpectatorTeleSearch, NumberInputAndSteppingStayInMapRange)
{
	EXPECT_EQ(qm_spectator_tele::ParseNumber("1"), 1);
	EXPECT_EQ(qm_spectator_tele::ParseNumber("042"), 42);
	EXPECT_EQ(qm_spectator_tele::ParseNumber("255"), 255);
	for(const char *pInvalid : {"", "0", "256", "-1", "1a", "1.5", " 1", "999999999999"})
		EXPECT_EQ(qm_spectator_tele::ParseNumber(pInvalid), 0) << pInvalid;
	EXPECT_EQ(qm_spectator_tele::StepNumber(1, -1), 255);
	EXPECT_EQ(qm_spectator_tele::StepNumber(255, 1), 1);
	EXPECT_EQ(qm_spectator_tele::StepNumber(42, -1), 41);
	EXPECT_EQ(qm_spectator_tele::StepNumber(42, 1), 43);
	EXPECT_EQ(qm_spectator_tele::StepNumber(0, 1), 1);
	EXPECT_EQ(qm_spectator_tele::StepNumber(0, -1), 255);
}

TEST(QmSpectatorTeleSearch, ReadsTopRowAndKeypadDigitsWhileHoldingSpectatorKey)
{
	EXPECT_EQ(qm_spectator_tele::DigitFromKey(KEY_0), 0);
	EXPECT_EQ(qm_spectator_tele::DigitFromKey(KEY_KP_0), 0);
	for(int Digit = 1; Digit <= 9; ++Digit)
	{
		EXPECT_EQ(qm_spectator_tele::DigitFromKey(KEY_1 + Digit - 1), Digit);
		EXPECT_EQ(qm_spectator_tele::DigitFromKey(KEY_KP_1 + Digit - 1), Digit);
	}
	EXPECT_EQ(qm_spectator_tele::DigitFromKey(KEY_RSHIFT), -1);
	EXPECT_EQ(qm_spectator_tele::DigitFromKey(KEY_A), -1);
}

TEST(QmSpectatorTeleSearch, MatchesEditorNumberedTeleTypes)
{
	for(const unsigned char Type : {TILE_TELEIN, TILE_TELEINEVIL, TILE_TELEINWEAPON, TILE_TELEINHOOK, TILE_TELEOUT, TILE_TELECHECK, TILE_TELECHECKOUT})
	{
		CTeleTile aTiles[1] = {};
		aTiles[0].m_Number = 7;
		aTiles[0].m_Type = Type;
		EXPECT_EQ(qm_spectator_tele::FindNext(aTiles, 1, 1, 7, -1), 0) << static_cast<int>(Type);
	}
	for(const unsigned char Type : {TILE_AIR, TILE_SOLID, TILE_TELECHECKIN, TILE_TELECHECKINEVIL})
	{
		CTeleTile aTiles[1] = {};
		aTiles[0].m_Number = 7;
		aTiles[0].m_Type = Type;
		EXPECT_EQ(qm_spectator_tele::FindNext(aTiles, 1, 1, 7, -1), -1) << static_cast<int>(Type);
	}
}

TEST(QmSpectatorTeleSearch, CyclesDistinctLocationsInMapOrder)
{
	CTeleTile aTiles[64] = {};
	for(const int Index : {0, 1, 9, 10, 30})
	{
		aTiles[Index].m_Number = 7;
		aTiles[Index].m_Type = TILE_TELECHECK;
	}
	aTiles[10].m_Type = TILE_TELEOUT;
	aTiles[30].m_Type = TILE_TELECHECKOUT;
	EXPECT_EQ(qm_spectator_tele::FindNext(aTiles, 64, 1, 7, -1), 0);
	EXPECT_EQ(qm_spectator_tele::FindNext(aTiles, 64, 1, 7, 0), 10);
	EXPECT_EQ(qm_spectator_tele::FindNext(aTiles, 64, 1, 7, 10), 30);
	EXPECT_EQ(qm_spectator_tele::FindNext(aTiles, 64, 1, 7, 30), 0);
	// 改号时从头查找；不同编号不会沿用上一个编号的位置。
	aTiles[20].m_Number = 255;
	aTiles[20].m_Type = TILE_TELECHECKOUT;
	EXPECT_EQ(qm_spectator_tele::FindNext(aTiles, 64, 1, 255, -1), 20);
	EXPECT_EQ(qm_spectator_tele::FindNext(aTiles, 64, 1, 255, 20), 20);
}

TEST(QmSpectatorTeleSearch, UsesTwoDimensionalDistanceAndHandlesMissingPoints)
{
	CTeleTile aTiles[144] = {};
	for(const int Index : {0, 12, 120})
	{
		aTiles[Index].m_Number = 1;
		aTiles[Index].m_Type = TILE_TELECHECK;
	}
	EXPECT_EQ(qm_spectator_tele::FindNext(aTiles, 12, 12, 1, 0), 120);
	EXPECT_EQ(qm_spectator_tele::FindNext(aTiles, 12, 12, 1, 120), 0);
	EXPECT_EQ(qm_spectator_tele::FindNext(aTiles, 12, 12, 2, -1), -1);
	EXPECT_EQ(qm_spectator_tele::FindNext(aTiles, 12, 12, 0, -1), -1);
	EXPECT_EQ(qm_spectator_tele::FindNext(aTiles, 12, 12, 256, -1), -1);
	EXPECT_EQ(qm_spectator_tele::FindNext(nullptr, 12, 12, 1, -1), -1);
	EXPECT_EQ(qm_spectator_tele::FindNext(aTiles, 0, 12, 1, -1), -1);
}

TEST(PlausibleSizes, RefreshRateAndWindowGuardsMatchContract)
{
	// Refresh rate: 0..1000 inclusive is the persisted-range contract.
	EXPECT_TRUE(IsPlausibleRefreshRate(0));
	EXPECT_TRUE(IsPlausibleRefreshRate(60));
	EXPECT_TRUE(IsPlausibleRefreshRate(1000));
	EXPECT_FALSE(IsPlausibleRefreshRate(-1));
	EXPECT_FALSE(IsPlausibleRefreshRate(1001));
	// Window size: 320..16384 on both axes.
	EXPECT_TRUE(IsPlausibleWindowSize(320, 240));
	EXPECT_TRUE(IsPlausibleWindowSize(16384, 16384));
	EXPECT_FALSE(IsPlausibleWindowSize(319, 240)); // under min width
	EXPECT_FALSE(IsPlausibleWindowSize(320, 239)); // under min height
	EXPECT_FALSE(IsPlausibleWindowSize(16385, 1080)); // over max width
	EXPECT_FALSE(IsPlausibleWindowSize(1920, 16385)); // over max height
}

TEST(TClientStatusBarScore, FormatsPlayerPointsStates)
{
	char aBuf[32];

	EXPECT_TRUE(tclient_statusbar::FormatPlayerPoints(aBuf, sizeof(aBuf), EPointsStatus::READY, 12345));
	EXPECT_STREQ(aBuf, "12345");
	EXPECT_TRUE(tclient_statusbar::FormatPlayerPoints(aBuf, sizeof(aBuf), EPointsStatus::NOT_REQUESTED, 0));
	EXPECT_STREQ(aBuf, "...");
	EXPECT_TRUE(tclient_statusbar::FormatPlayerPoints(aBuf, sizeof(aBuf), EPointsStatus::FETCHING, 0));
	EXPECT_STREQ(aBuf, "...");
	EXPECT_TRUE(tclient_statusbar::FormatPlayerPoints(aBuf, sizeof(aBuf), EPointsStatus::FAILED, 0));
	EXPECT_STREQ(aBuf, "?");
	EXPECT_FALSE(tclient_statusbar::FormatPlayerPoints(aBuf, 0, EPointsStatus::READY, 0));
}

TEST(QmNewUiMenuBranches, CallvoteFiltersUseSharedBoundedSemantics)
{
	EXPECT_TRUE(QmTextMatchesIncludeExcludeFilter("Deep Freeze", "deep", ""));
	EXPECT_TRUE(QmTextMatchesIncludeExcludeFilter("Deep Freeze", "", "race"));
	EXPECT_FALSE(QmTextMatchesIncludeExcludeFilter("Deep Freeze", "race", ""));
	EXPECT_FALSE(QmTextMatchesIncludeExcludeFilter("Deep Freeze", "deep", "FREEZE"));
	EXPECT_FALSE(QmTextMatchesIncludeExcludeFilter(nullptr, "", ""));
}

TEST(QmCollisionHitbox, SemanticTogglesAndFreezeLaserVolumeAreIndependent)
{
	const std::string Config = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Menu = ReadTestSourceFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Hitbox = ReadTestSourceFile("src/game/client/components/qmclient/collision_hitbox.cpp");
	const std::string Client = ReadTestSourceFile("src/engine/client/client.cpp");

	for(const char *pConfigName : {
		    "QmHitboxShowMap",
		    "QmHitboxShowTeeCollision",
		    "QmHitboxShowTeeFreeze",
		    "QmHitboxShowTeeDeath",
		    "QmHitboxShowPickups",
		    "QmHitboxShowHammer",
		    "QmHitboxShowProjectiles",
		    "QmHitboxShowLasers",
		    "QmHitboxShowFreezeLasers",
		    "QmHitboxShowHook"})
	{
		EXPECT_NE(Config.find(std::string("MACRO_CONFIG_INT(") + pConfigName), std::string::npos) << pConfigName;
	}

	for(const char *pState : {
		    "g_Config.m_QmHitboxShowMap",
		    "g_Config.m_QmHitboxShowTeeCollision",
		    "g_Config.m_QmHitboxShowTeeFreeze",
		    "g_Config.m_QmHitboxShowTeeDeath",
		    "g_Config.m_QmHitboxShowPickups",
		    "g_Config.m_QmHitboxShowHammer",
		    "g_Config.m_QmHitboxShowProjectiles",
		    "g_Config.m_QmHitboxShowLasers",
		    "g_Config.m_QmHitboxShowFreezeLasers",
		    "g_Config.m_QmHitboxShowHook"})
	{
		EXPECT_NE(Menu.find(pState), std::string::npos) << pState;
		EXPECT_NE(Hitbox.find(pState), std::string::npos) << pState;
	}

	EXPECT_NE(Hitbox.find("LASERTYPE_FREEZE"), std::string::npos);
	EXPECT_NE(Hitbox.find("LASERGUNTYPE_FREEZE"), std::string::npos);
	EXPECT_NE(Hitbox.find("LASERGUNTYPE_EXPFREEZE"), std::string::npos);
	EXPECT_NE(Hitbox.find("BuildHitboxCapsuleOutline"), std::string::npos);
	EXPECT_NE(Hitbox.find("CCharacterCore::PhysicalSize()"), std::string::npos);
	EXPECT_NE(Client.find("if(g_Config.m_ClConfigVersion < 4)"), std::string::npos);
	EXPECT_NE(Client.find("g_Config.m_QmHitboxShowTeeCollision = g_Config.m_QmHitboxShowTees"), std::string::npos);
	EXPECT_NE(Client.find("g_Config.m_QmHitboxShowFreezeLasers = g_Config.m_QmHitboxShowWeapons"), std::string::npos);
	EXPECT_NE(Hitbox.find("RenderHammerHitboxes();"), std::string::npos);
	EXPECT_NE(Hitbox.find("RenderProjectileHitboxes();"), std::string::npos);
	EXPECT_NE(Hitbox.find("RenderLaserHitboxes();"), std::string::npos);
	EXPECT_NE(Hitbox.find("RenderHookHitboxes();"), std::string::npos);
}

namespace
{

	std::string ReadTextFile(const char *pPath)
	{
		std::string Content = ReadTestSourceFile(pPath);
		// menus_settings.cpp ships with CRLF line terminators; normalize so the
		// multi-line BlockBodyAfter anchors below match regardless of source EOL.
		Content.erase(std::remove(Content.begin(), Content.end(), '\r'), Content.end());
		return Content;
	}

	std::string FunctionBody(const std::string &Source, const std::string &Signature)
	{
		const size_t FunctionStart = Source.find(Signature);
		EXPECT_NE(FunctionStart, std::string::npos) << Signature;
		const size_t BodyStart = Source.find("{", FunctionStart);
		EXPECT_NE(BodyStart, std::string::npos) << Signature;
		int Depth = 0;
		for(size_t Index = BodyStart; Index < Source.size(); ++Index)
		{
			if(Source[Index] == '{')
				++Depth;
			else if(Source[Index] == '}')
			{
				--Depth;
				if(Depth == 0)
					return Source.substr(BodyStart, Index - BodyStart);
			}
		}
		ADD_FAILURE() << Signature;
		return {};
	}

	std::string BlockBodyAfter(const std::string &Source, const std::string &Anchor)
	{
		const size_t AnchorPos = Source.find(Anchor);
		EXPECT_NE(AnchorPos, std::string::npos) << Anchor;
		const size_t BodyStart = Source.find("{", AnchorPos);
		EXPECT_NE(BodyStart, std::string::npos) << Anchor;
		int Depth = 0;
		for(size_t Index = BodyStart; Index < Source.size(); ++Index)
		{
			if(Source[Index] == '{')
				++Depth;
			else if(Source[Index] == '}')
			{
				--Depth;
				if(Depth == 0)
					return Source.substr(BodyStart, Index - BodyStart);
			}
		}
		ADD_FAILURE() << Anchor;
		return {};
	}

	size_t MatchingBrace(const std::string &Source, size_t BodyStart)
	{
		int Depth = 0;
		for(size_t Index = BodyStart; Index < Source.size(); ++Index)
		{
			if(Source[Index] == '{')
				++Depth;
			else if(Source[Index] == '}')
			{
				--Depth;
				if(Depth == 0)
					return Index;
			}
		}
		return std::string::npos;
	}

	size_t CountRoundedRectDirectCalls(const std::string &Source)
	{
		const std::regex CallRegex("Graphics\\(\\)->DrawRect(Ext|Ext4|4)?\\([^;]{0,260}IGraphics::CORNER_(ALL|TL|TR|BL|BR|L|R|T|B)");
		size_t Count = 0;
		for(std::sregex_iterator It(Source.begin(), Source.end(), CallRegex), End; It != End; ++It)
			++Count;
		return Count;
	}

	size_t CountOccurrences(const std::string &Text, const char *pNeedle)
	{
		size_t Count = 0;
		for(size_t Position = Text.find(pNeedle); Position != std::string::npos; Position = Text.find(pNeedle, Position + 1))
			++Count;
		return Count;
	}

} // namespace

TEST(QmTooltips, OwnsCallerTextAndBoundsFriendNotes)
{
	char aCallerText[] = "rabbit";
	CTooltip Tooltip{nullptr, CUIRect{}, aCallerText, -1.0f, false};
	aCallerText[0] = 'R';
	EXPECT_EQ(Tooltip.m_Text, "rabbit");

	const std::string BrowserSource = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string FriendsRender = FunctionBody(BrowserSource, "void CMenus::RenderServerbrowserFriends(CUIRect View)");
	EXPECT_NE(FriendsRender.find("DoToolTip(pListItemId, &Rect, TooltipText.c_str(), 320.0f);"), std::string::npos);
	EXPECT_NE(FriendsRender.find("DoToolTip(pSkinTooltipId, &Skin, Friend.Skin());"), std::string::npos);
}

TEST(TClientStatusBarScore, RegistersUniqueScoreSchemeCode)
{
	const std::string Header = ReadTextFile("src/game/client/components/tclient/statusbar.h");
	const std::string Source = ReadTextFile("src/game/client/components/tclient/statusbar.cpp");
	const std::string ApplyScheme = FunctionBody(Source, "void CStatusBar::ApplyStatusBarScheme(const char *pScheme)");
	const std::string UpdateScheme = FunctionBody(Source, "void CStatusBar::UpdateStatusBarScheme(char *pScheme)");
	const std::string ScoreRegistration = "\"s\", \"Points\", \"Points\", \"Displays the DDNet Points of the current player\"";

	const size_t RegistrationPos = Header.find(ScoreRegistration);
	ASSERT_NE(RegistrationPos, std::string::npos);
	EXPECT_EQ(Header.find(ScoreRegistration, RegistrationPos + 1), std::string::npos);
	EXPECT_NE(Header.find("m_Zoom, m_Score, m_Downstream"), std::string::npos);
	EXPECT_NE(ApplyScheme.find("for(char ItemLetter : ItemType.m_aLetters)"), std::string::npos);
	EXPECT_NE(ApplyScheme.find("m_StatusBarItems.push_back(&ItemType);"), std::string::npos);
	EXPECT_NE(UpdateScheme.find("pScheme[Index++] = pItem->m_aLetters[0];"), std::string::npos);
}

TEST(TClientStatusBarNetworkMetrics, PreservesSnapshotLatencyAndSeparatesRoundTripTime)
{
	const std::string Header = ReadTextFile("src/game/client/components/tclient/statusbar.h");
	const std::string Source = ReadTextFile("src/game/client/components/tclient/statusbar.cpp");
	const std::string SnapshotLatencyRender = FunctionBody(Source, "void CStatusBar::DownstreamRender()");
	const std::string RoundTripTimeRender = FunctionBody(Source, "void CStatusBar::RttRender()");

	EXPECT_NE(Header.find("\"u\", \"Snapshot Latency\", \"Latency\", \"Displays server snapshot latency\""), std::string::npos);
	EXPECT_NE(Header.find("\"t\", \"Round-trip time\", \"RTT\", \"Displays the game connection round-trip time\""), std::string::npos);
	EXPECT_NE(Header.find("m_Score, m_Downstream, m_Rtt, m_Upstream"), std::string::npos);
	EXPECT_NE(SnapshotLatencyRender.find("Client()->GetServerInfo(&CurrentServerInfo)"), std::string::npos);
	EXPECT_NE(SnapshotLatencyRender.find("CurrentServerInfo.m_Latency"), std::string::npos);
	EXPECT_NE(RoundTripTimeRender.find("m_PingMs"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsShellAndOuterScrollbarUseStableContracts)
{
	const std::string ShellSource = ReadTextFile("src/game/client/QmUi/SettingsPageLayout.h");
	const std::string TokenSource = ReadTextFile("src/game/client/QmUi/UiTokens.h");
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string ScrollSource = ReadTextFile("src/game/client/QmUi/QmScroll.cpp");
	const std::string AssetsSource = ReadTextFile("src/game/client/components/menus_settings_assets.cpp");
	EXPECT_NE(TokenSource.find("MAX_CONTENT_WIDTH = 1000.0f"), std::string::npos);
	EXPECT_NE(ShellSource.find("ResolveSettingsShellLayout"), std::string::npos);
	EXPECT_NE(ShellSource.find("Frame.m_ShellRect.VSplitRight"), std::string::npos);
	EXPECT_NE(ShellSource.find("Frame.m_ContentRect.Margin(PanelMargin"), std::string::npos);
	EXPECT_NE(SettingsSource.find("ResolveSettingsShellLayout(MainView, NeedRestart ? 30.0f : 0.0f)"), std::string::npos);
	EXPECT_NE(ScrollSource.find("SETTINGS_OUTER"), std::string::npos);
	EXPECT_NE(ScrollSource.find("QmScrollContainerStyleForSize(EQmScrollSize::MEDIUM, 1.0f)"), std::string::npos);
	EXPECT_NE(AssetsSource.find("s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_OUTER);"), std::string::npos);
	EXPECT_NE(AssetsSource.find("s_WorkshopAssetsListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_OUTER);"), std::string::npos);
	EXPECT_NE(AssetsSource.find("StableCustomList.w / (Margin + TextureWidth)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, P6QmClientContributorsUsesCanonicalDeck)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsQmClientContributors(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(Body.find("SSettingsCardDefinition"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(Body.find("deck:qmclient-contributors-community"), std::string::npos);
	EXPECT_NE(Body.find("deck:qmclient-contributors-sponsors"), std::string::npos);
	EXPECT_NE(Body.find("QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(Body.find("if(!ReadOnly)\n\t\t\t\t{"), std::string::npos);
	EXPECT_NE(Body.find("ResolveSettingsRowsHeight((int)BuildSponsorLines(ContentWidth).get().size(), LineHeight, LineSpacing)"), std::string::npos);
	EXPECT_NE(Body.find("s_CachedTextGeneration == m_MenuTextPoolGeneration"), std::string::npos);
	EXPECT_NE(Body.find("return std::cref(Lines);"), std::string::npos);
	EXPECT_EQ(Body.find("LineHeight * 0.96f"), std::string::npos);
	EXPECT_EQ(Body.find("BeginSettingsQmScrollContainer("), std::string::npos);
	EXPECT_EQ(Body.find("RenderQmSettingsGlassCard("), std::string::npos);
	const std::string Dispatch = FunctionBody(Source, "void CMenus::RenderSettingsQmClientContent(CUIRect MainView, bool ContributorsPage, bool PrewarmOnly)");
	ASSERT_FALSE(Dispatch.empty());
	EXPECT_NE(Dispatch.find("RenderSettingsQmClientContributors(MainView, PrewarmOnly)"), std::string::npos);
	// 搜索页的 tab 路由表已删除：contributors 的落地页只由 SetSettingsPageFromCardTab 解析（见下），菜单文件不得再留一份。
	EXPECT_EQ(Source.find("str_comp(pTab, \"qmclient-contributors\") == 0"), std::string::npos);
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string SetPageBody = FunctionBody(MenusSource, "bool CMenus::SetSettingsPageFromCardTab(const char *pTab)");
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"qmclient-contributors\") == 0"), std::string::npos);
	EXPECT_NE(Body.find("qmclient-community-thanks"), std::string::npos);
	EXPECT_NE(Body.find("BuildSponsorLines"), std::string::npos);
	EXPECT_NE(Body.find("!ReadOnly && g_QmClientEnsureSponsorQrTexture"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RenderCached("), std::string::npos);
}

TEST(QmNewUiMenuBranches, SponsorsUseLiveNamesAndInvalidateCardCaches)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsQmClientContributors(CUIRect MainView, bool PrewarmOnly)");
	const std::string Lines = BlockBodyAfter(Body, "const auto BuildSponsorLines");
	EXPECT_NE(Lines.find("GameClient()->m_QmClient.QmSponsorNames()"), std::string::npos);
	EXPECT_NE(Lines.find("s_CachedSponsorsRevision == SponsorsRevision"), std::string::npos);
	EXPECT_NE(Lines.find("s_CachedSponsorsRevision = SponsorsRevision;"), std::string::npos);
	EXPECT_NE(Body.find("CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (uint64_t)SponsorsRevision;"), std::string::npos);
	EXPECT_NE(Body.find("Sponsors.m_MeasureRevision = (uint64_t)SponsorsRevision"), std::string::npos);
	EXPECT_NE(Body.find("ColorRGBA(0.95f, 0.8f, 0.2f, 1.0f)"), std::string::npos);
	EXPECT_NE(Lines.find("const char *pSeparator = \", \";"), std::string::npos);
	const std::string LongName = BlockBodyAfter(Lines, "if(NameWidth > MaxLineWidth)");
	EXPECT_NE(LongName.find("qm_md::SplitUtf8(Name)"), std::string::npos);
	EXPECT_NE(LongName.find("LineWidth + GlyphWidth > MaxLineWidth"), std::string::npos);
	EXPECT_NE(LongName.find("Lines.back().append(Glyph);"), std::string::npos);
	EXPECT_EQ(Source.find("QM_SPONSOR_NAMES"), std::string::npos);
	EXPECT_EQ(Source.find("qmclient/player_title.h"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SponsorPublishingControlsRespectReadOnlyAndBusyState)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsQmClientContributors(CUIRect MainView, bool PrewarmOnly)");
	const std::string Render = BlockBodyAfter(Body, "Sponsors.m_Render =");
	const std::string Developer = BlockBodyAfter(Render, "if(HasSponsorDeveloper)");
	EXPECT_NE(Body.find("const bool HasSponsorDeveloper = GameClient()->m_QmClient.HasDeveloperCredential();"), std::string::npos);
	EXPECT_NE(Body.find("CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (HasSponsorDeveloper ? 1u : 0u);"), std::string::npos);
	EXPECT_NE(Render.find("if(!ReadOnly && ui_widget::SecondaryButton(Ctx, &s_RefreshSponsorsButton"), std::string::npos);
	EXPECT_NE(Render.find("QmClient.QmSponsorsRefresh();"), std::string::npos);
	EXPECT_NE(Developer.find("if(!ReadOnly && ui_widget::SecondaryButton(Ctx, &s_ReloadSponsorsDraftButton, Localize(\"Reload\"), ReloadDraftRect, Publishing))"), std::string::npos);
	EXPECT_NE(Developer.find("QmClient.QmSponsorsReloadDraft();"), std::string::npos);
	EXPECT_NE(Developer.find("if(!ReadOnly && ui_widget::PrimaryButton(Ctx, &s_PublishSponsorsButton"), std::string::npos);
	EXPECT_NE(Developer.find("Publishing || QmClient.QmSponsorsDraft()[0] == '\\0'"), std::string::npos);
	EXPECT_NE(Developer.find("QmClient.QmSponsorsPublishDraft();"), std::string::npos);
	EXPECT_NE(Developer.find("if(!ReadOnly && ui_widget::SecondaryButton(Ctx, &s_OpenSponsorsFolderButton"), std::string::npos);
	EXPECT_NE(Developer.find("Storage()->GetCompletePath(IStorage::TYPE_SAVE, \"qmclient\""), std::string::npos);
	EXPECT_NE(Render.find("QmClient.QmSponsorNames().empty()"), std::string::npos);
	EXPECT_NE(Render.find("Localize(\"Loading\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SponsorNudgeUsesLiveCountAndFallsBackBeforeNamesArrive)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSponsorNudge(CUIRect Screen)");
	EXPECT_NE(Body.find("else if(!pGameClient->m_QmClient.QmSponsorNames().empty())"), std::string::npos);
	EXPECT_NE(Body.find("(int)pGameClient->m_QmClient.QmSponsorNames().size()"), std::string::npos);
	EXPECT_NE(Body.find("str_copy(aText, Localize(\"Thanks for supporting QmClient\"), sizeof(aText));"), std::string::npos);
	EXPECT_EQ(Body.find("QM_SPONSOR_NAMES"), std::string::npos);
}

TEST(QmNewUiMenuBranches, MenubarUsesExplicitQmNewUiColorBranch)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string DoMenuTabV2 = FunctionBody(Source, "int CMenus::DoMenuTabV2(");
	const std::string RenderMenubar = FunctionBody(Source, "void CMenus::RenderMenubar(");
	const size_t UseNewUiIfPos = RenderMenubar.find("if(UseNewUi)");
	ASSERT_NE(UseNewUiIfPos, std::string::npos);
	const size_t UseNewUiBodyStart = RenderMenubar.find("{", UseNewUiIfPos);
	ASSERT_NE(UseNewUiBodyStart, std::string::npos);
	const size_t UseNewUiBodyEnd = MatchingBrace(RenderMenubar, UseNewUiBodyStart);
	ASSERT_NE(UseNewUiBodyEnd, std::string::npos);
	const std::string UseNewUiBlock = RenderMenubar.substr(UseNewUiBodyStart, UseNewUiBodyEnd - UseNewUiBodyStart);
	const size_t OldUiElsePos = RenderMenubar.find("else", UseNewUiBodyEnd);
	ASSERT_NE(OldUiElsePos, std::string::npos);
	const size_t OldUiBodyStart = RenderMenubar.find("{", OldUiElsePos);
	ASSERT_NE(OldUiBodyStart, std::string::npos);
	const size_t OldUiBodyEnd = MatchingBrace(RenderMenubar, OldUiBodyStart);
	ASSERT_NE(OldUiBodyEnd, std::string::npos);
	const std::string OldUiBlock = RenderMenubar.substr(OldUiBodyStart, OldUiBodyEnd - OldUiBodyStart);
	const size_t HoverBranch = DoMenuTabV2.find("if(Hover)");
	const size_t ActiveBranch = DoMenuTabV2.find("else if(Active)");

	EXPECT_NE(Source.find("const bool UseNewUi = g_Config.m_QmNewUi != 0;"), std::string::npos);
	EXPECT_NE(Source.find("MenuTabDefaultColor("), std::string::npos);
	EXPECT_NE(Source.find("MenuTabActiveColor("), std::string::npos);
	EXPECT_NE(Source.find("MenuTabHoverColor("), std::string::npos);
	EXPECT_NE(Source.find("MenuIconButtonDefaultColor("), std::string::npos);
	ASSERT_NE(HoverBranch, std::string::npos);
	ASSERT_NE(ActiveBranch, std::string::npos);
	EXPECT_LT(HoverBranch, ActiveBranch);
	EXPECT_NE(DoMenuTabV2.find("Target = pCustomHover != nullptr ? *pCustomHover : HoverColor;"), std::string::npos);
	EXPECT_NE(DoMenuTabV2.find("Target = pCustomActive != nullptr ? *pCustomActive : ActiveColor;"), std::string::npos);
	EXPECT_NE(Source.find("return UseNewUi ? MenuTabDefaultColor() : ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);"), std::string::npos);
	EXPECT_NE(Source.find("const ColorRGBA DefaultColor = UseNewUi ? MenuTabDefaultColor() : ms_ColorTabbarInactive;"), std::string::npos);
	EXPECT_NE(Source.find("const ColorRGBA ActiveColor = UseNewUi ? MenuTabActiveColor() : ms_ColorTabbarActive;"), std::string::npos);
	EXPECT_NE(Source.find("const ColorRGBA HoverColor = UseNewUi ? MenuTabHoverColor() : ms_ColorTabbarHover;"), std::string::npos);
	EXPECT_NE(DoMenuTabV2.find("DrawRoundedSurface(Ui(), *pRect, Resolved, ColorRGBA(), UseNewUi ? 7.0f * ContentScale : 10.0f, 0.0f, Corners);"), std::string::npos);
	EXPECT_NE(DoMenuTabV2.find("const float LabelFontSize = UseNewUi ? ui_token::settings::TAB_FONT_SIZE * ContentScale : Label.h * CUi::ms_FontmodHeight;"), std::string::npos);
	EXPECT_NE(DoMenuTabV2.find("Ui()->DoLabel(&Label, pText, LabelFontSize, TEXTALIGN_MC);"), std::string::npos);
	EXPECT_NE(Source.find("const bool UseNewUi = g_Config.m_QmNewUi != 0;"), std::string::npos);
	// 新 UI 菜单栏页签的表面色统一由胶囊 Tabbar 样式给出，页签自己不再各自算一套色。
	EXPECT_NE(Source.find("ui_widget::SCapsuleTabBarStyle MenuCapsuleTabBarStyle()"), std::string::npos);
	EXPECT_NE(Source.find("Style.m_CapsuleColor = MenuTabDefaultColor();"), std::string::npos);
	EXPECT_NE(Source.find("Style.m_IndicatorColor = MenuCapsuleTabIndicatorColor();"), std::string::npos);
	EXPECT_NE(Source.find("ColorRGBA InactiveColor = ms_ColorTabbarInactive;"), std::string::npos);
	EXPECT_NE(Source.find("ColorRGBA ActiveColor = ms_ColorTabbarActive;"), std::string::npos);
	EXPECT_NE(Source.find("ColorRGBA HoverColor = ms_ColorTabbarHover;"), std::string::npos);
	// 胶囊 Tabbar：新 UI 的激活位置由滑块胶囊表达，页签下方的下划线小块必须消失。
	EXPECT_EQ(Source.find("const ColorRGBA IndicatorColor = g_Config.m_QmNewUi != 0 ? MenuUiColorAccent(1.0f) : ui_token::color::ACCENT_PRIMARY;"), std::string::npos);
	EXPECT_EQ(RenderMenubar.find("if(UseNewUi && MenubarHaveActive && !Ui()->RenderOnly())"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("ui_widget::CapsuleTabBarChrome(TabBarCtx, MakeUiScopeHash(\"menubar_capsule_ingame_tabs\")"), std::string::npos);
	EXPECT_LT(UseNewUiBlock.find("ui_widget::CapsuleTabBarChrome("), UseNewUiBlock.find("DoIngameMenuTab(&s_aOnlineTabButtons[DrawnOnlineTabs]"));
	EXPECT_NE(RenderMenubar.find("if(!UseNewUi && MenubarHaveActive && !Ui()->RenderOnly())"), std::string::npos);
	EXPECT_NE(RenderMenubar.find("if(UseNewUi)"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("Box.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.12f)"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("Box.VMargin(MenubarOuterInsetX, &Box);"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("Box.HMargin(MenubarOuterInsetY, &Box);"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float BrowserButtonWidth = 58.0f * MENU_MENUBAR_CONTENT_SCALE_NEW;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float GameButtonWidth = (CompactOnlineMenuTabs ? 56.0f : 64.0f) * MENU_MENUBAR_CONTENT_SCALE_NEW;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float ServerInfoButtonWidth = (CompactOnlineMenuTabs ? 94.0f : 104.0f) * MENU_MENUBAR_CONTENT_SCALE_NEW;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float OnlineTabGap = 4.0f;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("{PAGE_GAME, \"ingame-tab-game\", Localize(\"Game\"), GameButtonWidth, true},"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("DoIngameMenuTab(&s_aOnlineTabButtons[DrawnOnlineTabs], Tab.m_Page, Tab.m_pTextId, Tab.m_pText, ActivePage == Tab.m_Page, &TabRect, IGraphics::CORNER_ALL)"), std::string::npos);
	EXPECT_EQ(UseNewUiBlock.find("IGraphics::CORNER_TL"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("if(DoMenuTabV2(&s_SettingsButton"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("ui_widget::CapsuleTabBarChrome(TabBarCtx, MakeUiScopeHash(\"menubar_capsule_start_tabs\")"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("if(DoMenuTabV2(&s_aStartTabButtons[TabIndex], Tab.m_pIcon, TabActive, &aStartTabSlots[TabIndex], IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, Tab.m_pCommunityIcon, nullptr, MENU_MENUBAR_CONTENT_SCALE_NEW, true))"), std::string::npos);
	EXPECT_LT(UseNewUiBlock.find("ui_widget::CapsuleTabBarChrome(TabBarCtx, MakeUiScopeHash(\"menubar_capsule_start_tabs\")"), UseNewUiBlock.find("if(DoMenuTabV2(&s_aStartTabButtons[TabIndex]"));
	EXPECT_EQ(UseNewUiBlock.find("DoButton_MenuTab(&s_SettingsButton"), std::string::npos);
	EXPECT_EQ(UseNewUiBlock.find("DoButton_MenuTab(&s_aStartTabButtons"), std::string::npos);
	EXPECT_EQ(UseNewUiBlock.find("static CButtonContainer s_InternetButton;"), std::string::npos);
	EXPECT_EQ(OldUiBlock.find("Box.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.12f)"), std::string::npos);
	EXPECT_EQ(OldUiBlock.find("Box.VMargin(MenubarOuterInsetX, &Box);"), std::string::npos);
	EXPECT_EQ(OldUiBlock.find("Box.HMargin(MenubarOuterInsetY, &Box);"), std::string::npos);
	EXPECT_NE(OldUiBlock.find("if(DoButton_MenuTab(&s_SettingsButton"), std::string::npos);
	EXPECT_NE(OldUiBlock.find("if(DoButton_MenuTab(&s_InternetButton"), std::string::npos);
	EXPECT_EQ(OldUiBlock.find("DoMenuTabV2(&s_SettingsButton"), std::string::npos);
	EXPECT_EQ(OldUiBlock.find("DoMenuTabV2(&s_InternetButton"), std::string::npos);
	EXPECT_NE(OldUiBlock.find("DoIngameMenuTab(&s_GameButton, PAGE_GAME, \"ingame-tab-game\", Localize(\"Game\"), ActivePage == PAGE_GAME, &Button, IGraphics::CORNER_TL)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, MenubarScalesOnlyNewUiInternalElementsByTenPercent)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Header = ReadTextFile("src/game/client/components/menus.h");
	const std::string DoMenuTabV2 = FunctionBody(Source, "int CMenus::DoMenuTabV2(");
	const std::string RenderMenubar = FunctionBody(Source, "void CMenus::RenderMenubar(");
	const std::string DoIngameMenuTab = FunctionBody(Source, "int CMenus::DoIngameMenuTab(");
	const size_t UseNewUiIfPos = RenderMenubar.find("if(UseNewUi)");
	ASSERT_NE(UseNewUiIfPos, std::string::npos);
	const size_t UseNewUiBodyStart = RenderMenubar.find("{", UseNewUiIfPos);
	ASSERT_NE(UseNewUiBodyStart, std::string::npos);
	const size_t UseNewUiBodyEnd = MatchingBrace(RenderMenubar, UseNewUiBodyStart);
	ASSERT_NE(UseNewUiBodyEnd, std::string::npos);
	const std::string UseNewUiBlock = RenderMenubar.substr(UseNewUiBodyStart, UseNewUiBodyEnd - UseNewUiBodyStart);
	const size_t OldUiElsePos = RenderMenubar.find("else", UseNewUiBodyEnd);
	ASSERT_NE(OldUiElsePos, std::string::npos);
	const size_t OldUiBodyStart = RenderMenubar.find("{", OldUiElsePos);
	ASSERT_NE(OldUiBodyStart, std::string::npos);
	const size_t OldUiBodyEnd = MatchingBrace(RenderMenubar, OldUiBodyStart);
	ASSERT_NE(OldUiBodyEnd, std::string::npos);
	const std::string OldUiBlock = RenderMenubar.substr(OldUiBodyStart, OldUiBodyEnd - OldUiBodyStart);

	EXPECT_NE(Source.find("constexpr float MENU_MENUBAR_HEIGHT_NEW = 24.0f;"), std::string::npos);
	EXPECT_NE(Source.find("constexpr float MENU_MENUBAR_CONTENT_SCALE_NEW = 1.10f;"), std::string::npos);
	EXPECT_NE(Header.find("float ContentScale = 1.0f"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float MenubarOuterInsetX = 6.0f;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float MenubarBaseOuterInsetY = 2.5f;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float MenubarOuterInsetY = (Box.h - (Box.h - 2.0f * MenubarBaseOuterInsetY) * MENU_MENUBAR_CONTENT_SCALE_NEW) * 0.5f;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float MenubarIconGap = 6.0f;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float MenubarItemGap = 4.0f;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float BrowserButtonWidth = 58.0f * MENU_MENUBAR_CONTENT_SCALE_NEW;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float GameButtonWidth = (CompactOnlineMenuTabs ? 56.0f : 64.0f) * MENU_MENUBAR_CONTENT_SCALE_NEW;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float ServerInfoButtonWidth = (CompactOnlineMenuTabs ? 94.0f : 104.0f) * MENU_MENUBAR_CONTENT_SCALE_NEW;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float BrowserButtonWidth = (CompactOnlineMenuTabs ? 56.0f : 64.0f) * MENU_MENUBAR_CONTENT_SCALE_NEW;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float GhostButtonWidth = (CompactOnlineMenuTabs ? 56.0f : 64.0f) * MENU_MENUBAR_CONTENT_SCALE_NEW;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float CallVoteButtonWidth = (CompactOnlineMenuTabs ? 80.0f : 88.0f) * MENU_MENUBAR_CONTENT_SCALE_NEW;"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("const float OnlineTabGap = 4.0f;"), std::string::npos);
	// 右侧图标簇（Quit / Settings / Editor / 进服后的 Demo）必须共用同一个槽位尺寸与间隔，
	// 否则 Demo 两侧会各多出一段空白，整排图标看上去被隔开。
	EXPECT_EQ(UseNewUiBlock.find("Box.VSplitRight(10.0f, &Box, nullptr);"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("Box.VSplitRight(MenubarIconGap, &Box, nullptr);"), std::string::npos);
	EXPECT_NE(UseNewUiBlock.find("Box.VSplitRight(MenubarIconButtonSize, &Box, &Button);"), std::string::npos);
	EXPECT_NE(DoMenuTabV2.find("pRect->Margin(2.0f * ContentScale, &IconRect);"), std::string::npos);
	EXPECT_NE(DoMenuTabV2.find("pRect->HMargin(2.0f * ContentScale, &Label);"), std::string::npos);
	EXPECT_NE(DoMenuTabV2.find("UseNewUi ? 7.0f * ContentScale : 10.0f"), std::string::npos);
	EXPECT_NE(DoMenuTabV2.find("const float LabelFontSize = UseNewUi ? ui_token::settings::TAB_FONT_SIZE * ContentScale : Label.h * CUi::ms_FontmodHeight;"), std::string::npos);
	EXPECT_NE(DoIngameMenuTab.find("const float ContentScale = g_Config.m_QmNewUi != 0 ? MENU_MENUBAR_CONTENT_SCALE_NEW : 1.0f;"), std::string::npos);
	EXPECT_NE(DoIngameMenuTab.find("Text.HMargin(2.0f * ContentScale, &Text);"), std::string::npos);
	EXPECT_NE(DoIngameMenuTab.find("const float FontSize = g_Config.m_QmNewUi != 0 ? ui_token::settings::TAB_FONT_SIZE * ContentScale : Text.h * CUi::ms_FontmodHeight;"), std::string::npos);
	EXPECT_NE(DoIngameMenuTab.find("return DoMenuTabV2(pButtonContainer, pText, Checked != 0, pRect, Corners, nullptr, nullptr, nullptr, nullptr, &TextElement, ContentScale, true);"), std::string::npos);
	EXPECT_EQ(OldUiBlock.find("MENU_MENUBAR_CONTENT_SCALE_NEW"), std::string::npos);
}

TEST(QmNewUiMenuBranches, CapsuleTabBarRowRectSpansSlotsAndGaps)
{
	// 意图：胶囊容器覆盖整排 Tab（含 Tab 之间的间隙），而不是只包住第一个槽位。
	const CUIRect aSlots[] = {
		{10.0f, 4.0f, 60.0f, 20.0f},
		{74.0f, 4.0f, 60.0f, 20.0f},
		{138.0f, 4.0f, 100.0f, 20.0f},
	};
	const CUIRect Row = ui_widget::CapsuleTabBarRowRect(aSlots, 3);
	EXPECT_FLOAT_EQ(Row.x, 10.0f);
	EXPECT_FLOAT_EQ(Row.y, 4.0f);
	EXPECT_FLOAT_EQ(Row.w, 228.0f);
	EXPECT_FLOAT_EQ(Row.h, 20.0f);

	EXPECT_FLOAT_EQ(ui_widget::CapsuleTabBarRowRect(aSlots, 1).w, 60.0f);
	EXPECT_FLOAT_EQ(ui_widget::CapsuleTabBarRowRect(nullptr, 3).w, 0.0f);
	EXPECT_FLOAT_EQ(ui_widget::CapsuleTabBarRowRect(aSlots, 0).h, 0.0f);
}

TEST(QmNewUiMenuBranches, NestedRadioRowKeepsLabelAboveFixedHeightContainer)
{
	// 意图：两级分段行固定「标签一行 + 控件一行」，一级选项带不带子级都不改行高，
	// 否则切换「显示昵称」会让卡片高度跳动、预布局与绘制阶段消耗的行高对不上。
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(420.0f);
	const SSettingsNestedRadioRowLayout Row = ResolveSettingsNestedRadioRowLayout({10.0f, 20.0f, 420.0f, Metrics.m_LineHeight}, Metrics);
	EXPECT_FLOAT_EQ(Row.m_LabelRect.x, 10.0f);
	EXPECT_FLOAT_EQ(Row.m_LabelRect.y, 20.0f);
	EXPECT_FLOAT_EQ(Row.m_LabelRect.w, 420.0f);
	EXPECT_FLOAT_EQ(Row.m_LabelRect.h, Metrics.m_LineHeight);
	EXPECT_FLOAT_EQ(Row.m_ContainerRect.x, 10.0f);
	EXPECT_FLOAT_EQ(Row.m_ContainerRect.y, 20.0f + Metrics.m_LineHeight + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(Row.m_ContainerRect.w, 420.0f);
	EXPECT_FLOAT_EQ(Row.m_ContainerRect.h, Metrics.m_ButtonHeight);
	EXPECT_FLOAT_EQ(Row.m_Height, Metrics.m_LineHeight + Metrics.m_LineSpacing + Metrics.m_ButtonHeight);

	EXPECT_FLOAT_EQ(ResolveSettingsNestedRadioRowLayout({0.0f, 0.0f, 0.0f, Metrics.m_LineHeight}, Metrics).m_Height, 0.0f);
}

TEST(QmNewUiMenuBranches, NestedRadioSlotsReplaceActiveMainLabelWithSubMenu)
{
	// 意图：一级项带子级时，它的一级槽位整段让给子级菜单（一级标签被替换掉），
	// 主滑块因此盖住整段子级区域；否则一级标签会和子级并排、滑块只盖住标签。
	const CUIRect Container = {0.0f, 0.0f, 600.0f, 24.0f};
	const SSettingsNestedRadioSlots Plain = ResolveSettingsNestedRadioSlots(Container, 4, 0, 0, 0.0f);
	ASSERT_EQ(Plain.m_MainCount, 4);
	EXPECT_EQ(Plain.m_SubCount, 0);
	for(int i = 0; i < 4; ++i)
	{
		EXPECT_FLOAT_EQ(Plain.m_aMain[i].x, 150.0f * (float)i);
		EXPECT_FLOAT_EQ(Plain.m_aMain[i].w, 150.0f);
	}

	// 5 个单位宽 120：一级 0 / 一级 1（被替换，主滑块占 2 个单位）/ 一级 2 / 一级 3。
	const SSettingsNestedRadioSlots Nested = ResolveSettingsNestedRadioSlots(Container, 4, 1, 2, 0.0f);
	ASSERT_EQ(Nested.m_MainCount, 4);
	ASSERT_EQ(Nested.m_SubCount, 2);
	EXPECT_FLOAT_EQ(Nested.m_aMain[0].x, 0.0f);
	EXPECT_FLOAT_EQ(Nested.m_aMain[0].w, 120.0f);
	EXPECT_FLOAT_EQ(Nested.m_aMain[1].x, 120.0f);
	EXPECT_FLOAT_EQ(Nested.m_aMain[1].w, 240.0f);
	EXPECT_FLOAT_EQ(Nested.m_aSub[0].x, 120.0f);
	EXPECT_FLOAT_EQ(Nested.m_aSub[0].w, 120.0f);
	EXPECT_FLOAT_EQ(Nested.m_aSub[1].x, 240.0f);
	EXPECT_FLOAT_EQ(Nested.m_aSub[1].w, 120.0f);
	EXPECT_FLOAT_EQ(Nested.m_aMain[2].x, 360.0f);
	EXPECT_FLOAT_EQ(Nested.m_aMain[3].x, 480.0f);
	// 槽位内缩由调用方给：容器与槽位之间留出胶囊边距。
	const SSettingsNestedRadioSlots Inset = ResolveSettingsNestedRadioSlots(Container, 4, 1, 2, 2.0f);
	EXPECT_FLOAT_EQ(Inset.m_aMain[0].x, 2.0f);
	EXPECT_FLOAT_EQ(Inset.m_aMain[0].h, 20.0f);
}

TEST(QmNewUiMenuBranches, NestedSegmentChromeDrawsContainerAndTwoSpringTracks)
{
	// 意图：容器与两枚滑块必须先于分段文字绘制（滑块压在文字之下），
	// 主滑块与次级滑块各占一条弹簧轨道，切换时带速度续接地滑过去。
	const std::string Source = ReadTextFile("src/game/client/QmUi/UiNavigation.cpp");
	const std::string Header = ReadTextFile("src/game/client/QmUi/UiNavigation.h");
	ASSERT_NE(Source.find("void NestedSegmentChrome("), std::string::npos);
	ASSERT_NE(Header.find("struct SNestedSegmentStyle"), std::string::npos);
	EXPECT_NE(Header.find("void NestedSegmentChrome(const IUiContext &Ctx, uint64_t GroupId, const CUIRect &ContainerRect, const CUIRect *pMainSlot, const CUIRect *pSubSlot, const SNestedSegmentStyle &Style);"), std::string::npos);

	const std::string Body = FunctionBody(Source, "void NestedSegmentChrome(");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("if(Ctx.m_pUi->RenderOnly())\n\t\t\treturn;"), std::string::npos);
	const size_t ContainerDraw = Body.find("DrawRoundedSurface(Ctx, ContainerRect, Style.m_ContainerColor, ColorRGBA(), ui_token::radius::PILL);");
	const size_t MainDraw = Body.find("DrawIndicator(pMainSlot, Style.m_IndicatorInset, Style.m_MainIndicatorColor, ColorRGBA(), 0);");
	const size_t SubDraw = Body.find("DrawIndicator(pSubSlot, Style.m_SubIndicatorInset, Style.m_SubIndicatorColor, Style.m_SubIndicatorBorderColor, 1);");
	ASSERT_NE(ContainerDraw, std::string::npos);
	ASSERT_NE(MainDraw, std::string::npos);
	ASSERT_NE(SubDraw, std::string::npos);
	EXPECT_LT(ContainerDraw, MainDraw);
	EXPECT_LT(MainDraw, SubDraw);
	// 次级滑块带描边，才和主滑块区分得开。
	EXPECT_NE(Body.find("DrawRoundedSurface(Ctx, Indicator, Fill, Border, ui_token::radius::PILL, HasBorder ? Ctx.m_pUi->PixelSize() : 0.0f);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NamePlateShowRowUsesFlatSixSegmentCapsule)
{
	// 意图：昵称板块的「显示昵称」重整为六档互斥范围（无 / 当前 / 当前+本地 / 他人 / 本地+他人 / 全体），
	// 宽够时是单行六段胶囊（只有主滑块），旧 UI 与窄窗口用同一份选项走可换行分段行，语义一致。
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const size_t RowPos = Source.find("const SSettingsSegmentedRowLayout ShowNameRow = ResolveShowNameRowLayout(LeftView);");
	ASSERT_NE(RowPos, std::string::npos);
	const size_t ChromePos = Source.find("ui_widget::NestedSegmentChrome(TabBarUiContext(), ShowNameGroup, ShowNameRow.m_ContainerRect,", RowPos);
	const size_t MainDrawPos = Source.find("if(DoSettingsButton_CapsuleSegment(SETTINGS_APPEARANCE, APPEARANCE_TAB_NAME_PLATE, APPEARANCE_TAB_NAME_PLATE, &m_vButtonContainersNamePlateShow[i]", RowPos);
	ASSERT_NE(ChromePos, std::string::npos);
	ASSERT_NE(MainDrawPos, std::string::npos);
	EXPECT_LT(RowPos, ChromePos);
	EXPECT_LT(ChromePos, MainDrawPos);
	// 六档选项按顺序绑定档位常量（逐条检查，避免把缩进写进断言）。
	EXPECT_NE(Source.find("const int aShowScopeValues[] = {"), std::string::npos);
	const size_t ValuesPos = Source.find("const int aShowScopeValues[] = {");
	ASSERT_NE(ValuesPos, std::string::npos);
	const size_t ValuesEnd = Source.find("};", ValuesPos);
	ASSERT_NE(ValuesEnd, std::string::npos);
	const std::string ValuesBlock = Source.substr(ValuesPos, ValuesEnd - ValuesPos);
	const char *apExpectedScopeOrder[] = {"QM_NAMEPLATE_SHOW_SCOPE_OFF", "QM_NAMEPLATE_SHOW_SCOPE_CURRENT", "QM_NAMEPLATE_SHOW_SCOPE_LOCAL", "QM_NAMEPLATE_SHOW_SCOPE_OTHERS", "QM_NAMEPLATE_SHOW_SCOPE_OTHERS_LOCAL", "QM_NAMEPLATE_SHOW_SCOPE_ALL"};
	size_t PreviousEntry = 0;
	for(const char *pEntry : apExpectedScopeOrder)
	{
		const size_t EntryPos = ValuesBlock.find(pEntry);
		ASSERT_NE(EntryPos, std::string::npos) << pEntry;
		EXPECT_LT(PreviousEntry, EntryPos) << pEntry;
		PreviousEntry = EntryPos;
	}
	// 档位个数常量与档位数组同源，不允许再多写魔法数字 6。
	EXPECT_NE(Source.find("const int ShowScopeCount = (int)std::size(aShowScopeValues);"), std::string::npos);
	EXPECT_NE(Source.find("const int Pressed = std::clamp(g_Config.m_QmNameplateShowScope, 0, ShowScopeCount - 1);"), std::string::npos);
	EXPECT_NE(Source.find("if(DoSettingsButton_CapsuleSegment(SETTINGS_APPEARANCE, APPEARANCE_TAB_NAME_PLATE, APPEARANCE_TAB_NAME_PLATE, &m_vButtonContainersNamePlateShow[i], apShowScopeTextIds[i], apShowScopeLabels[i], Pressed == aShowScopeValues[i], &ShowNameSlots.m_aMain[i], AppearanceMetrics.m_SmallSize))"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmNameplateShowScope = aShowScopeValues[i];"), std::string::npos);
	// 单行六段：没有子级菜单，滑块只标主槽位。
	EXPECT_NE(Source.find("const SSettingsNestedRadioSlots ShowNameSlots = ResolveSettingsNestedRadioSlots(ShowNameRow.m_ContainerRect, ShowScopeCount, Pressed, 0, CapsuleSegmentInset);"), std::string::npos);
	EXPECT_NE(Source.find("ui_widget::NestedSegmentChrome(TabBarUiContext(), ShowNameGroup, ShowNameRow.m_ContainerRect, &ShowNameSlots.m_aMain[Pressed], nullptr, SettingsNestedSegmentStyle());"), std::string::npos);
	EXPECT_EQ(Source.find("if(ShowSubMenu && i == Pressed)"), std::string::npos);
	EXPECT_EQ(Source.find("const int SubCount = (OwnRow || OthersRow) ? 2 : 0;"), std::string::npos);
	EXPECT_EQ(Source.find("m_vButtonContainersNamePlateOwnScope"), std::string::npos);
	EXPECT_EQ(Source.find("m_vButtonContainersNamePlateOthersScope"), std::string::npos);
	// 窄窗口兜底：放不下六段胶囊时退回可换行分段行，两条路径都写同一个配置项。
	EXPECT_NE(Source.find("if(g_Config.m_QmNewUi != 0 && ShowNameRow.m_Capsule)"), std::string::npos);
	EXPECT_NE(Source.find("// 旧 UI，以及新 UI 下主内容区太窄放不下六段胶囊时：走可换行分段行。"), std::string::npos);
	EXPECT_NE(Source.find("DoShowScopeRadioMenu();"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmNameplateShowScope = Selected;"), std::string::npos);
	// 测量、预布局与绘制三个阶段必须共用 ResolveShowNameRowLayout 这一个行高解析结果。
	// 该 lambda 会被预布局回调按值拷走，所以标签表必须是静态的，不能指向栈上数组。
	EXPECT_NE(Source.find("const auto ResolveShowNameRowLayout = [=](const CUIRect &View) {"), std::string::npos);
	EXPECT_NE(Source.find("static const char *const apShowScopeLabels[] = {"), std::string::npos);
	EXPECT_NE(Source.find("return ResolveSettingsSegmentedRowLayout(View, ShowScopeCount, AppearanceMetrics, ShowNameOptionMinWidth());"), std::string::npos);
	EXPECT_NE(Source.find("const float GeneralContentHeight = ResolveShowNameRowLayout({0.0f, 0.0f, ContentWidth, LineSize * 2.0f + MarginSmall}).m_Height"), std::string::npos);
	EXPECT_NE(Source.find("LeftView.HSplitTop(ResolveShowNameRowLayout(LeftView).m_Height, nullptr, &LeftView);"), std::string::npos);
	EXPECT_NE(Source.find("const SSettingsSegmentedRowLayout ShowNameRow = ResolveShowNameRowLayout(LeftView);"), std::string::npos);
	// 分段宽度阈值按最长档位标签的真实宽度算，否则正常窗口宽度下胶囊分段永远不生效。
	EXPECT_NE(Source.find("const auto ShowNameOptionMinWidth = [this, AppearanceMetrics]() {"), std::string::npos);
	EXPECT_NE(Source.find("Widest = maximum(Widest, pTextRender->TextWidth(AppearanceMetrics.m_SmallSize, pLabel, -1));"), std::string::npos);
	EXPECT_EQ(Source.find("return RadioHeight(6);"), std::string::npos);
	EXPECT_EQ(Source.find("ConsumeRadio(6);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsSegmentedRowFallsBackToWrappingRadioWhenNarrow)
{
	// 意图：多档分段行的共用落位解析。够宽给胶囊分段（标签一行 + 控件一行），
	// 窄到分段放不下时给可换行分段行的标签/控件矩形，两者都由同一函数给出。
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	const CUIRect Wide{0.0f, 0.0f, 600.0f, Metrics.m_LineHeight};
	const CUIRect Narrow{0.0f, 0.0f, 300.0f, Metrics.m_LineHeight};
	const float Measured = 52.0f; // 小字号下「本地+他人」的量级

	const SSettingsSegmentedRowLayout WideLayout = ResolveSettingsSegmentedRowLayout(Wide, 6, Metrics, Measured);
	EXPECT_TRUE(WideLayout.m_Capsule);
	EXPECT_FLOAT_EQ(WideLayout.m_Height, Metrics.m_LineHeight + Metrics.m_LineSpacing + Metrics.m_ButtonHeight);
	EXPECT_FLOAT_EQ(WideLayout.m_ContainerRect.y, Metrics.m_LineHeight + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(WideLayout.m_ContainerRect.w, Wide.w);

	// 窄窗口必须退回可换行分段行，且行高/控件矩形与普通分段行一致。
	const SSettingsSegmentedRowLayout NarrowLayout = ResolveSettingsSegmentedRowLayout(Narrow, 6, Metrics, Measured);
	EXPECT_FALSE(NarrowLayout.m_Capsule);
	const SSettingsRadioRowLayout NarrowRadio = ResolveSettingsRadioRowLayout(Narrow, 6, Metrics);
	EXPECT_FLOAT_EQ(NarrowLayout.m_Height, NarrowRadio.m_Height);
	EXPECT_FLOAT_EQ(NarrowLayout.m_ContainerRect.w, NarrowRadio.m_ButtonsRect.w);

	// 不给量测宽度时退回保守估算（72px 下限），不能因此永远选胶囊：
	// 400 放不下六档，600 按胶囊真实几何够（旧阈值多扣一列标签宽时会误判）。
	EXPECT_FALSE(ResolveSettingsSegmentedRowLayout({0.0f, 0.0f, 400.0f, Metrics.m_LineHeight}, 6, Metrics, 0.0f).m_Capsule);
	EXPECT_TRUE(ResolveSettingsSegmentedRowLayout(Wide, 6, Metrics, 0.0f).m_Capsule);
	EXPECT_TRUE(ResolveSettingsSegmentedRowLayout({0.0f, 0.0f, 1000.0f, Metrics.m_LineHeight}, 6, Metrics, 0.0f).m_Capsule);

	// 宽度为 0 不能出现半成品布局；档位少时宽窗口仍走胶囊。
	const SSettingsSegmentedRowLayout Zero = ResolveSettingsSegmentedRowLayout({0.0f, 0.0f, 0.0f, 0.0f}, 6, Metrics, Measured);
	EXPECT_FLOAT_EQ(Zero.m_Height, 0.0f);
	EXPECT_FALSE(Zero.m_Capsule);
	EXPECT_FLOAT_EQ(ResolveSettingsSegmentedRowLayout(Wide, 2, Metrics, Measured).m_ContainerRect.w, Wide.w);

	// 阈值单调：宽度递增时只从「可换行」切到「胶囊」一次。
	bool SeenCapsule = false;
	for(float Width = 100.0f; Width <= 900.0f; Width += 4.0f)
	{
		const SSettingsSegmentedRowLayout Layout = ResolveSettingsSegmentedRowLayout({0.0f, 0.0f, Width, Metrics.m_LineHeight}, 6, Metrics, Measured);
		if(Layout.m_Capsule)
			SeenCapsule = true;
		else
			EXPECT_FALSE(SeenCapsule) << "宽度 " << Width << " 又退回可换行分段行";
	}
}

TEST(QmNewUiMenuBranches, SegmentedRowCapsuleFitsTwoColumnCardWidth)
{
	// 意图：胶囊分段的标签独占上一行、控件行用满整行宽度，阈值里不能再预扣一列标签宽。
	// 1920 宽窗口的新 UI 用两列卡片，卡片内容宽约 464：旧阈值（约 148 + 5 + 6×60 ≈ 513）
	// 会让「显示昵称」白白退回旧分段行，与同页「钩索强度范围」的胶囊外观对不上。
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	const CUIRect CardContent{0.0f, 0.0f, 464.0f, Metrics.m_LineHeight};
	// 与调用方一致：MinOptionWidth 含每档 8px 间隙（ShowNameOptionMinWidth / Widest + 8.0f）。
	const float ShowScopeOptionWidth = 52.0f + 8.0f; // 小字号下「本地+他人」+ 间隙
	const float HookScopeOptionWidth = 28.0f + 8.0f; // 小字号下「强钩」+ 间隙

	const SSettingsSegmentedRowLayout ShowScope = ResolveSettingsSegmentedRowLayout(CardContent, 6, Metrics, ShowScopeOptionWidth);
	EXPECT_TRUE(ShowScope.m_Capsule);
	EXPECT_FLOAT_EQ(ShowScope.m_ContainerRect.w, CardContent.w);
	EXPECT_TRUE(ResolveSettingsSegmentedRowLayout(CardContent, 5, Metrics, HookScopeOptionWidth).m_Capsule);

	// 真放不下的判定仍然保留：六档各要 52+8 时，356 退回可换行分段行，360 才够。
	EXPECT_FALSE(ResolveSettingsSegmentedRowLayout({0.0f, 0.0f, 356.0f, Metrics.m_LineHeight}, 6, Metrics, ShowScopeOptionWidth).m_Capsule);
	EXPECT_TRUE(ResolveSettingsSegmentedRowLayout({0.0f, 0.0f, 360.0f, Metrics.m_LineHeight}, 6, Metrics, ShowScopeOptionWidth).m_Capsule);
}

TEST(QmNewUiMenuBranches, CapsuleSegmentKeepsBudgetedTextPipeline)
{
	// 意图：胶囊分段只有外观换了，文字仍必须走设置页文本缓存，不能退化成直绘标签。
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Body = FunctionBody(Source, "int CMenus::DoSettingsButton_CapsuleSegment(");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("CollectMenuTextPlanItem(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, Subtab, pResolvedTextId, pText, &Text, ResolvedBodySize, TEXTALIGN_MC, Props, StyleKey);"), std::string::npos);
	EXPECT_NE(Body.find("CUIElement *pTextElement = pTextId != nullptr ? &MenuTextElement(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, Subtab, pResolvedTextId, StyleKey) : nullptr;"), std::string::npos);
	EXPECT_NE(Body.find("DoButton_MenuTab(pBC, pText, Checked, pRect, IGraphics::CORNER_ALL, nullptr, &Transparent, &Transparent, pHoverColor, Rounding, nullptr, pTextElement, ResolvedBodySize, CapsuleTab);"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoLabel("), std::string::npos);
	// 压在主滑块上的次级分段用调用方给的实色字，画完必须还原本帧字色。
	const size_t SetColorPos = Body.find("TextRender()->TextColor(*pLabelColor);");
	const size_t RestorePos = Body.find("TextRender()->TextColor(PreviousLabelColor);");
	ASSERT_NE(SetColorPos, std::string::npos);
	ASSERT_NE(RestorePos, std::string::npos);
	EXPECT_LT(SetColorPos, RestorePos);
	EXPECT_NE(Body.find("const bool CapsuleTab = !ExplicitLabelColor;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NestedSegmentStyleColorsSubMenuAgainstMainIndicator)
{
	// 意图：子级菜单压在主滑块上，字色/次级滑块都必须按「主滑块明暗」推导 ——
	// 按容器明暗推导会让子级文字在主滑块上糊成一片。
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Body = FunctionBody(Source, "ui_widget::SNestedSegmentStyle CMenus::SettingsNestedSegmentStyle() const");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("const bool MainIndicatorIsDark = ui_widget::CapsuleTabBarSurfaceIsLight(SurfaceColor);"), std::string::npos);
	EXPECT_NE(Body.find("Style.m_SubActiveLabelColor = ui_widget::CapsuleTabBarActiveLabelColor(SurfaceColor);"), std::string::npos);
	EXPECT_NE(Body.find("Style.m_SubInactiveLabelColor = MainIndicatorIsDark ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.55f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.50f);"), std::string::npos);
	EXPECT_NE(Body.find("Style.m_SubIndicatorColor = MainIndicatorIsDark ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, CapsuleTabBarChromeDrawsContainerThenSpringIndicatorUnderLabels)
{
	// 意图：滑块胶囊必须由容器的同一入口画在文字之前，并且由弹簧驱动、可被打断续接，
	// 否则切换 Tab 时会瞬移或盖住经过的页签文字。
	const std::string Source = ReadTextFile("src/game/client/QmUi/UiNavigation.cpp");
	const std::string Header = ReadTextFile("src/game/client/QmUi/UiNavigation.h");
	ASSERT_NE(Source.find("void CapsuleTabBarChrome("), std::string::npos);

	// ui_widget::TabBar（UiDogfood 调试页）同样走胶囊，不再保留下划线小块。
	const std::string DogfoodTabBar = FunctionBody(Source, "int TabBar(");
	ASSERT_FALSE(DogfoodTabBar.empty());
	EXPECT_EQ(DogfoodTabBar.find("Underline"), std::string::npos);
	EXPECT_EQ(DogfoodTabBar.find("Indicator.Draw("), std::string::npos);
	EXPECT_NE(DogfoodTabBar.find("std::vector<CUIRect> vTabSlots(static_cast<std::size_t>(Count));"), std::string::npos);
	EXPECT_LT(DogfoodTabBar.find("CapsuleTabBarChrome(Ctx, BuildUiAnimNodeKey(MakeUiScopeHash(\"ui_widget_tabbar_capsule\")"), DogfoodTabBar.find("DoButton_MenuTab(&ButtonPool[i], ppLabels[i], Checked, &vTabSlots[static_cast<std::size_t>(i)]"));

	EXPECT_NE(Source.find("if(Ctx.m_pUi->RenderOnly())\n\t\t\treturn;"), std::string::npos);
	const size_t CapsuleDraw = Source.find("DrawRoundedSurface(Ctx, Capsule, Style.m_CapsuleColor, ColorRGBA(), ui_token::radius::PILL);");
	const size_t IndicatorDraw = Source.find("DrawRoundedSurface(Ctx, Indicator, Style.m_IndicatorColor, ColorRGBA(), ui_token::radius::PILL);");
	ASSERT_NE(CapsuleDraw, std::string::npos);
	ASSERT_NE(IndicatorDraw, std::string::npos);
	EXPECT_LT(CapsuleDraw, IndicatorDraw);
	// 导航滑块统一走 NAVIGATION_SPRING，四轴（x/y/w/h）同弹簧、可续接打断。
	EXPECT_NE(Source.find("ResolveUiAnimSpringValue(*Ctx.m_pAnim, NodeKey, EUiAnimProperty::POS_X, Target.x, ui_token::motion::NAVIGATION_SPRING, 2)"), std::string::npos);
	EXPECT_NE(Source.find("ResolveUiAnimSpringValue(*Ctx.m_pAnim, NodeKey, EUiAnimProperty::WIDTH, Target.w, ui_token::motion::NAVIGATION_SPRING, 2)"), std::string::npos);
	EXPECT_NE(Source.find("ui_token::motion::NAVIGATION_SPRING"), std::string::npos);
	EXPECT_EQ(Source.find("s_IndicatorSpring"), std::string::npos);
	EXPECT_NE(Header.find("inline CUIRect CapsuleTabBarRowRect(const CUIRect *pSlots, int Count)"), std::string::npos);
	// 配色自适应由 QmUi 统一提供，各 Tabbar 只传自己的容器表面色。
	EXPECT_NE(Header.find("inline bool CapsuleTabBarSurfaceIsLight(const ColorRGBA &SurfaceColor)"), std::string::npos);
	EXPECT_NE(Header.find("inline ColorRGBA CapsuleTabBarIndicatorColor(const ColorRGBA &SurfaceColor)"), std::string::npos);
	EXPECT_NE(Header.find("inline ColorRGBA CapsuleTabBarActiveLabelColor(const ColorRGBA &SurfaceColor)"), std::string::npos);
	EXPECT_NE(Header.find("inline ColorRGBA CapsuleTabBarInactiveLabelColor(const ColorRGBA &SurfaceColor)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, CapsuleTabKeepsHoverOnlyAndFlipsLabelColor)
{
	// 意图：胶囊 Tab 自己不再画分块底色（激活外观由滑块承担），但 hover 反馈与
	// 激活文字转深色必须保留。
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string DoMenuTabV2 = FunctionBody(Source, "int CMenus::DoMenuTabV2(");
	const std::string DoIngameMenuTab = FunctionBody(Source, "int CMenus::DoIngameMenuTab(");
	ASSERT_FALSE(DoMenuTabV2.empty());

	EXPECT_NE(Source.find("const bool InCapsule = CapsuleTab && UseNewUi;"), std::string::npos);
	EXPECT_NE(DoMenuTabV2.find("if(InCapsule)\n\t{\n\t\tif(Hover)\n\t\t\tDrawRoundedSurface(Ui(), *pRect, MenuCapsuleTabHoverColor(), ColorRGBA(), ui_token::radius::PILL, 0.0f, Corners);\n\t}\n\telse\n\t\tDrawRoundedSurface(Ui(), *pRect, Resolved, ColorRGBA(), UseNewUi ? 7.0f * ContentScale : 10.0f, 0.0f, Corners);"), std::string::npos);
	EXPECT_NE(DoMenuTabV2.find("TextRender()->TextColor(Active ? MenuCapsuleTabActiveLabelColor() : MenuCapsuleTabInactiveLabelColor());"), std::string::npos);
	EXPECT_LT(DoMenuTabV2.find("TextRender()->TextColor(Active ? MenuCapsuleTabActiveLabelColor()"), DoMenuTabV2.find("DoMenuLabelStreamed(MENU_TEXT_SCOPE_INGAME, *pTextUiElement"));
	EXPECT_NE(DoMenuTabV2.find("TextRender()->TextColor(PreviousLabelColor);"), std::string::npos);
	EXPECT_NE(DoIngameMenuTab.find("ContentScale, true);"), std::string::npos);
	EXPECT_NE(Source.find("ColorRGBA MenuCapsuleTabIndicatorColor()"), std::string::npos);
	EXPECT_NE(Source.find("return ui_widget::CapsuleTabBarIndicatorColor(MenuTabDefaultColor());"), std::string::npos);
	// DoButton_MenuTab 也要支持胶囊模式：设置页左栏等竖向 Tabbar 走同一条路径。
	const std::string DoButtonMenuTab = FunctionBody(Source, "int CMenus::DoButton_MenuTab(");
	ASSERT_FALSE(DoButtonMenuTab.empty());
	EXPECT_NE(DoButtonMenuTab.find("if(CapsuleTab)\n\t{\n\t\t// 胶囊 Tab 的激活外观由滑块胶囊承担，Tab 自己不再画分块底色，只保留 hover 反馈。\n\t\tif(AnimValue > MENU_TAB_ANIM_EPSILON)\n\t\t{\n\t\t\tColorRGBA HoverColor = MenuCapsuleTabHoverColor();\n\t\t\tHoverColor.a *= AnimValue;\n\t\t\tDrawRoundedSurface(Ui(), *pRect, HoverColor, ColorRGBA(), ui_token::radius::PILL, 0.0f, Corners);\n\t\t}\n\t}\n\telse if(Checked)"), std::string::npos);
	EXPECT_NE(DoButtonMenuTab.find("TextRender()->TextColor(Checked ? MenuCapsuleTabActiveLabelColor() : MenuCapsuleTabInactiveLabelColor());"), std::string::npos);
	EXPECT_NE(DoButtonMenuTab.find("TextRender()->TextColor(PreviousLabelColor);"), std::string::npos);
	EXPECT_NE(ReadTextFile("src/game/client/components/menus.h").find("CUIElement *pTextUiElement = nullptr, float FontSize = -1.0f, bool CapsuleTab = false);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsSidebarKeepsPerTabFillInsteadOfCapsule)
{
	// 意图：设置页左栏是**竖排**页签，保持原来的分块选中/悬停底色，不做胶囊滑块；
	// 横向子 Tab 行才走胶囊（见 SettingsSubTabRowsUseCapsuleTabBar）。
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettings = FunctionBody(Source, "void CMenus::RenderSettings(CUIRect MainView)");
	ASSERT_FALSE(RenderSettings.empty());

	EXPECT_EQ(RenderSettings.find("ui_widget::CapsuleTabBarChrome(TabBarCtx, MakeUiScopeHash(\"settings_tabbar_capsule\")"), std::string::npos);
	EXPECT_EQ(RenderSettings.find("aSettingsTabSlots"), std::string::npos);
	EXPECT_NE(RenderSettings.find("const ColorRGBA SettingsNavigationSelected = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiSelectedColor)).WithAlpha(0.42f);"), std::string::npos);
	EXPECT_NE(RenderSettings.find("const ColorRGBA SettingsNavigationHover = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiSelectedColor)).WithAlpha(0.20f);"), std::string::npos);
	const size_t TabDrawPos = RenderSettings.find("if(DoButton_MenuTab(&m_aSettingsTabButtons[i], m_apSettingsTabs[i], Active, &Button, IGraphics::CORNER_ALL, &m_aAnimatorsSettingsTab[i], nullptr, &SettingsNavigationSelected, &SettingsNavigationHover, 10.0f, nullptr, &m_aSettingsTabLabelElements[i]))");
	ASSERT_NE(TabDrawPos, std::string::npos);
	// 页面切换仍要在卡片组装开始之前发生。
	EXPECT_LT(TabDrawPos, RenderSettings.find("m_SettingsCardDeck.BeginDisplayCycle("));
	// 旧 UI 仍走 CORNER_R 的贴边分支。
	EXPECT_NE(RenderSettings.find("if(DoButton_MenuTab(&m_aSettingsTabButtons[i], m_apSettingsTabs[i], Active, &Button, IGraphics::CORNER_R"), std::string::npos);
	// 设置页的胶囊配色零件仍供横向子 Tab 使用。
	EXPECT_NE(Source.find("ui_widget::SCapsuleTabBarStyle CMenus::SettingsCapsuleTabBarStyle() const"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsSubTabRowsUseCapsuleTabBar)
{
	// 意图：设置页各子 Tab 行（外观 / Assets / TClient / QmClient）统一走
	// 「槽位预布局 → 容器与滑块 → 页签文字」，旧 UI 分支保留原来的分段外观。
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string MenusHeader = ReadTextFile("src/game/client/components/menus.h");
	const std::string Settings = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Assets = ReadTextFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string TClient = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string QmClient = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	// 公共零件：绘制上下文与设置页胶囊配色。
	EXPECT_NE(MenusHeader.find("IUiContext TabBarUiContext() const;"), std::string::npos);
	EXPECT_NE(MenusHeader.find("ui_widget::SCapsuleTabBarStyle SettingsCapsuleTabBarStyle() const;"), std::string::npos);
	EXPECT_NE(MenusSource.find("IUiContext CMenus::TabBarUiContext() const"), std::string::npos);
	EXPECT_NE(MenusSource.find("ui_widget::SCapsuleTabBarStyle CMenus::SettingsCapsuleTabBarStyle() const"), std::string::npos);

	const std::string RenderAppearance = FunctionBody(Settings, "void CMenus::RenderSettingsAppearance(CUIRect MainView)");
	ASSERT_FALSE(RenderAppearance.empty());
	const size_t AppearanceGrid = RenderAppearance.find("AppearanceTabsRemainder.VSplitLeft(TabWidth, &aAppearanceTabSlots[Tab], &AppearanceTabsRemainder);");
	const size_t AppearanceChrome = RenderAppearance.find("ui_widget::CapsuleTabBarChrome(AppearanceTabBarCtx, MakeUiScopeHash(\"settings_appearance_tabs_capsule\")");
	const size_t AppearanceDraw = RenderAppearance.find("DoButton_MenuTab(&s_aPageTabs[Tab], s_apAppearanceTabNames[Tab], m_AppearanceSettingsTab == Tab, &aAppearanceTabSlots[Tab]");
	ASSERT_NE(AppearanceGrid, std::string::npos);
	ASSERT_NE(AppearanceChrome, std::string::npos);
	ASSERT_NE(AppearanceDraw, std::string::npos);
	EXPECT_LT(AppearanceGrid, AppearanceChrome);
	EXPECT_LT(AppearanceChrome, AppearanceDraw);
	EXPECT_NE(RenderAppearance.find("nullptr, nullptr, -1.0f, true))"), std::string::npos);
	EXPECT_NE(RenderAppearance.find("IGraphics::CORNER_L"), std::string::npos);

	const std::string RenderAssets = FunctionBody(Assets, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(RenderAssets.empty());
	const size_t AssetsGrid = RenderAssets.find("AssetsTabsRemainder.VSplitLeft(TabWidth, &aAssetsTabSlots[Tab], &AssetsTabsRemainder);");
	const size_t AssetsChrome = RenderAssets.find("ui_widget::CapsuleTabBarChrome(AssetsTabBarCtx, MakeUiScopeHash(\"settings_assets_tabs_capsule\")");
	const size_t AssetsDraw = RenderAssets.find("DoButton_MenuTab(&s_aPageTabs[Tab], s_apAssetsTabNames[Tab], s_CurCustomTab == Tab, &aAssetsTabSlots[Tab]");
	ASSERT_NE(AssetsGrid, std::string::npos);
	ASSERT_NE(AssetsChrome, std::string::npos);
	ASSERT_NE(AssetsDraw, std::string::npos);
	EXPECT_LT(AssetsGrid, AssetsChrome);
	EXPECT_LT(AssetsChrome, AssetsDraw);
	EXPECT_NE(RenderAssets.find("IGraphics::CORNER_L"), std::string::npos);

	const std::string RenderTClient = FunctionBody(TClient, "void CMenus::RenderSettingsTClient(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(RenderTClient.empty());
	const size_t TClientGrid = RenderTClient.find("TabsRemainder.VSplitLeft(TabWidth, &aTClientTabSlots[NumTClientTabs], &TabsRemainder);");
	const size_t TClientChrome = RenderTClient.find("ui_widget::CapsuleTabBarChrome(TClientTabBarCtx, MakeUiScopeHash(\"settings_tclient_tabs_capsule\")");
	const size_t TClientDraw = RenderTClient.find("DoButton_MenuTab(&s_aPageTabs[Tab], s_apTClientTabNames[Tab], ActiveTab == Tab, &aTClientTabSlots[TabIndex]");
	ASSERT_NE(TClientGrid, std::string::npos);
	ASSERT_NE(TClientChrome, std::string::npos);
	ASSERT_NE(TClientDraw, std::string::npos);
	EXPECT_LT(TClientGrid, TClientChrome);
	EXPECT_LT(TClientChrome, TClientDraw);
	// 旧 UI 仍按 CORNER_L/R/NONE 的分段外观逐段切分。
	EXPECT_NE(RenderTClient.find("ActiveTab == Tab, &Button, Corners"), std::string::npos);

	const std::string RenderQmClient = FunctionBody(QmClient, "void CMenus::RenderSettingsQmClientContent(CUIRect MainView, bool ContributorsPage, bool PrewarmOnly)");
	ASSERT_FALSE(RenderQmClient.empty());
	const size_t QmGrid = RenderQmClient.find("QmTabsRemainder.VSplitLeft(TabWidth, &aQmTabSlots[Tab], &QmTabsRemainder);");
	const size_t QmChrome = RenderQmClient.find("ui_widget::CapsuleTabBarChrome(QmTabBarCtx, MakeUiScopeHash(\"settings_qmclient_tabs_capsule\")");
	const size_t QmDraw = RenderQmClient.find("DoButton_MenuTab(&s_aPageTabs[Tab], apQmTabNames[Tab], m_QmClientSettingsTab == Tab, &aQmTabSlots[Tab]");
	ASSERT_NE(QmGrid, std::string::npos);
	ASSERT_NE(QmChrome, std::string::npos);
	ASSERT_NE(QmDraw, std::string::npos);
	EXPECT_LT(QmGrid, QmChrome);
	EXPECT_LT(QmChrome, QmDraw);
	// 页签计时段仍然覆盖两条分支。
	EXPECT_LT(QmDraw, RenderQmClient.find("LogQmPerfStage(Client(), \"tabbar\", StageTimer.ElapsedMs(), false, aTabExtra);"));

	// 玩家/Dummy 行与皮肤（Player/Dummy/Profiles）行同样先画胶囊再画文字。
	const std::string RenderPlayer = FunctionBody(Settings, "void CMenus::RenderSettingsPlayer(CUIRect MainView)");
	const std::string RenderTee = FunctionBody(Settings, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_FALSE(RenderPlayer.empty());
	ASSERT_FALSE(RenderTee.empty());
	const size_t PlayerChrome = RenderPlayer.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"settings_player_dummy_tabs_capsule\"), aPlayerTabSlots, std::size(aPlayerTabSlots), m_Dummy ? 1 : 0, SettingsCapsuleTabBarStyle());");
	const size_t PlayerDraw = RenderPlayer.find("if(DoButton_MenuTab(&s_PlayerTabButton, Localize(\"Player\"), !m_Dummy, &PlayerTab, IGraphics::CORNER_ALL");
	ASSERT_NE(PlayerChrome, std::string::npos);
	ASSERT_NE(PlayerDraw, std::string::npos);
	EXPECT_LT(PlayerChrome, PlayerDraw);
	EXPECT_NE(RenderPlayer.find("&PlayerTab, IGraphics::CORNER_L"), std::string::npos);
	const size_t TeeChrome = RenderTee.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"settings_tee_sub_tabs_capsule\"), aTeeTabSlots, std::size(aTeeTabSlots), ActiveTeeTab, SettingsCapsuleTabBarStyle());");
	const size_t TeeDraw = RenderTee.find("if(DoButton_MenuTab(&s_PlayerTabButton, pPlayerTabLabel, s_TeeSubTab == 0, &PlayerTab, IGraphics::CORNER_ALL");
	ASSERT_NE(TeeChrome, std::string::npos);
	ASSERT_NE(TeeDraw, std::string::npos);
	EXPECT_LT(TeeChrome, TeeDraw);
	EXPECT_NE(RenderTee.find("SeparateProfilesTab ? IGraphics::CORNER_R : IGraphics::CORNER_NONE"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ServerBrowserToolboxUsesCapsuleTabBar)
{
	// 意图：服务器浏览器工具箱页签（过滤器 / 信息 / 好友）在新 UI 下同样是胶囊，
	// 旧 UI 保留原来的分段底色。
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderServerbrowserTabBar(CUIRect TabBar)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiNavigation.h>"), std::string::npos);
	const size_t Chrome = Body.find("ui_widget::CapsuleTabBarChrome(ToolboxTabBarCtx, MakeUiScopeHash(\"browser_toolbox_tabs_capsule\")");
	const size_t Draw = Body.find("if(DoButton_MenuTab(&s_FilterTabButton, FONT_ICON_LIST_UL, g_Config.m_UiToolboxPage == UI_TOOLBOX_PAGE_FILTERS, &FilterTabButton, IGraphics::CORNER_ALL, &m_aAnimatorsSmallPage[SMALL_TAB_BROWSER_FILTER], nullptr, nullptr, nullptr, 10.0f, nullptr, nullptr, -1.0f, true))");
	ASSERT_NE(Chrome, std::string::npos);
	ASSERT_NE(Draw, std::string::npos);
	EXPECT_LT(Chrome, Draw);
	EXPECT_NE(Body.find("CapsuleTabBarStyleFor(BrowserPanelColor(1.0f))"), std::string::npos);
	EXPECT_NE(Body.find("const ColorRGBA ColorActive = UseNewUi ? BrowserPanelElevatedColor(0.92f) : ms_ColorTabbarActive;"), std::string::npos);

	// 通用配色零件：轨道压暗 + 滑块/文字自适应。
	EXPECT_NE(ReadTextFile("src/game/client/components/menus.h").find("ui_widget::SCapsuleTabBarStyle CapsuleTabBarStyleFor(const ColorRGBA &SurfaceColor) const;"), std::string::npos);
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	EXPECT_NE(MenusSource.find("ui_widget::SCapsuleTabBarStyle CMenus::CapsuleTabBarStyleFor(const ColorRGBA &SurfaceColor) const"), std::string::npos);
	EXPECT_NE(MenusSource.find("return CapsuleTabBarStyleFor(SettingsTabbarColor());"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ServerControlTabsUseCapsuleTabBarInNewUi)
{
	// 意图：游戏中"服务器控制"页的三个页签（改设置 / 踢人 / 移到观察者）在新 UI 下
	// 同样先画胶囊容器与滑块，再画页签文字；旧 UI 保留贴边的分段外观。
	const std::string Source = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderServerControl(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiNavigation.h>"), std::string::npos);
	const size_t Chrome = Body.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"ingame_server_control_tabs_capsule\"), aControlTabSlots, 3, ActiveControlTab, CapsuleTabBarStyleFor(ms_ColorTabbarActive));");
	const size_t Draw = Body.find("if(DoButton_MenuTab(&s_Button0, Localize(\"Change settings\"), s_ControlPage == EServerControlTab::SETTINGS, &aControlTabSlots[0], IGraphics::CORNER_ALL");
	ASSERT_NE(Chrome, std::string::npos);
	ASSERT_NE(Draw, std::string::npos);
	EXPECT_LT(Chrome, Draw);
	EXPECT_NE(Body.find("ControlTabsRemainder.VSplitLeft(ControlTabsRemainder.w / 3.0f, &aControlTabSlots[0], &ControlTabsRemainder);"), std::string::npos);
	EXPECT_NE(Body.find("ControlTabsRemainder.VSplitMid(&aControlTabSlots[1], &aControlTabSlots[2]);"), std::string::npos);
	EXPECT_NE(Body.find("&Button, IGraphics::CORNER_NONE"), std::string::npos);
}

TEST(QmNewUiMenuBranches, Tee7SubTabsUseCapsuleTabBar)
{
	// 意图：Tee7 皮肤的「玩家/Dummy」「Basic/Custom」「皮肤部位」三行子 Tab 同样
	// 先画胶囊容器与滑块、再画文字；旧 UI 保留贴边分段外观。
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings7.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsTee7Content(CUIRect MainView, const SSettingsContentMetrics &Metrics)");
	ASSERT_FALSE(Body.empty());

	// 三个锚点在整份源码里各只出现一次，直接按文件位置比较先后。
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiNavigation.h>"), std::string::npos);
	const size_t PlayerDummyChrome = Source.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"settings_tee7_player_dummy_tabs_capsule\"), aPlayerDummySlots, std::size(aPlayerDummySlots), m_Dummy ? 1 : 0, SettingsCapsuleTabBarStyle());");
	const size_t PlayerDummyDraw = Source.find("if(DoButton_MenuTab(&s_PlayerTabButton, Localize(\"Player\"), !m_Dummy, &LeftTab, IGraphics::CORNER_ALL");
	const size_t ModeChrome = Source.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"settings_tee7_mode_tabs_capsule\"), aModeTabSlots, std::size(aModeTabSlots), m_CustomSkinMenu ? 1 : 0, SettingsCapsuleTabBarStyle());");
	const size_t ModeDraw = Source.find("ClickedBasicTab = DoButton_MenuTab(&s_BasicTabButton");
	const size_t SkinPartChrome = Source.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"settings_tee7_skin_part_tabs_capsule\"), aSkinPartSlots, protocol7::NUM_SKINPARTS, ActiveSkinPart, SettingsCapsuleTabBarStyle());");
	const size_t SkinPartDraw = Source.find("if(DoButton_MenuTab(&s_aSkinPartButtons[i], Localize(CSkins7::ms_apSkinPartNamesLocalized[i], \"skins\"), m_TeePartSelected == i, &aSkinPartSlots[i], IGraphics::CORNER_ALL");
	ASSERT_NE(PlayerDummyChrome, std::string::npos);
	ASSERT_NE(PlayerDummyDraw, std::string::npos);
	ASSERT_NE(ModeChrome, std::string::npos);
	ASSERT_NE(ModeDraw, std::string::npos);
	ASSERT_NE(SkinPartChrome, std::string::npos);
	ASSERT_NE(SkinPartDraw, std::string::npos);
	EXPECT_LT(PlayerDummyChrome, PlayerDummyDraw);
	EXPECT_LT(ModeChrome, ModeDraw);
	EXPECT_LT(SkinPartChrome, SkinPartDraw);
	// 旧 UI 的贴边分段外观与圆角分支仍在。
	EXPECT_NE(Source.find("!m_Dummy, &LeftTab, IGraphics::CORNER_L"), std::string::npos);
	EXPECT_NE(Source.find("!m_CustomSkinMenu, &LeftTab, IGraphics::CORNER_L"), std::string::npos);
	EXPECT_NE(Source.find("Button, Corners, nullptr, nullptr, nullptr, nullptr, ui_token::radius::BASE"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsChoiceSegmentsUseCapsuleTabBar)
{
	// 意图：设置卡片里的分段选择行（图标颜色 / 图标粗细等共用 DoIconChoiceRow）
	// 在新 UI 下同样是胶囊滑块；同一函数里多行选择器必须各自带行标识，不能共用轨道。
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const size_t LambdaPos = Source.find("const auto DoIconChoiceRow = [this, BodySize](CUIRect Row");
	ASSERT_NE(LambdaPos, std::string::npos);
	const size_t ChromePos = Source.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), SegmentGroup, aSegmentSlots, SegmentCount, Current, SettingsCapsuleTabBarStyle());", LambdaPos);
	const size_t DrawPos = Source.find("if(DoButton_MenuTab(&pButtons[i], ppLabels[i], Current == i, &aSegmentSlots[i], IGraphics::CORNER_ALL", LambdaPos);
	ASSERT_NE(ChromePos, std::string::npos);
	ASSERT_NE(DrawPos, std::string::npos);
	EXPECT_LT(ChromePos, DrawPos);
	EXPECT_NE(Source.find("const uint64_t SegmentGroup = BuildUiAnimNodeKey(MakeUiScopeHash(\"settings_choice_row_capsule\"), reinterpret_cast<uint64_t>(pButtons));"), std::string::npos);
	EXPECT_LT(Source.find("const uint64_t SegmentGroup = BuildUiAnimNodeKey"), ChromePos);
	EXPECT_LT(Source.find("SegmentsRemainder.VSplitLeft(SegmentsRemainder.w / (SegmentCount - i), &aSegmentSlots[i], &SegmentsRemainder);"), ChromePos);
	// 旧 UI 的贴边分段外观仍在。
	EXPECT_NE(Source.find("const int Corners = i == 0 ? IGraphics::CORNER_L : (i == Count - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);"), std::string::npos);
	// 预布局会被任意位置的鼠标按下触发，只处理命中与配置更新，不能重复绘制半透明背景。
	const std::string ProcessChoice = FunctionBody(Source, "const auto ProcessChoiceRow = [this, &Changed](CUIRect Row");
	ASSERT_FALSE(ProcessChoice.empty());
	EXPECT_EQ(ProcessChoice.find("CapsuleTabBarChrome("), std::string::npos);
	EXPECT_EQ(ProcessChoice.find("DoButton_MenuTab("), std::string::npos);
	EXPECT_NE(ProcessChoice.find("SegmentsRemainder.VSplitLeft(SegmentsRemainder.w / (SegmentCount - i), &aSegmentSlots[i], &SegmentsRemainder);"), std::string::npos);
	EXPECT_NE(ProcessChoice.find("if(Ui()->DoButtonLogic(&pButtons[i], Current == i, &aSegmentSlots[i], BUTTONFLAG_LEFT))"), std::string::npos);
	EXPECT_NE(ProcessChoice.find("if(Ui()->DoButtonLogic(&pButtons[i], Current == i, &Segment, BUTTONFLAG_LEFT))"), std::string::npos);
	EXPECT_NE(ProcessChoice.find("OnChanged(i);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, IngameGameButtonBarRoundsAllCornersOnlyInNewUi)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const std::string RenderGame = FunctionBody(Source, "void CMenus::RenderGame(CUIRect MainView)");

	EXPECT_NE(RenderGame.find("const int ButtonBarsCorners = g_Config.m_QmNewUi != 0 ? IGraphics::CORNER_ALL : IGraphics::CORNER_B;"), std::string::npos);
	EXPECT_NE(RenderGame.find("ButtonBars.Draw(ms_ColorTabbarActive, ButtonBarsCorners, 10.0f);"), std::string::npos);
	EXPECT_EQ(RenderGame.find("ButtonBars.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);"), std::string::npos);
}

TEST(QmCameraEffects, DynamicFovRemovalKeepsBaseZoomStable)
{
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomWithoutDynamicFov(2.0f, 1.25f), 1.6f);
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomWithoutDynamicFov(1.6f, 1.0f), 1.6f);
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomWithoutDynamicFov(1.6f, 0.0f), 1.6f);
}

TEST(QmCameraEffects, CinematicFreeviewSmoothingIsFrameRateIndependent)
{
	const vec2 Start(10.0f, 20.0f);
	const vec2 Target(30.0f, 60.0f);
	vec2 At30Fps = Start;
	vec2 At60Fps = Start;
	for(int Frame = 0; Frame < 30; ++Frame)
		At30Fps = QmCameraEffects::SmoothCinematicPosition(At30Fps, Target, 1.0f / 30.0f);
	for(int Frame = 0; Frame < 60; ++Frame)
		At60Fps = QmCameraEffects::SmoothCinematicPosition(At60Fps, Target, 1.0f / 60.0f);

	EXPECT_FLOAT_EQ(QmCameraEffects::SmoothCinematicPosition(Start, Target, 0.0f).x, Start.x);
	EXPECT_NEAR(At30Fps.x, At60Fps.x, 0.0001f);
	EXPECT_NEAR(At30Fps.y, At60Fps.y, 0.0001f);
	EXPECT_GT(At30Fps.x, Start.x);
	EXPECT_LT(At30Fps.x, Target.x);
}

TEST(QmCameraEffectsSource, CinematicCameraAndDynamicFovKeepScopedState)
{
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Header = ReadTextFile("src/game/client/components/camera.h");
	const std::string Source = ReadTextFile("src/game/client/components/camera.cpp");
	const std::string OnRender = FunctionBody(Source, "void CCamera::OnRender()");
	const std::string ScaleZoom = FunctionBody(Source, "void CCamera::ScaleZoom(");
	const std::string ChangeZoom = FunctionBody(Source, "void CCamera::ChangeZoom(");
	const std::string UpdateCamera = FunctionBody(Source, "void CCamera::UpdateCamera()");
	const std::string OnReset = FunctionBody(Source, "void CCamera::OnReset()");
	const std::string GameClient = ReadTextFile("src/game/client/gameclient.cpp");

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmCinematicCamera, qm_cinematic_camera"), std::string::npos);
	EXPECT_NE(Header.find("m_CinematicCameraSmoothing"), std::string::npos);
	EXPECT_NE(OnRender.find("GameClient()->m_Snap.m_SpecInfo.m_Active && !GameClient()->m_Snap.m_SpecInfo.m_UsePosition"), std::string::npos);
	EXPECT_NE(OnRender.find("if(g_Config.m_QmCinematicCamera)"), std::string::npos);
	EXPECT_NE(OnRender.find("m_CinematicCameraSmoothing = false;"), std::string::npos);
	EXPECT_NE(ScaleZoom.find("RemoveDynamicFovZoom();"), std::string::npos);
	EXPECT_NE(ChangeZoom.find("RemoveDynamicFovZoom();"), std::string::npos);
	// 反向按键立即生效已移除：步进基准回到上游「缩放中沿用旧目标」的行为
	EXPECT_NE(ScaleZoom.find("float CurrentTarget = m_Zooming ? m_ZoomSmoothingTarget : m_Zoom;"), std::string::npos);
	EXPECT_NE(UpdateCamera.find("RemoveDynamicFovZoom();"), std::string::npos);
	EXPECT_NE(OnReset.find("m_DynamicFovAppliedFactor = 1.0f;"), std::string::npos);
	EXPECT_EQ(UpdateCamera.find("m_aDyncamCurrentCameraOffset[g_Config.m_ClDummy] += m_DriftCurrentOffset;"), std::string::npos);
	EXPECT_NE(OnRender.find("m_aDyncamCurrentCameraOffset[g_Config.m_ClDummy] + m_DriftCurrentOffset"), std::string::npos);
	EXPECT_NE(Header.find("float BaseZoom() const"), std::string::npos);
	EXPECT_NE(GameClient.find("m_Camera.BaseZoom()"), std::string::npos);
	EXPECT_EQ(GameClient.find("float ShowDistanceZoom = m_Camera.m_Zoom;"), std::string::npos);
}

TEST(QmCameraEffectsSource, CameraViewCardHeightTracksRowCount)
{
	const std::string QmMenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string CameraView = FunctionBody(QmMenusSource, "void CMenus::RenderQmVisualCameraViewContent(");
	// 卡片高度测量已搬到视觉卡片目录（MeasureVisualCardHeight）。
	const std::string VisualCatalog = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogVisual.cpp");
	const std::string VisualMeasure = FunctionBody(VisualCatalog, "float MeasureVisualCardHeight(");
	ASSERT_FALSE(VisualMeasure.empty());

	// 「反向缩放立即生效」已连同 qm_zoom_instant_reverse 一起移除，渲染与预布局输入不得再残留该行
	EXPECT_EQ(CameraView.find("m_QmZoomInstantReverse"), std::string::npos);
	EXPECT_EQ(QmMenusSource.find("m_QmZoomInstantReverse"), std::string::npos);
	EXPECT_EQ(VisualCatalog.find("m_QmZoomInstantReverse"), std::string::npos);
	// 基础行数去掉该行后为 5；行数与渲染行数不一致会让卡片高度错位
	EXPECT_NE(VisualMeasure.find("return Rows(5.0f + (g_Config.m_QmCameraDrift ? 3.0f : 0.0f) + (g_Config.m_QmDynamicFov ? 2.0f : 0.0f) + (g_Config.m_QmAspectPreset == 6 ? 1.0f : 0.0f)) + Metrics.m_BodySize;"), std::string::npos);
}

TEST(QmStoragePath, BuildsCandidatesRelativeToExecutable)
{
	char aPath[IO_MAX_PATH_LENGTH];
	EXPECT_TRUE(StoragePathFromExecutable("C:\\QmClient\\DDNet.exe", "data/mapres", aPath, sizeof(aPath)));
	EXPECT_STREQ(aPath, "C:\\QmClient/data/mapres");
	EXPECT_TRUE(StoragePathFromExecutable("/opt/qmclient/DDNet", "storage.cfg", aPath, sizeof(aPath)));
	EXPECT_STREQ(aPath, "/opt/qmclient/storage.cfg");
	EXPECT_FALSE(StoragePathFromExecutable("DDNet.exe", "data", aPath, sizeof(aPath)));
}

TEST(QmStoragePathSource, ExecutableCandidatesPrecedeCurrentDirectoryFallbacks)
{
	const std::string Source = ReadTextFile("src/engine/shared/storage.cpp");
	const std::string LoadPaths = FunctionBody(Source, "bool LoadPathsFromFile(");
	const std::string FindData = FunctionBody(Source, "void FindDataDirectory(");

	const size_t ExecutableStorage = LoadPaths.find("StoragePathFromExecutable");
	const size_t CurrentDirectoryStorage = LoadPaths.find("io_open(\"storage.cfg\"");
	const size_t ExecutableData = FindData.find("StoragePathFromExecutable");
	const size_t CurrentDirectoryData = FindData.find("fs_is_dir(\"data/mapres\"");
	ASSERT_NE(ExecutableStorage, std::string::npos);
	ASSERT_NE(CurrentDirectoryStorage, std::string::npos);
	ASSERT_NE(ExecutableData, std::string::npos);
	ASSERT_NE(CurrentDirectoryData, std::string::npos);
	EXPECT_LT(ExecutableStorage, CurrentDirectoryStorage);
	EXPECT_LT(ExecutableData, CurrentDirectoryData);
}

TEST(QmLocalization, ContextRequiresOpeningAndClosingBrackets)
{
	EXPECT_TRUE(LocalizationIsContextLine("[menu]"));
	EXPECT_FALSE(LocalizationIsContextLine("[%s] %s (Map: %s, Time: %s)"));
	EXPECT_FALSE(LocalizationIsContextLine("[broken"));
	EXPECT_FALSE(LocalizationIsContextLine("plain"));
}

TEST(QmLocalizationSource, LocalizedDropdownNamesAreNotStaticHeapPointers)
{
	const std::string Chat = ReadTextFile("src/game/client/components/chat.cpp");
	const std::string Menus = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Localization = ReadTextFile("src/game/localization.cpp");

	EXPECT_EQ(Chat.find("static const char *s_apBackendNames[] = {Localize"), std::string::npos);
	EXPECT_EQ(Menus.find("static std::vector<const char *> s_LlmProviderDropDownNames ="), std::string::npos);
	EXPECT_EQ(Menus.find("static const char *s_apSourceNames[] ="), std::string::npos);
	EXPECT_EQ(Menus.find("static const char *s_apOutgoingModeNames[] ="), std::string::npos);
	EXPECT_NE(Localization.find("if(LocalizationIsContextLine(pLine))"), std::string::npos);
	EXPECT_NE(Localization.find("Couldn't open language file '%s'"), std::string::npos);
}

TEST(QmUiScaleSource, ScaleChangesResetContainersAndInvalidateScaleKeys)
{
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string TClientMenusSource = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string QmMenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Update = FunctionBody(UiSource, "void CUi::Update(");
	const std::string Screen = FunctionBody(UiSource, "const CUIRect *CUi::Screen()");
	const std::string QmUiScaleHelper = FunctionBody(QmMenusSource, "void CMenus::RenderQmSettingsSliderWithValueInput(");
	const std::string CameraView = FunctionBody(QmMenusSource, "void CMenus::RenderQmVisualCameraViewContent(");

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmUiScale, qm_ui_scale, 100, 50, 200"), std::string::npos);
	EXPECT_NE(Update.find("Client()->OnWindowResize();"), std::string::npos);
	EXPECT_NE(Screen.find("QmUiVirtualScreenHeight(g_Config.m_QmUiScale)"), std::string::npos);
	EXPECT_NE(MenusSource.find("StyleKey.m_UiScaleBucket = std::clamp(g_Config.m_QmUiScale, 50, 200);"), std::string::npos);
	EXPECT_NE(TClientMenusSource.find("std::clamp(g_Config.m_QmUiScale, 50, 200)"), std::string::npos);
	EXPECT_NE(QmUiScaleHelper.find("Options.m_Flags = Flags;"), std::string::npos);
	EXPECT_NE(QmUiScaleHelper.find("Options.m_CommitPolicy = (Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE) != 0 ?"), std::string::npos);
	EXPECT_NE(CameraView.find("RenderValue(\"qmclient-ui-scale\", \"UI scale\", &s_QmUiScaleInputId, &g_Config.m_QmUiScale, 50, 200, \"%\", CUi::SCROLLBAR_OPTION_DELAYUPDATE);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientPreLayoutUsesDeckContentCoordinates)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string PreLayout = FunctionBody(Source, "const auto BuildTClientConditionalRowsPreLayoutInput =");

	EXPECT_EQ(PreLayout.find("StartRows"), std::string::npos);
	EXPECT_NE(PreLayout.find("CTClientSettingsRowAllocator Rows(Content)"), std::string::npos);
	EXPECT_NE(PreLayout.find("s_vTinyTeeModeButtons"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsInputFieldsReserveTrailingActionsAndKeepQueueUnitsInline)
{
	const std::string FormsSource = ReadTextFile("src/game/client/QmUi/UiForms.cpp");
	const std::string ThemeSource = ReadTextFile("src/game/client/QmUi/UiTheme.h");
	const std::string Forms = FunctionBody(FormsSource, "SInputFieldResult InputField(");
	EXPECT_NE(Forms.find("CUIRect InputHitRect = Layout.m_ShellRect;"), std::string::npos);
	EXPECT_NE(Forms.find("RenderOptions.m_pHitRect = &InputHitRect;"), std::string::npos);
	EXPECT_NE(ThemeSource.find("ColorHSLA(g_Config.m_QmUiColor), g_Config.m_QmUiOpacity / 100.0f"), std::string::npos);

	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string SkinRenderList = FunctionBody(SettingsSource, "const auto RenderList =");
	EXPECT_NE(SkinRenderList.find("QueueIntervalOptions.m_pSuffix = \"ms\";"), std::string::npos);
	EXPECT_EQ(SkinRenderList.find("Ui()->DoLabel(&IntervalUnit"), std::string::npos);
}

TEST(QmUiScale, VirtualHeightUsesClampedPercentage)
{
	EXPECT_FLOAT_EQ(QmUiVirtualScreenHeight(50), 1200.0f);
	EXPECT_FLOAT_EQ(QmUiVirtualScreenHeight(100), 600.0f);
	EXPECT_FLOAT_EQ(QmUiVirtualScreenHeight(200), 300.0f);
	EXPECT_FLOAT_EQ(QmUiVirtualScreenHeight(0), 1200.0f);
	EXPECT_FLOAT_EQ(QmUiVirtualScreenHeight(300), 300.0f);
}

TEST(QmUiScale, CenteredPopupMarginKeepsUsableContentAtTwoHundredPercent)
{
	const CUIRect DefaultScreen = {0.0f, 0.0f, 1066.0f, 600.0f};
	const CUIRect ScaledScreen = {0.0f, 0.0f, 533.0f, 300.0f};
	const CUIRect NarrowScaledScreen = {0.0f, 0.0f, 375.0f, 300.0f};

	EXPECT_FLOAT_EQ(QmUiCenteredMargin(DefaultScreen, 150.0f, 300.0f, 180.0f), 150.0f);
	EXPECT_FLOAT_EQ(QmUiCenteredMargin(ScaledScreen, 150.0f, 300.0f, 180.0f), 60.0f);
	EXPECT_FLOAT_EQ(QmUiCenteredMargin(NarrowScaledScreen, 150.0f, 300.0f, 180.0f), 37.5f);
	EXPECT_FLOAT_EQ(QmUiCenteredMargin({0.0f, 0.0f, 200.0f, 120.0f}, 150.0f, 300.0f, 180.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmUiCenteredMargin(ScaledScreen, 150.0f, 300.0f, 300.0f), 0.0f);
	EXPECT_EQ(QmUiVisibleRows(52.0f, 20.0f, 20.0f, 4, 4), 1);
	EXPECT_EQ(QmUiVisibleRows(126.0f, 20.0f, 20.0f, 8, 4), 4);
	EXPECT_EQ(QmUiVisibleRows(19.0f, 20.0f, 20.0f, 4, 4), 0);
}

TEST(QmUiScaleSource, TouchMenusRespectCallerProvidedScaledHeight)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_ingame_touch_controls.cpp");
	EXPECT_EQ(Source.find("MainView.h = 600.0f - 40.0f - MainView.y;"), std::string::npos);
	EXPECT_NE(Source.find("void CMenusIngameTouchControls::RenderTouchButtonEditor(CUIRect MainView)"), std::string::npos);
	EXPECT_NE(Source.find("void CMenusIngameTouchControls::RenderTouchButtonBrowser(CUIRect MainView)"), std::string::npos);
	EXPECT_NE(Source.find("void CMenusIngameTouchControls::RenderPreviewSettings(CUIRect MainView)"), std::string::npos);
}

TEST(QmUiScaleSource, BlockingPopupsAndDemoRowsFitScaledScreen)
{
	const std::string Menus = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string DemoMenus = ReadTextFile("src/game/client/components/menus_demo.cpp");
	EXPECT_NE(Menus.find("QmUiCenteredMargin(Box, 150.0f, 300.0f, 300.0f)"), std::string::npos);
	EXPECT_NE(Menus.find("QmUiCenteredMargin(Screen, 150.0f, 300.0f, 300.0f)"), std::string::npos);
	EXPECT_NE(DemoMenus.find("QmUiVisibleRows(SegmentsArea.h"), std::string::npos);
	EXPECT_NE(DemoMenus.find("VerticalExpansion = std::min(60.0f, PopupMargin)"), std::string::npos);
}

TEST(QmDemoCutRender, UsesExportedCutAsRenderSource)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_demo.cpp");
	const std::string SlicePopup = FunctionBody(Source, "void CMenus::RenderDemoPlayerSliceSavePopup(CUIRect MainView)");

	EXPECT_NE(SlicePopup.find("str_format(m_aPendingDemoRenderSelectionName, sizeof(m_aPendingDemoRenderSelectionName), \"%s.demo\", m_DemoSliceInput.GetString());"), std::string::npos);
	EXPECT_EQ(SlicePopup.find("str_copy(m_aPendingDemoRenderSelectionName, m_aCurrentDemoSelectionName"), std::string::npos);
}

TEST(QmDemoUi, CompactPlayerFitsScaledScreensAndKeepsControlsInside)
{
	for(const CUIRect Screen : {CUIRect{0.0f, 0.0f, 1066.0f, 600.0f}, CUIRect{20.0f, 30.0f, 375.0f, 300.0f}})
	{
		for(bool Expanded : {false, true})
		{
			const CUIRect Panel = qm_demo_ui::PlayerRect(Screen, Expanded);
			EXPECT_GE(Panel.x, Screen.x);
			EXPECT_GE(Panel.y, Screen.y);
			EXPECT_LE(Panel.x + Panel.w, Screen.x + Screen.w);
			EXPECT_LE(Panel.y + Panel.h, Screen.y + Screen.h);
			EXPECT_LE(Panel.w, 640.0f);
			EXPECT_LE(qm_demo_ui::TransportWidth(qm_demo_ui::TransportButtonSize(Panel.w)), Panel.w - 16.0f);
			if(!Expanded)
				EXPECT_LE(Panel.h, 120.0f);
		}
	}
}

TEST(QmDemoUi, ExportContentGrowsOnlyForVisibleOptionsAndSegments)
{
	const float Base = qm_demo_ui::SliceContentHeight(0, false);
	EXPECT_GT(qm_demo_ui::SliceContentHeight(0, true), Base);
	EXPECT_GT(qm_demo_ui::SliceContentHeight(1, false), Base);
	EXPECT_FLOAT_EQ(qm_demo_ui::SliceContentHeight(4, false), qm_demo_ui::SliceContentHeight(100, false));
	const CUIRect Screen{20.0f, 30.0f, 375.0f, 300.0f};
	const CUIRect Popup = qm_demo_ui::PopupRect(Screen, qm_demo_ui::SliceContentHeight(4, true) + 86.0f);
	EXPECT_GE(Popup.x, Screen.x);
	EXPECT_GE(Popup.y, Screen.y);
	EXPECT_LE(Popup.x + Popup.w, Screen.x + Screen.w);
	EXPECT_LE(Popup.y + Popup.h, Screen.y + Screen.h);
	EXPECT_GT(qm_demo_ui::SliceContentHeight(4, true), Popup.h - 86.0f);
}

TEST(QmDemoUi, ExportBackdropIsOutsidePlayerBlurSuppression)
{
	const std::string DemoSource = ReadTextFile("src/game/client/components/menus_demo.cpp");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Player = FunctionBody(DemoSource, "void CMenus::RenderDemoPlayer(");
	EXPECT_EQ(Player.find("RenderDemoPlayerSliceSavePopup("), std::string::npos);
	EXPECT_NE(MenusSource.find("RenderDemoPlayerSliceSavePopup(Screen);"), std::string::npos);
	const std::string Popup = FunctionBody(DemoSource, "void CMenus::RenderDemoPlayerSliceSavePopup(");
	const size_t Backdrop = Popup.find("RenderDemoCard(Box);");
	const size_t Suppression = Popup.find("CUiScopedGaussianBlurSuppression");
	ASSERT_NE(Backdrop, std::string::npos);
	ASSERT_NE(Suppression, std::string::npos);
	EXPECT_LT(Backdrop, Suppression);
}

TEST(QmDemoDisplay, PreviewAndVideoUseTheSameProfile)
{
	CConfig Config{};
	Config.m_ClShowDirection = 3;
	Config.m_ClVideoShowDirection = 0;
	Config.m_ClNamePlatesStrong = 0;
	Config.m_QmDemoShowDirection = 2;
	Config.m_QmDemoShowStrongWeak = 2;
	Config.m_QmDemoStrongWeakScope = 4;
	Config.m_QmDemoShowHud = 1;
	Config.m_QmDemoShowChat = 0;
	for(bool Rendering : {false, true})
	{
		const auto Display = qm_demo_display::Resolve(Config, true, Rendering);
		EXPECT_EQ(Display.m_Direction, 2);
		EXPECT_EQ(Display.m_StrongWeak, 2);
		EXPECT_EQ(Display.m_StrongWeakScope, 4);
		EXPECT_TRUE(Display.m_Hud);
		EXPECT_FALSE(Display.m_Chat);
	}
}

TEST(QmDemoDisplay, ChangingDemoOptionsDoesNotChangeGameplay)
{
	CConfig Config{};
	Config.m_ClShowDirection = 3;
	Config.m_ClNamePlatesStrong = 1;
	Config.m_QmNameplateHookStrongWeakScope = 2;
	Config.m_ClShowhud = 1;
	Config.m_ClShowChat = 1;
	for(int Direction = 0; Direction <= 3; ++Direction)
	{
		Config.m_QmDemoShowDirection = Direction;
		for(int StrongWeak = 0; StrongWeak <= 2; ++StrongWeak)
		{
			Config.m_QmDemoShowStrongWeak = StrongWeak;
			const auto Demo = qm_demo_display::Resolve(Config, true, false);
			EXPECT_EQ(Demo.m_Direction, Direction);
			EXPECT_EQ(Demo.m_StrongWeak, StrongWeak);
			const auto Game = qm_demo_display::Resolve(Config, false, false);
			EXPECT_EQ(Game.m_Direction, 3);
			EXPECT_EQ(Game.m_StrongWeak, 1);
			EXPECT_EQ(Game.m_StrongWeakScope, 2);
			EXPECT_TRUE(Game.m_Hud);
			EXPECT_TRUE(Game.m_Chat);
		}
	}
}

TEST(QmDemoCut, RequiresASelectionAndResolvesOpenEnds)
{
	SDemoSliceSegment Range;
	EXPECT_FALSE(qm_demo_cut::ResolveRange(-1, -1, 100, 500, Range));
	ASSERT_TRUE(qm_demo_cut::ResolveRange(175, -1, 100, 500, Range));
	EXPECT_EQ(Range.m_StartTick, 175);
	EXPECT_EQ(Range.m_EndTick, 500);
	ASSERT_TRUE(qm_demo_cut::ResolveRange(-1, 325, 100, 500, Range));
	EXPECT_EQ(Range.m_StartTick, 100);
	EXPECT_EQ(Range.m_EndTick, 325);
}

TEST(QmDemoCut, RejectsEmptyReversedAndOutOfBoundsCuts)
{
	SDemoSliceSegment Range;
	EXPECT_FALSE(qm_demo_cut::ResolveRange(200, 200, 100, 500, Range));
	EXPECT_FALSE(qm_demo_cut::ResolveRange(300, 200, 100, 500, Range));
	EXPECT_FALSE(qm_demo_cut::ResolveRange(600, -1, 100, 500, Range));
	EXPECT_FALSE(qm_demo_cut::ResolveRange(-1, 50, 100, 500, Range));
	EXPECT_FALSE(qm_demo_cut::ResolveRange(100, 200, 500, 100, Range));
	EXPECT_FALSE(qm_demo_cut::ResolveRange(100, 200, 100, 100, Range));
	ASSERT_TRUE(qm_demo_cut::ResolveRange(50, 550, 100, 500, Range));
	EXPECT_EQ(Range.m_StartTick, 100);
	EXPECT_EQ(Range.m_EndTick, 500);
}

TEST(QmDemoCut, TimeDisplayRetainsSubsecondTicks)
{
	EXPECT_EQ(qm_demo_cut::ToCentiseconds(1, 50), 2);
	EXPECT_EQ(qm_demo_cut::ToCentiseconds(25, 50), 50);
	EXPECT_EQ(qm_demo_cut::ToCentiseconds(51, 50), 102);
	EXPECT_EQ(qm_demo_cut::ToCentiseconds(180001, 50), 360002);
	EXPECT_EQ(qm_demo_cut::ToCentiseconds(2000000000, 50), 4000000000LL);
}

TEST(QmDemoCut, PreviewStopsAtTheEndIncludingSkippedFrames)
{
	qm_demo_cut::CPreview Preview;
	EXPECT_FALSE(Preview.IsActive());
	ASSERT_TRUE(Preview.Start({100, 200}));
	EXPECT_FALSE(Preview.Update(199));
	EXPECT_TRUE(Preview.IsActive());
	EXPECT_TRUE(Preview.Update(200));
	EXPECT_FALSE(Preview.IsActive());
	EXPECT_TRUE(Preview.IsFinished());
	EXPECT_FALSE(Preview.Update(201));
	ASSERT_TRUE(Preview.Start({100, 200}));
	EXPECT_TRUE(Preview.Update(203));
	EXPECT_FALSE(Preview.IsActive());
}

TEST(QmDemoCut, PreviewCanBeCancelledAndCannotStartAnInvalidCut)
{
	qm_demo_cut::CPreview Preview;
	ASSERT_TRUE(Preview.Start({100, 200}));
	Preview.Reset();
	EXPECT_FALSE(Preview.IsActive());
	EXPECT_FALSE(Preview.IsFinished());
	EXPECT_FALSE(Preview.Update(250));
	EXPECT_FALSE(Preview.Start({200, 100}));
	EXPECT_FALSE(Preview.Start({200, 200}));
	EXPECT_FALSE(Preview.Start({-1, 200}));
	EXPECT_FALSE(Preview.IsActive());
}

TEST(QmNewUiMenuBranches, BrowserUsesExplicitQmNewUiShellBranch)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string RenderServerbrowser = FunctionBody(Source, "void CMenus::RenderServerbrowser(");
	const size_t TopUseNewUiIfPos = RenderServerbrowser.find("if(UseNewUi)\n\t\tView.Margin(6.0f, &View);");
	ASSERT_NE(TopUseNewUiIfPos, std::string::npos);
	const size_t TopOldUiElsePos = RenderServerbrowser.find("else\n\t{", TopUseNewUiIfPos);
	ASSERT_NE(TopOldUiElsePos, std::string::npos);
	const size_t TopOldUiBodyStart = RenderServerbrowser.find("{", TopOldUiElsePos);
	ASSERT_NE(TopOldUiBodyStart, std::string::npos);
	const size_t TopOldUiBodyEnd = MatchingBrace(RenderServerbrowser, TopOldUiBodyStart);
	ASSERT_NE(TopOldUiBodyEnd, std::string::npos);
	const std::string TopOldUiBlock = RenderServerbrowser.substr(TopOldUiBodyStart, TopOldUiBodyEnd - TopOldUiBodyStart);

	const size_t UseNewUiIfPos = RenderServerbrowser.find("if(UseNewUi)", TopOldUiBodyEnd);
	ASSERT_NE(UseNewUiIfPos, std::string::npos);
	const size_t UseNewUiBodyStart = RenderServerbrowser.find("{", UseNewUiIfPos);
	ASSERT_NE(UseNewUiBodyStart, std::string::npos);
	const size_t UseNewUiBodyEnd = MatchingBrace(RenderServerbrowser, UseNewUiBodyStart);
	ASSERT_NE(UseNewUiBodyEnd, std::string::npos);
	const size_t OldUiElsePos = RenderServerbrowser.find("else", UseNewUiBodyEnd);
	ASSERT_NE(OldUiElsePos, std::string::npos);
	const size_t OldUiBodyStart = RenderServerbrowser.find("{", OldUiElsePos);
	ASSERT_NE(OldUiBodyStart, std::string::npos);
	const size_t OldUiBodyEnd = MatchingBrace(RenderServerbrowser, OldUiBodyStart);
	ASSERT_NE(OldUiBodyEnd, std::string::npos);
	const std::string OldUiBlock = RenderServerbrowser.substr(OldUiBodyStart, OldUiBodyEnd - OldUiBodyStart);

	EXPECT_NE(Source.find("const bool UseNewUi = g_Config.m_QmNewUi != 0;"), std::string::npos);
	EXPECT_NE(Source.find("if(UseNewUi)"), std::string::npos);
	EXPECT_NE(Source.find("ServerListBase.Draw(BrowserPanelColor()"), std::string::npos);
	EXPECT_NE(Source.find("(void)DrawBackground;"), std::string::npos);
	EXPECT_NE(Source.find("const float ToolBoxWidth = UseNewUi ? 205.0f : 188.0f;"), std::string::npos);
	EXPECT_NE(Source.find("const float ColumnGap = UseNewUi ? 10.0f : 6.0f;"), std::string::npos);
	EXPECT_NE(Source.find("const float StatusHeight = UseNewUi ? 84.0f : 76.0f;"), std::string::npos);
	EXPECT_NE(Source.find("CUIRect ServerListStackBase = ServerListBase;"), std::string::npos);
	EXPECT_NE(Source.find("ServerListStackBase.HSplitBottom(StatusHeight, &ServerListBase, &StatusBox);"), std::string::npos);
	EXPECT_NE(Source.find("StatusBox.y = ServerListStackBase.y + ServerListStackBase.h - StatusHeight;"), std::string::npos);
	EXPECT_NE(Source.find("ServerListBase.h = maximum(StatusBox.y - ColumnGap - ServerListBase.y, 0.0f);"), std::string::npos);
	EXPECT_EQ(Source.find("ServerListBase.HSplitBottom(ColumnGap, &ServerListBase, nullptr);"), std::string::npos);
	EXPECT_NE(Source.find("ServerListBase.Margin(std::clamp(ServerListBase.w * 0.006f, 1.0f, 4.0f), &ServerListBase);"), std::string::npos);
	// 服务器列表只保留外层卡片框：New UI 分支不再额外内缩。
	EXPECT_NE(Source.find("ServerListBase.Margin(2.0f, &ServerListBase);"), std::string::npos);
	EXPECT_EQ(Source.find("ServerListBase.Margin(10.0f, &ServerListBase);"), std::string::npos);
	EXPECT_NE(TopOldUiBlock.find("View.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);"), std::string::npos);
	EXPECT_NE(TopOldUiBlock.find("View.Margin(10.0f, &View);"), std::string::npos);
	EXPECT_EQ(TopOldUiBlock.find("View.Margin(std::clamp(View.w * 0.008f, 4.0f, 8.0f), &View);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, BrowserServerListKeepsSinglePanelFrame)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string RenderServerbrowser = FunctionBody(Source, "void CMenus::RenderServerbrowser(");
	ASSERT_FALSE(RenderServerbrowser.empty());

	// 列表区块只画一次卡片背景，避免同色半透明叠加出「框中框」的深色内块。
	size_t PanelDrawCount = 0;
	for(size_t Pos = RenderServerbrowser.find("ServerListBase.Draw("); Pos != std::string::npos; Pos = RenderServerbrowser.find("ServerListBase.Draw(", Pos + 1))
		PanelDrawCount++;
	EXPECT_EQ(PanelDrawCount, 1);
	EXPECT_NE(RenderServerbrowser.find("ServerListBase.Draw(BrowserPanelColor(), IGraphics::CORNER_ALL, ui_token::radius::CARD);"), std::string::npos);

	// 表头与列表的内层底色由 RenderServerbrowserServerList 负责，仍是列表自身的层次，
	// 不再有第二张卡片。
	EXPECT_EQ(RenderServerbrowser.find("ServerListBase.Draw(BrowserPanelElevatedColor()"), std::string::npos);
}

TEST(QmNewUiMenuBranches, BrowserServerListUsesOneSurfaceColor)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string ListBody = FunctionBody(Source, "void CMenus::RenderServerbrowserServerList(");
	ASSERT_FALSE(ListBody.empty());

	// 表头与列表正文不再各自铺一层半透明表面：同一张卡片里出现两种底色、两种圆角
	// （表头只圆上边、正文无圆角）就是「框里还有一层」的来源。
	EXPECT_EQ(ListBody.find("Headers.Draw(BrowserOpacityColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f))"), std::string::npos);
	EXPECT_EQ(ListBody.find("View.Draw(BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f))"), std::string::npos);

	// 页面半透明仍然生效，只是不再用于列表内部的叠加层。
	EXPECT_NE(Source.find("g_Config.m_QmMapBrowserOpacity / 100.0f"), std::string::npos);
	EXPECT_NE(ListBody.find("BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f))"), std::string::npos);
	EXPECT_NE(Source.find("View.Draw(BrowserPanelColor(0.82f)"), std::string::npos);
	EXPECT_NE(Source.find("Tab.Draw(BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f))"), std::string::npos);
	EXPECT_EQ(Source.find("BrowserOpacityColor(ColorRGBA(0.0f, 0.0f, 0.3f))"), std::string::npos);
}

TEST(QmNewUiMenuBranches, BrowserServerListDimsScrollbarRailWithCardSurface)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string RenderServerbrowser = FunctionBody(Source, "void CMenus::RenderServerbrowser(");
	ASSERT_FALSE(RenderServerbrowser.empty());

	// 滚动条轨道的白色 25% 由 ScaleBackgroundAlpha 缩放，必须跟着卡片一起压暗，
	// 否则它会成为卡片里最亮的一块表面。
	const size_t ScopePos = RenderServerbrowser.find("CUiBackgroundAlphaScaleScope ListOpacityScope(Ui(), SERVER_LIST_SCROLLBAR_RAIL_ALPHA_SCALE);");
	ASSERT_NE(ScopePos, std::string::npos);
	const size_t CallPos = RenderServerbrowser.find("RenderServerbrowserServerList(ServerList, WasListboxItemActivated);");
	ASSERT_NE(CallPos, std::string::npos);
	EXPECT_LT(ScopePos, CallPos);
	EXPECT_NE(Source.find("static constexpr float SERVER_LIST_SCROLLBAR_RAIL_ALPHA_SCALE = 0.28f;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, BrowserServerListRevealSelectionDoesNotFightUserScroll)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string ListBody = FunctionBody(Source, "void CMenus::RenderServerbrowserServerList(");
	ASSERT_FALSE(ListBody.empty());

	// 列表只渲染可见区内的行（SkipItems 虚拟化），选中行滚出可见区后渲染循环不会再碰到它。
	// 因此「选中项」必须取自排序后的真实选中服务器：一旦用本帧渲染结果预置（= -1），
	// NewSelected != m_SelectedIndex 会每帧成立，于是每帧 ScrollToSelected() 把选中行拉回视野，
	// 用户永远无法把选中行滚出列表。
	EXPECT_NE(ListBody.find("m_SelectedIndex = SelectedServerIndex;"), std::string::npos);
	EXPECT_NE(ListBody.find("if(NewSelected != m_SelectedIndex)"), std::string::npos);
	EXPECT_EQ(ListBody.find("m_SelectedIndex = -1;"), std::string::npos);
	EXPECT_EQ(ListBody.find("m_SelectedIndex = i;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, BrowserSortedHeaderIndicatorStaysVisibleOnSingleSurface)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string GridHeader = FunctionBody(Source, "int CMenus::DoButton_GridHeader(");
	ASSERT_FALSE(GridHeader.empty());

	// 表头底色取消后，已排序指示不能再是固定的 0.5 白块，也不能跟着透明度过低而消失。
	EXPECT_EQ(GridHeader.find("ColorRGBA(1, 1, 1, 0.5f)"), std::string::npos);
	EXPECT_NE(GridHeader.find("const float SortedAlpha = std::clamp(0.34f + g_Config.m_QmMapBrowserOpacity / 100.0f * 0.6f, 0.0f, 0.55f);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, AppearanceNamePlateContainsNameplateTextControlsWithoutInternalScrollRegion)
{
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string NamePlateBranch = BlockBodyAfter(SettingsSource, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_NAME_PLATE)");
	ASSERT_FALSE(NamePlateBranch.empty());

	const size_t TextSettingsPos = NamePlateBranch.find("Localize(\"Nameplate text\")");
	const size_t HookStrengthPos = NamePlateBranch.find("Localize(\"Hook Strength\")");
	ASSERT_NE(TextSettingsPos, std::string::npos);
	ASSERT_NE(HookStrengthPos, std::string::npos);
	EXPECT_LT(TextSettingsPos, HookStrengthPos);
	EXPECT_EQ(NamePlateBranch.find("appearance-name-plate-title"), std::string::npos);

	EXPECT_NE(NamePlateBranch.find("g_Config.m_QmNameplateTextEffects"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("QM_TEXT_EFFECT_BORDER"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("QM_TEXT_EFFECT_GRADIENT"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("QM_TEXT_EFFECT_RAINBOW"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("QM_TEXT_EFFECT_GLOW"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Localize(\"Playing effects\")"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Localize(\"Spectate effects\")"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Localize(\"Demo effects\")"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Localize(\"Demo target\")"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Localize(\"Border range\")"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Localize(\"Glow range\")"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("DoLine_ColorPicker(&s_NameplateTextBorderColorId"), std::string::npos);

	EXPECT_EQ(NamePlateBranch.find("static CScrollRegion s_NameplateTextCardScrollRegion;"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("BeginSettingsScrollRegion(s_NameplateTextCardScrollRegion"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("FinishSettingsScrollRegion(s_NameplateTextCardScrollRegion"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("static CScrollRegion s_NameplateTextPlayingDropDownScrollRegion;"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("static CScrollRegion s_NameplateTextSpectateDropDownScrollRegion;"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("static CScrollRegion s_NameplateTextDemoDropDownScrollRegion;"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("static CScrollRegion s_NameplateTextDemoTargetDropDownScrollRegion;"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("State.m_SelectionPopupContext.m_pScrollRegion = &ScrollRegion;"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("s_NameplateTextDemoTargetDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_NameplateTextDemoTargetDropDownScrollRegion;"), std::string::npos);

	const std::string QmSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	EXPECT_EQ(QmSource.find("auto RenderNameplateTextSettings = [&](CUIRect &CardContent)"), std::string::npos);
	EXPECT_EQ(QmSource.find("RenderNameplateTextSettings(CardContent);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DemoBrowserUsesExplicitLegacyShellBranches)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_demo.cpp");
	const std::string RenderDemoBrowser = FunctionBody(Source, "void CMenus::RenderDemoBrowser(CUIRect MainView)");
	const std::string RenderDemoBrowserList = FunctionBody(Source, "void CMenus::RenderDemoBrowserList(CUIRect ListView, bool &WasListboxItemActivated)");
	const std::string RenderDemoBrowserDetails = FunctionBody(Source, "void CMenus::RenderDemoBrowserDetails(CUIRect DetailsView)");
	const std::string RenderDemoBrowserButtons = FunctionBody(Source, "void CMenus::RenderDemoBrowserButtons(CUIRect ButtonsView, bool WasListboxItemActivated)");
	const size_t UseNewUiButtonsPos = RenderDemoBrowserButtons.find("if(UseNewUi)");
	ASSERT_NE(UseNewUiButtonsPos, std::string::npos);
	const size_t UseNewUiButtonsBodyStart = RenderDemoBrowserButtons.find("{", UseNewUiButtonsPos);
	ASSERT_NE(UseNewUiButtonsBodyStart, std::string::npos);
	const size_t UseNewUiButtonsBodyEnd = MatchingBrace(RenderDemoBrowserButtons, UseNewUiButtonsBodyStart);
	ASSERT_NE(UseNewUiButtonsBodyEnd, std::string::npos);
	const std::string UseNewUiButtonsBranch = RenderDemoBrowserButtons.substr(UseNewUiButtonsBodyStart, UseNewUiButtonsBodyEnd - UseNewUiButtonsBodyStart);
	const size_t LegacyButtonsElsePos = RenderDemoBrowserButtons.find("CUIRect ButtonBarTop, ButtonBarBottom;", UseNewUiButtonsBodyEnd);
	ASSERT_NE(LegacyButtonsElsePos, std::string::npos);
	const std::string LegacyButtonsBranch = RenderDemoBrowserButtons.substr(LegacyButtonsElsePos);

	EXPECT_NE(Source.find("const bool UseNewUi = g_Config.m_QmNewUi != 0;"), std::string::npos);
	EXPECT_NE(RenderDemoBrowser.find("if(UseNewUi)"), std::string::npos);
	EXPECT_NE(RenderDemoBrowser.find("MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowser.find("MainView.Margin(10.0f, &MainView);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowser.find("MainView.HSplitBottom(44.0f, &ListView, &ButtonsView);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowser.find("MainView.HSplitBottom(22.0f * 2.0f + 5.0f, &ListView, &ButtonsView);"), std::string::npos);
	EXPECT_EQ(RenderDemoBrowser.find("MainView.HSplitBottom(22.0f * 2.0f + 10.0f, &ListView, &ButtonsView);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("Headers.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_T, 5.0f);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("ListBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_B, 5.0f);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("const float HeaderGap = UseNewUi ? 4.0f : 2.0f;"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("const float RowHeight = UseNewUi ? ms_ListheaderHeight + 1.0f : ms_ListheaderHeight;"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("CColumn aCols[] = {"), std::string::npos);
	EXPECT_EQ(RenderDemoBrowserList.find("static CColumn s_aCols[] = {"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("{COL_MARKERS, SORT_MARKERS, FONT_ICON_BOOKMARK, 1, true, UseNewUi ? 34.0f : 30.0f, {0}, Localizable(\"Markers\")}"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("{COL_LENGTH, SORT_LENGTH, Localizable(\"Length\"), 1, false, UseNewUi ? 84.0f : 75.0f, {0}, nullptr}"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("{COL_DATE, SORT_DATE, Localizable(\"Date\"), 1, false, UseNewUi ? 156.0f : 150.0f, {0}, nullptr}"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("aCols[9].m_Width = BrowsingScreenshots ? (UseNewUi ? 176.0f : 170.0f) : (UseNewUi ? 156.0f : 150.0f);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("s_ListBox.DoStart(UseNewUi ? RowHeight : ms_ListheaderHeight, m_vpFilteredDemos.size(), 1, 3, m_DemolistSelectedIndex, &ListBox, false, IGraphics::CORNER_ALL);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserDetails.find("Header.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_T, 5.0f);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserDetails.find("Contents.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_B, 5.0f);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserDetails.find("Contents.Margin(5.0f, &Contents);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserButtons.find("if(UseNewUi)"), std::string::npos);
	EXPECT_NE(UseNewUiButtonsBranch.find("CUIRect MainRow = ButtonsView;"), std::string::npos);
	EXPECT_NE(UseNewUiButtonsBranch.find("const float ButtonWidth = MainRow.h * 1.55f;"), std::string::npos);
	EXPECT_NE(UseNewUiButtonsBranch.find("const float RowHeight = minimum(22.0f, ButtonsView.h);"), std::string::npos);
	EXPECT_NE(UseNewUiButtonsBranch.find("ButtonsView.HSplitTop(3.0f, nullptr, &ButtonsView);"), std::string::npos);
	EXPECT_NE(UseNewUiButtonsBranch.find("ButtonsView.HSplitBottom(3.0f, &ButtonsView, nullptr);"), std::string::npos);
	EXPECT_NE(LegacyButtonsBranch.find("ButtonsView.HSplitMid(&ButtonBarTop, &ButtonBarBottom, 5.0f);"), std::string::npos);
	EXPECT_EQ(RenderDemoBrowser.find("MainView.Draw(MenuPanelColor()"), std::string::npos);
}

TEST(QmNewUiMenuBranches, BrowserFavoriteMapsEarlyReturnAvoidsLegacyDoubleInset)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string RenderServerbrowser = FunctionBody(Source, "void CMenus::RenderServerbrowser(");
	const size_t FavoriteMapsPos = RenderServerbrowser.find("if(g_Config.m_UiPage == PAGE_FAVORITE_MAPS)");
	ASSERT_NE(FavoriteMapsPos, std::string::npos);
	const size_t DrawPos = RenderServerbrowser.find("View.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);");
	ASSERT_NE(DrawPos, std::string::npos);
	EXPECT_LT(FavoriteMapsPos, DrawPos);
	EXPECT_NE(RenderServerbrowser.find("RenderServerbrowserFavoriteMaps(MainView);"), std::string::npos);
	EXPECT_NE(RenderServerbrowser.find("View.Margin(6.0f, &View);\n\t\t\tRenderServerbrowserFavoriteMaps(View);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, MapHistoryUsesFullHeightTabbedResponsiveCardGrid)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string RenderFavoriteMaps = FunctionBody(Source, "void CMenus::RenderServerbrowserFavoriteMaps(");

	EXPECT_EQ(RenderFavoriteMaps.find("SplitHistoryPanel"), std::string::npos);
	EXPECT_EQ(RenderFavoriteMaps.find("SplitHistoryColumns"), std::string::npos);
	EXPECT_NE(RenderFavoriteMaps.find("s_aFavoriteMapsWorkspaceTabButtons"), std::string::npos);
	EXPECT_NE(RenderFavoriteMaps.find("QmMapHistoryUi::GridColumns(HistoryPanel.w - QmMapHistoryUi::LIST_SCROLLBAR_WIDTH, CardRowHeight)"), std::string::npos);
	EXPECT_NE(RenderFavoriteMaps.find("QmMapHistoryUi::StackControls(HistoryPanel.w, Layout.m_ControlHeight)"), std::string::npos);
	EXPECT_NE(RenderFavoriteMaps.find("s_MapHistoryListBox.SetScrollbarAlwaysReserved(true);"), std::string::npos);
	EXPECT_NE(RenderFavoriteMaps.find("s_MapHistoryListBox.DoStart(CardRowHeight"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmLocalizationEnglishOverlayUsesExplicitEnglishFile)
{
	const std::string Source = ReadTextFile("src/game/client/gameclient.cpp");

	EXPECT_EQ(Source.find("str_format(aBuf, sizeof(aBuf), \"qmclient/%s\", g_Config.m_ClLanguagefile);"), std::string::npos);
	EXPECT_EQ(Source.find("static void LoadQmClientLanguageOverlay("), std::string::npos);
	EXPECT_EQ(Source.find("const char *pQmLanguageFile = g_Config.m_ClLanguagefile[0] != '\\0' ? g_Config.m_ClLanguagefile : \"english.txt\";"), std::string::npos);
	EXPECT_EQ(Source.find("const char *pQmLanguageFile = pLanguageFile[0] != '\\0' ? pLanguageFile : \"english.txt\";"), std::string::npos);
	EXPECT_EQ(Source.find("if(str_comp(pLanguageFile, \"languages/simplified_chinese.txt\") == 0)"), std::string::npos);
	EXPECT_EQ(Source.find("const char *pQmLanguageFile = pLanguageFile[0] != '\\0' ? pLanguageFile : \"languages/english.txt\";"), std::string::npos);
	EXPECT_EQ(Source.find("str_format(aBuf, sizeof(aBuf), \"qmclient/%s\", pQmLanguageFile);"), std::string::npos);
	EXPECT_EQ(Source.find("LoadQmClientLanguageOverlay(g_Localization, g_Config.m_ClLanguagefile, Storage(), Console());"), std::string::npos);
	EXPECT_NE(Source.find("g_Localization.Load(g_Config.m_ClLanguagefile, Storage(), Console());"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmClientTabLabelsDoNotCacheLocalizedPointers)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	EXPECT_EQ(Source.find("static const char *s_apQmTabNames"), std::string::npos);
	EXPECT_EQ(Source.find("s_aQmLanguageFile"), std::string::npos);
	EXPECT_NE(Source.find("const char *apQmTabNames[NUMBER_OF_QMCLIENT_SETTINGS_TABS] = {};"), std::string::npos);
	EXPECT_NE(Source.find("apQmTabNames[QMCLIENT_SETTINGS_TAB_VISUAL] = Localize(\"Visuals\");"), std::string::npos);
	EXPECT_NE(Source.find("apQmTabNames[QMCLIENT_SETTINGS_TAB_FUNCTION] = Localize(\"Functions\");"), std::string::npos);
	EXPECT_NE(Source.find("apQmTabNames[QMCLIENT_SETTINGS_TAB_HUD] = Localize(\"HUD\");"), std::string::npos);
	EXPECT_NE(Source.find("apQmTabNames[QMCLIENT_SETTINGS_TAB_CONTRIBUTORS] = Localize(\"Contributors\");"), std::string::npos);
	EXPECT_NE(Source.find("apQmTabNames[QMCLIENT_SETTINGS_TAB_CONFIG] = Localize(\"Config\");"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TranslateTargetRatioDoesNotRenderSkipNotes)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string TranslateModule = FunctionBody(Source, "void CMenus::RenderQmFunctionTranslateContent(");

	EXPECT_NE(TranslateModule.find("RenderSliderWithNumberInput(&s_LocalDetectRatioSelectorId"), std::string::npos);
	EXPECT_EQ(TranslateModule.find("qmclient-translate-skip-target-language-note"), std::string::npos);
	EXPECT_EQ(TranslateModule.find("qmclient-translate-skip-numeric-note"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmClientUpdateFlowUsesQmClientNamingAndComparisonHelper)
{
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string TClientHeader = ReadTextFile("src/game/client/components/tclient/tclient.h");
	const std::string MenusStartSource = ReadTextFile("src/game/client/components/menus_start.cpp");
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string QmMenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string OnUpdate = FunctionBody(TClientSource, "void CTClient::OnUpdate()");

	EXPECT_NE(TClientSource.find("#include <game/client/components/qmclient/update_version.h>"), std::string::npos);
	EXPECT_NE(TClientSource.find("static constexpr const char *QMCLIENT_INFO_URL"), std::string::npos);
	EXPECT_NE(TClientSource.find("QMCLIENT_UPDATE_PACKAGE_NAME = \"QmClient-windows.zip\""), std::string::npos);
	EXPECT_NE(TClientSource.find("qm_update_verify_manifest_package"), std::string::npos);
	EXPECT_NE(TClientSource.find("qm_update_verify_package_digest"), std::string::npos);
	EXPECT_NE(TClientSource.find("qm_update_extract_bootstrap_updater"), std::string::npos);
	EXPECT_NE(TClientSource.find("ResultSha256()"), std::string::npos);
	EXPECT_NE(TClientSource.find("FetchQmClientUpdateInfo();"), std::string::npos);
	EXPECT_NE(TClientSource.find("FinishQmClientUpdateInfo();"), std::string::npos);
	EXPECT_NE(TClientSource.find("ResetQmClientUpdateInfoTask();"), std::string::npos);
	EXPECT_NE(TClientSource.find("NeedQmClientUpdate()"), std::string::npos);
	EXPECT_NE(TClientSource.find("RequestQmClientUpdateCheckAndUpdate()"), std::string::npos);
	EXPECT_NE(TClientSource.find("ParseQmClientUpdateRelease"), std::string::npos);
	EXPECT_EQ(TClientSource.find("NeedUpdate()"), std::string::npos);
	EXPECT_EQ(TClientSource.find("FetchTClientInfo()"), std::string::npos);
	EXPECT_EQ(TClientSource.find("FinishTClientInfo()"), std::string::npos);
	EXPECT_EQ(TClientSource.find("ResetTClientInfoTask()"), std::string::npos);
	EXPECT_EQ(TClientSource.find("TCLIENT_INFO_URL"), std::string::npos);
	EXPECT_EQ(TClientSource.find("TCLIENT_UPDATE_EXE_URL"), std::string::npos);
	EXPECT_EQ(TClientSource.find("CalculateHashes(m_aUpdatePackageTmp"), std::string::npos);
	EXPECT_LT(OnUpdate.find("FinishUpdateDownloads();"), OnUpdate.find("!IsUpdateChecking() && !IsUpdateDownloading() && !m_UpdateReady"));
	EXPECT_NE(TClientSource.find("Force && m_UpdateShutdownRequested"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmAutoUpdate, qm_auto_update, 0"), std::string::npos);
	EXPECT_NE(ConfigSource.find("QmShowOutdatedVersionWarning"), std::string::npos);
	EXPECT_NE(QmMenusSource.find("Show outdated version warning"), std::string::npos);
	const std::string FunctionMetrics = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogFunctionMetrics.h");
	EXPECT_NE(FunctionMetrics.find("Localizable(\"Automatic updates\")"), std::string::npos);
	EXPECT_NE(FunctionMetrics.find("Localizable(\"Show outdated version warning\")"), std::string::npos);
	EXPECT_NE(FunctionMetrics.find("{&g_Config.m_QmAutoUpdate,"), std::string::npos);

	EXPECT_NE(TClientHeader.find("m_pQmClientUpdateInfoTask"), std::string::npos);
	EXPECT_NE(TClientHeader.find("m_FetchedQmClientUpdateInfo"), std::string::npos);
	EXPECT_NE(TClientHeader.find("m_QmClientAutoUpdateAfterCheck"), std::string::npos);
	EXPECT_NE(TClientHeader.find("m_aQmClientLatestVersionStr"), std::string::npos);
	EXPECT_NE(TClientHeader.find("m_pUpdatePackageTask"), std::string::npos);
	EXPECT_NE(TClientHeader.find("m_UpdateShutdownRequested"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("m_pTClientInfoTask"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("m_FetchedTClientInfo"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("m_AutoUpdateAfterCheck"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("m_aVersionStr"), std::string::npos);

	EXPECT_NE(MenusStartSource.find("m_FetchedQmClientUpdateInfo"), std::string::npos);
	EXPECT_NE(MenusStartSource.find("NeedQmClientUpdate()"), std::string::npos);
	EXPECT_NE(MenusStartSource.find("defined(CONF_AUTOUPDATE) && defined(CONF_FAMILY_WINDOWS)"), std::string::npos);
	EXPECT_NE(MenusStartSource.find("if(g_Config.m_QmAutoUpdate)"), std::string::npos);
	EXPECT_EQ(MenusStartSource.find("m_FetchedTClientInfo"), std::string::npos);
	EXPECT_EQ(MenusStartSource.find("NeedUpdate()"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientHeaderIncludesGeneratedProtocolForWeaponDefaults)
{
	const std::string TClientHeader = ReadTextFile("src/game/client/components/tclient/tclient.h");

	EXPECT_NE(TClientHeader.find("#include <generated/protocol.h>"), std::string::npos);
	EXPECT_NE(TClientHeader.find("m_aGoresPreHammerWeapon[NUM_DUMMIES] = {WEAPON_GUN, WEAPON_GUN};"), std::string::npos);
}

TEST(QmNewUiMenuBranches, StartMenuKeepsExplicitUseV2AndLegacyButtonPaths)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_start.cpp");
	const std::string RenderStartMenuImpl = FunctionBody(Source, "void CMenusStart::RenderStartMenuImpl(");
	const std::string UseV2Block = BlockBodyAfter(RenderStartMenuImpl, "if(UseV2Layout)");

	EXPECT_NE(Source.find("void CMenusStart::RenderStartMenu(CUIRect MainView)"), std::string::npos);
	EXPECT_NE(Source.find("RenderStartMenuImpl(MainView, false);"), std::string::npos);
	EXPECT_NE(Source.find("void CMenusStart::RenderStartMenuV2(CUIRect MainView)"), std::string::npos);
	EXPECT_NE(Source.find("RenderStartMenuImpl(MainView, true);"), std::string::npos);
	EXPECT_NE(RenderStartMenuImpl.find("if(UseV2Layout)"), std::string::npos);
	EXPECT_NE(UseV2Block.find("ui_widget::PrimaryButton"), std::string::npos);
	EXPECT_NE(UseV2Block.find("ui_widget::SecondaryButton"), std::string::npos);
	EXPECT_EQ(UseV2Block.find("DoButton_Menu("), std::string::npos);
	EXPECT_NE(RenderStartMenuImpl.find("static float s_aMenuButtonScale[MenuButtonCount] = {};"), std::string::npos);
	EXPECT_NE(RenderStartMenuImpl.find("const auto ScaleButtonRect = [](const CUIRect &Base, float Scale) {"), std::string::npos);
	EXPECT_NE(RenderStartMenuImpl.find("GameClient()->m_Menus.DoButton_Menu(&s_QuitButton"), std::string::npos);
	EXPECT_NE(RenderStartMenuImpl.find("GameClient()->m_Menus.DoButton_Menu(&s_PlayButton"), std::string::npos);
}

TEST(QmNewUiMenuBranches, StartMenuEntryKeepsLegacyStartPageWithQmNewUi)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	EXPECT_NE(Source.find("else if(m_ShowStart)"), std::string::npos);
	const std::string Render = FunctionBody(Source, "void CMenus::Render()");
	const size_t StartMenuPos = Render.find("else if(m_ShowStart)");
	ASSERT_NE(StartMenuPos, std::string::npos);
	const size_t StartMenuBodyStart = Render.find("{", StartMenuPos);
	ASSERT_NE(StartMenuBodyStart, std::string::npos);
	const size_t StartMenuBodyEnd = MatchingBrace(Render, StartMenuBodyStart);
	ASSERT_NE(StartMenuBodyEnd, std::string::npos);
	const std::string StartMenuBlock = Render.substr(StartMenuBodyStart, StartMenuBodyEnd - StartMenuBodyStart);
	EXPECT_NE(StartMenuBlock.find("m_MenusStart.RenderStartMenu(Screen);"), std::string::npos);
	EXPECT_EQ(StartMenuBlock.find("m_MenusStart.RenderStartMenuV2(Screen);"), std::string::npos);
	EXPECT_EQ(StartMenuBlock.find("g_Config.m_QmNewUi"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsShellKeepsExplicitQmNewUiContainerBranch)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettings = FunctionBody(Source, "void CMenus::RenderSettings(CUIRect MainView)");
	const size_t UseNewSettingsUiIfPos = RenderSettings.find("if(UseNewSettingsUi)");
	ASSERT_NE(UseNewSettingsUiIfPos, std::string::npos);
	const size_t UseNewSettingsUiBodyStart = RenderSettings.find("{", UseNewSettingsUiIfPos);
	ASSERT_NE(UseNewSettingsUiBodyStart, std::string::npos);
	const size_t UseNewSettingsUiBodyEnd = MatchingBrace(RenderSettings, UseNewSettingsUiBodyStart);
	ASSERT_NE(UseNewSettingsUiBodyEnd, std::string::npos);
	const std::string UseNewSettingsUiBlock = RenderSettings.substr(UseNewSettingsUiBodyStart, UseNewSettingsUiBodyEnd - UseNewSettingsUiBodyStart);
	const size_t OldSettingsUiElsePos = RenderSettings.find("else", UseNewSettingsUiBodyEnd);
	ASSERT_NE(OldSettingsUiElsePos, std::string::npos);
	const size_t OldSettingsUiBodyStart = RenderSettings.find("{", OldSettingsUiElsePos);
	ASSERT_NE(OldSettingsUiBodyStart, std::string::npos);
	const size_t OldSettingsUiBodyEnd = MatchingBrace(RenderSettings, OldSettingsUiBodyStart);
	ASSERT_NE(OldSettingsUiBodyEnd, std::string::npos);
	const std::string OldSettingsUiBlock = RenderSettings.substr(OldSettingsUiBodyStart, OldSettingsUiBodyEnd - OldSettingsUiBodyStart);
	const std::string SettingsHeaderBranch = BlockBodyAfter(RenderSettings, "if(UseNewSettingsUi)\n\t{\n\t\tTabBar.Margin(10.0f, &TabBar);");
	const std::string SettingsHeaderLegacyBranch = BlockBodyAfter(RenderSettings, "else\n\t{\n\t\tTabBar.HSplitTop(50.0f, &Button, &TabBar);");

	EXPECT_NE(Source.find("const bool UseNewSettingsUi = g_Config.m_QmNewUi != 0;"), std::string::npos);
	EXPECT_NE(UseNewSettingsUiBlock.find("TabBar.Draw(SettingsTabbarColor()"), std::string::npos);
	EXPECT_NE(UseNewSettingsUiBlock.find("Shell.m_ContentPanelRect.Draw(MenuPanelColor()"), std::string::npos);
	EXPECT_EQ(UseNewSettingsUiBlock.find("MainView.Draw(ms_ColorTabbarActive"), std::string::npos);
	EXPECT_NE(OldSettingsUiBlock.find("MainView.Draw(ms_ColorTabbarActive"), std::string::npos);
	EXPECT_EQ(OldSettingsUiBlock.find("SettingsTabbarColor()"), std::string::npos);
	EXPECT_EQ(OldSettingsUiBlock.find("MenuPanelColor()"), std::string::npos);
	EXPECT_EQ(SettingsHeaderBranch.find("Button.Draw(ms_ColorTabbarActive"), std::string::npos);
	EXPECT_NE(SettingsHeaderLegacyBranch.find("Button.Draw(ms_ColorTabbarActive"), std::string::npos);
}

TEST(QmNewUiMenuBranches, LegacyMenusKeepTabAndPanelShellConnected)
{
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string MenuShellSplit = "const bool UseNewUi = g_Config.m_QmNewUi != 0;\n\t\t\tScreen.HSplitTop(MenuMenubarHeight(UseNewUi), &TabBar, &MainView);\n\t\t\tif(UseNewUi)\n\t\t\t\tMainView.HSplitTop(6.0f, nullptr, &MainView);";
	EXPECT_NE(MenusSource.find("constexpr float MENU_MENUBAR_HEIGHT_NEW = 24.0f;"), std::string::npos);
	EXPECT_NE(MenusSource.find("constexpr float MENU_MENUBAR_HEIGHT_LEGACY = 30.0f;"), std::string::npos);
	EXPECT_NE(MenusSource.find("constexpr float MenuMenubarHeight(bool UseNewUi)"), std::string::npos);
	EXPECT_NE(MenusSource.find(MenuShellSplit), std::string::npos);
	EXPECT_NE(MenusSource.find("case IClient::STATE_ONLINE:"), std::string::npos);
	EXPECT_NE(MenusSource.find(MenuShellSplit, MenusSource.find("case IClient::STATE_ONLINE:")), std::string::npos);

	const std::string QmClientSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	EXPECT_NE(QmClientSource.find("const bool UseNewUi = g_Config.m_QmNewUi != 0;"), std::string::npos);
	EXPECT_EQ(QmClientSource.find("if(UseNewUi)\n\t\t\tMainView.HSplitTop(Margin, nullptr, &MainView);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, AssetsPreviewUsesInnerFrameRectForPreviewImage)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings_assets.cpp");

	EXPECT_NE(Source.find("auto DrawPreviewFrame = [&](const CUIRect &TextureRect) -> CUIRect {"), std::string::npos);
	EXPECT_NE(Source.find("PreviewFrame.Margin(3.0f, &PreviewFrame);"), std::string::npos);
	EXPECT_NE(Source.find("return PreviewFrame;"), std::string::npos);
	EXPECT_NE(Source.find("auto ComputeAssetPreviewContentSize = [&](bool WorkshopCard)"), std::string::npos);
	EXPECT_NE(Source.find("CUIRect PreviewFrameRect = DrawPreviewFrame(Shell.m_TextureRect);"), std::string::npos);
	EXPECT_NE(Source.find("const auto [PreviewContentWidth, PreviewContentHeight] = ComputeAssetPreviewContentSize(WorkshopCard);"), std::string::npos);
	EXPECT_NE(Source.find("const auto [PreviewContentWidth, PreviewContentHeight] = ComputeAssetPreviewContentSize(true);"), std::string::npos);
	EXPECT_NE(Source.find("const CUIRect PreviewRect = ComputePreviewDrawRect(PreviewFrameRect, PreviewContentWidth, PreviewContentHeight);"), std::string::npos);
	EXPECT_EQ(Source.find("const CUIRect PreviewRect = ComputePreviewDrawRect(HeaderLayout.m_TextureRect, TextureWidth, TextureHeight);"), std::string::npos);
	EXPECT_EQ(Source.find("const CUIRect PreviewRect = ComputePreviewDrawRect(HeaderLayout.m_TextureRect, TextureWidth, TextureWidth);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, GaussianBlurSettingReplacesBetterScoreboardAndIsVersioned)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string MenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string MiniFeaturesContent = FunctionBody(MenusSource, "void CMenus::RenderQmFunctionMiniFeaturesContent(");
	// 普通开关行表与「梦境特性」的行数口径都搬到了卡片目录的 FunctionMetrics 头文件，
	// 卡片模块（QmCardCatalogFunction.cpp）与渲染函数共用同一份表。
	const std::string FunctionMetrics = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogFunctionMetrics.h");
	const std::string FunctionCatalog = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogFunction.cpp");
	const std::string MenusToml = ReadTextFile("qmclient_scripts/languages_qmclient/translations/i18n/menus.toml");
	const std::string VersionSource = ReadTextFile("src/game/version.h");

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmBetterScoreboard, qm_better_scoreboard, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_NE(FunctionMetrics.find("{&g_Config.m_QmBetterScoreboard, Localizable(\"Better scoreboard\"), &g_Config.m_QmBetterScoreboard},"), std::string::npos);
	EXPECT_NE(MiniFeaturesContent.find("RenderQmFunctionCheckbox(Feature.m_pId, Feature.m_pTextId, Localize(Feature.m_pTextId), Feature.m_pValue, &Row, PrewarmOnly);"), std::string::npos);
	// 卡片高度必须等于真实渲染行数，否则会在卡片末尾留下空白行。
	// 普通行是一张 16 行的表（渲染侧 s_aQmMiniFeatureRows 与卡片目录 QmMiniFeatureRows() 各一份，
	// 两份必须同长，否则高度又会对不上），另加「新版 IME」与「赞助提醒」两个特殊行 = 18 行。
	// 卡片高度必须等于真实渲染行数。行表只保留 FunctionMetrics 一份（16 行），
	// 另加「新版 IME」与「赞助提醒」两个特殊行 = 18 行。
	const size_t MiniFeatureTableRows = CountOccurrences(FunctionMetrics, "{&g_Config.m_Qm");
	EXPECT_EQ(MiniFeatureTableRows, 16u);
	EXPECT_EQ(MiniFeatureTableRows + 2u, 18u);
	EXPECT_NE(MiniFeaturesContent.find("for(const qm_card_catalog::SQmMiniFeatureRow &Feature : qm_card_catalog::QmMiniFeatureRows())"), std::string::npos);
	EXPECT_NE(MiniFeaturesContent.find("RenderQmFunctionCheckbox(Feature.m_pId, Feature.m_pTextId, Localize(Feature.m_pTextId), Feature.m_pValue, &Row, PrewarmOnly);"), std::string::npos);
	EXPECT_NE(MiniFeaturesContent.find("RenderQmFunctionCheckbox(&g_Config.m_QmNewIme, \"New IME\""), std::string::npos);
	EXPECT_NE(MiniFeaturesContent.find("RenderQmFunctionCheckbox(&g_Config.m_QmSponsorNudge, \"Sponsor reminder\""), std::string::npos);
	EXPECT_NE(FunctionCatalog.find("case EQmModuleId::MiniFeatures: return Rows(static_cast<float>(QmMiniFeatureRows().size() + QmMiniFeatureSpecialRowCount));"), std::string::npos);

	// 删除实时通道开关后，卡片高度仍须容纳末尾的赞助提醒。
	EXPECT_NE(MiniFeaturesContent.find("RenderQmFunctionCheckbox(&g_Config.m_QmSponsorNudge, \"Sponsor reminder\""), std::string::npos);
	EXPECT_NE(MenusToml.find("key = \"Better scoreboard\""), std::string::npos);
	EXPECT_NE(MenusToml.find("simplified_chinese = \"更好的计分板\""), std::string::npos);
	EXPECT_NE(VersionSource.find("#define QMCLIENT_VERSION \""), std::string::npos);
}

TEST(QmNewUiMenuBranches, RealtimeServiceHasNoDisableSetting)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string MenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string ClientSource = ReadTextFile("src/game/client/components/qmclient/qmclient.cpp");

	// 设置界面与旧配置均不能再关闭自有服务，保留地址、日志等独立配置。
	EXPECT_EQ(ConfigSource.find("QmWebSocket, qm_websocket,"), std::string::npos);
	EXPECT_EQ(MenusSource.find("\"Realtime channel\""), std::string::npos);
	EXPECT_FALSE(std::regex_search(ClientSource, std::regex(R"(\bg_Config\.m_QmWebSocket\b)")));
}

TEST(QmNewUiMenuBranches, NewSettingsUseToggleAndExposeAccentAndBlurControls)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");

	EXPECT_NE(ConfigSource.find("QmUiAccentColor, qm_ui_accent_color"), std::string::npos);
	EXPECT_NE(ConfigSource.find("QmUiSelectedColor, qm_ui_selected_color"), std::string::npos);
	const std::string SettingsCheckbox = FunctionBody(MenusSource, "int CMenus::DoSettingsButton_CheckBox(int Page, int Tab, int Subtab, const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect, const SLabelProperties &LabelProps, const bool ProcessInput, const float RequestedFontSize)");
	EXPECT_NE(SettingsCheckbox.find("if(g_Config.m_QmNewUi)"), std::string::npos);
	EXPECT_NE(SettingsCheckbox.find("ui_widget::Toggle(Context, pId, &ToggleValue, ToggleRect, false, ProcessInput)"), std::string::npos);
	EXPECT_NE(SettingsCheckbox.find("Ui()->DoButtonLogic(pId, 0, pRect, BUTTONFLAG_LEFT)"), std::string::npos);
	EXPECT_NE(SettingsSource.find("Localize(\"Interface accent color\")"), std::string::npos);
	EXPECT_NE(SettingsSource.find("Localize(\"Selected item color\")"), std::string::npos);
	EXPECT_NE(SettingsSource.find("Localize(\"Enable Gaussian blur\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsColorLabelsUseQmLocalizedKeys)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string MenusToml = ReadTextFile("qmclient_scripts/languages_qmclient/translations/i18n/menus.toml");

	EXPECT_EQ(Source.find("Localize(\"UI Color\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"Menu panel color\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"Menu panel opacity\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"Menu panel elevated opacity\")"), std::string::npos);
	EXPECT_EQ(Source.find("s_MenuPanelColorResetId"), std::string::npos);
	EXPECT_EQ(Source.find("g_Config.m_ClMenuPanelColor"), std::string::npos);
	EXPECT_EQ(Source.find("g_Config.m_UiColor"), std::string::npos);
	EXPECT_NE(Source.find("CSettingsContentRowFlow Rows(ContentRect, GraphicsMetrics);"), std::string::npos);
	EXPECT_NE(Source.find("CUIRect UiColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Source.find("DoLine_AlphaColorPicker(&s_UiColorResetId, ColorMetrics, &UiColorRow, Localize(\"Interface surface\"), &g_Config.m_QmUiColor, &g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_NE(Source.find("CUIRect MapBrowserColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Source.find("DoLine_AlphaColorPicker(&s_MapBrowserColorResetId, ColorMetrics, &MapBrowserColorRow, Localize(\"Map browser surface\"), &g_Config.m_QmMapBrowserColor, &g_Config.m_QmMapBrowserOpacity"), std::string::npos);
	EXPECT_NE(Source.find("CUIRect ScoreboardColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Source.find("DoLine_AlphaColorPicker(&s_ScoreboardColorResetId, ColorMetrics, &ScoreboardColorRow, Localize(\"Scoreboard surface\"), &g_Config.m_QmScoreboardColor, &g_Config.m_QmScoreboardOpacity"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmUiColor"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmMapBrowserColor"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmScoreboardColor"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmMapBrowserOpacity"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmScoreboardOpacity"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Interface surface\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Map browser surface\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Scoreboard surface\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"UI opacity\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"Map browser opacity\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"Scoreboard opacity\")"), std::string::npos);
	EXPECT_NE(MenusToml.find("key = \"UI opacity\""), std::string::npos);
	EXPECT_NE(MenusToml.find("simplified_chinese = \"界面不透明度\""), std::string::npos);
	EXPECT_NE(MenusToml.find("key = \"Map browser opacity\""), std::string::npos);
	EXPECT_NE(MenusToml.find("simplified_chinese = \"地图浏览器不透明度\""), std::string::npos);
	EXPECT_NE(MenusToml.find("key = \"Scoreboard opacity\""), std::string::npos);
	EXPECT_NE(MenusToml.find("simplified_chinese = \"计分板不透明度\""), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsGraphicsColorPickersExposeIndependentAlphaDomains)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Graphics = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(Graphics.empty());

	EXPECT_NE(Graphics.find("CSettingsContentRowFlow Rows(ContentRect, GraphicsMetrics);"), std::string::npos);
	EXPECT_NE(Graphics.find("CUIRect UiColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Graphics.find("DoLine_AlphaColorPicker(&s_UiColorResetId, ColorMetrics, &UiColorRow, Localize(\"Interface surface\"), &g_Config.m_QmUiColor, &g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_NE(Graphics.find("CUIRect MapBrowserColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Graphics.find("DoLine_AlphaColorPicker(&s_MapBrowserColorResetId, ColorMetrics, &MapBrowserColorRow, Localize(\"Map browser surface\"), &g_Config.m_QmMapBrowserColor, &g_Config.m_QmMapBrowserOpacity"), std::string::npos);
	EXPECT_NE(Graphics.find("CUIRect ScoreboardColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Graphics.find("DoLine_AlphaColorPicker(&s_ScoreboardColorResetId, ColorMetrics, &ScoreboardColorRow, Localize(\"Scoreboard surface\"), &g_Config.m_QmScoreboardColor, &g_Config.m_QmScoreboardOpacity"), std::string::npos);
	EXPECT_EQ(Graphics.find("graphics-ui-opacity"), std::string::npos);
	EXPECT_EQ(Graphics.find("graphics-map-browser-opacity"), std::string::npos);
	EXPECT_EQ(Graphics.find("graphics-scoreboard-opacity"), std::string::npos);
	EXPECT_EQ(Graphics.find("DoSliderWithValueInput("), std::string::npos);
}

TEST(QmNewUiMenuBranches, DynamicIslandColorPickerOwnsExistingOpacitySetting)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderQmHudDynamicIslandContent(");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("DoLine_AlphaColorPicker(&s_DynamicIslandBgColorId, CurrentSettingsContentMetrics(), &Content, Localize(\"Background color\"), &g_Config.m_QmHudIslandBgColor, &g_Config.m_QmHudIslandBgOpacity, 0x9C460E, 80)"), std::string::npos);
	EXPECT_EQ(Body.find("s_QmHudIslandBgOpacityInputId"), std::string::npos);
	EXPECT_EQ(Body.find("RenderQmSettingsSliderWithValueInput(&s_QmHudIslandBgOpacityInputId"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_COL(QmHudIslandBgColor, qm_hud_island_bg_color, 0x9C460E, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmHudIslandBgOpacity, qm_hud_island_bg_opacity, 80"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TranslateUiColorsPreserveConfiguredAlpha)
{
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	for(const char *pKey : {
		    "MACRO_CONFIG_COL(QmTranslateBtnColorDisabled, qm_translate_btn_color_disabled, 0xD1000029, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA",
		    "MACRO_CONFIG_COL(QmTranslateBtnColorEnabled, qm_translate_btn_color_enabled, 0xE69E5E86, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA",
		    "MACRO_CONFIG_COL(QmTranslateMenuBgColor, qm_translate_menu_bg_color, 0xF200001F, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA",
		    "MACRO_CONFIG_COL(QmTranslateMenuOptionSelected, qm_translate_menu_option_selected, 0xE69E5E86, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA",
		    "MACRO_CONFIG_COL(QmTranslateMenuOptionNormal, qm_translate_menu_option_normal, 0xE6000033, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA",
	    })
		EXPECT_NE(Config.find(pKey), std::string::npos) << pKey;
}

TEST(QmNewUiMenuBranches, DynamicIslandSettingsOmitsEdgeMarginControl)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderQmHudDynamicIslandContent(");
	ASSERT_FALSE(Body.empty());

	EXPECT_EQ(Body.find("QmHudIslandEdgeMargin"), std::string::npos);
	EXPECT_EQ(Body.find("Localize(\"Edge margin\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DynamicIslandOwnsHookCountdownToggleNextToSwitchCountdown)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderQmHudDynamicIslandContent(");
	// 预布局输入（命中判定）已搬到卡片目录的 HUD 卡片模块 BuildHudPreLayoutInput。
	const std::string HudCatalog = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogHud.cpp");
	const std::string PreLayout = FunctionBody(HudCatalog, "FSettingsCardPreLayoutInput BuildHudPreLayoutInput(");
	ASSERT_FALSE(Body.empty());
	ASSERT_FALSE(PreLayout.empty());

	const std::string Toggle =
		"RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHookCountdown, \"Enable hook countdown\", Localize(\"Enable hook countdown\"), &g_Config.m_QmHookCountdown);";
	const size_t HookRow = Body.find(Toggle);
	ASSERT_NE(HookRow, std::string::npos);
	// 必须排在开关倒计时的展开组之前：那组末尾有 `if(!g_Config.m_QmSwitchCountdown) return;`，
	// 放到后面会被一起藏掉，用户就再也打不开钩子倒计时。
	const size_t SwitchRow = Body.find("&g_Config.m_QmSwitchCountdown, \"Enable switch countdown\"");
	ASSERT_NE(SwitchRow, std::string::npos);
	EXPECT_LT(HookRow, SwitchRow);
	// 常驻单行开关，不跟着开关倒计时的展开收起变化。
	EXPECT_NE(Body.find("if(!g_Config.m_QmSwitchCountdown)\n\t\treturn;"), std::string::npos);

	// 预布局命中判定必须与渲染路径同序，否则点击会落到错位的行上。
	// 卡片目录里的桥接调用多带一个 pMenus（卡片模块不持有 CMenus 成员）。
	EXPECT_NE(PreLayout.find("HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmHookCountdown, &g_Config.m_QmHookCountdown)"), std::string::npos);
	const size_t HookInput = PreLayout.find("&g_Config.m_QmHookCountdown, &g_Config.m_QmHookCountdown");
	const size_t SwitchInput = PreLayout.find("&g_Config.m_QmSwitchCountdown, &g_Config.m_QmSwitchCountdown");
	ASSERT_NE(SwitchInput, std::string::npos);
	EXPECT_LT(HookInput, SwitchInput);

	// 默认关闭，避免升级后凭空多出一个跟随 Tee 的环。
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmHookCountdown, qm_hook_countdown, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DynamicIslandOwnsSwitchCountdownLocationChooser)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderQmHudDynamicIslandContent(");
	ASSERT_FALSE(Body.empty());

	// 总开关与两个位置开关成组；移除位置标题，关闭总开关时位置开关整组消失。
	EXPECT_NE(Body.find("RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmSwitchCountdown, \"Enable switch countdown\", Localize(\"Enable switch countdown\"), &g_Config.m_QmSwitchCountdown)"), std::string::npos);
	EXPECT_EQ(Body.find("qmclient-switch-countdown-location-label"), std::string::npos);
	EXPECT_NE(Body.find("RenderQmHudCheckbox(Content, LineHeight, LineSpacing, qm_card_catalog::SwitchCountdownFollowTeeId(), \"qmclient-switch-countdown-follow-tee\", Localize(\"Follow Tee\"), &FollowTee)"), std::string::npos);
	EXPECT_NE(Body.find("RenderQmHudCheckbox(Content, LineHeight, LineSpacing, qm_card_catalog::SwitchCountdownMediaIslandId(), \"qmclient-switch-countdown-media-island\", Localize(\"Show in Dynamic Island\"), &MediaIsland)"), std::string::npos);
	EXPECT_NE(Body.find("if(!g_Config.m_QmSwitchCountdown)\n\t\treturn;"), std::string::npos);
	// 两个位置都不勾时回落到关闭总开关，不留下界面与渲染互相打架的非法组合。
	EXPECT_NE(Body.find("if(FollowTee == 0 && MediaIsland == 0)"), std::string::npos);
	EXPECT_NE(Body.find("g_Config.m_QmSwitchCountdownMode = QmHudSwitchCountdownModeFromLocations(FollowTee != 0, MediaIsland != 0, CurrentMode);"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmSwitchCountdown, qm_switch_countdown, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmSwitchCountdownMode, qm_switch_countdown_mode, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DynamicIslandEdgeMarginIsOnlyAnIgnoredLegacyCommand)
{
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string GameClient = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string Callback = FunctionBody(GameClient, "void ConDiscardLegacyHudIslandEdgeMargin(");
	const std::string OnConsoleInit = FunctionBody(GameClient, "void CGameClient::OnConsoleInit()");

	EXPECT_EQ(Config.find("qm_hud_island_edge_margin"), std::string::npos);
	ASSERT_FALSE(Callback.empty());
	EXPECT_EQ(Callback.find("g_Config"), std::string::npos);
	EXPECT_NE(OnConsoleInit.find("pConsole->Register(\"qm_hud_island_edge_margin\", \"?i[value]\", CFGFLAG_CLIENT, ConDiscardLegacyHudIslandEdgeMargin, nullptr"), std::string::npos);
}

TEST(QmNewUiMenuBranches, LineInputRendersActiveTextOnlyOnce)
{
	const std::string Source = ReadTextFile("src/game/client/lineinput.cpp");
	const std::string Header = ReadTextFile("src/engine/textrender.h");
	const std::string TextSource = ReadTextFile("src/engine/client/text.cpp");
	const std::string Render = FunctionBody(Source, "STextBoundingBox CLineInput::Render(");
	const std::string RenderSelection = FunctionBody(Source, "void CLineInput::RenderSelection(");
	const std::string RenderCaret = FunctionBody(Source, "void CLineInput::RenderCaret(");
	ASSERT_FALSE(Render.empty());
	ASSERT_FALSE(RenderSelection.empty());
	ASSERT_FALSE(RenderCaret.empty());
	const auto CountOccurrences = [](const std::string &Text, const char *pNeedle) {
		size_t Count = 0;
		for(size_t Position = Text.find(pNeedle); Position != std::string::npos; Position = Text.find(pNeedle, Position + 1))
			++Count;
		return Count;
	};

	EXPECT_NE(Render.find("m_CaretPosition = Cursor.m_CursorRenderedPosition;"), std::string::npos);
	EXPECT_NE(Render.find("SetCompositionWindowPosition(m_CaretPosition + vec2"), std::string::npos);
	EXPECT_NE(Render.find("Cursor.m_RenderCursor = false;"), std::string::npos);
	EXPECT_NE(Render.find("Cursor.m_RenderSelection = false;"), std::string::npos);
	const size_t SelectionPrepass = Render.find("CTextCursor SelectionCursor = Cursor;");
	const size_t SelectionUnderlay = Render.find("RenderSelection(SelectionCursor, TextRender()->GetTextSelectionColor());", SelectionPrepass);
	const size_t TextPass = Render.find("TextRender()->TextEx(&Cursor, pDisplayStr);", SelectionPrepass);
	ASSERT_NE(SelectionPrepass, std::string::npos);
	ASSERT_NE(SelectionUnderlay, std::string::npos);
	ASSERT_NE(TextPass, std::string::npos);
	EXPECT_LT(SelectionPrepass, SelectionUnderlay);
	EXPECT_LT(SelectionUnderlay, TextPass);
	EXPECT_NE(Render.find("if(Cursor.m_HasCursorRenderedPosition)"), std::string::npos);
	EXPECT_NE(Render.find("RenderCaret(Cursor, Cursor.m_ForceCursorRendering"), std::string::npos);
	EXPECT_EQ(Render.find("CTextCursor CaretCursor;"), std::string::npos);
	EXPECT_EQ(Render.find("TextRender()->TextEx(&CaretCursor, pDisplayStr);"), std::string::npos);
	EXPECT_EQ(CountOccurrences(Render, "TextRender()->TextEx(&Cursor, pDisplayStr);"), 2u);
	EXPECT_NE(Header.find("bool m_RenderCursor = true;"), std::string::npos);
	EXPECT_NE(Header.find("bool m_RenderSelection = true;"), std::string::npos);
	EXPECT_NE(Header.find("bool m_HasCursorRenderedPosition = false;"), std::string::npos);
	EXPECT_NE(TextSource.find("const bool HasRenderedCursor = HasCursor && pCursor->m_RenderCursor;"), std::string::npos);
	EXPECT_NE(TextSource.find("const bool HasRenderedSelection = HasSelection && pCursor->m_RenderSelection;"), std::string::npos);
	const size_t SelectionRenderPos = TextSource.find("if(TextContainer.m_HasSelection)");
	const size_t TextRenderPos = TextSource.find("if(!TextContainer.m_StringInfo.m_vCharacterQuads.empty())");
	ASSERT_NE(SelectionRenderPos, std::string::npos);
	ASSERT_NE(TextRenderPos, std::string::npos);
	EXPECT_GT(SelectionRenderPos, TextRenderPos);
	EXPECT_NE(TextSource.find("if(SelectionStarted)"), std::string::npos);
	EXPECT_NE(TextSource.find("pCursor->m_HasCursorRenderedPosition = true;"), std::string::npos);
	const size_t TextExPos = TextSource.find("void TextEx(CTextCursor *pCursor, const char *pText, int Length = -1) override");
	const size_t LayoutOnlyGuard = TextSource.find("if((pCursor->m_Flags & TEXTFLAG_RENDER) == 0)", TextExPos);
	const size_t LayoutContainer = TextSource.find("STextContainer LayoutContainer;", LayoutOnlyGuard);
	const size_t LayoutPass = TextSource.find("AppendTextContainerImpl(LayoutContainer, pCursor, pText, Length);", LayoutContainer);
	const size_t RenderContainer = TextSource.find("STextContainerIndex TextCont;", LayoutPass);
	ASSERT_NE(TextExPos, std::string::npos);
	ASSERT_NE(LayoutOnlyGuard, std::string::npos);
	ASSERT_NE(LayoutContainer, std::string::npos);
	ASSERT_NE(LayoutPass, std::string::npos);
	ASSERT_NE(RenderContainer, std::string::npos);
	EXPECT_LT(LayoutOnlyGuard, LayoutContainer);
	EXPECT_LT(LayoutContainer, LayoutPass);
	EXPECT_LT(LayoutPass, RenderContainer);
	EXPECT_EQ(TextSource.find("CreateTextContainer(LayoutContainer", LayoutOnlyGuard), std::string::npos);
	EXPECT_NE(RenderSelection.find("Graphics()->TextureClear();"), std::string::npos);
	EXPECT_NE(RenderSelection.find("Graphics()->QuadsBegin();"), std::string::npos);
	EXPECT_NE(RenderSelection.find("Graphics()->QuadsEnd();"), std::string::npos);
	EXPECT_NE(RenderSelection.find("Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);"), std::string::npos);
	EXPECT_EQ(RenderSelection.find("RenderQuadContainerEx"), std::string::npos);
	EXPECT_NE(RenderCaret.find("Graphics()->QuadsBegin();"), std::string::npos);
	EXPECT_NE(RenderCaret.find("Graphics()->QuadsEnd();"), std::string::npos);
	EXPECT_EQ(RenderCaret.find("RenderQuadContainerEx"), std::string::npos);
	EXPECT_NE(RenderCaret.find("if(!Cursor.m_HasCursorRenderedPosition)"), std::string::npos);
	EXPECT_EQ(RenderCaret.find("m_CursorRenderedPosition.x < 0.0f"), std::string::npos);
	EXPECT_EQ(RenderCaret.find("m_CursorRenderedPosition.y < 0.0f"), std::string::npos);
	const size_t TextureClear = RenderCaret.find("Graphics()->TextureClear();");
	const size_t HiddenReturn = RenderCaret.find("if(!ForceVisible && !qm_lineinput::CaretVisibleForElapsed");
	ASSERT_NE(TextureClear, std::string::npos);
	ASSERT_NE(HiddenReturn, std::string::npos);
	EXPECT_LT(TextureClear, HiddenReturn);
}

TEST(QmNewUiMenuBranches, BufferedTextUploadsMissingGpuContainerWithoutUsingImmediateQuads)
{
	const std::string TextSource = ReadTextFile("src/engine/client/text.cpp");
	const std::string Render = FunctionBody(TextSource, "void RenderTextContainer(STextContainerIndex TextContainerIndex, const ColorRGBA &TextColor, const ColorRGBA &TextOutlineColor) override");
	const std::string Upload = FunctionBody(TextSource, "void UploadTextContainer(STextContainerIndex TextContainerIndex) override");

	ASSERT_FALSE(Render.empty());
	ASSERT_FALSE(Upload.empty());
	const size_t BufferedPath = Render.find("if(Graphics()->IsTextBufferingEnabled())");
	const size_t MissingContainer = Render.find("if(TextContainer.m_StringInfo.m_QuadBufferContainerIndex == -1)", BufferedPath);
	const size_t UploadMissingContainer = Render.find("UploadTextContainer(TextContainerIndex);", MissingContainer);
	const size_t BufferedRender = Render.find("Graphics()->RenderText(", UploadMissingContainer);
	const size_t ImmediateFallback = Render.find("else\n\t\t\t{\n\t\t\t\t// render tiles", BufferedPath);
	ASSERT_NE(BufferedPath, std::string::npos);
	ASSERT_NE(MissingContainer, std::string::npos);
	ASSERT_NE(UploadMissingContainer, std::string::npos);
	ASSERT_NE(BufferedRender, std::string::npos);
	ASSERT_NE(ImmediateFallback, std::string::npos);
	EXPECT_LT(BufferedPath, MissingContainer);
	EXPECT_LT(MissingContainer, UploadMissingContainer);
	EXPECT_LT(UploadMissingContainer, BufferedRender);
	EXPECT_LT(BufferedRender, ImmediateFallback);
	EXPECT_EQ(Render.find("Graphics()->IsTextBufferingEnabled() &&"), std::string::npos);
	EXPECT_NE(Render.find("Graphics()->QuadsBegin();"), std::string::npos);
	const size_t CreateBuffer = Upload.find("Graphics()->CreateBufferObject(");
	const size_t RecreateBuffer = Upload.find("Graphics()->RecreateBufferObject(");
	const size_t CreateContainer = Upload.find("Graphics()->CreateBufferContainer(&m_DefaultTextContainerInfo);");
	const size_t EmptyTextReturn = Upload.find("if(TextContainer.m_StringInfo.m_vCharacterQuads.empty())");
	ASSERT_NE(CreateBuffer, std::string::npos);
	ASSERT_NE(RecreateBuffer, std::string::npos);
	ASSERT_NE(CreateContainer, std::string::npos);
	ASSERT_NE(EmptyTextReturn, std::string::npos);
	EXPECT_LT(EmptyTextReturn, CreateBuffer);
	EXPECT_LT(CreateBuffer, RecreateBuffer);
	EXPECT_LT(RecreateBuffer, CreateContainer);
	EXPECT_EQ(Upload.find("Graphics()->DeleteBufferContainer("), std::string::npos);
}

TEST(QmNewUiMenuBranches, TextRendererKeepsInternalCaretStateSelfContained)
{
	const std::string Source = ReadTextFile("src/engine/client/text.cpp");
	const std::string Render = FunctionBody(Source, "void RenderTextContainer(STextContainerIndex TextContainerIndex, const ColorRGBA &TextColor, const ColorRGBA &TextOutlineColor) override");
	ASSERT_FALSE(Render.empty());

	const size_t CursorBlock = Render.find("if(TextContainer.m_HasCursor)");
	ASSERT_NE(CursorBlock, std::string::npos);
	EXPECT_NE(Render.find("Graphics()->TextureClear();", CursorBlock), std::string::npos);
	EXPECT_NE(Render.find("Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);", CursorBlock), std::string::npos);
}

TEST(QmNewUiMenuBranches, ColorPickerUsesIndependentPointerCapture)
{
	const std::string Source = ReadTextFile("src/game/client/ui.cpp");
	const std::string Picker = FunctionBody(Source, "EEditState CUi::DoPickerLogic(");
	ASSERT_FALSE(Picker.empty());

	EXPECT_NE(Picker.find("const bool Inside = MouseHovered(pRect);"), std::string::npos);
	EXPECT_NE(Picker.find("if(Inside && MouseButtonClicked(0))"), std::string::npos);
	EXPECT_NE(Picker.find("if(!CheckActiveItem(pId))"), std::string::npos);
	EXPECT_NE(Picker.find("if(!MouseButton(0))"), std::string::npos);
	EXPECT_EQ(Picker.find("m_pLastEditingItem"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ConsoleRestoresCompleteTextRenderState)
{
	const std::string Source = ReadTextFile("src/game/client/components/console.cpp");
	const std::string Render = FunctionBody(Source, "void CGameConsole::OnRender()");
	ASSERT_FALSE(Render.empty());

	for(const char *pState : {
		    "const ColorRGBA PreviousTextColor = TextRender()->GetTextColor();",
		    "const ColorRGBA PreviousTextOutlineColor = TextRender()->GetTextOutlineColor();",
		    "const ColorRGBA PreviousTextSelectionColor = TextRender()->GetTextSelectionColor();",
		    "const unsigned PreviousRenderFlags = TextRender()->GetRenderFlags();",
		    "const EFontPreset PreviousFontPreset = TextRender()->GetFontPreset();",
		    "TextRender()->SetRenderFlags(PreviousRenderFlags);",
		    "TextRender()->SetFontPreset(PreviousFontPreset);",
		    "TextRender()->TextOutlineColor(PreviousTextOutlineColor);",
		    "TextRender()->TextSelectionColor(PreviousTextSelectionColor);",
		    "TextRender()->TextColor(PreviousTextColor);",
	    })
		EXPECT_NE(Render.find(pState), std::string::npos) << pState;

	EXPECT_LT(Render.find("Ui()->SetEnabled(false);"), Render.find("TextRender()->SetRenderFlags(PreviousRenderFlags);"));
}

TEST(QmNewUiMenuBranches, DynamicIslandPreLayoutConsumesTheSameConditionalRows)
{
	const std::string Source = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogHud.cpp");
	const size_t FactoryPos = Source.find("FSettingsCardPreLayoutInput BuildHudPreLayoutInput(");
	ASSERT_NE(FactoryPos, std::string::npos);
	const std::string PreLayoutSource = Source.substr(FactoryPos);
	const size_t DynamicIslandPos = PreLayoutSource.find("case EQmModuleId::DynamicIsland:");
	const size_t PlayerStatsPos = PreLayoutSource.find("case EQmModuleId::PlayerStats:", DynamicIslandPos);
	ASSERT_NE(DynamicIslandPos, std::string::npos);
	ASSERT_NE(PlayerStatsPos, std::string::npos);
	const std::string DynamicIsland = PreLayoutSource.substr(DynamicIslandPos, PlayerStatsPos - DynamicIslandPos);

	EXPECT_NE(DynamicIsland.find("g_Config.m_QmHudIslandUseOriginalStyle"), std::string::npos);
	EXPECT_NE(DynamicIsland.find("g_Config.m_QmHudIslandShowTeam"), std::string::npos);
	EXPECT_EQ(DynamicIsland.find("ConsumeQmHudRow(Content); // edge margin"), std::string::npos);
	EXPECT_NE(DynamicIsland.find("ResolveSettingsColorRowLayout(Content, Metrics, false)"), std::string::npos);
	EXPECT_NE(DynamicIsland.find("if(!g_Config.m_QmHudIslandUseOriginalStyle)"), std::string::npos);
	// 开关倒计时总开关关闭时不再消耗位置行，与 ResolveQmHudDynamicIslandHeight 的行数保持一致。
	EXPECT_NE(DynamicIsland.find("&g_Config.m_QmSwitchCountdown, &g_Config.m_QmSwitchCountdown"), std::string::npos);
	EXPECT_NE(DynamicIsland.find("if(!g_Config.m_QmSwitchCountdown)"), std::string::npos);
	// 位置开关的行消耗与渲染路径共用卡片目录权威定义的同一组复选框 id（原先的独立位置标题行已移除）。
	EXPECT_NE(DynamicIsland.find("ToggleQmHudCountdownLocation(pMenus, Content, LineHeight, LineSpacing, SwitchCountdownFollowTeeId(), &FollowTee)"), std::string::npos);
	EXPECT_NE(DynamicIsland.find("ToggleQmHudCountdownLocation(pMenus, Content, LineHeight, LineSpacing, SwitchCountdownMediaIslandId(), &MediaIsland)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, GeneralSettingsListsShareSelectedAndHoveredBackgroundTokens)
{
	const std::string Tokens = ReadTextFile("src/game/client/QmUi/UiTokens.h");
	const std::string ListboxHeader = ReadTextFile("src/game/client/ui_listbox.h");
	const std::string Listbox = ReadTextFile("src/game/client/ui_listbox.cpp");
	const std::string Menus = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Settings = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Theme = FunctionBody(Menus, "void CMenus::RenderThemeSelection(");
	const std::string Language = FunctionBody(Settings, "bool CMenus::RenderLanguageSelection(");
	ASSERT_FALSE(Theme.empty());
	ASSERT_FALSE(Language.empty());

	EXPECT_NE(Tokens.find("LIST_ITEM_SELECTED{1.0f, 1.0f, 1.0f, 0.14f}"), std::string::npos);
	EXPECT_NE(Tokens.find("LIST_ITEM_HOVER{1.0f, 1.0f, 1.0f, 0.08f}"), std::string::npos);
	EXPECT_NE(ListboxHeader.find("void SetItemColors(ColorRGBA SelectedActive, ColorRGBA SelectedInactive, ColorRGBA Hovered)"), std::string::npos);
	EXPECT_NE(Listbox.find("m_SelectedItemActiveColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f);"), std::string::npos);
	EXPECT_NE(Listbox.find("Ui()->ScaleBackgroundAlpha(m_Active ? m_SelectedItemActiveColor : m_SelectedItemInactiveColor)"), std::string::npos);
	EXPECT_NE(Theme.find("s_ListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_HOVER);"), std::string::npos);
	const size_t LanguageSelectedCondition = Language.find("if(Selected)");
	const size_t LanguageSelected = Language.find("DrawRoundedSurface(Ui(), ItemRect, Ui()->ScaleBackgroundAlpha(ui_token::color::LIST_ITEM_SELECTED)");
	const size_t LanguageHoveredCondition = Language.find("if(Ui()->HotItem() == pRowId)");
	const size_t LanguageHovered = Language.find("DrawRoundedSurface(Ui(), ItemRect, Ui()->ScaleBackgroundAlpha(ui_token::color::LIST_ITEM_HOVER)");
	ASSERT_NE(LanguageSelectedCondition, std::string::npos);
	ASSERT_NE(LanguageSelected, std::string::npos);
	ASSERT_NE(LanguageHoveredCondition, std::string::npos);
	ASSERT_NE(LanguageHovered, std::string::npos);
	EXPECT_LT(LanguageSelectedCondition, LanguageSelected);
	EXPECT_LT(LanguageHoveredCondition, LanguageHovered);
	EXPECT_LT(LanguageSelected, LanguageHovered);
}

TEST(QmNewUiMenuBranches, DefaultUiSurfacesUseBlackThirtyPercent)
{
	const std::string QmConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables.h");

	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_COL(QmUiColor, qm_ui_color, 0x000000"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_COL(QmMapBrowserColor, qm_map_browser_color, 0x000000"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_COL(QmScoreboardColor, qm_scoreboard_color, 0x000000"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_INT(QmUiOpacity, qm_ui_opacity, 30"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_INT(QmUiCardOpacity, qm_ui_card_opacity, 30"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_INT(QmMapBrowserOpacity, qm_map_browser_opacity, 30"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_INT(QmScoreboardOpacity, qm_scoreboard_opacity, 30"), std::string::npos);

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(UiColor, ui_color, 0x4D000000"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(ClMenuPanelColor, cl_menu_panel_color, 0x000000"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(ClMenuPanelOpacity, cl_menu_panel_opacity, 30"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(ClMenuPanelElevatedOpacity, cl_menu_panel_elevated_opacity, 30"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(ClSettingsTabbarOpacity, cl_settings_tabbar_opacity, 30"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmFeatureDefaultsAreDisabledExceptRequiredDefaults)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::regex BinaryQmDefaultOn(R"(MACRO_CONFIG_INT\([^,]+,\s*qm_[^,]+,\s*1,\s*0,\s*1,)");
	const char *apIntentionalDefaultOn[] = {
		"QmImeAutoManage",
		"QmNewIme",
		"QmUiListEntryAnimations",
		"QmUiCardHeightAnimations",
		"QmUiCardReflowAnimations",
		"QmUiCardBorders",
		"QmUiIconWeight",
		"QmNameplateCoordX",
		"QmAutoMargin",
		"QmSkinChangeTransition",
		"QmWeaponTrajectoryGun",
		"QmGoresAutoWeaponSwitch",
		"QmGoresDisableIfWeapons",
		"QmSkinQueueEnabled",
		"QmDummySkinQueueEnabled",
		"QmChatSaveDraft",
		"QmChatHideSystemPrefix",
		"QmSmtcEnable",
		"QmNeteaseHookEnable",
		"QmSmtcShowHud",
		"QmAutoUpdate",
		"QmSwitchCountdown",
		"QmMessageMerge",
	};
	std::istringstream Lines(ConfigSource);
	std::string Line;
	while(std::getline(Lines, Line))
	{
		bool IntentionalDefaultOn = false;
		for(const char *pName : apIntentionalDefaultOn)
		{
			if(Line.find(std::string("MACRO_CONFIG_INT(") + pName + ",") != std::string::npos)
			{
				IntentionalDefaultOn = true;
				break;
			}
		}
		EXPECT_FALSE(std::regex_search(Line, BinaryQmDefaultOn) && !IntentionalDefaultOn) << Line;
	}

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmUiMotionLevel, qm_ui_motion_level, 2, 0, 2"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponTrajectory, qm_weapon_trajectory, 1, 0, 2"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmVoiceNoiseSuppressEnable, qm_voice_noise_suppress_enable, 0, 0, 2"), std::string::npos);
	EXPECT_EQ(ConfigSource.find("MACRO_CONFIG_INT(QmVoiceNoiseSuppressEnable, qm_voice_noise_suppress_enable, 2, 0, 2"), std::string::npos);
}

TEST(QmNewUiMenuBranches, WeaponTrajectoryExposesDefaultOnPistolGuideToggle)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string MenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string CardRegistrySource = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string WeaponTrajectoryBody = FunctionBody(MenusSource, "void CMenus::RenderQmFunctionWeaponTrajectoryContent(");
	// 卡片高度测量已搬到功能卡片目录的 MeasureFunctionCardHeight。
	const std::string FunctionCatalog = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogFunction.cpp");
	const std::string FunctionMeasure = FunctionBody(FunctionCatalog, "float MeasureFunctionCardHeight(");

	ASSERT_FALSE(WeaponTrajectoryBody.empty());
	ASSERT_FALSE(FunctionMeasure.empty());
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponTrajectoryGun, qm_weapon_trajectory_gun, 1, 0, 1"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponTrajectoryNinja, qm_weapon_trajectory_ninja, 0, 0, 1"), std::string::npos);
	EXPECT_NE(WeaponTrajectoryBody.find("RenderQmFunctionCheckbox(&g_Config.m_QmWeaponTrajectoryGun, \"qmclient-weapon-trajectory-gun\", Localize(\"Pistol guide line\")"), std::string::npos);
	EXPECT_NE(WeaponTrajectoryBody.find("RenderQmFunctionCheckbox(&g_Config.m_QmWeaponTrajectoryNinja, \"qmclient-weapon-trajectory-ninja\", Localize(\"Predict ninja path\")"), std::string::npos);
	EXPECT_NE(FunctionMeasure.find("case EQmModuleId::WeaponTrajectory: return g_Config.m_QmWeaponTrajectory == 0 ? Row() : Row() * 6.0f;"), std::string::npos);
	EXPECT_NE(CardRegistrySource.find("手枪辅助线 shouqiang fuzhuxian pistol guide line"), std::string::npos);
	EXPECT_NE(CardRegistrySource.find("预测忍者路径 yuce renzhe lujing predict ninja path"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmDefaultOffMigrationKeepsExplicitLegacyValues)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config.cpp");
	const std::string ClientSource = ReadTextFile("src/engine/client/client.cpp");
	const std::string DomainSource = ReadTextFile("src/engine/shared/config_domains.h");
	const std::string IncludeSource = ReadTextFile("src/engine/shared/config_includes.h");

	EXPECT_NE(IncludeSource.find("SET_CONFIG_DOMAIN(ConfigDomain::QMCLIENT)\n#include \"config_variables_qmclient.h\""), std::string::npos);
	EXPECT_NE(DomainSource.find("CONFIG_DOMAIN(QMCLIENT, \"qmclient/settings.cfg\", nullptr, nullptr, true)"), std::string::npos);
	EXPECT_NE(ClientSource.find("pConfigManager->Init();"), std::string::npos);
	EXPECT_NE(ClientSource.find("if(!pConsole->ExecuteFile(pConfigPath, IConsole::CLIENT_ID_UNSPECIFIED))"), std::string::npos);
	EXPECT_LT(ClientSource.find("pConfigManager->Init();"), ClientSource.find("if(!pConsole->ExecuteFile(pConfigPath, IConsole::CLIENT_ID_UNSPECIFIED))"));
	EXPECT_NE(ConfigSource.find("pVariable->m_ConfigDomain == ConfigDomain && (pVariable->m_Flags & CFGFLAG_SAVE) != 0 && !pVariable->IsDefault()"), std::string::npos);
	EXPECT_NE(ConfigSource.find("std::vector<char> vLineBuf(pVariable->MaxSerializedSize());"), std::string::npos);
	EXPECT_NE(ConfigSource.find("pVariable->Serialize(vLineBuf.data(), vLineBuf.size());"), std::string::npos);
	EXPECT_NE(ConfigSource.find("WriteLine(vLineBuf.data(), ConfigDomain);"), std::string::npos);
	EXPECT_EQ(ConfigSource.find("Reset(\"qm_"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SkinTransitionAnimationToggleOwnsAdvancedControls)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string MenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string CardRegistrySource = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string GameClientSource = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string PlayersSource = ReadTextFile("src/game/client/components/players.cpp");
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string LanguageSource = ReadTextFile("data/languages/simplified_chinese.txt");
	const std::string SkinSource = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogSkin.cpp");
	const std::string AppearanceContent = FunctionBody(SkinSource, "void CMenus::RenderQmVisualSkinAppearanceContent(");
	const std::string SkinTransitionContent = FunctionBody(SkinSource, "void CMenus::RenderQmVisualSkinTransitionContent(");

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmSkinChangeTransition, qm_skin_change_transition, 1, 0, 1"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmSkinChangeTransitionScope, qm_skin_change_transition_scope, 1, 0, 2"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmSkinChangeTransitionEasing, qm_skin_change_transition_easing"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmSkinChangeTransitionIntensity, qm_skin_change_transition_intensity"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmCycleTeeHueDummy, qm_cycle_tee_hue_dummy, 0, 0, 1"), std::string::npos);
	EXPECT_NE(CardRegistrySource.find("\"qm:skin_transition\", \"visual\", ECardColumn::Left, 2, \"Skin transition animation\", \"皮肤切换 pifu qiehuan skin transition"), std::string::npos);
	EXPECT_NE(AppearanceContent.find("RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmCycleTeeHueDummy, \"Also apply to dummy\""), std::string::npos);
	EXPECT_NE(PlayersSource.find("LocalDummy == 0 || g_Config.m_QmCycleTeeHueDummy != 0"), std::string::npos);
	EXPECT_NE(PlayersSource.find("LocalDummy != 0 ? g_Config.m_ClDummyUseCustomColor != 0 : g_Config.m_ClPlayerUseCustomColor != 0"), std::string::npos);

	ASSERT_FALSE(AppearanceContent.empty());
	ASSERT_FALSE(SkinTransitionContent.empty());
	EXPECT_EQ(MenusSource.find("void CMenus::RenderQmVisualSkinTransitionContent("), std::string::npos);
	EXPECT_EQ(AppearanceContent.find("m_QmSkinChangeTransition"), std::string::npos);
	EXPECT_EQ(SkinTransitionContent.find("m_QmSkinOutline"), std::string::npos);
	EXPECT_EQ(SkinTransitionContent.find("RenderSkinQueueRotationRow"), std::string::npos);
	EXPECT_NE(SkinTransitionContent.find("m_QmSkinChangeTransition"), std::string::npos);

	const size_t Toggle = SkinTransitionContent.find("DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, &g_Config.m_QmSkinChangeTransition");
	ASSERT_NE(Toggle, std::string::npos);
	const size_t AdvancedIf = SkinTransitionContent.find("if(!g_Config.m_QmSkinChangeTransition)\n\t\treturn;", Toggle);
	const size_t TypeLabel = SkinTransitionContent.find("RenderDropDown(\"qmclient-skin-transition-type\", Localize(\"Skin transition type\")", Toggle);
	const size_t ScopeLabel = SkinTransitionContent.find("RenderDropDown(\"qmclient-skin-transition-range\", Localize(\"Animation range\")", Toggle);
	const size_t DurationLabel = SkinTransitionContent.find("Localize(\"Skin transition duration\")", Toggle);
	const size_t EasingLabel = SkinTransitionContent.find("RenderDropDown(\"qmclient-skin-transition-easing\", Localize(\"Skin transition easing\")", Toggle);
	const size_t IntensityLabel = SkinTransitionContent.find("Localize(\"Skin transition intensity\")", Toggle);
	ASSERT_NE(AdvancedIf, std::string::npos);
	ASSERT_NE(TypeLabel, std::string::npos);
	ASSERT_NE(ScopeLabel, std::string::npos);
	ASSERT_NE(DurationLabel, std::string::npos);
	ASSERT_NE(EasingLabel, std::string::npos);
	ASSERT_NE(IntensityLabel, std::string::npos);
	EXPECT_LT(Toggle, AdvancedIf);
	EXPECT_LT(AdvancedIf, TypeLabel);
	EXPECT_LT(TypeLabel, ScopeLabel);
	EXPECT_LT(ScopeLabel, DurationLabel);
	EXPECT_LT(DurationLabel, EasingLabel);
	EXPECT_LT(EasingLabel, IntensityLabel);
	EXPECT_NE(SkinTransitionContent.find("s_SkinTransitionScopeDropDownNames = {Localize(\"Self only\"), Localize(\"Local\"), Localize(\"All players\")};"), std::string::npos);
	EXPECT_NE(SkinTransitionContent.find("RenderDropDown(\"qmclient-skin-transition-range\", Localize(\"Animation range\"), &g_Config.m_QmSkinChangeTransitionScope, 2"), std::string::npos);

	EXPECT_NE(GameClientSource.find("QM_SKIN_CHANGE_TRANSITION_SCOPE_OWN"), std::string::npos);
	EXPECT_NE(GameClientSource.find("bool CGameClient::ShouldRunSkinChangeTransition(int ClientId) const"), std::string::npos);
	EXPECT_NE(GameClientSource.find("if(!g_Config.m_QmSkinChangeTransition || g_Config.m_QmSkinChangeTransitionMs <= 0)"), std::string::npos);
	EXPECT_NE(GameClientSource.find("if(!g_Config.m_QmSkinChangeTransition || g_Config.m_QmSkinChangeTransitionMs <= 0 || !m_SkinTransitionStart.has_value()"), std::string::npos);
	EXPECT_NE(GameClientSource.find("m_pGameClient->ShouldRunSkinChangeTransition(m_ClientId)"), std::string::npos);
	EXPECT_NE(SettingsSource.find("if(!g_Config.m_QmSkinChangeTransition || g_Config.m_QmSkinChangeTransitionMs <= 0)"), std::string::npos);
	EXPECT_NE(SettingsSource.find("if(!g_Config.m_QmSkinChangeTransition || g_Config.m_QmSkinChangeTransitionMs <= 0 || !m_StartTime.has_value()"), std::string::npos);
	EXPECT_NE(LanguageSource.find("Skin transition animation\n== 皮肤切换动画"), std::string::npos);
}

TEST(QmNewUiMenuBranches, WeaponAnimationAdvancedControlsAreConfigurable)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string PlayersSource = ReadTextFile("src/game/client/components/players.cpp");
	const std::string MenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string RegistrySource = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponSwitchAnimDurationMs, qm_weapon_switch_anim_duration_ms, 300"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponSwitchAnimDistance, qm_weapon_switch_anim_distance, 40"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponSwitchAnimRotation, qm_weapon_switch_anim_rotation, 360"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponSwitchAnimEasing, qm_weapon_switch_anim_easing"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponReloadAnim, qm_weapon_reload_anim"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponReloadAnimProbability, qm_weapon_reload_anim_probability"), std::string::npos);

	EXPECT_NE(PlayersSource.find("g_Config.m_QmWeaponSwitchAnimDurationMs"), std::string::npos);
	EXPECT_NE(PlayersSource.find("g_Config.m_QmWeaponSwitchAnimDistance"), std::string::npos);
	EXPECT_NE(PlayersSource.find("g_Config.m_QmWeaponSwitchAnimRotation"), std::string::npos);
	EXPECT_NE(PlayersSource.find("g_Config.m_QmWeaponSwitchAnimEasing"), std::string::npos);

	const std::string WeaponAnimationContent = FunctionBody(MenusSource, "void CMenus::RenderQmVisualWeaponAnimationContent(");
	const size_t SwitchToggle = WeaponAnimationContent.find("RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponSwitchAnim");
	const size_t ReloadToggle = WeaponAnimationContent.find("RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponReloadAnim");
	const size_t ReloadProbability = WeaponAnimationContent.find("RenderValue(\"qmclient-weapon-reload-animation-probability\", \"Weapon reload animation probability\"");
	const size_t SharedControls = WeaponAnimationContent.find("if(!g_Config.m_QmWeaponSwitchAnim && !g_Config.m_QmWeaponReloadAnim)");
	const size_t SwitchControls = WeaponAnimationContent.find("if(!g_Config.m_QmWeaponSwitchAnim)", SharedControls);
	ASSERT_NE(SwitchToggle, std::string::npos);
	ASSERT_NE(ReloadToggle, std::string::npos);
	ASSERT_NE(ReloadProbability, std::string::npos);
	ASSERT_NE(SharedControls, std::string::npos);
	ASSERT_NE(SwitchControls, std::string::npos);
	EXPECT_LT(SwitchToggle, ReloadToggle);
	EXPECT_LT(ReloadToggle, ReloadProbability);
	EXPECT_LT(ReloadProbability, SharedControls);
	EXPECT_LT(SharedControls, SwitchControls);
	EXPECT_NE(WeaponAnimationContent.find("RenderValue(\"qmclient-weapon-switch-duration\", \"Weapon switch duration\"", SwitchControls), std::string::npos);
	EXPECT_NE(WeaponAnimationContent.find("RenderValue(\"qmclient-weapon-switch-distance\", \"Weapon switch distance\"", SwitchControls), std::string::npos);
	EXPECT_NE(WeaponAnimationContent.find("RenderValue(\"qmclient-weapon-switch-rotation\", \"Weapon switch rotation\"", SwitchControls), std::string::npos);
	EXPECT_NE(WeaponAnimationContent.find("Localize(\"Weapon switch easing\")", SwitchControls), std::string::npos);

	// 武器动画卡片的高度/重测版本/预布局输入都在视觉卡片目录里：
	// 高度决定展开后的行数，重测版本里装填动画占 2u，预布局必须命中同一个装填动画开关。
	const std::string VisualCatalog = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogVisual.cpp");
	const std::string VisualMeasure = FunctionBody(VisualCatalog, "float MeasureVisualCardHeight(");
	const std::string VisualRevision = FunctionBody(VisualCatalog, "uint64_t MeasureVisualCardRevision(");
	const std::string VisualPreLayout = FunctionBody(VisualCatalog, "FSettingsCardPreLayoutInput BuildVisualPreLayoutInput(");
	ASSERT_FALSE(VisualMeasure.empty());
	ASSERT_FALSE(VisualRevision.empty());
	ASSERT_FALSE(VisualPreLayout.empty());
	EXPECT_NE(VisualMeasure.find("ResolveQmVisualWeaponAnimationHeight(Metrics, g_Config.m_QmWeaponSwitchAnim != 0, g_Config.m_QmWeaponReloadAnim != 0)"), std::string::npos);
	EXPECT_NE(VisualRevision.find("(g_Config.m_QmWeaponReloadAnim ? 2u : 0u)"), std::string::npos);
	EXPECT_NE(VisualPreLayout.find("HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponReloadAnim, &g_Config.m_QmWeaponReloadAnim)"), std::string::npos);
	EXPECT_NE(RegistrySource.find("装填动画 zhuangtian donghua reload animation"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ProcessPriorityAndImeHaveVisibleSettings)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string ClientSource = ReadTextFile("src/engine/client/client.cpp");
	const std::string MenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmProcessHighPriority, qm_process_high_priority, 0, 0, 1"), std::string::npos);
	EXPECT_NE(ClientSource.find("ApplyProcessPriorityConfig();"), std::string::npos);
	EXPECT_NE(ClientSource.find("m_pConsole->Chain(\"qm_process_high_priority\", ConchainProcessHighPriority, this);"), std::string::npos);
	const std::string MiniFeaturesBody = FunctionBody(MenusSource, "void CMenus::RenderQmFunctionMiniFeaturesContent(");
	ASSERT_FALSE(MiniFeaturesBody.empty());
	// 普通开关行表在卡片目录 FunctionMetrics；渲染函数只负责遍历与两个特殊行。
	const std::string FunctionMetrics = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogFunctionMetrics.h");
	EXPECT_NE(FunctionMetrics.find("{&g_Config.m_QmProcessHighPriority, Localizable(\"High process priority\"), &g_Config.m_QmProcessHighPriority},"), std::string::npos);
	EXPECT_NE(FunctionMetrics.find("{&g_Config.m_QmImeAutoManage, Localizable(\"Auto manage IME while typing\"), &g_Config.m_QmImeAutoManage},"), std::string::npos);
	EXPECT_NE(MiniFeaturesBody.find("&g_Config.m_QmNewIme"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ConfigPageLocalizesVariableHelpText)
{
	const std::string ConfigHeader = ReadTextFile("src/engine/shared/config.h");
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config.cpp");
	const std::string TClientMenusSource = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");

	EXPECT_NE(ConfigHeader.find("const char *m_pHelpLocalizeKey;"), std::string::npos);
	EXPECT_NE(ConfigSource.find("SConfigVariable::VAR_INT, Flags, pHelp, Desc"), std::string::npos);
	EXPECT_NE(ConfigSource.find("SConfigVariable::VAR_COLOR, Flags, pHelp, Desc"), std::string::npos);
	EXPECT_NE(ConfigSource.find("SConfigVariable::VAR_STRING, Flags, pHelp, Desc"), std::string::npos);
	EXPECT_NE(TClientMenusSource.find("BuildLocalizedConfigHelpText"), std::string::npos);
	EXPECT_NE(TClientMenusSource.find("pVar->m_pHelpLocalizeKey ? pVar->m_pHelpLocalizeKey"), std::string::npos);
	EXPECT_NE(TClientMenusSource.find("Localize(pHelpKey)"), std::string::npos);
	EXPECT_NE(TClientMenusSource.find("s_CachedConfigLanguageHash"), std::string::npos);
	EXPECT_NE(TClientMenusSource.find("str_quickhash(g_Config.m_ClLanguagefile)"), std::string::npos);
	EXPECT_EQ(TClientMenusSource.find("Ui()->DoLabel(&Help, pVar->m_pHelp ? pVar->m_pHelp : \"\""), std::string::npos);
}

TEST(QmNewUiMenuBranches, EmoticonShadowHasConfigRenderPassAndVisualToggle)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string PlayersSource = ReadTextFile("src/game/client/components/players.cpp");
	const std::string EmoticonSource = ReadTextFile("src/game/client/components/emoticon.cpp");
	const std::string RenderPlayerBody = FunctionBody(PlayersSource, "void CPlayers::RenderPlayer(");
	const std::string EmoticonRenderBody = FunctionBody(EmoticonSource, "void CEmoticon::OnRender()");
	const std::string EmoticonItemsBody = BlockBodyAfter(EmoticonRenderBody, "for(int Emote = 0; Emote < NUM_EMOTICONS; Emote++)");
	const auto CountOccurrences = [](const std::string &Text, const char *pNeedle) {
		int Count = 0;
		size_t Pos = 0;
		while((Pos = Text.find(pNeedle, Pos)) != std::string::npos)
		{
			++Count;
			Pos += str_length(pNeedle);
		}
		return Count;
	};

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmEmoticonShadow, qm_emoticon_shadow, 0, 0, 1"), std::string::npos);
	EXPECT_NE(RenderPlayerBody.find("EmoticonShadowOpacity"), std::string::npos);
	EXPECT_NE(RenderPlayerBody.find("EmoticonShadowOffsetX"), std::string::npos);
	EXPECT_EQ(CountOccurrences(RenderPlayerBody, "if(g_Config.m_QmEmoticonShadow)"), 3);
	EXPECT_EQ(CountOccurrences(RenderPlayerBody, "Graphics()->SetColor(0.0f, 0.0f, 0.0f"), 3);
	EXPECT_NE(RenderPlayerBody.find("EmoticonShadowOffsetX * h"), std::string::npos);
	EXPECT_NE(RenderPlayerBody.find("EmoticonShadowOffsetY * h, h * EmoticonScale, h * EmoticonScale"), std::string::npos);
	EXPECT_NE(RenderPlayerBody.find("Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);\n\t\tGraphics()->RenderQuadContainerAsSprite"), std::string::npos);
	EXPECT_NE(RenderPlayerBody.find("Graphics()->SetColor(1.0f, 1.0f, 1.0f, a * Alpha);\n\t\t\tGraphics()->RenderQuadContainerAsSprite"), std::string::npos);
	EXPECT_NE(EmoticonRenderBody.find("EmoticonSelectorShadowOpacity"), std::string::npos);
	EXPECT_NE(EmoticonRenderBody.find("if(g_Config.m_QmEmoticonShadow)"), std::string::npos);
	EXPECT_NE(EmoticonRenderBody.find("Graphics()->SetColor(0.0f, 0.0f, 0.0f, EmoticonSelectorShadowOpacity);"), std::string::npos);
	EXPECT_NE(EmoticonRenderBody.find("ScreenCenter.x + Nudge.x + EmoticonSelectorShadowOffsetX"), std::string::npos);
	EXPECT_EQ(CountOccurrences(EmoticonItemsBody, "Graphics()->QuadsBegin();"), 2);
	const size_t ShadowBranch = EmoticonItemsBody.find("if(g_Config.m_QmEmoticonShadow)");
	ASSERT_NE(ShadowBranch, std::string::npos);
	const size_t ShadowClear = EmoticonItemsBody.find("Graphics()->TextureClear();", ShadowBranch);
	const size_t ShadowBegin = EmoticonItemsBody.find("Graphics()->QuadsBegin();", ShadowBranch);
	ASSERT_NE(ShadowClear, std::string::npos);
	ASSERT_NE(ShadowBegin, std::string::npos);
	EXPECT_LT(ShadowClear, ShadowBegin);
	const std::string SkinSource = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogSkin.cpp");
	const std::string AppearanceContent = FunctionBody(SkinSource, "void CMenus::RenderQmVisualSkinAppearanceContent(");
	EXPECT_NE(AppearanceContent.find("RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmEmoticonShadow"), std::string::npos);
	EXPECT_NE(SkinSource.find("Localize(\"Emoticon shadow\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NameplateOthersModeSuppressesLocalIdentityRows)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string UpdateCoordXAlignFrameState = FunctionBody(Source, "void CNamePlates::UpdateCoordXAlignFrameState");
	const std::string RenderNamePlateGame = FunctionBody(Source, "void CNamePlates::RenderNamePlateGame");

	EXPECT_EQ(UpdateCoordXAlignFrameState.find("FrameState.m_LocalRoundedX = RoundCoordToCentitiles(GameClient()->m_LocalCharacterPos.x / 32.0f);\n\tFrameState.m_LocalAligned = true;"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("std::array<SCoordXAlignReference, NUM_DUMMIES> aLocalRefs{};"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("if(aLocalRefs[i].m_ClientId == ClientId)"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("CoordXAlignState.m_ReferenceClientId != ReferenceClientId"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("FrameState.m_aClientAligned[ReferenceClientId] = true;"), std::string::npos);
	EXPECT_EQ(UpdateCoordXAlignFrameState.find("if(ClientId == FrameState.m_LocalClientId"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("if(CoordXAlignState.m_Aligned)\n\t\t{"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("FrameState.m_LocalAligned = true;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("const bool IsAnyLocalClient = GameClient()->IsLocalClientId(ClientId);"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("IsAnyLocalClient &&\n\t\tm_pData->m_CoordXAlignFrame.m_aClientAligned[ClientId];"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("CoordXAlignState.m_Aligned || m_pData->m_CoordXAlignFrame.m_LocalAligned"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("IsLocalClient &&\n\t\tm_pData->m_CoordXAlignFrame.m_LocalAligned"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("const bool OwnNameplateScopeVisible"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowName = ShouldShowQmNameplateName("), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("g_Config.m_QmNameplateShowScope,"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("g_Config.m_QmNameplateOwnScope,"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("g_Config.m_QmNameplateOthersScope,"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("Data.m_ShowName = pPlayerInfo->m_Local ? g_Config.m_ClNamePlatesOwn : g_Config.m_ClNamePlates;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowClientId = Data.m_ShowName && (g_Config.m_Debug || g_Config.m_ClNamePlatesIds) && !HideIdentity;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowClan = Data.m_ShowName && g_Config.m_ClNamePlatesClan && !HideIdentity;"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("const bool NameplateScopeAllowsCoords"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("const bool CoordModuleAllowsCoords = IsAnyLocalClient ? g_Config.m_QmNameplateCoordsOwn : g_Config.m_QmNameplateCoords;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("const bool ShowLocalAlignedCoordX = CoordModuleAllowsCoords && CoordXAlignHintEnabled && LocalCoordXAligned;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowCoordX = (CoordModuleAllowsCoords && g_Config.m_QmNameplateCoordX != 0) || ShowLocalAlignedCoordX;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowCoordY = CoordModuleAllowsCoords && g_Config.m_QmNameplateCoordY != 0;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowCoords = CoordModuleAllowsCoords || ShowLocalAlignedCoordX;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("TrackCoordXAlign &&\n\t\tGameClient()->m_Snap.m_LocalClientId >= 0"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_CoordXAligned = IsAnyLocalClient ? LocalCoordXAligned : CoordXAlignState.m_Aligned;"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("!IsAnyLocalClient &&\n\t\tGameClient()->m_Snap.m_LocalClientId >= 0"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("if(Data.m_ShowName && !HideIdentity && g_Config.m_TcWarList && g_Config.m_TcWarListShowClan"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_Local = pPlayerInfo->m_Local;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, PlayerTitlePrecedesInlineClientIdAndNameWithoutOverridingIdSettings)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string AddNameRow = FunctionBody(Source, "void AddNameRow(");
	const std::string RenderNamePlateGame = FunctionBody(Source, "void CNamePlates::RenderNamePlateGame");

	const size_t FriendMark = AddNameRow.find("AddPart<CNamePlatePartFriendMark>(This);");
	const size_t Developer = AddNameRow.find("AddPart<CNamePlatePartTitle>(This);");
	const size_t InlineClientId = AddNameRow.find("AddPart<CNamePlatePartClientId>(This, false);");
	const size_t Name = AddNameRow.find("AddPart<CNamePlatePartName>(This);");
	ASSERT_NE(FriendMark, std::string::npos);
	ASSERT_NE(Developer, std::string::npos);
	ASSERT_NE(InlineClientId, std::string::npos);
	ASSERT_NE(Name, std::string::npos);
	EXPECT_LT(FriendMark, Developer);
	EXPECT_LT(Developer, InlineClientId);
	EXPECT_LT(InlineClientId, Name);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowClientId = Data.m_ShowName && (g_Config.m_Debug || g_Config.m_ClNamePlatesIds) && !HideIdentity;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NameplatePreviewShowsPlayerStrongHookMarker)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string BuildPreviewData = FunctionBody(Source, "static void BuildNamePlatePreviewData");

	EXPECT_NE(BuildPreviewData.find("const bool PreviewIsCurrentChar = DummyIdx == g_Config.m_ClDummy;"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("if(DummyIdx == g_Config.m_ClDummy)"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("Data.m_HookStrongWeakState = EHookStrongWeakState::NEUTRAL;"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("Data.m_HookStrongWeakState = Data.m_HookStrongWeakId == 2 ? EHookStrongWeakState::STRONG : EHookStrongWeakState::WEAK;"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("Data.m_ShowHookStrongWeak = NameplateScopeAllowsPreview && (Data.m_ShowHookStrongWeakId || (g_Config.m_ClNamePlatesStrong > 0 && ShouldShowQmHookStrongWeakScope(g_Config.m_QmNameplateHookStrongWeakScope, true, false, false)));"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("Data.m_ShowHookStrongWeak = NameplateScopeAllowsPreview && g_Config.m_ClNamePlatesStrong > 0 && ShouldShowQmHookStrongWeakScope(g_Config.m_QmNameplateHookStrongWeakScope, false, Strong, Weak);"), std::string::npos);
}

TEST(QmNameplateIndicatorLayout, HookToggleAndSizeDoNotMoveKeys)
{
	for(const float HookHeight : {0.0f, 13.0f, 29.0f, 43.0f})
	{
		SCOPED_TRACE(HookHeight);
		const auto aBaselines = QmNameplateCoreRowBaselines({24.0f, 20.0f, HookHeight, 29.0f, 36.0f});
		EXPECT_FLOAT_EQ(aBaselines[static_cast<size_t>(ENameplateCoreRow::HOOK)], -44.0f);
		EXPECT_FLOAT_EQ(aBaselines[static_cast<size_t>(ENameplateCoreRow::KEYS)], -102.0f);
		// 坐标行继续沿用现有堆叠行为，本次只解除方向键对强弱钩高度的依赖。
		EXPECT_FLOAT_EQ(aBaselines[static_cast<size_t>(ENameplateCoreRow::COORDS)], -44.0f - HookHeight);

		const auto aOnlyIndicators = QmNameplateCoreRowBaselines({0.0f, 0.0f, HookHeight, 0.0f, 36.0f});
		EXPECT_FLOAT_EQ(aOnlyIndicators[static_cast<size_t>(ENameplateCoreRow::HOOK)], 0.0f);
		EXPECT_FLOAT_EQ(aOnlyIndicators[static_cast<size_t>(ENameplateCoreRow::KEYS)], -29.0f);
	}
}

TEST(QmNameplateIndicatorLayout, KeysToggleAndSizeDoNotMoveHook)
{
	for(const float KeysHeight : {0.0f, 12.0f, 36.0f, 57.0f})
	{
		SCOPED_TRACE(KeysHeight);
		const auto aBaselines = QmNameplateCoreRowBaselines({24.0f, 20.0f, 29.0f, 29.0f, KeysHeight});
		EXPECT_FLOAT_EQ(aBaselines[static_cast<size_t>(ENameplateCoreRow::HOOK)], -44.0f);
		EXPECT_FLOAT_EQ(aBaselines[static_cast<size_t>(ENameplateCoreRow::KEYS)], -102.0f);
	}
}

TEST(QmNameplateIndicatorLayout, PreviewSpaceStaysFixedAndContainsIndicatorSizeLimits)
{
	for(const float HookHeight : {0.0f, 13.0f, 29.0f, 43.0f})
	{
		for(const float KeysHeight : {0.0f, 12.0f, 36.0f, 57.0f})
		{
			const std::array<float, kNameplateCoreRowCount> aHeights = {24.0f, 20.0f, HookHeight, 29.0f, KeysHeight};
			const float Span = QmNameplatePreviewContentSpan(aHeights);
			EXPECT_FLOAT_EQ(Span, 159.0f);
			const auto aBaselines = QmNameplateCoreRowBaselines(aHeights);
			for(size_t RowIndex = 0; RowIndex < aHeights.size(); ++RowIndex)
				EXPECT_GE(Span, -aBaselines[RowIndex] + aHeights[RowIndex]);
		}
	}
}

TEST(QmNewUiMenuBranches, NameplateStrongHookRowReservesLayoutWithoutContentWidth)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string RangeSize = FunctionBody(Source, "vec2 RangeSize(");
	const std::string AddHookRow = FunctionBody(Source, "void AddHookRow(");
	const std::string RenderNamePlateGame = FunctionBody(Source, "void CNamePlates::RenderNamePlateGame");

	EXPECT_NE(Source.find("bool m_ReserveHookStrongWeakRow;"), std::string::npos);
	EXPECT_NE(Source.find("bool m_ReserveLineHeight = false;"), std::string::npos);
	EXPECT_NE(Source.find("bool ReserveLineHeight() const { return m_ReserveLineHeight; }"), std::string::npos);
	EXPECT_NE(Source.find("class CNamePlatePartHookStrongWeakRowReserve"), std::string::npos);
	EXPECT_NE(RangeSize.find("else if(Part.ReserveLineHeight())\n\t\t\t{"), std::string::npos);
	EXPECT_NE(RangeSize.find("LineSize.y = std::max(LineSize.y, Part.Size().y + Part.Padding().y);"), std::string::npos);
	EXPECT_NE(AddHookRow.find("AddPart<CNamePlatePartHookStrongWeakRowReserve>(This);"), std::string::npos);
	EXPECT_LT(AddHookRow.find("AddPart<CNamePlatePartHookStrongWeakRowReserve>(This);"), AddHookRow.find("AddPart<CNamePlatePartHookStrongWeak>(This);"));
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ReserveHookStrongWeakRow = g_Config.m_Debug || g_Config.m_ClNamePlatesStrong > 0;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowHookStrongWeak = false;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowHookStrongWeak = g_Config.m_Debug || (g_Config.m_ClNamePlatesStrong > 0 && ShouldShowQmHookStrongWeakScope(g_Config.m_QmNameplateHookStrongWeakScope, false, Strong, Weak));"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NameplatePreviewNameScopeGatesPlateExceptDirectionKeys)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string BuildPreviewData = FunctionBody(Source, "static void BuildNamePlatePreviewData");

	EXPECT_NE(BuildPreviewData.find("const bool IsOwnPreview = DummyIdx == 0;"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("const bool PreviewIsCurrentChar = DummyIdx == g_Config.m_ClDummy;"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("const bool NameplateScopeAllowsPreview = ForceNameplateScopeAll || ShouldShowQmNameplateName(g_Config.m_QmNameplateShowScope, PreviewIsCurrentChar, true);"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("const bool CoordModuleAllowsPreview = IsOwnPreview ? g_Config.m_QmNameplateCoordsOwn : g_Config.m_QmNameplateCoords;"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("Data.m_ShowName = NameplateScopeAllowsPreview;"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("Data.m_ShowClientId = Data.m_ShowName && (g_Config.m_Debug || g_Config.m_ClNamePlatesIds);"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("Data.m_ShowClan = Data.m_ShowName && g_Config.m_ClNamePlatesClan;"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("Data.m_ShowCoords = CoordModuleAllowsPreview;"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("Data.m_ShowCoordX = Data.m_ShowCoords && g_Config.m_QmNameplateCoordX != 0;"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("Data.m_ShowCoordY = Data.m_ShowCoords && g_Config.m_QmNameplateCoordY != 0;"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("case 1: // Others\n\t\tData.m_ShowDirection = !PreviewIsCurrentChar;"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("case 2: // Everyone\n\t\tData.m_ShowDirection = true;"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("case 3: // Only self\n\t\tData.m_ShowDirection = PreviewIsCurrentChar;"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("Data.m_ShowHookStrongWeakId = NameplateScopeAllowsPreview && g_Config.m_ClNamePlatesStrong == 2;"), std::string::npos);
	EXPECT_EQ(BuildPreviewData.find("Data.m_ShowHookStrongWeakId = g_Config.m_ClNamePlatesStrong == 2;"), std::string::npos);
	EXPECT_EQ(BuildPreviewData.find("Data.m_ShowName = g_Config.m_ClNamePlates || g_Config.m_ClNamePlatesOwn;"), std::string::npos);
	EXPECT_EQ(BuildPreviewData.find("Data.m_ShowDirection = NameplateScopeAllowsPreview && !IsOwnPreview;"), std::string::npos);
	EXPECT_EQ(BuildPreviewData.find("Data.m_ShowDirection = NameplateScopeAllowsPreview;"), std::string::npos);
	EXPECT_EQ(BuildPreviewData.find("Data.m_ShowDirection = NameplateScopeAllowsPreview && IsOwnPreview;"), std::string::npos);
	EXPECT_EQ(BuildPreviewData.find("Data.m_ShowDirection = g_Config.m_ClShowDirection != 0 ? true : false;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NameplatePreviewUsesFullScopeReferenceFrame)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string BuildPreviewData = FunctionBody(Source, "static void BuildNamePlatePreviewData");
	const std::string RenderNamePlatePreview = FunctionBody(Source, "void CNamePlates::RenderNamePlatePreview");

	ASSERT_FALSE(BuildPreviewData.empty());
	EXPECT_NE(Source.find("static void BuildNamePlatePreviewData(CGameClient &This, int DummyIdx, bool ForceNameplateScopeAll, CNamePlateData &Data)"), std::string::npos);
	EXPECT_NE(RenderNamePlatePreview.find("BuildNamePlatePreviewData(*GameClient(), Dummy, false, Data);"), std::string::npos);
	// 全 scope 参考框仍在：只用于行基线布局，切换预览/开关模块时行位置不漂移。
	EXPECT_NE(RenderNamePlatePreview.find("BuildNamePlatePreviewData(*GameClient(), Dummy, true, FrameData);"), std::string::npos);
	EXPECT_NE(RenderNamePlatePreview.find("NamePlate.CollectCoreRowRects(NameplateBottomMiddle, aEditorRects, pFrameNamePlate);"), std::string::npos);
	EXPECT_NE(RenderNamePlatePreview.find("DragHasRow, DragRowCenter, DragRowSize, pFrameNamePlate);"), std::string::npos);
	EXPECT_NE(RenderNamePlatePreview.find("NamePlate.Render(*GameClient(), NameplateBottomMiddle, pFrameNamePlate);"), std::string::npos);
	EXPECT_NE(Source.find("float ContentSpan(const CNamePlate *pLayoutReference = nullptr) const"), std::string::npos);
	EXPECT_NE(Source.find("LayoutCoreRowSize(const SCoreRowParts &CoreRow, const CNamePlate *pLayoutReference) const"), std::string::npos);
	EXPECT_NE(Source.find("QmNameplateCoreRowBaselines(LayoutCoreRowHeights(pLayoutReference))"), std::string::npos);
	// 拖动边界改成预览框本身：不再有 3 倍基准框，白框也就不会画到卡片外面。
	EXPECT_EQ(Source.find("NAMEPLATE_FREE_MOVE_FRAME_SCALE"), std::string::npos);
	EXPECT_EQ(Source.find("ScaleFrameAroundCenter"), std::string::npos);
	EXPECT_NE(RenderNamePlatePreview.find("const vec2 FrameMin = vec2(PreviewArea.x + NAMEPLATE_PREVIEW_FRAME_INSET, PreviewArea.y + NAMEPLATE_PREVIEW_FRAME_INSET);"), std::string::npos);
	// 铭牌与脚本体贴框底排布，框高由 MeasurePreviewAreaHeight() 按内容撑开。
	EXPECT_NE(Source.find("float CNamePlates::MeasurePreviewAreaHeight() const"), std::string::npos);
	EXPECT_NE(RenderNamePlatePreview.find("const vec2 NameplateBottomMiddle = TeeRenderPosition - vec2(0.0f, (float)g_Config.m_ClNamePlatesOffset);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NameplateGameUsesFullScopeReferenceFrame)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string RenderNamePlateGame = FunctionBody(Source, "void CNamePlates::RenderNamePlateGame");
	const std::string ResetNamePlates = FunctionBody(Source, "void CNamePlates::ResetNamePlates");

	EXPECT_NE(Source.find("CNamePlate m_aNamePlateFrameReferences[MAX_CLIENTS];"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("CNamePlate *pLayoutReference = nullptr;"), std::string::npos);
	// 自由移动参考框：只有该玩家当前画不出昵称行时才需要按参考框定行基线。
	EXPECT_NE(RenderNamePlateGame.find("if(Alpha > 0.0f && NameplateFreeMoveEnabled() && !Data.m_ShowName)"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("CNamePlateData FrameData = Data;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("FrameData.m_ShowName = true;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("const bool FrameShowLocalAlignedCoordX = CoordModuleAllowsCoords && CoordXAlignHintEnabled && LocalCoordXAligned;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("FrameData.m_ShowCoords = CoordModuleAllowsCoords || FrameShowLocalAlignedCoordX;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("CNamePlate &FrameNamePlate = m_pData->m_aNamePlateFrameReferences[ClientId];"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("FrameNamePlate.Update(*GameClient(), FrameData);"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("pLayoutReference = &FrameNamePlate;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("NamePlate.Render(*GameClient(), Position - vec2(0.0f, (float)g_Config.m_ClNamePlatesOffset), pLayoutReference);"), std::string::npos);
	EXPECT_NE(ResetNamePlates.find("for(CNamePlate &NamePlate : m_pData->m_aNamePlateFrameReferences)\n\t\tNamePlate.Reset(*GameClient());"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowDirection = !pPlayerInfo->m_Local;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowDirection = true;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowDirection = pPlayerInfo->m_Local;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, MediaIslandLyricsUsesNeteaseIntegration)
{
	const std::string HudSource = ReadTextFile("src/game/client/components/hud.cpp");
	const std::string RenderMediaIsland = FunctionBody(HudSource, "void CHud::RenderMediaIsland()");
	const std::string IntegrationSource = ReadTextFile("src/game/client/components/qmclient/netease/netease_integration.cpp");
	EXPECT_NE(RenderMediaIsland.find("GameClient()->m_NeteaseIntegration.GetCurrentLyric"), std::string::npos);
	EXPECT_EQ(RenderMediaIsland.find("m_QmLyrics"), std::string::npos);
	EXPECT_NE(IntegrationSource.find("qm_lyrics"), std::string::npos);
	EXPECT_NE(IntegrationSource.find("qm_lyrics_in_media_island"), std::string::npos);
}

TEST(QmNewUiMenuBranches, HudNotificationsKeepEdgeGeometryStableDuringSlide)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/hud_notifications/hud_notifications.cpp");
	const std::string MeasureVisibleRect = FunctionBody(Source, "CUIRect CQmHudNotifications::MeasureVisibleRect");
	const std::string RenderNotifications = FunctionBody(Source, "void CQmHudNotifications::RenderNotifications");

	EXPECT_EQ(MeasureVisibleRect.find("MaxSlideOffset"), std::string::npos);
	EXPECT_NE(MeasureVisibleRect.find("return QmHudNotifications::NotificationVisibleRect(BaseRect, MaxWidth, UsedHeight, Flow);"), std::string::npos);
	EXPECT_NE(RenderNotifications.find("Alpha = SmoothStep(ElapsedMs / (float)AnimMs);"), std::string::npos);
	EXPECT_NE(RenderNotifications.find("Alpha = 1.0f - SmoothStep((ElapsedMs - AnimMs - HoldMs) / (float)AnimMs);"), std::string::npos);
	EXPECT_NE(RenderNotifications.find("OffsetX = (1.0f - Alpha) * 14.0f * QmHudNotifications::SmallTextScale(FontSize);"), std::string::npos);
	EXPECT_EQ(RenderNotifications.find("OffsetX = (1.0f - Alpha) * 32.0f;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SpectatorSpecTeeDoesNotFallbackToMissingSkin)
{
	const std::string Source = ReadTextFile("src/game/client/components/players.cpp");
	const std::string Render = FunctionBody(Source, "void CPlayers::OnRender()");
	const std::string SkinsSource = ReadTextFile("src/game/client/components/skins.cpp");
	const std::string Refresh = FunctionBody(SkinsSource, "void CSkins::Refresh(TSkinLoadedCallback &&SkinLoadedCallback)");

	EXPECT_NE(Refresh.find("LoadSpecialSkinDirect(\"x_ninja\");"), std::string::npos);
	EXPECT_NE(Refresh.find("LoadSpecialSkinDirect(\"x_spec\");"), std::string::npos);
	EXPECT_NE(Refresh.find("GameClient()->OnSkinUpdate(pName);"), std::string::npos);
	// 仅在 x_spec 与 TeeRenderInfo 均可用时绘制观战 Tee，不回落到缺失皮肤。
	EXPECT_NE(Render.find("const bool SpectatorTeeRenderable ="), std::string::npos);
	EXPECT_NE(Render.find("GameClient()->m_Skins.FindOrNullptr(\"x_spec\") != nullptr &&"), std::string::npos);
	EXPECT_NE(Render.find("SpectatorTeeRenderInfo() != nullptr &&"), std::string::npos);
	EXPECT_NE(Render.find("SpectatorTeeRenderInfo()->TeeRenderInfo().Valid();"), std::string::npos);
	EXPECT_NE(Source.find("SpectatorTeeRenderInfo.m_TeeRenderFlags = TEE_PREVIEW_LAYER_BODY_OUTLINE;"), std::string::npos);
	EXPECT_NE(Render.find("const bool LocalSpecChar = GameClient()->IsLocalClientId(ClientId);"), std::string::npos);
	EXPECT_NE(Render.find("const bool OtherSpecChar = !LocalSpecChar && (GameClient()->IsOtherTeam(ClientId) || ClientId < 0);"), std::string::npos);
	EXPECT_NE(Render.find("Alpha = OtherSpecChar ? g_Config.m_ClShowOthersAlpha / 100.f : 1.f;"), std::string::npos);
	EXPECT_NE(Render.find("RenderTools()->RenderTee(CAnimState::GetIdle(), &SpectatorTeeRenderInfo()->TeeRenderInfo()"), std::string::npos);
}

TEST(QmNewUiMenuBranches, WeaponImpactEventsUseInferredOwnerAlpha)
{
	const std::string Source = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string ProcessEvents = FunctionBody(Source, "void CGameClient::ProcessEvents()");
	const std::string FinalizeHammerHitEvents = FunctionBody(Source, "void CGameClient::FinalizeHammerHitEvents()");

	EXPECT_NE(Source.find("float QmKnownOwnerEventAlpha(CGameClient *pGameClient, int Owner)"), std::string::npos);
	EXPECT_NE(Source.find("int QmInferExplosionOwner(CGameClient *pGameClient, vec2 Pos)"), std::string::npos);
	EXPECT_NE(Source.find("SQmHammerHitMatch QmInferHammerHit(CGameClient *pGameClient, vec2 Pos, int EventTick,"), std::string::npos);
	EXPECT_NE(ProcessEvents.find("const float ExplosionAlpha = QmKnownOwnerEventAlpha(this, QmInferExplosionOwner(this, ExplosionPos));"), std::string::npos);
	EXPECT_NE(ProcessEvents.find("m_Effects.Explosion(ExplosionPos, ExplosionAlpha);"), std::string::npos);
	EXPECT_NE(ProcessEvents.find("m_vPendingHammerHitEvents.push_back({"), std::string::npos);
	EXPECT_EQ(ProcessEvents.find("CheckPredictedHammerHitHandled("), std::string::npos);
	EXPECT_EQ(ProcessEvents.find("QmInferHammerHit(this"), std::string::npos);
	EXPECT_EQ(ProcessEvents.find("m_HammerHitTracker.Record(Hit)"), std::string::npos);
	EXPECT_EQ(ProcessEvents.find("m_Effects.HammerHit("), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("const SQmHammerHitMatch Match = QmInferHammerHit(this, Event.m_Pos, Event.m_SnapshotTick, apCurrent, apPrevious);"), std::string::npos);
	// 回归守卫：每个快照只允许做一次角色项收集，不允许回到「每个事件 × 每个客户端」的
	// 线性快照查找（CSnapshot::GetItemIndex 是 O(items)，满员时会被放大到百万次比较量级）。
	EXPECT_NE(FinalizeHammerHitEvents.find("QmCollectCharactersById(this, IClient::SNAP_CURRENT, apCurrent);"), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("QmCollectCharactersById(this, IClient::SNAP_PREV, apPrevious);"), std::string::npos);
	EXPECT_EQ(FinalizeHammerHitEvents.find("SnapFindItem"), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("m_PredictedWorld.CheckPredictedHammerHitHandled("), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("Match.m_AttackerId, Event.m_SnapshotTick, Match.m_TargetId"), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("m_HammerHitTracker.Record(Hit)"), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("const float HammerHitAlpha = QmKnownOwnerEventAlpha(this, Match.m_AttackerId);"), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("m_Effects.HammerHit(Event.m_Pos, HammerHitAlpha, 1.0f);"), std::string::npos);
	EXPECT_EQ(ProcessEvents.find("m_Effects.Explosion(vec2(pEvent->m_X, pEvent->m_Y), Alpha);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, HammerHitPredictionMatchingUsesOwnerDistanceAndOneToOneConsumption)
{
	std::vector<CGameWorld::CPredictedEvent> vPredictedEvents;
	CGameWorld::CPredictedEvent Near(NETEVENTTYPE_HAMMERHIT, vec2(100.0f, 100.0f), 3, 100, 4);
	Near.m_Handled = true;
	vPredictedEvents.push_back(Near);
	EXPECT_FALSE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(124.0f, 100.0f), 4, 102, 4)));
	EXPECT_EQ(vPredictedEvents.size(), 1u);
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(132.0f, 100.0f), 3, 102, 4)));
	EXPECT_TRUE(vPredictedEvents.empty());

	CGameWorld::CPredictedEvent First(NETEVENTTYPE_HAMMERHIT, vec2(100.0f, 100.0f), 3, 100, 4);
	CGameWorld::CPredictedEvent Second(NETEVENTTYPE_HAMMERHIT, vec2(130.0f, 100.0f), 3, 100, 5);
	First.m_Handled = true;
	Second.m_Handled = true;
	vPredictedEvents = {First, Second};
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(101.0f, 100.0f), 3, 102, 4)));
	EXPECT_EQ(vPredictedEvents.size(), 1u);
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(131.0f, 100.0f), 3, 102, 5)));
	EXPECT_TRUE(vPredictedEvents.empty());

	CGameWorld::CPredictedEvent TargetA(NETEVENTTYPE_HAMMERHIT, vec2(200.0f, 100.0f), 3, 200, 4);
	CGameWorld::CPredictedEvent TargetB(NETEVENTTYPE_HAMMERHIT, vec2(202.0f, 100.0f), 3, 200, 5);
	TargetA.m_Handled = true;
	TargetB.m_Handled = true;
	vPredictedEvents = {TargetA, TargetB};
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(201.0f, 100.0f), 3, 202, 5)));
	EXPECT_EQ(vPredictedEvents.size(), 1u);
	EXPECT_EQ(vPredictedEvents.front().m_ExtraInfo, 4);
	vPredictedEvents.clear();

	CGameWorld::CPredictedEvent Far(NETEVENTTYPE_HAMMERHIT, vec2(100.0f, 100.0f), 3, 100, 4);
	Far.m_Handled = true;
	vPredictedEvents.push_back(Far);
	EXPECT_FALSE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(133.0f, 100.0f), 3, 102, 4)));
	EXPECT_EQ(vPredictedEvents.size(), 1u);
	EXPECT_FALSE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(100.0f, 100.0f), 3, 100 + SERVER_TICK_SPEED + 1, 4)));
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(132.0f, 100.0f), 3, 102, 4)));
	EXPECT_TRUE(vPredictedEvents.empty());

	vPredictedEvents.clear();
	CGameWorld::CPredictedEvent UnknownOwner(NETEVENTTYPE_HAMMERHIT, vec2(200.0f, 100.0f), 7, 200, 8);
	UnknownOwner.m_Handled = true;
	vPredictedEvents.push_back(UnknownOwner);
	EXPECT_FALSE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(201.0f, 100.0f), -1, 202, 8)));

	CGameWorld::CPredictedEvent AmbiguousA(NETEVENTTYPE_HAMMERHIT, vec2(300.0f, 100.0f), 7, 300, 9);
	CGameWorld::CPredictedEvent AmbiguousB(NETEVENTTYPE_HAMMERHIT, vec2(302.0f, 100.0f), 8, 300, 9);
	AmbiguousA.m_Handled = true;
	AmbiguousB.m_Handled = true;
	vPredictedEvents = {AmbiguousA, AmbiguousB};
	EXPECT_FALSE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(301.0f, 100.0f), -1, 302, 9)));
	EXPECT_EQ(vPredictedEvents.size(), 2u);

	vPredictedEvents.clear();
	CGameWorld::CPredictedEvent Boundary(NETEVENTTYPE_HAMMERHIT, vec2(400.0f, 100.0f), 3, 400, 4);
	Boundary.m_Handled = true;
	vPredictedEvents.push_back(Boundary);
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(432.0f, 100.0f), 3, 402, 4)));

	CGameWorld::CPredictedEvent Earlier(NETEVENTTYPE_HAMMERHIT, vec2(500.0f, 100.0f), 3, 500, 4);
	CGameWorld::CPredictedEvent Later(NETEVENTTYPE_HAMMERHIT, vec2(520.0f, 100.0f), 3, 516, 4);
	Earlier.m_Handled = true;
	Later.m_Handled = true;
	vPredictedEvents = {Earlier, Later};
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(501.0f, 100.0f), 3, 517, 4)));
	EXPECT_EQ(vPredictedEvents.size(), 1u);
	EXPECT_EQ(vPredictedEvents.front().m_Tick, 500);
}

TEST(QmNewUiMenuBranches, HammerHitConsumersUseDeferredServerEvidenceOnly)
{
	const std::string CharacterSource = ReadTextFile("src/game/client/prediction/entities/character.cpp");
	const std::string GameClientSource = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string GameWorldSource = ReadTextFile("src/game/client/prediction/gameworld.cpp");
	const std::string FastPracticeSource = ReadTextFile("src/game/client/components/tclient/fast_practice.cpp");
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string InferHammerHit = FunctionBody(GameClientSource, "SQmHammerHitMatch QmInferHammerHit(");
	const std::string FinalizeHammerHitEvents = FunctionBody(GameClientSource, "void CGameClient::FinalizeHammerHitEvents()");
	const std::string OnNewSnapshot = FunctionBody(GameClientSource, "void CGameClient::OnNewSnapshot()");
	const std::string WakeupActions = FunctionBody(TClientSource, "void CTClient::CheckHammerWakeupActions()");

	EXPECT_EQ(CharacterSource.find("CreateHammerHitEvent"), std::string::npos);
	EXPECT_EQ(GameWorldSource.find("HammerHitEvents"), std::string::npos);
	EXPECT_EQ(GameWorldSource.find("BeginHammerHitEventBatch"), std::string::npos);
	EXPECT_EQ(GameClientSource.find("RecordPredictedHammerHits"), std::string::npos);
	EXPECT_EQ(GameClientSource.find("ConfirmPredictedEvent"), std::string::npos);
	EXPECT_EQ(GameClientSource.find("MatchPredictedEvent"), std::string::npos);
	EXPECT_EQ(InferHammerHit.find("m_PredictedWorld"), std::string::npos);
	// 回归守卫：推断函数本身不得再做逐客户端快照查找。
	EXPECT_EQ(InferHammerHit.find("SnapFindItem"), std::string::npos);
	EXPECT_NE(InferHammerHit.find("QmIsHammerSuperTeam(DDTeam, pGameClient->m_Teams.m_IsDDRace16)"), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("m_HammerHitTracker.Record(Hit)"), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("QmIsHammerWakeupTransition("), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("HandleConfirmedHammerHit(Hit);"), std::string::npos);
	EXPECT_NE(GameClientSource.find("const bool Online = Client()->State() == IClient::STATE_ONLINE;"), std::string::npos);
	EXPECT_NE(GameClientSource.find("if(Online)\n\t\tHandleHammerSkinSwap(Hit);"), std::string::npos);
	EXPECT_NE(FastPracticeSource.find("void CFastPractice::MaybePlayHammerHitEffect(CCharacter *pChar)"), std::string::npos);
	EXPECT_NE(FastPracticeSource.find("closest_point_on_line(StartPos, EndPos"), std::string::npos);
	EXPECT_EQ(FastPracticeSource.find("HammerHitTracker"), std::string::npos);
	EXPECT_NE(TClientSource.find("FindTargetHitsAtTick("), std::string::npos);
	EXPECT_NE(TClientSource.find("if(!Hit.m_TargetWoke)"), std::string::npos);
	EXPECT_NE(TClientSource.find("CheckHammerWakeupActions();"), std::string::npos);
	EXPECT_NE(TClientSource.find("m_aaComboLastHammerHitSnapshotTick[Dummy][TargetId]"), std::string::npos);
	EXPECT_EQ(GameClientSource.find("QmJellyHammerHitRadius"), std::string::npos);

	const size_t FinalizePos = OnNewSnapshot.find("FinalizeHammerHitEvents();");
	const size_t ComponentSnapshotPos = OnNewSnapshot.find("pComponent->OnNewSnapshot();");
	ASSERT_NE(FinalizePos, std::string::npos);
	ASSERT_NE(ComponentSnapshotPos, std::string::npos);
	EXPECT_LT(FinalizePos, ComponentSnapshotPos);

	const size_t UnspecPos = WakeupActions.find("Client()->SendPackMsg(Input.m_ActiveConnection");
	const size_t CloseChatPos = WakeupActions.find("GameClient()->m_Chat.DisableMode();");
	const size_t SwitchPos = WakeupActions.find("Console()->ExecuteLine(aCommand);");
	ASSERT_NE(UnspecPos, std::string::npos);
	ASSERT_NE(CloseChatPos, std::string::npos);
	ASSERT_NE(SwitchPos, std::string::npos);
	EXPECT_LT(UnspecPos, CloseChatPos);
	EXPECT_LT(CloseChatPos, SwitchPos);
}

TEST(QmNewUiMenuBranches, SettingsCardMigrationsKeepVersionPendingWhenExactMigrationFails)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const size_t ProfilesStart = Source.find("if(g_Config.m_QmCardLayoutVersion < 2)");
	const size_t StatusBarStart = Source.find("if(g_Config.m_QmCardLayoutVersion < 3)");
	const size_t TeeStart = Source.find("if(g_Config.m_QmCardLayoutVersion < 4)");
	ASSERT_NE(ProfilesStart, std::string::npos);
	ASSERT_NE(StatusBarStart, std::string::npos);
	ASSERT_NE(TeeStart, std::string::npos);
	const std::string Profiles = Source.substr(ProfilesStart, StatusBarStart - ProfilesStart);
	const std::string StatusBar = Source.substr(StatusBarStart, TeeStart - StatusBarStart);
	const std::string Tee = Source.substr(TeeStart, Source.find("if(g_Config.m_QmCardLayoutVersion < 5)", TeeStart) - TeeStart);
	for(const std::string *pMigration : {&Profiles, &StatusBar, &Tee})
	{
		const size_t ShouldMigrate = pMigration->find("const bool ShouldMigrate = ExplicitStatus == qm_card_order::EExplicitLayoutStatus::MATCH;");
		const size_t CandidateChanged = pMigration->find("const bool CandidateChanged = ShouldMigrate && qm_card_order::MigrateExactLayout", ShouldMigrate);
		const size_t FailureGuard = pMigration->find("if(ShouldMigrate && !CandidateChanged)", CandidateChanged);
		const size_t Persist = pMigration->find("if(!PersistCandidate(Candidate, CandidateChanged))", FailureGuard);
		ASSERT_NE(ShouldMigrate, std::string::npos);
		ASSERT_NE(CandidateChanged, std::string::npos);
		ASSERT_NE(FailureGuard, std::string::npos);
		ASSERT_NE(Persist, std::string::npos);
		EXPECT_LT(ShouldMigrate, CandidateChanged);
		EXPECT_LT(CandidateChanged, FailureGuard);
		EXPECT_LT(FailureGuard, Persist);
	}
}

TEST(QmNewUiMenuBranches, NewOpacityControlsDoNotChainLegacyPanelOpacity)
{
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");

	EXPECT_EQ(MenusSource.find("m_ClMenuPanelOpacity / 100.0f) * (g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_EQ(MenusSource.find("m_ClMenuPanelElevatedOpacity / 100.0f) * (g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_EQ(MenusSource.find("m_ClSettingsTabbarOpacity / 100.0f) * (g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_EQ(MenusSource.find("m_ClMenuPanelOpacity / 100.0f) * (g_Config.m_QmMapBrowserOpacity"), std::string::npos);
	EXPECT_EQ(MenusSource.find("m_ClMenuPanelElevatedOpacity / 100.0f) * (g_Config.m_QmMapBrowserOpacity"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NewColorControlsUseIndependentUiDomains)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string BrowserSource = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string ScoreboardSource = ReadTextFile("src/game/client/components/scoreboard.cpp");

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(QmUiColor, qm_ui_color"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(QmMapBrowserColor, qm_map_browser_color"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(QmScoreboardColor, qm_scoreboard_color"), std::string::npos);
	EXPECT_NE(MenusSource.find("ColorHSLA(g_Config.m_QmUiColor)"), std::string::npos);
	EXPECT_NE(MenusSource.find("ColorHSLA(g_Config.m_QmMapBrowserColor)"), std::string::npos);
	EXPECT_EQ(MenusSource.find("ColorHSLA(g_Config.m_ClMenuPanelColor)"), std::string::npos);
	const std::string UpdateColors = FunctionBody(MenusSource, "void CMenus::UpdateColors()");
	const std::string RenderBackground = FunctionBody(MenusSource, "void CMenus::RenderBackground()");
	EXPECT_NE(UpdateColors.find("ColorHSLA(g_Config.m_UiColor, true)"), std::string::npos);
	EXPECT_NE(RenderBackground.find("ms_GuiColor.WithAlpha(1.0f)"), std::string::npos);
	EXPECT_EQ(RenderBackground.find("g_Config.m_QmUiColor"), std::string::npos);
	EXPECT_NE(BrowserSource.find("ColorHSLA(g_Config.m_QmMapBrowserColor)"), std::string::npos);
	EXPECT_NE(ScoreboardSource.find("ColorHSLA(g_Config.m_QmScoreboardColor)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ScoreboardBackgroundsUseScoreboardOpacity)
{
	const std::string Source = ReadTextFile("src/game/client/components/scoreboard.cpp");

	EXPECT_NE(Source.find("Color.a = ScoreboardUiAlpha(AlphaScale);"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmScoreboardOpacity / 100.0f"), std::string::npos);
	EXPECT_NE(Source.find("ScoreboardDecorationColor(GameClient()->GetDDTeamColor(DDTeam).WithAlpha(0.5f * ItemAlpha))"), std::string::npos);
	EXPECT_NE(Source.find("Row.Draw(ScoreboardDecorationColor(ui_token::color::ACCENT_PRIMARY_DIM.WithMultipliedAlpha(ItemAlpha * 1.45f))"), std::string::npos);
	EXPECT_NE(Source.find("Row.Draw(ScoreboardDecorationColor(ColorRGBA(0.7f, 0.7f, 0.7f, 0.7f * ItemAlpha))"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ScoreboardDdTeamLabelUsesUnifiedBelowRowLayout)
{
	const std::string Source = ReadTextFile("src/game/client/components/scoreboard.cpp");
	const std::string RenderScoreboard = FunctionBody(Source, "void CScoreboard::RenderScoreboard(");

	EXPECT_NE(RenderScoreboard.find("ResolveScoreboardTeamLabelLayout("), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("if(EndsDDTeam)"), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("TextRender()->Text(TeamLabelLayout.m_X, TeamLabelLayout.m_Y, TeamFontSize, aBuf);"), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("TeamLabelLayout.m_IconY"), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("SCOREBOARD_TEAM_MODE_ICON_SIZE"), std::string::npos);
	EXPECT_EQ(RenderScoreboard.find("TeamLabelLayout.m_Y,\n\t\t\t\t\tTeamFontSize"), std::string::npos);
	EXPECT_EQ(RenderScoreboard.find("NumPlayers > 8"), std::string::npos);
	EXPECT_EQ(RenderScoreboard.find("State.m_TeamStartX"), std::string::npos);
	EXPECT_EQ(RenderScoreboard.find("Row.x + Row.w / 2.0f - TextRender()->TextWidth(TeamFontSize, aBuf) / 2.0f + 5.0f"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ScoreboardMediaIsInformationOnly)
{
	const std::string Source = ReadTextFile("src/game/client/components/scoreboard.cpp");
	EXPECT_EQ(Source.find("DoScoreboardMediaIconButton"), std::string::npos);
	EXPECT_EQ(Source.find("m_SystemMediaControls.Previous()"), std::string::npos);
	EXPECT_EQ(Source.find("m_SystemMediaControls.PlayPause()"), std::string::npos);
	EXPECT_EQ(Source.find("m_SystemMediaControls.Next()"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ScoreboardUsesOneRowPlanAndDenseTeeLod)
{
	const std::string Source = ReadTextFile("src/game/client/components/scoreboard.cpp");
	const std::string OnRender = FunctionBody(Source, "void CScoreboard::OnRender()");
	const std::string RenderScoreboard = FunctionBody(Source, "void CScoreboard::RenderScoreboard(");

	EXPECT_NE(OnRender.find("BuildPlayerRowPlan"), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("TEE_PREVIEW_LAYER_BODY"), std::string::npos);
	EXPECT_EQ(RenderScoreboard.find("for(int j ="), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("const bool HasWar ="), std::string::npos);

	// The rendering optimization must not alter point lookup or display behavior.
	EXPECT_NE(OnRender.find("m_PlayerPoints.EnsureQueried"), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("m_PlayerPoints.GetPoints"), std::string::npos);
}

TEST(QmNewUiMenuBranches, GaussianBlurUsesSharedUiBackdropWithTransparentFallback)
{
	const std::string UiHeader = ReadTextFile("src/game/client/ui.h");
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string UiRectSource = ReadTextFile("src/game/client/ui_rect.cpp");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string ScoreboardSource = ReadTextFile("src/game/client/components/scoreboard.cpp");
	const std::string TooltipsSource = ReadTextFile("src/game/client/components/tooltips.cpp");
	const std::string ImeSource = ReadTextFile("src/game/client/qm_ime_manager.cpp");
	const std::string CachedRectDraw = FunctionBody(UiSource, "void CUIElement::SUIElementRect::Draw(");
	const std::string BatchableRectDraw = FunctionBody(UiSource, "void CUi::RenderBatchableRect(");
	const std::string MenuButtonDraw = FunctionBody(UiSource, "int CUi::DoButton_Menu(");
	const std::string RectDraw = FunctionBody(UiRectSource, "void CUIRect::Draw(");
	const std::string RectDraw4 = FunctionBody(UiRectSource, "void CUIRect::Draw4(");
	const std::string MenuRender = FunctionBody(MenusSource, "void CMenus::Render()");
	const std::string LoadingRender = FunctionBody(MenusSource, "void CMenus::RenderLoading(");

	EXPECT_NE(UiHeader.find("CUiScopedGaussianBlur(CUi *pUi, float Alpha = 1.0f)"), std::string::npos);
	EXPECT_NE(UiHeader.find("GaussianBlurScopeAlpha() const"), std::string::npos);
	EXPECT_NE(UiHeader.find("RenderGaussianBlur(const CUIRect &Rect, float Alpha"), std::string::npos);
	EXPECT_NE(UiSource.find("IsBackbufferCaptureSupported"), std::string::npos);
	EXPECT_NE(UiSource.find("IsRenderTargetGaussianBlurSupported"), std::string::npos);
	EXPECT_NE(UiSource.find("CaptureBackbufferToRenderTarget"), std::string::npos);
	EXPECT_NE(UiSource.find("GaussianBlurRenderTarget"), std::string::npos);
	const std::string PrepareBlur = FunctionBody(UiSource, "bool CUi::PrepareGaussianBlur()");
	EXPECT_NE(PrepareBlur.find("m_GaussianBlurPrepared && m_GaussianBlurPreparedFrame == PerfFrame"), std::string::npos);
	EXPECT_NE(PrepareBlur.find("m_GaussianBlurPreparedFrame = PerfFrame"), std::string::npos);
	EXPECT_NE(PrepareBlur.find("m_GaussianBlurPrepared = true"), std::string::npos);
	EXPECT_NE(UiSource.find("Graphics()->GetScreen"), std::string::npos);
	EXPECT_NE(UiSource.find("UiGaussianBlurTargetDimension"), std::string::npos);
	EXPECT_NE(CachedRectDraw.find("GaussianBlurScopeAlpha()"), std::string::npos);
	EXPECT_NE(BatchableRectDraw.find("GaussianBlurScopeAlpha()"), std::string::npos);
	EXPECT_NE(MenuButtonDraw.find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_EQ(CachedRectDraw.find("RenderGaussianBlur(*pRect, Color.a)"), std::string::npos);
	EXPECT_EQ(BatchableRectDraw.find("RenderGaussianBlur(*pRect, Color.a)"), std::string::npos);
	EXPECT_EQ(MenuButtonDraw.find("RenderGaussianBlur(*pRect, BackgroundColor.a)"), std::string::npos);
	EXPECT_NE(RectDraw.find("DrawRectBackdrop(Corners, Rounding)"), std::string::npos);
	EXPECT_NE(RectDraw4.find("DrawRectBackdrop(Corners, Rounding)"), std::string::npos);
	EXPECT_NE(MenuRender.find("CUiScopedGaussianBlur GaussianBlurScope(Ui());"), std::string::npos);
	EXPECT_NE(LoadingRender.find("CUiScopedGaussianBlur GaussianBlurScope(Ui());"), std::string::npos);
	EXPECT_NE(ScoreboardSource.find("const float GaussianBlurAlpha = WantActive ? 1.0f : m_AnimContentAlpha;"), std::string::npos);
	EXPECT_NE(ScoreboardSource.find("CUiScopedGaussianBlur GaussianBlurScope(Ui(), GaussianBlurAlpha);"), std::string::npos);
	EXPECT_EQ(ScoreboardSource.find("CUiScopedGaussianBlur GaussianBlurScope(Ui());"), std::string::npos);
	EXPECT_EQ(ScoreboardSource.find("CUiScopedGaussianBlur GaussianBlurScope(Ui(), BackgroundAlphaFinal);"), std::string::npos);
	EXPECT_NE(TooltipsSource.find("CUiScopedGaussianBlur GaussianBlurScope(Ui(), AlphaFactor);"), std::string::npos);
	EXPECT_NE(ImeSource.find("CUiScopedGaussianBlur GaussianBlurScope(m_pGameClient->Ui());"), std::string::npos);
	EXPECT_EQ(ScoreboardSource.find("BetterScoreboardBlur"), std::string::npos);

	// Existing translucent surfaces remain the unsupported-backend fallback.
	EXPECT_NE(ScoreboardSource.find("Scoreboard.Draw(ScoreboardGlassSurface(BackgroundAlphaFinal)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, GaussianBlurCoversRequestedHudAndVoteBackgroundsOnly)
{
	const std::string HudSource = ReadTextFile("src/game/client/components/hud.cpp");
	const std::string VotingSource = ReadTextFile("src/game/client/components/voting.cpp");
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string MovementInfo = FunctionBody(HudSource, "void CHud::RenderMovementInformation()");
	const std::string KeyStatus = FunctionBody(HudSource, "void CHud::RenderKeyStatus()");
	const std::string ScoreHud = FunctionBody(HudSource, "void CHud::RenderScoreHud()");
	const std::string Vote = FunctionBody(VotingSource, "void CVoting::Render()");
	const std::string MiniVote = FunctionBody(TClientSource, "void CTClient::RenderMiniVoteHud(");

	EXPECT_NE(MovementInfo.find("RenderGaussianBlur"), std::string::npos);
	EXPECT_NE(KeyStatus.find("RenderGaussianBlur"), std::string::npos);
	EXPECT_NE(ScoreHud.find("RenderGaussianBlur"), std::string::npos);
	EXPECT_NE(Vote.find("RenderGaussianBlur"), std::string::npos);
	EXPECT_NE(MiniVote.find("RenderGaussianBlur"), std::string::npos);
	EXPECT_NE(Vote.find("View.Draw(ui_token::color::SURFACE_GLASS"), std::string::npos);
	EXPECT_NE(MiniVote.find("View.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, GaussianBlurSkipsPageSwitchTransitionOverlays)
{
	const std::string MenusHeader = ReadTextFile("src/game/client/components/menus.h");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string BrowserSource = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string IngameSource = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const std::string TouchControlsSource = ReadTextFile("src/game/client/components/menus_ingame_touch_controls.cpp");
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Settings7Source = ReadTextFile("src/game/client/components/menus_settings7.cpp");
	const std::string QmClientSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string TransitionSources = MenusSource + BrowserSource + IngameSource + SettingsSource + Settings7Source + QmClientSource + TClientSource;
	const std::string TransitionOverlay = FunctionBody(MenusSource, "void CMenus::DrawUiSwitchTransitionOverlay(");
	const std::string TouchButtonEditor = FunctionBody(TouchControlsSource, "void CMenusIngameTouchControls::RenderTouchButtonEditor(");
	const std::regex DirectTransitionOverlayDraw(R"(\b[A-Za-z_][A-Za-z0-9_]*\.Draw\([^\n]*(TransitionAlpha|TabTransitionAlpha))");

	EXPECT_NE(MenusHeader.find("void DrawUiSwitchTransitionOverlay(const CUIRect &Rect, ColorRGBA Color);"), std::string::npos);
	EXPECT_NE(TransitionOverlay.find("CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());"), std::string::npos);
	EXPECT_NE(TransitionOverlay.find("Rect.Draw(Color, IGraphics::CORNER_NONE, 0.0f);"), std::string::npos);
	EXPECT_FALSE(std::regex_search(TransitionSources, DirectTransitionOverlayDraw));
	EXPECT_NE(TouchButtonEditor.find("CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());"), std::string::npos);
	EXPECT_NE(TouchButtonEditor.find("BlockClip.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha)"), std::string::npos);
	EXPECT_NE(BrowserSource.find("DrawUiSwitchTransitionOverlay("), std::string::npos);
	EXPECT_NE(IngameSource.find("DrawUiSwitchTransitionOverlay("), std::string::npos);
	EXPECT_EQ(SettingsSource.find("DrawUiSwitchTransitionOverlay("), std::string::npos);
	EXPECT_EQ(Settings7Source.find("DrawUiSwitchTransitionOverlay("), std::string::npos);
	EXPECT_EQ(QmClientSource.find("DrawUiSwitchTransitionOverlay("), std::string::npos);
	EXPECT_EQ(TClientSource.find("DrawUiSwitchTransitionOverlay("), std::string::npos);
}

TEST(QmNewUiMenuBranches, GaussianBlurSkipsButtonsAndKeepsSurfaceRounding)
{
	const std::string UiHeader = ReadTextFile("src/game/client/ui.h");
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string UiListboxHeader = ReadTextFile("src/game/client/ui_listbox.h");
	const std::string UiListboxSource = ReadTextFile("src/game/client/ui_listbox.cpp");
	const std::string UiRectSource = ReadTextFile("src/game/client/ui_rect.cpp");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string BrowserSource = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string DemoSource = ReadTextFile("src/game/client/components/menus_demo.cpp");
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string SettingsAssetsSource = ReadTextFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string StartSource = ReadTextFile("src/game/client/components/menus_start.cpp");
	const std::string QmClientSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string ButtonsSource = ReadTextFile("src/game/client/QmUi/UiButtons.cpp");
	const std::string FormsSource = ReadTextFile("src/game/client/QmUi/UiForms.cpp");
	const std::string NavigationSource = ReadTextFile("src/game/client/QmUi/UiNavigation.cpp");
	const std::string HudSource = ReadTextFile("src/game/client/components/hud.cpp");
	const std::string VotingSource = ReadTextFile("src/game/client/components/voting.cpp");
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string BrowserServerList = FunctionBody(BrowserSource, "void CMenus::RenderServerbrowserServerList(");
	const std::string BrowserFade = BlockBodyAfter(BrowserServerList, "if(NumServers * ms_ListheaderHeight > ListView.h)");
	const std::string StartMenu = FunctionBody(StartSource, "void CMenusStart::RenderStartMenuImpl(");

	EXPECT_NE(UiHeader.find("class CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(UiHeader.find("RenderGaussianBlur(const CUIRect &Rect, float Alpha = 1.0f, int Corners = IGraphics::CORNER_NONE, float Rounding = 0.0f)"), std::string::npos);
	EXPECT_NE(FunctionBody(UiRectSource, "void CUIRect::DrawRectBackdrop(").find("Corners, Rounding"), std::string::npos);
	EXPECT_NE(FunctionBody(UiSource, "void CUIElement::SUIElementRect::Draw(").find("Corners, Rounding"), std::string::npos);
	EXPECT_NE(FunctionBody(UiSource, "void CUi::RenderBatchableRect(").find("Corners, Rounding"), std::string::npos);

	EXPECT_NE(FunctionBody(UiSource, "int CUi::DoButton_Menu(").find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(FunctionBody(UiSource, "int CUi::DoButton_FontIcon(").find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(FunctionBody(UiSource, "int CUi::DoButton_PopupMenu(").find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(FunctionBody(UiSource, "bool CUi::DoClearableEditBox(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, int Corners, const std::vector<STextColorSplit> &vColorSplits, const SEditBoxRenderOptions &RenderOptions)").find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(FunctionBody(UiSource, "SEditResult<int64_t> CUi::DoValueSelectorWithState(").find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(FunctionBody(MenusSource, "int CMenus::DoButton_Menu(").find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(FunctionBody(MenusSource, "int CMenus::DoButton_MenuTab(").find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(FunctionBody(MenusSource, "int CMenus::DoButton_Toggle(").find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(FunctionBody(MenusSource, "int CMenus::DoButton_CheckBox_Common_WithLabelElement(").find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(FunctionBody(MenusSource, "ColorHSLA CMenus::DoButton_ColorPicker(").find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(FunctionBody(MenusSource, "int CMenus::DoMenuTabV2(").find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(FunctionBody(ButtonsSource, "bool DoStyledButton(").find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(FunctionBody(ButtonsSource, "bool IconButton(").find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(FunctionBody(FormsSource, "bool Toggle(").find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(FunctionBody(NavigationSource, "bool ListItem(").find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(FunctionBody(BrowserSource, "void CMenus::RenderServerbrowserServerList(").find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(StartMenu.find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_EQ(StartMenu.find("CUiScopedGaussianBlur GaussianBlurScope"), std::string::npos);

	EXPECT_NE(UiListboxHeader.find("SuppressGaussianBlur() const"), std::string::npos);
	EXPECT_NE(FunctionBody(UiListboxSource, "CListboxItem CListBox::DoNextItem(").find("Item.m_GaussianBlurSuppressed = true;"), std::string::npos);
	EXPECT_NE(DemoSource.find("auto GaussianBlurSuppression = ListItem.SuppressGaussianBlur();"), std::string::npos);
	EXPECT_NE(BrowserSource.find("auto GaussianBlurSuppression = Item.SuppressGaussianBlur();"), std::string::npos);
	EXPECT_NE(SettingsSource.find("auto GaussianBlurSuppression = Item.SuppressGaussianBlur();"), std::string::npos);
	EXPECT_NE(SettingsAssetsSource.find("auto GaussianBlurSuppression = Item.SuppressGaussianBlur();"), std::string::npos);
	EXPECT_NE(BrowserFade.find("CUiScopedGaussianBlurSuppression"), std::string::npos);
	EXPECT_NE(BrowserFade.find("Fade.Draw4("), std::string::npos);

	const size_t PreviewSuppression = QmClientSource.find("CUiScopedGaussianBlurSuppression PreviewBlurSuppression(Ui());");
	ASSERT_NE(PreviewSuppression, std::string::npos);
	const size_t PreviewBlockStart = QmClientSource.rfind('{', PreviewSuppression);
	ASSERT_NE(PreviewBlockStart, std::string::npos);
	const size_t PreviewBlockEnd = MatchingBrace(QmClientSource, PreviewBlockStart);
	ASSERT_NE(PreviewBlockEnd, std::string::npos);
	const size_t PreviewDraw = QmClientSource.find("PreviewFrame.Draw(", PreviewSuppression);
	const size_t PreviewMargin = QmClientSource.find("PreviewRect.Margin(maximum", PreviewSuppression);
	const size_t PreviewButtonLogic = QmClientSource.find("Ui()->DoButtonLogic(&s_ColorPreviewButton", PreviewSuppression);
	ASSERT_NE(PreviewDraw, std::string::npos);
	ASSERT_NE(PreviewMargin, std::string::npos);
	ASSERT_NE(PreviewButtonLogic, std::string::npos);
	EXPECT_LT(PreviewDraw, PreviewBlockEnd);
	EXPECT_GT(PreviewMargin, PreviewBlockEnd);
	EXPECT_GT(PreviewButtonLogic, PreviewBlockEnd);

	EXPECT_NE(HudSource.find("ScoreHudCorners, 5.0f"), std::string::npos);
	EXPECT_NE(HudSource.find("IGraphics::CORNER_ALL, ui_token::radius::BASE"), std::string::npos);
	EXPECT_NE(HudSource.find("HudEditorScope.m_Corners, ui_token::radius::BASE"), std::string::npos);
	EXPECT_NE(VotingSource.find("HudEditorScope.m_Corners, ui_token::radius::BASE"), std::string::npos);
	EXPECT_NE(TClientSource.find("HudEditorScope.m_Corners, 3.0f"), std::string::npos);
}

TEST(QmNewUiMenuBranches, IngameMenuPrimaryActionLabelsUseEnglishKeys)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_ingame.cpp");

	EXPECT_NE(Source.find("pDisconnectButtonLabel = Localize(\"Disconnect\")"), std::string::npos);
	EXPECT_NE(Source.find("pDummyButtonLabel = Localize(\"Connect dummy\")"), std::string::npos);
	EXPECT_NE(Source.find("pDummyButtonLabel = Localize(\"Connecting dummy\")"), std::string::npos);
	EXPECT_NE(Source.find("pDummyButtonLabel = Localize(\"Disconnect dummy\")"), std::string::npos);
	EXPECT_NE(Source.find("pEditHudButtonLabel = Localize(\"Edit HUD\")"), std::string::npos);
	EXPECT_NE(Source.find("pDemoButtonLabel = Recording ? Localize(\"Stop record\") : Localize(\"Record demo\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Save last %d min\")"), std::string::npos);
	EXPECT_NE(Source.find("pDemoMarkerButtonLabel = Localize(\"Mark demo\")"), std::string::npos);
	EXPECT_NE(Source.find("pJoinRedButtonLabel = Localize(\"Join red\")"), std::string::npos);
	EXPECT_NE(Source.find("pJoinBlueButtonLabel = Localize(\"Join blue\")"), std::string::npos);
	EXPECT_NE(Source.find("pJoinGameButtonLabel = Localize(\"Join game\")"), std::string::npos);
	EXPECT_NE(Source.find("pKillButtonLabel = Localize(\"Kill\")"), std::string::npos);
	EXPECT_NE(Source.find("pPauseButtonLabel = (!Paused && !Spec) ? Localize(\"Pause\") : Localize(\"Join game\")"), std::string::npos);
	EXPECT_NE(Source.find("pFastPracticeLabel = FastPracticeEnabled ? Localize(\"Stop practice\") : Localize(\"Fast practice\")"), std::string::npos);
	EXPECT_NE(Source.find("DoToolTip(&s_DummyButton, &Button, Localize(\"Please wait…\"))"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DummyAndSpectateBindLabelsUseEnglishKeys)
{
	const std::string ControlsSource = ReadTextFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string TouchSource = ReadTextFile("src/game/client/components/touch_controls.cpp");

	EXPECT_NE(ControlsSource.find("Localizable(\"Toggle dummy\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Dummy jump\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Dummy fire\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Dummy hook\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Dummy copy\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Dummy hammer fly\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Dummy Control\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Spectate mode\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Spectate teleport\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Spectate next\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Spectate previous\")"), std::string::npos);
	EXPECT_NE(TouchSource.find("Localizable(\"Toggle dummy\")"), std::string::npos);
	EXPECT_NE(TouchSource.find("Localizable(\"Spectate mode\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ConsoleChatExportLabelsUseEnglishKeys)
{
	const std::string Source = ReadTextFile("src/game/client/components/console.cpp");

	EXPECT_NE(Source.find("Localize(\"QmClient chat log\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Total\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Messages\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"No chat log selected\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Chat export failed\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Exported %d chat messages\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Selected %d\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Cancel\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Export selected\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Clear\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Select all chat\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Select export\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, HudDummyStatusLabelsUseEnglishKeys)
{
	const std::string Source = ReadTextFile("src/game/client/components/hud.cpp");

	EXPECT_NE(Source.find("Localize(\"Dummy mini view\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Connect dummy to enable\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Key Sticking: ?\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Key Sticking: On\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Key Sticking: Off\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Key Sticking: Reset Self\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Hammer: %s\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Dummy Control: %s\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Dummy copy: %s\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TranslationAndDemoUiLabelsUseEnglishKeys)
{
	const std::string ChatSource = ReadTextFile("src/game/client/components/chat.cpp");
	const std::string DemoSource = ReadTextFile("src/game/client/components/menus_demo.cpp");
	const std::string BrowserSource = ReadTextFile("src/game/client/components/menus_browser.cpp");

	EXPECT_NE(ChatSource.find("Localize(\"Translation Settings\")"), std::string::npos);
	EXPECT_NE(ChatSource.find("Localize(\"Auto-translate incoming messages\")"), std::string::npos);
	EXPECT_NE(ChatSource.find("Localize(\"Auto-translate outgoing messages\")"), std::string::npos);
	EXPECT_NE(ChatSource.find("Localize(\"Incoming language\")"), std::string::npos);
	EXPECT_NE(ChatSource.find("Localize(\"Outgoing language\")"), std::string::npos);
	EXPECT_NE(ChatSource.find("Localize(\"Translation service\")"), std::string::npos);
	EXPECT_NE(DemoSource.find("Localize(\"Could not preview this image\")"), std::string::npos);
	EXPECT_NE(DemoSource.find("BrowsingScreenshots ? Localize(\"Open the folder containing screenshots\") : Localize(\"Open the folder containing demo files\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"Map\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"Category\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"Difficulty stars\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"Note\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"Has save\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"None\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, FriendCategoryHeadersExposeManagement)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");

	EXPECT_NE(Source.find("FONT_ICON_GEAR"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Manage categories\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Drag to reorder; click to expand or collapse. Right-click or use the gear to manage categories\")"), std::string::npos);
}

TEST(QmFriendCategoryDrag, ClickAndSmallMovementDoNotStartSorting)
{
	SFriendsCategoryDragState Drag;
	const CUIRect Header{10.0f, 20.0f, 180.0f, 24.0f};
	Drag.Begin(2, Header, vec2(30.0f, 30.0f));
	EXPECT_FALSE(Drag.Update(vec2(30.0f, 30.0f), true));
	EXPECT_FALSE(Drag.Update(vec2(32.0f, 33.0f), true));
	EXPECT_FALSE(Drag.Update(vec2(32.0f, 33.0f), false));
	EXPECT_EQ(Drag.m_DraggingIndex, -1);
}

TEST(QmFriendCategoryDrag, MovingBeyondHeaderStartsSortingWithoutModifier)
{
	SFriendsCategoryDragState Drag;
	const CUIRect Header{10.0f, 20.0f, 180.0f, 24.0f};
	Drag.Begin(3, Header, vec2(30.0f, 30.0f));
	EXPECT_TRUE(Drag.Update(vec2(30.0f, 90.0f), true));
	EXPECT_EQ(Drag.m_DraggingIndex, 3);
	EXPECT_FLOAT_EQ(Drag.m_GrabOffset.x, 20.0f);
	EXPECT_FLOAT_EQ(Drag.m_GrabOffset.y, 10.0f);
	EXPECT_FLOAT_EQ(Drag.m_DraggedWidth, 180.0f);
	EXPECT_FLOAT_EQ(Drag.m_DraggedHeight, 24.0f);
	// 回到起点或释放鼠标仍属于同一次拖动，不能变成展开/收起点击。
	EXPECT_FALSE(Drag.Update(vec2(30.0f, 30.0f), true));
	EXPECT_FALSE(Drag.Update(vec2(30.0f, 30.0f), false));
	EXPECT_EQ(Drag.m_DraggingIndex, 3);
}

TEST(QmFriendCategoryDrag, PressIsRequiredAndEveryCategoryCanBeDragged)
{
	SFriendsCategoryDragState Drag;
	EXPECT_FALSE(Drag.Update(vec2(100.0f, 100.0f), true));
	const CUIRect Header{10.0f, 20.0f, 180.0f, 24.0f};
	for(int CategoryIndex = 0; CategoryIndex < 4; ++CategoryIndex)
	{
		Drag.Begin(CategoryIndex, Header, vec2(30.0f, 30.0f));
		EXPECT_FALSE(Drag.Update(vec2(30.0f, 100.0f), false));
		EXPECT_TRUE(Drag.Update(vec2(30.0f, 35.0f), true));
		EXPECT_EQ(Drag.m_DraggingIndex, CategoryIndex);
	}
}

TEST(QmFriendPlayerDrag, RequiresPressAndFiveUnitsOfHeldMovement)
{
	SFriendsPlayerDragState Drag;
	EXPECT_FALSE(Drag.Update(vec2(100.0f, 100.0f), true));
	int ItemId = 0;
	const CUIRect Row{10.0f, 20.0f, 180.0f, 24.0f};
	Drag.Begin(&ItemId, IFriends::FRIEND_PLAYER, "Alice", "Clan", "Friends", Row, vec2(30.0f, 30.0f));
	EXPECT_FALSE(Drag.Update(vec2(32.0f, 33.0f), true));
	EXPECT_FALSE(Drag.Update(vec2(33.0f, 34.0f), false));
	EXPECT_FALSE(Drag.m_Dragging);
	EXPECT_TRUE(Drag.Update(vec2(33.0f, 34.0f), true));
	EXPECT_TRUE(Drag.m_Dragging);
	EXPECT_FALSE(Drag.Update(vec2(30.0f, 30.0f), true));
	EXPECT_FALSE(Drag.Update(vec2(30.0f, 30.0f), false));
	EXPECT_TRUE(Drag.m_Dragging);
}

TEST(QmFriendPlayerDrag, OwnsIdentityAndPreservesGrabGeometry)
{
	SFriendsPlayerDragState Drag;
	int ItemId = 0;
	char aName[] = "Alice";
	char aClan[] = "Clan";
	char aCategory[] = "Practice";
	const CUIRect Row{10.0f, 20.0f, 180.0f, 24.0f};
	Drag.Begin(&ItemId, IFriends::FRIEND_PLAYER, aName, aClan, aCategory, Row, vec2(30.0f, 30.0f));
	aName[0] = 'X';
	aClan[0] = 'X';
	aCategory[0] = 'X';
	EXPECT_EQ(Drag.m_pPressedItem, &ItemId);
	EXPECT_STREQ(Drag.m_aName, "Alice");
	EXPECT_STREQ(Drag.m_aClan, "Clan");
	EXPECT_STREQ(Drag.m_aCategory, "Practice");
	EXPECT_FLOAT_EQ(Drag.m_PressMouse.x, 30.0f);
	EXPECT_FLOAT_EQ(Drag.m_PressMouse.y, 30.0f);
	EXPECT_FLOAT_EQ(Drag.m_GrabOffset.x, 20.0f);
	EXPECT_FLOAT_EQ(Drag.m_GrabOffset.y, 10.0f);
	EXPECT_FLOAT_EQ(Drag.m_DraggedWidth, 180.0f);
	EXPECT_FLOAT_EQ(Drag.m_DraggedHeight, 24.0f);
}

TEST(QmFriendPlayerDrag, RejectsClanMembersAndClearsPreviousDrag)
{
	SFriendsPlayerDragState Drag;
	int ItemId = 0;
	const CUIRect Row{10.0f, 20.0f, 180.0f, 24.0f};
	for(int FriendState : {IFriends::FRIEND_NO, IFriends::FRIEND_CLAN})
	{
		Drag.Begin(&ItemId, IFriends::FRIEND_PLAYER, "Alice", "Clan", "Friends", Row, vec2(30.0f, 30.0f));
		ASSERT_TRUE(Drag.Update(vec2(30.0f, 35.0f), true));
		Drag.Begin(&ItemId, FriendState, "Alice", "Clan", "Friends", Row, vec2(30.0f, 30.0f));
		EXPECT_EQ(Drag.m_pPressedItem, nullptr);
		EXPECT_FALSE(Drag.m_Dragging);
		EXPECT_STREQ(Drag.m_aName, "");
		EXPECT_STREQ(Drag.m_aClan, "");
		EXPECT_STREQ(Drag.m_aCategory, "");
		EXPECT_FALSE(Drag.Update(vec2(30.0f, 35.0f), true));
	}
	Drag.Begin(&ItemId, IFriends::FRIEND_PLAYER, "", "Clan", "Friends", Row, vec2(30.0f, 30.0f));
	EXPECT_EQ(Drag.m_pPressedItem, nullptr);
	Drag.Begin(nullptr, IFriends::FRIEND_PLAYER, "Alice", "Clan", "Friends", Row, vec2(30.0f, 30.0f));
	EXPECT_EQ(Drag.m_pPressedItem, nullptr);
}

TEST(QmFriendPlayerDrag, RejectsAutomaticGroupsAndUnchangedMembership)
{
	SFriendsPlayerDragState Drag;
	int ItemId = 0;
	const CUIRect Row{10.0f, 20.0f, 180.0f, 24.0f};
	Drag.Begin(&ItemId, IFriends::FRIEND_PLAYER, "Alice", "Clan", "Practice", Row, vec2(30.0f, 30.0f));
	EXPECT_FALSE(Drag.CanDropTo("Friends"));
	ASSERT_TRUE(Drag.Update(vec2(30.0f, 35.0f), true));
	EXPECT_FALSE(Drag.CanDropTo(nullptr));
	EXPECT_FALSE(Drag.CanDropTo(""));
	EXPECT_FALSE(Drag.CanDropTo("clan members"));
	EXPECT_FALSE(Drag.CanDropTo("OFFLINE"));
	EXPECT_FALSE(Drag.CanDropTo("PRACTICE"));
	EXPECT_TRUE(Drag.CanDropTo("Friends"));
	EXPECT_TRUE(Drag.CanDropTo("Race"));
}

TEST(QmFriendPlayerDrag, OfflineSourceCanMoveToOrdinaryGroups)
{
	SFriendsPlayerDragState Drag;
	int ItemId = 0;
	const CUIRect Row{10.0f, 20.0f, 180.0f, 24.0f};
	Drag.Begin(&ItemId, IFriends::FRIEND_PLAYER, "Alice", "Clan", IFriends::OFFLINE_CATEGORY, Row, vec2(30.0f, 30.0f));
	ASSERT_TRUE(Drag.Update(vec2(30.0f, 35.0f), true));
	EXPECT_TRUE(Drag.CanDropTo(IFriends::DEFAULT_CATEGORY));
	EXPECT_TRUE(Drag.CanDropTo("Practice"));
	EXPECT_FALSE(Drag.CanDropTo(IFriends::OFFLINE_CATEGORY));
}

TEST(QmNewUiMenuBranches, FriendCategorySortingPreservesClickAndExpandedState)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const size_t Start = Source.find("void CMenus::RenderServerbrowserFriends(CUIRect View)");
	const size_t End = Source.find("CUi::EPopupMenuFunctionResult CMenus::PopupFriendsCategory", Start);
	ASSERT_NE(Start, std::string::npos);
	ASSERT_NE(End, std::string::npos);
	const std::string Body = Source.substr(Start, End - Start);
	EXPECT_EQ(Body.find("ModifierIsPressed"), std::string::npos);
	EXPECT_NE(Body.find("s_CategoryDragState.Update"), std::string::npos);
	EXPECT_NE(Body.find("s_CategoryDragState.Begin(CategoryIndex, Header"), std::string::npos);
	EXPECT_NE(Body.find("!DraggingAnyHeader && HeaderResult == 1"), std::string::npos);
	EXPECT_NE(Body.find("m_vFriendsCategoryExpanded[CategoryIndex] && !DraggingAnyHeader"), std::string::npos);
	EXPECT_EQ(Body.find("m_vFriendsCategoryExpanded[CollapseIndex] = false"), std::string::npos);
	EXPECT_NE(Body.find("MoveCategoryExpandedState(FromIndex, ToIndex);"), std::string::npos);
	EXPECT_NE(Body.find("SaveFriendsCategoryExpandedState();"), std::string::npos);
	EXPECT_NE(Body.find("if(!Ui()->MouseHovered(&ListViewport))"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ProtectedFriendCategoriesCannotBeRenamedOrDeleted)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const size_t ProtectedFn = Source.find("static bool IsProtectedFriendsCategory");
	ASSERT_NE(ProtectedFn, std::string::npos);
	const size_t ProtectedFnEnd = Source.find("static const char *LocalizeFriendsCategory", ProtectedFn);
	ASSERT_NE(ProtectedFnEnd, std::string::npos);
	const std::string ProtectedBody = Source.substr(ProtectedFn, ProtectedFnEnd - ProtectedFn);

	EXPECT_NE(ProtectedBody.find("IFriends::DEFAULT_CATEGORY"), std::string::npos);
	EXPECT_NE(ProtectedBody.find("IsClanMembersCategory(pCategory)"), std::string::npos);
	EXPECT_NE(ProtectedBody.find("IsOfflineFriendsCategory(pCategory)"), std::string::npos);

	const size_t Popup = Source.find("CUi::EPopupMenuFunctionResult CMenus::PopupFriendsCategory");
	ASSERT_NE(Popup, std::string::npos);
	const std::string PopupBody = Source.substr(Popup);
	EXPECT_NE(PopupBody.find("const bool IsProtectedCategory = IsProtectedFriendsCategory(pCategory);"), std::string::npos);
	EXPECT_NE(PopupBody.find("Localize(\"Rename\"), &Button, FontSize, TEXTALIGN_MC, 0.0f, false, !IsProtectedCategory"), std::string::npos);
	EXPECT_NE(PopupBody.find("Localize(\"Delete category\"), &Button, FontSize, TEXTALIGN_MC, 0.0f, false, !IsProtectedCategory"), std::string::npos);
}

TEST(QmNewUiMenuBranches, FriendAddPopupExposesCreateCategoryAction)
{
	const std::string Header = ReadTextFile("src/game/client/components/menus.h");
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");

	EXPECT_NE(Header.find("m_FriendsAddCategoryCreateButton"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Create category\")"), std::string::npos);
	EXPECT_NE(Source.find("m_FriendsCategoryPopupContext.m_Mode = CFriendsCategoryPopupContext::MODE_ADD"), std::string::npos);
	EXPECT_NE(Source.find("Ui()->DoPopupMenu(&m_FriendsCategoryPopupContext"), std::string::npos);
}

TEST(QmNewUiMenuBranches, FriendCategoryHeaderActionExcludesManageButton)
{
	const CUIRect Header{10.0f, 20.0f, 180.0f, 24.0f};
	CUIRect HeaderAction;
	CUIRect ManageButton;

	CMenus::SplitFriendsCategoryHeaderRects(Header, &HeaderAction, &ManageButton);

	EXPECT_FLOAT_EQ(HeaderAction.x, Header.x);
	EXPECT_FLOAT_EQ(HeaderAction.y, Header.y);
	EXPECT_FLOAT_EQ(HeaderAction.w, Header.w - Header.h);
	EXPECT_FLOAT_EQ(HeaderAction.h, Header.h);
	EXPECT_FLOAT_EQ(ManageButton.x, Header.x + Header.w - Header.h + 2.0f);
	EXPECT_FLOAT_EQ(ManageButton.y, Header.y + 2.0f);
	EXPECT_FLOAT_EQ(ManageButton.w, Header.h - 4.0f);
	EXPECT_FLOAT_EQ(ManageButton.h, Header.h - 4.0f);
	EXPECT_LE(HeaderAction.x + HeaderAction.w, ManageButton.x);
}

TEST(QmNewUiMenuBranches, FriendCategoryEditPopupHasRoomForInputAndActions)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	constexpr float RequiredHeight = 5.0f * 2.0f + 12.0f + 3.0f + 18.0f + 6.0f + 20.0f;

	EXPECT_FLOAT_EQ(CMenus::FriendsCategoryEditPopupHeight(), RequiredHeight);
	EXPECT_GT(CMenus::FriendsCategoryActionsPopupHeight(), 60.0f);
	EXPECT_NE(Source.find("CMenus::FriendsCategoryEditPopupHeight()"), std::string::npos);
	EXPECT_NE(Source.find("CMenus::SecondaryPanelRect("), std::string::npos);
	EXPECT_EQ(Source.find("250.0f, 62.0f"), std::string::npos);
	EXPECT_EQ(Source.find("250.0f, 110.0f"), std::string::npos);
	EXPECT_EQ(Source.find("280.0f, CMenus::FriendsCategoryEditPopupHeight()"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SecondaryPanelRectClampsNearScreenEdges)
{
	const CUIRect Screen{0.0f, 0.0f, 800.0f, 600.0f};
	const CUIRect Panel = CMenus::SecondaryPanelRect(780.0f, 590.0f, 300.0f, 140.0f, Screen);

	EXPECT_FLOAT_EQ(Panel.w, 300.0f);
	EXPECT_FLOAT_EQ(Panel.h, 140.0f);
	EXPECT_LE(Panel.x + Panel.w, 792.0f);
	EXPECT_LE(Panel.y + Panel.h, 592.0f);
	EXPECT_GE(Panel.x, 8.0f);
	EXPECT_GE(Panel.y, 8.0f);
}

TEST(QmNewUiMenuBranches, FriendAutoFollowDelaysAndStopsAfterTwoJumps)
{
	CMenus::SFriendAutoFollowState State;
	char aConnect[NETADDR_MAXSTRSIZE] = "";

	CMenus::StartFriendAutoFollow(State, "Alice", "Clan", "127.0.0.1:8303");
	EXPECT_TRUE(State.m_Active);
	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8303", 10.0f, 3, 2, aConnect, sizeof(aConnect)));

	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8304", 11.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_TRUE(State.m_HasPendingAddress);
	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8304", 13.9f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_TRUE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8304", 14.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_STREQ(aConnect, "127.0.0.1:8304");
	EXPECT_TRUE(State.m_Active);
	EXPECT_EQ(State.m_JumpCount, 1);

	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8305", 20.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_TRUE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8305", 23.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_STREQ(aConnect, "127.0.0.1:8305");
	EXPECT_FALSE(State.m_Active);
	EXPECT_EQ(State.m_JumpCount, 2);
}

TEST(QmNewUiMenuBranches, FriendAutoFollowCancelsWhenTargetGoesOffline)
{
	CMenus::SFriendAutoFollowState State;
	char aConnect[NETADDR_MAXSTRSIZE] = "";

	CMenus::StartFriendAutoFollow(State, "Alice", "Clan", "127.0.0.1:8303");
	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8304", 11.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_TRUE(State.m_HasPendingAddress);
	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, false, "", 12.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_FALSE(State.m_Active);
	EXPECT_FALSE(State.m_HasPendingAddress);
}

TEST(QmNewUiMenuBranches, FriendAutoFollowDistinguishesManualAndAutomaticConnects)
{
	const std::string Header = ReadTextFile("src/game/client/components/menus.h");
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string QmMenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	EXPECT_NE(Header.find("enum class EConnectIntent"), std::string::npos);
	EXPECT_NE(Header.find("void Connect(const char *pAddress, EConnectIntent Intent = EConnectIntent::Manual)"), std::string::npos);
	EXPECT_NE(Source.find("if(Intent == EConnectIntent::Manual)"), std::string::npos);
	EXPECT_NE(Source.find("StopFriendAutoFollow(m_FriendAutoFollowState);"), std::string::npos);
	EXPECT_NE(Source.find("Connect(g_Config.m_UiServerAddress, EConnectIntent::AutoFollow)"), std::string::npos);
	const std::string FriendNotifyBody = FunctionBody(QmMenusSource, "void CMenus::RenderQmFunctionFriendNotifyContent(");
	ASSERT_FALSE(FriendNotifyBody.empty());
	EXPECT_NE(FriendNotifyBody.find("RenderValue(\"qmclient-friend-auto-follow-delay\", \"Auto-follow delay\""), std::string::npos);
	EXPECT_NE(FriendNotifyBody.find("&g_Config.m_QmFriendAutoFollowDelay, 0, 30, \"s\""), std::string::npos);
}

TEST(QmNewUiMenuBranches, ServerBrowserQmClientColumnUsesDistributionCount)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string BrowserServerList = FunctionBody(Source, "void CMenus::RenderServerbrowserServerList(");
	ASSERT_FALSE(BrowserServerList.empty());

	// 「梦」列直接显示中心服下发的在线分布总数（Qm + Arg），并按服务器地址匹配。
	// 列定义与宽度配置的断言在 ServerBrowserColumnsStayVisibleAndSizedToContent 里。
	EXPECT_NE(Source.find("MACRO_CONFIG_INT(BrColWidthQmClients, br_col_width_qm_clients"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_BrColWidthQmClients = 24;"), std::string::npos);
	EXPECT_NE(BrowserServerList.find("GameClient()->m_QmClient.QmClientServerDistribution()"), std::string::npos);
	EXPECT_NE(BrowserServerList.find("const int Count = Distribution.m_UserCount + Distribution.m_DummyCount;"), std::string::npos);
	EXPECT_NE(BrowserServerList.find("net_addr_str(&pInfo->m_aAddresses[0], aAddress, sizeof(aAddress), true);"), std::string::npos);
	// 没有梦客户端的服务器留空，不写 0。
	EXPECT_NE(BrowserServerList.find("if(QmClients > 0)"), std::string::npos);
	// 排序键由 CQmClient 推给引擎（SORT_QM_CLIENTS），表头可点出正/倒序；列头是品牌字，直接显示不走 Localize。
	EXPECT_NE(Source.find("{COL_QM_CLIENTS, IServerBrowser::SORT_QM_CLIENTS, \"梦\", 1, 24.0f, {0}},"), std::string::npos);
	EXPECT_NE(Source.find("if(DoButton_GridHeader(&Col.m_Id, Col.m_Id == COL_QM_CLIENTS ? Col.m_pCaption : Localize(Col.m_pCaption), Checked, &Col.m_Rect))"), std::string::npos);
}

// 意图：「梦」列排序键来自 CQmClient 推送的中心服在线分布；
// 引擎保存最近一次推送，并在按该列排序前重新物化（服务器列表重载会清零 CServerInfo）。
TEST(QmNewUiMenuBranches, ServerBrowserQmClientColumnSortKeyComesFromDistribution)
{
	const std::string EngineHeader = ReadTextFile("src/engine/serverbrowser.h");
	const std::string EngineSource = ReadTextFile("src/engine/client/serverbrowser.cpp");
	const std::string QmClientHeader = ReadTextFile("src/game/client/components/qmclient/qmclient.h");
	const std::string QmClientSource = ReadTextFile("src/game/client/components/qmclient/qmclient.cpp");

	EXPECT_NE(EngineHeader.find("SORT_QM_CLIENTS,"), std::string::npos);
	EXPECT_NE(EngineHeader.find("int m_QmClientCount;"), std::string::npos);
	EXPECT_NE(EngineHeader.find("virtual void SetQmClientServerCounts(const std::unordered_map<std::string, int> &Counts) = 0;"), std::string::npos);
	EXPECT_NE(EngineSource.find("bool CServerBrowser::SortCompareQmClients(int Index1, int Index2) const"), std::string::npos);
	EXPECT_NE(EngineSource.find("void CServerBrowser::UpdateQmClientServerCounts()"), std::string::npos);
	EXPECT_NE(EngineSource.find("UpdateQmClientServerCounts();"), std::string::npos);
	EXPECT_NE(EngineSource.find("CSortWrap(this, &CServerBrowser::SortCompareQmClients)"), std::string::npos);

	EXPECT_NE(QmClientHeader.find("void PushQmClientServerCounts();"), std::string::npos);
	const std::string PushBody = FunctionBody(QmClientSource, "void CQmClient::PushQmClientServerCounts()");
	ASSERT_FALSE(PushBody.empty());
	EXPECT_NE(PushBody.find("pServerBrowser->SetQmClientServerCounts(Counts);"), std::string::npos);
	const std::string FinishBody = FunctionBody(QmClientSource, "void CQmClient::FinishQmClientUsers()");
	ASSERT_FALSE(FinishBody.empty());
	EXPECT_NE(FinishBody.find("PushQmClientServerCounts();"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ServerBrowserColumnsStayVisibleAndSizedToContent)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string BrowserServerList = FunctionBody(Source, "void CMenus::RenderServerbrowserServerList(");
	ASSERT_FALSE(BrowserServerList.empty());

	// 所有列常显：不做列可见性开关，靠小字号 + 紧凑行高 + 贴内容定宽解决拥挤。
	EXPECT_NE(Source.find("static constexpr float SERVER_LIST_TEXT_SIZE = 11.0f;"), std::string::npos);
	EXPECT_NE(BrowserServerList.find("const float FontSize = SERVER_LIST_TEXT_SIZE;"), std::string::npos);
	EXPECT_NE(ReadTextFile("src/game/client/components/menus.cpp").find("float CMenus::ms_ListheaderHeight = 15.0f;"), std::string::npos);

	// 类型列必须放得下 "DDraceNetwork"，不能再裁成 "DDraceN"。
	EXPECT_NE(Source.find("{COL_GAMETYPE, IServerBrowser::SORT_GAMETYPE, Localizable(\"Type\"), 1, 68.0f, {0}},"), std::string::npos);

	// 名称与地图按比例分剩余宽度；地图不再吃固定公式，否则宽屏下会把名称挤到截断。
	EXPECT_NE(BrowserServerList.find("const float Split = std::clamp((float)g_Config.m_BrColNameSplit / 1000.0f, 0.35f, 0.75f);"), std::string::npos);
	EXPECT_EQ(Source.find("(Headers.w - 480)"), std::string::npos);
	EXPECT_NE(ReadTextFile("src/engine/shared/config_variables.h").find("br_col_name_split"), std::string::npos);
	EXPECT_EQ(Source.find("g_Config.m_BrColWidthMap"), std::string::npos);

	// 名称列的拖拽改的是比例，不是固定宽度。
	EXPECT_NE(BrowserServerList.find("const bool IsNameSplit = s_aCols[ColIdx].m_Id == COL_NAME;"), std::string::npos);
	EXPECT_NE(BrowserServerList.find("g_Config.m_BrColNameSplit = std::clamp((int)((NewWidth - MinNameFlexWidth) / FreeWidth * 1000.0f + 0.5f), 0, 1000);"), std::string::npos);

	// 列间距压到 2px 后，拖拽手柄必须跟着收窄，否则相邻列的手柄会互相抢悬停。
	EXPECT_NE(BrowserServerList.find("constexpr float ColumnGapWidth = 2.0f;"), std::string::npos);
	EXPECT_NE(BrowserServerList.find("Gap.x -= 2.0f;"), std::string::npos);
	EXPECT_EQ(BrowserServerList.find("Gap.w = 8.0f;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ShortServerNamesCoverKnownFamilies)
{
	CServerInfo Info{};
	char aBuf[sizeof(Info.m_aName)];

	str_copy(Info.m_aName, "KoG | China #12 - HappyHook [kog.tw]");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "China - HappyHook");

	str_copy(Info.m_aName, "Axiom 北京 普通 - CHN1O 钩累死");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "简单图 - CHN1O 钩累死");

	str_copy(Info.m_aName, "DDNet CHN7 西安 - Moderate 中阶");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "中阶图 - CHN7 西安");

	str_copy(Info.m_aName, "DDNet CHN2 上海 - Brutal 高阶");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "高阶 - CHN2 上海");

	str_copy(Info.m_aName, "Axiom Novice - CHN12 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "简单图 - CHN12 钩累死");

	str_copy(Info.m_aName, "Axiom Insane - CHN7 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "疯狂 - CHN7 钩累死");

	str_copy(Info.m_aName, "Axiom Axiom ◇ 广州 ✦ 困难 - CHN9 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "高阶 - CHN9 钩累死");

	str_copy(Info.m_aName, "Axiom Axiom ◇ 北京 ✦ 困难 - CHN10 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "高阶 - CHN10 钩累死");

	str_copy(Info.m_aName, "Axiom ◇ 广州 ✦ 活动 - CHN9 AXRace");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "活动 - CHN9 AXRace");

	str_copy(Info.m_aName, "Axiom ◇ 广州 ✦ 极限 - CHN9 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "极限 - CHN9 钩累死");

	str_copy(Info.m_aName, "Axiom ◇ 上海 ✦ 训练 - CHN2 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "训练 - CHN2 钩累死");

	str_copy(Info.m_aName, "Axiom ◇ 成都 ✦ 娱乐 - CHN12 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "娱乐 - CHN12 钩累死");

	str_copy(Info.m_aName, "DDNet Moderate - CHN7 西安");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "中阶图 - CHN7 西安");

	str_copy(Info.m_aName, "DDNet CHN2 上海 - DDmaX.Easy 古典");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "DDmaX.Easy 古典 - CHN2 上海");

	str_copy(Info.m_aName, "DDNet CHN7 西安 - DDmaX.Next");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "DDmaX.Next 古典 - CHN7 西安");

	str_copy(Info.m_aName, "DDNet CHN3 宁波 - DDmaX.Pro 古典");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "DDmaX.Pro 古典 - CHN3 宁波");

	str_copy(Info.m_aName, "DDNet CHN6 上海 - Oldschool 传统");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "古典图 - CHN6 上海");

	str_copy(Info.m_aName, "DDNet CHN6 上海 - Solo 单人");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "单人 - CHN6 上海");

	str_copy(Info.m_aName, "DDNet CHN5 上海 - Dummy 分身");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "分身 - CHN5 上海");

	str_copy(Info.m_aName, "DDNet Taiwan - Moderate");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "中阶图 - Taiwan");

	str_copy(Info.m_aName, "DDNet Taiwan - Brutal");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "高阶 - Taiwan");

	str_copy(Info.m_aName, "Brutal - CHN5 上海");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "高阶 - CHN5 上海");

	str_copy(Info.m_aName, "Plain Server Name");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "Plain Server Name");
}

TEST(QmNewUiMenuBranches, ShortServerNamesKeepDisplayNameHighlightPath)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");

	EXPECT_NE(Source.find("g_Config.m_QmShortServerNames || (pItem->m_QuickSearchHit & IServerBrowser::QUICK_SERVERNAME)"), std::string::npos);
	EXPECT_NE(Source.find("PrintHighlighted(pDisplayServerName"), std::string::npos);
	EXPECT_EQ(Source.find("!g_Config.m_QmShortServerNames && g_Config.m_BrFilterString"), std::string::npos);
}

TEST(QmNewUiMenuBranches, CallVoteMapListShowsFinishedIcon)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const size_t RenderPos = Source.find("bool CMenus::RenderServerControlServer(CUIRect MainView, bool UpdateScroll)");
	ASSERT_NE(RenderPos, std::string::npos);
	const size_t EndPos = Source.find("bool CMenus::RenderServerControlKick(CUIRect MainView", RenderPos);
	ASSERT_NE(EndPos, std::string::npos);
	const std::string Body = Source.substr(RenderPos, EndPos - RenderPos);

	EXPECT_NE(Body.find("ExtractMapName(pOption->m_aDescription"), std::string::npos);
	EXPECT_NE(Body.find("g_Config.m_BrIndicateFinished"), std::string::npos);
	EXPECT_NE(Body.find("pCurrentCommunity->HasRank(aMapName) == CServerInfo::RANK_RANKED"), std::string::npos);
	EXPECT_NE(Body.find("RenderFontIcon(Icon, FONT_ICON_FLAG_CHECKERED"), std::string::npos);
	EXPECT_NE(Body.find("GameClient()->m_TClient.IsFavoriteMap(aMapName)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ClientSourceDoesNotUseChineseLocalizeKeys)
{
	const std::string HudEditorSource = ReadTextFile("src/game/client/components/hud_editor.cpp");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string BrowserSource = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string DemoSource = ReadTextFile("src/game/client/components/menus_demo.cpp");
	const std::string IngameTouchSource = ReadTextFile("src/game/client/components/menus_ingame_touch_controls.cpp");
	const std::string IngameSource = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string SettingsControlsSource = ReadTextFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string Settings7Source = ReadTextFile("src/game/client/components/menus_settings7.cpp");
	const std::string StartSource = ReadTextFile("src/game/client/components/menus_start.cpp");
	const std::string PieMenuSource = ReadTextFile("src/game/client/components/pie_menu.cpp");
	const std::string ScoreboardSource = ReadTextFile("src/game/client/components/scoreboard.cpp");

	EXPECT_NE(HudEditorSource.find("Localize(\"Position jump tip\")"), std::string::npos);
	EXPECT_NE(MenusSource.find("m_apSettingsTabs[SETTINGS_SOUND] = Localize(\"Sound\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"DDmaX Easy\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"Favorite map\")"), std::string::npos);
	EXPECT_NE(DemoSource.find("Localize(\"Screenshots directory\")"), std::string::npos);
	EXPECT_NE(IngameTouchSource.find("Localize(\"Allow dummy\", \"Touch button visibilities\")"), std::string::npos);
	EXPECT_NE(IngameTouchSource.find("Localize(\"Dummy connected\", \"Touch button visibilities\")"), std::string::npos);
	EXPECT_NE(IngameTouchSource.find("Localize(\"Spectate\", \"Predefined touch button behaviors\")"), std::string::npos);
	EXPECT_NE(IngameSource.find("Localize(\"Spectate\")"), std::string::npos);
	EXPECT_NE(IngameSource.find("Localize(\"Dummies are not allowed on this server\")"), std::string::npos);
	EXPECT_NE(SettingsSource.find("Localize(\"Show spectator cursor\")"), std::string::npos);
	EXPECT_NE(SettingsSource.find("Localize(\"Auto save chat log\")"), std::string::npos);
	EXPECT_NE(Settings7Source.find("Localize(\"Dummy\")"), std::string::npos);
	EXPECT_NE(Settings7Source.find("Localize(\"Dummy\")"), std::string::npos);
	EXPECT_NE(StartSource.find("Localize(\"(Update required)\")"), std::string::npos);
	EXPECT_NE(PieMenuSource.find("Localize(\"Spectate\")"), std::string::npos);
	EXPECT_NE(ScoreboardSource.find("Localize(\"Spectators\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, PieMenuSeparatesSelfRenameFromOtherPlayerActions)
{
	const std::string Source = ReadTextFile("src/game/client/components/pie_menu.cpp");
	const std::string FindNearestPlayer = FunctionBody(Source, "int CPieMenu::FindNearestPlayer()");
	const std::string OpenMenu = FunctionBody(Source, "void CPieMenu::OpenMenu()");
	const std::string OnInput = FunctionBody(Source, "bool CPieMenu::OnInput(");
	const std::string UpdateSelection = FunctionBody(Source, "void CPieMenu::UpdateSelection()");
	const std::string OnRender = FunctionBody(Source, "void CPieMenu::OnRender()");
	const std::string RenderCenterInfo = FunctionBody(Source, "void CPieMenu::RenderCenterInfo()");
	const std::string ExecuteRenameOption = FunctionBody(Source, "void CPieMenu::ExecuteRenameOption(");

	// Both local connections belong to the user and must never become inner-ring targets.
	EXPECT_NE(FindNearestPlayer.find("GameClient()->IsLocalClientId(i)"), std::string::npos);

	// A connected local identity and at least one usable ring are required to open the menu.
	EXPECT_NE(OpenMenu.find("Client()->State() != IClient::STATE_ONLINE"), std::string::npos);
	EXPECT_NE(OpenMenu.find("LocalClientId < 0 || LocalClientId >= MAX_CLIENTS"), std::string::npos);
	EXPECT_NE(OpenMenu.find("if(TargetId < 0 && m_vRenameQueue.empty())"), std::string::npos);

	// Without another player the hidden inner ring cannot be selected or triggered by number keys.
	EXPECT_NE(OnInput.find("if(!HasTargetPlayer())"), std::string::npos);
	EXPECT_NE(UpdateSelection.find("if(HasTargetPlayer() && MouseDistance <= OuterRadius)"), std::string::npos);
	EXPECT_NE(OnRender.find("if(HasTargetPlayer())"), std::string::npos);

	// Targetless mode displays self, and hovering the outer ring identifies rename as a self action.
	EXPECT_NE(RenderCenterInfo.find("const int DisplayClientId = HasTargetPlayer() ? m_TargetClientId : LocalClientId;"), std::string::npos);
	EXPECT_NE(RenderCenterInfo.find("Localize(\"Self\")"), std::string::npos);
	EXPECT_EQ(ExecuteRenameOption.find("m_TargetClientId"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmClientAxiomAutoLoginLivesInQmClientComponent)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/axiom_auto_login.cpp");
	const std::string Header = ReadTextFile("src/game/client/components/qmclient/axiom_auto_login.h");
	const std::string TClientHeader = ReadTextFile("src/game/client/components/tclient/tclient.h");
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string GameClientHeader = ReadTextFile("src/game/client/gameclient.h");
	const std::string QmConfigHeader = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string QmMenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string IngameMenusSource = ReadTextFile("src/game/client/components/menus_ingame.cpp");

	EXPECT_NE(Header.find("class CQmAxiomAutoLogin : public CComponent"), std::string::npos);
	EXPECT_NE(GameClientHeader.find("CQmAxiomAutoLogin m_QmAxiomAutoLogin;"), std::string::npos);
	EXPECT_NE(Source.find("void CQmAxiomAutoLogin::TrySendLogin()"), std::string::npos);
	EXPECT_NE(Source.find("void CQmAxiomAutoLogin::TrySendDummyLogin()"), std::string::npos);
	EXPECT_NE(Source.find("SendChatOnConn(IClient::CONN_DUMMY"), std::string::npos);
	EXPECT_NE(Header.find("TrySendDummyLogin"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("QmAxiomDummyLoginPassword"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("qm_axiom_dummy_login_password"), std::string::npos);
	EXPECT_NE(QmMenusSource.find("Axiom dummy password"), std::string::npos);
	EXPECT_NE(Source.find("m_DummyLoginAllowedThisServer"), std::string::npos);
	EXPECT_NE(Source.find("m_DummyWasConnected"), std::string::npos);
	EXPECT_EQ(Source.find("if(DummyConnected && !m_DummyWasConnected)"), std::string::npos);
	EXPECT_NE(Source.find("m_DummyLoginAllowedThisServer = true;"), std::string::npos);
	EXPECT_NE(Source.find("if(!m_DummyLoginAllowedThisServer)"), std::string::npos);
	EXPECT_NE(Header.find("EnableDummyReconnectForServer"), std::string::npos);
	EXPECT_NE(Header.find("DisableDummyReconnectForServer"), std::string::npos);
	EXPECT_NE(Source.find("Client()->DummyConnect();"), std::string::npos);
	EXPECT_NE(IngameMenusSource.find("GameClient()->m_QmAxiomAutoLogin.EnableDummyReconnectForServer();"), std::string::npos);
	EXPECT_NE(IngameMenusSource.find("GameClient()->OnDummyManualDisconnect();"), std::string::npos);
	EXPECT_NE(GameClientHeader.find("void OnDummyManualDisconnect() override;"), std::string::npos);
	const std::string GameClientSource = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string ManualDisconnectBody = FunctionBody(GameClientSource, "void CGameClient::OnDummyManualDisconnect()");
	ASSERT_FALSE(ManualDisconnectBody.empty());
	EXPECT_NE(ManualDisconnectBody.find("m_QmAxiomAutoLogin.DisableDummyReconnectForServer();"), std::string::npos);
	const std::string ClientSource = ReadTextFile("src/engine/client/client.cpp");
	const std::string DummyDisconnectBody = FunctionBody(ClientSource, "void CClient::Con_DummyDisconnect(");
	ASSERT_FALSE(DummyDisconnectBody.empty());
	EXPECT_NE(DummyDisconnectBody.find("GameClient()->OnDummyManualDisconnect();"), std::string::npos);
	EXPECT_NE(DummyDisconnectBody.find("DummyDisconnect(nullptr);"), std::string::npos);
	EXPECT_NE(Source.find("bool CQmAxiomAutoLogin::IsAxiomCommunity() const"), std::string::npos);
	EXPECT_NE(Header.find("QMCLIENT_AXIOM_AUTO_LOGIN_SLOW_RETRY_SECONDS"), std::string::npos);
	EXPECT_NE(Header.find("SQmAxiomAutoLoginState m_AutoLoginState;"), std::string::npos);
	EXPECT_NE(Header.find("m_SlowRetryMode"), std::string::npos);
	EXPECT_NE(Header.find("m_HardFailed"), std::string::npos);
	EXPECT_NE(Header.find("QmScheduleAxiomAutoLoginRetry"), std::string::npos);
	EXPECT_NE(Source.find("QmClassifyAxiomLoginReply(pText)"), std::string::npos);
	EXPECT_NE(Source.find("QmApplyAxiomLoginReply"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Trying Axiom auto login\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Trying Axiom dummy auto login\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Axiom auto login succeeded\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Axiom auto login failed, retrying\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Axiom auto login failed\")"), std::string::npos);

	EXPECT_EQ(TClientHeader.find("IsAxiomCommunity() const"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("ResetAxiomAutoLoginState"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("UpdateAxiomAutoLogin"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("HandleAxiomAutoLoginMessage"), std::string::npos);
	EXPECT_EQ(TClientSource.find("TrySendAxiomLogin"), std::string::npos);
	EXPECT_EQ(TClientSource.find("HandleAxiomAutoLoginMessage"), std::string::npos);
}

TEST(QmNewUiMenuBranches, FastPracticeSurfacesPracticeStateInHud)
{
	const std::string HudSource = ReadTextFile("src/game/client/components/hud.cpp");
	const std::string HudHeader = ReadTextFile("src/game/client/components/hud.h");

	const std::string PlayerStateBody = FunctionBody(HudSource, "void CHud::RenderPlayerState(");
	ASSERT_FALSE(PlayerStateBody.empty());
	EXPECT_NE(PlayerStateBody.find("const bool FastPracticeParticipant = GameClient()->m_FastPractice.IsPracticeParticipant(ClientId);"), std::string::npos);
	EXPECT_NE(PlayerStateBody.find("|| FastPracticeParticipant"), std::string::npos);
	EXPECT_EQ(PlayerStateBody.find("m_FastPractice.Enabled()"), std::string::npos);
	EXPECT_NE(PlayerStateBody.find("m_PracticeModeOffset"), std::string::npos);

	const std::string MovementBody = FunctionBody(HudSource, "void CHud::RenderMovementInformation()");
	ASSERT_FALSE(MovementBody.empty());
	EXPECT_NE(MovementBody.find("const bool FastPracticeParticipant = GameClient()->m_FastPractice.IsPracticeParticipant(ClientId);"), std::string::npos);
	EXPECT_NE(MovementBody.find("const bool ShowSpeed = !PosOnly && (g_Config.m_ClShowhudPlayerSpeed || FastPracticeParticipant);"), std::string::npos);
	EXPECT_EQ(MovementBody.find("m_FastPractice.Enabled()"), std::string::npos);
	EXPECT_NE(HudHeader.find("void RenderMovementInformation();"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NameplateTextEffectsUseSharedRenderHelper)
{
	const std::string RenderHeader = ReadTextFile("src/game/client/render.h");
	const std::string RenderSource = ReadTextFile("src/game/client/render.cpp");
	const std::string NameplatesSource = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string ModesHeader = ReadTextFile("src/game/client/components/qmclient/modes.h");
	const std::string ModesSource = ReadTextFile("src/game/client/components/qmclient/modes.cpp");
	const std::string QmConfigHeader = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string QmMenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	EXPECT_NE(RenderHeader.find("struct SQmTextEffectRenderStyle"), std::string::npos);
	EXPECT_NE(RenderHeader.find("m_OutlineColor"), std::string::npos);
	EXPECT_NE(RenderHeader.find("RenderTextContainerWithEffects"), std::string::npos);
	EXPECT_NE(RenderSource.find("void CRenderTools::RenderTextContainerWithEffects"), std::string::npos);
	EXPECT_NE(RenderSource.find("ColorRGBA OutlineColor = Style.m_OutlineColor.WithMultipliedAlpha(Alpha);"), std::string::npos);
	EXPECT_NE(RenderSource.find("if(BorderEnabled)\n\t\tOutlineColor = Style.m_BorderColor.WithMultipliedAlpha(Alpha);"), std::string::npos);
	EXPECT_NE(RenderSource.find("QM_TEXT_EFFECT_RAINBOW"), std::string::npos);
	EXPECT_NE(RenderSource.find("QM_TEXT_EFFECT_GLOW"), std::string::npos);
	EXPECT_NE(RenderSource.find("for(int Pass = 0; Pass < GlowPasses; ++Pass)"), std::string::npos);

	EXPECT_NE(QmConfigHeader.find("QmNameplateTextEffects"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("QmNameplateTextBorderColor"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("QmNameplateTextGradientColor"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("QmNameplateTextGlowColor"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("QmNameplateTextGlowRange"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("QmNameplateTextPlayingScope"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("QmNameplateTextSpectateScope"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("QmNameplateTextDemoMode"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("QmNameplateTextDemoTarget"), std::string::npos);

	EXPECT_NE(ModesHeader.find("enum EQmNameplateTextPlayingScope"), std::string::npos);
	EXPECT_NE(ModesHeader.find("enum EQmNameplateTextSpectateScope"), std::string::npos);
	EXPECT_NE(ModesHeader.find("enum EQmNameplateTextDemoMode"), std::string::npos);
	EXPECT_NE(ModesHeader.find("bool ShouldUseQmNameplateTextEffects("), std::string::npos);
	EXPECT_NE(ModesSource.find("bool ShouldUseQmNameplateTextEffects("), std::string::npos);
	EXPECT_NE(ModesSource.find("case QM_NAMEPLATE_TEXT_PLAYING_SCOPE_FRIENDS:"), std::string::npos);
	EXPECT_NE(ModesSource.find("case QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_TARGET:"), std::string::npos);
	EXPECT_NE(ModesSource.find("case QM_NAMEPLATE_TEXT_DEMO_MODE_MANUAL_TARGET:"), std::string::npos);

	EXPECT_NE(NameplatesSource.find("BuildQmNameplateTextStyle("), std::string::npos);
	EXPECT_NE(NameplatesSource.find("bool UseEffects"), std::string::npos);
	EXPECT_NE(NameplatesSource.find("Style.m_Effects = UseEffects ? (g_Config.m_QmNameplateTextEffects & ~QM_TEXT_EFFECT_GRADIENT) : 0;"), std::string::npos);
	EXPECT_NE(NameplatesSource.find("AddNameplateGradientSplits(Cursor, m_aText, m_Color, m_GradientColor);"), std::string::npos);
	EXPECT_EQ(NameplatesSource.find("QmRainbowName"), std::string::npos);
	EXPECT_NE(NameplatesSource.find("Data.m_UseTextEffects = ShouldUseQmNameplateTextEffects("), std::string::npos);
	EXPECT_NE(NameplatesSource.find("Style.m_OutlineColor = s_OutlineColor;"), std::string::npos);
	EXPECT_NE(NameplatesSource.find("QmNameplateTextEffectPadding(g_Config.m_QmNameplateTextEffects"), std::string::npos);
	EXPECT_NE(NameplatesSource.find("m_Size = m_RenderSize + vec2(EffectPadding * 2.0f, EffectPadding * 2.0f + ExtraPadding * 2.0f);"), std::string::npos);
	EXPECT_NE(NameplatesSource.find("Pos.x - m_RenderSize.x / 2.0f"), std::string::npos);
	EXPECT_NE(NameplatesSource.find("Data.m_UseTextEffects = g_Config.m_QmNameplateTextEffects != 0;"), std::string::npos);
	EXPECT_NE(NameplatesSource.find("RenderTools()->RenderTextContainerWithEffects"), std::string::npos);
	EXPECT_EQ(NameplatesSource.find("Rainbow name for local player"), std::string::npos);

	const std::string AppearanceSettings = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string NamePlateBranch = BlockBodyAfter(AppearanceSettings, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_NAME_PLATE)");
	ASSERT_FALSE(NamePlateBranch.empty());
	EXPECT_EQ(QmMenusSource.find("RenderNameplateTextSettings(CardContent);"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Nameplate text"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("QmNameplateTextEffects"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("QmNameplateTextGlowRange"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("DoLine_ColorPicker(&s_NameplateTextBorderColorId"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Playing effects"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Spectate effects"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Demo effects"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Demo target"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("auto RenderNameplateTextControlRow = [&](const char *pTextId, const char *pLabel, const auto &RenderControl)"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("DoAppearanceNumericField(APPEARANCE_TAB_NAME_PLATE, \"appearance-nameplate-text-border-range\""), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("DoAppearanceNumericField(APPEARANCE_TAB_NAME_PLATE, \"appearance-nameplate-text-glow-range\""), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Localize(\"Glow\")"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("NameplateTextLabelProps.m_DisallowNewline = true"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("NameplateTextLabelProps.m_MinimumFontSize = 6.0f"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("s_NameplateTextDemoTargetDropDownNames"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("bool DemoTargetListed = g_Config.m_QmNameplateTextDemoTarget < 0;"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("if(!DemoTargetListed)"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("g_Config.m_QmNameplateTextDemoTarget = DemoTargetNew - 1;"), std::string::npos);
	EXPECT_NE(AppearanceSettings.find("DoAppearanceNumericField(APPEARANCE_TAB_NAME_PLATE, \"appearance-hook-strength-size\""), std::string::npos);
}

TEST(QmNewUiMenuBranches, NameplatePreviewRebuildsTextContainerInsteadOfAppendingSizes)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string Body = BlockBodyAfter(Source, "void Update(CGameClient &This, const CNamePlateData &Data) override");
	ASSERT_FALSE(Body.empty());

	const size_t DeletePos = Body.find("This.TextRender()->DeleteTextContainer(m_TextContainerIndex);");
	const size_t UpdateTextPos = Body.find("UpdateText(This, Data);");
	ASSERT_NE(DeletePos, std::string::npos);
	ASSERT_NE(UpdateTextPos, std::string::npos);
	EXPECT_LT(DeletePos, UpdateTextPos);
	EXPECT_EQ(Body.find("else\n\t\t{\n\t\t\tUpdateText(This, Data);\n\t\t}"), std::string::npos);
	EXPECT_NE(Body.find("QmNameplateTextEffectPadding"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NameplateTextEffectsReserveTheirRenderedExtent)
{
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(0, 4, 12), 0.0f);
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_BORDER, 1, 12), 1.0f);
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_BORDER, 8, 12), 4.0f);
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_GLOW, 4, 0), 1.0f);
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_GLOW, 4, 7), 7.0f);
	EXPECT_FLOAT_EQ(QmNameplateTextEffectPadding(QM_TEXT_EFFECT_BORDER | QM_TEXT_EFFECT_GLOW, 3, 7), 7.0f);

	const std::string QmConfigHeader = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string AppearanceSettings = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	EXPECT_NE(QmConfigHeader.find("QmNameplateTextGlowRange, qm_nameplate_text_glow_range, 4, 1, 12"), std::string::npos);
	EXPECT_NE(AppearanceSettings.find("Localize(\"Glow range\"), 1, 12"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NameplateTextEffectAutoLodCutsOuterLayersWhenCrowded)
{
	// 满档特效绘制次数：辉光每圈 4 次方向绘制（最多 6 圈），描边每圈 8 次（最多 4 圈），本体不计入预算。
	EXPECT_EQ(QmNameplateEffectFullDraws(0, 4, 12), 0);
	EXPECT_EQ(QmNameplateEffectFullDraws(QM_TEXT_EFFECT_GRADIENT | QM_TEXT_EFFECT_RAINBOW, 4, 12), 0);
	EXPECT_EQ(QmNameplateEffectFullDraws(QM_TEXT_EFFECT_BORDER, 1, 12), 0);
	EXPECT_EQ(QmNameplateEffectFullDraws(QM_TEXT_EFFECT_BORDER, 4, 0), 32);
	EXPECT_EQ(QmNameplateEffectFullDraws(QM_TEXT_EFFECT_GLOW, 4, 3), 12);
	EXPECT_EQ(QmNameplateEffectFullDraws(QM_TEXT_EFFECT_BORDER | QM_TEXT_EFFECT_GLOW, 1, 1), 4);
	EXPECT_EQ(QmNameplateEffectFullDraws(QM_TEXT_EFFECT_BORDER | QM_TEXT_EFFECT_GLOW, 4, 12), 56);

	// 名牌数不超过满档阈值时一个绘制都不少；超过后按人数比例缩减每行允许的绘制次数。
	EXPECT_EQ(QmNameplateEffectLodIdealDraws(0, 56, 20), 56);
	EXPECT_EQ(QmNameplateEffectLodIdealDraws(20, 56, 20), 56);
	EXPECT_EQ(QmNameplateEffectLodIdealDraws(30, 56, 20), 38);
	EXPECT_EQ(QmNameplateEffectLodIdealDraws(90, 56, 20), 13);
	EXPECT_EQ(QmNameplateEffectLodIdealDraws(128, 56, 20), 9);
	// 极端人数下也至少留下最内圈一层特效（向上取整）。
	EXPECT_EQ(QmNameplateEffectLodIdealDraws(600, 16, 20), 1);
	// 没有特效可降时不进入降级状态。
	EXPECT_EQ(QmNameplateEffectLodIdealDraws(90, 0, 20), QM_TEXT_EFFECT_DRAWS_UNLIMITED);

	// 分配顺序：先砍外层辉光，再砍外层描边，本体永远保留。
	SQmNameplateEffectPasses Passes = QmNameplateEffectResolvePasses(QM_TEXT_EFFECT_DRAWS_UNLIMITED, 4, 6);
	EXPECT_EQ(Passes.m_BorderPasses, 4);
	EXPECT_EQ(Passes.m_GlowPasses, 6);
	Passes = QmNameplateEffectResolvePasses(56, 4, 6);
	EXPECT_EQ(Passes.m_BorderPasses, 4);
	EXPECT_EQ(Passes.m_GlowPasses, 6);
	Passes = QmNameplateEffectResolvePasses(40, 4, 6);
	EXPECT_EQ(Passes.m_BorderPasses, 4);
	EXPECT_EQ(Passes.m_GlowPasses, 2);
	Passes = QmNameplateEffectResolvePasses(13, 4, 6);
	EXPECT_EQ(Passes.m_BorderPasses, 1);
	EXPECT_EQ(Passes.m_GlowPasses, 1);
	Passes = QmNameplateEffectResolvePasses(13, 0, 6);
	EXPECT_EQ(Passes.m_BorderPasses, 0);
	EXPECT_EQ(Passes.m_GlowPasses, 3);
	Passes = QmNameplateEffectResolvePasses(3, 0, 6);
	EXPECT_EQ(Passes.m_BorderPasses, 0);
	EXPECT_EQ(Passes.m_GlowPasses, 0);
	Passes = QmNameplateEffectResolvePasses(0, 4, 6);
	EXPECT_EQ(Passes.m_BorderPasses, 0);
	EXPECT_EQ(Passes.m_GlowPasses, 0);

	// 平滑：死区内不动（屏幕边缘单个玩家进出不该改变档位），真实变化按步长跟上。
	EXPECT_EQ(QmNameplateEffectLodSmoothCount(20, 21, 2, 8, 2), 20);
	EXPECT_EQ(QmNameplateEffectLodSmoothCount(20, 18, 2, 8, 2), 20);
	EXPECT_EQ(QmNameplateEffectLodSmoothCount(20, 22, 2, 8, 2), 20);
	EXPECT_EQ(QmNameplateEffectLodSmoothCount(20, 23, 2, 8, 2), 23);
	EXPECT_EQ(QmNameplateEffectLodSmoothCount(20, 90, 2, 8, 2), 28);
	EXPECT_EQ(QmNameplateEffectLodSmoothCount(90, 20, 2, 8, 2), 88);
	EXPECT_EQ(QmNameplateEffectLodSmoothCount(0, 3, 2, 8, 2), 3);
	EXPECT_EQ(QmNameplateEffectLodSmoothCount(5, 5, 2, 8, 2), 5);

	// 阈值附近 19/21 交替 60 帧：档位必须一直是满档，不能出现特效闪烁。
	int Smoothed = 20;
	for(int Frame = 0; Frame < 60; ++Frame)
	{
		const int Visible = (Frame % 2 == 0) ? 19 : 21;
		Smoothed = QmNameplateEffectLodSmoothCount(
			Smoothed, Visible,
			QM_NAMEPLATE_EFFECT_LOD_DEAD_ZONE_NAMEPLATES,
			QM_NAMEPLATE_EFFECT_LOD_STEP_UP_NAMEPLATES,
			QM_NAMEPLATE_EFFECT_LOD_STEP_DOWN_NAMEPLATES);
		EXPECT_EQ(QmNameplateEffectLodIdealDraws(Smoothed, 56, 20), 56);
	}

	// 渲染侧接线：pass 数由档位决定，但半径与淡化的分母仍取满档值，
	// 这样降级时消失的只有最外圈，留下的内圈与改动前逐层一致。
	const std::string RenderSource = ReadTextFile("src/game/client/render.cpp");
	EXPECT_NE(RenderSource.find("Style.m_MaxEffectDraws"), std::string::npos);
	EXPECT_NE(RenderSource.find("QmNameplateEffectResolvePasses("), std::string::npos);
	EXPECT_NE(RenderSource.find("const int GlowPasses = EffectPasses.m_GlowPasses;"), std::string::npos);
	EXPECT_NE(RenderSource.find("const int BorderPasses = EffectPasses.m_BorderPasses;"), std::string::npos);
	EXPECT_NE(RenderSource.find("Style.m_GlowRange * (float)(Pass + 1) / (float)MaxGlowPasses"), std::string::npos);
	EXPECT_NE(RenderSource.find("Style.m_GlowColor.a * Alpha * (1.0f - (float)Pass / (float)(MaxGlowPasses + 1))"), std::string::npos);
	EXPECT_NE(RenderSource.find("OutlineColor.a * (1.0f - (float)Pass / (float)(MaxBorderPasses + 1))"), std::string::npos);
	EXPECT_NE(RenderSource.find("for(int Pass = 0; Pass < GlowPasses; ++Pass)"), std::string::npos);
	EXPECT_NE(RenderSource.find("TextRender()->RenderTextContainer(TextContainerIndex, TextColor, OutlineColor, X, Y);"), std::string::npos);

	const std::string NameplatesSource = ReadTextFile("src/game/client/components/nameplates.cpp");
	EXPECT_NE(NameplatesSource.find("Style.m_MaxEffectDraws = "), std::string::npos);
	EXPECT_NE(NameplatesSource.find("QmNameplateEffectLodSmoothCount("), std::string::npos);
	EXPECT_NE(NameplatesSource.find("QmNameplateEffectLodIdealDraws("), std::string::npos);
	EXPECT_NE(NameplatesSource.find("QmNameplateEffectFullDraws("), std::string::npos);
	EXPECT_NE(NameplatesSource.find("g_Config.m_QmNameplateEffectAutoLod"), std::string::npos);
	EXPECT_NE(NameplatesSource.find("g_Config.m_QmNameplateEffectLodThreshold"), std::string::npos);

	const std::string QmConfigHeader = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	EXPECT_NE(QmConfigHeader.find("QmNameplateEffectAutoLod, qm_nameplate_effect_auto_lod, 1, 0, 1"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("QmNameplateEffectLodThreshold, qm_nameplate_effect_lod_threshold, 20, 4, 64"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmLaserSettingsMovedToAppearanceLaserTab)
{
	const std::string QmSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string VisualDeck = FunctionBody(QmSource, "void CMenus::RenderSettingsQmClientVisualDeck(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(VisualDeck.empty());
	EXPECT_EQ(VisualDeck.find("qm:laser"), std::string::npos);
	EXPECT_EQ(VisualDeck.find("qm:nameplate_text"), std::string::npos);
	// 薄壳只声明清单，卡片本体在卡片目录：目录清单与视觉卡片模块都不得收录这两个设置。
	const std::string VisualCatalog = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogVisual.cpp");
	const std::string CardCatalog = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalog.cpp");
	ASSERT_FALSE(VisualCatalog.empty());
	ASSERT_FALSE(CardCatalog.empty());
	EXPECT_EQ(VisualCatalog.find("qm:laser"), std::string::npos);
	EXPECT_EQ(VisualCatalog.find("qm:nameplate_text"), std::string::npos);
	EXPECT_EQ(CardCatalog.find("qm:laser"), std::string::npos);
	EXPECT_EQ(CardCatalog.find("qm:nameplate_text"), std::string::npos);

	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string LaserBranch = BlockBodyAfter(SettingsSource, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_LASER)");
	ASSERT_FALSE(LaserBranch.empty());
	EXPECT_NE(LaserBranch.find("AddCard(10, ResolveLaserEnhancedMinCardHeight()"), std::string::npos);
	EXPECT_NE(LaserBranch.find("AddCard(11, LaserColorMinCardHeight"), std::string::npos);
	EXPECT_NE(LaserBranch.find("AddCard(12, LaserPreviewMinCardHeight"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("RenderQmSettingsGlassCard(EnhancedCard, QmCardStyle);"), std::string::npos);
	EXPECT_NE(SettingsSource.find("QmResolveScrollPolicy(ScrollRequest, AppearanceUiScale"), std::string::npos);
	EXPECT_NE(SettingsSource.find("std::array<CScrollRegion, NUMBER_OF_APPEARANCE_TABS> s_AppearanceSettingsCardScrollRegions"), std::string::npos);
	EXPECT_NE(SettingsSource.find("CQmScrollState &ScrollState = s_AppearanceSettingsCardScrollRegions[m_AppearanceSettingsTab].State()"), std::string::npos);
	EXPECT_NE(SettingsSource.find("SettingsCardDeckForRenderPass().RenderCached(AppearanceCardCtx, AppearancePage, pAppearanceDeckTab"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("const float EnhancedContentHeight ="), std::string::npos);
	EXPECT_EQ(LaserBranch.find("const float ColorContentHeight ="), std::string::npos);
	EXPECT_EQ(LaserBranch.find("const float PreviewContentHeight ="), std::string::npos);
	EXPECT_EQ(LaserBranch.find("s_LaserMeasuredEnhancedCardHeight"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("s_LaserMeasuredColorCardHeight"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("s_LaserMeasuredPreviewCardHeight"), std::string::npos);
	EXPECT_NE(LaserBranch.find("const auto ResolveLaserEnhancedMinCardHeight"), std::string::npos);
	EXPECT_NE(LaserBranch.find("ResolveAppearanceLaserEnhancedHeight(AppearanceMetrics, g_Config.m_QmLaserEnhanced != 0)"), std::string::npos);
	EXPECT_NE(LaserBranch.find("vCards.back().m_PreLayoutInput"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("appearance-laser-enhancement-title"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_LASER, &g_Config.m_QmLaserEnhanced"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserGlowIntensity"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserSize"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserAlpha"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserRoundCaps"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserPulseSpeed"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserPulseAmplitude"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoLaserPreview(&LaserPreviewRect, LaserRifleOutlineColor, LaserRifleInnerColor, LASERTYPE_RIFLE);"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoLaserPreview(&LaserPreviewRect, LaserShotgunOutlineColor, LaserShotgunInnerColor, LASERTYPE_SHOTGUN);"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoLaserPreview(&LaserPreviewRect, LaserDoorOutlineColor, LaserDoorInnerColor, LASERTYPE_DOOR);"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoLaserPreview(&LaserPreviewRect, LaserFreezeOutlineColor, LaserFreezeInnerColor, LASERTYPE_FREEZE);"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoLaserPreview(&LaserPreviewRect, LaserDraggerOutlineColor, LaserDraggerInnerColor, LASERTYPE_DRAGGER);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmSettingsCardsUseSharedStyleHelpers)
{
	const std::string HeaderSource = ReadTextFile("src/game/client/components/menus.h");
	EXPECT_NE(HeaderSource.find("struct SQmSettingsCardStyle"), std::string::npos);
	EXPECT_NE(HeaderSource.find("SQmSettingsCardStyle QmSettingsCardStyle(float UiScale) const;"), std::string::npos);
	EXPECT_NE(HeaderSource.find("CScrollRegionParams QmSettingsScrollRegionParams(float UiScale) const;"), std::string::npos);
	EXPECT_EQ(HeaderSource.find("RenderQmSettingsGlassCard"), std::string::npos);

	const std::string MenuSource = ReadTextFile("src/game/client/components/menus.cpp");
	EXPECT_NE(MenuSource.find("CMenus::SQmSettingsCardStyle CMenus::QmSettingsCardStyle(float UiScale) const"), std::string::npos);
	EXPECT_NE(MenuSource.find("const SQmScrollContainerStyle ScrollStyle = QmScrollContainerStyleForSize(EQmScrollSize::MEDIUM, 1.0f);"), std::string::npos);
	EXPECT_NE(MenuSource.find("Style.m_ScrollbarWidth = ScrollStyle.m_ScrollbarWidth;"), std::string::npos);
	EXPECT_NE(MenuSource.find("Request.m_Profile = EQmScrollProfile::SETTINGS_OUTER;"), std::string::npos);
	EXPECT_NE(MenuSource.find("return QmScrollRegionParamsFromPolicy(QmResolveScrollPolicy(Request, UiScale, 0.0f));"), std::string::npos);
	EXPECT_EQ(MenuSource.find("QmScrollRegionParamsForSize(EQmScrollSize::LARGE, UiScale)"), std::string::npos);
	EXPECT_EQ(MenuSource.find("Params.m_ScrollUnit = 60.0f * UiScale;"), std::string::npos);
	EXPECT_EQ(MenuSource.find("Params.m_ScrollbarThickness = Style.m_ScrollbarWidth;"), std::string::npos);
	EXPECT_EQ(MenuSource.find("Params.m_ScrollbarMargin = Style.m_ScrollbarMargin;"), std::string::npos);
	EXPECT_EQ(MenuSource.find("RenderQmSettingsGlassCard"), std::string::npos);

	const std::string QmSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string VisualDeck = FunctionBody(QmSource, "void CMenus::RenderSettingsQmClientVisualDeck(CUIRect MainView, bool PrewarmOnly)");
	const std::string FunctionDeck = FunctionBody(QmSource, "void CMenus::RenderSettingsQmClientFunctionDeck(CUIRect MainView, bool PrewarmOnly)");
	const std::string HudDeck = FunctionBody(QmSource, "void CMenus::RenderSettingsQmClientHudDeck(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(VisualDeck.empty());
	ASSERT_FALSE(FunctionDeck.empty());
	ASSERT_FALSE(HudDeck.empty());
	// 卡片的高度测量、预布局输入与构造都搬到了卡片目录：薄壳只声明「这一页有哪些卡片」。
	const std::string HudCatalog = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogHud.cpp");
	const std::string VisualCatalog = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogVisual.cpp");
	const std::string CardCatalog = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalog.cpp");
	ASSERT_FALSE(HudCatalog.empty());
	ASSERT_FALSE(VisualCatalog.empty());
	ASSERT_FALSE(CardCatalog.empty());
	const std::string HudMeasure = FunctionBody(HudCatalog, "float MeasureHudCardHeight(");
	const std::string HudPreLayout = FunctionBody(HudCatalog, "FSettingsCardPreLayoutInput BuildHudPreLayoutInput(");
	const std::string HudCard = FunctionBody(HudCatalog, "bool BuildHudCard(");
	const std::string VisualPreLayout = FunctionBody(VisualCatalog, "FSettingsCardPreLayoutInput BuildVisualPreLayoutInput(");
	const std::string VisualCard = FunctionBody(VisualCatalog, "bool BuildVisualCard(");
	ASSERT_FALSE(HudMeasure.empty());
	ASSERT_FALSE(HudPreLayout.empty());
	ASSERT_FALSE(HudCard.empty());
	ASSERT_FALSE(VisualPreLayout.empty());
	ASSERT_FALSE(VisualCard.empty());
	EXPECT_NE(HudMeasure.find("const bool DummyMiniViewExpanded = g_Config.m_QmDummyMiniView != 0;"), std::string::npos);
	EXPECT_NE(HudMeasure.find("ResolveQmHudDummyMiniViewHeight(Metrics, DummyMiniViewExpanded)"), std::string::npos);
	EXPECT_NE(HudMeasure.find("case EQmModuleId::Coords: return ResolveQmHudCoordsHeight(Metrics);"), std::string::npos);
	EXPECT_NE(HudMeasure.find("ResolveQmHudNotificationsHeight(Metrics, g_Config.m_QmHudNotificationsShowAdvanced != 0, g_Config.m_QmHudNotificationsUseCategoryFilters != 0)"), std::string::npos);
	EXPECT_EQ(HudMeasure.find("case EQmModuleId::Coords: return Rows(8.0f) + LineHeight;"), std::string::npos);
	EXPECT_EQ(HudMeasure.find("case EQmModuleId::HudNotifications: return g_Config.m_QmHudNotificationsShowAdvanced ? Rows(15.0f) + LineHeight * 3.0f : Rows(4.0f);"), std::string::npos);
	EXPECT_NE(HudMeasure.find("const bool DynamicIslandOriginalStyle = g_Config.m_QmHudIslandUseOriginalStyle != 0;"), std::string::npos);
	EXPECT_NE(HudMeasure.find("ResolveQmHudDynamicIslandHeight(Metrics, DynamicIslandOriginalStyle, g_Config.m_QmSwitchCountdown != 0, ContentWidth)"), std::string::npos);
	EXPECT_NE(HudCard.find("QmCardRenderHook::RenderQmHudDummyMiniViewContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, g_Config.m_QmDummyMiniView != 0, ReadOnly);"), std::string::npos);
	EXPECT_NE(HudCard.find("QmCardRenderHook::RenderQmHudDynamicIslandContent(pMenus, Content, LineHeight, LineSpacing, g_Config.m_QmHudIslandUseOriginalStyle != 0);"), std::string::npos);
	EXPECT_NE(HudPreLayout.find("HandleQmHudCheckboxInput(pMenus, Content"), std::string::npos);
	EXPECT_NE(HudMeasure.find("case EQmModuleId::DebugGraph:"), std::string::npos);
	EXPECT_NE(HudMeasure.find("case EQmModuleId::SystemMediaControls:"), std::string::npos);
	// 调试模式卡只渲染一个复选框行，测量必须同为一行；按 5 行预留会在卡片底部留出约 4 行空白。
	EXPECT_NE(HudMeasure.find("case EQmModuleId::DebugMode: return Rows(1.0f);"), std::string::npos);
	EXPECT_EQ(HudMeasure.find("case EQmModuleId::DebugMode: return Rows(5.0f);"), std::string::npos);
	EXPECT_NE(VisualCard.find("BuildVisualPreLayoutInput(Ctx, ModuleId),"), std::string::npos);
	EXPECT_NE(CardCatalog.find("Out.m_PreLayoutInput = std::move(PreLayoutInput);"), std::string::npos);
	EXPECT_NE(VisualCard.find("case EQmModuleId::ChatBubble:"), std::string::npos);
	EXPECT_NE(VisualCard.find("case EQmModuleId::CameraView:"), std::string::npos);
	EXPECT_NE(VisualCard.find("case EQmModuleId::SkinTransition:"), std::string::npos);
	EXPECT_NE(VisualCard.find("case EQmModuleId::WeaponAnimation:"), std::string::npos);
	EXPECT_NE(VisualCard.find("case EQmModuleId::CollisionHitbox:"), std::string::npos);
	EXPECT_NE(HudPreLayout.find("ConsumeQmHudRow(Content, Metrics); // push radius"), std::string::npos);
	EXPECT_NE(HudPreLayout.find("ResolveSettingsRadioRowLayout(Content, 2, Metrics)"), std::string::npos);
	EXPECT_NE(QmSource.find("const int NoiseSuppressModeForLayout = std::clamp(g_Config.m_QmVoiceNoiseSuppressEnable, 0, 2);"), std::string::npos);
	EXPECT_NE(QmSource.find("g_Config.m_QmLyricsInMediaIsland"), std::string::npos);
	// 分类页不再构造 Lyrics 卡（原来的 AddCard(EQmModuleId::Lyrics, ...) 分支），它只在卡片目录的 HUD 模块里。
	EXPECT_EQ(HudDeck.find("case EQmModuleId::Lyrics:"), std::string::npos);
	EXPECT_EQ(QmSource.find("AddCard(EQmModuleId::Lyrics"), std::string::npos);
	EXPECT_NE(HudCard.find("case EQmModuleId::Lyrics:"), std::string::npos);
	EXPECT_NE(QmSource.find("if(!PrewarmOnly && !Ui()->RenderOnly())"), std::string::npos);

	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string NamePlateBranch = BlockBodyAfter(SettingsSource, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_NAME_PLATE)");
	ASSERT_FALSE(NamePlateBranch.empty());
	EXPECT_NE(NamePlateBranch.find("AddMeasuredCard(5,"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("AddMeasuredCard(6, ResolveNamePlatePreviewCardHeight"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("const auto NamePlateStrongEnabled = [] { return g_Config.m_ClNamePlatesStrong != 0; };"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("ResolveSettingsRadioRowLayout"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("if(NamePlateStrongEnabled())"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("vCards.back().m_MeasureRevision ="), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("m_PreLayoutInput = [this, LineSize, MarginSmall, AppearanceMetrics, NamePlateSectionHeaderHeight, NamePlateColorPickerHeight, NamePlateStrongEnabled]"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("LeftView.HSplitTop(NamePlateContentPaddingY"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("RightView.HSplitTop(NamePlateContentPaddingY"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("NamePlateSettingsShadow.Draw"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("NamePlatePreviewShadow.Draw"), std::string::npos);

	const std::string LaserBranch = BlockBodyAfter(SettingsSource, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_LASER)");
	ASSERT_FALSE(LaserBranch.empty());
	EXPECT_NE(LaserBranch.find("AddCard(10, ResolveLaserEnhancedMinCardHeight()"), std::string::npos);
	EXPECT_NE(LaserBranch.find("AddCard(11, LaserColorMinCardHeight"), std::string::npos);
	EXPECT_NE(LaserBranch.find("AddCard(12, LaserPreviewMinCardHeight"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("auto RenderQmSettingsGlassCard ="), std::string::npos);
	EXPECT_EQ(LaserBranch.find("80.0f * UiScale"), std::string::npos);

	for(const std::string *pDeck : {&VisualDeck, &FunctionDeck, &HudDeck})
	{
		EXPECT_NE(pDeck->find("ResolveSettingsContentMetrics(MainView.w)"), std::string::npos);
		EXPECT_NE(pDeck->find("CardDeck.RenderCached("), std::string::npos);
		EXPECT_NE(pDeck->find("ResolveSettingsCardDefinitionsRevision("), std::string::npos);
		EXPECT_NE(pDeck->find("static std::array<CButtonContainer, QmModuleCount> s_aCollapseButtons;"), std::string::npos);
		EXPECT_EQ(pDeck->find("Ui()->DoButtonLogic(&Collapsed[Index]"), std::string::npos);
		EXPECT_EQ(pDeck->find("BeginSettingsQmScrollContainer("), std::string::npos);
		EXPECT_EQ(pDeck->find("s_GlassCards"), std::string::npos);
	}
	// 折叠按钮的命中判定与绘制随卡片装配一起搬进了卡片目录（MakeModuleCard），页面只提供容器与回调。
	EXPECT_NE(CardCatalog.find("QmCardRenderHook::DoButtonLogic(Ctx.m_pMenus, &pCollapseButtons[Index], IsCollapsed, &Frame.m_HandleRect, BUTTONFLAG_LEFT)"), std::string::npos);
	EXPECT_NE(CardCatalog.find("RenderSettingsCardCollapseButton(CardCtx, Frame.m_HandleRect, Collapsed)"), std::string::npos);
	for(const std::string *pDeck : {&VisualDeck, &FunctionDeck, &HudDeck})
	{
		EXPECT_NE(pDeck->find("m_pCollapseButtons = s_aCollapseButtons.data();"), std::string::npos);
		EXPECT_NE(pDeck->find("m_pToggleCollapsed = ToggleQmSidebarCardCollapsed;"), std::string::npos);
	}
}

TEST(QmNewUiMenuBranches, SettingsCardUsesOneCanonicalSurfaceWithoutLegacyGlass)
{
	const std::string MenuSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Body = ReadTextFile("src/game/client/QmUi/SettingsCard.cpp");
	ASSERT_FALSE(Body.empty());
	EXPECT_EQ(Body.find("Shadow.Draw"), std::string::npos);
	EXPECT_NE(Body.find("DrawRoundedSurface(Ctx, ChromeRect, Surface, Border, CardRadius"), std::string::npos);
	EXPECT_EQ(Body.find("ChromeRect.Draw(Surface, IGraphics::CORNER_ALL, CardRadius)"), std::string::npos);
	EXPECT_EQ(Body.find("ChromeRect.Draw(Border, IGraphics::CORNER_ALL, CardRadius)"), std::string::npos);
	EXPECT_EQ(Body.find("ResolveSettingsCardBorderRingClipRects"), std::string::npos);
	EXPECT_EQ(Body.find("InnerSurface.Margin(BorderWidth, &InnerSurface);"), std::string::npos);
	EXPECT_EQ(Body.find("BorderRect.Draw(Border, IGraphics::CORNER_ALL, CardRadius)"), std::string::npos);
	EXPECT_EQ(Body.find("DrawOutline(Border)"), std::string::npos);
	EXPECT_EQ(MenuSource.find("RenderQmSettingsGlassCard"), std::string::npos);
	EXPECT_EQ(MenuSource.find("m_QmCardBackdropBlur"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsNumericFieldRectanglesInitializeBeforeLayoutBranching)
{
	const std::string Source = ReadTextFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Body = FunctionBody(Source, "bool NumericField(const IUiContext &Ctx");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("CUIRect Label{}, Controls{}, ValueRect{}, ScrollBar{}, InputField{};"), std::string::npos);
	EXPECT_NE(Body.find("Controls.VSplitRight(ValueWidth, &ScrollBar, &InputField);"), std::string::npos);
	EXPECT_NE(Body.find("FieldOptions.m_TextAlign = TEXTALIGN_MC;"), std::string::npos);
	EXPECT_NE(Body.find("FieldOptions.m_pTrailingText = HasSuffix ? Options.m_pSuffix : nullptr;"), std::string::npos);
	EXPECT_NE(Body.find("FieldOptions.m_InlineTrailingText = HasSuffix;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NumericInputKeepsValueAndUnitInOneGeometry)
{
	const std::string Forms = ReadTextFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Header = ReadTextFile("src/game/client/QmUi/UiForms.h");
	EXPECT_NE(Header.find("struct SInlineTrailingTextLayout"), std::string::npos);
	EXPECT_NE(Header.find("ResolveInlineTrailingTextLayout("), std::string::npos);
	EXPECT_NE(Forms.find("const SInlineTrailingTextLayout InlineLayout = ResolveInlineTrailingTextLayout"), std::string::npos);
	EXPECT_NE(Forms.find("FieldOptions.m_TextAlign = TEXTALIGN_MC;"), std::string::npos);
	EXPECT_NE(Forms.find("FieldOptions.m_InlineTrailingText = HasSuffix;"), std::string::npos);
	EXPECT_EQ(Forms.find("m_pInactiveDisplayText"), std::string::npos);
}

TEST(QmNewUiMenuBranches, EditBoxesActivateFromTheirConfiguredHitRect)
{
	const std::string Source = ReadTextFile("src/game/client/ui.cpp");
	const std::string Body = FunctionBody(Source, "bool CUi::DoEditBox(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, int Corners, const std::vector<STextColorSplit> &vColorSplits, int Align, const SEditBoxRenderOptions &RenderOptions)");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("else if(Inside)"), std::string::npos);
	EXPECT_EQ(Body.find("else if(HotItem() == pLineInput)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, InputTrailingActionsDoNotStealTextEditingHitArea)
{
	const std::string Source = ReadTextFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Header = ReadTextFile("src/game/client/QmUi/UiForms.h");
	const std::string Body = FunctionBody(Source, "SInputFieldResult InputField(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, const SInputFieldOptions &Options)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Header.find("m_pTrailingActionId"), std::string::npos);
	EXPECT_NE(Header.find("m_pTrailingActionIcon"), std::string::npos);
	EXPECT_NE(Header.find("m_TrailingActionQmIcon"), std::string::npos);
	EXPECT_NE(Body.find("const bool HasTrailingAction"), std::string::npos);
	EXPECT_NE(Body.find("CUIRect InputHitRect = Layout.m_ShellRect;"), std::string::npos);
	EXPECT_NE(Body.find("InputHitRect.VSplitRight(Layout.m_ClearRect.w, &InputHitRect, nullptr);"), std::string::npos);
	EXPECT_NE(Body.find("InputHitRect.VSplitRight(TrailingRect.w, &InputHitRect, nullptr);"), std::string::npos);
	EXPECT_NE(Body.find("RenderOptions.m_pHitRect = &InputHitRect;"), std::string::npos);
	EXPECT_NE(Body.find("DoButtonLogic(Options.m_pTrailingActionId"), std::string::npos);
	EXPECT_NE(Body.find("Search ? static_cast<int>(EQmIcon::SEARCH) : -1"), std::string::npos);
	EXPECT_NE(Body.find("static_cast<int>(EQmIcon::CLOSE)"), std::string::npos);
	EXPECT_NE(Source.find("Ctx.m_pIconManager->RenderIcon"), std::string::npos);
	EXPECT_NE(Source.find("const float EyeOffScale = QmIconWeightUsesBoldFontFallback(g_Config.m_QmUiIconWeight) ? 1.25f : 1.15f;"), std::string::npos);
	EXPECT_NE(Source.find("const float IconScale = QmIcon == static_cast<int>(EQmIcon::EYE_OFF) ? EyeOffScale : 1.0f;"), std::string::npos);
	EXPECT_NE(Source.find("const float IconSide = minimum(Rect.w, Rect.h) * 0.58f * IconScale;"), std::string::npos);
	EXPECT_NE(Source.find("Ctx.m_pUi->DoLabel(&Rect, pIcon, Rect.h * 0.65f * IconScale, TEXTALIGN_MC);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, GraphicsFsaaSelectionDefersBackendReconfigure)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("static constexpr int s_aFsaaSamples[] = {0, 2, 4, 8, 16, 32, 64};"), std::string::npos);
	EXPECT_NE(Body.find("g_Config.m_GfxFsaaSamples = s_aFsaaSamples[FsaaSampleIndex];"), std::string::npos);
	EXPECT_NE(Body.find("CheckSettings = true;"), std::string::npos);
	EXPECT_EQ(Body.find("Graphics()->SetMultiSampling"), std::string::npos);
	EXPECT_NE(Body.find("m_NeedRestartGraphics = !(s_GfxFsaaSamples == g_Config.m_GfxFsaaSamples"), std::string::npos);
}

TEST(QmNewUiMenuBranches, AnimationControlsExposeIndependentScopes)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("Localize(\"UI motion level\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Card list entry animation\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Card height animation\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Card reflow animation\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Presentation animations\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Animate chat box, emote selector, scoreboard, and spectate selection\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Off: disables all interface animations while preserving the options below\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Reduced: uses shorter transitions and disables presentation animations\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Full: each enabled animation category uses its complete transition\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Text input focus ring color\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, MultilineInputFieldsReleaseFocusOutsideAndCenterSingleLineContent)
{
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string QmClientSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string MultiLineBody = FunctionBody(UiSource, "bool CUi::DoEditBoxMultiLine(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, float LineSpacing, int TextAlign, const SEditBoxRenderOptions &RenderOptions)");
	ASSERT_FALSE(MultiLineBody.empty());

	EXPECT_NE(MultiLineBody.find("const bool ClickedOutside = (MouseButtonClicked(0) || MouseButtonClicked(1)) && !Inside;"), std::string::npos);
	EXPECT_NE(MultiLineBody.find("if(Active && ClickedOutside)"), std::string::npos);
	EXPECT_NE(MultiLineBody.find("ReleaseActiveTextInput(pLineInput);"), std::string::npos);
	EXPECT_NE(QmClientSource.find("InputOptions.m_TextAlign = TEXTALIGN_ML;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientScaledInputsUseSettingsThemeAndPreciseFreezeLabels)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string SliderBody = FunctionBody(Source, "bool CMenus::DoSliderWithScaledValue(");
	ASSERT_FALSE(SliderBody.empty());

	EXPECT_NE(SliderBody.find("IUiContext InputCtx = SettingsUiContext(\"tclient_slider_input\""), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Base prediction margin\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Maximum reduction\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Delay before reduction\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Frozen prediction margin\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SharedListEntryRevealUsesElapsedGapWithoutMovingScrollGeometry)
{
	const std::string ListBoxSource = ReadTextFile("src/game/client/ui_listbox.cpp");
	const std::string ListBoxHeader = ReadTextFile("src/game/client/ui_listbox.h");

	EXPECT_EQ(ListBoxSource.find("PerfFrame()"), std::string::npos);
	EXPECT_EQ(ListBoxSource.find("QmListBoxShouldRearmInitialScroll"), std::string::npos);
	EXPECT_EQ(ListBoxHeader.find("m_LastRenderFrame"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("m_InitialScrollPending = true;"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("m_EntryAnimationStartTime"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("QmListBoxEntryAnimatedRect"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("QmListBoxShouldStartEntryAnimation"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("QmListBoxEntryOffset"), std::string::npos);
	const std::string DoNextRowBody = FunctionBody(ListBoxSource, "CListboxItem CListBox::DoNextRow()");
	const size_t AddRectPos = DoNextRowBody.find("m_ScrollRegion.AddRect(m_RowView);");
	const size_t AnimateRectPos = DoNextRowBody.find("QmListBoxEntryAnimatedRect");
	ASSERT_NE(AddRectPos, std::string::npos);
	ASSERT_NE(AnimateRectPos, std::string::npos);
	EXPECT_LT(AddRectPos, AnimateRectPos);
	const std::string DoCustomRowBody = FunctionBody(ListBoxSource, "CListboxItem CListBox::DoCustomRow(float Height, bool ScrollHere)");
	const size_t CustomAddRectPos = DoCustomRowBody.find("m_ScrollRegion.AddRect(Item.m_Rect, ScrollHere);");
	const size_t CustomAnimateRectPos = DoCustomRowBody.find("QmListBoxEntryAnimatedRect");
	ASSERT_NE(CustomAddRectPos, std::string::npos);
	ASSERT_NE(CustomAnimateRectPos, std::string::npos);
	EXPECT_LT(CustomAddRectPos, CustomAnimateRectPos);
	EXPECT_NE(ListBoxSource.find("if(!RenderOnly && EntryAnimationEnabled"), std::string::npos);
}

TEST(QmNewUiMenuBranches, StatusBarOpacityUsesPercentAndAxiomUsesInputTrailingActions)
{
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string QmClientSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	EXPECT_NE(TClientSource.find("\"tclient-statusbar-alpha\""), std::string::npos);
	EXPECT_NE(TClientSource.find("\"tclient-statusbar-text-alpha\""), std::string::npos);
	EXPECT_NE(TClientSource.find("&CUi::ms_LinearScrollbarScale, 0, \"%\""), std::string::npos);
	EXPECT_NE(QmClientSource.find("Options.m_pTrailingActionId = &ToggleButton;"), std::string::npos);
	EXPECT_NE(QmClientSource.find("Options.m_pTrailingActionIcon = Visible ? FONT_ICON_EYE_SLASH : FONT_ICON_EYE;"), std::string::npos);
	EXPECT_NE(QmClientSource.find("Options.m_TrailingActionQmIcon = static_cast<int>(Visible ? EQmIcon::EYE_OFF : EQmIcon::EYE);"), std::string::npos);
	EXPECT_EQ(QmClientSource.find("PasswordToggleRect"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NoThemeBackgroundAndFriendRowsUseSharedSdfSurfaces)
{
	const std::string Menus = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Browser = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string Background = FunctionBody(Menus, "void CMenus::RenderBackground()");
	ASSERT_FALSE(Background.empty());

	EXPECT_NE(Background.find("const bool NoMenuTheme = g_Config.m_ClMenuMap[0] == '\\0';"), std::string::npos);
	EXPECT_NE(Background.find("const ColorRGBA CheckerColor = NoMenuTheme"), std::string::npos);
	EXPECT_NE(Background.find("Graphics()->SetColor(CheckerColor);"), std::string::npos);
	EXPECT_NE(Browser.find("#include <game/client/QmUi/UiSurface.h>"), std::string::npos);
	EXPECT_NE(Browser.find("DrawRoundedSurface(Ui(), Header, HeaderColor, ColorRGBA(), 5.0f);"), std::string::npos);
	EXPECT_NE(Browser.find("DrawRoundedSurface(Ui(), Rect, Color, ColorRGBA(), 5.0f);"), std::string::npos);
	EXPECT_EQ(Browser.find("Rect.Draw(Color, IGraphics::CORNER_ALL, 5.0f);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, KeyReaderUsesOneOuterShellForValueAndDeleteAction)
{
	const std::string Source = ReadTextFile("src/game/client/components/key_binder.cpp");
	const std::string Body = FunctionBody(Source, "CKeyBinder::CKeyReaderResult CKeyBinder::DoKeyReader(");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiSurface.h>"), std::string::npos);
	EXPECT_NE(Body.find("DrawRoundedSurface(Ui(), *pRect, ReaderBaseColor"), std::string::npos);
	EXPECT_NE(Body.find("if(ClearChecked == 0)"), std::string::npos);
	EXPECT_NE(Body.find("const float ClearSurfaceAlpha = 0.22f * Ui()->ButtonColorMul(pClearButton);"), std::string::npos);
	EXPECT_NE(Body.find("DrawRoundedSurface(Ui(), ClearButton"), std::string::npos);
	EXPECT_NE(Body.find("ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f)"), std::string::npos);
	EXPECT_NE(Body.find("if(m_pKeyReaderId == pReaderButton && m_TakeKey)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SharedListsAndResourceCardsUseRoundedSurfacePath)
{
	const std::string ListBox = ReadTextFile("src/game/client/ui_listbox.cpp");
	const std::string Assets = ReadTextFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Settings = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Sound = FunctionBody(Settings, "void CMenus::RenderSettingsSound(CUIRect MainView)");
	const std::string Language = FunctionBody(Settings, "bool CMenus::RenderLanguageSelection(CUIRect MainView, const SSettingsContentMetrics *pMetrics)");
	const std::string Controls = ReadTextFile("src/game/client/components/menus_settings_controls.cpp");
	EXPECT_NE(ListBox.find("DrawRoundedSurface(Ui(), Item.m_Rect"), std::string::npos);
	EXPECT_NE(ListBox.find("DrawRoundedSurface(Ui(), View"), std::string::npos);
	EXPECT_NE(Assets.find("DrawRoundedSurface(Ui(), ShellRect"), std::string::npos);
	EXPECT_NE(Assets.find("DrawRoundedSurface(Ui(), PreviewFrame"), std::string::npos);
	EXPECT_NE(Assets.find("DrawRoundedSurface(Ui(), FallbackRect"), std::string::npos);
	EXPECT_NE(Assets.find("DrawRoundedSurface(pUi, StatusRect"), std::string::npos);
	EXPECT_NE(Assets.find("DrawRoundedSurface(Ui(), WorkshopHudView"), std::string::npos);
	EXPECT_NE(Sound.find("DrawRoundedSurface(Ui(), ListRow"), std::string::npos);
	EXPECT_NE(Sound.find("DrawRoundedSurface(Ui(), BadgeRect"), std::string::npos);
	EXPECT_NE(Language.find("DrawRoundedSurface(Ui(), ItemRect"), std::string::npos);
	EXPECT_NE(Controls.find("DrawRoundedSurface(Ui(), KeyReaders"), std::string::npos);
	EXPECT_NE(Controls.find("DrawRoundedSurface(Ui(), Row"), std::string::npos);
}

TEST(QmNewUiMenuBranches, AppearanceTabsUseQmCards)
{
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettingsAppearance = FunctionBody(SettingsSource, "void CMenus::RenderSettingsAppearance(CUIRect MainView)");
	ASSERT_FALSE(RenderSettingsAppearance.empty());
	EXPECT_NE(RenderSettingsAppearance.find("const auto BuildDefinitions ="), std::string::npos);
	EXPECT_NE(RenderSettingsAppearance.find("const auto AddCard ="), std::string::npos);
	EXPECT_NE(RenderSettingsAppearance.find("const char *pAppearanceDeckTab ="), std::string::npos);
	EXPECT_NE(RenderSettingsAppearance.find("\"appearance-hud\""), std::string::npos);
	EXPECT_NE(SettingsSource.find("SettingsCardDeckForRenderPass().RenderCached(AppearanceCardCtx, AppearancePage, pAppearanceDeckTab"), std::string::npos);
	EXPECT_NE(SettingsSource.find("QmResolveScrollPolicy(ScrollRequest, AppearanceUiScale"), std::string::npos);

	const std::string HudBranch = BlockBodyAfter(RenderSettingsAppearance, "if(m_AppearanceSettingsTab == APPEARANCE_TAB_HUD)");
	ASSERT_FALSE(HudBranch.empty());
	EXPECT_NE(HudBranch.find("AddCard(0, HudLeftMinCardHeight"), std::string::npos);
	EXPECT_NE(SettingsSource.find("AddCard(1, ResolveHudRightMinCardHeight()"), std::string::npos);
	EXPECT_NE(HudBranch.find("const int HudRightCheckboxRowCount = 8 + (g_Config.m_ClShowhudDDRace ? 2 : 0);"), std::string::npos);
	EXPECT_NE(HudBranch.find("ResolveSettingsRowsHeight(HudRightCheckboxRowCount, LineSize, MarginSmall)"), std::string::npos);
	EXPECT_EQ(HudBranch.find("LineSize * 16.0f"), std::string::npos);
	EXPECT_EQ(HudBranch.find("RightView.HSplitTop(LineSize * 2.0f, nullptr, &RightView);"), std::string::npos);
	EXPECT_NE(RenderSettingsAppearance.find("\"deck:appearance-hud-main\""), std::string::npos);
	EXPECT_NE(RenderSettingsAppearance.find("\"deck:appearance-hud-ddrace\""), std::string::npos);
	EXPECT_EQ(HudBranch.find("UpdateMeasuredCardHeight"), std::string::npos);

	const std::string ChatBranch = BlockBodyAfter(RenderSettingsAppearance, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_CHAT)");
	ASSERT_FALSE(ChatBranch.empty());
	EXPECT_NE(ChatBranch.find("SSettingsCardDefinition ChatSettingsDefinition"), std::string::npos);
	EXPECT_NE(ChatBranch.find("ChatSettingsDefinition.m_Measure = [ResolveChatSettingsMinCardHeight]"), std::string::npos);
	EXPECT_NE(ChatBranch.find("ChatSettingsDefinition.m_PreLayoutInput"), std::string::npos);
	EXPECT_NE(ChatBranch.find("AddCard(3, ChatMessagesMinCardHeight"), std::string::npos);
	EXPECT_NE(ChatBranch.find("const auto MeasureChatPreview"), std::string::npos);
	EXPECT_NE(ChatBranch.find("AddMeasuredCard(4, MeasureChatPreview"), std::string::npos);
	EXPECT_NE(ChatBranch.find("minimum(ConfiguredLineWidth, CardLineWidth)"), std::string::npos);
	EXPECT_NE(SettingsSource.find("if(g_Config.m_QmChatLogAutoSave)"), std::string::npos);
	EXPECT_NE(ChatBranch.find("ResolveSettingsRowsHeight(ChatSettingsRowCount, LineSize, MarginSmall)"), std::string::npos);
	EXPECT_NE(ChatBranch.find("const auto NextChatRow"), std::string::npos);
	EXPECT_NE(ChatBranch.find("LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);"), std::string::npos);
	EXPECT_EQ(ChatBranch.find("vCards.back().m_Measure = [ResolveChatSettingsMinCardHeight]"), std::string::npos);
	EXPECT_EQ(ChatBranch.find("vCards.back().m_PreLayoutInput"), std::string::npos);
	EXPECT_NE(ChatBranch.find("ResolveAppearanceChatMessagesHeight(AppearanceMetrics)"), std::string::npos);
	EXPECT_NE(ChatBranch.find("DoMessageGradientLine(*pChat"), std::string::npos);
	EXPECT_NE(ChatBranch.find("appearance-chat-hide-system-prefix"), std::string::npos);
	EXPECT_NE(ChatBranch.find("ChatPreviewMeasureRevision"), std::string::npos);
	EXPECT_NE(ChatBranch.find("SystemMessageNamePrefix(g_Config.m_QmChatHideSystemPrefix != 0)"), std::string::npos);
	EXPECT_NE(SettingsSource.find("DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, Tab, Tab, pCheckBoxValue, pLabelTextId, pLabel, *pCheckBoxValue, &Label, LabelProps, true, BodySize)"), std::string::npos);
	EXPECT_EQ(SettingsSource.find("Label.Margin(2.0f, &Label);"), std::string::npos);
	EXPECT_EQ(SettingsSource.find("Section.VSplitRight(55.0f, &Section, &TextLabel);"), std::string::npos);
	EXPECT_NE(SettingsSource.find("const float ResolvedButtonHeight = ButtonHeight > 0.0f ? ButtonHeight : LineHeight;"), std::string::npos);
	EXPECT_NE(SettingsSource.find("const float ChangeButtonSize = ResolvedButtonHeight;"), std::string::npos);
	EXPECT_NE(SettingsSource.find("Metrics.m_ButtonHeight = ResolvedButtonHeight;"), std::string::npos);
	EXPECT_EQ(SettingsSource.find("ResetButton.HMargin(2.0f, &ResetButton);"), std::string::npos);
	EXPECT_EQ(ChatBranch.find("ContentView.HSplitBottom(220.0f"), std::string::npos);
	EXPECT_EQ(ChatBranch.find("PreviewView.w *= 0.5f;"), std::string::npos);
	EXPECT_NE(RenderSettingsAppearance.find("\"deck:appearance-chat-settings\""), std::string::npos);
	EXPECT_NE(RenderSettingsAppearance.find("\"deck:appearance-chat-messages\""), std::string::npos);
	EXPECT_NE(RenderSettingsAppearance.find("\"deck:appearance-chat-preview\""), std::string::npos);

	const std::string NamePlateBranch = BlockBodyAfter(RenderSettingsAppearance, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_NAME_PLATE)");
	ASSERT_FALSE(NamePlateBranch.empty());
	EXPECT_NE(NamePlateBranch.find("ResolveSettingsRowsHeight(10, LineSize, MarginSmall)"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("const auto NextNamePlateRow"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("const auto DoNamePlateCheckBox"), std::string::npos);
	EXPECT_NE(SettingsSource.find("AddMeasuredCard(5, ResolveNamePlateContentHeight"), std::string::npos);
	EXPECT_NE(SettingsSource.find("AddMeasuredCard(6, ResolveNamePlatePreviewCardHeight"), std::string::npos);
	EXPECT_NE(SettingsSource.find("ResolveNamePlatePreviewAreaHeight() + MarginSmall + NamePlatePreviewControlsHeight"), std::string::npos);
	EXPECT_NE(SettingsSource.find("maximum(NamePlatePreviewMinAreaHeight, GameClient()->m_NamePlates.MeasurePreviewAreaHeight())"), std::string::npos);
	EXPECT_NE(SettingsSource.find("}, ResolveNamePlatePreviewMeasureRevision());"), std::string::npos);
	EXPECT_NE(SettingsSource.find("const auto NextPreviewControl"), std::string::npos);
	EXPECT_NE(SettingsSource.find("PreviewArea.Draw(ui_token::color::SURFACE_OVERLAY"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("RenderQmSettingsGlassCard(NamePlateSettingsCard, QmCardStyle);"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("RenderQmSettingsGlassCard(NamePlatePreviewCard, QmCardStyle);"), std::string::npos);

	const std::string HookCollisionBranch = BlockBodyAfter(RenderSettingsAppearance, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_HOOK_COLLISION)");
	ASSERT_FALSE(HookCollisionBranch.empty());
	EXPECT_NE(RenderSettingsAppearance.find("\"deck:appearance-hook-collision-main\""), std::string::npos);
	EXPECT_NE(RenderSettingsAppearance.find("\"deck:appearance-hook-collision-preview\""), std::string::npos);

	const std::string InfoMessagesBranch = BlockBodyAfter(RenderSettingsAppearance, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_INFO_MESSAGES)");
	ASSERT_FALSE(InfoMessagesBranch.empty());
	EXPECT_NE(RenderSettingsAppearance.find("\"deck:appearance-info-messages\""), std::string::npos);

	const std::string LaserBranch = BlockBodyAfter(RenderSettingsAppearance, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_LASER)");
	ASSERT_FALSE(LaserBranch.empty());
	EXPECT_NE(LaserBranch.find("const auto ResolveLaserEnhancedMinCardHeight"), std::string::npos);
	EXPECT_NE(LaserBranch.find("ResolveAppearanceLaserEnhancedHeight(AppearanceMetrics, g_Config.m_QmLaserEnhanced != 0)"), std::string::npos);
	EXPECT_NE(LaserBranch.find("if(g_Config.m_QmLaserEnhanced)"), std::string::npos);
	EXPECT_NE(LaserBranch.find("vCards.back().m_MeasureRevision = static_cast<uint64_t>(g_Config.m_QmLaserEnhanced != 0)"), std::string::npos);
	EXPECT_NE(RenderSettingsAppearance.find("\"deck:appearance-laser-enhanced\""), std::string::npos);
	EXPECT_NE(RenderSettingsAppearance.find("\"deck:appearance-laser-colors\""), std::string::npos);
	EXPECT_NE(RenderSettingsAppearance.find("\"deck:appearance-laser-preview\""), std::string::npos);
	EXPECT_EQ(LaserBranch.find("RenderQmSettingsGlassCard(EnhancedCard, QmCardStyle);"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("RenderQmSettingsGlassCard(ColorCard, QmCardStyle);"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("RenderQmSettingsGlassCard(PreviewCard, QmCardStyle);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsCardDeckResetsStateWhenDefinitionViewChanges)
{
	const std::string SettingsDeck = ReadTextFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	ASSERT_FALSE(SettingsDeck.empty());
	EXPECT_NE(SettingsDeck.find("void CSettingsCardDeck::ResetDefinitionViewState()"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("if(TabChanged || StableIdsChanged || ModelCountChanged || StateIndexChanged)"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_PreparedDefinitionStateIndexRevision != Model.StateIndexRevision()"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_vContentHeights.clear();"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_ProjectionCache = {};"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsCardDeckPreLayoutUsesTheLastVisibleAnimatedFrame)
{
	const std::string SettingsDeck = ReadTextFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string ButtonLogic = FunctionBody(UiSource, "int CUi::DoButtonLogic(");
	ASSERT_FALSE(SettingsDeck.empty());
	ASSERT_FALSE(ButtonLogic.empty());
	EXPECT_NE(SettingsDeck.find("const SSettingsCardFrame PreLayoutFrame = ResolveSettingsCardDrawFrame(Card.m_Frame, Runtime.m_LastDrawOffsetX, Runtime.m_LastDrawOffsetY);"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_PreLayoutHeaderInput(PreLayoutFrame, CollapsedBeforeHeader)"), std::string::npos);
	const size_t CardLoop = SettingsDeck.find("for(const SPreparedCard &Card : m_vPreparedCards)");
	const size_t ActiveHeaderContinuation = SettingsDeck.find("const bool HasActiveHeaderContinuation = SettingsCardDeckHasActiveItemContinuation", CardLoop);
	const size_t ActiveContentContinuation = SettingsDeck.find("const bool HasActiveContentContinuation = SettingsCardDeckHasActiveItemContinuation", ActiveHeaderContinuation);
	ASSERT_NE(CardLoop, std::string::npos);
	ASSERT_NE(ActiveHeaderContinuation, std::string::npos);
	ASSERT_NE(ActiveContentContinuation, std::string::npos);
	EXPECT_LT(CardLoop, ActiveHeaderContinuation);
	EXPECT_LT(ActiveHeaderContinuation, ActiveContentContinuation);
	EXPECT_NE(SettingsDeck.find("(ControllerVisible || HasActiveHeaderContinuation) && Card.m_pDefinition->m_PreLayoutHeaderInput"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("SettingsCardDeckShouldRunPreLayoutInput(HasPointerInput, HasPendingPreLayoutInput, HasActiveContentContinuation"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("SettingsCardDeckUsesDefaultCollapseControl(HasCustomCollapsedState, static_cast<bool>(Card.m_pDefinition->m_PreLayoutHeaderInput))"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("Card.m_pDefinition->m_HeaderAction"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_PreLayoutInput(PreLayoutFrame.m_ContentRect)"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("Runtime.m_LastDrawOffsetY = State.m_DrawOffsetY;"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("if(PreLayoutInput() && Inside && !IsPopupOpen())"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("m_pHotItem = pId;"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("m_pBecomingHotItem = pId;"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("PreLayoutCurrentFramePress && MouseButtonClicked(Button)"), std::string::npos);
	EXPECT_LT(ButtonLogic.find("m_pHotItem = pId;"), ButtonLogic.find("SetActiveItem(pId);"));
}

TEST(QmNewUiMenuBranches, TClientSettingsCardsUseSharedQmCardStyle)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string HeaderSource = ReadTextFile("src/game/client/components/menus.h");
	const std::string RenderSettingsTClientSettings = FunctionBody(Source, "void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	const std::string RenderSettingsTClientChatBinds = FunctionBody(Source, "void CMenus::RenderSettingsTClientChatBinds(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(RenderSettingsTClientSettings.empty());
	ASSERT_FALSE(RenderSettingsTClientChatBinds.empty());

	EXPECT_EQ(Source.find("RenderQmSettingsGlassCard(TClientCacheSectionBoxRect(BoxRect), QmSettingsCardStyle(1.0f));"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientSettings.find("m_SettingsCardDeck.RenderCached(SettingsUiContext(\"settings_tclient_main\""), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientChatBinds.find("BeginSettingsScrollRegion("), std::string::npos);
	EXPECT_EQ(Source.find("ScrollParams.m_ScrollUnit = 60.0f;"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientSettings.find("ScrollParams.m_ScrollbarMargin = 5.0f;"), std::string::npos);
	EXPECT_EQ(Source.find("BoxRect.Draw(Ui()->ScaleBackgroundAlpha(MenuPanelColor(0.92f))"), std::string::npos);
	EXPECT_EQ(HeaderSource.find("void DrawTClientCacheSectionBox(CUIRect BoxRect);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientConditionalCardsKeepMeasureRenderAndPreLayoutRowsInSync)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string NameplateLayout = BlockBodyAfter(Source, "auto LayoutVisualNameplateSection =");
	const std::string InputLayout = BlockBodyAfter(Source, "auto LayoutInputSection =");
	const std::string PlayerIndicatorLayout = BlockBodyAfter(Source, "auto LayoutPlayerIndicatorSection =");
	const std::string TeeStatusBarLayout = BlockBodyAfter(Source, "auto LayoutTeeStatusBarSection =");
	const size_t PreLayoutFactoryPos = Source.find("const auto BuildTClientConditionalRowsPreLayoutInput");
	ASSERT_NE(PreLayoutFactoryPos, std::string::npos);
	const std::string PreLayoutSource = Source.substr(PreLayoutFactoryPos);
	const std::string NameplatePreLayout = BlockBodyAfter(PreLayoutSource, "if(str_comp(pStableCardId, \"tclient:visual-nameplates\") == 0)");
	const std::string InputPreLayout = BlockBodyAfter(PreLayoutSource, "if(str_comp(pStableCardId, \"tclient:input\") == 0)");
	const std::string PlayerIndicatorPreLayout = BlockBodyAfter(PreLayoutSource, "if(str_comp(pStableCardId, \"tclient:player-indicator\") == 0)");
	const std::string TeeStatusBarPreLayout = BlockBodyAfter(PreLayoutSource, "if(str_comp(pStableCardId, \"tclient:tee-status-bar\") == 0)");
	for(const std::string *pBody : {&NameplateLayout, &InputLayout, &PlayerIndicatorLayout, &TeeStatusBarLayout, &NameplatePreLayout, &InputPreLayout, &PlayerIndicatorPreLayout, &TeeStatusBarPreLayout})
		ASSERT_FALSE(pBody->empty());

	// The same section lambda feeds the cached measure and both render paths.
	EXPECT_NE(Source.find("S.m_MeasureFn = [&LayoutVisualNameplateSection]"), std::string::npos);
	EXPECT_NE(Source.find("S.m_RenderCompactFn = [&LayoutVisualNameplateSection"), std::string::npos);
	EXPECT_NE(Source.find("S.m_RenderFullFn = [&LayoutVisualNameplateSection"), std::string::npos);
	EXPECT_NE(Source.find("S.m_MeasureFn = [&LayoutInputSection]"), std::string::npos);
	EXPECT_NE(Source.find("S.m_RenderCompactFn = [&LayoutInputSection"), std::string::npos);
	EXPECT_NE(Source.find("S.m_RenderFullFn = [&LayoutInputSection"), std::string::npos);
	EXPECT_NE(Source.find("S.m_MeasureFn = [&LayoutPlayerIndicatorSection]"), std::string::npos);
	EXPECT_NE(Source.find("S.m_RenderCompactFn = [&LayoutPlayerIndicatorSection"), std::string::npos);
	EXPECT_NE(Source.find("S.m_RenderFullFn = [&LayoutPlayerIndicatorSection"), std::string::npos);
	EXPECT_NE(Source.find("S.m_MeasureFn = [&LayoutTeeStatusBarSection]"), std::string::npos);
	EXPECT_NE(Source.find("S.m_RenderCompactFn = [&LayoutTeeStatusBarSection"), std::string::npos);
	EXPECT_NE(Source.find("S.m_RenderFullFn = [&LayoutTeeStatusBarSection"), std::string::npos);

	// Each conditional row is counted by the render/measure lambda and consumed
	// conditionally by the pre-layout input path.
	EXPECT_NE(NameplateLayout.find("const int NameplateRowCount = 7 + (g_Config.m_TcWhiteFeet ? 1 : 0);"), std::string::npos);
	EXPECT_NE(NameplateLayout.find("if(g_Config.m_TcWhiteFeet)"), std::string::npos);
	EXPECT_NE(NameplatePreLayout.find("return ProcessToggle(Rows.Next(), &g_Config.m_TcWhiteFeet);"), std::string::npos);

	EXPECT_NE(InputLayout.find("if(UiMode == 0)"), std::string::npos);
	EXPECT_EQ(InputLayout.find("else if(UiMode == 3)"), std::string::npos);
	EXPECT_NE(InputPreLayout.find("ActiveMode == 0 ? &g_Config.m_TcFastInputOthers"), std::string::npos);

	EXPECT_NE(PlayerIndicatorLayout.find("const int DistanceRowCount = 3 + (g_Config.m_TcIndicatorVariableDistance ? 3 : 1);"), std::string::npos);
	EXPECT_NE(PlayerIndicatorLayout.find("if(g_Config.m_TcIndicatorVariableDistance)"), std::string::npos);
	EXPECT_NE(PlayerIndicatorLayout.find("if(ShowWarListIndicatorOptions)"), std::string::npos);
	EXPECT_NE(PlayerIndicatorPreLayout.find("if(g_Config.m_TcIndicatorVariableDistance)"), std::string::npos);
	EXPECT_NE(PlayerIndicatorPreLayout.find("if(g_Config.m_TcWarListIndicator)"), std::string::npos);
	EXPECT_NE(PlayerIndicatorPreLayout.find("if(!g_Config.m_TcWarListIndicator || !g_Config.m_TcWarListIndicatorColors)"), std::string::npos);

	EXPECT_NE(TeeStatusBarLayout.find("if(g_Config.m_TcShowFrozenText)"), std::string::npos);
	EXPECT_NE(TeeStatusBarPreLayout.find("if(g_Config.m_TcShowFrozenText)"), std::string::npos);
	EXPECT_NE(TeeStatusBarPreLayout.find("g_Config.m_TcShowFrozenText = g_Config.m_TcShowFrozenText != 2 ? 2 : 1;"), std::string::npos);

	// Smaller tees and fast-input radios must use the exact same button IDs in
	// the formal render and the pre-layout click pass.
	EXPECT_NE(Source.find("static std::vector<CButtonContainer> s_vTinyTeeModeButtons = {{}, {}, {}};"), std::string::npos);
	EXPECT_NE(Source.find("DoSettingsLine_RadioMenu(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, Row, \"tclient-smaller-tees-label\", Localize(\"Smaller tees\"), s_vTinyTeeModeButtons"), std::string::npos);
	EXPECT_NE(Source.find("DoButtonLogic(&s_vTinyTeeModeButtons[Index]"), std::string::npos);
	EXPECT_EQ(Source.find("s_vTinyTeePreLayoutButtons"), std::string::npos);
	EXPECT_NE(Source.find("static CButtonContainer s_FastInputModeFast;"), std::string::npos);
	EXPECT_NE(Source.find("DoButton_Menu(&s_FastInputModeFast"), std::string::npos);
	EXPECT_NE(InputPreLayout.find("DoButtonLogic(&s_FastInputModeFast"), std::string::npos);
	EXPECT_NE(InputPreLayout.find("DoButtonLogic(&s_FastInputModeSaikoPlus"), std::string::npos);
	// Best input 已整体删除，两个布局分支都不允许再出现对应按钮。
	EXPECT_EQ(Source.find("s_FastInputModeBest"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DeckPreLayoutPressClearsStaleActiveInput)
{
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const size_t PreLayoutPress = UiSource.find("if(PreLayoutInput() && Inside && !IsPopupOpen())");
	ASSERT_NE(PreLayoutPress, std::string::npos);
	const size_t PreLayoutPressEnd = UiSource.find("int CUi::DoDraggableButtonLogic", PreLayoutPress);
	ASSERT_NE(PreLayoutPressEnd, std::string::npos);
	const std::string ButtonLogic = UiSource.substr(PreLayoutPress, PreLayoutPressEnd - PreLayoutPress);
	EXPECT_NE(ButtonLogic.find("CLineInput::GetActiveInput()"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("pActiveInput->Deactivate()"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("m_pLastActiveItem = nullptr"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("SetActiveItem(nullptr)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DDNetSettingsPageUsesSharedQmCards)
{
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettingsDDNet = FunctionBody(SettingsSource, "void CMenus::RenderSettingsDDNet(CUIRect MainView)");
	ASSERT_FALSE(RenderSettingsDDNet.empty());

	EXPECT_NE(RenderSettingsDDNet.find("const SSettingsPageLayoutFrame DDNetPage = SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const IUiContext DDNetCardCtx = SettingsUiContext(\"settings_ddnet\", UiScale);"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("static CScrollRegion s_DDNetSettingsCardScrollRegion;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("SettingsCardDeckForRenderPass().RenderCached(DDNetCardCtx, DDNetPage, \"ddnet\""), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(DemoSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const bool ReplaysLayout = g_Config.m_ClReplays != 0;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const bool RaceGhostLayout = g_Config.m_ClRaceGhost != 0;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const bool RaceSaveGhostLayout = g_Config.m_ClRaceSaveGhost != 0;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("ResolveDDNetDemoRows(ReplaysLayout, RaceGhostLayout, RaceSaveGhostLayout)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("vCards.back().m_Measure = [DDNetRowPitch]"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("ResolveDDNetDemoRows(g_Config.m_ClReplays != 0, g_Config.m_ClRaceGhost != 0, g_Config.m_ClRaceSaveGhost != 0)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("vCards.back().m_PreLayoutInput = ProcessDemoPreLayoutInput;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("DDNetLayoutRevision = DDNetLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClReplays != 0);"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("if(g_Config.m_ClRaceGhost)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("if(g_Config.m_ClRaceSaveGhost)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(GameplaySpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const bool TextEntitiesLayout = g_Config.m_ClTextEntities != 0;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const bool AntiPingLayout = g_Config.m_ClAntiPing != 0;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("ResolveDDNetGameplayRows(TextEntitiesLayout, AntiPingLayout)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("vCards.back().m_PreLayoutInput = ProcessGameplayPreLayoutInput;"), std::string::npos);
	const std::string GameplayPreLayout = BlockBodyAfter(RenderSettingsDDNet, "const auto ProcessGameplayPreLayoutInput");
	ASSERT_FALSE(GameplayPreLayout.empty());
	EXPECT_NE(GameplayPreLayout.find("SplitDDNetRow(Gameplay, &GameplayRow);"), std::string::npos);
	EXPECT_NE(GameplayPreLayout.find("GameplayRow.VSplitLeft(std::clamp(GameplayRow.w * 0.38f"), std::string::npos);
	EXPECT_NE(GameplayPreLayout.find("Ui()->DoButtonLogic(&g_Config.m_ClTextEntities"), std::string::npos);
	EXPECT_NE(GameplayPreLayout.find("g_Config.m_ClTextEntities ^= 1;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("((uint64_t)(g_Config.m_ClTextEntities != 0) << 0)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("if(g_Config.m_ClTextEntities)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("if(g_Config.m_ClAntiPing)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("CUIRect GameplayRow;"), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("Gameplay.VSplitMid(&Left, &Right, 20.0f);"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(BackgroundSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(MiscellaneousSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("SaveSettingsCardOrderModel();"), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("EndSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("s_PrevDDNetSettingsScrollY"), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("MainView.HSplitTop(130.0f, &Demo, &MainView);"), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("MainView.HSplitTop(GameplayHeight, &Gameplay, &MainView);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsCardDeckSharedComponentMigratesSoundBindWheelStatusBar)
{
	const std::string HeaderSource = ReadTextFile("src/game/client/components/menus.h");
	EXPECT_EQ(HeaderSource.find("struct SSettingsCardDeckLayout"), std::string::npos);
	EXPECT_EQ(HeaderSource.find("struct SSettingsCardDeckCard"), std::string::npos);
	EXPECT_EQ(HeaderSource.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(HeaderSource.find("BeginSettingsCardDeckCard("), std::string::npos);
	EXPECT_EQ(HeaderSource.find("RenderSettingsCardDragHandle("), std::string::npos);
	EXPECT_EQ(HeaderSource.find("RenderSettingsCardDeckDragOverlay("), std::string::npos);
	EXPECT_EQ(HeaderSource.find("m_SettingsCardDeckOrders"), std::string::npos);
	EXPECT_EQ(HeaderSource.find("m_SettingsCardDeckColumnPrefs"), std::string::npos);
	EXPECT_NE(HeaderSource.find("qm_card_order::CModel m_SettingsCardOrderModel;"), std::string::npos);
	EXPECT_NE(HeaderSource.find("CSettingsCardDeck m_SettingsCardDeck;"), std::string::npos);

	const std::string MenuSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string SettingsDeck = ReadTextFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	EXPECT_EQ(MenuSource.find("RenderQmSettingsGlassCard"), std::string::npos);
	EXPECT_EQ(MenuSource.find("SettingsCardDeckStableId"), std::string::npos);
	EXPECT_EQ(MenuSource.find("LoadSettingsCardDeckOrdersFromGlobalConfig"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("CommitSettingsCardDeckDrop(Model, pTab, pStableId"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("SettingsCard(Ctx, Card.m_Frame"), std::string::npos);

	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string FormatBackendDisplayName = FunctionBody(SettingsSource, "void FormatQmGraphicsBackendDisplayName(");
	ASSERT_FALSE(FormatBackendDisplayName.empty());
	EXPECT_NE(FormatBackendDisplayName.find("\"OpenGL %d.%d\""), std::string::npos);
	EXPECT_EQ(FormatBackendDisplayName.find("\"OpenGL_QmClient_%d_%d\""), std::string::npos);
	const std::string RenderSettingsSound = FunctionBody(SettingsSource, "void CMenus::RenderSettingsSound(CUIRect MainView)");
	ASSERT_FALSE(RenderSettingsSound.empty());
	EXPECT_NE(RenderSettingsSound.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("CQmScrollState"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("SettingsCardDeckForRenderPass().RenderCached(SoundCardCtx, SoundPage, \"sound\""), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("AddCard(ToggleSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("AddCard(VolumeSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("AddCard(AudioPackSpec"), std::string::npos);
	EXPECT_LT(RenderSettingsSound.find("AddCard(VolumeSpec"), RenderSettingsSound.find("AddCard(AudioPackSpec"));
	EXPECT_NE(RenderSettingsSound.find("DoSoundNumericField(\"sound-volume\""), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("DoSoundNumericField(\"sound-background-music-volume\""), std::string::npos);
	EXPECT_EQ(RenderSettingsSound.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsSound.find("DoSliderWithValueInput("), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("Ui()->DoButton_FontIcon(&s_AudioPackRefreshButton, FONT_ICON_ARROW_ROTATE_RIGHT"), std::string::npos);
	EXPECT_EQ(RenderSettingsSound.find("DoButton_Menu(&s_AudioPackRefreshButton, FONT_ICON_ARROW_ROTATE_RIGHT"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("str_format(aBadge, sizeof(aBadge), \"%d\", Entry.m_FileCount);"), std::string::npos);
	EXPECT_EQ(RenderSettingsSound.find("str_copy(aBadge, Localize(\"Built-in\"), sizeof(aBadge));"), std::string::npos);
	EXPECT_EQ(RenderSettingsSound.find("Localize(\"Selected pack\")"), std::string::npos);
	EXPECT_EQ(RenderSettingsSound.find("\"audio_packs_title\""), std::string::npos);
	EXPECT_EQ(RenderSettingsSound.find("EndSettingsCardDeck("), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("Definition.m_IsVisible = std::move(IsVisible);"), std::string::npos);
	const size_t ToggleCard = RenderSettingsSound.find("AddCard(ToggleSpec");
	const size_t VolumeCard = RenderSettingsSound.find("AddCard(VolumeSpec");
	const size_t AudioPackCard = RenderSettingsSound.find("AddCard(AudioPackSpec");
	ASSERT_NE(ToggleCard, std::string::npos);
	ASSERT_NE(VolumeCard, std::string::npos);
	ASSERT_NE(AudioPackCard, std::string::npos);
	EXPECT_NE(RenderSettingsSound.substr(ToggleCard, VolumeCard - ToggleCard).find("}, {}, true, ProcessSoundToggleInput, g_Config.m_SndEnable);"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.substr(VolumeCard, AudioPackCard - VolumeCard).find("[]() { return g_Config.m_SndEnable != 0; }"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.substr(AudioPackCard).find("[]() { return g_Config.m_SndEnable != 0; }"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("Definition.m_PreLayoutInput = std::move(PreLayoutInput);"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("ProcessSoundToggleInput"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("vCards.back().m_Measure = [LineHeight, LineSpacing]"), std::string::npos);
	EXPECT_NE(RenderSettingsSound.find("SLabelProperties{}, false"), std::string::npos);
	EXPECT_EQ(RenderSettingsSound.find("AudioPackView.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.05f)"), std::string::npos);

	const size_t RebuildActiveStateIndices = SettingsDeck.find("RebuildActiveStateIndices");
	const size_t InitialBuild = SettingsDeck.find("BuildPreparedCards(*pColumns);");
	const size_t PreLayoutInput = SettingsDeck.find("m_pDefinition->m_PreLayoutInput");
	const size_t PreLayoutClipCheck = SettingsDeck.find("pScrollRegion == nullptr || !pScrollRegion->RectClipped", InitialBuild);
	const size_t ActiveStateRebuild = SettingsDeck.find("RebuildActiveStateIndices();", PreLayoutInput);
	const size_t FinalBuild = SettingsDeck.find("BuildPreparedCards(*pColumns);", ActiveStateRebuild);
	const size_t ScrollRegistration = SettingsDeck.find("pScrollRegion->AddRect", FinalBuild);
	const size_t SettingsCardRender = SettingsDeck.find("SettingsCard(Ctx, Card.m_Frame", FinalBuild);
	const size_t VisibleDropCommit = SettingsDeck.find("CommitSettingsCardDeckDrop(Model, pTab, pStableId, m_Drag.m_TargetColumn, m_Drag.m_TargetOrder, &m_vActiveStateIndices)");
	const size_t ViewportHeightInvalidation = SettingsDeck.find("std::abs(m_LastViewportHeight - ScrollViewport.h) > 0.01f");
	const size_t HeightCacheReset = SettingsDeck.find("std::fill(m_vContentHeights.begin(), m_vContentHeights.end(), -1.0f);", ViewportHeightInvalidation);
	EXPECT_NE(RebuildActiveStateIndices, std::string::npos);
	EXPECT_NE(SettingsDeck.find("PreLayoutGeometryChanged"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_vContentHeights[Card.m_StateIndex] = -1.0f;"), std::string::npos);
	ASSERT_NE(InitialBuild, std::string::npos);
	ASSERT_NE(PreLayoutInput, std::string::npos);
	ASSERT_NE(PreLayoutClipCheck, std::string::npos);
	ASSERT_NE(ActiveStateRebuild, std::string::npos);
	ASSERT_NE(FinalBuild, std::string::npos);
	ASSERT_NE(ViewportHeightInvalidation, std::string::npos);
	ASSERT_NE(HeightCacheReset, std::string::npos);
	EXPECT_LT(ViewportHeightInvalidation, HeightCacheReset);
	EXPECT_EQ(SettingsDeck.find("std::array<std::vector<int>, 3> aColumns ="), std::string::npos);
	EXPECT_NE(SettingsDeck.find("const std::array<std::vector<int>, 3> *pColumns ="), std::string::npos);
	ASSERT_NE(ScrollRegistration, std::string::npos);
	ASSERT_NE(SettingsCardRender, std::string::npos);
	ASSERT_NE(VisibleDropCommit, std::string::npos);
	EXPECT_LT(InitialBuild, PreLayoutInput);
	EXPECT_LT(PreLayoutClipCheck, PreLayoutInput);
	EXPECT_LT(PreLayoutInput, ActiveStateRebuild);
	EXPECT_LT(ActiveStateRebuild, FinalBuild);
	EXPECT_LT(FinalBuild, ScrollRegistration);
	EXPECT_LT(FinalBuild, SettingsCardRender);
	EXPECT_EQ(SettingsDeck.find("PrioritizeVisibilityControllers"), std::string::npos);
	EXPECT_EQ(SettingsDeck.find("vRenderedStates"), std::string::npos);

	const std::string RenderSettingsTClientSettings = FunctionBody(TClientSource, "void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	const std::string RenderSettingsTClientBindWheel = FunctionBody(TClientSource, "void CMenus::RenderSettingsTClientBindWheel(CUIRect MainView, bool PrewarmOnly)");
	const std::string RenderSettingsTClientChatBinds = FunctionBody(TClientSource, "void CMenus::RenderSettingsTClientChatBinds(CUIRect MainView, bool PrewarmOnly)");
	const std::string RenderSettingsTClientStatusBar = FunctionBody(TClientSource, "void CMenus::RenderSettingsTClientStatusBar(CUIRect MainView, bool PrewarmOnly)");
	const std::string StatusBarHeader = ReadTextFile("src/game/client/components/tclient/statusbar.h");
	ASSERT_FALSE(RenderSettingsTClientSettings.empty());
	ASSERT_FALSE(RenderSettingsTClientBindWheel.empty());
	ASSERT_FALSE(RenderSettingsTClientChatBinds.empty());
	ASSERT_FALSE(RenderSettingsTClientStatusBar.empty());
	EXPECT_EQ(TClientSource.find("void CMenus::HandleSettingsCardDeckDrag("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientSettings.find("RenderSettingsCardDragHandle(CardBoxRect, &HandleRect, QmSettingsCardStyle(1.0f));"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientSettings.find("SettingsCardDeckItemFromSection(SectionMeta, ColumnId, (int)i, CardRect, HandleRect);"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientSettings.find("m_SettingsCardDeck.RenderCached(SettingsUiContext(\"settings_tclient_main\""), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("SettingsPageLayout(MainView, UiScale)"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("deck:tclient-bind-wheel-editor"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("deck:tclient-bind-wheel-preview"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_BindWheelPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("SettingsCardOrderModel()"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientBindWheel.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientBindWheel.find("BeginSettingsCardDeckCard("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientBindWheel.find("EndSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientBindWheel.find("MainView.VSplitLeft(MainView.w / 2.1f"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientBindWheel.find("BeginSettingsCardDeck(MainView, s_BindWheelSettingsScrollRegion, s_BindWheelSettingsScrollY, 1.0f, \"tclient-bind-wheel\", SETTINGS_TCLIENT, nullptr)"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("deck:tclient-status-bar-settings"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("deck:tclient-status-bar-items"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("deck:tclient-status-bar-preview"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_StatusBarPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("InputState.m_AllowHeaderDrag = !ReadOnly;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("const int Rows = ResolveSettingsStatusCodeRows(StatusBarCodeCount, ContentWidth);"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("const float StatusBarPreviewHeight = LineSize + MarginSmall * 2.0f;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("const auto MeasurePreview = [&MeasureItems, StatusBarCodeCount, StatusBarItemCount, StatusBarPreviewHeight]"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("s_TypeSelectedOld < StatusBarCodeCount"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("if(s_SelectedItem >= 0 && s_TypeSelectedOld >= 0)"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("RenderStatusBarCodes(Content);"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("Definition.m_MeasureRevision = StatusLayoutRevision;"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("PreviewContentHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("static char s_aCodeLanguage[sizeof(g_Config.m_ClLanguagefile)]"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("static char s_aDropDownLanguage[sizeof(g_Config.m_ClLanguagefile)]"), std::string::npos);
	EXPECT_NE(StatusBarHeader.find("\"g\", \"Snapshot Age\""), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("if(View.w > 360.0f)"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("CTClientSettingsRowAllocator Rows(View);"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("MarginSmall * 10.0f"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("View.HSplitTop(LineSize, &Label, &View);"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("if(!ReadOnly && DoSettingsButton_Menu"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("if(!ReadOnly && DoSettingsButton_CheckBox"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("if(!ReadOnly && DoButtonLineSize_Menu"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("BeginSettingsCardDeckCard("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("EndSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("MainView.HSplitBottom(100.0f, &MainView, &StatusBar);"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);\n\t\tLeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("tclient-statusbar-seconds\", Localize(\"Show seconds on clock\"), g_Config.m_TcStatusBarLocalTimeSeconds, &CheckBoxRect))\n\t\t\tg_Config.m_TcStatusBarLocalTimeSeconds ^= 1;\n\t\tLeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);\n\t\t{\n\t\t\tLeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("s_StatusBarSettingsCardHeight"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientStatusBar.find("s_StatusBarSettingsScrollY"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("const float EditorContentHeight = LineSize * 7.0f + SmallSize + MarginSmall * 4.0f;"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientBindWheel.find("320.0f - CardChromeHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("const auto RenderPreview = [this, ReadOnly](CUIRect RightView) {\n\t\tif(ReadOnly)\n\t\t\treturn;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientBindWheel.find("InputState.m_AllowHeaderDrag = !ReadOnly;"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientBindWheel.find("s_BindWheelEditorCardHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("deck:tclient-chat-binds-kaomoji"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("deck:tclient-chat-binds-warlist"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("deck:tclient-chat-binds-other"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_ChatBindsPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("InputState.m_AllowHeaderDrag = !ReadOnly;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("if(!ReadOnly && ui_widget::InputField"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("return CBindChat::BIND_DEFAULTS[Index].second.size() * (MarginSmall + LineSize);"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientChatBinds.find("Content.HSplitTop(HeadlineHeight, &Label, &Content);"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientChatBinds.find("BeginSettingsScrollRegion("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientChatBinds.find("FinishSettingsScrollRegion("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientChatBinds.find("TClientCacheSectionBoxRect("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientChatBinds.find("s_PrevChatBindsScrollY"), std::string::npos);
	const std::string CardRegistry = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	EXPECT_NE(CardRegistry.find("{\"deck:tclient-chat-binds-kaomoji\", \"tclient-chat-binds\", ECardColumn::Left, 0"), std::string::npos);
	EXPECT_NE(CardRegistry.find("{\"deck:tclient-chat-binds-warlist\", \"tclient-chat-binds\", ECardColumn::Right, 0"), std::string::npos);
	EXPECT_NE(CardRegistry.find("{\"deck:tclient-chat-binds-other\", \"tclient-chat-binds\", ECardColumn::Left, 1"), std::string::npos);

	const std::string RenderSettingsGraphics = FunctionBody(SettingsSource, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(RenderSettingsGraphics.empty());
	EXPECT_NE(RenderSettingsGraphics.find("const SSettingsPageLayoutFrame GraphicsPage = SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("qm_card_registry::FindByStableId(\"deck:graphics-display\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("qm_card_registry::FindByStableId(\"deck:graphics-visual\")"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("qm_card_registry::FindByStableId(\"deck:graphics-backend\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("qm_card_registry::FindByStableId(\"deck:graphics-modes\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("qm_card_registry::FindByStableId(\"deck:graphics-interaction\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("Localize(\"Graphics backend\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("&g_Config.m_QmUiListEntryAnimations, \"settings-card-list-entry-animations\", Localize(\"Card list entry animation\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("&g_Config.m_QmUiCardHeightAnimations, \"settings-card-height-animations\", Localize(\"Card height animation\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("&g_Config.m_QmUiCardReflowAnimations, \"settings-card-reflow-animations\", Localize(\"Card reflow animation\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("&g_Config.m_QmExtraAnimations, \"presentation-animations\", Localize(\"Presentation animations\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("&g_Config.m_QmUiCardRainbowTitles, \"rainbow-card-titles\", Localize(\"Rainbow card titles\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("&g_Config.m_QmUiCardBorders, \"show-settings-card-borders\", Localize(\"Show settings card borders\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("Localize(\"Settings card border color\"), &g_Config.m_QmUiCardBorderColor"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("DoLine_AlphaColorPicker(&s_UiCardColorResetId, ColorMetrics, &UiCardColorRow, Localize(\"Settings card background\"), &g_Config.m_QmUiCardColor, &g_Config.m_QmUiCardOpacity"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(QmUiCardColor"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmUiCardOpacity, qm_ui_card_opacity, 30"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("DoGraphicsNumericField(\"graphics-card-corner-segments\""), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("RenderQmVisualCardAppearanceContent"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("CSettingsContentRowFlow Rows(ContentRect, GraphicsMetrics);"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("int RowsRemaining = 6;"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("DoSettingsLabel(SETTINGS_GRAPHICS, -1, \"graphics-ui-motion-level-label\""), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const char *apMotionLabels[] = {Localize(\"Off\"), Localize(\"Reduced\"), Localize(\"Full\")}"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("AddCard(DisplaySpec, GraphicsDisplayMinCardHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("AddCard(VisualSpec, GraphicsVisualMinCardHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("AddCard(InteractionSpec, GraphicsInteractionMinCardHeight"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("AddCard(BackendSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("AddCard(ModesSpec, GraphicsModesMinCardHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("ResolveSettingsGraphicsModesGeometry("), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const int GraphicsDisplayRowCount = 5 + (Graphics()->GetNumScreens() > 1 ? 1 : 0) + GraphicsBackendRowCount;"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("GraphicsPage.m_ScrollViewport.h - GraphicsPage.m_CardGap"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const float GraphicsVisualContentHeight = ResolveSettingsContentFlowHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const float GraphicsInteractionContentHeight = ResolveSettingsContentFlowHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const uint64_t GraphicsDisplayMeasureRevision ="), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("GraphicsDisplayMeasureRevision"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const float GraphicsInteractionMinCardHeight = InteractionChromeHeight + GraphicsInteractionContentHeight;"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("GraphicsModesTargetContentHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const uint64_t GraphicsModesMeasureRevision = static_cast<uint64_t>(std::max(0, s_NumNodes));"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("ScreenDropDownProps.m_pPopupViewport = &GraphicsPage.m_ScrollViewport;"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("WindowModeDropDownProps.m_pPopupViewport = &GraphicsPage.m_ScrollViewport;"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("settings-graphics-modes-height"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("GraphicsModesHeightAnimationActive"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("MotionRow.VSplitLeft"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("static CUi::SDropDownState s_BackendDropDownState;"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("static CUi::SDropDownState s_GpuDropDownState;"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("static CUi::SDropDownState s_State;"), std::string::npos);
	const size_t ModesCard = RenderSettingsGraphics.find("AddCard(ModesSpec");
	const size_t DisplayCard = RenderSettingsGraphics.find("AddCard(DisplaySpec");
	const size_t DisplayEnd = RenderSettingsGraphics.find("AddCard(VisualSpec", DisplayCard);
	ASSERT_NE(ModesCard, std::string::npos);
	ASSERT_NE(DisplayCard, std::string::npos);
	ASSERT_NE(DisplayEnd, std::string::npos);
	EXPECT_LT(ModesCard, DisplayCard);
	const size_t WindowMode = RenderSettingsGraphics.find("s_WindowModeDropDownState", ModesCard);
	ASSERT_NE(WindowMode, std::string::npos);
	EXPECT_LT(WindowMode, DisplayCard);
	EXPECT_EQ(RenderSettingsGraphics.find("UpdateMeasuredCardHeight"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("s_GraphicsInteractionCardHeight"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("SaveSettingsCardOrderModel();"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("\"graphics-renderer-title\""), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("EndSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("MainView.VSplitLeft(OptionsBlockWidth"), std::string::npos);

	const std::string RenderSettingsAppearance = FunctionBody(SettingsSource, "void CMenus::RenderSettingsAppearance(CUIRect MainView)");
	ASSERT_FALSE(RenderSettingsAppearance.empty());
	EXPECT_NE(RenderSettingsAppearance.find("const char *const aAppearanceIds[]"), std::string::npos);
	EXPECT_NE(RenderSettingsAppearance.find("qm_card_registry::FindByStableId"), std::string::npos);
	EXPECT_NE(RenderSettingsAppearance.find("Definition.m_Spec = Spec;"), std::string::npos);
	EXPECT_EQ(RenderSettingsAppearance.find("UpdateMeasuredCardHeight"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsDisplayCycleUpdatesAfterTabInputBeforePageRender)
{
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string DeckSource = ReadTextFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	const std::string RenderSettings = FunctionBody(SettingsSource, "void CMenus::RenderSettings(CUIRect MainView)");
	ASSERT_FALSE(RenderSettings.empty());
	const size_t TabInput = RenderSettings.find("DoButton_MenuTab(&m_aSettingsTabButtons[i]");
	const size_t DisplayCycle = RenderSettings.find("m_SettingsCardDeck.BeginDisplayCycle(");
	const size_t PageRender = RenderSettings.find("RenderSettingsGeneral(ContentView)");
	ASSERT_NE(TabInput, std::string::npos);
	ASSERT_NE(DisplayCycle, std::string::npos);
	ASSERT_NE(PageRender, std::string::npos);
	EXPECT_LT(TabInput, DisplayCycle);
	EXPECT_LT(DisplayCycle, PageRender);
	EXPECT_NE(DeckSource.find("const bool TabChanged = m_LastRenderedTab != pTab;"), std::string::npos);
	EXPECT_NE(DeckSource.find("if(TabChanged || StableIdsChanged || ModelCountChanged || StateIndexChanged)"), std::string::npos);
	EXPECT_NE(DeckSource.find("m_SuppressHoverFeedbackOnce = true;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsTransitionsDoNotChangePageBrightness)
{
	const std::vector<const char *> vFiles = {
		"src/game/client/components/menus_settings.cpp",
		"src/game/client/components/menus_settings7.cpp",
		"src/game/client/components/tclient/menus_tclient.cpp",
		"src/game/client/components/qmclient/menus_qmclient.cpp",
	};
	for(const char *pFile : vFiles)
	{
		const std::string Source = ReadTextFile(pFile);
		EXPECT_EQ(Source.find("ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha)"), std::string::npos) << pFile;
		EXPECT_EQ(Source.find("ColorRGBA(0.0f, 0.0f, 0.0f, TabTransitionAlpha)"), std::string::npos) << pFile;
	}
}

TEST(QmNewUiMenuBranches, SettingsCardFeedbackFixesUseStableLayouts)
{
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettingsDDNet = FunctionBody(SettingsSource, "void CMenus::RenderSettingsDDNet(CUIRect MainView)");
	ASSERT_FALSE(RenderSettingsDDNet.empty());
	EXPECT_NE(RenderSettingsDDNet.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(DemoSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(GameplaySpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(BackgroundSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(MiscellaneousSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("SettingsCardDeckForRenderPass().RenderCached(DDNetCardCtx, DDNetPage, \"ddnet\""), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("BeginDDNetCard(MainView"), std::string::npos);

	const std::string NamePlateBranch = BlockBodyAfter(SettingsSource, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_NAME_PLATE)");
	ASSERT_FALSE(NamePlateBranch.empty());
	EXPECT_EQ(NamePlateBranch.find("BeginSettingsScrollRegion(s_NameplateTextCardScrollRegion"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("FinishSettingsScrollRegion(s_NameplateTextCardScrollRegion"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Localize(\"Nameplate text\")"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("NameplateTextExpandedMinHeight"), std::string::npos);

	const std::string QmSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	EXPECT_EQ(QmSource.find("RenderNameplateTextSettings(CardContent);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, LaserPreviewEntityBranchesReserveEndpointDecorationSpace)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string DoLaserPreview = FunctionBody(Source, "void CMenus::DoLaserPreview(const CUIRect *pRect, const ColorHSLA LaserOutlineColor, const ColorHSLA LaserInnerColor, const int LaserType)");

	EXPECT_NE(DoLaserPreview.find("const vec2 EntityLaserEnd = LaserType == LASERTYPE_DOOR ? Pos - vec2(34.0f, 0.0f) : Pos - vec2(42.0f, 0.0f);"), std::string::npos);
	EXPECT_NE(DoLaserPreview.find("RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, EMOTE_NORMAL, vec2(-1, 0), Pos - vec2(20.0f, 0.0f));"), std::string::npos);
	EXPECT_EQ(DoLaserPreview.find("RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, EMOTE_NORMAL, vec2(-1, 0), Pos);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, LaserEntityTypesUseDdnetEndpointRendering)
{
	const std::string Source = ReadTextFile("src/game/client/components/items.cpp");
	const std::string RenderLaser = FunctionBody(Source, "void CItems::RenderLaser(vec2 From, vec2 Pos, ColorRGBA OuterColor, ColorRGBA InnerColor, float TicksBody, float TicksHead, int Type, float GlowIntensity) const");
	ASSERT_FALSE(RenderLaser.empty());

	// Regression guard for DDNet entity beams: door/freeze/dragger are carried by
	// the laser render path, but they are not weapon lasers and must not receive
	// generic impact-splat endpoints. The endpoint side is part of the behavior:
	// door blocker at Pos only, dragger pulley at From, freeze hectagon at Pos.
	const size_t DoorBranchPos = RenderLaser.find("if(Type == LASERTYPE_DOOR)");
	const size_t DraggerBranchPos = RenderLaser.find("else if(Type == LASERTYPE_DRAGGER)");
	const size_t FreezeBranchPos = RenderLaser.find("else if(Type == LASERTYPE_FREEZE)");
	const size_t GenericHeadPos = RenderLaser.find("else\n\t{", FreezeBranchPos);
	ASSERT_NE(DoorBranchPos, std::string::npos);
	ASSERT_NE(DraggerBranchPos, std::string::npos);
	ASSERT_NE(FreezeBranchPos, std::string::npos);
	ASSERT_NE(GenericHeadPos, std::string::npos);
	EXPECT_LT(DoorBranchPos, GenericHeadPos);
	EXPECT_LT(DraggerBranchPos, GenericHeadPos);
	EXPECT_LT(FreezeBranchPos, GenericHeadPos);

	const std::string DoorBranch = RenderLaser.substr(DoorBranchPos, DraggerBranchPos - DoorBranchPos);
	const std::string DraggerBranch = RenderLaser.substr(DraggerBranchPos, FreezeBranchPos - DraggerBranchPos);
	const std::string FreezeBranch = RenderLaser.substr(FreezeBranchPos, GenericHeadPos - FreezeBranchPos);
	const std::string GenericHeadBranch = RenderLaser.substr(GenericHeadPos);
	EXPECT_NE(DoorBranch.find("m_DoorHeadOffset"), std::string::npos);
	EXPECT_NE(DoorBranch.find("Pos.x - 8.0f, Pos.y - 8.0f"), std::string::npos);
	EXPECT_EQ(DoorBranch.find("From.x"), std::string::npos);
	EXPECT_NE(DraggerBranch.find("GameClient()->m_ExtrasSkin.m_SpritePulley"), std::string::npos);
	EXPECT_NE(DraggerBranch.find("m_PulleyHeadOffset, From.x, From.y"), std::string::npos);
	EXPECT_EQ(DraggerBranch.find("m_PulleyHeadOffset, Pos.x, Pos.y"), std::string::npos);
	EXPECT_NE(FreezeBranch.find("GameClient()->m_ExtrasSkin.m_SpriteHectagon"), std::string::npos);
	EXPECT_NE(FreezeBranch.find("m_FreezeHeadOffset, Pos.x, Pos.y"), std::string::npos);
	EXPECT_EQ(FreezeBranch.find("m_FreezeHeadOffset, From.x, From.y"), std::string::npos);
	EXPECT_NE(GenericHeadBranch.find("m_aParticleSplatOffset[CurParticle]"), std::string::npos);
	EXPECT_EQ(DoorBranch.find("m_aParticleSplatOffset"), std::string::npos);
	EXPECT_EQ(DraggerBranch.find("m_aParticleSplatOffset"), std::string::npos);
	EXPECT_EQ(FreezeBranch.find("m_aParticleSplatOffset"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientSettingsTabsPreserveHiddenStateAndVisibleCorners)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string RenderSettingsTClient = FunctionBody(Source, "void CMenus::RenderSettingsTClient(CUIRect MainView, bool PrewarmOnly)");
	const std::string RenderSettingsTClientInfo = FunctionBody(Source, "void CMenus::RenderSettingsTClientInfo(CUIRect MainView, bool PrewarmOnly)");

	EXPECT_NE(RenderSettingsTClient.find("if(TabCount <= 0)"), std::string::npos);
	EXPECT_NE(RenderSettingsTClient.find("FirstVisibleTab"), std::string::npos);
	EXPECT_NE(RenderSettingsTClient.find("VisibleTabIndex"), std::string::npos);
	EXPECT_NE(RenderSettingsTClient.find("VisibleTabIndex == 0"), std::string::npos);
	EXPECT_NE(RenderSettingsTClient.find("VisibleTabIndex == TabCount - 1"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientInfo.find("s_aShowTabs[i] = IsFlagSet(g_Config.m_TcTClientSettingsTabs, i);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientInfoUsesPublicCardDeck)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Registry = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsTClientInfo(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(Body.find("SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(Body.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_InfoPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_EQ(Body.find("MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-info-links\", \"tclient-info\", ECardColumn::Left, 0"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-info-files\", \"tclient-info\", ECardColumn::Left, 1"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-info-developers\", \"tclient-info\", ECardColumn::Right, 0"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-info-tabs\", \"tclient-info\", ECardColumn::Right, 1"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientProfilesUsesPublicCardDeck)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Registry = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsTClientProfiles(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(Body.find("SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(Body.find("ProfilesListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);"), std::string::npos);
	EXPECT_NE(Body.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_ProfilesPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(Body.find("const float ProfileActionsHeight = s_AllowDelete ? ProfileMetrics.m_ButtonHeight * 5.0f"), std::string::npos);
	EXPECT_NE(Body.find("Rect.VSplitLeft(ProfileMetrics.m_LineSpacing, nullptr, &Rect);"), std::string::npos);
	EXPECT_NE(Body.find("DrawRoundedSurface(Ui(), Skin, ColorRGBA(1.0f, 1.0f, 1.0f, 0.035f)"), std::string::npos);
	EXPECT_NE(Body.find("Skin.VMargin(std::max(0.0f, (Skin.w - PreviewRowWidth) * 0.5f), &Skin);"), std::string::npos);
	EXPECT_NE(Body.find("ResolveSettingsInlineRowMinimumWidth(ProfileMetrics.m_LabelWidth"), std::string::npos);
	EXPECT_NE(Body.find("ProfileMetrics.m_ListRowHeight"), std::string::npos);
	EXPECT_NE(Body.find("static_cast<uint64_t>(s_AllowDelete != 0) << 1"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-profiles-actions\", \"tclient-profiles\", ECardColumn::Left, 0"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-profiles-options\", \"tclient-profiles\", ECardColumn::Right, 0"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-profiles-list\", \"tclient-profiles\", ECardColumn::Left, 1"), std::string::npos);
	EXPECT_NE(Body.find("CTClientSettingsRowAllocator IdentityRows(View);"), std::string::npos);
	EXPECT_NE(Body.find("Row = IdentityRows.Next();"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientConfigsUsesPublicCardDeck)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Registry = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsTClientConfigs(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(Body.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_ConfigsPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(Body.find("static CScrollRegion s_ConfigListScrollRegion;"), std::string::npos);
	EXPECT_NE(Body.find("ConfigListScrollRequest.m_Profile = EQmScrollProfile::SETTINGS_INNER;"), std::string::npos);
	EXPECT_EQ(Body.find("QmSettingsScrollRegionParams(1.0f)"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-configs-actions\", \"tclient-configs\", ECardColumn::Full, 0"), std::string::npos);
	EXPECT_EQ(Registry.find("deck:tclient-configs-filters"), std::string::npos);
	EXPECT_EQ(Registry.find("deck:tclient-configs-list"), std::string::npos);
	EXPECT_NE(Body.find("ResolveSettingsInlineRowMinimumWidth("), std::string::npos);
	EXPECT_EQ(Body.find("ContentWidth < 760.0f"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmClientDecksIsolateRenderOnlyState)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	for(const char *pSignature : {
		    "void CMenus::RenderSettingsQmClientHudDeck(CUIRect MainView, bool PrewarmOnly)",
		    "void CMenus::RenderSettingsQmClientFunctionDeck(CUIRect MainView, bool PrewarmOnly)",
		    "void CMenus::RenderSettingsQmClientVisualDeck(CUIRect MainView, bool PrewarmOnly)",
		    "void CMenus::RenderSettingsQmClientContributors(CUIRect MainView, bool PrewarmOnly)",
	    })
	{
		const std::string Body = FunctionBody(Source, pSignature);
		ASSERT_FALSE(Body.empty());
		EXPECT_NE(Body.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
		EXPECT_NE(Body.find("CSettingsCardDeck &CardDeck = ReadOnly ?"), std::string::npos);
		EXPECT_NE(Body.find("qm_card_order::CModel &CardOrderModel = ReadOnly ?"), std::string::npos);
		EXPECT_NE(Body.find("InputState.m_AllowHeaderDrag = !ReadOnly;"), std::string::npos);
		EXPECT_NE(Body.find("ReadOnly ? nullptr : &"), std::string::npos);
		EXPECT_NE(Body.find("if(!ReadOnly && DeckResult.m_OrderChanged)"), std::string::npos);
	}
}

TEST(QmNewUiMenuBranches, TClientWarListDefersDeletesAndValidatesSelections)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string RenderSettingsTClientWarList = FunctionBody(Source, "void CMenus::RenderSettingsTClientWarList(CUIRect MainView, bool PrewarmOnly)");

	EXPECT_NE(RenderSettingsTClientWarList.find("static CWarType *s_pSelectedType = nullptr;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientWarList.find("WarTypeExists"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientWarList.find("WarEntryExists"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientWarList.find("CWarEntry *pEntryToRemove = nullptr;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientWarList.find("RemoveWarEntry(pEntryToRemove);"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientWarList.find("s_pSelectedEntry = nullptr;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientWarList.find("NewSelectedEntry < (int)s_vFilteredEntries.size()"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientWarList.find("NewSelectedType < (int)GameClient()->m_WarList.m_WarTypes.size()"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientWarListUsesPublicCardDeck)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Registry = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsTClientWarList(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(Body.find("SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(Body.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_WarListPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(Body.find("str_startswith(m_SettingsCardFocusStableId.c_str(), \"deck:tclient-warlist\")"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RequestReveal(m_SettingsCardFocusStableId.c_str());"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(Body.find("if(!ReadOnly)\n\t\t\tui_widget::InputField"), std::string::npos);
	EXPECT_NE(Body.find("if(!ReadOnly && DeckResult.m_OrderChanged)"), std::string::npos);
	EXPECT_EQ(Body.find("MainView.VSplitMid(&LeftView, &RightView, Margin);"), std::string::npos);
	EXPECT_EQ(Body.find("LeftView.VSplitMid(&Column1, &Column2, Margin);"), std::string::npos);
	EXPECT_EQ(Body.find("RightView.VSplitMid(&Column3, &Column4, Margin);"), std::string::npos);

	EXPECT_NE(Registry.find("{\"deck:tclient-warlist\", \"tclient-warlist\", ECardColumn::Full, 0"), std::string::npos);
	EXPECT_NE(Body.find("const float FourColumnMinWidth = 4.0f * WarListColumnMinimum"), std::string::npos);
	EXPECT_NE(Body.find("const float TwoColumnMinWidth = 2.0f * WarListColumnMinimum"), std::string::npos);
	EXPECT_NE(Body.find("WarListMetrics.m_ListRowHeight"), std::string::npos);
	EXPECT_NE(Body.find("constexpr int WarListViewportRows = 8;"), std::string::npos);
	EXPECT_NE(Body.find("const float EntriesHeight = LineSize * 2.0f + MarginSmall + WarListViewportRows * ListRowHeight;"), std::string::npos);
	EXPECT_NE(Body.find("Column.HSplitTop(WarListViewportRows * ListRowHeight, &WarTypeList, &Column);"), std::string::npos);
	EXPECT_NE(Body.find("const float PlayersHeight = LineSize + MarginSmall + WarListViewportRows * ListRowHeight;"), std::string::npos);
	EXPECT_NE(Body.find("if(!ReadOnly)\n\t\t\t\tRenderTeeCute"), std::string::npos);
	EXPECT_NE(Body.find("RenderWarListLayout(ContentRect, true);"), std::string::npos);
	EXPECT_NE(Body.find("Localizable(\"War Entries\")"), std::string::npos);
	EXPECT_NE(Body.find("Localizable(\"War Groups\")"), std::string::npos);
	EXPECT_NE(Body.find("Localizable(\"Edit Entry\")"), std::string::npos);
	EXPECT_NE(Body.find("Localizable(\"Online Players\")"), std::string::npos);
	EXPECT_NE(Body.find("RenderSection(EntriesColumn, \"tclient-warlist-section-entries\", pWarEntriesTitle"), std::string::npos);
	EXPECT_NE(Body.find("RenderSection(EditorColumn, \"tclient-warlist-section-editor\", pEditEntryTitle"), std::string::npos);
	EXPECT_NE(Body.find("RenderSection(GroupsColumn, \"tclient-warlist-section-groups\", pWarGroupsTitle"), std::string::npos);
	EXPECT_NE(Body.find("RenderSection(PlayersColumn, \"tclient-warlist-section-players\", pOnlinePlayersTitle"), std::string::npos);
	EXPECT_NE(Body.find("RenderSection(EntriesColumn, \"tclient-warlist-section-settings\", pSettingsTitle"), std::string::npos);
	EXPECT_NE(Body.find("PlayerListBox.DoStart(ListRowHeight, s_vFilteredPlayerIds.size()"), std::string::npos);
	EXPECT_EQ(Body.find("PlayerListBox.DoStart(ListRowHeight, MAX_CLIENTS"), std::string::npos);
	EXPECT_NE(Body.find("maximum(EntriesSectionHeight + SectionGap + SettingsSectionHeight, EditorSectionHeight)"), std::string::npos);
	EXPECT_NE(Body.find("SecondRow.VSplitMid(&GroupsColumn, &PlayersColumn, SectionGap);"), std::string::npos);
	const size_t TwoColumnBranch = Body.find("else if(ContentRect.w >= TwoColumnMinWidth)");
	ASSERT_NE(TwoColumnBranch, std::string::npos);
	const size_t SingleColumnBranch = Body.find("\t\telse\n\t\t{", TwoColumnBranch);
	ASSERT_NE(SingleColumnBranch, std::string::npos);
	const size_t SingleEntries = Body.find("RenderSection(ContentRect, \"tclient-warlist-section-entries\"", SingleColumnBranch);
	const size_t SingleSettings = Body.find("RenderSection(ContentRect, \"tclient-warlist-section-settings\"", SingleColumnBranch);
	const size_t SingleEditor = Body.find("RenderSection(ContentRect, \"tclient-warlist-section-editor\"", SingleColumnBranch);
	const size_t SingleGroups = Body.find("RenderSection(ContentRect, \"tclient-warlist-section-groups\"", SingleColumnBranch);
	const size_t SinglePlayers = Body.find("RenderSection(ContentRect, \"tclient-warlist-section-players\"", SingleColumnBranch);
	ASSERT_NE(SingleEntries, std::string::npos);
	ASSERT_NE(SingleSettings, std::string::npos);
	ASSERT_NE(SingleEditor, std::string::npos);
	ASSERT_NE(SingleGroups, std::string::npos);
	ASSERT_NE(SinglePlayers, std::string::npos);
	EXPECT_LT(SingleEntries, SingleSettings);
	EXPECT_LT(SingleSettings, SingleEditor);
	EXPECT_LT(SingleEditor, SingleGroups);
	EXPECT_LT(SingleGroups, SinglePlayers);
	EXPECT_EQ(Body.find("deck:tclient-warlist-entries"), std::string::npos);

	const size_t EntriesPriority = Body.find("EntriesListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t EntriesStart = Body.find("EntriesListBox.DoStart(");
	const size_t GroupsPriority = Body.find("WarTypeListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t GroupsStart = Body.find("WarTypeListBox.DoStart(");
	const size_t PlayersPriority = Body.find("PlayerListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t PlayersStart = Body.find("PlayerListBox.DoStart(");
	ASSERT_NE(EntriesPriority, std::string::npos);
	ASSERT_NE(EntriesStart, std::string::npos);
	ASSERT_NE(GroupsPriority, std::string::npos);
	ASSERT_NE(GroupsStart, std::string::npos);
	ASSERT_NE(PlayersPriority, std::string::npos);
	ASSERT_NE(PlayersStart, std::string::npos);
	EXPECT_LT(EntriesPriority, EntriesStart);
	EXPECT_LT(GroupsPriority, GroupsStart);
	EXPECT_LT(PlayersPriority, PlayersStart);
}

TEST(QmNewUiMenuBranches, TClientProfilesAndStatusBarClampUiIndices)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string RenderSettingsTClientProfiles = FunctionBody(Source, "void CMenus::RenderSettingsTClientProfiles(CUIRect MainView, bool PrewarmOnly)");
	const std::string RenderSettingsTClientStatusBar = FunctionBody(Source, "void CMenus::RenderSettingsTClientStatusBar(CUIRect MainView, bool PrewarmOnly)");

	EXPECT_NE(RenderSettingsTClientProfiles.find("Profile.m_FeetColor >= 0"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientProfiles.find("ProfilesPerRow = maximum(1"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("StatusItemTypeCount"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("s_TypeSelectedOld < StatusItemTypeCount"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("s_SelectedItem < (int)GameClient()->m_StatusBar.m_StatusBarItems.size()"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsListSelectionsClampBeforeIndexing)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettingsPlayer = FunctionBody(Source, "void CMenus::RenderSettingsPlayer(CUIRect MainView)");
	const std::string RenderSettingsGraphics = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	const std::string PopupMapPicker = FunctionBody(Source, "CUi::EPopupMenuFunctionResult CMenus::PopupMapPicker(void *pContext, CUIRect View, bool Active)");

	EXPECT_NE(RenderSettingsPlayer.find("NewSelected >= 0 && NewSelected < (int)s_vpFilteredFlags.size()"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("NewSelected >= 0 && NewSelected < s_NumNodes"), std::string::npos);
	EXPECT_NE(PopupMapPicker.find("const int ItemIndex = MapIndex++;"), std::string::npos);
	EXPECT_NE(PopupMapPicker.find("ItemIndex == pPopupContext->m_Selection"), std::string::npos);
	EXPECT_NE(PopupMapPicker.find("NewSelected >= 0 && NewSelected < (int)pPopupContext->m_vMaps.size()"), std::string::npos);
	EXPECT_NE(PopupMapPicker.find("pPopupContext->m_Selection >= 0"), std::string::npos);
}

TEST(QmNewUiMenuBranches, BackgroundMapPickerUsesMapsRootAndSupportedFiles)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettingsDDNet = FunctionBody(Source, "void CMenus::RenderSettingsDDNet(CUIRect MainView)");
	const std::string MapListPopulate = FunctionBody(Source, "void CMenus::CPopupMapPickerContext::MapListPopulate()");
	const std::string MapListFetchCallback = FunctionBody(Source, "int CMenus::CPopupMapPickerContext::MapListFetchCallback");

	EXPECT_NE(RenderSettingsDDNet.find("str_copy(s_PopupMapPickerContext.m_aRootPath, \"maps\""), std::string::npos);
	EXPECT_NE(MapListPopulate.find("ListRoot(m_aRootPath[0] != '\\0' ? m_aRootPath : \"maps\", m_aValuePrefix);"), std::string::npos);
	EXPECT_EQ(MapListPopulate.find("m_aFallbackRootPath"), std::string::npos);
	EXPECT_EQ(MapListPopulate.find("m_aFallbackValuePrefix"), std::string::npos);
	EXPECT_NE(MapListFetchCallback.find("FindBackgroundFileExtension(pInfo->m_pName)"), std::string::npos);
	EXPECT_EQ(MapListFetchCallback.find("str_endswith(pInfo->m_pName, \".map\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, EditorSaveFileDialogKeepsFilenameInputInControl)
{
	const std::string Source = ReadTextFile("src/game/editor/file_browser.cpp");
	const std::string OnRender = FunctionBody(Source, "void CFileBrowser::OnRender(CUIRect _)");

	EXPECT_NE(OnRender.find("m_ListBox.SetActive(!Ui()->IsPopupOpen() && (!m_SaveAction || !m_FilenameInput.IsActive()))"), std::string::npos);
	EXPECT_NE(OnRender.find("const bool ListChoseItem = m_ListBox.WasItemSelected() || m_ListBox.WasItemActivated();"), std::string::npos);
	EXPECT_NE(OnRender.find("const bool SyncFilenameInput = !m_SaveAction || (ListChoseItem && m_SelectedFileIndex >= 0);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmClientLanguageReadmeDescribesChineseSourceKeys)
{
	EXPECT_FALSE(fs_is_dir(TestSourcePath("data/qmclient/languages").c_str()));
}

TEST(QmNewUiMenuBranches, KcpLogUsesBoundedFormatting)
{
	const std::string Source = ReadTextFile("src/engine/external/kcp/ikcp.c");

	EXPECT_EQ(Source.find("vsprintf(buffer, fmt, argptr);"), std::string::npos);
	EXPECT_NE(Source.find("vsnprintf(buffer, sizeof(buffer), fmt, argptr);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DisplayChangedDoesNotUseDisplayUnionData)
{
	const std::string Source = ReadTextFile("src/engine/client/input.cpp");
	const size_t CaseStart = Source.find("case SDL_WINDOWEVENT_DISPLAY_CHANGED:");
	ASSERT_NE(CaseStart, std::string::npos);
	const size_t Break = Source.find("break;", CaseStart);
	ASSERT_NE(Break, std::string::npos);
	const std::string Body = Source.substr(CaseStart, Break - CaseStart);

	EXPECT_EQ(Body.find("Event.display.data1"), std::string::npos);
	EXPECT_NE(Body.find("Event.window.data1"), std::string::npos);
	EXPECT_NE(Body.find("Graphics()->SwitchWindowScreen(DisplayIndex, false);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, GraphicsDriverCrashRecoveryUsesSafeStartupFallback)
{
	const std::string Source = ReadTextFile("src/engine/client/client.cpp");
	const std::string Detector = FunctionBody(Source, "static bool QmCrashTextHasGraphicsDriverFault");
	const std::string Recovery = FunctionBody(Source, "static bool ApplyQmSafeGraphicsRecovery");
	const std::string StartupHook = FunctionBody(Source, "static void RecoverQmGraphicsSettingsAfterDriverCrash");

	EXPECT_NE(Detector.find("Exception module: nvoglv64.dll"), std::string::npos);
	EXPECT_NE(Detector.find(" in module nvoglv64.dll"), std::string::npos);
	EXPECT_NE(Detector.find("Exception module: vulkan-1.dll"), std::string::npos);
	EXPECT_NE(Detector.find("Exception module: D3D12Core.dll"), std::string::npos);
	EXPECT_NE(Detector.find(" in module D3D12Core.dll"), std::string::npos);
	EXPECT_NE(Detector.find("Exception module: d3d12.dll"), std::string::npos);
	EXPECT_NE(Detector.find("Exception module: dxgi.dll"), std::string::npos);
	EXPECT_NE(Detector.find(" in module opengl32.dll"), std::string::npos);
	// 「Exception module:」只在异常地址落在模块内时才写。跳 NULL 的崩溃（本次 nvoglv64
	// 就是 call 0x0）拿不到那一行，栈帧归因写的是「模块名 + 偏移」，必须能匹配到；
	// 而且要限定在那一行内匹配，否则「Loaded modules」清单里的同名 DLL 会造成误判。
	EXPECT_NE(Detector.find("return QmCrashTextExceptionModuleIsGraphicsDriver(pText);"), std::string::npos);
	const std::string ExceptionModule = FunctionBody(Source, "static bool QmCrashTextExceptionModuleIsGraphicsDriver");
	EXPECT_NE(ExceptionModule.find("str_startswith(aLine, gs_pQmCrashReportModulePrefix)"), std::string::npos);
	EXPECT_NE(ExceptionModule.find("str_find(pModule, \" + 0x\")"), std::string::npos);
	EXPECT_NE(ExceptionModule.find("str_toint_base(pOffset, 16) == 0"), std::string::npos);
	EXPECT_NE(ExceptionModule.find("str_find(pModule, \"!\")"), std::string::npos);

	EXPECT_NE(StartupHook.find("gs_pQmLifecycleMarkerFile"), std::string::npos);
	EXPECT_NE(StartupHook.find("ListDirectoryInfo"), std::string::npos);
	EXPECT_NE(StartupHook.find("ReadFileStr"), std::string::npos);
	EXPECT_NE(StartupHook.find("ParseQmCrashReportGraphicsBackend(pCrashReport, aCrashedBackend, sizeof(aCrashedBackend))"), std::string::npos);

	EXPECT_EQ(Recovery.find("str_copy(g_Config.m_GfxBackend, \"OpenGL\");"), std::string::npos);
	EXPECT_NE(Recovery.find("const int FallbackGLMajor = 0;"), std::string::npos);
	EXPECT_NE(Recovery.find("const int FallbackGLMinor = 0;"), std::string::npos);
	EXPECT_EQ(Recovery.find("CONF_PLATFORM_MACOS"), std::string::npos);
	EXPECT_NE(Recovery.find("g_Config.m_GfxFsaaSamples = 0;"), std::string::npos);
	EXPECT_NE(Recovery.find("g_Config.m_GfxFullscreen = 0;"), std::string::npos);
	EXPECT_NE(Recovery.find("Changed |= SwitchQmGraphicsBackendAwayFrom(pCrashedBackend, pFailedBackends);"), std::string::npos);
	EXPECT_NE(StartupHook.find("switching backend and resetting safe graphics settings in windowed mode without FSAA"), std::string::npos);
	EXPECT_EQ(StartupHook.find("CONF_PLATFORM_MACOS"), std::string::npos);

	// 后端选择必须是对称的：崩在 OpenGL 家族上要往 Vulkan 走，崩在 Vulkan 上才回 OpenGL。
	// 旧逻辑无条件切 OpenGL，正好把用户按在 wglSwapBuffers 那个崩点上反复重启。
	const std::string BackendSwitch = FunctionBody(Source, "static bool SwitchQmGraphicsBackendAwayFrom");
	EXPECT_NE(BackendSwitch.find("str_copy(g_Config.m_GfxBackend, s_pFallback);"), std::string::npos);
	EXPECT_NE(BackendSwitch.find("s_pFallback = \"Vulkan\""), std::string::npos);
	EXPECT_NE(BackendSwitch.find("str_comp_nocase(pCrashedBackend, \"OpenGL\") == 0"), std::string::npos);
	EXPECT_NE(BackendSwitch.find("str_comp_nocase(pCrashedBackend, \"GLES\") == 0"), std::string::npos);
	EXPECT_NE(BackendSwitch.find("s_pOpenGLFallback = \"OpenGL\""), std::string::npos);

	// 两个后端都崩过之后必须停止来回换，否则 OpenGL/Vulkan 乒乓，用户永远进不去。
	EXPECT_NE(BackendSwitch.find("ParseQmGraphicsRecoveryFailedBackendCount(pFailedBackends, s_pFallback) >= 2"), std::string::npos);
	EXPECT_NE(BackendSwitch.find("ParseQmGraphicsRecoveryFailedBackendCount(pFailedBackends, s_pOpenGLFallback) >= 2"), std::string::npos);
	// 计数必须跨崩溃累加，否则每次重写状态文件都会把「崩过的后端」丢掉。
	const std::string MarkRecovered = FunctionBody(Source, "static bool MarkQmGraphicsCrashReportRecovered");
	EXPECT_NE(MarkRecovered.find("ParseQmGraphicsRecoveryFailedBackendCount(pPrevious, \"OpenGL\")"), std::string::npos);
	EXPECT_NE(MarkRecovered.find("ParseQmGraphicsRecoveryFailedBackendCount(pPrevious, \"Vulkan\")"), std::string::npos);
	EXPECT_NE(MarkRecovered.find("++OpenGLCrashes;"), std::string::npos);

	// 同一次会话里换过后端再次崩溃时，指纹必须变，否则第二次不会再自愈。
	const std::string Fingerprint = FunctionBody(Source, "static void FormatQmGraphicsCrashReportFingerprint");
	EXPECT_NE(Fingerprint.find("pCrashedBackend"), std::string::npos);

	const size_t HookCall = Source.find("RecoverQmGraphicsSettingsAfterDriverCrash(pStorage);");
	const size_t CommandLineParse = Source.find("pConsole->ParseArguments(argc - 1, &argv[1]);");
	ASSERT_NE(HookCall, std::string::npos);
	ASSERT_NE(CommandLineParse, std::string::npos);
	EXPECT_LT(HookCall, CommandLineParse);
}

TEST(QmNewUiMenuBranches, ImplausibleRefreshRatesAreNotPersisted)
{
	const std::string Backend = ReadTextFile("src/engine/client/backend_sdl.cpp");
	const std::string Graphics = ReadTextFile("src/engine/client/graphics_threaded.cpp");

	// IsPlausible* guards now live in the shared plausible_sizes.h header
	// (behavior-tested in PlausibleSizes.RefreshRateAndWindowGuardsMatchContract);
	// both backends include it instead of re-declaring file-static copies.
	EXPECT_NE(Backend.find("#include <engine/client/plausible_sizes.h>"), std::string::npos);
	EXPECT_NE(Backend.find("Ignoring implausible configured window size"), std::string::npos);
	EXPECT_NE(Backend.find("*pWidth = DisplayMode.w;"), std::string::npos);
	EXPECT_NE(Backend.find("*pHeight = DisplayMode.h;"), std::string::npos);
	EXPECT_NE(Backend.find("Ignoring implausible configured refresh rate"), std::string::npos);
	EXPECT_NE(Backend.find("*pRefreshRate = 0;"), std::string::npos);
	EXPECT_NE(Graphics.find("#include <engine/client/plausible_sizes.h>"), std::string::npos);
	EXPECT_NE(Graphics.find("Ignoring implausible refresh rate during resize"), std::string::npos);
	EXPECT_NE(Graphics.find("RefreshRate = m_ScreenRefreshRate;"), std::string::npos);
	EXPECT_NE(Graphics.find("static int LogicalWindowSizeFromViewport(int ViewportSize, float HiDPIScale)"), std::string::npos);
	EXPECT_NE(Graphics.find("Ignoring implausible resize dimensions"), std::string::npos);
	EXPECT_NE(Graphics.find("if(IsPlausibleWindowSize(g_Config.m_GfxScreenWidth, g_Config.m_GfxScreenHeight))"), std::string::npos);
	EXPECT_EQ(Graphics.find("w = g_Config.m_GfxScreenWidth > 0 ? g_Config.m_GfxScreenWidth : m_ScreenWidth;"), std::string::npos);
	EXPECT_EQ(Graphics.find("h = g_Config.m_GfxScreenHeight > 0 ? g_Config.m_GfxScreenHeight : m_ScreenHeight;"), std::string::npos);
	EXPECT_NE(Graphics.find("w = LogicalWindowSizeFromViewport(m_ScreenWidth, m_ScreenHiDPIScale);"), std::string::npos);
	EXPECT_NE(Graphics.find("h = LogicalWindowSizeFromViewport(m_ScreenHeight, m_ScreenHiDPIScale);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, UnavailableVulkanFallsBackToAutoDetectedOpenGL)
{
	const std::string Backend = ReadTextFile("src/engine/client/backend_sdl.cpp");
	const std::string Fallback = FunctionBody(Backend, "static void ResetOpenGLFallbackConfig");
	const std::string Init = FunctionBody(Backend, "int CGraphicsBackend_SDL_GL::Init");

	EXPECT_NE(Fallback.find("g_Config.m_GfxGLMajor = 0;"), std::string::npos);
	EXPECT_NE(Fallback.find("g_Config.m_GfxGLMinor = 0;"), std::string::npos);
	EXPECT_NE(Fallback.find("自动探测"), std::string::npos);
	EXPECT_NE(Init.find("bool ConfiguredVulkanUnavailable"), std::string::npos);
	EXPECT_NE(Init.find("if(ConfiguredVulkanUnavailable)"), std::string::npos);
	EXPECT_NE(Init.find("ResetOpenGLFallbackConfig();"), std::string::npos);
	EXPECT_NE(Init.find("m_BackendType = DetectBackend();"), std::string::npos);
	EXPECT_NE(Init.find("m_GpuList = {};"), std::string::npos);
	EXPECT_NE(Init.find("m_Capabilities.Reset();"), std::string::npos);

	const size_t UnavailableFallback = Init.find("if(ConfiguredVulkanUnavailable)");
	const size_t ClampVersion = Init.find("ClampDriverVersion(m_BackendType);");
	ASSERT_NE(UnavailableFallback, std::string::npos);
	ASSERT_NE(ClampVersion, std::string::npos);
	EXPECT_LT(UnavailableFallback, ClampVersion);
}

TEST(QmNewUiMenuBranches, OpenGLSelectionUsesRuntimeContextDetection)
{
	const std::string ConfigVariables = ReadTextFile("src/engine/shared/config_variables.h");
	const SOpenGLVersion AutoGL = AutoOpenGLProbeVersion(EBackendType::BACKEND_TYPE_OPENGL);
	const SOpenGLVersion AutoGLES = AutoOpenGLProbeVersion(EBackendType::BACKEND_TYPE_OPENGL_ES);
	EXPECT_EQ(AutoGL.m_Major, 4);
#if defined(CONF_PLATFORM_MACOS)
	EXPECT_EQ(AutoGL.m_Minor, 1);
#else
	EXPECT_EQ(AutoGL.m_Minor, 6);
#endif
	EXPECT_EQ(AutoGLES.m_Major, 3);
	EXPECT_EQ(AutoGLES.m_Minor, 0);
	EXPECT_TRUE(IsOpenGLVersionAtLeast({4, 6, 0}, {4, 5, 0}));
	EXPECT_FALSE(IsOpenGLVersionAtLeast({4, 5, 0}, {4, 6, 0}));
	EXPECT_TRUE(IsOpenGLVersionAtLeast({3, 3, 0}, {3, 3, 0}));
	EXPECT_FALSE(IsOpenGLVersionAtLeast({1, 2, 0}, {1, 2, 1}));
	SOpenGLVersion ProbeVersion{4, 6, 0};
	for(int Minor = 5; Minor >= 0; --Minor)
	{
		EXPECT_TRUE(NextAutoOpenGLProbeVersion(ProbeVersion));
		EXPECT_EQ(ProbeVersion.m_Major, 4);
		EXPECT_EQ(ProbeVersion.m_Minor, Minor);
	}
	EXPECT_TRUE(NextAutoOpenGLProbeVersion(ProbeVersion));
	EXPECT_EQ(ProbeVersion.m_Major, 3);
	EXPECT_EQ(ProbeVersion.m_Minor, 3);
	EXPECT_TRUE(NextAutoOpenGLProbeVersion(ProbeVersion));
	EXPECT_EQ(ProbeVersion.m_Minor, 2);
	EXPECT_NE(ConfigVariables.find("MACRO_CONFIG_INT(GfxGLMajor, gfx_gl_major, 0, 0, 10"), std::string::npos);
	EXPECT_NE(ConfigVariables.find("MACRO_CONFIG_INT(GfxGLMinor, gfx_gl_minor, 0, 0, 10"), std::string::npos);

	const SOpenGLVersion Actual41{4, 1, 0};
	EXPECT_TRUE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL, {3, 3, 0}, Actual41));
	EXPECT_TRUE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL, {4, 6, 0}, Actual41));
	EXPECT_FALSE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL, {3, 0, 0}, Actual41));
	EXPECT_FALSE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_VULKAN, {3, 3, 0}, Actual41));
	EXPECT_TRUE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL_ES, {3, 0, 0}, {3, 2, 0}));
	EXPECT_FALSE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL_ES, {1, 0, 0}, {3, 2, 0}));
}

TEST(QmNewUiMenuBranches, VulkanApiSelectionDefaultsTo11AndTreats14AsStrict)
{
	EXPECT_EQ(gs_BackendVulkanMinimumVersion.m_Major, 1);
	EXPECT_EQ(gs_BackendVulkanMinimumVersion.m_Minor, 1);
	EXPECT_EQ(gs_BackendVulkanMaximumVersion.m_Major, 1);
	EXPECT_EQ(gs_BackendVulkanMaximumVersion.m_Minor, 4);
	EXPECT_TRUE(IsVulkanVersionAtLeast({1, 4, 0}, {1, 1, 0}));
	EXPECT_FALSE(IsVulkanVersionAtLeast({1, 3, 999}, {1, 4, 0}));
	EXPECT_EQ(ClampVulkanVersionToSupportedRange({1, 0, 99}).m_Minor, 1);
	EXPECT_EQ(ClampVulkanVersionToSupportedRange({1, 1, 0}).m_Minor, 1);
	EXPECT_EQ(ClampVulkanVersionToSupportedRange({1, 2, 37}).m_Minor, 2);
	EXPECT_EQ(ClampVulkanVersionToSupportedRange({1, 3, 0}).m_Minor, 3);
	EXPECT_EQ(ClampVulkanVersionToSupportedRange({1, 4, 99}).m_Minor, 4);
	EXPECT_EQ(ClampVulkanVersionToSupportedRange({2, 0, 0}).m_Minor, 4);
	EXPECT_EQ(MinVulkanVersion({1, 4, 0}, {1, 2, 37}).m_Minor, 2);
	EXPECT_EQ(MinVulkanVersion({1, 1, 99}, {1, 4, 0}).m_Minor, 1);
	EXPECT_EQ(ResolveConfiguredVulkanApiVersion(11).m_Minor, 1);
	EXPECT_EQ(ResolveConfiguredVulkanApiVersion(12).m_Minor, 1);
	EXPECT_EQ(ResolveConfiguredVulkanApiVersion(13).m_Minor, 1);
	EXPECT_EQ(ResolveConfiguredVulkanApiVersion(14).m_Minor, 4);

	const std::string BackendSource = ReadTextFile("src/engine/client/backend_sdl.cpp");
	const std::string GraphicsThreadedSource = ReadTextFile("src/engine/client/graphics_threaded.cpp");
	const std::string VulkanSource = ReadTextFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string ClampDriverVersion = FunctionBody(BackendSource, "void CGraphicsBackend_SDL_GL::ClampDriverVersion(");
	const std::string DriverVersions = FunctionBody(BackendSource, "bool CGraphicsBackend_SDL_GL::GetDriverVersion(");
	const std::string DetectedVersion = FunctionBody(BackendSource, "bool CGraphicsBackend_SDL_GL::GetDetectedContextVersion(");
	const std::string ResolveApiVersion = FunctionBody(VulkanSource, "bool ResolveRequestedVulkanApiVersion(");
	const std::string CreateInstance = FunctionBody(VulkanSource, "bool CreateVulkanInstance(");
	const std::string SelectGpu = FunctionBody(VulkanSource, "bool SelectGpu(");
	const std::string InitVulkanSdl = FunctionBody(VulkanSource, "int InitVulkanSDL(");

	EXPECT_NE(DriverVersions.find("Major = 0;"), std::string::npos);
	EXPECT_NE(DriverVersions.find("Minor = 0;"), std::string::npos);
	EXPECT_EQ(ClampDriverVersion.find("NormalizeRequestedVulkanVersion"), std::string::npos);
	EXPECT_EQ(ClampDriverVersion.find("g_Config.m_GfxGLMajor = Version.m_Major"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmVulkanApiVersion, qm_vulkan_api_version, 11, 11, 14"), std::string::npos);
	EXPECT_NE(ResolveApiVersion.find("ResolveConfiguredVulkanApiVersion(g_Config.m_QmVulkanApiVersion)"), std::string::npos);
	EXPECT_EQ(ResolveApiVersion.find("ClampVulkanVersionToSupportedRange(LoaderVersion)"), std::string::npos);
	EXPECT_NE(VulkanSource.find("SDL_Vulkan_GetVkGetInstanceProcAddr"), std::string::npos);
	EXPECT_NE(VulkanSource.find("vkEnumerateInstanceVersion"), std::string::npos);
	EXPECT_NE(VulkanSource.find("FirstCompatibleDeviceIndex"), std::string::npos);
	EXPECT_NE(VulkanSource.find("configured graphics card is unavailable"), std::string::npos);
	EXPECT_EQ(GraphicsThreadedSource.find("Trying Vulkan 1.1 instead"), std::string::npos);
	EXPECT_NE(GraphicsThreadedSource.find("Falling back to automatically detected OpenGL"), std::string::npos);
	EXPECT_NE(SettingsSource.find("s_GfxBackendChanged = true;"), std::string::npos);
	EXPECT_NE(SettingsSource.find("s_GfxGpuChanged = true;"), std::string::npos);
	EXPECT_NE(SettingsSource.find("s_GfxVulkanApiVersionChanged = true;"), std::string::npos);
	EXPECT_NE(SettingsSource.find("Localize(\"Vulkan API\")"), std::string::npos);
	EXPECT_NE(SettingsSource.find("g_Config.m_QmVulkanApiVersion"), std::string::npos);
	EXPECT_NE(CreateInstance.find("VKAppInfo.apiVersion = m_RequestedApiVersion;"), std::string::npos);
	EXPECT_NE(CreateInstance.find("VkInstance CreatedInstance = VK_NULL_HANDLE;"), std::string::npos);
	EXPECT_NE(CreateInstance.find("m_LastVulkanInstanceCreateResult = Res;"), std::string::npos);
	EXPECT_NE(CreateInstance.find("Res == VK_ERROR_INCOMPATIBLE_DRIVER"), std::string::npos);
	EXPECT_NE(CreateInstance.find("if(Res != VK_SUCCESS)"), std::string::npos);
	EXPECT_NE(CreateInstance.find("The Vulkan driver rejected the requested Vulkan 1.4 instance"), std::string::npos);
	EXPECT_NE(SelectGpu.find("const SVulkanVersion RequiredVersion"), std::string::npos);
	EXPECT_NE(SelectGpu.find("IsVulkanVersionAtLeast(DeviceVersion, RequiredVersion)"), std::string::npos);
	EXPECT_NE(SelectGpu.find("HasRequiredVersionDevice"), std::string::npos);
	EXPECT_NE(SelectGpu.find("m_RequiredVulkanVersionUnavailable = true;"), std::string::npos);
	EXPECT_NE(SelectGpu.find("m_RequiredVulkanVersionUnavailable = !HasRequiredVersionDevice;"), std::string::npos);
	EXPECT_NE(SelectGpu.find("m_EffectiveApiVersion"), std::string::npos);
	EXPECT_NE(InitVulkanSdl.find("g_Config.m_QmVulkanApiVersion = 11;"), std::string::npos);
	EXPECT_NE(InitVulkanSdl.find("falling back to Vulkan 1.1"), std::string::npos);
	EXPECT_EQ(InitVulkanSdl.find("m_LastVulkanInstanceCreateResult != VK_ERROR_INCOMPATIBLE_DRIVER"), std::string::npos);
	EXPECT_NE(InitVulkanSdl.find("The selected Vulkan 1.4 instance could not be created"), std::string::npos);
	EXPECT_NE(InitVulkanSdl.find("FallbackToVulkan11"), std::string::npos);
	EXPECT_NE(InitVulkanSdl.find("ResetInitializationDiagnostics();"), std::string::npos);
	EXPECT_NE(InitVulkanSdl.find("DestroyVulkanInstance();"), std::string::npos);
	EXPECT_NE(DetectedVersion.find("BACKEND_TYPE_VULKAN"), std::string::npos);
	EXPECT_NE(SettingsSource.find("\"Vulkan (%s)\""), std::string::npos);
}

TEST(QmNewUiMenuBranches, GraphicsCurrentModeLabelSanitizesScaleAndAspectRatio)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");

	EXPECT_NE(Source.find("const float HiDPIScale = std::isfinite(RawHiDPIScale) && RawHiDPIScale > 0.0f ? RawHiDPIScale : 1.0f;"), std::string::npos);
	EXPECT_NE(Source.find("const int AspectGcd = G > 0 ? G : 1;"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_GfxScreenWidth / AspectGcd"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_GfxScreenHeight / AspectGcd"), std::string::npos);
}

TEST(QmMovingTiles, RestoresIncomingScreenMappingAfterRendering)
{
	const std::string Body = FunctionBody(ReadTextFile("src/game/client/components/tclient/moving_tiles.cpp"), "void CMovingTiles::OnRender()");
	const size_t GroupStatePos = Body.find("auto ApplyGroupState =");
	ASSERT_NE(GroupStatePos, std::string::npos);
	const std::string BeforeGroupState = Body.substr(0, GroupStatePos);

	// 从入口保存操作提取四个坐标，要求绘制结束后按原顺序恢复，而不是重设默认相机。
	const std::regex SaveScreenPattern(R"(Graphics\(\)->GetScreen\(&([A-Za-z0-9_]+), &([A-Za-z0-9_]+), &([A-Za-z0-9_]+), &([A-Za-z0-9_]+)\);)");
	std::smatch SavedScreen;
	ASSERT_TRUE(std::regex_search(BeforeGroupState, SavedScreen, SaveScreenPattern));
	const std::string RestoreScreen = "Graphics()->MapScreen(" + SavedScreen[1].str() + ", " + SavedScreen[2].str() + ", " + SavedScreen[3].str() + ", " + SavedScreen[4].str() + ");";

	const size_t RenderPos = Body.rfind("RenderPass();");
	ASSERT_NE(RenderPos, std::string::npos);
	const size_t RestorePos = Body.find(RestoreScreen, RenderPos);
	ASSERT_NE(RestorePos, std::string::npos);
	EXPECT_EQ(Body.find("return", RenderPos), std::string::npos);
	EXPECT_EQ(Body.find("MapScreen", RestorePos + RestoreScreen.size()), std::string::npos);
}

TEST(QmCameraAspectRatio, KeepsUiAspectPhysicalAndOverridesOnlyGameWorld)
{
	const std::string GraphicsHeader = ReadTextFile("src/engine/graphics.h");
	const std::string GraphicsSource = ReadTextFile("src/engine/graphics.cpp");
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string GameClientSource = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string ControlsSource = ReadTextFile("src/game/client/components/controls.cpp");
	const std::string HudSource = ReadTextFile("src/game/client/components/hud.cpp");
	const std::string TouchControlsSource = ReadTextFile("src/game/client/components/touch_controls.cpp");
	const std::string CollisionHitboxSource = ReadTextFile("src/game/client/components/qmclient/collision_hitbox.cpp");
	const std::string BackgroundParticlesSource = ReadTextFile("src/game/client/components/tclient/background_particles.cpp");
	const std::string RenderLayerSource = ReadTextFile("src/game/map/render_layer.cpp");
	const std::string MapRendererSource = ReadTextFile("src/game/map/map_renderer.cpp");
	const std::string MovingTilesSource = ReadTextFile("src/game/client/components/tclient/moving_tiles.cpp");
	const std::string NameplatesSource = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");

	EXPECT_NE(GraphicsHeader.find("float ScreenAspect() const { return (float)ScreenWidth() / (float)ScreenHeight(); }"), std::string::npos);
	EXPECT_NE(GraphicsHeader.find("float GameScreenAspect() const { return m_GameScreenAspectOverride > 0.0f ? m_GameScreenAspectOverride : ScreenAspect(); }"), std::string::npos);
	EXPECT_NE(TClientSource.find("Graphics()->SetGameScreenAspectOverride(GameScreenAspectOverride);"), std::string::npos);
	EXPECT_EQ(TClientSource.find("SetScreenAspectOverride"), std::string::npos);
	EXPECT_NE(RenderLayerSource.find("Graphics()->GameScreenAspect()"), std::string::npos);
	EXPECT_NE(RenderLayerSource.find("Graphics()->MapScreenToGameInterface("), std::string::npos);
	EXPECT_NE(MapRendererSource.find("Graphics()->MapScreenToGameInterface("), std::string::npos);
	EXPECT_NE(MovingTilesSource.find("Graphics()->MapScreenToGameInterface("), std::string::npos);
	EXPECT_NE(NameplatesSource.find("This.Graphics()->MapScreenToGameInterface("), std::string::npos);
	EXPECT_NE(GameClientSource.find("CalcScreenParams(Graphics()->GameScreenAspect(), ShowDistanceZoom"), std::string::npos);
	EXPECT_NE(GameClientSource.find("m_LastScreenAspect = Graphics()->GameScreenAspect();"), std::string::npos);
	EXPECT_NE(GameClientSource.find("CalcScreenParams(Graphics()->GameScreenAspect(), m_Camera.m_Zoom"), std::string::npos);
	EXPECT_NE(ControlsSource.find("CalcScreenParams(Graphics()->GameScreenAspect(), 1.0f"), std::string::npos);
	EXPECT_NE(HudSource.find("CalcScreenParams(pGraphics->GameScreenAspect(), GameClient.m_Camera.m_Zoom"), std::string::npos);
	EXPECT_NE(HudSource.find("Graphics()->GameScreenAspect(), MiniZoom, aPoints"), std::string::npos);
	EXPECT_NE(HudSource.find("Graphics()->GameScreenAspect(), 1.0f, aPoints"), std::string::npos);
	EXPECT_NE(TouchControlsSource.find("CalcScreenParams(m_pTouchControls->Graphics()->GameScreenAspect()"), std::string::npos);
	EXPECT_NE(TouchControlsSource.find("CalcScreenParams(Graphics()->GameScreenAspect(), Zoom"), std::string::npos);
	EXPECT_NE(CollisionHitboxSource.find("Graphics()->GameScreenAspect(), GameClient()->m_Camera.m_Zoom"), std::string::npos);
	EXPECT_NE(BackgroundParticlesSource.find("Graphics()->GameScreenAspect(), Zoom, aPoints"), std::string::npos);

	const std::string MapScreenToInterface = FunctionBody(GraphicsSource, "void IGraphics::MapScreenToInterface(");
	const std::string MapScreenToGameInterface = FunctionBody(GraphicsSource, "void IGraphics::MapScreenToGameInterface(");
	EXPECT_NE(MapScreenToInterface.find("ScreenAspect()"), std::string::npos);
	EXPECT_EQ(MapScreenToInterface.find("GameScreenAspect()"), std::string::npos);
	EXPECT_NE(MapScreenToGameInterface.find("GameScreenAspect()"), std::string::npos);
	EXPECT_NE(UiSource.find("Graphics()->ScreenAspect()"), std::string::npos);
	EXPECT_EQ(UiSource.find("GameScreenAspect()"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientQueuesAspectRefreshFromSnapshots)
{
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string SnapshotBody = FunctionBody(TClientSource, "void CTClient::OnNewSnapshot()");
	const std::string UpdateBody = FunctionBody(TClientSource, "void CTClient::OnUpdate()");

	ASSERT_FALSE(SnapshotBody.empty());
	ASSERT_FALSE(UpdateBody.empty());
	EXPECT_NE(SnapshotBody.find("QueueAspectApply();"), std::string::npos);
	EXPECT_EQ(SnapshotBody.find("SetForcedAspect();"), std::string::npos);
	EXPECT_NE(UpdateBody.find("if(m_QmAspectApplyPending)"), std::string::npos);
	EXPECT_NE(UpdateBody.find("SetForcedAspect();"), std::string::npos);
}

TEST(QmNewUiMenuBranches, GraphicsBackendDropdownUsesCleanDisplayNames)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string BackendSource = ReadTextFile("src/engine/client/backend_sdl.cpp");
	const std::string OpenGLSource = ReadTextFile("src/engine/client/backend/opengl/backend_opengl.cpp");
	const std::string GraphicsHeader = ReadTextFile("src/engine/graphics.h");
	const std::string Formatter = FunctionBody(Source, "void FormatQmGraphicsBackendDisplayName(char *pBuf, int BufSize, const char *pBackendName, int Major, int Minor, int Patch, bool IsDefault)");
	const std::string RenderSettingsGraphics = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	const std::string GetDriverVersion = FunctionBody(BackendSource, "bool CGraphicsBackend_SDL_GL::GetDriverVersion(");

	ASSERT_FALSE(Formatter.empty());
	ASSERT_FALSE(RenderSettingsGraphics.empty());
	EXPECT_NE(Formatter.find("\"OpenGL %d.%d\""), std::string::npos);
	EXPECT_NE(Formatter.find("\"OpenGL (%s)\""), std::string::npos);
	EXPECT_NE(Formatter.find("Localize(\"auto\")"), std::string::npos);
	EXPECT_NE(Formatter.find("\"Vulkan\""), std::string::npos);
	EXPECT_NE(Formatter.find("\"GLES (%s)\""), std::string::npos);
	EXPECT_NE(Formatter.find("\"GLES %d.%d\""), std::string::npos);
	EXPECT_EQ(Formatter.find("QmClient"), std::string::npos);
	EXPECT_EQ(Formatter.find("\"OpenGL_QmClient_%d_%d\""), std::string::npos);
	EXPECT_EQ(Formatter.find("\"Vulkan_QmClient\""), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("Localize(\"Graphics backend\")"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("FormatQmGraphicsBackendDisplayName(aBackendDisplayName"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("ResolveSettingsSelectionWithCustomFallback"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("s_CustomBackendDisplayName"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("s_vGraphicsBackendInfos"), std::string::npos);
	EXPECT_NE(BackendSource.find("m_DetectedContextMajor"), std::string::npos);
	EXPECT_NE(GetDriverVersion.find("Major = 3;\n\t\t\tMinor = 3;"), std::string::npos);
	EXPECT_NE(GetDriverVersion.find("Major = 3;\n\t\t\tMinor = 0;"), std::string::npos);
	EXPECT_EQ(GetDriverVersion.find("m_Capabilities.m_DetectedContextMajor"), std::string::npos);
	EXPECT_EQ(GetDriverVersion.find("m_Capabilities.m_ContextMajor"), std::string::npos);
	EXPECT_NE(OpenGLSource.find("m_DetectedContextMajor = pCommand->m_pCapabilities->m_ContextMajor"), std::string::npos);
	EXPECT_NE(GraphicsHeader.find("GetDetectedContextVersion"), std::string::npos);
	EXPECT_NE(BackendSource.find("bool CGraphicsBackend_SDL_GL::GetDetectedContextVersion"), std::string::npos);
	EXPECT_NE(BackendSource.find("m_Capabilities.m_DetectedContextMajor"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("Graphics()->GetDetectedContextVersion"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("s_vGraphicsBackendInfos[Selected].m_Major == 0"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("\"%s (%s: %d.%d)\""), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const size_t BackendStartIndex = s_vSupportedBackendInfos.size();"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("s_vSupportedBackendInfos.insert(s_vSupportedBackendInfos.begin() + BackendStartIndex, AutoInfo);"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const SOpenGLVersion PreferredVersion = AutoOpenGLProbeVersion(EBackendType(i));"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("s_vSupportedBackendInfos.begin() + BackendStartIndex + 1, PreferredInfo"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("s_ActiveBackendDisplayName"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("s_vpGraphicsBackendNames[Selected] = s_ActiveBackendDisplayName.c_str();"), std::string::npos);
	EXPECT_NE(ReadTextFile("src/engine/client/graphics_threaded.cpp").find("RestoreAutomaticOpenGLConfig"), std::string::npos);
	EXPECT_NE(Source.find("static std::vector<const CCountryFlags::CCountryFlag *> s_vpFilteredFlags"), std::string::npos);
	EXPECT_NE(Source.find("s_vpFilteredFlags.reserve(GameClient()->m_CountryFlags.Num());"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("s_aScreenNamesCacheLanguage"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("const bool RefreshScreenNames"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("s_vSupportedBackendNames"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("s_BackendListCacheDriverBlocked"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("s_vGpuNames"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("s_vpGpuIdNames[i] = aCurDeviceName"), std::string::npos);
	EXPECT_EQ(RenderSettingsGraphics.find("Localize(\"Renderer\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DropDownPopupFollowsScrolledControlRect)
{
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string UiHeader = ReadTextFile("src/game/client/ui.h");
	const std::string DoDropDown = FunctionBody(UiSource, "int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, const SDropDownProperties &DropDownProps)");
	const std::string DoDropDownActive = FunctionBody(UiSource, "int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, bool Enabled)");
	const std::string DoPopupMenu = FunctionBody(UiSource, "void CUi::DoPopupMenu(");

	ASSERT_FALSE(DoDropDown.empty());
	ASSERT_FALSE(DoDropDownActive.empty());
	ASSERT_FALSE(DoPopupMenu.empty());
	EXPECT_NE(DoDropDown.find("bool PopupOpen = IsPopupOpen(&State.m_SelectionPopupContext);"), std::string::npos);
	EXPECT_NE(DoDropDown.find("if(PopupOpen)"), std::string::npos);
	EXPECT_NE(DoDropDown.find("ShowPopupSelection(pRect->x, pRect->y, &State.m_SelectionPopupContext);"), std::string::npos);
	EXPECT_NE(DoDropDown.find("PopupOpen = IsPopupOpen(&State.m_SelectionPopupContext);"), std::string::npos);
	EXPECT_NE(DoDropDown.find("if(State.m_DropDownState.IsOpen() && !PopupOpen)"), std::string::npos);
	// Popup 以设置页最外层 viewport 定位，并在锚点离开所属容器时关闭。
	EXPECT_NE(DoDropDown.find("DropDownProps.m_pPopupViewport != nullptr"), std::string::npos);
	EXPECT_NE(DoDropDown.find("QmDropdownAnchorFullyVisible(*pRect, AnchorViewport)"), std::string::npos);
	EXPECT_NE(DoDropDown.find("SQmDropdownInput DropDownInput;"), std::string::npos);
	EXPECT_NE(DoDropDown.find("State.m_DropDownState.Update(DropDownInput, Num);"), std::string::npos);
	EXPECT_NE(DoDropDown.find("DropDownInput.m_KeyUp = ConsumeHotkey(HOTKEY_UP);"), std::string::npos);
	EXPECT_NE(DoDropDown.find("DropDownInput.m_KeyDown = ConsumeHotkey(HOTKEY_DOWN);"), std::string::npos);
	EXPECT_NE(DoDropDown.find("DropDownInput.m_KeyEnter = ConsumeHotkey(HOTKEY_ENTER);"), std::string::npos);
	EXPECT_NE(DoDropDown.find("DropDownInput.m_KeyEscape = ConsumeHotkey(HOTKEY_ESCAPE);"), std::string::npos);
	EXPECT_NE(DoDropDown.find("State.m_SelectionPopupContext.m_ActiveIndex = State.m_DropDownState.ActiveIndex();"), std::string::npos);
	const size_t SelectedBranch = DoDropDown.find("if(DropDownResult.m_Selected)");
	const size_t ClosedBranch = DoDropDown.find("else if(DropDownResult.m_Closed)");
	ASSERT_NE(SelectedBranch, std::string::npos);
	ASSERT_NE(ClosedBranch, std::string::npos);
	EXPECT_LT(SelectedBranch, ClosedBranch);
	EXPECT_NE(DoPopupMenu.find("std::find_if(m_vPopupMenus.begin(), m_vPopupMenus.end()"), std::string::npos);
	EXPECT_NE(DoPopupMenu.find("ExistingPopupMenu->m_Rect.x = X;"), std::string::npos);
	EXPECT_NE(DoPopupMenu.find("ExistingPopupMenu->m_Rect.y = Y;"), std::string::npos);
	const size_t DisabledBranch = DoDropDown.find("if(!DropDownProps.m_Enabled)");
	const size_t CloseWhenDisabled = DoDropDown.find("if(DropDownProps.m_ClosePopupWhenDisabled)", DisabledBranch);
	const size_t CloseDisabledPopup = DoDropDown.find("ClosePopupMenu(&State.m_SelectionPopupContext);", DisabledBranch);
	ASSERT_NE(DisabledBranch, std::string::npos);
	ASSERT_NE(CloseWhenDisabled, std::string::npos);
	ASSERT_NE(CloseDisabledPopup, std::string::npos);
	EXPECT_LT(CloseWhenDisabled, CloseDisabledPopup);
	EXPECT_LT(DisabledBranch, CloseDisabledPopup);
	EXPECT_NE(UiHeader.find("m_ClosePopupWhenDisabled(true),"), std::string::npos);
	EXPECT_NE(DoDropDownActive.find("DropDownProps.m_ClosePopupWhenDisabled = false;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsDropdownsUseTheSharedWrapper)
{
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string ControlsSource = ReadTextFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string Wrapper = FunctionBody(MenusSource, "int CMenus::DoSettingsDropDown(CUIRect *pRect, const int CurSelection, const char *const *ppStrs, const int Num, CUi::SDropDownState &State, CUi::SDropDownProperties Properties)");

	ASSERT_FALSE(Wrapper.empty());
	EXPECT_NE(Wrapper.find("Properties.m_pAnchorViewport"), std::string::npos);
	EXPECT_NE(Wrapper.find("Properties.m_pPopupViewport"), std::string::npos);
	EXPECT_EQ(ControlsSource.find("Ui()->DoDropDown(&JoystickDropDown"), std::string::npos);
	EXPECT_NE(ControlsSource.find("GameClient()->m_Menus.DoSettingsDropDown(&JoystickDropDown"), std::string::npos);

	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string DoDropDown = FunctionBody(UiSource, "int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, const SDropDownProperties &DropDownProps)");
	ASSERT_FALSE(DoDropDown.empty());
	EXPECT_EQ(DoDropDown.find("static CScrollRegion"), std::string::npos);
	EXPECT_NE(DoDropDown.find("State.m_pOwnedScrollRegion = std::make_shared<CScrollRegion>();"), std::string::npos);
	EXPECT_NE(DoDropDown.find("State.m_pScrollRegion = State.m_SelectionPopupContext.m_pScrollRegion != nullptr"), std::string::npos);
	EXPECT_NE(DoDropDown.find("pScrollRegion != nullptr ? pScrollRegion : State.m_pScrollRegion"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsDropdownWrapperAndNestedListsKeepSharedVisualAndScrollContracts)
{
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string ListBoxHeader = ReadTextFile("src/game/client/ui_listbox.h");
	const std::string ListBoxSource = ReadTextFile("src/game/client/ui_listbox.cpp");
	const std::string Tee = FunctionBody(SettingsSource, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	const std::string Wrapper = FunctionBody(MenusSource, "int CMenus::DoSettingsDropDown(CUIRect *pRect, const int CurSelection, const char *const *ppStrs, const int Num, CUi::SDropDownState &State, CUi::SDropDownProperties Properties)");

	ASSERT_FALSE(Tee.empty());
	ASSERT_FALSE(Wrapper.empty());
	EXPECT_NE(Wrapper.find("Properties.m_VisualStyle = QmSettingsDropdownVisualStyle(m_SettingsUiTheme);"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("void SetScrollbarAlwaysReserved(bool AlwaysReserved)"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("ScrollParams.m_ScrollbarAlwaysReserved = m_ScrollbarAlwaysReserved;"), std::string::npos);
	EXPECT_NE(Tee.find("s_QueueListBox.SetScrollbarAlwaysReserved(true);"), std::string::npos);
	EXPECT_NE(Tee.find("s_PresetListBox.SetScrollbarAlwaysReserved(true);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, CallVoteSearchSupportsIndependentExclusion)
{
	const std::string MenusHeader = ReadTextFile("src/game/client/components/menus.h");
	const std::string IngameMenus = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const std::string ServerList = FunctionBody(IngameMenus, "bool CMenus::RenderServerControlServer(CUIRect MainView, bool UpdateScroll)");
	const std::string PlayerList = FunctionBody(IngameMenus, "bool CMenus::RenderServerControlKick(CUIRect MainView, bool FilterSpectators, bool UpdateScroll)");
	const std::string RenderControl = FunctionBody(IngameMenus, "void CMenus::RenderServerControl(CUIRect MainView)");

	ASSERT_FALSE(ServerList.empty());
	ASSERT_FALSE(PlayerList.empty());
	ASSERT_FALSE(RenderControl.empty());
	EXPECT_NE(MenusHeader.find("CLineInputBuffered<64> m_ExcludeInput;"), std::string::npos);
	EXPECT_NE(ServerList.find("QmTextMatchesIncludeExcludeFilter(pOption->m_aDescription, m_FilterInput.GetString(), m_ExcludeInput.GetString())"), std::string::npos);
	EXPECT_NE(PlayerList.find("QmTextMatchesIncludeExcludeFilter(GameClient()->m_aClients[Index].m_aName, m_FilterInput.GetString(), m_ExcludeInput.GetString())"), std::string::npos);
	EXPECT_NE(RenderControl.find("ingame_callvote_exclude"), std::string::npos);
	EXPECT_NE(RenderControl.find("CallvoteExcludeOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;"), std::string::npos);
	EXPECT_NE(RenderControl.find("CallvoteExcludeOptions.m_pPlaceholder = Localize(\"Exclude\");"), std::string::npos);
	EXPECT_NE(RenderControl.find("const float MapSortWidth = HasMapSort ? 140.0f : 0.0f;"), std::string::npos);
	EXPECT_NE(RenderControl.find("const float FilterWidth = std::min(220.0f, std::max(1.0f, (Bottom.w - 5.0f - MapSortWidth - MapSortGap) * 0.5f));"), std::string::npos);
}

TEST(QmNewUiMenuBranches, BrowserSearchUsesSharedIconAndExcludeKeepsItsOwnIconAndTooltipRect)
{
	const std::string Browser = FunctionBody(ReadTextFile("src/game/client/components/menus_browser.cpp"), "void CMenus::RenderServerbrowserStatusBox(CUIRect StatusBox, bool WasListboxItemActivated)");

	ASSERT_FALSE(Browser.empty());
	EXPECT_NE(Browser.find("ui_widget::InputField(ServerBrowserSearchCtx, &s_FilterInput, QuickSearch, SearchOptions)"), std::string::npos);
	EXPECT_EQ(Browser.find("Ui()->DoLabel(&QuickSearch, FONT_ICON_MAGNIFYING_GLASS"), std::string::npos);
	EXPECT_NE(Browser.find("Ui()->DoLabel(&QuickExclude, FONT_ICON_BAN"), std::string::npos);
	EXPECT_NE(Browser.find("DoToolTip(&s_ExcludeInput, &QuickExclude"), std::string::npos);
}

TEST(QmNewUiMenuBranches, IngameFavoriteMapsUsesSharedBookmarkIcon)
{
	const std::string Ingame = FunctionBody(ReadTextFile("src/game/client/components/menus_ingame.cpp"), "void CMenus::RenderInGameNetwork(CUIRect MainView)");

	ASSERT_FALSE(Ingame.empty());
	EXPECT_NE(Ingame.find("DoMenuTabV2(&s_FavoriteMapsButton, \"\", g_Config.m_UiPage == PAGE_FAVORITE_MAPS"), std::string::npos);
	EXPECT_NE(Ingame.find("QmIconManager()->RenderIcon(EQmIcon::BOOKMARK"), std::string::npos);
	EXPECT_NE(Ingame.find("FONT_ICON_BOOKMARK"), std::string::npos);
	EXPECT_NE(Ingame.find("TextRender()->TextColor(OldTextColor)"), std::string::npos);
	EXPECT_EQ(Ingame.find("\xF0\x9F\x94\x96"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TeePresetListUsesTheSameRowSpacingAsItsMeasuredViewport)
{
	const std::string Settings = FunctionBody(ReadTextFile("src/game/client/components/menus_settings.cpp"), "void CMenus::RenderSettingsTee(CUIRect MainView)");

	ASSERT_FALSE(Settings.empty());
	EXPECT_NE(Settings.find("const float PresetRowSpacing = TeeMetrics.m_LineSpacing * 0.5f;"), std::string::npos);
	EXPECT_NE(Settings.find("s_PresetListBox.DoAutoSpacing(PresetRowSpacing);"), std::string::npos);
	EXPECT_NE(Settings.find("s_PresetListBox.DoNextItem(&s_vPresetItemIds[i], ActivePresetIndex == (int)i, PresetRowSpacing)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DropDownKeyboardActiveIndexIsRendered)
{
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string UiHeader = ReadTextFile("src/game/client/ui.h");
	const std::string SelectionReset = FunctionBody(UiSource, "void CUi::SSelectionPopupContext::Reset()");
	const std::string PopupSelection = FunctionBody(UiSource, "CUi::EPopupMenuFunctionResult CUi::PopupSelection(void *pContext, CUIRect View, bool Active)");
	const std::string PopupButton = FunctionBody(UiSource, "int CUi::DoButton_PopupMenu(CButtonContainer *pButtonContainer");
	const std::string DoDropDown = FunctionBody(UiSource, "int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, const SDropDownProperties &DropDownProps)");

	ASSERT_FALSE(SelectionReset.empty());
	ASSERT_FALSE(PopupSelection.empty());
	ASSERT_FALSE(PopupButton.empty());
	ASSERT_FALSE(DoDropDown.empty());
	EXPECT_NE(UiHeader.find("int m_ActiveIndex;"), std::string::npos);
	EXPECT_NE(SelectionReset.find("m_ActiveIndex = -1;"), std::string::npos);
	EXPECT_NE(PopupButton.find("ButtonColor.has_value() || !TransparentInactive"), std::string::npos);
	EXPECT_NE(PopupSelection.find("const bool ActiveEntry = pSelectionPopup->m_ActiveIndex == static_cast<int>(Index);"), std::string::npos);
	EXPECT_NE(PopupSelection.find("ActiveEntry ? std::optional<ColorRGBA>"), std::string::npos);
	EXPECT_EQ(PopupSelection.find("Accent.VSplitLeft(2.0f"), std::string::npos);
	EXPECT_NE(PopupSelection.find("pSelectionPopup->m_TransparentButtons, true, ActiveColor"), std::string::npos);
	const size_t UpdateResult = DoDropDown.find("const SQmDropdownUpdateResult DropDownResult = State.m_DropDownState.Update(DropDownInput, Num);");
	const size_t ActiveIndexSync = DoDropDown.find("State.m_SelectionPopupContext.m_ActiveIndex = State.m_DropDownState.ActiveIndex();");
	const size_t PopupRender = DoDropDown.find("ShowPopupSelection(pRect->x, pRect->y, &State.m_SelectionPopupContext);");
	ASSERT_NE(UpdateResult, std::string::npos);
	ASSERT_NE(ActiveIndexSync, std::string::npos);
	ASSERT_NE(PopupRender, std::string::npos);
	EXPECT_LT(UpdateResult, ActiveIndexSync);
	EXPECT_LT(ActiveIndexSync, PopupRender);
}

TEST(QmNewUiMenuBranches, GeneralStandardPageUsesUnifiedSettingsStack)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Menus = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string General = FunctionBody(Source, "void CMenus::RenderSettingsGeneral(CUIRect MainView)");
	const std::string NumericLabelBridge = FunctionBody(Menus, "bool CMenus::PrepareSettingsNumericFieldLabel(");
	ASSERT_FALSE(General.empty());
	ASSERT_FALSE(NumericLabelBridge.empty());
	EXPECT_NE(General.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(General.find("SSettingsCardDefinition"), std::string::npos);
	EXPECT_NE(General.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(General.find("CQmScrollState"), std::string::npos);
	EXPECT_NE(General.find("const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(General.find("QmScrollRegionParamsFromPolicy(ScrollPolicy)"), std::string::npos);
	EXPECT_EQ(General.find("(void)QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(General.find("PrepareSettingsNumericFieldLabel("), std::string::npos);
	EXPECT_NE(NumericLabelBridge.find("if(m_MenuTextPlanCollecting)"), std::string::npos);
	EXPECT_NE(NumericLabelBridge.find("CollectMenuTextPlanItem(MENU_TEXT_SCOPE_SETTINGS"), std::string::npos);
	EXPECT_NE(General.find("ui_widget::NumericField("), std::string::npos);
	EXPECT_NE(General.find("const auto IsGeneralDynamicCameraEnabled"), std::string::npos);
	EXPECT_NE(General.find("ResolveSettingsGeneralGameContentHeight(GeneralMetrics, IsGeneralDynamicCameraEnabled())"), std::string::npos);
	EXPECT_NE(General.find("vCards.back().m_PreLayoutInput = [this, GeneralMetrics, IsGeneralDynamicCameraEnabled]"), std::string::npos);
	EXPECT_NE(General.find("ResolveSettingsGeneralLanguageListGeometry("), std::string::npos);
	EXPECT_NE(General.find("ResolveSettingsGeneralThemeListGeometry("), std::string::npos);
	EXPECT_NE(General.find("RenderLanguageSelection(Content, &GeneralMetrics);"), std::string::npos);
	EXPECT_NE(General.find("RenderThemeSelection(Content, &GeneralMetrics);"), std::string::npos);
	EXPECT_NE(General.find("ResolveSettingsGeneralLayoutRevision("), std::string::npos);
	EXPECT_NE(General.find("Content.h = std::min(Content.h, GeneralLanguageListHeight);"), std::string::npos);
	EXPECT_NE(General.find("Content.h = std::min(Content.h, GeneralThemeListHeight);"), std::string::npos);
	EXPECT_NE(General.find("Row.VSplitMid(&LeftButton, &RightButton, GeneralMetrics.m_LineSpacing);"), std::string::npos);
	EXPECT_EQ(General.find("maximum(300.0f * UiScale, GeneralPage.m_ScrollViewport.h - 100.0f * UiScale)"), std::string::npos);
	EXPECT_NE(General.find("deck:general-game"), std::string::npos);
	EXPECT_NE(General.find("deck:general-language"), std::string::npos);
	EXPECT_NE(General.find("deck:general-client"), std::string::npos);
	EXPECT_NE(General.find("deck:general-recording"), std::string::npos);
	EXPECT_NE(General.find("RecordingDefinition.m_MeasureRevision"), std::string::npos);
	EXPECT_NE(General.find("RecordingDefinition.m_PreLayoutInput"), std::string::npos);
	EXPECT_NE(General.find("RecordingDefinition.m_VisibilityController = true;"), std::string::npos);
	EXPECT_NE(General.find("return 4.0f * GeneralMetrics.m_RowStep + EnabledRows * (GeneralMetrics.m_RowStep + GeneralMetrics.m_LineSpacing);"), std::string::npos);
	EXPECT_EQ(General.find("AddCard(RecordingSpec"), std::string::npos);
	EXPECT_EQ(General.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(General.find("DoSettingsScrollbarOption("), std::string::npos);
	EXPECT_EQ(General.find("Ui()->DoEditBox("), std::string::npos);
	EXPECT_EQ(General.find("Ui()->DoScrollbarH("), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsCardContentHeightsExcludeSharedHeaderChrome)
{
	const std::string ControlsSource = ReadTextFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string ContributorsSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string MouseMeasure = FunctionBody(ControlsSource, "float CMenusSettingsControls::MeasureSettingsMouseHeight() const");
	const std::string Contributors = FunctionBody(ContributorsSource, "void CMenus::RenderSettingsQmClientContributors(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(MouseMeasure.empty());
	ASSERT_FALSE(Contributors.empty());
	EXPECT_NE(MouseMeasure.find("return 2.0f * BUTTON_HEIGHT + BUTTON_SPACING;"), std::string::npos);
	EXPECT_EQ(MouseMeasure.find("CARD_HEADER"), std::string::npos);
	EXPECT_NE(Contributors.find("Community.m_Measure = [LineHeight, LineSpacing](float) { return ResolveSettingsRowsHeight(3, LineHeight, LineSpacing); };"), std::string::npos);
}
TEST(QmNewUiMenuBranches, PlayerStandardPageUsesUnifiedSettingsStack)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	// 搜索页的 tab 路由表已删除：卡片 tab → 标准设置页的落地映射由 SetSettingsPageFromCardTab 统一拥有。
	const std::string Navigation = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Identity = FunctionBody(Source, "void CMenus::RenderSettingsTeeIdentity(CUIRect MainView, CUIRect *pFlagButton, float BodySize)");
	const std::string Player = FunctionBody(Source, "void CMenus::RenderSettingsPlayer(CUIRect MainView)");
	ASSERT_FALSE(Identity.empty());
	ASSERT_FALSE(Player.empty());
	EXPECT_NE(Identity.find("ui_widget::InputField("), std::string::npos);
	EXPECT_NE(Player.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(Player.find("SSettingsCardDefinition"), std::string::npos);
	EXPECT_NE(Player.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(Player.find("CQmScrollState"), std::string::npos);
	EXPECT_NE(Player.find("const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(Player.find("QmScrollRegionParamsFromPolicy(ScrollPolicy)"), std::string::npos);
	EXPECT_NE(Player.find("ui_widget::InputField("), std::string::npos);
	const std::string ListBox = ReadTextFile("src/game/client/ui_listbox.cpp");
	const size_t PlayerListPriority = Player.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t PlayerListStart = Player.find("s_ListBox.DoStart(");
	ASSERT_NE(PlayerListPriority, std::string::npos);
	ASSERT_NE(PlayerListStart, std::string::npos);
	EXPECT_LT(PlayerListPriority, PlayerListStart);
	EXPECT_NE(ListBox.find("ScrollParams.m_WheelOwnerPriority = m_WheelOwnerPriority;"), std::string::npos);
	EXPECT_NE(ListBox.find("m_WheelOwnerPriority = EUiWheelOwnerPriority::PAGE;"), std::string::npos);
	EXPECT_NE(Player.find("deck:player-identity"), std::string::npos);
	EXPECT_NE(Player.find("deck:player-country"), std::string::npos);
	EXPECT_NE(FunctionBody(Navigation, "bool CMenus::SetSettingsPageFromCardTab(const char *pTab)").find("str_comp(pTab, \"player\") == 0)\n\t\tg_Config.m_UiSettingsPage = SETTINGS_PLAYER;"), std::string::npos);
	EXPECT_EQ(Player.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(Player.find("ui_widget::TextField("), std::string::npos);
	EXPECT_EQ(Player.find("ui_widget::SearchField("), std::string::npos);
	EXPECT_EQ(Player.find("Ui()->DoEditBox("), std::string::npos);
	EXPECT_EQ(Player.find("Ui()->DoScrollbarH("), std::string::npos);
}

TEST(QmNewUiMenuBranches, TeeStandardPageUsesUnifiedSettingsStack)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string DeckSource = ReadTextFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	// 搜索页的 tab 路由表已删除：卡片 tab → 标准设置页的落地映射由 SetSettingsPageFromCardTab 统一拥有。
	const std::string Navigation = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Tee = FunctionBody(Source, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_FALSE(Tee.empty());
	EXPECT_NE(Tee.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(Tee.find("SSettingsCardDefinition"), std::string::npos);
	EXPECT_NE(Tee.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(Tee.find("CQmScrollState"), std::string::npos);
	EXPECT_NE(Tee.find("const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(Tee.find("QmScrollRegionParamsFromPolicy(ScrollPolicy)"), std::string::npos);
	EXPECT_NE(Tee.find("ui_widget::InputField("), std::string::npos);
	EXPECT_NE(Tee.find("ui_widget::NumericField("), std::string::npos);
	EXPECT_NE(Tee.find("SetSettingsTeeVisibleSnapshot("), std::string::npos);
	const size_t SkinListPriority = Tee.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t SkinListStart = Tee.find("s_ListBox.DoStart(TeeSkinListRowHeight", SkinListPriority);
	const size_t DeckRender = Tee.find("SettingsCardDeckForRenderPass().RenderCached(");
	const size_t RefreshAfterDeck = Tee.find("if(!RenderOnly && ShouldRefresh)", DeckRender);
	ASSERT_NE(SkinListPriority, std::string::npos);
	ASSERT_NE(SkinListStart, std::string::npos);
	ASSERT_NE(DeckRender, std::string::npos);
	ASSERT_NE(RefreshAfterDeck, std::string::npos);
	EXPECT_LT(SkinListPriority, SkinListStart);
	EXPECT_LT(DeckRender, RefreshAfterDeck);
	EXPECT_NE(Tee.find("IdentityContentHeight"), std::string::npos);
	EXPECT_NE(Tee.find("ResolveTeeTopContentHeight"), std::string::npos);
	EXPECT_NE(Tee.find("ListContentHeight"), std::string::npos);
	EXPECT_NE(Tee.find("constexpr int TeeSkinGridVisibleRows = 6;"), std::string::npos);
	EXPECT_NE(Tee.find("ResolveSettingsTeeQueuePanelHeight(TeeMetrics, QueueItemCount, QueuePresetCount)"), std::string::npos);
	EXPECT_NE(Tee.find("ResolveSettingsTeeQueuePanelGeometry(TeeMetrics, (int)SkinQueue.size(), (int)vQueuePresets.size())"), std::string::npos);
	EXPECT_NE(Tee.find("QueueListBody.HSplitTop(TeeMetrics.m_LineSpacing, nullptr, &QueueListBody);"), std::string::npos);
	EXPECT_NE(Tee.find("QueueGeometry.m_QueueListViewportHeight"), std::string::npos);
	EXPECT_NE(Tee.find("TeeMetrics.m_ButtonHeight), &QueueListHeaderLabel, &ClearQueueRect"), std::string::npos);
	EXPECT_NE(Tee.find("s_QueueListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED"), std::string::npos);
	EXPECT_NE(Tee.find("s_PresetListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED"), std::string::npos);
	EXPECT_NE(Tee.find("const float QueueValueInputWidth = 58.0f * UiScale;"), std::string::npos);
	EXPECT_NE(Tee.find("const float QueueIntervalLabelWidth = TextRender()->TextWidth(BodySize, pQueueIntervalLabel) + TeeMetrics.m_LineSpacing;"), std::string::npos);
	EXPECT_NE(Tee.find("IntervalRow.VSplitLeft(minimum(IntervalRow.w, QueueIntervalLabelWidth), &IntervalLabel, &IntervalControls);"), std::string::npos);
	EXPECT_NE(Tee.find("QuickSearch.VSplitRight(SkinControlGap, &QuickSearch, nullptr);"), std::string::npos);
	EXPECT_NE(Tee.find("DrawRoundedSurface(Ui(), QueueSection, ui_token::color::SURFACE_OVERLAY"), std::string::npos);
	EXPECT_NE(Tee.find("const float MinimumSearchWidth = 140.0f * UiScale;"), std::string::npos);
	EXPECT_EQ(Tee.find("SkinSearchPreferredWidth"), std::string::npos);
	EXPECT_NE(Tee.find("AddCard(IdentitySpec, [IdentityContentHeight]"), std::string::npos);
	EXPECT_NE(Tee.find("vCards.back().m_PreLayoutInput = [this, TeeMetrics, ControlSpacing, ControlLineHeight, pUseCustomColor]"), std::string::npos);
	EXPECT_NE(Tee.find("AddCard(OptionsSpec, [ResolveTeeTopContentHeight]"), std::string::npos);
	EXPECT_EQ(Tee.find("TeePage.m_ScrollViewport.h * 0.8f"), std::string::npos);
	EXPECT_NE(Tee.find("Definition.m_MeasureRevision = MeasureRevision;"), std::string::npos);
	EXPECT_EQ(Tee.find("AddCard(IdentitySpec, 180.0f * UiScale"), std::string::npos);
	EXPECT_EQ(Tee.find("AddCard(OptionsSpec, 420.0f * UiScale"), std::string::npos);
	EXPECT_EQ(Tee.find("AddCard(ListSpec, 760.0f * UiScale"), std::string::npos);
	const size_t ListCard = Tee.find("AddCard(ListSpec, [ListContentHeight]");
	ASSERT_NE(ListCard, std::string::npos);
	EXPECT_NE(Tee.find("if(TeeSectionVisible(Content))", ListCard), std::string::npos);
	EXPECT_NE(Tee.find("RenderList(Content);", ListCard), std::string::npos);
	EXPECT_NE(Tee.find("AdvanceListOffscreen();"), std::string::npos);
	EXPECT_NE(Tee.find("gs_TeeListPreviewCache.BeginFrame();"), std::string::npos);
	EXPECT_NE(Tee.find("BeginSettingsUiFrameScheduler("), std::string::npos);
	EXPECT_NE(Tee.find("SettingsSkinBackgroundRequestBudgetDecision({"), std::string::npos);
	EXPECT_NE(Tee.find("RequestLoad(ESettingsResourcePriority::VISIBLE)"), std::string::npos);
	EXPECT_NE(Tee.find("SetSettingsTeeVisibleSnapshot(VisibleSnapshot)"), std::string::npos);
	EXPECT_NE(Tee.find("s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_GRID);"), std::string::npos);
	EXPECT_NE(Tee.find("gs_TeeListDrainPerfSession.m_LastLoads = LoadsNow;"), std::string::npos);
	EXPECT_NE(Tee.find("gs_TeeSettingsPageState.m_LastRequestBudgetBlockReason = BackgroundBudgetDecision.m_BlockReason;"), std::string::npos);
	EXPECT_NE(DeckSource.find("if(DrawLayout.m_TwoColumns && !aDisplayColumns[0].empty())"), std::string::npos);
	EXPECT_NE(DeckSource.find("const size_t NumLayers = std::max({aDisplayColumns[0].size(), aDisplayColumns[1].size(), aDisplayColumns[2].size()});"), std::string::npos);
	const size_t LeftLayerCard = DeckSource.find("AppendCard(aDisplayColumns[1][Layer], 1, DrawLayout.m_aColumns[0], LeftPlan);");
	const size_t RightLayerCard = DeckSource.find("AppendCard(aDisplayColumns[2][Layer], 2, DrawLayout.m_aColumns[1], RightPlan);");
	const size_t FullLayerCard = DeckSource.find("AppendCard(aDisplayColumns[0][Layer], 0, DrawLayout.m_ContentViewport, FullPlan);");
	ASSERT_NE(LeftLayerCard, std::string::npos);
	ASSERT_NE(RightLayerCard, std::string::npos);
	ASSERT_NE(FullLayerCard, std::string::npos);
	EXPECT_LT(LeftLayerCard, FullLayerCard);
	EXPECT_LT(RightLayerCard, FullLayerCard);
	EXPECT_NE(DeckSource.find("CSettingsCardColumnFramePlan FullPlan(std::max(LeftPlan.CursorY(), RightPlan.CursorY()), DrawLayout.m_CardGap);"), std::string::npos);
	EXPECT_NE(DeckSource.find("LeftPlan.SetCursorY(FullPlan.CursorY());"), std::string::npos);
	EXPECT_NE(DeckSource.find("RightPlan.SetCursorY(FullPlan.CursorY());"), std::string::npos);
	EXPECT_NE(DeckSource.find("if(Visible || Card.m_pDefinition->m_RenderWhenClipped)"), std::string::npos);
	EXPECT_NE(Tee.find("deck:tee-identity"), std::string::npos);
	EXPECT_NE(Tee.find("deck:tee-skin-options"), std::string::npos);
	EXPECT_NE(Tee.find("deck:tee-skin-list"), std::string::npos);
	EXPECT_NE(FunctionBody(Navigation, "bool CMenus::SetSettingsPageFromCardTab(const char *pTab)").find("str_comp(pTab, \"tee\") == 0)\n\t\tg_Config.m_UiSettingsPage = SETTINGS_TEE;"), std::string::npos);
	EXPECT_EQ(Tee.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(Tee.find("DoSettingsScrollbarOption("), std::string::npos);
	EXPECT_NE(Tee.find("ui_widget::InputField(ColorCodeInputCtx, &ColorCodeInput, ColorCodeEditBox, ColorCodeInputOptions).m_Changed"), std::string::npos);
	EXPECT_EQ(Tee.find("Ui()->DoEditBox(&ColorCodeInput, &ColorCodeEditBox"), std::string::npos);
	const std::string Ui = ReadTextFile("src/game/client/ui.cpp");
	EXPECT_NE(FunctionBody(Ui, "bool CUi::DoEditBox(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, int Corners, const std::vector<STextColorSplit> &vColorSplits, int Align, const SEditBoxRenderOptions &RenderOptions)").find("DrawRoundedSurface(this, *pRect"), std::string::npos);
	EXPECT_EQ(Tee.find("Ui()->DoScrollbarH("), std::string::npos);
}

TEST(QmNewUiMenuBranches, TeeRestoresCardContentsAndKeepsDoubleClickActions)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Tee = FunctionBody(Source, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_FALSE(Tee.empty());
	const size_t Options = Tee.find("const auto RenderOptions =");
	const size_t Identity = Tee.find("const auto RenderIdentity =");
	const size_t List = Tee.find("const auto RenderList =");
	ASSERT_NE(Options, std::string::npos);
	ASSERT_NE(Identity, std::string::npos);
	ASSERT_NE(List, std::string::npos);
	const size_t Colors = Tee.find("ResolveSettingsTeeCustomColorsLayout(MainView,");
	EXPECT_GT(Colors, Options);
	EXPECT_LT(Colors, Identity);
	const size_t Queue = Tee.find("CUIRect QueuePanel;");
	EXPECT_GT(Queue, List);
	EXPECT_LT(Queue, Tee.find("// Layout bottom controls", List));
	EXPECT_EQ(Tee.find("const SSettingsTeeCustomColorsLayout TeeCustomColors"), std::string::npos);
	// 双击功能保留，布局恢复不能移除本体与分身的快捷应用入口。
	EXPECT_EQ(Tee.find("MouseDoubleClick"), std::string::npos);
	const size_t ItemButton = Tee.find("const int ItemButton = Ui()->DoButtonLogic(SkinListEntry.ListItemId(), 0, &Item.m_Rect, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);");
	ASSERT_NE(ItemButton, std::string::npos);
	EXPECT_NE(Tee.find("if(ItemButton != 0 && Ui()->DoDoubleClickLogic(SkinListEntry.ListItemId()))", ItemButton), std::string::npos);
	EXPECT_NE(Tee.find("QmTeeSkinApplyTargetForButton(ItemButton)"), std::string::npos);
	EXPECT_NE(Tee.find("ApplySkinListEntry(SkinListEntry, Target, QmTeeSkinApplyTargetDummy(Target) != (m_Dummy ? 1 : 0));"), std::string::npos);
	const size_t DoubleClick = Tee.find("Ui()->DoDoubleClickLogic(", ItemButton);
	ASSERT_NE(DoubleClick, std::string::npos);
	EXPECT_EQ(Tee.find("Ui()->DoDoubleClickLogic(", DoubleClick + 1), std::string::npos);
	// 单击与双击共用同一份赋值路径。
	EXPECT_NE(Tee.find("ApplySkinListEntry(vSkinList[NewSelected], m_Dummy ? ETeeSkinApplyTarget::DUMMY : ETeeSkinApplyTarget::MAIN, false);"), std::string::npos);
	EXPECT_NE(Tee.find("QmApplyTeeSkinToTarget(g_Config, Target, Entry.SkinContainer()->Name(), HasColorKey, EntryUseCustomColor, EntryColorBody, EntryColorFeet);"), std::string::npos);
	EXPECT_NE(Source.find("#include <game/client/components/qmclient/tee_skin_apply.h>"), std::string::npos);
	EXPECT_NE(Tee.find("Localize(\"Double-click: left applies to main, right applies to dummy\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TeeOriginalLayoutRestoresSavedVersionEightPositions)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const size_t Start = Source.find("if(g_Config.m_QmCardLayoutVersion < 9)");
	ASSERT_NE(Start, std::string::npos);
	const size_t End = Source.find("g_Config.m_QmCardLayoutVersion = 9;", Start);
	ASSERT_NE(End, std::string::npos);
	const std::string Migration = Source.substr(Start, End - Start);
	EXPECT_EQ(Source.find("if(g_Config.m_QmCardLayoutVersion < 8)"), std::string::npos);
	EXPECT_NE(Migration.find("Candidate.MoveToTab(\"deck:tee-identity\", \"tee\", 1, 0);"), std::string::npos);
	EXPECT_NE(Migration.find("Candidate.MoveToTab(\"deck:tee-skin-options\", \"tee\", 2, 0);"), std::string::npos);
	EXPECT_NE(Migration.find("Candidate.MoveToTab(\"deck:tee-skin-list\", \"tee\", 0, 0);"), std::string::npos);
	EXPECT_NE(Migration.find("if(!PersistCandidate(Candidate, true))"), std::string::npos);
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmCardLayoutVersion, qm_card_layout_version, 0, 0, 9,"), std::string::npos);
}

TEST(QmNewUiMenuBranches, Tee7NestedGridsOwnWheelAndCacheRefreshes)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings7.cpp");
	const std::string Tee7 = FunctionBody(Source, "void CMenus::RenderSettingsTee7Content(CUIRect MainView, const SSettingsContentMetrics &Metrics)");
	const std::string SkinSelection = FunctionBody(Source, "void CMenus::RenderSkinSelection7(CUIRect MainView, float BodySize)");
	const std::string SkinPartSelection = FunctionBody(Source, "void CMenus::RenderSkinPartSelection7(CUIRect MainView, float BodySize)");
	ASSERT_FALSE(Tee7.empty());
	ASSERT_FALSE(SkinSelection.empty());
	ASSERT_FALSE(SkinPartSelection.empty());
	EXPECT_EQ(Tee7.find("Buttons.VSplitLeft(220.0f, &QuickSearch, &Buttons);"), std::string::npos);
	EXPECT_NE(Tee7.find("Buttons.VSplitRight(120.0f, &QuickSearch, &SaveDeleteButton);"), std::string::npos);

	const size_t SkinPriority = SkinSelection.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t SkinStart = SkinSelection.find("s_ListBox.DoStart(", SkinPriority);
	ASSERT_NE(SkinPriority, std::string::npos);
	ASSERT_NE(SkinStart, std::string::npos);
	EXPECT_LT(SkinPriority, SkinStart);
	EXPECT_NE(SkinSelection.find("SetScrollProfile(EQmScrollProfile::SETTINGS_GRID)"), std::string::npos);
	EXPECT_NE(SkinSelection.find("std::vector<std::string>"), std::string::npos);
	EXPECT_EQ(SkinSelection.find("std::vector<const CSkins7::CSkin *>"), std::string::npos);
	EXPECT_NE(SkinSelection.find("m_SkinList7LastRefreshTime.value() != RefreshTime"), std::string::npos);
	EXPECT_NE(SkinSelection.find("m_SkinList7LastRefreshTime = RefreshTime;"), std::string::npos);
	EXPECT_EQ(SkinSelection.find("m_SkinList7LastRefreshTime.value() != m_SkinList7LastRefreshTime"), std::string::npos);

	const size_t SkinPartPriority = SkinPartSelection.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t SkinPartStart = SkinPartSelection.find("s_ListBox.DoStart(", SkinPartPriority);
	ASSERT_NE(SkinPartPriority, std::string::npos);
	ASSERT_NE(SkinPartStart, std::string::npos);
	EXPECT_LT(SkinPartPriority, SkinPartStart);
	EXPECT_NE(SkinPartSelection.find("SetScrollProfile(EQmScrollProfile::SETTINGS_GRID)"), std::string::npos);
	EXPECT_NE(SkinPartSelection.find("std::vector<std::string>"), std::string::npos);
	EXPECT_EQ(SkinPartSelection.find("std::vector<const CSkins7::CSkinPart *>"), std::string::npos);
	EXPECT_NE(SkinPartSelection.find("m_SkinPartsList7LastRefreshTime.value() != RefreshTime"), std::string::npos);
	EXPECT_NE(SkinPartSelection.find("m_SkinPartsList7LastRefreshTime = RefreshTime;"), std::string::npos);
	EXPECT_EQ(SkinPartSelection.find("m_SkinList7LastRefreshTime"), std::string::npos);
}

TEST(QmNewUiMenuBranches, CountryPopupOwnsWheelAndBlocksTheSettingsPage)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Popup = FunctionBody(Source, "CUi::EPopupMenuFunctionResult CMenus::PopupSettingsCountrySelection(void *pContext, CUIRect View, bool Active)");
	const std::string Identity = FunctionBody(Source, "void CMenus::RenderSettingsTeeIdentity(CUIRect MainView, CUIRect *pFlagButton, float BodySize)");
	const std::string MapPopup = FunctionBody(Source, "CUi::EPopupMenuFunctionResult CMenus::PopupMapPicker(void *pContext, CUIRect View, bool Active)");
	const std::string DDNet = FunctionBody(Source, "void CMenus::RenderSettingsDDNet(CUIRect MainView)");
	ASSERT_FALSE(Popup.empty());
	ASSERT_FALSE(Identity.empty());
	ASSERT_FALSE(MapPopup.empty());
	ASSERT_FALSE(DDNet.empty());
	EXPECT_NE(Popup.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::POPUP);"), std::string::npos);
	EXPECT_NE(Popup.find("s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_GRID);"), std::string::npos);
	EXPECT_NE(Identity.find("PopupProps.m_BlockUnderlyingScroll = true;"), std::string::npos);
	EXPECT_NE(Identity.find("PopupSettingsCountrySelection, PopupProps"), std::string::npos);
	EXPECT_NE(MapPopup.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::POPUP);"), std::string::npos);
	EXPECT_NE(MapPopup.find("s_ListBox.SetScrollProfile(EQmScrollProfile::POPUP_LIST);"), std::string::npos);
	const size_t MapPickerId = DDNet.find("s_PopupMapPickerId");
	ASSERT_NE(MapPickerId, std::string::npos);
	EXPECT_NE(DDNet.find("QmResolveDropdownPopupPolicy", MapPickerId), std::string::npos);
	EXPECT_NE(DDNet.find("CUi::PopupMenuContentInset()", MapPickerId), std::string::npos);
	EXPECT_NE(DDNet.find("PopupPolicy.m_PreferredHeight", MapPickerId), std::string::npos);
	EXPECT_NE(DDNet.find("PopupProps.m_BlockUnderlyingScroll = true;", MapPickerId), std::string::npos);
	EXPECT_NE(DDNet.find("PopupMapPicker, PopupProps", MapPickerId), std::string::npos);
}

TEST(QmNewUiMenuBranches, TeeOptionsMeasureAllRowsAndPlayerDummyChangeDisplayCycle)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Tee = FunctionBody(Source, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	const std::string Settings = FunctionBody(Source, "void CMenus::RenderSettings(CUIRect MainView)");
	ASSERT_FALSE(Tee.empty());
	ASSERT_FALSE(Settings.empty());
	EXPECT_NE(Tee.find("ResolveSettingsRowsHeight(4, ControlLineHeight, ControlSpacing)"), std::string::npos);
	EXPECT_NE(Tee.find("ResolveSettingsRowsHeight(6, ControlLineHeight, ControlSpacing)"), std::string::npos);
	EXPECT_NE(Tee.find("ResolveSettingsTeeCustomColorsLayout"), std::string::npos);
	EXPECT_NE(Tee.find("g_Config.m_QmSkinShowMetadata != 0"), std::string::npos);
	EXPECT_EQ(Tee.find("g_Config.m_QmSkinSortMode == 1 && g_Config.m_QmSkinShowMetadata"), std::string::npos);
	EXPECT_NE(Tee.find("SkinSortDropDownProps.m_FontSize = BodySize;"), std::string::npos);
	EXPECT_NE(Tee.find("const float SortLabelWidth = std::clamp(SortModeControl.w * 0.36f"), std::string::npos);
	EXPECT_NE(Tee.find("SortDropDown.VSplitLeft(ControlSpacing, nullptr, &SortDropDown);"), std::string::npos);
	EXPECT_EQ(Tee.find("settings_tee_skin_sort_dropdown"), std::string::npos);
	EXPECT_EQ(Tee.find("SkinSortDropDownProps.m_VisualStyle"), std::string::npos);
	EXPECT_NE(ConfigSource.find("\"Show skin release date and author\""), std::string::npos);
	EXPECT_EQ(ConfigSource.find("\"Show release date and author when sorted by date\""), std::string::npos);
	EXPECT_NE(Tee.find("const auto NextCheckboxRow"), std::string::npos);
	EXPECT_NE(Tee.find("const auto NextPrefixRow"), std::string::npos);
	EXPECT_NE(Tee.find("s_QueueListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);"), std::string::npos);
	EXPECT_NE(Tee.find("s_PresetListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);"), std::string::npos);
	const size_t DummyInput = Tee.find("s_TeeSubTab = 1;");
	const size_t DisplayCycle = Tee.find("const uint64_t TeeDisplayKey", DummyInput);
	ASSERT_NE(DummyInput, std::string::npos);
	ASSERT_NE(DisplayCycle, std::string::npos);
	EXPECT_LT(DummyInput, DisplayCycle);
	EXPECT_NE(Tee.find("m_SettingsCardDeckDisplayState.EnterView(TeeDisplayKey)", DisplayCycle), std::string::npos);
	EXPECT_NE(Settings.find("g_Config.m_UiSettingsPage != SETTINGS_TEE"), std::string::npos);
	EXPECT_EQ(Settings.find("m_Dummy + 1"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsSubTabPagesUseTheSharedLayoutContract)
{
	const std::string Settings = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Assets = ReadTextFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string TClient = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string QmClient = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	EXPECT_NE(FunctionBody(Settings, "void CMenus::RenderSettingsPlayer(CUIRect MainView)").find("ResolveSettingsSubTabLayout("), std::string::npos);
	EXPECT_NE(FunctionBody(Settings, "void CMenus::RenderSettingsTee(CUIRect MainView)").find("ResolveSettingsSubTabLayout("), std::string::npos);
	EXPECT_NE(FunctionBody(Settings, "void CMenus::RenderSettingsAppearance(CUIRect MainView)").find("ResolveSettingsSubTabLayout("), std::string::npos);
	EXPECT_NE(FunctionBody(Assets, "void CMenus::RenderSettingsCustom(CUIRect MainView)").find("ResolveSettingsSubTabLayout("), std::string::npos);
	EXPECT_NE(FunctionBody(TClient, "void CMenus::RenderSettingsTClient(CUIRect MainView, bool PrewarmOnly)").find("TClientSettingsContentView("), std::string::npos);
	EXPECT_NE(TClient.find("ResolveSettingsSubTabLayout(MainView, Metrics.m_UiScale)"), std::string::npos);
	EXPECT_NE(FunctionBody(QmClient, "void CMenus::RenderSettingsQmClientContent(CUIRect MainView, bool ContributorsPage, bool PrewarmOnly)").find("ResolveSettingsSubTabLayout("), std::string::npos);
}

TEST(QmNewUiMenuBranches, GraphicsAndSoundNestedListsOwnWheel)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Graphics = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	const std::string Sound = FunctionBody(Source, "void CMenus::RenderSettingsSound(CUIRect MainView)");
	ASSERT_FALSE(Graphics.empty());
	ASSERT_FALSE(Sound.empty());
	const size_t GraphicsPriority = Graphics.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t GraphicsStart = Graphics.find("s_ListBox.DoStart(RowHeightResList", GraphicsPriority);
	const size_t SoundPriority = Sound.find("s_AudioPackListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t SoundStart = Sound.find("s_AudioPackListBox.DoStart(", SoundPriority);
	ASSERT_NE(GraphicsPriority, std::string::npos);
	ASSERT_NE(GraphicsStart, std::string::npos);
	ASSERT_NE(SoundPriority, std::string::npos);
	ASSERT_NE(SoundStart, std::string::npos);
	EXPECT_LT(GraphicsPriority, GraphicsStart);
	EXPECT_LT(SoundPriority, SoundStart);
	EXPECT_NE(Graphics.find("s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);"), std::string::npos);
	EXPECT_NE(Sound.find("s_AudioPackListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);"), std::string::npos);
	EXPECT_NE(Graphics.find("ResolveSettingsGraphicsModesGeometry("), std::string::npos);
	EXPECT_NE(Sound.find("ResolveSettingsSoundAudioPackGeometry("), std::string::npos);
	EXPECT_NE(Sound.find("ResolveSettingsSoundAudioPackGeometry(AudioPackCount, SoundMetrics)"), std::string::npos);
	EXPECT_NE(Sound.find("s_AudioPackListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED"), std::string::npos);
	EXPECT_NE(Sound.find("ResolveSettingsSoundLayoutRevision(RenderOnly, g_Config.m_SndEnable != 0, AudioPackCount)"), std::string::npos);
	EXPECT_NE(Graphics.find("const int GraphicsBackendRowCount"), std::string::npos);
	EXPECT_NE(Graphics.find("GraphicsDisplayRowCount = 5 + (Graphics()->GetNumScreens() > 1 ? 1 : 0) + GraphicsBackendRowCount"), std::string::npos);
	EXPECT_EQ(Graphics.find("const auto NextBackendRow"), std::string::npos);
	EXPECT_NE(Graphics.find("GraphicsModesMeasureRevision"), std::string::npos);
	EXPECT_NE(Graphics.find("s_ListBox.SetHideScrollbar(true);"), std::string::npos);
	EXPECT_NE(Graphics.find("s_ListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_HOVER);"), std::string::npos);
	EXPECT_EQ(Graphics.find("GraphicsBackendMinCardHeight = 104.0f"), std::string::npos);
	EXPECT_EQ(Graphics.find("Localize(\"Graphics card\"), 16.0f"), std::string::npos);
}

TEST(QmNewUiMenuBranches, GraphicsPilotHasNoRemainingLegacyInputOrScrollPath)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Graphics = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(Graphics.empty());
	EXPECT_NE(Graphics.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(Graphics.find("SSettingsCardDefinition"), std::string::npos);
	EXPECT_NE(Graphics.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(Graphics.find("ui_widget::NumericField("), std::string::npos);
	EXPECT_NE(Graphics.find("QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(Graphics.find("CQmScrollState"), std::string::npos);
	EXPECT_NE(Graphics.find("deck:graphics-display"), std::string::npos);
	EXPECT_NE(Graphics.find("deck:graphics-visual"), std::string::npos);
	EXPECT_EQ(Graphics.find("deck:graphics-backend"), std::string::npos);
	EXPECT_NE(Graphics.find("deck:graphics-modes"), std::string::npos);
	EXPECT_EQ(Graphics.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(Graphics.find("DoSliderWithValueInput("), std::string::npos);
	EXPECT_EQ(Graphics.find("Ui()->DoScrollbarH("), std::string::npos);
	EXPECT_EQ(Graphics.find("Ui()->DoValueSelectorWithState("), std::string::npos);
	EXPECT_EQ(Graphics.find("s_GraphicsSettingsScrollRegion"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DisplayModesHideOnlyTheirVisualScrollbar)
{
	const std::string ListBoxHeader = ReadTextFile("src/game/client/ui_listbox.h");
	const std::string ListBoxSource = ReadTextFile("src/game/client/ui_listbox.cpp");
	EXPECT_NE(ListBoxHeader.find("bool m_HideScrollbar;"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("void SetHideScrollbar(bool HideScrollbar)"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("m_HideScrollbar = false;"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("ScrollParams.m_HideScrollbar = m_HideScrollbar;"), std::string::npos);
}
TEST(QmNewUiMenuBranches, NestedLanguageListWheelOwnerOutranksGeneralPage)
{
	EXPECT_TRUE(QmHotScrollRegionPriorityWins(EUiWheelOwnerPriority::PAGE, EUiWheelOwnerPriority::COMPOSITE_CONTROL));
	EXPECT_FALSE(QmHotScrollRegionPriorityWins(EUiWheelOwnerPriority::COMPOSITE_CONTROL, EUiWheelOwnerPriority::PAGE));
	const std::string ScrollRegionSource = ReadTextFile("src/game/client/ui_scrollregion.cpp");
	EXPECT_NE(ScrollRegionSource.find("Ui()->SetHotScrollRegion(this, m_Params.m_WheelOwnerPriority);"), std::string::npos);

	CScrollWheelOwnership Ownership;
	int OuterOwner = 0;
	int InnerOwner = 0;
	ASSERT_TRUE(Ownership.BeginFrame(1, 1.0f, false));
	Ownership.Register(&OuterOwner, EUiWheelOwnerPriority::PAGE, true);
	Ownership.Register(&InnerOwner, EUiWheelOwnerPriority::COMPOSITE_CONTROL, true);
	float WheelDelta = 0.0f;
	EXPECT_FALSE(Ownership.TryConsume(&OuterOwner, &WheelDelta));
	EXPECT_TRUE(Ownership.TryConsume(&InnerOwner, &WheelDelta));
	EXPECT_EQ(WheelDelta, 1.0f);

	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string LanguageSelection = FunctionBody(Source, "bool CMenus::RenderLanguageSelection(CUIRect MainView, const SSettingsContentMetrics *pMetrics)");
	ASSERT_FALSE(LanguageSelection.empty());
	EXPECT_NE(LanguageSelection.find("ScrollParams.m_WheelOwnerPriority = EUiWheelOwnerPriority::COMPOSITE_CONTROL;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ControlsStandardPageUsesUnifiedSettingsStack)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string Header = ReadTextFile("src/game/client/components/menus_settings_controls.h");
	// 搜索页的 tab 路由表已删除：卡片 tab → 标准设置页的落地映射由 SetSettingsPageFromCardTab 统一拥有。
	const std::string Navigation = ReadTextFile("src/game/client/components/menus.cpp");
	EXPECT_NE(Source.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(Source.find("SSettingsCardDefinition"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardOrderModelForRenderPass()"), std::string::npos);
	EXPECT_NE(Source.find("ui_widget::InputField("), std::string::npos);
	EXPECT_NE(Source.find("ui_widget::NumericField("), std::string::npos);
	EXPECT_NE(Source.find("ResolveSettingsControllerAxisPickerHeight("), std::string::npos);
	EXPECT_NE(Source.find("m_SettingsScrollRegion.State()"), std::string::npos);
	EXPECT_NE(Source.find("m_RenderWhenClipped = RenderWhenClipped"), std::string::npos);
	EXPECT_NE(Source.find("QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(Source.find("controls_text_cache"), std::string::npos);
	EXPECT_NE(Source.find("controls_bind_list"), std::string::npos);
	EXPECT_NE(Source.find("DoKeyReader"), std::string::npos);
	EXPECT_EQ(Source.find("RenderSettingsBlock"), std::string::npos);
	EXPECT_EQ(Source.find("BeginSettingsScrollRegion"), std::string::npos);
	EXPECT_EQ(Source.find("FinishSettingsScrollRegion"), std::string::npos);
	EXPECT_EQ(Source.find("DoScrollbarH"), std::string::npos);
	EXPECT_EQ(Source.find("DoValueSelector"), std::string::npos);
	EXPECT_EQ(Header.find("DoSettingsControlsScrollbarOption"), std::string::npos);
	EXPECT_NE(Source.find("deck:controls-mouse"), std::string::npos);
	EXPECT_NE(Source.find("deck:controls-custom"), std::string::npos);
	EXPECT_NE(Navigation.find("str_comp(pTab, \"controls\") == 0)\n\t\tg_Config.m_UiSettingsPage = SETTINGS_CONTROLS;"), std::string::npos);
	const std::string Menus = ReadTextFile("src/game/client/components/menus.cpp");
	EXPECT_NE(Menus.find("str_comp(pTab, \"controls\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ControlsControllerCardUsesDynamicHeightPreLayout)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings_controls.cpp");
	const size_t CardStart = Source.find("AddCard(vCards, \"deck:controls-controller\"");
	ASSERT_NE(CardStart, std::string::npos);
	const size_t CardEnd = Source.find("const std::pair<EBindOptionGroup", CardStart);
	ASSERT_NE(CardEnd, std::string::npos);
	const std::string CardBody = Source.substr(CardStart, CardEnd - CardStart);
	EXPECT_NE(CardBody.find("ControllerMeasureRevision"), std::string::npos);
	EXPECT_NE(CardBody.find("m_PreLayoutInput"), std::string::npos);
	EXPECT_NE(CardBody.find("const bool WasJoystickEnabled"), std::string::npos);
	EXPECT_NE(CardBody.find("ResolveSettingsRadioRowLayout(Content, 2, Metrics)"), std::string::npos);
	EXPECT_NE(CardBody.find("m_vJoystickIngameModeButtonContainers"), std::string::npos);
	EXPECT_NE(CardBody.find("if(!WasAbsolute)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ShutdownReleasesUiResourcesBeforeRendererProviders)
{
	const std::string NameplatesSource = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string NameplatesHeader = ReadTextFile("src/game/client/components/nameplates.h");
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string UiHeader = ReadTextFile("src/game/client/ui.h");
	const std::string GameClientSource = ReadTextFile("src/game/client/gameclient.cpp");

	const std::string NameplatesShutdown = FunctionBody(NameplatesSource, "void CNamePlates::OnShutdown()");
	const std::string NameplatesDestructor = FunctionBody(NameplatesSource, "CNamePlates::~CNamePlates()");
	ASSERT_FALSE(NameplatesShutdown.empty());
	ASSERT_FALSE(NameplatesDestructor.empty());
	EXPECT_NE(NameplatesHeader.find("void OnShutdown() override;"), std::string::npos);
	EXPECT_NE(NameplatesShutdown.find("ResetNamePlates();"), std::string::npos);
	EXPECT_NE(NameplatesShutdown.find("ResetChatBubbleAnimState(i, true);"), std::string::npos);
	EXPECT_EQ(NameplatesDestructor.find("ResetNamePlates"), std::string::npos);
	EXPECT_EQ(NameplatesDestructor.find("TextRender"), std::string::npos);

	const std::string UiShutdown = FunctionBody(UiSource, "void CUi::OnShutdown()");
	const std::string UiDestructor = FunctionBody(UiSource, "CUi::~CUi()");
	ASSERT_FALSE(UiShutdown.empty());
	ASSERT_FALSE(UiDestructor.empty());
	EXPECT_NE(UiHeader.find("void OnShutdown();"), std::string::npos);
	EXPECT_NE(UiShutdown.find("OnElementsReset();"), std::string::npos);
	EXPECT_NE(UiShutdown.find("if(m_pGraphics == nullptr || m_pTextRender == nullptr)"), std::string::npos);
	EXPECT_EQ(UiDestructor.find("Graphics()"), std::string::npos);
	EXPECT_EQ(UiDestructor.find("TextRender()"), std::string::npos);

	const std::string GameClientShutdown = FunctionBody(GameClientSource, "void CGameClient::OnShutdown()");
	ASSERT_FALSE(GameClientShutdown.empty());
	const size_t ComponentsShutdown = GameClientShutdown.find("pComponent->OnShutdown();");
	const size_t UiShutdownCall = GameClientShutdown.find("m_UI.OnShutdown();");
	ASSERT_NE(ComponentsShutdown, std::string::npos);
	ASSERT_NE(UiShutdownCall, std::string::npos);
	EXPECT_LT(ComponentsShutdown, UiShutdownCall);
}

TEST(QmNewUiMenuBranches, ValueSelectorUsesOneFittedTextLayoutForDisplayAndEditing)
{
	EXPECT_FLOAT_EQ(QmFitSingleLineFontSize(10.0f, 6.0f, 40.0f, 80.0f), 10.0f);
	EXPECT_FLOAT_EQ(QmFitSingleLineFontSize(10.0f, 6.0f, 100.0f, 80.0f), 8.0f);
	EXPECT_FLOAT_EQ(QmFitSingleLineFontSize(10.0f, 6.0f, 200.0f, 80.0f), 6.0f);

	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string Selector = FunctionBody(UiSource, "SEditResult<int64_t> CUi::DoValueSelectorWithState");
	ASSERT_FALSE(Selector.empty());
	EXPECT_NE(Selector.find("QmFitSingleLineFontSize("), std::string::npos);
	EXPECT_NE(Selector.find("pRect->VMargin(2.0f, &Textbox);"), std::string::npos);
	EXPECT_NE(Selector.find("DoLabel(&Textbox, pDisplayText, ValueFontSize"), std::string::npos);
	EXPECT_NE(Selector.find("m_ActiveValueSelectorState.m_NumberInput.Render(&Textbox, EditFontSize, Props.m_TextAlign"), std::string::npos);
	EXPECT_NE(Selector.find("auto RenderValueSelectorDisplay = [&](bool RenderText = true)"), std::string::npos);
	EXPECT_NE(Selector.find("RenderValueSelectorDisplay(false);"), std::string::npos);
	EXPECT_EQ(Selector.find("TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));"), std::string::npos);
	EXPECT_NE(Selector.find("const ColorRGBA PreviousTextColor = TextRender()->GetTextColor();"), std::string::npos);
	EXPECT_NE(Selector.find("TextRender()->TextColor(PreviousTextColor);"), std::string::npos);
	EXPECT_EQ(Selector.find("m_NumberInput.Render(pRect, 10.0f"), std::string::npos);
	const std::string EditorSource = ReadTestSourceFile("src/game/editor/editor_ui.cpp");
	const std::string EditorSelector = FunctionBody(EditorSource, "SEditResult<int> CEditor::UiDoValueSelector");
	ASSERT_FALSE(EditorSelector.empty());
	EXPECT_NE(EditorSelector.find("DoEditBox(&s_NumberInput, pRect, 10.0f, Corners);"), std::string::npos);
	EXPECT_NE(EditorSelector.find("pRect->VMargin(2.0f, &Textbox);"), std::string::npos);
	EXPECT_NE(EditorSelector.find("Ui()->DoLabel(&Textbox, aBuf, 10, TEXTALIGN_MC);"), std::string::npos);
	const size_t EditingBranch = Selector.find("if(m_ActiveValueSelectorState.m_pLastTextId == pId)");
	const size_t DisplayBeforeOverlay = Selector.find("RenderValueSelectorDisplay(false);", EditingBranch);
	const size_t InputOverlay = Selector.find("m_ActiveValueSelectorState.m_NumberInput.Render(", EditingBranch);
	ASSERT_NE(DisplayBeforeOverlay, std::string::npos);
	ASSERT_NE(InputOverlay, std::string::npos);
	const size_t RestoreAfterInput = Selector.find("TextRender()->TextColor(PreviousTextColor);", InputOverlay);
	ASSERT_NE(RestoreAfterInput, std::string::npos);
	EXPECT_LT(DisplayBeforeOverlay, InputOverlay);
	EXPECT_LT(InputOverlay, RestoreAfterInput);
	const size_t FormatLambda = Selector.find("auto RenderValueSelectorDisplay = [&](bool RenderText = true)");
	const size_t FormatCurrent = Selector.find("Props.m_pfnFormatValue(Current", FormatLambda);
	const size_t ScrollUpdate = Selector.find("Current += Props.m_Step * Count;");
	const size_t FinalDisplay = Selector.rfind("RenderValueSelectorDisplay();");
	ASSERT_NE(FormatLambda, std::string::npos);
	ASSERT_NE(FormatCurrent, std::string::npos);
	ASSERT_NE(ScrollUpdate, std::string::npos);
	ASSERT_NE(FinalDisplay, std::string::npos);
	EXPECT_GT(FormatCurrent, FormatLambda);
	EXPECT_LT(ScrollUpdate, FinalDisplay);
}

TEST(QmNewUiMenuBranches, AudioPackRefreshUsesPhosphorFontIconButton)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Sound = FunctionBody(Source, "void CMenus::RenderSettingsSound(CUIRect MainView)");
	ASSERT_FALSE(Sound.empty());
	EXPECT_NE(Sound.find("Ui()->DoButton_FontIcon(&s_AudioPackRefreshButton, FONT_ICON_ARROW_ROTATE_RIGHT"), std::string::npos);
	EXPECT_EQ(Sound.find("DoButton_Menu(&s_AudioPackRefreshButton, FONT_ICON_ARROW_ROTATE_RIGHT"), std::string::npos);
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string FontIconButton = FunctionBody(UiSource, "int CUi::DoButton_FontIcon");
	EXPECT_NE(FontIconButton.find("ConfiguredQmUiIconColor(TextRender()->DefaultTextColor())"), std::string::npos);
	EXPECT_NE(FontIconButton.find("QmIconWeightUsesBoldFontFallback(g_Config.m_QmUiIconWeight)"), std::string::npos);
	EXPECT_NE(FontIconButton.find("SetRenderFlags(PreviousFlags)"), std::string::npos);
	EXPECT_NE(FontIconButton.find("TextColor(PreviousColor)"), std::string::npos);
	const std::string TextSource = ReadTextFile("src/engine/client/text.cpp");
	EXPECT_NE(TextSource.find("m_IconBoldFace = m_IconRegularFace;"), std::string::npos);
	EXPECT_NE(TextSource.find("falling back to regular"), std::string::npos);
}

TEST(QmNewUiMenuBranches, GraphicsIconCardSupportsDynamicCustomColorAndFourWeights)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Graphics = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(Graphics.empty());
	EXPECT_NE(Graphics.find("s_aGraphicsIconColorButtons[4]"), std::string::npos);
	EXPECT_NE(Graphics.find("s_aGraphicsIconWeightButtons[4]"), std::string::npos);
	EXPECT_NE(Graphics.find("Localize(\"Custom\")"), std::string::npos);
	EXPECT_NE(Graphics.find("Localize(\"Rainbow\")"), std::string::npos);
	EXPECT_NE(Graphics.find("Localize(\"Thin\")"), std::string::npos);
	EXPECT_NE(Graphics.find("Localize(\"Fill\")"), std::string::npos);
	EXPECT_NE(Graphics.find("static constexpr int s_aIconWeightValues[] = {2, 0, 1, 3};"), std::string::npos);
	EXPECT_NE(Graphics.find("DoLine_ColorPicker(&s_GraphicsIconCustomColorResetId"), std::string::npos);
	EXPECT_NE(Graphics.find("vCards.back().m_MeasureRevision = static_cast<uint64_t>(g_Config.m_QmUiIconColor == 3);"), std::string::npos);
	EXPECT_NE(Graphics.find("vCards.back().m_PreLayoutInput = [this, GraphicsMetrics]"), std::string::npos);
	EXPECT_NE(Graphics.find("return ResolveSettingsContentFlowHeight(GraphicsMetrics, g_Config.m_QmUiIconColor == 3"), std::string::npos);
	EXPECT_NE(Graphics.find("std::initializer_list<float>{GraphicsMetrics.m_LineHeight, GraphicsMetrics.m_ButtonHeight, GraphicsMetrics.m_LineHeight}"), std::string::npos);

	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	EXPECT_NE(Config.find("MACRO_CONFIG_COL(QmUiIconCustomColor, qm_ui_icon_custom_color"), std::string::npos);
	EXPECT_NE(Config.find("Qm UI icon color: 1=White, 2=Black, 3=Custom, 4=Rainbow"), std::string::npos);
	EXPECT_NE(Config.find("Qm UI icon weight: 0=Regular, 1=Bold, 2=Thin, 3=Fill"), std::string::npos);
}

TEST(QmNewUiMenuBranches, RoundedUiSurfacesUseClampedGeometryAndSharedPaths)
{
	const SRoundedRectGeometry Geometry = ResolveRoundedRectGeometry(0.24f, 0.74f, 10.32f, 4.19f, 3.9f, 0.5f);
	EXPECT_NEAR(Geometry.m_X, 0.0f, 1e-6f);
	EXPECT_NEAR(Geometry.m_Y, 0.5f, 1e-6f);
	EXPECT_NEAR(Geometry.m_W, 10.5f, 1e-6f);
	EXPECT_NEAR(Geometry.m_H, 4.5f, 1e-6f);
	EXPECT_NEAR(Geometry.m_Rounding, 2.25f, 1e-6f);
	const SRoundedRectGeometry SmallGeometry = ResolveRoundedRectGeometry(0.24f, 0.24f, 0.51f, 0.51f, 0.4f, 0.5f);
	EXPECT_NEAR(SmallGeometry.m_W, 1.0f, 1e-6f);
	EXPECT_NEAR(SmallGeometry.m_H, 1.0f, 1e-6f);
	EXPECT_NEAR(SmallGeometry.m_Rounding, 0.5f, 1e-6f);
	const SRoundedRectGeometry InvalidGeometry = ResolveRoundedRectGeometry(0.5f, 0.5f, 0.0f, 4.0f, 3.0f, 0.5f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_X, 0.5f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_Y, 0.5f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_W, 0.0f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_H, 4.0f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_Rounding, 0.0f);

	const CUIRect Rect{0.2f, 0.2f, 20.0f, 10.0f};
	SRoundedSurfaceParams SdfParams;
	SdfParams.m_Radius = 8.0f;
	SdfParams.m_BorderWidth = 0.6f;
	SdfParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan Sdf = ResolveRoundedSurfacePlan(Rect, SdfParams, true);
	EXPECT_TRUE(Sdf.m_UseSdf);
	EXPECT_FLOAT_EQ(Sdf.m_Rect.x, 0.0f);
	EXPECT_FLOAT_EQ(Sdf.m_Rect.y, 0.0f);
	EXPECT_FLOAT_EQ(Sdf.m_Rect.w, 20.0f);
	EXPECT_FLOAT_EQ(Sdf.m_Rect.h, 10.0f);
	EXPECT_FLOAT_EQ(Sdf.m_Radius, 5.0f);
	EXPECT_FLOAT_EQ(Sdf.m_BorderWidth, 0.5f);
	EXPECT_FLOAT_EQ(Sdf.m_PixelSize, 0.5f);
	EXPECT_FLOAT_EQ(Sdf.m_CornerRadii.x, 5.0f);
	EXPECT_FLOAT_EQ(Sdf.m_CornerRadii.y, 5.0f);
	EXPECT_FLOAT_EQ(Sdf.m_CornerRadii.z, 5.0f);
	EXPECT_FLOAT_EQ(Sdf.m_CornerRadii.w, 5.0f);
	SRoundedSurfaceParams NonIntegerPixelParams;
	NonIntegerPixelParams.m_Radius = 3.9f;
	NonIntegerPixelParams.m_BorderWidth = 0.6f;
	NonIntegerPixelParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan NonIntegerPixelPlan = ResolveRoundedSurfacePlan(CUIRect{0.24f, 0.74f, 10.32f, 4.19f}, NonIntegerPixelParams, true);
	EXPECT_NEAR(NonIntegerPixelPlan.m_Rect.x, 0.0f, 1e-6f);
	EXPECT_NEAR(NonIntegerPixelPlan.m_Rect.y, 0.5f, 1e-6f);
	EXPECT_NEAR(NonIntegerPixelPlan.m_Rect.w, 10.5f, 1e-6f);
	EXPECT_NEAR(NonIntegerPixelPlan.m_Rect.h, 4.5f, 1e-6f);
	SRoundedSurfaceParams OnePhysicalPixelParams;
	OnePhysicalPixelParams.m_Radius = 0.4f;
	OnePhysicalPixelParams.m_BorderWidth = 0.4f;
	OnePhysicalPixelParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan OnePhysicalPixelPlan = ResolveRoundedSurfacePlan(CUIRect{0.24f, 0.24f, 0.51f, 0.51f}, OnePhysicalPixelParams, true);
	EXPECT_NEAR(OnePhysicalPixelPlan.m_Rect.w, 1.0f, 1e-6f);
	EXPECT_NEAR(OnePhysicalPixelPlan.m_Rect.h, 1.0f, 1e-6f);
	EXPECT_NEAR(OnePhysicalPixelPlan.m_Radius, 0.5f, 1e-6f);
	EXPECT_NEAR(OnePhysicalPixelPlan.m_BorderWidth, 0.5f, 1e-6f);
	const auto ExpectCornerRadii = [](const SRoundedSurfacePlan &Plan, const float Tl, const float Tr, const float Br, const float Bl) {
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.x, Tl);
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.y, Tr);
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.z, Br);
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.w, Bl);
	};

	SRoundedSurfaceParams PartialParams;
	PartialParams.m_Radius = 4.0f;
	PartialParams.m_BorderWidth = 1.0f;
	PartialParams.m_PixelSize = 0.5f;
	PartialParams.m_Corners = IGraphics::CORNER_R;
	const SRoundedSurfacePlan Partial = ResolveRoundedSurfacePlan(Rect, PartialParams, true);
	EXPECT_TRUE(Partial.m_UseSdf);
	ExpectCornerRadii(Partial, 0.0f, 4.0f, 4.0f, 0.0f);
	SRoundedSurfaceParams LeftParams;
	LeftParams.m_Radius = 4.0f;
	LeftParams.m_BorderWidth = 1.0f;
	LeftParams.m_PixelSize = 0.5f;
	LeftParams.m_Corners = IGraphics::CORNER_L;
	const SRoundedSurfacePlan Left = ResolveRoundedSurfacePlan(Rect, LeftParams, true);
	EXPECT_TRUE(Left.m_UseSdf);
	ExpectCornerRadii(Left, 4.0f, 0.0f, 0.0f, 4.0f);
	const auto ExpectMask = [&](const int Corners, const float Tl, const float Tr, const float Br, const float Bl) {
		SRoundedSurfaceParams Params;
		Params.m_Radius = 4.0f;
		Params.m_BorderWidth = 1.0f;
		Params.m_PixelSize = 0.5f;
		Params.m_Corners = Corners;
		const SRoundedSurfacePlan Plan = ResolveRoundedSurfacePlan(Rect, Params, true);
		EXPECT_TRUE(Plan.m_UseSdf);
		ExpectCornerRadii(Plan, Tl, Tr, Br, Bl);
	};
	ExpectMask(IGraphics::CORNER_T, 4.0f, 4.0f, 0.0f, 0.0f);
	ExpectMask(IGraphics::CORNER_B, 0.0f, 0.0f, 4.0f, 4.0f);
	ExpectMask(IGraphics::CORNER_TL, 4.0f, 0.0f, 0.0f, 0.0f);
	ExpectMask(IGraphics::CORNER_TR, 0.0f, 4.0f, 0.0f, 0.0f);
	ExpectMask(IGraphics::CORNER_BR, 0.0f, 0.0f, 4.0f, 0.0f);
	ExpectMask(IGraphics::CORNER_BL, 0.0f, 0.0f, 0.0f, 4.0f);
	ExpectMask(IGraphics::CORNER_NONE, 0.0f, 0.0f, 0.0f, 0.0f);
	SRoundedSurfaceParams WideBorderParams;
	WideBorderParams.m_Radius = 2.0f;
	WideBorderParams.m_BorderWidth = 4.0f;
	WideBorderParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan WideBorder = ResolveRoundedSurfacePlan(Rect, WideBorderParams, true);
	EXPECT_FLOAT_EQ(WideBorder.m_Radius, 2.0f);
	EXPECT_FLOAT_EQ(WideBorder.m_BorderWidth, 4.0f);
	SRoundedSurfaceParams SwallowedInteriorParams;
	SwallowedInteriorParams.m_Radius = 8.0f;
	SwallowedInteriorParams.m_BorderWidth = 9.0f;
	SwallowedInteriorParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan SwallowedInterior = ResolveRoundedSurfacePlan(CUIRect{0.0f, 0.0f, 6.0f, 4.0f}, SwallowedInteriorParams, true);
	EXPECT_FLOAT_EQ(SwallowedInterior.m_Radius, 2.0f);
	EXPECT_FLOAT_EQ(SwallowedInterior.m_BorderWidth, 2.0f);
	SRoundedSurfaceParams UnsupportedParams;
	UnsupportedParams.m_Radius = 4.0f;
	UnsupportedParams.m_BorderWidth = 1.0f;
	UnsupportedParams.m_PixelSize = 0.0f;
	const SRoundedSurfacePlan Unsupported = ResolveRoundedSurfacePlan(Rect, UnsupportedParams, false);
	EXPECT_FALSE(Unsupported.m_UseSdf);
	EXPECT_FLOAT_EQ(Unsupported.m_PixelSize, 0.0001f);

	const std::string Buttons = ReadTextFile("src/game/client/QmUi/UiButtons.cpp");
	const std::string Forms = ReadTextFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Surface = ReadTextFile("src/game/client/QmUi/UiSurface.cpp");
	const std::string SurfaceHeader = ReadTextFile("src/game/client/QmUi/UiSurface.h");
	const std::string UiRect = ReadTextFile("src/game/client/ui_rect.cpp");
	const std::string Containers = ReadTextFile("src/game/client/QmUi/UiContainers.h");
	const std::string Overlays = ReadTextFile("src/game/client/QmUi/UiOverlays.h");
	const std::string Ui = ReadTextFile("src/game/client/ui.cpp");
	const std::string Menus = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string IngameMenus = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const std::string QmClientMenus = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string TClientMenus = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string ScrollRegion = ReadTextFile("src/game/client/ui_scrollregion.cpp");
	const std::string ImePopup = ReadTextFile("src/game/client/qm_ime_candidate_popup.cpp");
	const std::string Editor = ReadTextFile("src/game/editor/editor_ui.cpp");
	EXPECT_NE(Buttons.find("DrawRoundedSurface("), std::string::npos);
	EXPECT_NE(Forms.find("DrawRoundedSurface("), std::string::npos);
	EXPECT_NE(SurfaceHeader.find("vec4 m_CornerRadii{};"), std::string::npos);
	EXPECT_NE(SurfaceHeader.find("struct SRoundedSurfaceParams"), std::string::npos);
	EXPECT_NE(SurfaceHeader.find("const SRoundedSurfaceParams &Params"), std::string::npos);
	EXPECT_EQ(SurfaceHeader.find("float PixelSize, int Corners"), std::string::npos);
	EXPECT_NE(SurfaceHeader.find("ResolveRoundedSurfaceCornerRadii"), std::string::npos);
	EXPECT_NE(SurfaceHeader.find("Plan.m_UseSdf = HasSdf"), std::string::npos);
	EXPECT_NE(Surface.find("Params.m_CornerRadii = Plan.m_CornerRadii;"), std::string::npos);
	EXPECT_NE(Surface.find("Params.m_Params = vec4(Plan.m_BorderWidth, Plan.m_PixelSize, Plan.m_PixelSize * 2.0f, 0.0f);"), std::string::npos);
	EXPECT_NE(UiRect.find("DrawRoundedSurface(ms_pGraphics, *this"), std::string::npos);
	EXPECT_NE(UiRect.find("const float PixelSize = CurrentPixelSize(ms_pGraphics);"), std::string::npos);
	EXPECT_NE(UiRect.find("SRoundedSurfaceParams Params;"), std::string::npos);
	EXPECT_NE(UiRect.find("Params.m_PixelSize = PixelSize;"), std::string::npos);
	EXPECT_NE(UiRect.find("DrawRoundedSurface(ms_pGraphics, *this, Color, ColorRGBA(), Params)"), std::string::npos);
	EXPECT_EQ(UiRect.find("Rounding, 0.0f, PixelSize, Corners"), std::string::npos);
	EXPECT_EQ(UiRect.find("ResolveRoundedRectGeometry(x, y, w, h, Rounding"), std::string::npos);
	EXPECT_NE(Ui.find("DrawRoundedSurface(this, ClearButton"), std::string::npos);
	EXPECT_NE(FunctionBody(Ui, "bool CUi::DoEditBox(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, int Corners, const std::vector<STextColorSplit> &vColorSplits, int Align, const SEditBoxRenderOptions &RenderOptions)").find("DrawRoundedSurface(this, *pRect"), std::string::npos);
	EXPECT_NE(FunctionBody(Ui, "SEditResult<int64_t> CUi::DoValueSelectorWithState").find("DrawRoundedSurface(this, *pRect"), std::string::npos);
	EXPECT_NE(FunctionBody(Ui, "int CUi::DoButton_FontIcon").find("DrawRoundedSurface(this, *pRect"), std::string::npos);
	EXPECT_NE(FunctionBody(Ui, "void CUi::RenderPopupMenus").find("SPopupMenu::POPUP_BORDER"), std::string::npos);
	EXPECT_NE(FunctionBody(Ui, "float CUi::DoScrollbarV").find("DrawRoundedSurface(this, Rail"), std::string::npos);
	EXPECT_NE(FunctionBody(Ui, "void CUi::RenderProgressBar").find("DrawRoundedSurface(this, ProgressBar"), std::string::npos);
	EXPECT_NE(IngameMenus.find("#include <game/client/QmUi/UiSurface.h>"), std::string::npos);
	EXPECT_NE(FunctionBody(IngameMenus, "void CMenus::RenderServerControl(CUIRect MainView)").find("DrawRoundedSurface(Ui(), MainView, ms_ColorTabbarActive, ms_ColorTabbarActive, 10.0f, 0.0f, IGraphics::CORNER_B);"), std::string::npos);
	const std::string ColorPicker = FunctionBody(Ui, "CUi::EPopupMenuFunctionResult CUi::PopupColorPicker");
	EXPECT_NE(ColorPicker.find("const CUIRect ColorMarker{MarkerX - 4.5f, MarkerY - 4.5f, 9.0f, 9.0f};"), std::string::npos);
	EXPECT_NE(ColorPicker.find("DrawRoundedSurface(pUI, ColorMarker, PickerColorRGB, MarkerOutline, 4.5f, 1.0f);"), std::string::npos);
	EXPECT_EQ(ColorPicker.find("DrawCircle(MarkerX"), std::string::npos);
	EXPECT_NE(ColorPicker.find("DrawRoundedSurface(pUI, HueMarker, HueMarkerColor, HueMarkerOutline, 1.2f, 1.2f);"), std::string::npos);
	EXPECT_EQ(ColorPicker.find("HueMarker.Draw("), std::string::npos);
	EXPECT_NE(FunctionBody(Ui, "int CUi::DoButton_Menu").find("const bool UseRoundedRectSdf = Graphics()->HasRoundedRectSdf();"), std::string::npos);
	EXPECT_NE(FunctionBody(Ui, "int CUi::DoButton_Menu").find("if(!UseRoundedRectSdf)"), std::string::npos);
	EXPECT_NE(Menus.find("DrawRoundedSurface(Ui(), *pRect"), std::string::npos);
	EXPECT_NE(Containers.find("DrawRoundedSurface(Ctx, Shadow"), std::string::npos);
	EXPECT_LT(Containers.find("DrawRoundedSurface(Ctx, BorderBg"), Containers.find("DrawRoundedSurface(Ctx, Rect, Props.m_FillColor"));
	EXPECT_NE(Containers.find("BorderBg.Margin(-1.0f, &BorderBg);"), std::string::npos);
	EXPECT_NE(Containers.find("DrawRoundedSurface(Ctx, Rect, Props.m_FillColor"), std::string::npos);
	EXPECT_EQ(Containers.find("BorderBg.Draw"), std::string::npos);
	EXPECT_NE(Overlays.find("DrawRoundedSurface(Ctx, ShadowRect"), std::string::npos);
	EXPECT_NE(Overlays.find("DrawRoundedSurface(Ctx, ToastRect"), std::string::npos);
	EXPECT_NE(FunctionBody(ScrollRegion, "void CScrollRegion::DrawBackground(const CUIRect &ScrollbarBg)").find("DrawRoundedSurface(Ui(), ScrollbarBg"), std::string::npos);
	EXPECT_NE(FunctionBody(ScrollRegion, "void CScrollRegion::DoSlider()").find("DrawRoundedSurface(Ui(), Slider"), std::string::npos);
	EXPECT_NE(QmClientMenus.find("DrawRoundedSurface(Ui(), Frame.m_Frame.m_ScrollbarTrackRect"), std::string::npos);
	EXPECT_NE(QmClientMenus.find("DrawRoundedSurface(Ui(), QrRect"), std::string::npos);
	EXPECT_NE(QmClientMenus.find("DrawRoundedSurface(Ui(), Preview, PreviewBg"), std::string::npos);
	EXPECT_NE(TClientMenus.find("DrawRoundedSurface(Ui(), PlayerRect, NameButtonColor"), std::string::npos);
	EXPECT_NE(TClientMenus.find("DrawRoundedSurface(Ui(), ClanRect, ClanButtonColor"), std::string::npos);
	EXPECT_NE(TClientMenus.find("if(!ReadOnly && NameButtonColor.a > 0.0f)"), std::string::npos);
	EXPECT_NE(TClientMenus.find("if(!ReadOnly && ClanButtonColor.a > 0.0f)"), std::string::npos);
	EXPECT_NE(TClientMenus.find("DrawRoundedSurface(Ui(), PreviewRect"), std::string::npos);
	EXPECT_NE(TClientMenus.find("DrawRoundedSurface(Ui(), StatusBar"), std::string::npos);
	EXPECT_NE(TClientMenus.find("DrawRoundedSurface(Ui(), Skin"), std::string::npos);
	EXPECT_NE(ImePopup.find("DrawRoundedSurface(pGraphics, PanelDropA"), std::string::npos);
	EXPECT_NE(ImePopup.find("DrawRoundedSurface(pGraphics, DrawRect"), std::string::npos);
	EXPECT_NE(ImePopup.find("SurfaceParams.m_PixelSize = PixelSize;"), std::string::npos);
	EXPECT_NE(ImePopup.find("PanelTopLine.x += Presentation.m_Radius"), std::string::npos);
	EXPECT_NE(ImePopup.find("PanelTopLine.w = maximum(0.0f"), std::string::npos);
	EXPECT_NE(Editor.find("DrawRoundedSurface(Ui(), *pRect"), std::string::npos);
	EXPECT_NE(FunctionBody(Editor, "SEditResult<int> CEditor::UiDoValueSelector").find("DrawRoundedSurface(Ui(), *pRect"), std::string::npos);
	EXPECT_EQ(Surface.find("DrawFallbackBorderRing"), std::string::npos);
	EXPECT_EQ(Surface.find("DrawRoundedRectAntialias"), std::string::npos);
	EXPECT_NE(Surface.find("pGraphics->DrawRect(Plan.m_Rect.x, Plan.m_Rect.y, Plan.m_Rect.w, Plan.m_Rect.h, Fill, Params.m_Corners, Plan.m_Radius);"), std::string::npos);
	EXPECT_NE(Surface.find("Inner.Margin(Plan.m_BorderWidth, &Inner);"), std::string::npos);
	EXPECT_NE(Surface.find("pGraphics->DrawRect(Inner.x, Inner.y, Inner.w, Inner.h, Fill, Params.m_Corners"), std::string::npos);
	EXPECT_EQ(Surface.find("QuadsDrawFreeform"), std::string::npos);
	EXPECT_EQ(Surface.find("pUi->ClipEnable(&Clip);"), std::string::npos);
	const std::string Graphics = ReadTextFile("src/engine/client/graphics_threaded.cpp");
	const std::string DrawRect = FunctionBody(Graphics, "void CGraphics_Threaded::DrawRect(float x, float y, float w, float h, ColorRGBA Color, int Corners, float Rounding)");
	const std::string DrawRectExtAntialias = FunctionBody(Graphics, "void CGraphics_Threaded::DrawRectExtAntialias(float x, float y, float w, float h, float r, int Corners, ColorRGBA Color, bool ResolveGeometry)");
	const std::string DrawRectExt = FunctionBody(Graphics, "void CGraphics_Threaded::DrawRectExt(float x, float y, float w, float h, float r, int Corners)");
	const std::string DrawRectExt4Antialias = FunctionBody(Graphics, "void CGraphics_Threaded::DrawRectExt4Antialias(float x, float y, float w, float h, float r, int Corners, ColorRGBA ColorTopLeft, ColorRGBA ColorTopRight, ColorRGBA ColorBottomLeft, ColorRGBA ColorBottomRight, bool ResolveGeometry)");
	const std::string DrawRectExt4 = FunctionBody(Graphics, "void CGraphics_Threaded::DrawRectExt4(float x, float y, float w, float h, ColorRGBA ColorTopLeft, ColorRGBA ColorTopRight, ColorRGBA ColorBottomLeft, ColorRGBA ColorBottomRight, float r, int Corners)");
	EXPECT_NE(DrawRect.find("DrawRectExt(x, y, w, h, Rounding, Corners);"), std::string::npos);
	EXPECT_NE(Graphics.find("#include <engine/client/rounded_rect_geometry.h>"), std::string::npos);
	EXPECT_NE(DrawRectExtAntialias.find("ResolveRoundedRectGeometry(x, y, w, h, r"), std::string::npos);
	EXPECT_NE(DrawRectExt.find("ResolveRoundedRectGeometry(x, y, w, h, r"), std::string::npos);
	EXPECT_NE(DrawRectExt4Antialias.find("ResolveRoundedRectGeometry(x, y, w, h, r"), std::string::npos);
	EXPECT_NE(DrawRectExt4.find("ResolveRoundedRectGeometry(x, y, w, h, r"), std::string::npos);
	EXPECT_NE(DrawRectExt.find("DrawRectExtAntialias(x, y, w, h, r, Corners, CommandColorToColorRGBA(m_aColor[0]), false);"), std::string::npos);
	EXPECT_NE(DrawRectExt4.find("DrawRectExt4Antialias(x, y, w, h, r, Corners, ColorTopLeft, ColorTopRight, ColorBottomLeft, ColorBottomRight, false);"), std::string::npos);
	EXPECT_NE(FunctionBody(Graphics, "int CGraphics_Threaded::CreateRectQuadContainer(float x, float y, float w, float h, float r, int Corners)").find("ResolveRoundedRectGeometry(x, y, w, h, r"), std::string::npos);
	for(const char *pShaderPath : {"data/shader/rounded_rect_sdf.frag", "data/shader/vulkan/rounded_rect_sdf.frag"})
	{
		const std::string Shader = ReadTextFile(pShaderPath);
		EXPECT_NE(Shader.find("gRoundedRectSdfData[5]"), std::string::npos);
		EXPECT_NE(Shader.find("float CornerRadius(vec2 Point, vec4 CornerRadii)"), std::string::npos);
		EXPECT_NE(Shader.find("float SdfFeather(float DistanceValue, float PixelSize)"), std::string::npos);
		EXPECT_NE(Shader.find("return max(PixelSize, length(vec2(dFdx(DistanceValue), dFdy(DistanceValue))));"), std::string::npos);
		EXPECT_NE(Shader.find("return 1.0 - smoothstep(-Feather * 0.5, Feather * 0.5, DistanceValue);"), std::string::npos);
		EXPECT_NE(Shader.find("vec4 InnerCornerRadii = max(CornerRadii - vec4(BorderWidth), vec4(0.0));"), std::string::npos);
		EXPECT_NE(Shader.find("float BorderCoverage = max(OuterCoverage - InnerCoverage, 0.0);"), std::string::npos);
		EXPECT_NE(Shader.find("float OuterCoverage = Coverage(OuterDistance, Params.y);"), std::string::npos);
		EXPECT_NE(Shader.find("InnerCoverage = BorderWidth > 0.0 && min(InnerHalfSize.x, InnerHalfSize.y) > 0.0 ? Coverage(InnerDistance, Params.y) : 0.0;"), std::string::npos);
		EXPECT_EQ(Shader.find("* 0.8"), std::string::npos);
		EXPECT_EQ(Shader.find("* 0.9"), std::string::npos);
		EXPECT_NE(Shader.find("Rect.zw + vec2(Params.z * 2.0)"), std::string::npos);
		EXPECT_NE(Shader.find("float OutputAlpha = FillAlpha + BorderAlpha;"), std::string::npos);
		EXPECT_NE(Shader.find("vec3 Premultiplied = FillColor.rgb * FillAlpha + BorderColor.rgb * BorderAlpha;"), std::string::npos);
		EXPECT_EQ(Shader.find("BorderAlpha * (1.0 - FillAlpha)"), std::string::npos);
		EXPECT_EQ(Shader.find("mix(BorderColor, FillColor, InnerCoverage)"), std::string::npos);
	}
	const auto NormalizeSdfCore = [](std::string Shader) {
		const size_t CoreStart = Shader.find("float RoundedRectSdf");
		if(CoreStart == std::string::npos)
			return std::string{};
		Shader.erase(0, CoreStart);
		const std::string VulkanDataPrefix = "gSdf.gRoundedRectSdfData";
		const std::string OpenGlDataPrefix = "gRoundedRectSdfData";
		size_t Position = 0;
		while((Position = Shader.find(VulkanDataPrefix, Position)) != std::string::npos)
		{
			Shader.replace(Position, VulkanDataPrefix.size(), OpenGlDataPrefix);
			Position += OpenGlDataPrefix.size();
		}
		return Shader;
	};
	const std::string OpenGlSdfShader = ReadTextFile("data/shader/rounded_rect_sdf.frag");
	const std::string VulkanSdfShader = ReadTextFile("data/shader/vulkan/rounded_rect_sdf.frag");
	EXPECT_EQ(NormalizeSdfCore(OpenGlSdfShader), NormalizeSdfCore(VulkanSdfShader));
	const std::string GraphicsHeader = ReadTextFile("src/engine/graphics.h");
	const std::string GraphicsThreaded = ReadTextFile("src/engine/client/graphics_threaded.cpp");
	const std::string OpenGl = ReadTextFile("src/engine/client/backend/opengl/backend_opengl3.cpp");
	const std::string Vulkan = ReadTextFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	EXPECT_NE(GraphicsHeader.find("static_assert(sizeof(SRoundedRectSdfParams) == sizeof(vec4) * 5);"), std::string::npos);
	EXPECT_NE(OpenGl.find("SetUniformVec4(m_pRoundedRectSdfProgram->m_LocData, 5"), std::string::npos);
	EXPECT_NE(FunctionBody(Vulkan, "[[nodiscard]] bool Cmd_RenderRoundedRectSdf").find("&pCommand->m_Params, sizeof(pCommand->m_Params)"), std::string::npos);
	const std::string RoundedCommand = FunctionBody(GraphicsThreaded, "void CGraphics_Threaded::RenderRoundedRectSdf");
	EXPECT_NE(RoundedCommand.find("Params.m_Params.z"), std::string::npos);
	EXPECT_NE(RoundedCommand.find("if(m_NumVertices > 0)"), std::string::npos);
	EXPECT_NE(RoundedCommand.find("FlushVertices();"), std::string::npos);
	EXPECT_NE(RoundedCommand.find("m_RoundedRectSdfFlushCount++"), std::string::npos);
	EXPECT_NE(RoundedCommand.find("m_RoundedRectSdfCommandCount++"), std::string::npos);
	EXPECT_NE(GraphicsThreaded.find("rounded_sdf_commands_sum"), std::string::npos);
	EXPECT_NE(GraphicsThreaded.find("rounded_sdf_flushes_sum"), std::string::npos);
	EXPECT_NE(GraphicsThreaded.find("m_RoundedRectSdfCommandCount = 0;"), std::string::npos);
	EXPECT_NE(GraphicsThreaded.find("m_RoundedRectSdfFlushCount = 0;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, OrdinaryUiRoundedSurfacesUseSharedPath)
{
	const std::string Appearance = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Effects = ReadTextFile("src/game/client/components/ui_effects.cpp");
	const std::string HudEditor = ReadTextFile("src/game/client/components/hud_editor.cpp");
	const std::string TClientMenus = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Chat = ReadTextFile("src/game/client/components/chat.cpp");

	EXPECT_NE(Appearance.find("DrawRoundedSurface(Ui(), MessageBackground"), std::string::npos);
	EXPECT_EQ(Appearance.find("Graphics()->DrawRectExt(PreviewView"), std::string::npos);
	EXPECT_NE(Effects.find("DrawRoundedSurface(Ui(), ShadowRect"), std::string::npos);
	EXPECT_EQ(Effects.find("Graphics()->DrawRect(PreviewX + ShadowOffset"), std::string::npos);
	EXPECT_NE(HudEditor.find("DrawRoundedSurface(Ui(), HelpRect"), std::string::npos);
	EXPECT_EQ(HudEditor.find("Graphics()->DrawRect(HelpX, HelpY"), std::string::npos);
	EXPECT_NE(TClientMenus.find("DrawRoundedSurface(Ui(), BodyColor"), std::string::npos);
	EXPECT_NE(TClientMenus.find("DrawRoundedSurface(Ui(), FeetColor"), std::string::npos);

	// 聊天滚动条和实时预览仍属于高频绘制，保留批量直绘路径。
	EXPECT_NE(Chat.find("Graphics()->DrawRect(ScrollbarRect.x"), std::string::npos);
	EXPECT_NE(Chat.find("Graphics()->DrawRect(x, PreviewY"), std::string::npos);
}

TEST(QmNewUiMenuBranches, LegacyRoundedRectDrawSitesRequireExplicitAllowlist)
{
	const char *const apAllowlistedFiles[] = {
		"src/game/client/components/hud.cpp",
		"src/game/client/components/chat.cpp",
		"src/game/client/components/nameplates.cpp",
		"src/game/client/components/spectator.cpp",
		"src/game/client/components/statboard.cpp",
	};
	for(const char *pPath : apAllowlistedFiles)
	{
		const std::string Source = ReadTextFile(pPath);
		EXPECT_GT(CountRoundedRectDirectCalls(Source), 0u) << pPath;
	}

	const char *const apOrdinaryUiFiles[] = {
		"src/game/client/components/menus.cpp",
		"src/game/client/components/menus_ingame.cpp",
		"src/game/client/components/menus_start.cpp",
		"src/game/client/components/menus_browser.cpp",
		"src/game/client/components/menus_demo.cpp",
		"src/game/client/components/menus_settings.cpp",
		"src/game/client/components/menus_settings7.cpp",
		"src/game/client/components/menus_settings_assets.cpp",
		"src/game/client/components/menus_settings_controls.cpp",
		"src/game/client/components/tclient/menus_tclient.cpp",
		"src/game/client/components/qmclient/menus_qmclient.cpp",
		"src/game/client/components/ui_effects.cpp",
		"src/game/client/components/hud_editor.cpp",
		"src/game/client/ui.cpp",
		"src/game/client/QmUi/UiButtons.cpp",
		"src/game/client/QmUi/UiForms.cpp",
		"src/game/client/QmUi/SettingsCard.cpp",
		"src/game/client/QmUi/SettingsCardDeck.cpp",
		"src/game/client/QmUi/UiContainers.h",
		"src/game/client/QmUi/UiOverlays.h",
	};
	for(const char *pPath : apOrdinaryUiFiles)
	{
		const std::string Source = ReadTextFile(pPath);
		EXPECT_EQ(CountRoundedRectDirectCalls(Source), 0u) << pPath;
	}
}

TEST(QmNewUiMenuBranches, RoundedSurfaceGeometryCoversUiScaleAndRetinaMatrix)
{
	constexpr float Aspect = 16.0f / 9.0f;
	const int aScales[] = {100, 125, 150, 200};
	const int aScreenWidths[] = {1920, 3840};
	const CUIRect Rect{0.2f, 0.2f, 20.0f, 10.0f};
	const int aCornerMasks[] = {
		IGraphics::CORNER_ALL,
		IGraphics::CORNER_L,
		IGraphics::CORNER_R,
		IGraphics::CORNER_T,
		IGraphics::CORNER_B,
		IGraphics::CORNER_TL,
		IGraphics::CORNER_TR,
		IGraphics::CORNER_BL,
		IGraphics::CORNER_BR,
		IGraphics::CORNER_NONE,
	};
	for(const int Scale : aScales)
	{
		const float VirtualHeight = QmUiVirtualScreenHeight(Scale);
		const float VirtualWidth = VirtualHeight * Aspect;
		for(const int ScreenWidth : aScreenWidths)
		{
			const float PixelSize = VirtualWidth / (float)ScreenWidth;
			ASSERT_GT(PixelSize, 0.0f);
			SRoundedSurfaceParams Params;
			Params.m_Radius = 8.0f;
			Params.m_PixelSize = PixelSize;
			const SRoundedSurfacePlan Plan = ResolveRoundedSurfacePlan(Rect, Params, true);
			EXPECT_TRUE(Plan.m_UseSdf);
			const auto IsPixelAligned = [PixelSize](const float Value) {
				return std::abs(Value - std::round(Value / PixelSize) * PixelSize) < 1e-3f;
			};
			EXPECT_TRUE(IsPixelAligned(Plan.m_Rect.x));
			EXPECT_TRUE(IsPixelAligned(Plan.m_Rect.y));
			EXPECT_TRUE(IsPixelAligned(Plan.m_Rect.w));
			EXPECT_TRUE(IsPixelAligned(Plan.m_Rect.h));
			EXPECT_FLOAT_EQ(Plan.m_Radius, std::min(Plan.m_Rect.w, Plan.m_Rect.h) * 0.5f);
			for(const int Corners : aCornerMasks)
			{
				const vec4 Radii = ResolveRoundedSurfaceCornerRadii(Plan.m_Radius, Corners);
				EXPECT_FLOAT_EQ(Radii.x, Corners & IGraphics::CORNER_TL ? Plan.m_Radius : 0.0f);
				EXPECT_FLOAT_EQ(Radii.y, Corners & IGraphics::CORNER_TR ? Plan.m_Radius : 0.0f);
				EXPECT_FLOAT_EQ(Radii.z, Corners & IGraphics::CORNER_BR ? Plan.m_Radius : 0.0f);
				EXPECT_FLOAT_EQ(Radii.w, Corners & IGraphics::CORNER_BL ? Plan.m_Radius : 0.0f);
			}
			const SRoundedSurfacePlan Fallback = ResolveRoundedSurfacePlan(Rect, Params, false);
			EXPECT_FALSE(Fallback.m_UseSdf);
			EXPECT_FLOAT_EQ(Fallback.m_Rect.x, Plan.m_Rect.x);
			EXPECT_FLOAT_EQ(Fallback.m_Rect.y, Plan.m_Rect.y);
			EXPECT_FLOAT_EQ(Fallback.m_Rect.w, Plan.m_Rect.w);
			EXPECT_FLOAT_EQ(Fallback.m_Rect.h, Plan.m_Rect.h);
			EXPECT_FLOAT_EQ(Fallback.m_Radius, Plan.m_Radius);
		}
	}
}

TEST(QmNewUiMenuBranches, RetinaNameplatesPreferPhysicalPixelAlignment)
{
	EXPECT_FALSE(QmNameplateUsesPhysicalPixelAlignment(1.0f, true));
	EXPECT_TRUE(QmNameplateUsesPhysicalPixelAlignment(1.5f, true));
	EXPECT_TRUE(QmNameplateUsesPhysicalPixelAlignment(2.0f, true));
	EXPECT_FALSE(QmNameplateUsesPhysicalPixelAlignment(2.0f, false));

	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	EXPECT_NE(Source.find("#if defined(CONF_PLATFORM_MACOS)"), std::string::npos);
	EXPECT_NE(Source.find("QmNameplateUsesPhysicalPixelAlignment(This.Graphics()->ScreenHiDPIScale(), true)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NameplateTextRasterizesAtStandardZoom)
{
	// 铭牌文字按官方行为在"标准缩放"的界面映射下栅格化：容器顶点固定在世界单位，
	// 绘制时随当前相机映射缩放。非 1.0 缩放下字形会被重采样，但所有铭牌一致，
	// 不会再出现"部分玩家清晰、部分玩家发虚"。
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	EXPECT_NE(Source.find("This.Graphics()->MapScreenToGameInterface(This.m_Camera.m_Center.x, This.m_Camera.m_Center.y);"), std::string::npos);
	// 文本容器失效时不得因为 UpdateNeeded() 为假而跳过重建（官方行为）。
	EXPECT_NE(Source.find("if(!NeedsTextUpdate && m_TextContainerIndex.Valid())"), std::string::npos);

	// 旧的"按真实屏幕映射密度栅格化 + 缩放停稳判定 + 每帧重建预算"机制必须整体移除：
	// 它只在"刚停稳那一帧"放行重建，且每帧只允许 64 个文本部件重建，
	// 没抢到预算的部件会永久停在旧密度上，表现为同一屏内部分铭牌长期发虚。
	EXPECT_EQ(Source.find("This.Graphics()->MapScreenToGameInterface(This.m_Camera.m_Center.x, This.m_Camera.m_Center.y, This.m_Camera.m_Zoom);"), std::string::npos);
	EXPECT_EQ(Source.find("QmNameplateTextNeedsRebake"), std::string::npos);
	EXPECT_EQ(Source.find("m_Rasterization"), std::string::npos);
	EXPECT_EQ(Source.find("m_ZoomStability"), std::string::npos);
	EXPECT_EQ(Source.find("NAMEPLATE_TEXT_REBUILD_BUDGET_PER_FRAME"), std::string::npos);
	EXPECT_EQ(Source.find("s_NameplateRasterizationDensity"), std::string::npos);

	const std::string Header = ReadTextFile("src/game/client/components/nameplates.h");
	EXPECT_EQ(Header.find("SQmNameplateTextRasterization"), std::string::npos);
	EXPECT_EQ(Header.find("SQmNameplateTextZoomStability"), std::string::npos);
	EXPECT_EQ(Header.find("QmNameplateTextRasterizationDensity"), std::string::npos);
	// HiDPI 物理像素对齐开关属于官方行为，保留。
	EXPECT_NE(Header.find("QmNameplateUsesPhysicalPixelAlignment"), std::string::npos);
}

TEST(QmSpectatorFriendPriority, StablePartitionPutsFriendsFirst)
{
	const bool aIsFriend[] = {false, true, false, true, false};
	int aOrder[5] = {};
	const int FriendCount = qm_spectator_friends::BuildFriendFirstOrder(aIsFriend, 5, aOrder);
	ASSERT_EQ(FriendCount, 2);
	// 好友保持原有相对顺序（原下标 1 在 3 之前），其余玩家同理（0、2、4）。
	EXPECT_EQ(aOrder[0], 1);
	EXPECT_EQ(aOrder[1], 3);
	EXPECT_EQ(aOrder[2], 0);
	EXPECT_EQ(aOrder[3], 2);
	EXPECT_EQ(aOrder[4], 4);
}

TEST(QmSpectatorFriendPriority, HandlesAllFriendsNoFriendsAndEmptyList)
{
	const bool aAllFriends[] = {true, true, true};
	int aOrder[3] = {};
	EXPECT_EQ(qm_spectator_friends::BuildFriendFirstOrder(aAllFriends, 3, aOrder), 3);
	for(int i = 0; i < 3; ++i)
		EXPECT_EQ(aOrder[i], i);

	const bool aNoFriends[] = {false, false, false};
	EXPECT_EQ(qm_spectator_friends::BuildFriendFirstOrder(aNoFriends, 3, aOrder), 0);
	for(int i = 0; i < 3; ++i)
		EXPECT_EQ(aOrder[i], i);

	// 空列表不写出任何下标。
	int Dummy = -1;
	EXPECT_EQ(qm_spectator_friends::BuildFriendFirstOrder(aNoFriends, 0, &Dummy), 0);
	EXPECT_EQ(Dummy, -1);
	EXPECT_EQ(qm_spectator_friends::BuildFriendFirstOrder(aNoFriends, -1, &Dummy), 0);
	EXPECT_EQ(Dummy, -1);
}

TEST(QmSpectatorFriendPriority, FullServerOrderIsAPermutationWithSingleBoundary)
{
	bool aIsFriend[MAX_CLIENTS] = {};
	// 每隔三个玩家取一个好友，覆盖满员（64 人）时的下标边界。
	for(int i = 0; i < MAX_CLIENTS; i += 3)
		aIsFriend[i] = true;

	int aOrder[MAX_CLIENTS] = {};
	const int FriendCount = qm_spectator_friends::BuildFriendFirstOrder(aIsFriend, MAX_CLIENTS, aOrder);
	int ExpectedFriends = 0;
	for(int i = 0; i < MAX_CLIENTS; ++i)
		ExpectedFriends += aIsFriend[i] ? 1 : 0;
	EXPECT_EQ(FriendCount, ExpectedFriends);

	bool aSeen[MAX_CLIENTS] = {};
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		ASSERT_GE(aOrder[i], 0);
		ASSERT_LT(aOrder[i], MAX_CLIENTS);
		EXPECT_FALSE(aSeen[aOrder[i]]);
		aSeen[aOrder[i]] = true;
		// 分区边界之前必须全是好友，之后必须全不是好友。
		EXPECT_EQ(aIsFriend[aOrder[i]], i < FriendCount);
	}
}

TEST(QmSpectatorFriendPriority, SpectatorHudRendersFriendsFirstWithOwnGroupTitles)
{
	const std::string Spectator = ReadTextFile("src/game/client/components/spectator.cpp");
	const std::string Body = FunctionBody(Spectator, "void CSpectator::OnRender()");

	// 显示顺序由纯函数算出，绘制循环按该顺序取玩家，先排序后绘制。
	const size_t BuildOrder = Body.find("qm_spectator_friends::BuildFriendFirstOrder(");
	const size_t DrawLoop = Body.find("apDisplayPlayers[aDisplayOrder[");
	ASSERT_NE(BuildOrder, std::string::npos);
	ASSERT_NE(DrawLoop, std::string::npos);
	EXPECT_LT(BuildOrder, DrawLoop);

	// 好友组与其余玩家各自带分组标题，边界只出现一次。
	EXPECT_NE(Body.find("Localize(\"Friends\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Others\")"), std::string::npos);
	EXPECT_NE(Body.find("i == FriendCount"), std::string::npos);
	// 分组标题不占用网格槽位，避免满员时多出一列把玩家挤出面板：换行判定只保留玩家行那一处。
	const size_t FirstWrap = Body.find("if(Count == PerLine + 1");
	ASSERT_NE(FirstWrap, std::string::npos);
	EXPECT_EQ(Body.find("if(Count == PerLine + 1", FirstWrap + 1), std::string::npos);

	// 好友判定沿用快照里的 m_Friend 缓存（与爱心图标同源），渲染路径不查好友表。
	EXPECT_NE(Body.find(".m_Friend;"), std::string::npos);
	EXPECT_EQ(Body.find("Friends()->IsFriend("), std::string::npos);
	EXPECT_EQ(Body.find("Foes()->IsFriend("), std::string::npos);
}

TEST(QmNewUiMenuBranches, FriendHeartsUseSolidHeartGlyph)
{
	// 意图：图标字体 Phosphor 只有中空心形（U+E2A8 为 2 轮廓），好友界面的爱心改用默认字体
	// DejaVu Sans 的实体心形 U+2665；调用点必须走共享常量，并把字体预设切回默认字体。
	EXPECT_STREQ(QM_FRIEND_HEART_ICON, "\xE2\x99\xA5");

	const std::string Header = ReadTextFile("src/game/client/components/qmclient/friend_heart_icon.h");
	EXPECT_NE(Header.find("QM_FRIEND_HEART_ICON"), std::string::npos);
	// 常量用 U+2665 的 UTF-8 字节，且不再引用图标字体的中空心形码位。
	EXPECT_NE(Header.find("\xE2\x99\xA5"), std::string::npos);
	EXPECT_EQ(Header.find("\xEE\x8A\xA8"), std::string::npos);

	const std::string Browser = ReadTextFile("src/game/client/components/menus_browser.cpp");
	EXPECT_NE(Browser.find("#include <game/client/components/qmclient/friend_heart_icon.h>"), std::string::npos);
	// 好友列表表头：默认字体的实体爱心。
	EXPECT_NE(Browser.find("Ui()->DoLabel(&Col.m_Rect, QM_FRIEND_HEART_ICON, 14.0f, TEXTALIGN_MC);"), std::string::npos);
	EXPECT_EQ(Browser.find("Ui()->DoLabel(&Col.m_Rect, FONT_ICON_HEART, 14.0f, TEXTALIGN_MC);"), std::string::npos);
	// 服务器列表好友图标：绘制辅助函数支持逐次指定字体预设，心形以外的图标继续用图标字体。
	EXPECT_NE(Browser.find("const char *pText, int TextAlign, bool SmallFont = false, EFontPreset FontPreset = EFontPreset::ICON_FONT)"), std::string::npos);
	EXPECT_NE(Browser.find("QM_FRIEND_HEART_ICON, TEXTALIGN_MC, false, EFontPreset::DEFAULT_FONT);"), std::string::npos);
	EXPECT_NE(Browser.find("FONT_ICON_STAR, TEXTALIGN_MC);"), std::string::npos);

	// 工具箱好友页签：两套 UI 分支都在画好友页签前切到默认字体，切换后不再切回图标字体。
	const std::string TabBar = FunctionBody(Browser, "void CMenus::RenderServerbrowserTabBar(CUIRect TabBar)");
	ASSERT_FALSE(TabBar.empty());
	size_t FriendsTabCount = 0;
	for(size_t Pos = TabBar.find("DoButton_MenuTab(&s_FriendsTabButton, QM_FRIEND_HEART_ICON"); Pos != std::string::npos;
		Pos = TabBar.find("DoButton_MenuTab(&s_FriendsTabButton, QM_FRIEND_HEART_ICON", Pos + 1))
	{
		const size_t SwitchPos = TabBar.rfind("TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);", Pos);
		ASSERT_NE(SwitchPos, std::string::npos);
		EXPECT_EQ(TabBar.substr(SwitchPos, Pos - SwitchPos).find("SetFontPreset(EFontPreset::ICON_FONT"), std::string::npos);
		++FriendsTabCount;
	}
	EXPECT_EQ(FriendsTabCount, 2);
	EXPECT_EQ(TabBar.find("DoButton_MenuTab(&s_FriendsTabButton, FONT_ICON_HEART"), std::string::npos);

	const std::string Scoreboard = ReadTextFile("src/game/client/components/scoreboard.cpp");
	EXPECT_NE(Scoreboard.find("#include <game/client/components/qmclient/friend_heart_icon.h>"), std::string::npos);
	// 未加好友态用实体爱心；已是好友悬停保留图标字体的中空裂心（图标字体没有实心裂心）。
	EXPECT_NE(Scoreboard.find("? FontIcons::FONT_ICON_HEART_CRACK : QM_FRIEND_HEART_ICON;"), std::string::npos);
	EXPECT_EQ(Scoreboard.find(": FontIcons::FONT_ICON_HEART;"), std::string::npos);
}
