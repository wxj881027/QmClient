#include <game/client/QmUi/QmSearchHeader.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/cards/QmSearchAnswerCard.h>
#include <game/client/components/qmclient/settings_global_search_cache.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string_view>

TEST(QmGlobalSearch, SubmittedPlaceholdersPublishAnswersAndTheirVisibleCards)
{
	qm_card_order::CModel Model;
	Model.SetEntries(qm_card_registry::BuildDefaultEntries());
	CSettingsGlobalSearchCache Cache;
	for(int Choice = 0; Choice < qm_search_header::NUM_SEARCH_PLACEHOLDERS; ++Choice)
	{
		qm_search_header::CSession Session;
		Session.Enter(1, 0, 0, Choice);
		const char *pQuery = Session.PlaceholderForSubmit("", false, true);
		ASSERT_STREQ(pQuery, Session.Placeholder());
		SCOPED_TRACE(pQuery);
		for(const bool Sixup : {false, true})
		{
			SCOPED_TRACE(Sixup);
			ASSERT_TRUE(Cache.TryUpdate(pQuery, "", Sixup, 0, Model));
			const auto &Results = Cache.Results();
			const auto &Order = Cache.OrderModel(false);
			const auto *pAnswer = Results.m_Answers.m_pAnswer;
			ASSERT_NE(pAnswer, nullptr);
			EXPECT_EQ(pAnswer, qm_search_answers::Search(pQuery).m_pAnswer);
			EXPECT_GE(Order.StateIndexForStableId(qm_card_catalog::SEARCH_ANSWER_CARD_ID), 0);
			for(const char *pId : pAnswer->m_vRelatedCards)
			{
				SCOPED_TRACE(pId);
				const auto *pCard = qm_card_registry::FindByStableId(pId);
				ASSERT_NE(pCard, nullptr);
				const auto Target = qm_card_registry::ResolveCardNavigationTarget(*pCard, Model);
				const std::string_view Tab = Target.m_pTab ? Target.m_pTab : "";
				const bool Visible = Tab != (Sixup ? "tee" : "tee7");
				const auto Count = std::count_if(Results.m_vAllVisibleCards.begin(), Results.m_vAllVisibleCards.end(), [&](const auto &Card) {
					return std::string_view(Card.m_pStableId) == Target.m_pStableId;
				});
				EXPECT_EQ(Count, Visible ? 1 : 0);
				EXPECT_EQ(Order.StateIndexForStableId(Target.m_pStableId) >= 0, Visible);
			}
		}
	}
}

TEST(QmGlobalSearch, NarrowSearchRowKeepsRoomForTextAndTheSearchButton)
{
	const auto Metrics = ResolveSettingsContentMetrics(80.0f);
	const float FontSize = Metrics.m_BodySize * 1.2f;
	for(float Width : {80.0f, 500.0f})
	{
		SCOPED_TRACE(Width);
		const auto Row = ResolveSettingsInputActionsLayout({0, 0, Width, Metrics.m_LineHeight * 1.2f}, 1, Metrics);
		const auto Field = ui_widget::ResolveInputFieldLayout(Row.m_Input, true, true, Metrics.m_UiScale, 0, FontSize, 2 * FontSize);
		EXPECT_GE(Field.m_TextRect.w, 2 * FontSize);
		EXPECT_GT(Row.m_Actions.m_aButtons[0].w, 0.0f);
		EXPECT_LT(Row.m_Input.x + Row.m_Input.w, Row.m_Actions.m_aButtons[0].x);
		if(Width == 80.0f)
			EXPECT_FLOAT_EQ(Field.m_IconRect.w, 0.0f);
		else
		{
			EXPECT_GT(Field.m_IconRect.w, 0.0f);
			EXPECT_GT(Field.m_ClearRect.w, 0.0f);
		}
	}
}
