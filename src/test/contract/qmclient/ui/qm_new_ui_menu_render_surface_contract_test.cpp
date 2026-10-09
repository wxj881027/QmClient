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
#include <test/support/qmclient_source_contract_test.h>
#include <test/test.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
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

TEST(QmNewUiMenuRenderSurfaceContract, ShaderBackendsKeepRoundedSurfaceComposition)
{
	// 着色器平台分支不能由 CPU 几何单元测试观察；这里只保留对应资源合同。
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
	EXPECT_NE(GraphicsHeader.find("static_assert(sizeof(SRoundedRectSdfParams) == sizeof(vec4) * 5);"), std::string::npos);
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
	const std::string Effects = ReadTextFile("src/game/client/components/ui_effects.cpp");
	const std::string HudEditor = ReadTextFile("src/game/client/components/hud_editor.cpp");

	EXPECT_NE(Effects.find("DrawRoundedSurface(Ui(), ShadowRect"), std::string::npos);
	EXPECT_EQ(Effects.find("Graphics()->DrawRect(PreviewX + ShadowOffset"), std::string::npos);
	EXPECT_NE(HudEditor.find("DrawRoundedSurface(Ui(), HelpRect"), std::string::npos);
	EXPECT_EQ(HudEditor.find("Graphics()->DrawRect(HelpX, HelpY"), std::string::npos);
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
		"src/game/client/QmUi/UiDiscreteSlider.cpp",
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

	const std::filesystem::path CardsPath = TestSourcePath("src/game/client/QmUi/cards");
	for(const auto &Entry : std::filesystem::directory_iterator(CardsPath))
	{
		if(!Entry.is_regular_file() || (Entry.path().extension() != ".cpp" && Entry.path().extension() != ".h"))
			continue;
		const std::string Path = "src/game/client/QmUi/cards/" + Entry.path().filename().string();
		EXPECT_EQ(CountRoundedRectDirectCalls(ReadTextFile(Path.c_str())), 0u) << Path;
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

TEST(QmNewUiMenuBranches, NoThemeBackgroundAndFriendRowsUseSharedSdfSurfaces)
{
	const std::string Menus = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Browser = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string Background = FunctionBody(Menus, "void CMenus::RenderBackground()");
	ASSERT_FALSE(Background.empty());

	EXPECT_NE(Background.find("const bool NoMenuTheme = g_Config.m_ClMenuMap[0] == '\\0';"), std::string::npos);
	EXPECT_NE(Background.find("const ColorRGBA CheckerColor = NoMenuTheme"), std::string::npos);
	EXPECT_NE(Background.find("Graphics()->SetColor(CheckerColor);"), std::string::npos);
	EXPECT_NE(Browser.find("#include <game/client/QmUi/UiSurface.h>"), std::string::npos);
	EXPECT_NE(Browser.find("DrawRoundedSurface(Ui(), Header, HeaderColor, ColorRGBA(), 5.0f);"), std::string::npos);
	EXPECT_NE(Browser.find("DrawRoundedSurface(Ui(), Rect, Color, ColorRGBA(), 5.0f);"), std::string::npos);
	EXPECT_EQ(Browser.find("Rect.Draw(Color, IGraphics::CORNER_ALL, 5.0f);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SharedListsAndResourceCardsUseRoundedSurfacePath)
{
	const std::string ListBox = ReadTextFile("src/game/client/ui_listbox.cpp");
	const std::string Assets = ReadTextFile("src/game/client/components/menus_settings_assets.cpp");
	EXPECT_NE(ListBox.find("DrawRoundedSurface(Ui(), Item.m_Rect"), std::string::npos);
	EXPECT_NE(ListBox.find("DrawRoundedSurface(Ui(), View"), std::string::npos);
	EXPECT_NE(Assets.find("DrawRoundedSurface(Ui(), ShellRect"), std::string::npos);
	EXPECT_NE(Assets.find("DrawRoundedSurface(Ui(), PreviewFrame"), std::string::npos);
	EXPECT_NE(Assets.find("DrawRoundedSurface(Ui(), FallbackRect"), std::string::npos);
	EXPECT_NE(Assets.find("DrawRoundedSurface(pUi, StatusRect"), std::string::npos);
	EXPECT_NE(Assets.find("DrawRoundedSurface(Ui(), WorkshopHudView"), std::string::npos);
}
