#include <base/system.h>

#include <game/client/QmUi/cards/QmConsoleSchemeList.h>

#include <gtest/gtest.h>

#include <set>

// 一份配置只对应一个方案编号，列表必须把历史三项与内置配色放在同一集合里，
// 否则玩家选中内置配色后再也切不回现代终端 / 经典 PowerShell / 自定义。
TEST(QmConsoleSchemeList, EverySchemeValueIsSelectableFromOneList)
{
	const auto &Entries = qm_console_scheme_list::All();
	std::set<int> Values;
	for(int Index = 0; Index < qm_console_scheme_list::Count(); ++Index)
	{
		EXPECT_TRUE(Values.insert(Entries[Index].m_Value).second) << "方案编号重复: " << Entries[Index].m_Value;
		// 列表里每一项都必须能被反查回来，否则配置值会在界面上"消失"。
		EXPECT_EQ(qm_console_scheme_list::IndexOf(Entries[Index].m_Value), Index);
	}
	// 历史三项 + 全部 Windows Terminal 内置配色，一个都不能少。
	EXPECT_EQ(static_cast<int>(Values.size()), 3 + QmConsoleSchemes::Count());
	for(int Legacy = 0; Legacy < 3; ++Legacy)
		EXPECT_NE(qm_console_scheme_list::IndexOf(Legacy), -1) << "缺少历史方案 " << Legacy;
	for(int Builtin = 0; Builtin < QmConsoleSchemes::Count(); ++Builtin)
		EXPECT_NE(qm_console_scheme_list::IndexOf(QmConsoleAppearance::k_BuiltinSchemeBase + Builtin), -1);
}

// 历史三项没有 ANSI 色板，内置配色必须有；界面靠这个字段决定画哪种色块。
TEST(QmConsoleSchemeList, LegacyEntriesHaveNoAnsiSwatchAndBuiltinsDo)
{
	const auto &Entries = qm_console_scheme_list::All();
	for(int Index = 0; Index < qm_console_scheme_list::Count(); ++Index)
	{
		const auto &Entry = Entries[Index];
		SCOPED_TRACE(Entry.m_pName);
		EXPECT_NE(Entry.m_pName, nullptr);
		if(Entry.m_Kind == qm_console_scheme_list::EKind::LEGACY)
		{
			EXPECT_EQ(Entry.m_pPalette, nullptr);
			EXPECT_GE(Entry.m_Value, 0);
			EXPECT_LE(Entry.m_Value, 2);
		}
		else
		{
			ASSERT_NE(Entry.m_pPalette, nullptr);
			EXPECT_GE(Entry.m_Value, QmConsoleAppearance::k_BuiltinSchemeBase);
		}
	}
}

// 历史方案在列表里给出的名称必须与配置语义一致；顺序是 0/1/2，不能乱。
TEST(QmConsoleSchemeList, LegacyNamesFollowTheConfigMeaning)
{
	const auto &Entries = qm_console_scheme_list::All();
	EXPECT_STREQ(Entries[qm_console_scheme_list::IndexOf(0)].m_pName, "Modern terminal");
	EXPECT_STREQ(Entries[qm_console_scheme_list::IndexOf(1)].m_pName, "Classic PowerShell");
	EXPECT_STREQ(Entries[qm_console_scheme_list::IndexOf(2)].m_pName, "Custom");
}

// 无语言库时 Localize 原样返回，显示名必须回落到英文 key；
// 这条同时守住"显示名走 Localize、模型里存 key"的分工。
TEST(QmConsoleSchemeList, DisplayNameFallsBackToTheEnglishKeyWithoutTranslation)
{
	const auto &Entries = qm_console_scheme_list::All();
	for(int Index = 0; Index < qm_console_scheme_list::Count(); ++Index)
	{
		SCOPED_TRACE(Entries[Index].m_pName);
		EXPECT_STREQ(qm_console_scheme_list::DisplayName(Entries[Index]), Entries[Index].m_pName);
	}
}

// 历史三项固定在列表最前面，内置配色在其后：这是界面把三项当表头的依据。
TEST(QmConsoleSchemeList, LegacyEntriesComeFirstAndStayContiguous)
{
	EXPECT_EQ(qm_console_scheme_list::k_LegacyCount, 3);
	const auto &Entries = qm_console_scheme_list::All();
	for(int Index = 0; Index < qm_console_scheme_list::Count(); ++Index)
	{
		const bool ShouldBeLegacy = Index < qm_console_scheme_list::k_LegacyCount;
		EXPECT_EQ(Entries[Index].m_Kind == qm_console_scheme_list::EKind::LEGACY, ShouldBeLegacy) << "位置 " << Index;
	}
	// 表头三项的配置编号必须是 0/1/2，顺序与配置语义绑定。
	for(int Index = 0; Index < qm_console_scheme_list::k_LegacyCount; ++Index)
		EXPECT_EQ(Entries[Index].m_Value, Index);
}

// 行内标题必须看得清：自定义方案的出厂配色接近"深蓝画在近黑底上"，
// 直接照搬前景色会渲染出一行看不见的字。
TEST(QmConsoleSchemeList, LegibleTextColorKeepsEveryRowLabelReadable)
{
	const auto &Entries = qm_console_scheme_list::All();
	for(int Index = 0; Index < qm_console_scheme_list::Count(); ++Index)
	{
		SCOPED_TRACE(Entries[Index].m_pName);
		const auto Preview = qm_console_scheme_list::BuildPreview(Entries[Index], 100, {});
		const float Contrast = std::abs(qm_console_scheme_list::RelativeLuminance(Preview.m_Foreground) -
						qm_console_scheme_list::RelativeLuminance(Preview.m_Background));
		EXPECT_GE(Contrast, 0.27f) << "行底色与标题色亮度太接近";
		// 行底色不透明，否则叠在面板上会串色。
		EXPECT_FLOAT_EQ(Preview.m_Background.a, 1.0f);
	}
}

// 自定义方案必须用玩家配的九项取色，不能拿空数组算出"一片黑"。
// 这是列表预览最容易回归的一处：渲染路径传了真实颜色，预览却忘了传。
TEST(QmConsoleSchemeList, CustomSchemePreviewUsesTheConfiguredColors)
{
	std::array<unsigned, QmConsoleAppearance::COLOR_COUNT> Custom{};
	// 每项给一个互不相同的亮色，任何一项被丢掉都会在断言里露出来。
	const unsigned aColors[] = {0x101010, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0x00FFFF, 0xFF00FF, 0x808080};
	for(size_t i = 0; i < std::size(aColors); ++i)
		Custom[i] = aColors[i];

	const auto &Entries = qm_console_scheme_list::All();
	const int CustomIndex = qm_console_scheme_list::IndexOf(2);
	ASSERT_NE(CustomIndex, -1);
	const auto Preview = qm_console_scheme_list::BuildPreview(Entries[CustomIndex], 100, Custom);

	// 行底色与标题色必须来自配置，而不是默认回退色。
	const ColorRGBA ExpectedBackground = color_cast<ColorRGBA>(ColorHSLA(Custom[QmConsoleAppearance::BACKGROUND]).WithAlpha(1.0f));
	EXPECT_FLOAT_EQ(Preview.m_Background.r, ExpectedBackground.r);
	EXPECT_FLOAT_EQ(Preview.m_Background.g, ExpectedBackground.g);
	EXPECT_FLOAT_EQ(Preview.m_Background.b, ExpectedBackground.b);

	// 色板前八格按语义色顺序摆放，逐格比对而非只看是否非黑。
	const std::array<QmConsoleAppearance::EColor, 8> aOrder = {
		QmConsoleAppearance::BACKGROUND, QmConsoleAppearance::TEXT, QmConsoleAppearance::COMMAND, QmConsoleAppearance::PARAMETER,
		QmConsoleAppearance::STRING, QmConsoleAppearance::NUMBER, QmConsoleAppearance::LINK, QmConsoleAppearance::SEARCH};
	for(size_t i = 0; i < aOrder.size(); ++i)
	{
		const ColorRGBA Expected = color_cast<ColorRGBA>(ColorHSLA(Custom[aOrder[i]]));
		EXPECT_FLOAT_EQ(Preview.m_aSwatch[i].r, Expected.r) << "第 " << i << " 格";
		EXPECT_FLOAT_EQ(Preview.m_aSwatch[i].g, Expected.g) << "第 " << i << " 格";
		EXPECT_FLOAT_EQ(Preview.m_aSwatch[i].b, Expected.b) << "第 " << i << " 格";
	}
	// 至少有一格不是黑色，否则整体的"全黑"观感就回来了。
	const bool AnyVisible = std::any_of(Preview.m_aSwatch.begin(), Preview.m_aSwatch.end(), [](const ColorRGBA &Color) {
		return Color.r + Color.g + Color.b > 0.05f;
	});
	EXPECT_TRUE(AnyVisible);
}

// 色板必须是 16 格且每格都不透明：带 alpha 的色块会透出面板底色。
TEST(QmConsoleSchemeList, EveryPreviewSwatchHasSixteenOpaqueColors)
{
	const auto &Entries = qm_console_scheme_list::All();
	for(int Index = 0; Index < qm_console_scheme_list::Count(); ++Index)
	{
		const auto Preview = qm_console_scheme_list::BuildPreview(Entries[Index], 100, {});
		for(int ColorIndex = 0; ColorIndex < 16; ++ColorIndex)
			EXPECT_FLOAT_EQ(Preview.m_aSwatch[ColorIndex].a, 1.0f);
	}
}

// 配置被外部改成越界值时，列表要明确报告找不到，界面据此回退而不是崩。
TEST(QmConsoleSchemeList, UnknownValueIsNotInTheList)
{
	EXPECT_EQ(qm_console_scheme_list::IndexOf(-1), -1);
	EXPECT_EQ(qm_console_scheme_list::IndexOf(qm_console_scheme_list::Count() + 100), -1);
	EXPECT_EQ(qm_console_scheme_list::IndexOf(QmConsoleAppearance::k_BuiltinSchemeBase + QmConsoleSchemes::Count()), -1);
}
