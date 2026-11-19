#include <generated/protocol.h>

#include <game/client/components/qmclient/statusbar_items.h>
#include <game/client/components/tclient/statusbar.h>

#include <gtest/gtest.h>

#include <string>

TEST(QmStatusBarItems, SchemePreservesRegisteredScoreIdentity)
{
	CStatusItem Score([] {}, [] { return 1.0f; }, "s", "Points", "Points", "");
	CStatusItem Space([] {}, [] { return 0.0f; }, " _", "Space", " ", "");
	const auto vpItems = QmParseStatusBarScheme("s_ s?", {&Score, &Space});
	ASSERT_EQ(vpItems.size(), 4u);
	EXPECT_EQ(vpItems[0], &Score);
	EXPECT_EQ(vpItems[1], &Space);
	EXPECT_EQ(vpItems[2], &Space);
	EXPECT_EQ(vpItems[3], &Score);
}

TEST(QmStatusBarItems, UsesCallbacksOfTheOriginalItem)
{
	int Rendered = 0;
	float Width = 20.0f;
	CStatusItem Score([&] { ++Rendered; }, [&] { return Width; }, "s", "Points", "", "");
	const auto vpItems = QmParseStatusBarScheme("s", {&Score});
	ASSERT_EQ(vpItems.size(), 1u);
	Width = 40.0f;
	EXPECT_FLOAT_EQ(vpItems[0]->m_GetWidth(), 40.0f);
	vpItems[0]->m_RenderItem();
	EXPECT_EQ(Rendered, 1);
}

TEST(QmStatusBarItems, UnknownCodesAndEmptySchemesProduceNoItems)
{
	CStatusItem Score([] {}, [] { return 1.0f; }, "s", "Points", "", "");
	EXPECT_TRUE(QmParseStatusBarScheme("", {&Score}).empty());
	EXPECT_TRUE(QmParseStatusBarScheme("???", {&Score}).empty());
}

TEST(QmStatusBarItems, SchemeCannotExceedEditorCapacity)
{
	CStatusItem Score([] {}, [] { return 1.0f; }, "s", "Points", "", "");
	const std::string Scheme(STATUSBAR_MAX_SIZE + 50, 's');
	EXPECT_EQ(QmParseStatusBarScheme(Scheme.c_str(), {&Score}).size(), STATUSBAR_MAX_SIZE);
}

TEST(QmStatusBarItems, UnusableFrameDurationProducesZeroFps)
{
	EXPECT_EQ(tclient_statusbar::FramesPerSecond(0.0f), 0);
	EXPECT_EQ(tclient_statusbar::FramesPerSecond(1.0e-10f), 0);
}

TEST(QmStatusBarItems, FrameDurationRoundsToTheNearestFps)
{
	EXPECT_EQ(tclient_statusbar::FramesPerSecond(0.0159f), 63);
}
