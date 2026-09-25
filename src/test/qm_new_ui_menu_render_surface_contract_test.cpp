// QmNewUi 菜单源码合同：render 圆角表面几何与共享绘制路径。运行时行为保留在 qm_new_ui_menu_branch_test.cpp.
#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>
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
#include <test/qmclient_source_contract_test.h>
#include <test/test.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <regex>
#include <sstream>
#include <string>

namespace
{

	[[maybe_unused]] size_t CountRoundedRectDirectCalls(const std::string &Source)
	{
		const std::regex CallRegex("Graphics\\(\\)->DrawRect(Ext|Ext4|4)?\\([^;]{0,260}IGraphics::CORNER_(ALL|TL|TR|BL|BR|L|R|T|B)");
		size_t Count = 0;
		for(std::sregex_iterator It(Source.begin(), Source.end(), CallRegex), End; It != End; ++It)
			++Count;
		return Count;
	}

} // namespace

TEST(QmNewUiMenuRenderSurfaceContract, ShutdownReleasesUiResourcesBeforeRendererProviders)
{
	const std::string NameplatesSource = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string NameplatesHeader = ReadTextFile("src/game/client/components/nameplates.h");
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string UiHeader = ReadTextFile("src/game/client/ui.h");
	const std::string GameClientSource = ReadTextFile("src/game/client/gameclient.cpp");

	const std::string NameplatesShutdown = FunctionBody(NameplatesSource, "void CNamePlates::OnShutdown()");
	const std::string NameplatesDestructor = FunctionBody(NameplatesSource, "CNamePlates::~CNamePlates()");
	ASSERT_FALSE(NameplatesShutdown.empty());
	ASSERT_FALSE(NameplatesDestructor.empty());
	EXPECT_NE(NameplatesHeader.find("void OnShutdown() override;"), std::string::npos);
	EXPECT_NE(NameplatesShutdown.find("ResetNamePlates();"), std::string::npos);
	EXPECT_NE(NameplatesShutdown.find("ResetChatBubbleAnimState(i, true);"), std::string::npos);
	EXPECT_EQ(NameplatesDestructor.find("ResetNamePlates"), std::string::npos);
	EXPECT_EQ(NameplatesDestructor.find("TextRender"), std::string::npos);

	const std::string UiShutdown = FunctionBody(UiSource, "void CUi::OnShutdown()");
	const std::string UiDestructor = FunctionBody(UiSource, "CUi::~CUi()");
	ASSERT_FALSE(UiShutdown.empty());
	ASSERT_FALSE(UiDestructor.empty());
	EXPECT_NE(UiHeader.find("void OnShutdown();"), std::string::npos);
	EXPECT_NE(UiShutdown.find("OnElementsReset();"), std::string::npos);
	EXPECT_NE(UiShutdown.find("if(m_pGraphics == nullptr || m_pTextRender == nullptr)"), std::string::npos);
	EXPECT_EQ(UiDestructor.find("Graphics()"), std::string::npos);
	EXPECT_EQ(UiDestructor.find("TextRender()"), std::string::npos);

	const std::string GameClientShutdown = FunctionBody(GameClientSource, "void CGameClient::OnShutdown()");
	ASSERT_FALSE(GameClientShutdown.empty());
	const size_t ComponentsShutdown = GameClientShutdown.find("pComponent->OnShutdown();");
	const size_t UiShutdownCall = GameClientShutdown.find("m_UI.OnShutdown();");
	ASSERT_NE(ComponentsShutdown, std::string::npos);
	ASSERT_NE(UiShutdownCall, std::string::npos);
	EXPECT_LT(ComponentsShutdown, UiShutdownCall);
}

TEST(QmNewUiMenuRenderSurfaceContract, AudioPackRefreshUsesPhosphorFontIconButton)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Sound = FunctionBody(Source, "void CMenus::RenderSettingsSound(CUIRect MainView)");
	ASSERT_FALSE(Sound.empty());
	EXPECT_NE(Sound.find("Ui()->DoButton_QmIcon(&s_AudioPackRefreshButton, EQmIcon::ARROW_ROTATE_RIGHT, FONT_ICON_ARROW_ROTATE_RIGHT"), std::string::npos);
	EXPECT_EQ(Sound.find("DoButton_Menu(&s_AudioPackRefreshButton, FONT_ICON_ARROW_ROTATE_RIGHT"), std::string::npos);
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string FontIconButton = FunctionBody(UiSource, "void CUi::DrawButton_FontIcon");
	EXPECT_NE(FontIconButton.find("ConfiguredQmUiIconColor(TextRender()->DefaultTextColor())"), std::string::npos);
	EXPECT_NE(FontIconButton.find("SetFontPreset(EFontPreset::ICON_FONT)"), std::string::npos);
	EXPECT_NE(FontIconButton.find("SetRenderFlags(PreviousFlags)"), std::string::npos);
	EXPECT_NE(FontIconButton.find("TextColor(PreviousColor)"), std::string::npos);
	const std::string TextSource = ReadTextFile("src/engine/client/text.cpp");
	EXPECT_NE(TextSource.find("m_IconBoldFace = m_IconRegularFace;"), std::string::npos);
	EXPECT_NE(TextSource.find("falling back to regular"), std::string::npos);
}

TEST(QmNewUiMenuRenderSurfaceContract, GraphicsIconCardSupportsDynamicCustomColorAndFourWeights)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Graphics = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(Graphics.empty());
	EXPECT_NE(Graphics.find("s_aGraphicsIconColorButtons[4]"), std::string::npos);
	EXPECT_NE(Graphics.find("s_aGraphicsIconWeightButtons[5]"), std::string::npos);
	EXPECT_NE(Graphics.find("Localize(\"Custom\")"), std::string::npos);
	EXPECT_NE(Graphics.find("Localize(\"Rainbow\")"), std::string::npos);
	// Thin 未随包字体，设置页不再提供该样式。
	EXPECT_EQ(Graphics.find("Localize(\"Thin\")"), std::string::npos);
	EXPECT_NE(Graphics.find("Localize(\"Fill\")"), std::string::npos);
	// 图标风格分段控件：索引 -> 配置值表必须唯一一份、由绘制与点击路径共用。
	// 历史上点击路径残留了含 Thin 的 6 项旧表，导致点击整体错位一位。
	EXPECT_NE(Source.find("constexpr int s_aIconWeightValues[] = {4, 0, 1, 3, 5};"), std::string::npos);
	EXPECT_EQ(Source.find("{2, 0, 1, 3, 4, 5}"), std::string::npos);
	EXPECT_NE(Source.find("QmIconWeightSegmentIndex(g_Config.m_QmUiIconWeight)"), std::string::npos);
	EXPECT_NE(Source.find("const int NewWeight = s_aIconWeightValues[NewValue];"), std::string::npos);
	EXPECT_NE(Graphics.find("DoLine_ColorPicker(&s_GraphicsIconCustomColorResetId"), std::string::npos);
	EXPECT_NE(Graphics.find("vCards.back().m_MeasureRevision = static_cast<uint64_t>(g_Config.m_QmUiIconColor == 3) |"), std::string::npos);
	EXPECT_NE(Graphics.find("vCards.back().m_PreLayoutInput = [this, GraphicsMetrics]"), std::string::npos);
	EXPECT_NE(Graphics.find("g_Config.m_QmUiIconColor == 3 && NormalizeQmIconWeight"), std::string::npos);
	EXPECT_NE(Graphics.find("g_Config.m_QmUiIconColor == 3 || NormalizeQmIconWeight"), std::string::npos);
	EXPECT_NE(Graphics.find("std::initializer_list<float>{GraphicsMetrics.m_LineHeight, GraphicsMetrics.m_ButtonHeight, GraphicsMetrics.m_LineHeight}"), std::string::npos);

	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	EXPECT_NE(Config.find("MACRO_CONFIG_COL(QmUiIconCustomColor, qm_ui_icon_custom_color"), std::string::npos);
	EXPECT_NE(Config.find("Qm UI icon color: 1=White, 2=Black, 3=Custom, 4=Rainbow"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_COL(QmUiIconDuotoneSecondaryColor, qm_ui_icon_duotone_secondary_color"), std::string::npos);
}

TEST(QmNewUiMenuRenderSurfaceContract, RoundedUiSurfacesUseClampedGeometryAndSharedPaths)
{
	const SRoundedRectGeometry Geometry = ResolveRoundedRectGeometry(0.24f, 0.74f, 10.32f, 4.19f, 3.9f, 0.5f);
	EXPECT_NEAR(Geometry.m_X, 0.0f, 1e-6f);
	EXPECT_NEAR(Geometry.m_Y, 0.5f, 1e-6f);
	EXPECT_NEAR(Geometry.m_W, 10.5f, 1e-6f);
	EXPECT_NEAR(Geometry.m_H, 4.5f, 1e-6f);
	EXPECT_NEAR(Geometry.m_Rounding, 2.25f, 1e-6f);
	const SRoundedRectGeometry SmallGeometry = ResolveRoundedRectGeometry(0.24f, 0.24f, 0.51f, 0.51f, 0.4f, 0.5f);
	EXPECT_NEAR(SmallGeometry.m_W, 1.0f, 1e-6f);
	EXPECT_NEAR(SmallGeometry.m_H, 1.0f, 1e-6f);
	EXPECT_NEAR(SmallGeometry.m_Rounding, 0.5f, 1e-6f);
	const SRoundedRectGeometry InvalidGeometry = ResolveRoundedRectGeometry(0.5f, 0.5f, 0.0f, 4.0f, 3.0f, 0.5f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_X, 0.5f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_Y, 0.5f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_W, 0.0f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_H, 4.0f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_Rounding, 0.0f);

	const CUIRect Rect{0.2f, 0.2f, 20.0f, 10.0f};
	SRoundedSurfaceParams SdfParams;
	SdfParams.m_Radius = 8.0f;
	SdfParams.m_BorderWidth = 0.6f;
	SdfParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan Sdf = ResolveRoundedSurfacePlan(Rect, SdfParams, true);
	EXPECT_TRUE(Sdf.m_UseSdf);
	EXPECT_FLOAT_EQ(Sdf.m_Rect.x, 0.0f);
	EXPECT_FLOAT_EQ(Sdf.m_Rect.y, 0.0f);
	EXPECT_FLOAT_EQ(Sdf.m_Rect.w, 20.0f);
	EXPECT_FLOAT_EQ(Sdf.m_Rect.h, 10.0f);
	EXPECT_FLOAT_EQ(Sdf.m_Radius, 5.0f);
	EXPECT_FLOAT_EQ(Sdf.m_BorderWidth, 0.5f);
	EXPECT_FLOAT_EQ(Sdf.m_PixelSize, 0.5f);
	EXPECT_FLOAT_EQ(Sdf.m_CornerRadii.x, 5.0f);
	EXPECT_FLOAT_EQ(Sdf.m_CornerRadii.y, 5.0f);
	EXPECT_FLOAT_EQ(Sdf.m_CornerRadii.z, 5.0f);
	EXPECT_FLOAT_EQ(Sdf.m_CornerRadii.w, 5.0f);
	SRoundedSurfaceParams NonIntegerPixelParams;
	NonIntegerPixelParams.m_Radius = 3.9f;
	NonIntegerPixelParams.m_BorderWidth = 0.6f;
	NonIntegerPixelParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan NonIntegerPixelPlan = ResolveRoundedSurfacePlan(CUIRect{0.24f, 0.74f, 10.32f, 4.19f}, NonIntegerPixelParams, true);
	EXPECT_NEAR(NonIntegerPixelPlan.m_Rect.x, 0.0f, 1e-6f);
	EXPECT_NEAR(NonIntegerPixelPlan.m_Rect.y, 0.5f, 1e-6f);
	EXPECT_NEAR(NonIntegerPixelPlan.m_Rect.w, 10.5f, 1e-6f);
	EXPECT_NEAR(NonIntegerPixelPlan.m_Rect.h, 4.5f, 1e-6f);
	SRoundedSurfaceParams OnePhysicalPixelParams;
	OnePhysicalPixelParams.m_Radius = 0.4f;
	OnePhysicalPixelParams.m_BorderWidth = 0.4f;
	OnePhysicalPixelParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan OnePhysicalPixelPlan = ResolveRoundedSurfacePlan(CUIRect{0.24f, 0.24f, 0.51f, 0.51f}, OnePhysicalPixelParams, true);
	EXPECT_NEAR(OnePhysicalPixelPlan.m_Rect.w, 1.0f, 1e-6f);
	EXPECT_NEAR(OnePhysicalPixelPlan.m_Rect.h, 1.0f, 1e-6f);
	EXPECT_NEAR(OnePhysicalPixelPlan.m_Radius, 0.5f, 1e-6f);
	EXPECT_NEAR(OnePhysicalPixelPlan.m_BorderWidth, 0.5f, 1e-6f);
	const auto ExpectCornerRadii = [](const SRoundedSurfacePlan &Plan, const float Tl, const float Tr, const float Br, const float Bl) {
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.x, Tl);
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.y, Tr);
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.z, Br);
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.w, Bl);
	};

	SRoundedSurfaceParams PartialParams;
	PartialParams.m_Radius = 4.0f;
	PartialParams.m_BorderWidth = 1.0f;
	PartialParams.m_PixelSize = 0.5f;
	PartialParams.m_Corners = IGraphics::CORNER_R;
	const SRoundedSurfacePlan Partial = ResolveRoundedSurfacePlan(Rect, PartialParams, true);
	EXPECT_TRUE(Partial.m_UseSdf);
	ExpectCornerRadii(Partial, 0.0f, 4.0f, 4.0f, 0.0f);
	SRoundedSurfaceParams LeftParams;
	LeftParams.m_Radius = 4.0f;
	LeftParams.m_BorderWidth = 1.0f;
	LeftParams.m_PixelSize = 0.5f;
	LeftParams.m_Corners = IGraphics::CORNER_L;
	const SRoundedSurfacePlan Left = ResolveRoundedSurfacePlan(Rect, LeftParams, true);
	EXPECT_TRUE(Left.m_UseSdf);
	ExpectCornerRadii(Left, 4.0f, 0.0f, 0.0f, 4.0f);
	const auto ExpectMask = [&](const int Corners, const float Tl, const float Tr, const float Br, const float Bl) {
		SRoundedSurfaceParams Params;
		Params.m_Radius = 4.0f;
		Params.m_BorderWidth = 1.0f;
		Params.m_PixelSize = 0.5f;
		Params.m_Corners = Corners;
		const SRoundedSurfacePlan Plan = ResolveRoundedSurfacePlan(Rect, Params, true);
		EXPECT_TRUE(Plan.m_UseSdf);
		ExpectCornerRadii(Plan, Tl, Tr, Br, Bl);
	};
	ExpectMask(IGraphics::CORNER_T, 4.0f, 4.0f, 0.0f, 0.0f);
	ExpectMask(IGraphics::CORNER_B, 0.0f, 0.0f, 4.0f, 4.0f);
	ExpectMask(IGraphics::CORNER_TL, 4.0f, 0.0f, 0.0f, 0.0f);
	ExpectMask(IGraphics::CORNER_TR, 0.0f, 4.0f, 0.0f, 0.0f);
	ExpectMask(IGraphics::CORNER_BR, 0.0f, 0.0f, 4.0f, 0.0f);
	ExpectMask(IGraphics::CORNER_BL, 0.0f, 0.0f, 0.0f, 4.0f);
	ExpectMask(IGraphics::CORNER_NONE, 0.0f, 0.0f, 0.0f, 0.0f);
	SRoundedSurfaceParams WideBorderParams;
	WideBorderParams.m_Radius = 2.0f;
	WideBorderParams.m_BorderWidth = 4.0f;
	WideBorderParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan WideBorder = ResolveRoundedSurfacePlan(Rect, WideBorderParams, true);
	EXPECT_FLOAT_EQ(WideBorder.m_Radius, 2.0f);
	EXPECT_FLOAT_EQ(WideBorder.m_BorderWidth, 4.0f);
	SRoundedSurfaceParams SwallowedInteriorParams;
	SwallowedInteriorParams.m_Radius = 8.0f;
	SwallowedInteriorParams.m_BorderWidth = 9.0f;
	SwallowedInteriorParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan SwallowedInterior = ResolveRoundedSurfacePlan(CUIRect{0.0f, 0.0f, 6.0f, 4.0f}, SwallowedInteriorParams, true);
	EXPECT_FLOAT_EQ(SwallowedInterior.m_Radius, 2.0f);
	EXPECT_FLOAT_EQ(SwallowedInterior.m_BorderWidth, 2.0f);
	SRoundedSurfaceParams UnsupportedParams;
	UnsupportedParams.m_Radius = 4.0f;
	UnsupportedParams.m_BorderWidth = 1.0f;
	UnsupportedParams.m_PixelSize = 0.0f;
	const SRoundedSurfacePlan Unsupported = ResolveRoundedSurfacePlan(Rect, UnsupportedParams, false);
	EXPECT_FALSE(Unsupported.m_UseSdf);
	EXPECT_FLOAT_EQ(Unsupported.m_PixelSize, 0.0001f);

	const std::string Buttons = ReadTextFile("src/game/client/QmUi/UiButtons.cpp");
	const std::string Forms = ReadTextFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Surface = ReadTextFile("src/game/client/QmUi/UiSurface.cpp");
	const std::string SurfaceHeader = ReadTextFile("src/game/client/QmUi/UiSurface.h");
	const std::string UiRect = ReadTextFile("src/game/client/ui_rect.cpp");
	const std::string Containers = ReadTextFile("src/game/client/QmUi/UiContainers.h");
	const std::string Overlays = ReadTextFile("src/game/client/QmUi/UiOverlays.h");
	const std::string Ui = ReadTextFile("src/game/client/ui.cpp") + ReadTextFile("src/game/client/ui_popups.cpp");
	const std::string Menus = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string IngameMenus = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const std::string QmClientMenus = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string TClientMenus = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string ScrollRegion = ReadTextFile("src/game/client/ui_scrollregion.cpp");
	const std::string ImePopup = ReadTextFile("src/game/client/qm_ime_candidate_popup.cpp");
	const std::string Editor = ReadTextFile("src/game/editor/editor_ui.cpp");
	EXPECT_NE(Buttons.find("DrawRoundedSurface("), std::string::npos);
	EXPECT_NE(Forms.find("DrawRoundedSurface("), std::string::npos);
	EXPECT_NE(SurfaceHeader.find("vec4 m_CornerRadii{};"), std::string::npos);
	EXPECT_NE(SurfaceHeader.find("struct SRoundedSurfaceParams"), std::string::npos);
	EXPECT_NE(SurfaceHeader.find("const SRoundedSurfaceParams &Params"), std::string::npos);
	EXPECT_EQ(SurfaceHeader.find("float PixelSize, int Corners"), std::string::npos);
	EXPECT_NE(SurfaceHeader.find("ResolveRoundedSurfaceCornerRadii"), std::string::npos);
	EXPECT_NE(SurfaceHeader.find("Plan.m_UseSdf = HasSdf"), std::string::npos);
	EXPECT_NE(Surface.find("Params.m_CornerRadii = Plan.m_CornerRadii;"), std::string::npos);
	EXPECT_NE(Surface.find("Params.m_Params = vec4(Plan.m_BorderWidth, Plan.m_PixelSize, Plan.m_PixelSize * 2.0f, 0.0f);"), std::string::npos);
	EXPECT_NE(UiRect.find("DrawRoundedSurface(ms_pGraphics, *this"), std::string::npos);
	EXPECT_NE(UiRect.find("const float PixelSize = CurrentPixelSize(ms_pGraphics);"), std::string::npos);
	EXPECT_NE(UiRect.find("SRoundedSurfaceParams Params;"), std::string::npos);
	EXPECT_NE(UiRect.find("Params.m_PixelSize = PixelSize;"), std::string::npos);
	EXPECT_NE(UiRect.find("DrawRoundedSurface(ms_pGraphics, *this, Color, ColorRGBA(), Params)"), std::string::npos);
	EXPECT_EQ(UiRect.find("Rounding, 0.0f, PixelSize, Corners"), std::string::npos);
	EXPECT_EQ(UiRect.find("ResolveRoundedRectGeometry(x, y, w, h, Rounding"), std::string::npos);
	EXPECT_NE(Ui.find("DrawRoundedSurface(this, ClearButton"), std::string::npos);
	EXPECT_NE(FunctionBody(Ui, "bool CUi::DoEditBox(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, int Corners, const std::vector<STextColorSplit> &vColorSplits, int Align, const SEditBoxRenderOptions &RenderOptions)").find("DrawRoundedSurface(this, *pRect"), std::string::npos);
	EXPECT_NE(FunctionBody(Ui, "SEditResult<int64_t> CUi::DoValueSelectorWithState").find("DrawRoundedSurface(this, *pRect"), std::string::npos);
	EXPECT_NE(FunctionBody(Ui, "void CUi::DrawButton_FontIcon").find("DrawRoundedSurface(this, *pRect"), std::string::npos);
	EXPECT_NE(FunctionBody(Ui, "void CUi::RenderPopupMenus").find("SPopupMenu::POPUP_BORDER"), std::string::npos);
	EXPECT_NE(FunctionBody(Ui, "float CUi::DoScrollbarV").find("DrawRoundedSurface(this, Rail"), std::string::npos);
	EXPECT_NE(FunctionBody(Ui, "void CUi::RenderProgressBar").find("DrawRoundedSurface(this, ProgressBar"), std::string::npos);
	EXPECT_NE(IngameMenus.find("#include <game/client/QmUi/UiSurface.h>"), std::string::npos);
	EXPECT_NE(FunctionBody(IngameMenus, "void CMenus::RenderServerControl(CUIRect MainView)").find("DrawRoundedSurface(Ui(), MainView, ms_ColorTabbarActive, ms_ColorTabbarActive, 10.0f, 0.0f, IGraphics::CORNER_B);"), std::string::npos);
	const std::string ColorPicker = FunctionBody(Ui, "CUi::EPopupMenuFunctionResult CUi::PopupColorPicker");
	EXPECT_NE(ColorPicker.find("const CUIRect ColorMarker{MarkerX - 4.5f, MarkerY - 4.5f, 9.0f, 9.0f};"), std::string::npos);
	EXPECT_NE(ColorPicker.find("DrawRoundedSurface(pUI, ColorMarker, PickerColorRGB, MarkerOutline, 4.5f, 1.0f);"), std::string::npos);
	EXPECT_EQ(ColorPicker.find("DrawCircle(MarkerX"), std::string::npos);
	EXPECT_NE(ColorPicker.find("DrawRoundedSurface(pUI, HueMarker, HueMarkerColor, HueMarkerOutline, 1.2f, 1.2f);"), std::string::npos);
	EXPECT_EQ(ColorPicker.find("HueMarker.Draw("), std::string::npos);
	EXPECT_NE(FunctionBody(Ui, "int CUi::DoButton_Menu").find("const bool UseRoundedRectSdf = Graphics()->HasRoundedRectSdf();"), std::string::npos);
	EXPECT_NE(FunctionBody(Ui, "int CUi::DoButton_Menu").find("if(!UseRoundedRectSdf)"), std::string::npos);
	EXPECT_NE(Menus.find("DrawRoundedSurface(Ui(), *pRect"), std::string::npos);
	EXPECT_NE(Containers.find("DrawRoundedSurface(Ctx, Shadow"), std::string::npos);
	EXPECT_LT(Containers.find("DrawRoundedSurface(Ctx, BorderBg"), Containers.find("DrawRoundedSurface(Ctx, Rect, Props.m_FillColor"));
	EXPECT_NE(Containers.find("BorderBg.Margin(-1.0f, &BorderBg);"), std::string::npos);
	EXPECT_NE(Containers.find("DrawRoundedSurface(Ctx, Rect, Props.m_FillColor"), std::string::npos);
	EXPECT_EQ(Containers.find("BorderBg.Draw"), std::string::npos);
	EXPECT_NE(Overlays.find("DrawRoundedSurface(Ctx, ShadowRect"), std::string::npos);
	EXPECT_NE(Overlays.find("DrawRoundedSurface(Ctx, ToastRect"), std::string::npos);
	EXPECT_NE(FunctionBody(ScrollRegion, "void CScrollRegion::DrawBackground(const CUIRect &ScrollbarBg)").find("DrawRoundedSurface(Ui(), ScrollbarBg"), std::string::npos);
	EXPECT_NE(FunctionBody(ScrollRegion, "void CScrollRegion::DoSlider()").find("DrawRoundedSurface(Ui(), Slider"), std::string::npos);
	EXPECT_NE(QmClientMenus.find("DrawRoundedSurface(Ui(), Frame.m_Frame.m_ScrollbarTrackRect"), std::string::npos);
	EXPECT_NE(QmClientMenus.find("DrawRoundedSurface(Ui(), QrRect"), std::string::npos);
	EXPECT_NE(QmClientMenus.find("g_QmClientRenderTexture(QrRect, 1.0f)"), std::string::npos);
	EXPECT_NE(TClientMenus.find("DrawRoundedSurface(Ui(), PlayerRect, NameButtonColor"), std::string::npos);
	EXPECT_NE(TClientMenus.find("DrawRoundedSurface(Ui(), ClanRect, ClanButtonColor"), std::string::npos);
	EXPECT_NE(TClientMenus.find("if(!ReadOnly && NameButtonColor.a > 0.0f)"), std::string::npos);
	EXPECT_NE(TClientMenus.find("if(!ReadOnly && ClanButtonColor.a > 0.0f)"), std::string::npos);
	EXPECT_NE(TClientMenus.find("DrawRoundedSurface(Ui(), PreviewRect"), std::string::npos);
	EXPECT_NE(TClientMenus.find("DrawRoundedSurface(Ui(), StatusBar"), std::string::npos);
	EXPECT_NE(TClientMenus.find("DrawRoundedSurface(Ui(), Skin"), std::string::npos);
	EXPECT_NE(ImePopup.find("DrawRoundedSurface(pGraphics, PanelDropA"), std::string::npos);
	EXPECT_NE(ImePopup.find("DrawRoundedSurface(pGraphics, DrawRect"), std::string::npos);
	EXPECT_NE(ImePopup.find("SurfaceParams.m_PixelSize = PixelSize;"), std::string::npos);
	EXPECT_NE(ImePopup.find("PanelTopLine.x += Presentation.m_Radius"), std::string::npos);
	EXPECT_NE(ImePopup.find("PanelTopLine.w = maximum(0.0f"), std::string::npos);
	EXPECT_NE(Editor.find("DrawRoundedSurface(Ui(), *pRect"), std::string::npos);
	EXPECT_NE(FunctionBody(Editor, "SEditResult<int> CEditor::UiDoValueSelector").find("DrawRoundedSurface(Ui(), *pRect"), std::string::npos);
	EXPECT_EQ(Surface.find("DrawFallbackBorderRing"), std::string::npos);
	EXPECT_EQ(Surface.find("DrawRoundedRectAntialias"), std::string::npos);
	EXPECT_NE(Surface.find("pGraphics->DrawRect(Plan.m_Rect.x, Plan.m_Rect.y, Plan.m_Rect.w, Plan.m_Rect.h, Fill, Params.m_Corners, Plan.m_Radius);"), std::string::npos);
	EXPECT_NE(Surface.find("Inner.Margin(Plan.m_BorderWidth, &Inner);"), std::string::npos);
	EXPECT_NE(Surface.find("pGraphics->DrawRect(Inner.x, Inner.y, Inner.w, Inner.h, Fill, Params.m_Corners"), std::string::npos);
	EXPECT_EQ(Surface.find("QuadsDrawFreeform"), std::string::npos);
	EXPECT_EQ(Surface.find("pUi->ClipEnable(&Clip);"), std::string::npos);
	const std::string Graphics = ReadTextFile("src/engine/client/graphics_threaded.cpp");
	const std::string DrawRect = FunctionBody(Graphics, "void CGraphics_Threaded::DrawRect(float x, float y, float w, float h, ColorRGBA Color, int Corners, float Rounding)");
	const std::string DrawRectExtAntialias = FunctionBody(Graphics, "void CGraphics_Threaded::DrawRectExtAntialias(float x, float y, float w, float h, float r, int Corners, ColorRGBA Color, bool ResolveGeometry)");
	const std::string DrawRectExt = FunctionBody(Graphics, "void CGraphics_Threaded::DrawRectExt(float x, float y, float w, float h, float r, int Corners)");
	const std::string DrawRectExt4Antialias = FunctionBody(Graphics, "void CGraphics_Threaded::DrawRectExt4Antialias(float x, float y, float w, float h, float r, int Corners, ColorRGBA ColorTopLeft, ColorRGBA ColorTopRight, ColorRGBA ColorBottomLeft, ColorRGBA ColorBottomRight, bool ResolveGeometry)");
	const std::string DrawRectExt4 = FunctionBody(Graphics, "void CGraphics_Threaded::DrawRectExt4(float x, float y, float w, float h, ColorRGBA ColorTopLeft, ColorRGBA ColorTopRight, ColorRGBA ColorBottomLeft, ColorRGBA ColorBottomRight, float r, int Corners)");
	EXPECT_NE(DrawRect.find("DrawRectExt(x, y, w, h, Rounding, Corners);"), std::string::npos);
	EXPECT_NE(Graphics.find("#include <engine/client/rounded_rect_geometry.h>"), std::string::npos);
	EXPECT_NE(DrawRectExtAntialias.find("ResolveRoundedRectGeometry(x, y, w, h, r"), std::string::npos);
	EXPECT_NE(DrawRectExt.find("ResolveRoundedRectGeometry(x, y, w, h, r"), std::string::npos);
	EXPECT_NE(DrawRectExt4Antialias.find("ResolveRoundedRectGeometry(x, y, w, h, r"), std::string::npos);
	EXPECT_NE(DrawRectExt4.find("ResolveRoundedRectGeometry(x, y, w, h, r"), std::string::npos);
	EXPECT_NE(DrawRectExt.find("DrawRectExtAntialias(x, y, w, h, r, Corners, CommandColorToColorRGBA(m_aColor[0]), false);"), std::string::npos);
	EXPECT_NE(DrawRectExt4.find("DrawRectExt4Antialias(x, y, w, h, r, Corners, ColorTopLeft, ColorTopRight, ColorBottomLeft, ColorBottomRight, false);"), std::string::npos);
	EXPECT_NE(FunctionBody(Graphics, "int CGraphics_Threaded::CreateRectQuadContainer(float x, float y, float w, float h, float r, int Corners)").find("ResolveRoundedRectGeometry(x, y, w, h, r"), std::string::npos);
	for(const char *pShaderPath : {"data/shader/rounded_rect_sdf.frag", "data/shader/vulkan/rounded_rect_sdf.frag"})
	{
		const std::string Shader = ReadTextFile(pShaderPath);
		EXPECT_NE(Shader.find("gRoundedRectSdfData[5]"), std::string::npos);
		EXPECT_NE(Shader.find("float CornerRadius(vec2 Point, vec4 CornerRadii)"), std::string::npos);
		EXPECT_NE(Shader.find("float SdfFeather(float DistanceValue, float PixelSize)"), std::string::npos);
		EXPECT_NE(Shader.find("return max(PixelSize, length(vec2(dFdx(DistanceValue), dFdy(DistanceValue))));"), std::string::npos);
		EXPECT_NE(Shader.find("return 1.0 - smoothstep(-Feather * 0.5, Feather * 0.5, DistanceValue);"), std::string::npos);
		EXPECT_NE(Shader.find("vec4 InnerCornerRadii = max(CornerRadii - vec4(BorderWidth), vec4(0.0));"), std::string::npos);
		EXPECT_NE(Shader.find("float BorderCoverage = max(OuterCoverage - InnerCoverage, 0.0);"), std::string::npos);
		EXPECT_NE(Shader.find("float OuterCoverage = Coverage(OuterDistance, Params.y);"), std::string::npos);
		EXPECT_NE(Shader.find("InnerCoverage = BorderWidth > 0.0 && min(InnerHalfSize.x, InnerHalfSize.y) > 0.0 ? Coverage(InnerDistance, Params.y) : 0.0;"), std::string::npos);
		EXPECT_EQ(Shader.find("* 0.8"), std::string::npos);
		EXPECT_EQ(Shader.find("* 0.9"), std::string::npos);
		EXPECT_NE(Shader.find("Rect.zw + vec2(Params.z * 2.0)"), std::string::npos);
		EXPECT_NE(Shader.find("float OutputAlpha = FillAlpha + BorderAlpha;"), std::string::npos);
		EXPECT_NE(Shader.find("vec3 Premultiplied = FillColor.rgb * FillAlpha + BorderColor.rgb * BorderAlpha;"), std::string::npos);
		EXPECT_EQ(Shader.find("BorderAlpha * (1.0 - FillAlpha)"), std::string::npos);
		EXPECT_EQ(Shader.find("mix(BorderColor, FillColor, InnerCoverage)"), std::string::npos);
	}
	// Metal 是 Apple 平台默认后端，Qm 圆角表面在 iOS/macOS 上走这条路径。
	// Metal 使用 float2/float4 + fwidth + mix 语法，因此断言同一套几何/合成语义的对应写法，
	// 而不是与 GLSL 做字面比对（字面比对会把后端语法差异误判成行为差异）。
	{
		const std::string MetalShader = ReadTextFile("data/shader/metal/qmclient.metal");
		ASSERT_FALSE(MetalShader.empty());
		// 负向断言必须限定在圆角 SDF 片段内：媒体岛片段同样使用 * 0.8/* 0.9 做羽化，
		// 全文件扫描会把它误判成圆角暗化回归。
		const std::string MetalSdfFragment = FunctionBody(MetalShader, "fragment float4 qmclient_rounded_rect_sdf_fragment(");
		ASSERT_FALSE(MetalSdfFragment.empty());
		EXPECT_NE(MetalShader.find("fragment float4 qmclient_rounded_rect_sdf_fragment("), std::string::npos);
		EXPECT_NE(MetalShader.find("constant float4 *Params [[buffer(1)]]"), std::string::npos);
		EXPECT_NE(MetalShader.find("float RoundedRectDistance(float2 Point, float2 HalfSize, float Radius)"), std::string::npos);
		EXPECT_NE(MetalShader.find("float RoundedRectCornerRadius(float2 Point, float4 CornerRadii)"), std::string::npos);
		EXPECT_NE(MetalShader.find("float RoundedRectCoverage(float DistanceValue, float PixelSize)"), std::string::npos);
		EXPECT_NE(MetalShader.find("const float Feather = max(PixelSize, length(float2(dfdx(DistanceValue), dfdy(DistanceValue))));"), std::string::npos);
		EXPECT_NE(MetalShader.find("return 1.0 - smoothstep(-Feather * 0.5, Feather * 0.5, DistanceValue);"), std::string::npos);
		EXPECT_NE(MetalShader.find("const float4 InnerCornerRadii = max(CornerRadii - float4(BorderWidth), float4(0.0));"), std::string::npos);
		EXPECT_NE(MetalShader.find("const float BorderCoverage = max(OuterCoverage - InnerCoverage, 0.0);"), std::string::npos);
		EXPECT_NE(MetalShader.find("const float OuterCoverage = RoundedRectCoverage(OuterDistance, RenderParams.y);"), std::string::npos);
		EXPECT_NE(MetalShader.find("const float InnerCoverage = BorderWidth > 0.0 && min(InnerHalfSize.x, InnerHalfSize.y) > 0.0 ? RoundedRectCoverage(InnerDistance, RenderParams.y) : 0.0;"), std::string::npos);
		EXPECT_NE(MetalShader.find("Rect.zw + float2(RenderParams.z * 2.0)"), std::string::npos);
		EXPECT_NE(MetalShader.find("const float OutputAlpha = FillAlpha + BorderAlpha;"), std::string::npos);
		EXPECT_NE(MetalShader.find("const float3 Premultiplied = FillColor.rgb * FillAlpha + BorderColor.rgb * BorderAlpha;"), std::string::npos);
		// 与 OpenGL/Vulkan 一致：圆角 SDF 不做 0.8/0.9 暗化，也不做非预乘的 border/fill 插值。
		EXPECT_EQ(MetalSdfFragment.find("* 0.8"), std::string::npos);
		EXPECT_EQ(MetalSdfFragment.find("* 0.9"), std::string::npos);
		EXPECT_EQ(MetalSdfFragment.find("BorderAlpha * (1.0 - FillAlpha)"), std::string::npos);
		EXPECT_EQ(MetalSdfFragment.find("mix(BorderColor, FillColor, InnerCoverage)"), std::string::npos);
	}
	const auto NormalizeSdfCore = [](std::string Shader) {
		const size_t CoreStart = Shader.find("float RoundedRectSdf");
		if(CoreStart == std::string::npos)
			return std::string{};
		Shader.erase(0, CoreStart);
		const std::string VulkanDataPrefix = "gSdf.gRoundedRectSdfData";
		const std::string OpenGlDataPrefix = "gRoundedRectSdfData";
		size_t Position = 0;
		while((Position = Shader.find(VulkanDataPrefix, Position)) != std::string::npos)
		{
			Shader.replace(Position, VulkanDataPrefix.size(), OpenGlDataPrefix);
			Position += OpenGlDataPrefix.size();
		}
		return Shader;
	};
	const std::string OpenGlSdfShader = ReadTextFile("data/shader/rounded_rect_sdf.frag");
	const std::string VulkanSdfShader = ReadTextFile("data/shader/vulkan/rounded_rect_sdf.frag");
	EXPECT_EQ(NormalizeSdfCore(OpenGlSdfShader), NormalizeSdfCore(VulkanSdfShader));

	// 跨后端几何核心必须一致：从 SDF 距离函数体里抽出「返回表达式」，
	// 把 Metal 的 float2 语法归一化到 GLSL 的 vec2，再逐字符比对。
	// 这能拦住只改一个后端的圆角/边界数学漂移，而不会把后端方言差异当成回归。
	const auto ExtractSdfReturnExpression = [](const std::string &Shader, const char *pSignature) {
		const size_t SignaturePos = Shader.find(pSignature);
		if(SignaturePos == std::string::npos)
			return std::string{};
		const size_t BodyStart = Shader.find('{', SignaturePos);
		if(BodyStart == std::string::npos)
			return std::string{};
		const size_t BodyEnd = Shader.find('}', BodyStart);
		if(BodyEnd == std::string::npos)
			return std::string{};
		std::string Body = Shader.substr(BodyStart, BodyEnd - BodyStart);
		const size_t ReturnPos = Body.find("return ");
		if(ReturnPos == std::string::npos)
			return std::string{};
		std::string Expression = Body.substr(ReturnPos + 7);
		Expression.erase(std::remove_if(Expression.begin(), Expression.end(), [](unsigned char Character) {
			return Character == ' ' || Character == '\t' || Character == '\n' || Character == '\r';
		}),
			Expression.end());
		// 统一向量类型写法：Metal 的 float2/float4 与 GLSL 的 vec2/vec4 语义相同。
		const char *const apFromTypes[] = {"float2", "float3", "float4"};
		const char *const apToTypes[] = {"vec2", "vec3", "vec4"};
		for(size_t TypeIndex = 0; TypeIndex < std::size(apFromTypes); ++TypeIndex)
		{
			const std::string From = apFromTypes[TypeIndex];
			const std::string To = apToTypes[TypeIndex];
			size_t Position = 0;
			while((Position = Expression.find(From, Position)) != std::string::npos)
			{
				Expression.replace(Position, From.size(), To);
				Position += To.size();
			}
		}
		return Expression;
	};
	const std::string MetalSdfShader = ReadTextFile("data/shader/metal/qmclient.metal");
	ASSERT_FALSE(MetalSdfShader.empty());
	EXPECT_EQ(
		ExtractSdfReturnExpression(OpenGlSdfShader, "float RoundedRectSdf(vec2 Point, vec2 HalfSize, float Radius)"),
		ExtractSdfReturnExpression(MetalSdfShader, "float RoundedRectDistance(float2 Point, float2 HalfSize, float Radius)"));
	EXPECT_EQ(
		ExtractSdfReturnExpression(OpenGlSdfShader, "float CornerRadius(vec2 Point, vec4 CornerRadii)"),
		ExtractSdfReturnExpression(MetalSdfShader, "float RoundedRectCornerRadius(float2 Point, float4 CornerRadii)"));

	const std::string GraphicsHeader = ReadTextFile("src/engine/graphics.h");
	const std::string GraphicsThreaded = ReadTextFile("src/engine/client/graphics_threaded.cpp");
	const std::string OpenGl = ReadTextFile("src/engine/client/backend/opengl/backend_opengl3.cpp");
	const std::string Vulkan = ReadTextFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	EXPECT_NE(GraphicsHeader.find("static_assert(sizeof(SRoundedRectSdfParams) == sizeof(vec4) * 5);"), std::string::npos);
	EXPECT_NE(OpenGl.find("SetUniformVec4(m_pRoundedRectSdfProgram->m_LocData, 5"), std::string::npos);
	EXPECT_NE(FunctionBody(Vulkan, "[[nodiscard]] bool Cmd_RenderRoundedRectSdf").find("&pCommand->m_Params, sizeof(pCommand->m_Params)"), std::string::npos);
	const std::string RoundedCommand = FunctionBody(GraphicsThreaded, "void CGraphics_Threaded::RenderRoundedRectSdf");
	EXPECT_NE(RoundedCommand.find("Params.m_Params.z"), std::string::npos);
	EXPECT_NE(RoundedCommand.find("if(m_NumVertices > 0)"), std::string::npos);
	EXPECT_NE(RoundedCommand.find("FlushVertices();"), std::string::npos);
	EXPECT_NE(RoundedCommand.find("m_RoundedRectSdfFlushCount++"), std::string::npos);
	EXPECT_NE(RoundedCommand.find("m_RoundedRectSdfCommandCount++"), std::string::npos);
	EXPECT_NE(GraphicsThreaded.find("rounded_sdf_commands_sum"), std::string::npos);
	EXPECT_NE(GraphicsThreaded.find("rounded_sdf_flushes_sum"), std::string::npos);
	EXPECT_NE(GraphicsThreaded.find("m_RoundedRectSdfCommandCount = 0;"), std::string::npos);
	EXPECT_NE(GraphicsThreaded.find("m_RoundedRectSdfFlushCount = 0;"), std::string::npos);
}

TEST(QmNewUiMenuRenderSurfaceContract, OrdinaryUiRoundedSurfacesUseSharedPath)
{
	const std::string Appearance = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Effects = ReadTextFile("src/game/client/components/ui_effects.cpp");
	const std::string HudEditor = ReadTextFile("src/game/client/components/hud_editor.cpp");
	const std::string TClientMenus = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Chat = ReadTextFile("src/game/client/components/chat.cpp");

	EXPECT_NE(Appearance.find("DrawRoundedSurface(Ui(), MessageBackground"), std::string::npos);
	EXPECT_EQ(Appearance.find("Graphics()->DrawRectExt(PreviewView"), std::string::npos);
	EXPECT_NE(Effects.find("DrawRoundedSurface(Ui(), ShadowRect"), std::string::npos);
	EXPECT_EQ(Effects.find("Graphics()->DrawRect(PreviewX + ShadowOffset"), std::string::npos);
	EXPECT_NE(HudEditor.find("DrawRoundedSurface(Ui(), HelpRect"), std::string::npos);
	EXPECT_EQ(HudEditor.find("Graphics()->DrawRect(HelpX, HelpY"), std::string::npos);
	EXPECT_NE(TClientMenus.find("DrawRoundedSurface(Ui(), BodyColor"), std::string::npos);
	EXPECT_NE(TClientMenus.find("DrawRoundedSurface(Ui(), FeetColor"), std::string::npos);
	// 聊天滚动条和实时预览仍属于高频绘制，保留批量直绘路径。
	EXPECT_NE(Chat.find("Graphics()->DrawRect(ScrollbarRect.x"), std::string::npos);
	EXPECT_NE(Chat.find("Graphics()->DrawRect(x, PreviewY"), std::string::npos);
}

TEST(QmNewUiMenuRenderSurfaceContract, LegacyRoundedRectDrawSitesRequireExplicitAllowlist)
{
	const char *const apAllowlistedFiles[] = {
		"src/game/client/components/hud.cpp",
		"src/game/client/components/chat.cpp",
		"src/game/client/components/nameplates.cpp",
		"src/game/client/components/spectator.cpp",
		"src/game/client/components/statboard.cpp",
	};
	for(const char *pPath : apAllowlistedFiles)
	{
		const std::string Source = ReadTextFile(pPath);
		EXPECT_GT(CountRoundedRectDirectCalls(Source), 0u) << pPath;
	}

	const char *const apOrdinaryUiFiles[] = {
		"src/game/client/components/menus.cpp",
		"src/game/client/components/menus_ingame.cpp",
		"src/game/client/components/menus_start.cpp",
		"src/game/client/components/menus_browser.cpp",
		"src/game/client/components/menus_demo.cpp",
		"src/game/client/components/menus_settings.cpp",
		"src/game/client/components/menus_settings7.cpp",
		"src/game/client/components/menus_settings_assets.cpp",
		"src/game/client/components/menus_settings_controls.cpp",
		"src/game/client/components/tclient/menus_tclient.cpp",
		"src/game/client/components/qmclient/menus_qmclient.cpp",
		"src/game/client/components/ui_effects.cpp",
		"src/game/client/components/hud_editor.cpp",
		"src/game/client/ui.cpp",
		"src/game/client/ui_popups.cpp",
		"src/game/client/QmUi/UiButtons.cpp",
		"src/game/client/QmUi/UiForms.cpp",
		"src/game/client/QmUi/SettingsCard.cpp",
		"src/game/client/QmUi/SettingsCardDeck.cpp",
		"src/game/client/QmUi/UiContainers.h",
		"src/game/client/QmUi/UiOverlays.h",
	};
	for(const char *pPath : apOrdinaryUiFiles)
	{
		const std::string Source = ReadTextFile(pPath);
		EXPECT_EQ(CountRoundedRectDirectCalls(Source), 0u) << pPath;
	}
}

TEST(QmNewUiMenuRenderSurfaceContract, RoundedSurfaceGeometryCoversUiScaleAndRetinaMatrix)
{
	constexpr float Aspect = 16.0f / 9.0f;
	const int aScales[] = {100, 125, 150, 200};
	const int aScreenWidths[] = {1920, 3840};
	const CUIRect Rect{0.2f, 0.2f, 20.0f, 10.0f};
	const int aCornerMasks[] = {
		IGraphics::CORNER_ALL,
		IGraphics::CORNER_L,
		IGraphics::CORNER_R,
		IGraphics::CORNER_T,
		IGraphics::CORNER_B,
		IGraphics::CORNER_TL,
		IGraphics::CORNER_TR,
		IGraphics::CORNER_BL,
		IGraphics::CORNER_BR,
		IGraphics::CORNER_NONE,
	};
	for(const int Scale : aScales)
	{
		const float VirtualHeight = QmUiVirtualScreenHeight(Scale);
		const float VirtualWidth = VirtualHeight * Aspect;
		for(const int ScreenWidth : aScreenWidths)
		{
			const float PixelSize = VirtualWidth / (float)ScreenWidth;
			ASSERT_GT(PixelSize, 0.0f);
			SRoundedSurfaceParams Params;
			Params.m_Radius = 8.0f;
			Params.m_PixelSize = PixelSize;
			const SRoundedSurfacePlan Plan = ResolveRoundedSurfacePlan(Rect, Params, true);
			EXPECT_TRUE(Plan.m_UseSdf);
			const auto IsPixelAligned = [PixelSize](const float Value) {
				return std::abs(Value - std::round(Value / PixelSize) * PixelSize) < 1e-3f;
			};
			EXPECT_TRUE(IsPixelAligned(Plan.m_Rect.x));
			EXPECT_TRUE(IsPixelAligned(Plan.m_Rect.y));
			EXPECT_TRUE(IsPixelAligned(Plan.m_Rect.w));
			EXPECT_TRUE(IsPixelAligned(Plan.m_Rect.h));
			EXPECT_FLOAT_EQ(Plan.m_Radius, std::min(Plan.m_Rect.w, Plan.m_Rect.h) * 0.5f);
			for(const int Corners : aCornerMasks)
			{
				const vec4 Radii = ResolveRoundedSurfaceCornerRadii(Plan.m_Radius, Corners);
				EXPECT_FLOAT_EQ(Radii.x, Corners & IGraphics::CORNER_TL ? Plan.m_Radius : 0.0f);
				EXPECT_FLOAT_EQ(Radii.y, Corners & IGraphics::CORNER_TR ? Plan.m_Radius : 0.0f);
				EXPECT_FLOAT_EQ(Radii.z, Corners & IGraphics::CORNER_BR ? Plan.m_Radius : 0.0f);
				EXPECT_FLOAT_EQ(Radii.w, Corners & IGraphics::CORNER_BL ? Plan.m_Radius : 0.0f);
			}
			const SRoundedSurfacePlan Fallback = ResolveRoundedSurfacePlan(Rect, Params, false);
			EXPECT_FALSE(Fallback.m_UseSdf);
			EXPECT_FLOAT_EQ(Fallback.m_Rect.x, Plan.m_Rect.x);
			EXPECT_FLOAT_EQ(Fallback.m_Rect.y, Plan.m_Rect.y);
			EXPECT_FLOAT_EQ(Fallback.m_Rect.w, Plan.m_Rect.w);
			EXPECT_FLOAT_EQ(Fallback.m_Rect.h, Plan.m_Rect.h);
			EXPECT_FLOAT_EQ(Fallback.m_Radius, Plan.m_Radius);
		}
	}
}

TEST(QmNewUiMenuRenderSurfaceContract, RetinaNameplatesPreferPhysicalPixelAlignment)
{
	EXPECT_FALSE(QmNameplateUsesPhysicalPixelAlignment(1.0f, true));
	EXPECT_TRUE(QmNameplateUsesPhysicalPixelAlignment(1.5f, true));
	EXPECT_TRUE(QmNameplateUsesPhysicalPixelAlignment(2.0f, true));
	EXPECT_FALSE(QmNameplateUsesPhysicalPixelAlignment(2.0f, false));

	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	EXPECT_NE(Source.find("#if defined(CONF_PLATFORM_MACOS)"), std::string::npos);
	EXPECT_NE(Source.find("QmNameplateUsesPhysicalPixelAlignment(This.Graphics()->ScreenHiDPIScale(), true)"), std::string::npos);
}
