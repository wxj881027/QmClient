#include <game/client/components/qmclient/browser_status_layout.h>

#include <gtest/gtest.h>

TEST(QmBrowserBottomInset, GlobalSafeAreaEightLeavesNoExtraInset)
{
	const CUIRect Screen{0.0f, 0.0f, 900.0f, 700.0f};
	const CUIRect View{8.0f, 8.0f, 884.0f, 684.0f};
	// 壳层已经内缩 8，页面再切 8 就会变成 16。
	EXPECT_FLOAT_EQ(QmResolveBrowserBottomInset(Screen, View), 0.0f);
	EXPECT_FLOAT_EQ(Screen.y + Screen.h - (View.y + View.h), ui_token::panel::MARGIN);
}

TEST(QmBrowserBottomInset, NoSafeAreaAddsFullMargin)
{
	const CUIRect Screen{0.0f, 0.0f, 900.0f, 700.0f};
	const CUIRect View{0.0f, 40.0f, 900.0f, 660.0f}; // 顶部让给导航栏，底部贴着窗口
	EXPECT_FLOAT_EQ(QmResolveBrowserBottomInset(Screen, View), ui_token::panel::MARGIN);
}

TEST(QmBrowserBottomInset, PartialInsetIsToppedUp)
{
	const CUIRect Screen{0.0f, 0.0f, 900.0f, 700.0f};
	const CUIRect View{0.0f, 40.0f, 900.0f, 657.0f}; // 底部已有 3
	EXPECT_FLOAT_EQ(QmResolveBrowserBottomInset(Screen, View), ui_token::panel::MARGIN - 3.0f);
}

TEST(QmBrowserBottomInset, LargerExistingInsetIsNotShrunk)
{
	const CUIRect Screen{0.0f, 0.0f, 900.0f, 700.0f};
	const CUIRect View{0.0f, 40.0f, 900.0f, 640.0f}; // 底部已有 20
	EXPECT_FLOAT_EQ(QmResolveBrowserBottomInset(Screen, View), 0.0f);
}

TEST(QmBrowserBottomInset, FinalPanelToWindowGapAlwaysEqualsToken)
{
	const CUIRect Screen{0.0f, 0.0f, 900.0f, 700.0f};
	for(const float Existing : {0.0f, 2.0f, 5.0f, 8.0f})
	{
		const CUIRect View{0.0f, 40.0f, 900.0f, 660.0f - Existing};
		const float Extra = QmResolveBrowserBottomInset(Screen, View);
		// 页面把 Extra 从底部切掉后，面板底边到窗口底的实际距离。
		const float FinalGap = Screen.y + Screen.h - (View.y + View.h - Extra);
		EXPECT_FLOAT_EQ(FinalGap, ui_token::panel::MARGIN) << "existing=" << Existing;
	}
}
