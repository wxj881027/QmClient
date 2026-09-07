#include <game/client/ui/card_presentation.h>

#include <gtest/gtest.h>

TEST(CardPresentation, ResolvesStatePriorityAndCollapsedLayout)
{
	SCardPresentationInput Input;
	Input.m_Bounds = {10.0f, 20.0f, 480.0f, 200.0f};
	Input.m_UiScale = 1.5f;
	Input.m_Hovered = true;
	Input.m_Focused = true;
	Input.m_Pressed = true;
	Input.m_Collapsed = true;
	const SCardPresentationLayout Layout = ResolveCardPresentationLayout(Input);
	EXPECT_TRUE(Layout.m_Valid);
	EXPECT_EQ(Layout.m_State, ECardVisualState::COLLAPSED);
	EXPECT_TRUE(Layout.m_DrawChrome);
	EXPECT_FALSE(Layout.m_DrawContent);
	EXPECT_GT(Layout.m_Header.m_Height, 0.0f);
}

TEST(CardPresentation, InvalidAndUnavailableCardsDoNotProduceLayout)
{
	SCardPresentationInput Input;
	Input.m_Bounds = {0.0f, 0.0f, 500.0f, 200.0f};
	Input.m_Available = false;
	EXPECT_FALSE(ResolveCardPresentationLayout(Input).m_Valid);
	Input.m_Available = true;
	Input.m_Bounds.m_Width = 0.0f;
	EXPECT_FALSE(ResolveCardPresentationLayout(Input).m_Valid);
}

TEST(CardPresentation, NonCollapsibleCardsKeepTheirContent)
{
	SCardPresentationInput Input;
	Input.m_Bounds = {0.0f, 0.0f, 400.0f, 160.0f};
	Input.m_Collapsed = true;
	Input.m_Spec.m_SupportsCollapse = false;
	EXPECT_TRUE(ResolveCardPresentationLayout(Input).m_DrawContent);
}

TEST(CardPresentation, ActionsUseDeterministicPriority)
{
	EXPECT_EQ(ResolveCardPresentationAction("qm.timer", true, true, false).m_Type, ECardActionType::TOGGLE);
	EXPECT_EQ(ResolveCardPresentationAction("qm.timer", true, true, true).m_Type, ECardActionType::RESET);
	EXPECT_EQ(ResolveCardPresentationAction("", false, true, false).m_Type, ECardActionType::NONE);
}
