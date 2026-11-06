#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsCardDeckLogic.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	const std::vector<const char *> s_vTeeIds = {
		"deck:tee-identity",
		"deck:tee-skin-options",
		"deck:tee-skin-list",
		"deck:tee-skin-queue",
		"deck:tee-glow",
		"qm:skin_appearance",
	};

	constexpr const char *s_pModernTeeLayout =
		"deck:tee-skin-list|modern-tee|right|0;"
		"deck:tee-identity|modern-tee|right|1;"
		"deck:tee-glow|modern-tee|left|0;"
		"deck:tee-skin-options|modern-tee|left|1;"
		"deck:tee-skin-queue|modern-tee-effects|right|0;"
		"qm:skin_appearance|modern-tee-effects|right|1;";

	std::array<std::vector<std::string>, 3> DeckColumns(const qm_card_order::CModel &Model, const char *pTab, const std::vector<const char *> &vStableIds)
	{
		std::vector<int> vActive;
		for(const char *pStableId : vStableIds)
		{
			const int Index = Model.StateIndexForStableId(pStableId);
			EXPECT_GE(Index, 0) << pStableId;
			if(Index >= 0)
				vActive.push_back(Index);
		}
		const auto aIndices = BuildSettingsCardDeckColumnOrder(Model, pTab, vActive);
		std::array<std::vector<std::string>, 3> aIds;
		for(size_t Column = 0; Column < aIndices.size(); ++Column)
			for(const int Index : aIndices[Column])
				aIds[Column].emplace_back(Model.Entry(Index).m_pStableId);
		return aIds;
	}

	void ExpectBefore(const std::vector<std::string> &vIds, const char *pFirst, const char *pSecond)
	{
		const auto First = std::find(vIds.begin(), vIds.end(), pFirst);
		const auto Second = std::find(vIds.begin(), vIds.end(), pSecond);
		ASSERT_NE(First, vIds.end()) << pFirst;
		ASSERT_NE(Second, vIds.end()) << pSecond;
		EXPECT_LT(std::distance(vIds.begin(), First), std::distance(vIds.begin(), Second));
	}
}

TEST(QmCardTabRecovery, TeeCardsReappearInTheLegacyDeckProjection)
{
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged(s_pModernTeeLayout, qm_card_registry::BuildDefaultEntries()));
	const auto Before = DeckColumns(Model, "tee", s_vTeeIds);
	for(const auto &Column : Before)
		EXPECT_TRUE(Column.empty());

	ASSERT_TRUE(qm_card_registry::RepairRemovedModernTabs(Model));
	const auto After = DeckColumns(Model, "tee", s_vTeeIds);
	EXPECT_EQ(After[0].size() + After[1].size() + After[2].size(), s_vTeeIds.size());
	for(const char *pStableId : s_vTeeIds)
	{
		const int Index = Model.StateIndexForStableId(pStableId);
		ASSERT_GE(Index, 0);
		EXPECT_STREQ(Model.Entry(Index).m_pDefaultTab, "tee");
	}
}

TEST(QmCardTabRecovery, GeneralGraphicsAndSoundCardsReturnToTheirLegacyPageProjections)
{
	struct SPageCase
	{
		const char *m_pStableId;
		const char *m_pLegacyTab;
		const char *m_pSaved;
	};
	const SPageCase aCases[] = {
		{"deck:general-game", "general", "deck:general-game|modern-features-game|full|0;"},
		{"deck:graphics-display", "graphics", "deck:graphics-display|modern-visual-graphics|right|0;"},
		{"deck:sound-toggle", "sound", "deck:sound-toggle|modern-sound|left|0;"},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pStableId);
		qm_card_order::CModel Model;
		ASSERT_TRUE(Model.LoadMerged(Case.m_pSaved, qm_card_registry::BuildDefaultEntries()));
		const auto Before = DeckColumns(Model, Case.m_pLegacyTab, {Case.m_pStableId});
		for(const auto &Column : Before)
			EXPECT_TRUE(Column.empty());

		ASSERT_TRUE(qm_card_registry::RepairRemovedModernTabs(Model));
		const auto After = DeckColumns(Model, Case.m_pLegacyTab, {Case.m_pStableId});
		EXPECT_EQ(After[0].size() + After[1].size() + After[2].size(), 1u);
	}
}

TEST(QmCardTabRecovery, RecoveryPreservesCustomColumnsAndEachSourceColumnsRelativeOrder)
{
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged(s_pModernTeeLayout, qm_card_registry::BuildDefaultEntries()));
	ASSERT_TRUE(qm_card_registry::RepairRemovedModernTabs(Model));

	const auto Columns = DeckColumns(Model, "tee", s_vTeeIds);
	EXPECT_TRUE(Columns[0].empty());
	EXPECT_EQ(Columns[1], (std::vector<std::string>{"deck:tee-glow", "deck:tee-skin-options"}));
	ASSERT_EQ(Columns[2].size(), 4u);
	ExpectBefore(Columns[2], "deck:tee-skin-list", "deck:tee-identity");
	ExpectBefore(Columns[2], "deck:tee-skin-queue", "qm:skin_appearance");
}

TEST(QmCardTabRecovery, RepeatedRecoveryAndSavingReloadingKeepTheSameDeckProjection)
{
	const auto vDefaults = qm_card_registry::BuildDefaultEntries();
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged(s_pModernTeeLayout, vDefaults));
	ASSERT_TRUE(qm_card_registry::RepairRemovedModernTabs(Model));
	const auto Before = DeckColumns(Model, "tee", s_vTeeIds);
	char aSaved[32768];
	ASSERT_TRUE(Model.Serialize(aSaved, sizeof(aSaved)));

	EXPECT_FALSE(qm_card_registry::RepairRemovedModernTabs(Model));
	EXPECT_FALSE(qm_card_registry::RepairRemovedModernTabs(Model));
	char aRepeated[32768];
	ASSERT_TRUE(Model.Serialize(aRepeated, sizeof(aRepeated)));
	EXPECT_STREQ(aRepeated, aSaved);

	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSaved, vDefaults));
	EXPECT_FALSE(qm_card_registry::RepairRemovedModernTabs(Reloaded));
	EXPECT_EQ(DeckColumns(Reloaded, "tee", s_vTeeIds), Before);
	char aReloaded[32768];
	ASSERT_TRUE(Reloaded.Serialize(aReloaded, sizeof(aReloaded)));
	EXPECT_STREQ(aReloaded, aSaved);
}

TEST(QmCardTabRecovery, ValidLegacyPlacementsRemainUnchangedWhileAnotherCardRecovers)
{
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged(
		"deck:tee-skin-list|tee|right|0;"
		"deck:tee-skin-options|tee|right|1;"
		"deck:tee-identity|modern-tee|left|0;"
		"deck:general-game|graphics|full|0;",
		qm_card_registry::BuildDefaultEntries()));
	const auto TeeRightBefore = Model.StableIdOrder("", "tee", 2);
	const int MovedGameIndex = Model.StateIndexForStableId("deck:general-game");
	ASSERT_GE(MovedGameIndex, 0);
	const int GameColumnBefore = Model.Entry(MovedGameIndex).m_Column;
	const int GameOrderBefore = Model.Entry(MovedGameIndex).m_OrderInColumn;

	ASSERT_TRUE(qm_card_registry::RepairRemovedModernTabs(Model));
	EXPECT_EQ(Model.StableIdOrder("", "tee", 2), TeeRightBefore);
	const int GameIndexAfter = Model.StateIndexForStableId("deck:general-game");
	ASSERT_GE(GameIndexAfter, 0);
	EXPECT_STREQ(Model.Entry(GameIndexAfter).m_pDefaultTab, "graphics");
	EXPECT_EQ(Model.Entry(GameIndexAfter).m_Column, GameColumnBefore);
	EXPECT_EQ(Model.Entry(GameIndexAfter).m_OrderInColumn, GameOrderBefore);
	EXPECT_EQ(DeckColumns(Model, "tee", {"deck:tee-identity"})[1], (std::vector<std::string>{"deck:tee-identity"}));
}

TEST(QmCardTabRecovery, UnknownRegistryIdsAndNonModernTabNamesAreNotReassigned)
{
	auto vDefaults = qm_card_registry::BuildDefaultEntries();
	// 扩展模块条目通过真实载入保留下来，验证修复不会改写注册表不认识的 ID。
	vDefaults.push_back({"external:layout-placeholder", "extension", 2, 0});
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged(
		"external:layout-placeholder|modern-extension|right|0;"
		"deck:sound-volume|personal-audio|full|0;"
		"deck:graphics-icons|modernity-theme|left|0;",
		vDefaults));
	ASSERT_GE(Model.StateIndexForStableId("external:layout-placeholder"), 0);
	char aBefore[32768];
	ASSERT_TRUE(Model.Serialize(aBefore, sizeof(aBefore)));

	EXPECT_FALSE(qm_card_registry::RepairRemovedModernTabs(Model));
	char aAfter[32768];
	ASSERT_TRUE(Model.Serialize(aAfter, sizeof(aAfter)));
	EXPECT_STREQ(aAfter, aBefore);
}

TEST(QmCardTabRecovery, SavedVisualChatBubbleAppearsInChatPage)
{
	for(const char *pSaved : {
		    "qm:chat_bubble|visual|left|0;",
		    "qm:chat_bubble|visual|right|5;",
		    "qm:chat_bubble|visual|full|0;",
	    })
	{
		SCOPED_TRACE(pSaved);
		qm_card_order::CModel Model;
		ASSERT_TRUE(Model.LoadMerged(pSaved, qm_card_registry::BuildDefaultEntries()));
		const auto &vChatIds = qm_card_catalog::CatalogPageStableIds("appearance-chat");
		const auto Before = DeckColumns(Model, "appearance-chat", vChatIds);
		EXPECT_EQ(Before[1], (std::vector<std::string>{"deck:appearance-chat-settings", "deck:appearance-chat-preview"}));

		ASSERT_TRUE(qm_card_registry::RepairLegacyChatBubbleTab(Model));
		const auto After = DeckColumns(Model, "appearance-chat", vChatIds);
		EXPECT_EQ(After[1], (std::vector<std::string>{"deck:appearance-chat-settings", "deck:appearance-chat-preview", "qm:chat_bubble"}));
		EXPECT_TRUE(After[0].empty());
		EXPECT_EQ(After[2], (std::vector<std::string>{"deck:appearance-chat-messages"}));
		const auto Visual = DeckColumns(Model, "visual", qm_card_catalog::VisualCardStableIds());
		for(const auto &Column : Visual)
			EXPECT_EQ(std::count(Column.begin(), Column.end(), "qm:chat_bubble"), 0);
	}
}

TEST(QmCardTabRecovery, ChatBubbleRepairPreservesOtherCardsCustomOrder)
{
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged(
		"qm:chat_bubble|visual|right|5;"
		"qm:camera_view|visual|right|0;"
		"qm:focus_mode|visual|right|6;"
		"deck:appearance-chat-preview|appearance-chat|left|0;"
		"deck:appearance-chat-settings|appearance-chat|left|1;",
		qm_card_registry::BuildDefaultEntries()));
	const auto VisualBefore = DeckColumns(Model, "visual", qm_card_catalog::VisualCardStableIds());
	const auto ChatBefore = DeckColumns(Model, "appearance-chat", qm_card_catalog::CatalogPageStableIds("appearance-chat"));
	const auto GeneralBefore = Model.StableIdOrder("", "general", 1);

	ASSERT_TRUE(qm_card_registry::RepairLegacyChatBubbleTab(Model));
	EXPECT_EQ(DeckColumns(Model, "visual", qm_card_catalog::VisualCardStableIds()), VisualBefore);
	const auto ChatAfter = DeckColumns(Model, "appearance-chat", qm_card_catalog::CatalogPageStableIds("appearance-chat"));
	auto ExpectedLeft = ChatBefore[1];
	ExpectedLeft.push_back("qm:chat_bubble");
	EXPECT_EQ(ChatAfter[1], ExpectedLeft);
	EXPECT_EQ(ChatAfter[0], ChatBefore[0]);
	EXPECT_EQ(ChatAfter[2], ChatBefore[2]);
	EXPECT_EQ(Model.StableIdOrder("", "general", 1), GeneralBefore);
}

TEST(QmCardTabRecovery, ChatBubbleAlreadyInChatKeepsUserPlacement)
{
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged("qm:chat_bubble|appearance-chat|right|0;", qm_card_registry::BuildDefaultEntries()));
	char aBefore[32768];
	ASSERT_TRUE(Model.Serialize(aBefore, sizeof(aBefore)));
	const uint64_t Revision = Model.LayoutRevision();

	EXPECT_FALSE(qm_card_registry::RepairLegacyChatBubbleTab(Model));
	EXPECT_EQ(Model.LayoutRevision(), Revision);
	char aAfter[32768];
	ASSERT_TRUE(Model.Serialize(aAfter, sizeof(aAfter)));
	EXPECT_STREQ(aAfter, aBefore);
}

TEST(QmCardTabRecovery, ChatBubbleRepairSurvivesReloadAndSearchTargetsChat)
{
	const auto vDefaults = qm_card_registry::BuildDefaultEntries();
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged("qm:chat_bubble|visual|right|5;", vDefaults));
	ASSERT_TRUE(qm_card_registry::RepairLegacyChatBubbleTab(Model));
	const uint64_t Revision = Model.LayoutRevision();
	EXPECT_FALSE(qm_card_registry::RepairLegacyChatBubbleTab(Model));
	EXPECT_EQ(Model.LayoutRevision(), Revision);
	char aSaved[32768];
	ASSERT_TRUE(Model.Serialize(aSaved, sizeof(aSaved)));

	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSaved, vDefaults));
	EXPECT_FALSE(qm_card_registry::RepairLegacyChatBubbleTab(Reloaded));
	EXPECT_EQ(DeckColumns(Reloaded, "appearance-chat", qm_card_catalog::CatalogPageStableIds("appearance-chat")),
		DeckColumns(Model, "appearance-chat", qm_card_catalog::CatalogPageStableIds("appearance-chat")));
	const auto Results = qm_card_registry::SearchCards("qm_chat_bubble", Reloaded);
	const auto It = std::find_if(Results.begin(), Results.end(), [](const auto &Result) {
		return std::string(Result.m_pStableId) == "qm:chat_bubble";
	});
	ASSERT_NE(It, Results.end());
	EXPECT_STREQ(It->m_Target.m_pTab, "appearance-chat");
}

TEST(QmCardTabRecovery, BindStatusHudDefaultsAndSearchUseAppearanceHud)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());
	const auto Columns = DeckColumns(Model, "appearance-hud", qm_card_catalog::CatalogPageStableIds("appearance-hud"));
	EXPECT_EQ(Columns[2], (std::vector<std::string>{"deck:appearance-hud-ddrace", "qm:bind_status_hud"}));
	EXPECT_TRUE(qm_card_catalog::HasCardModule("qm:bind_status_hud"));
	const auto &HudIds = qm_card_catalog::HudCardStableIds();
	EXPECT_TRUE(std::none_of(HudIds.begin(), HudIds.end(), [](const char *pId) { return std::string(pId) == "qm:bind_status_hud"; }));
	for(const char *pQuery : {"DDRace HUD Pro", "Show key stuck status", "cl_showhud_key_status_sync"})
	{
		SCOPED_TRACE(pQuery);
		const auto Results = qm_card_registry::SearchCards(pQuery, Model);
		const auto It = std::find_if(Results.begin(), Results.end(), [](const auto &Result) {
			return std::string(Result.m_pStableId) == "qm:bind_status_hud";
		});
		ASSERT_NE(It, Results.end());
		EXPECT_STREQ(It->m_Target.m_pTab, "appearance-hud");
		EXPECT_STREQ(It->m_Target.m_pStableId, "qm:bind_status_hud");
	}
}

TEST(QmCardTabRecovery, BindStatusHudSavedInQmHudMovesAfterExistingAppearanceCards)
{
	const auto Defaults = qm_card_registry::BuildDefaultEntries();
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged(
		"qm:bind_status_hud|hud|left|0;qm:debug_graph|hud|left|1;"
		"deck:appearance-hud-main|appearance-hud|right|0;"
		"deck:appearance-hud-ddrace|appearance-hud|right|1;", Defaults));
	const auto BeforeSource = DeckColumns(Model, "hud", qm_card_catalog::HudCardStableIds());
	const auto BeforeTarget = DeckColumns(Model, "appearance-hud", qm_card_catalog::CatalogPageStableIds("appearance-hud"));
	ASSERT_TRUE(qm_card_registry::RepairLegacyBindStatusHudTab(Model));
	auto ExpectedTarget = BeforeTarget;
	ExpectedTarget[2].push_back("qm:bind_status_hud");
	EXPECT_EQ(DeckColumns(Model, "appearance-hud", qm_card_catalog::CatalogPageStableIds("appearance-hud")), ExpectedTarget);
	EXPECT_EQ(DeckColumns(Model, "hud", qm_card_catalog::HudCardStableIds()), BeforeSource);
	const uint64_t Revision = Model.LayoutRevision();
	EXPECT_FALSE(qm_card_registry::RepairLegacyBindStatusHudTab(Model));
	EXPECT_EQ(Model.LayoutRevision(), Revision);
	char aSaved[32768];
	ASSERT_TRUE(Model.Serialize(aSaved, sizeof(aSaved)));
	qm_card_order::CModel Reloaded;
	ASSERT_TRUE(Reloaded.LoadMerged(aSaved, Defaults));
	EXPECT_FALSE(qm_card_registry::RepairLegacyBindStatusHudTab(Reloaded));
	EXPECT_EQ(DeckColumns(Reloaded, "appearance-hud", qm_card_catalog::CatalogPageStableIds("appearance-hud")), ExpectedTarget);
	const auto *pDefault = qm_card_registry::FindByStableId("qm:bind_status_hud");
	ASSERT_NE(pDefault, nullptr);
	EXPECT_STREQ(qm_card_registry::ResolveCardNavigationTarget(*pDefault, Reloaded).m_pTab, "appearance-hud");
}

TEST(QmCardTabRecovery, BindStatusHudAlreadyInAppearanceHudKeepsUserPlacement)
{
	qm_card_order::CModel Model;
	ASSERT_TRUE(Model.LoadMerged("qm:bind_status_hud|appearance-hud|left|0;", qm_card_registry::BuildDefaultEntries()));
	char aBefore[32768];
	ASSERT_TRUE(Model.Serialize(aBefore, sizeof(aBefore)));
	const uint64_t Revision = Model.LayoutRevision();
	EXPECT_FALSE(qm_card_registry::RepairLegacyBindStatusHudTab(Model));
	EXPECT_EQ(Model.LayoutRevision(), Revision);
	char aAfter[32768];
	ASSERT_TRUE(Model.Serialize(aAfter, sizeof(aAfter)));
	EXPECT_STREQ(aAfter, aBefore);
}
