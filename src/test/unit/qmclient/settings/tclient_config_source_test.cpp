#include <game/client/QmUi/cards/TClientConfigSource.h>

#include <gtest/gtest.h>

TEST(TClientConfigSource, RecognizesRenamedTClientVariables)
{
	EXPECT_TRUE(IsTClientConfigVariable("qm_statusbar_scheme"));
	EXPECT_TRUE(IsTClientConfigVariable("qm_bg_draw_width"));
	EXPECT_TRUE(IsTClientConfigVariable("qm_ui_show_tclient"));
}

TEST(TClientConfigSource, DoesNotClassifyOtherQmOrDDNetVariablesAsTClient)
{
	EXPECT_FALSE(IsTClientConfigVariable("qm_nonexistent_setting"));
	EXPECT_FALSE(IsTClientConfigVariable("cl_show_perf"));
	EXPECT_FALSE(IsTClientConfigVariable(""));
}
