// 图像页分组：形状/主题/图标/动效四张卡落在「视觉效果」子页签，显示类两张留在「显示选项」。
// 分组与搜索索引都是结构契约，一旦拆错，卡片会渲染在空页上或搜索指向不存在的配置。
#include <base/str.h>

#include <game/client/QmUi/QmCardOrderModel.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmCardSearchRegistry.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
	constexpr const char *CARD_EFFECTS = "deck:graphics-card-effects";
	constexpr const char *THEME = "deck:graphics-theme";
	constexpr const char *ICONS = "deck:graphics-icons";
	constexpr const char *INTERACTION = "deck:graphics-interaction";

	bool PageContains(const char *pPage, const char *pStableId)
	{
		const std::vector<const char *> &vIds = qm_card_catalog::CatalogPageStableIds(pPage);
		return std::any_of(vIds.begin(), vIds.end(), [pStableId](const char *pId) { return str_comp(pId, pStableId) == 0; });
	}

	const qm_card_search::SRegistration *Registration(const char *pStableId)
	{
		return qm_card_search::Find(pStableId);
	}

	bool EntryMatches(const char *pStableId, const char *pField, const char *pValue)
	{
		const qm_card_search::SRegistration *pRegistration = Registration(pStableId);
		if(pRegistration == nullptr)
			return false;
		for(size_t Index = 0; Index < pRegistration->m_Count; ++Index)
		{
			const char *pCandidate = str_comp(pField, "config") == 0 ? pRegistration->m_pEntries[Index].m_pConfigName : pRegistration->m_pEntries[Index].m_pLabel;
			if(pCandidate != nullptr && str_comp(pCandidate, pValue) == 0)
				return true;
		}
		return false;
	}
}

TEST(GraphicsCardSplit, VisualEffectsPageOwnsTheShapeThemeIconAndMotionCards)
{
	const std::vector<const char *> &vVisual = qm_card_catalog::CatalogPageStableIds("graphics-visual");
	ASSERT_EQ(vVisual.size(), 4u);
	// 阅读序：先形状/外观，再全局主题，然后图标与动效。
	EXPECT_STREQ(vVisual[0], CARD_EFFECTS);
	EXPECT_STREQ(vVisual[1], THEME);
	EXPECT_STREQ(vVisual[2], ICONS);
	EXPECT_STREQ(vVisual[3], INTERACTION);

	// 搬走的卡不能还留在显示页，否则同一张卡会渲染两遍。
	EXPECT_FALSE(PageContains("graphics", CARD_EFFECTS));
	EXPECT_FALSE(PageContains("graphics", THEME));
	EXPECT_FALSE(PageContains("graphics", ICONS));
	EXPECT_FALSE(PageContains("graphics", INTERACTION));
	// 显示页只留显示类两张卡。
	EXPECT_TRUE(PageContains("graphics", "deck:graphics-display"));
	EXPECT_TRUE(PageContains("graphics", "deck:graphics-modes"));

	// 两张新卡仍是标准卡：构建要分派到图像卡片构建器，否则子页签上是空页。
	EXPECT_GE(qm_card_catalog::StandardCardFamily(CARD_EFFECTS), 0);
	EXPECT_GE(qm_card_catalog::StandardCardFamily(THEME), 0);
}

// 意图：卡片换页后，玩家已保存的卡片顺序里仍记着旧 tab。若不做归属修复，
// 顺序模型按 tab 投影，两张卡会在新旧两页同时消失（本批实机复现过）。
TEST(GraphicsCardSplit, MovedCardsAreRehomedToTheirRegistryPage)
{
	qm_card_order::CModel Model;
	Model.LoadMerged("", qm_card_registry::BuildDefaultEntries());
	// 模拟旧存档：两张卡仍记着「显示选项」页与旧的列位置。
	Model.MoveToTab(ICONS, "graphics", 1, 0);
	Model.MoveToTab(INTERACTION, "graphics", 1, 1);

	const int Icons = Model.StateIndexForStableId(ICONS);
	const int Interaction = Model.StateIndexForStableId(INTERACTION);
	const auto CountInTab = [&](const char *pTab, int EntryIndex) {
		int Total = 0;
		for(int Column = 0; Column < 3; ++Column)
		{
			const std::vector<int> vColumn = Model.ColumnIndices(pTab, Column);
			Total += (int)std::count(vColumn.begin(), vColumn.end(), EntryIndex);
		}
		return Total;
	};
	// 旧存档里两张卡挂在「显示选项」，视觉页一张都没有。
	EXPECT_EQ(CountInTab("graphics-visual", Icons), 0);
	EXPECT_EQ(CountInTab("graphics-visual", Interaction), 0);

	EXPECT_TRUE(qm_card_registry::RepairLegacyGraphicsVisualCards(Model));
	// 修复后两张卡都在视觉页，且不再留在旧页（否则会渲染两遍或干脆看不见）。
	EXPECT_EQ(CountInTab("graphics-visual", Icons), 1);
	EXPECT_EQ(CountInTab("graphics-visual", Interaction), 1);
	EXPECT_EQ(CountInTab("graphics", Icons), 0);
	EXPECT_EQ(CountInTab("graphics", Interaction), 0);
	// 归属已正确时不再改动，重复调用是空操作。
	EXPECT_FALSE(qm_card_registry::RepairLegacyGraphicsVisualCards(Model));
}

TEST(GraphicsCardSplit, SearchIndexFollowsTheSplitCards)
{
	ASSERT_NE(Registration(CARD_EFFECTS), nullptr);
	ASSERT_NE(Registration(THEME), nullptr);

	// 卡片自身外观的配置只在卡片效果里。
	EXPECT_TRUE(EntryMatches(CARD_EFFECTS, "config", "qm_ui_card_border_color"));
	EXPECT_TRUE(EntryMatches(CARD_EFFECTS, "config", "qm_ui_card_color"));
	EXPECT_FALSE(EntryMatches(CARD_EFFECTS, "config", "qm_ui_text_color_mode"));
	EXPECT_FALSE(EntryMatches(CARD_EFFECTS, "config", "qm_ui_corner_curve"));

	// 形状（圆角/胶囊）与主题色同属界面主题卡。
	EXPECT_TRUE(EntryMatches(THEME, "config", "qm_ui_text_color_mode"));
	EXPECT_TRUE(EntryMatches(THEME, "config", "qm_blur_mode"));
	EXPECT_TRUE(EntryMatches(THEME, "config", "qm_ui_accent_color"));
	EXPECT_TRUE(EntryMatches(THEME, "config", "qm_ui_corner_curve"));
	EXPECT_TRUE(EntryMatches(THEME, "config", "qm_ui_capsule_style"));
	// 动效强度只属于界面动效卡。
	EXPECT_TRUE(EntryMatches(INTERACTION, "config", "qm_ui_motion_level"));
	EXPECT_FALSE(EntryMatches(THEME, "config", "qm_ui_motion_level"));

	// 「自动」档已删除，彩虹档是新增选项：搜索索引必须跟着文本颜色模式的语义走。
	EXPECT_TRUE(EntryMatches(THEME, "label", "Rainbow"));
	EXPECT_FALSE(EntryMatches(THEME, "label", "Auto"));

	// 旧卡 id 与已删除的焦点环配置都不能留在索引里：搜索会指向不存在的卡与配置。
	EXPECT_EQ(Registration("deck:graphics-visual"), nullptr);
	for(const qm_card_registry::SCardDefault &Default : qm_card_registry::Defaults())
	{
		if(Default.m_pStableId == nullptr)
			continue;
		EXPECT_FALSE(EntryMatches(Default.m_pStableId, "config", "qm_ui_focus_color")) << Default.m_pStableId;
	}
}
