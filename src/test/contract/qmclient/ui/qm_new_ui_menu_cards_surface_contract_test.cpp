// QmNewUi 菜单源码合同：设置卡片视觉表面与共享样式助手。
// 卡片甲板状态/生命周期合同见 qm_new_ui_menu_cards_deck_contract_test.cpp。
#include <engine/client/plausible_sizes.h>
#include <engine/client/rounded_rect_geometry.h>
#include <engine/storage.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/camera.h>
#include <game/client/components/controls.h>
#include <game/client/components/menus.h>
#include <game/client/components/nameplate_text_effects.h>
#include <game/client/components/nameplates.h>
#include <game/client/components/qmclient/axiom_auto_login.h>
#include <game/client/components/tclient/statusbar.h>
#include <game/client/components/tooltips.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>
#include <test/test.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

namespace
{

	[[maybe_unused]] size_t MatchingBrace(const std::string &Source, size_t BodyStart)
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

	[[maybe_unused]] size_t CountRoundedRectDirectCalls(const std::string &Source)
	{
		const std::regex CallRegex("Graphics\\(\\)->DrawRect(Ext|Ext4|4)?\\([^;]{0,260}IGraphics::CORNER_(ALL|TL|TR|BL|BR|L|R|T|B)");
		size_t Count = 0;
		for(std::sregex_iterator It(Source.begin(), Source.end(), CallRegex), End; It != End; ++It)
			++Count;
		return Count;
	}

} // namespace

TEST(QmNewUiMenuCardsSurfaceContract, QmSettingsCardsUseSharedStyleHelpers)
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
}

TEST(QmNewUiMenuCardsSurfaceContract, SettingsCardUsesOneCanonicalSurfaceWithoutLegacyGlass)
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

TEST(QmNewUiMenuCardsSurfaceContract, SettingsTransitionsDoNotChangePageBrightness)
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
