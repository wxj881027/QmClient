#include <game/client/QmUi/SettingsCardHelp.h>

#include <gtest/gtest.h>

#include <cstring>

TEST(SettingsCardHelp, HoverChangesTextWithoutChangingReservedHeight)
{
	CSettingsCardHelp Help;
	const auto Measure = [](const char *pText) { return static_cast<float>(std::strlen(pText)); };
	Help.Configure(100, 10, 0, "overview", Measure);
	Help.Register(1, "short", false, false, Measure);
	Help.Register(2, "a much longer explanation", false, false, Measure);
	const float Height = Help.Height();
	Help.BeginFrame();
	EXPECT_STREQ(Help.Text(), "overview");
	Help.Register(1, "short", true, false, Measure);
	EXPECT_STREQ(Help.Text(), "short");
	EXPECT_EQ(Help.Height(), Height);
	Help.BeginFrame();
	Help.Register(2, "a much longer explanation", true, false, Measure);
	EXPECT_STREQ(Help.Text(), "a much longer explanation");
	EXPECT_EQ(Help.Height(), Height);
}

TEST(SettingsCardHelp, FocusIsUsedWithoutHoverAndHoverTakesPriority)
{
	CSettingsCardHelp Help;
	const auto Measure = [](const char *) { return 10.0f; };
	Help.Configure(100, 10, 0, "overview", Measure);
	Help.BeginFrame();
	Help.Register(1, "focused", false, true, Measure);
	EXPECT_STREQ(Help.Text(), "focused");
	Help.Register(2, "hovered", true, false, Measure);
	Help.Register(1, "focused", false, true, Measure);
	EXPECT_STREQ(Help.Text(), "hovered");
	Help.BeginFrame();
	EXPECT_STREQ(Help.Text(), "overview");
}

TEST(SettingsCardHelp, LayoutOrLanguageChangeInvalidatesMeasurements)
{
	CSettingsCardHelp Help;
	int Calls = 0;
	const auto Measure = [&](const char *) { ++Calls; return 20.0f; };
	Help.Configure(100, 10, 0, "overview", Measure);
	Help.Register(1, "help", false, false, Measure);
	EXPECT_EQ(Calls, 2);
	Help.Configure(100, 10, 0, "overview", Measure);
	Help.Register(1, "help", true, false, Measure);
	EXPECT_EQ(Calls, 2);
	Help.Configure(50, 10, 0, "overview", Measure);
	Help.Register(1, "help", false, false, Measure);
	EXPECT_EQ(Calls, 4);
	Help.Configure(50, 10, 1, "overview", Measure);
	Help.Register(1, "new translation", false, false, Measure);
	EXPECT_EQ(Calls, 6);
}
