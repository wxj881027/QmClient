// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <engine/shared/json.h>

#include <game/client/qm_icon_manager.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace
{
	std::string ReadTextFile(const char *pPath)
	{
		return ReadTestSourceFile(pPath);
	}

	int JsonInt(const json_value *pObject, const char *pName)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		EXPECT_NE(pValue, &json_value_none);
		EXPECT_EQ(pValue->type, json_integer);
		return pValue->type == json_integer ? static_cast<int>(pValue->u.integer) : 0;
	}

	const char *JsonString(const json_value *pObject, const char *pName)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		EXPECT_NE(pValue, &json_value_none);
		EXPECT_EQ(pValue->type, json_string);
		return pValue->type == json_string ? pValue->u.string.ptr : "";
	}

	bool JsonBool(const json_value *pObject, const char *pName)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		EXPECT_NE(pValue, &json_value_none);
		EXPECT_EQ(pValue->type, json_boolean);
		return pValue->type == json_boolean && pValue->u.boolean;
	}

	const json_value *JsonObject(const json_value *pObject, const char *pName)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		EXPECT_NE(pValue, &json_value_none);
		EXPECT_EQ(pValue->type, json_object);
		return pValue;
	}

	const json_value *JsonArray(const json_value *pObject, const char *pName)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		EXPECT_NE(pValue, &json_value_none);
		EXPECT_EQ(pValue->type, json_array);
		return pValue;
	}

	double JsonDouble(const json_value *pObject, const char *pName)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		EXPECT_NE(pValue, &json_value_none);
		if(pValue->type == json_double)
			return pValue->u.dbl;
		if(pValue->type == json_integer)
			return static_cast<double>(pValue->u.integer);
		return 0.0;
	}
}

TEST(QmIconAtlas, RuntimeIconNamesAreStable)
{
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::STAR), "star");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::BOOKMARK), "bookmark");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::SEARCH), "magnifying-glass");
	// 名字必须与 Phosphor 官方一致（datasrc/qm_icons/phosphor.codepoints）。
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::CLOSE), "x");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::EYE), "eye");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::EYE_OFF), "eye-slash");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::CHEVRON_DOWN), "chevron-down");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::PLUS), "plus");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::TRASH), "trash");
	// 原自制 satellite 图标已替换为官方 Phosphor 图标。
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::ARROWS_IN), "arrows-in");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::ARROWS_OUT), "arrows-out");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::SWAP), "swap");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::SPEAKER_SLASH), "speaker-slash");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::CHECK), "check");

	const std::string Menus = ReadTextFile("src/game/client/components/menus.cpp");
	EXPECT_NE(Menus.find("RenderFavoriteMapsIcon"), std::string::npos);
	EXPECT_NE(Menus.find("EQmIcon::BOOKMARK"), std::string::npos);
	EXPECT_EQ(Menus.find("\xF0\x9F\x94\x96"), std::string::npos);
}

TEST(QmIconAtlas, MsdfOnlyReloadPolicy)
{
	EXPECT_EQ(NormalizeQmIconWeight(-1), 1);
	EXPECT_EQ(NormalizeQmIconWeight(0), 0);
	EXPECT_EQ(NormalizeQmIconWeight(1), 1);
	EXPECT_EQ(NormalizeQmIconWeight(2), 2);
	EXPECT_EQ(NormalizeQmIconWeight(3), 3);
	EXPECT_EQ(NormalizeQmIconWeight(4), 4);
	EXPECT_EQ(NormalizeQmIconWeight(5), 5);
	EXPECT_EQ(NormalizeQmIconWeight(6), 1);

	// 位图 alpha 图集已移除：图集不可用即字体兜底，仅剩 MSDF 单一路径。
	EXPECT_TRUE(QmIconAtlasNeedsReload(false, 1, 1));
	EXPECT_TRUE(QmIconAtlasNeedsReload(true, 0, 1));
	EXPECT_FALSE(QmIconAtlasNeedsReload(true, 1, 1));
	EXPECT_TRUE(QmIconAtlasRetryCooldownActive(99, 100));
	EXPECT_FALSE(QmIconAtlasRetryCooldownActive(100, 100));
	EXPECT_FALSE(QmIconAtlasRetryCooldownActive(101, 100));
	EXPECT_TRUE(QmIconReloadCooldownActive(99, 100, true, 1, true, 1, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(100, 100, true, 1, true, 1, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(99, 100, false, 1, true, 1, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(99, 100, true, 1, true, 0, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(99, 100, true, 1, false, 1, true));

	EXPECT_EQ(QmIconMsdfRunBucket(1), 0u);
	EXPECT_EQ(QmIconMsdfRunBucket(2), 1u);
	EXPECT_EQ(QmIconMsdfRunBucket(3), 2u);
	EXPECT_EQ(QmIconMsdfRunBucket(4), 2u);
	EXPECT_EQ(QmIconMsdfRunBucket(8), 3u);
	EXPECT_EQ(QmIconMsdfRunBucket(16), 4u);
	EXPECT_EQ(QmIconMsdfRunBucket(32), 5u);
	EXPECT_EQ(QmIconMsdfRunBucket(64), 6u);
	EXPECT_EQ(QmIconMsdfRunBucket(65), 7u);
	EXPECT_TRUE(QmIconReloadCooldownActive(99, 100, true, 0, true, 0, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(99, 100, true, 1, true, 0, true));

	EXPECT_TRUE(QmIconTextureCanCommit(true, false));
	EXPECT_FALSE(QmIconTextureCanCommit(false, false));
	EXPECT_FALSE(QmIconTextureCanCommit(true, true));
	EXPECT_FALSE(QmIconTextureCanCommit(false, true));

	EXPECT_FLOAT_EQ(QmIconPixelScale(0, 100.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmIconPixelScale(100, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmIconPixelScale(100, -1.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmIconPixelScale(200, 100.0f), 2.0f);

	const std::string Header = ReadTextFile("src/game/client/qm_icon_manager.h");
	const std::string Source = ReadTextFile("src/game/client/qm_icon_manager.cpp");
	const std::string GameClient = ReadTextFile("src/game/client/gameclient.cpp");
	EXPECT_NE(Header.find("bool PreferFontFallback() const { return !IsReady(); }"), std::string::npos);
	EXPECT_EQ(Header.find("EQmIconAtlasType"), std::string::npos);
	EXPECT_NE(Source.find("QmIconAtlasNeedsReload(IsReady(), m_AtlasWeight, Weight)"), std::string::npos);
	EXPECT_NE(Source.find("QmIconReloadCooldownActive(Now, m_NextReloadAttemptTime, m_HasFailedReloadTarget"), std::string::npos);
	EXPECT_EQ(Source.find("RetryMsdfAtlas();"), std::string::npos) << "probe 重试机制已随 alpha 图集移除";
	EXPECT_NE(Source.find("m_NextReloadAttemptTime"), std::string::npos);
	EXPECT_NE(Source.find("FinishMsdfManagerCallRun();"), std::string::npos);
	EXPECT_NE(Header.find("SQmIconDiagnostics"), std::string::npos);
	EXPECT_NE(Header.find("QmIconMsdfRunBucket"), std::string::npos);
	EXPECT_NE(GameClient.find("m_QmIconManager.RefreshForCurrentDpi();"), std::string::npos);
	EXPECT_NE(GameClient.find("m_QmIconManager.Shutdown();"), std::string::npos);
}

TEST(QmIconDiagnosticsWindow, AccumulatesDrawsAndRunBucketsUntilInterval)
{
	SQmIconDiagnosticsWindow Window;
	SQmIconDiagnostics First;
	First.m_MsdfIconDraws = 3;
	First.m_MaxMsdfManagerCallRun = 2;
	First.m_MsdfManagerCallRunBuckets[1] = 2;
	EXPECT_FALSE(Window.Add(First, 100, 10));

	SQmIconDiagnostics Second;
	Second.m_MsdfIconDraws = 5;
	Second.m_MaxMsdfManagerCallRun = 4;
	Second.m_MsdfManagerCallRunBuckets[1] = 1;
	Second.m_MsdfManagerCallRunBuckets[2] = 1;
	EXPECT_FALSE(Window.Add(Second, 109, 10));
	EXPECT_TRUE(Window.Add({}, 110, 10));
	EXPECT_EQ(Window.m_Frames, 3u);
	EXPECT_EQ(Window.m_Total.m_MsdfIconDraws, 8u);
	EXPECT_EQ(Window.m_MaxMsdfDraws, 5u);
	EXPECT_EQ(Window.m_Total.m_MaxMsdfManagerCallRun, 4u);
	EXPECT_EQ(Window.m_Total.m_MsdfManagerCallRunBuckets[1], 3u);
	EXPECT_EQ(Window.m_Total.m_MsdfManagerCallRunBuckets[2], 1u);

	Window.Clear(110);
	EXPECT_FALSE(Window.Add(First, 111, 10));
	EXPECT_EQ(Window.m_Frames, 1u);
	EXPECT_EQ(Window.m_Total.m_MsdfIconDraws, 3u);
	EXPECT_EQ(Window.m_Total.m_MsdfManagerCallRunBuckets[1], 2u);
}

TEST(QmIconDiagnosticsWindow, ResourceChangesFlushImmediatelyAndOnlyOnce)
{
	SQmIconDiagnosticsWindow Window;
	SQmIconDiagnostics Draw;
	Draw.m_MsdfIconDraws = 7;
	EXPECT_FALSE(Window.Add(Draw, 100, 10));

	SQmIconDiagnostics Resources;
	Resources.m_ReloadAttempts = 1;
	Resources.m_ReloadSuccesses = 1;
	Resources.m_AtlasSwaps = 1;
	Resources.m_TextureLoads = 1;
	EXPECT_TRUE(Window.Add(Resources, 101, 10));
	EXPECT_EQ(Window.m_Frames, 2u);
	EXPECT_EQ(Window.m_Total.m_MsdfIconDraws, 7u);
	EXPECT_EQ(Window.m_Total.m_TextureLoads, 1u);
	Window.Clear(101);
	EXPECT_FALSE(Window.Add({}, 102, 10));
	EXPECT_EQ(Window.m_Total.m_ReloadAttempts, 0u);
	EXPECT_EQ(Window.m_Total.m_TextureLoads, 0u);

	SQmIconDiagnostics Failure;
	Failure.m_TextureLoadFailures = 1;
	EXPECT_TRUE(Window.Add(Failure, 103, 10));
	Window.Clear(103);
	SQmIconDiagnostics Unload;
	Unload.m_TextureUnloads = 1;
	EXPECT_TRUE(Window.Add(Unload, 104, 10));
}

TEST(QmIconAtlas, UiTintKeepsAlphaAndDoesNotDefineSemanticDirectColor)
{
	const ColorRGBA SemanticColor(0.20f, 0.60f, 0.80f, 0.35f);
	const ColorRGBA White = QmUiIconColor(SemanticColor, 1);
	const ColorRGBA Black = QmUiIconColor(SemanticColor, 2);
	const unsigned int CustomColor = ColorHSLA(0.28f, 0.70f, 0.45f, 1.0f).Pack(false);
	const ColorRGBA Custom = QmUiIconColor(SemanticColor, 3, CustomColor);
	const ColorRGBA ExpectedCustom = color_cast<ColorRGBA>(ColorHSLA(CustomColor));
	const ColorRGBA Rainbow = QmUiIconColor(SemanticColor, 4, 0, 2.5f);
	const ColorRGBA ExpectedRainbow = color_cast<ColorRGBA>(ColorHSLA(0.5f, 0.75f, 0.6f, SemanticColor.a));
	EXPECT_FLOAT_EQ(White.r, 1.0f);
	EXPECT_FLOAT_EQ(White.g, 1.0f);
	EXPECT_FLOAT_EQ(White.b, 1.0f);
	EXPECT_FLOAT_EQ(White.a, SemanticColor.a);
	EXPECT_FLOAT_EQ(Black.r, 0.0f);
	EXPECT_FLOAT_EQ(Black.g, 0.0f);
	EXPECT_FLOAT_EQ(Black.b, 0.0f);
	EXPECT_FLOAT_EQ(Black.a, SemanticColor.a);
	EXPECT_FLOAT_EQ(Custom.r, ExpectedCustom.r);
	EXPECT_FLOAT_EQ(Custom.g, ExpectedCustom.g);
	EXPECT_FLOAT_EQ(Custom.b, ExpectedCustom.b);
	EXPECT_FLOAT_EQ(Custom.a, SemanticColor.a);
	EXPECT_FLOAT_EQ(Rainbow.r, ExpectedRainbow.r);
	EXPECT_FLOAT_EQ(Rainbow.g, ExpectedRainbow.g);
	EXPECT_FLOAT_EQ(Rainbow.b, ExpectedRainbow.b);
	EXPECT_FLOAT_EQ(Rainbow.a, SemanticColor.a);
	const unsigned OriginalSecondaryColor = g_Config.m_QmUiIconDuotoneSecondaryColor;
	g_Config.m_QmUiIconDuotoneSecondaryColor = 0xFFFFFFFF;
	const ColorRGBA Secondary = ConfiguredQmUiIconSecondaryColor(SemanticColor);
	g_Config.m_QmUiIconDuotoneSecondaryColor = OriginalSecondaryColor;
	EXPECT_FLOAT_EQ(Secondary.a, SemanticColor.a);

	const std::string Source = ReadTextFile("src/game/client/qm_icon_manager.cpp");
	const size_t DirectRender = Source.find("bool CQmIconManager::RenderIcon(EQmIcon Icon, const CUIRect &Rect, const ColorRGBA &Color, const bool PreserveAspect) const");
	const size_t RotatedRender = Source.find("bool CQmIconManager::RenderIconRotated", DirectRender);
	const size_t StateRender = Source.find("bool CQmIconManager::RenderIcon(EQmIcon Icon, const CUIRect &Rect, EQmIconState State", RotatedRender);
	ASSERT_NE(DirectRender, std::string::npos);
	ASSERT_NE(RotatedRender, std::string::npos);
	ASSERT_NE(StateRender, std::string::npos);
	EXPECT_EQ(Source.substr(DirectRender, RotatedRender - DirectRender).find("QmUiIconColor"), std::string::npos);
	EXPECT_EQ(Source.substr(StateRender).find("QmUiIconColor"), std::string::npos);

	const std::string Buttons = ReadTextFile("src/game/client/QmUi/UiButtons.cpp");
	EXPECT_NE(Buttons.find("IconStyle.m_Normal = ConfiguredQmUiIconColor"), std::string::npos);
	EXPECT_NE(Buttons.find("IconRect, IconState, IconStyle"), std::string::npos);
}

TEST(QmIconAtlas, GeneratedMsdfManifestsContainEveryRuntimeIcon)
{
	// Thin 未随包字体，不再烘焙（weight 2 复用 light 图集）。
	constexpr const char *apWeights[] = {"regular", "bold", "fill", "light", "duotone"};
	for(const char *pWeight : apWeights)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "data/qmclient/icons/qm_icons_%s_msdf.json", pWeight);
		const std::string Json = ReadTextFile(aPath);
		ASSERT_FALSE(Json.empty()) << aPath;

		json_value *pRoot = JsonParse(Json.c_str(), Json.size());
		ASSERT_NE(pRoot, nullptr) << aPath;

		const json_value *pAtlas = JsonObject(pRoot, "atlas");
		const json_value *pIcons = JsonObject(pRoot, "icons");
		const int AtlasWidth = JsonInt(pAtlas, "width");
		const int AtlasHeight = JsonInt(pAtlas, "height");
		constexpr int FieldSize = 48;
		constexpr int ToolPadding = 8; // 官方工具的字形 pxrange 出血
		constexpr int CellSize = 72; // 48 + 2×12 网格间距（字形外轮廓可略超 em 框）
		const int IconCount = static_cast<int>(pIcons->u.object.length);
		EXPECT_GE(IconCount, static_cast<int>(EQmIcon::COUNT));
		// 新旧官方名共用码点 → manifest 条目数可大于唯一格数；网格按唯一格数布局。
		EXPECT_EQ(AtlasWidth, AtlasHeight);
		EXPECT_EQ(AtlasWidth % CellSize, 0) << AtlasWidth;
		// manifest 条目可多于唯一格数（新旧官方名共享码点格子），网格以唯一格数布局。
		EXPECT_EQ(JsonInt(pRoot, "version"), 2);
		EXPECT_STREQ(JsonString(pRoot, "kind"), "mtsdf");
		EXPECT_STREQ(JsonString(pRoot, "distance_field"), "mtsdf");
		EXPECT_TRUE(JsonBool(pRoot, "alpha_sdf"));
		EXPECT_EQ(JsonInt(pRoot, "px_range"), 6);
		EXPECT_EQ(JsonInt(pAtlas, "padding"), ToolPadding);

		for(int IconIndex = 0; IconIndex < static_cast<int>(EQmIcon::COUNT); ++IconIndex)
		{
			const EQmIcon Icon = static_cast<EQmIcon>(IconIndex);
			const char *pIconName = CQmIconManager::IconName(Icon);
			ASSERT_NE(pIconName[0], '\0');

			const json_value *pEntry = JsonObject(pIcons, pIconName);
			const int X = JsonInt(pEntry, "x");
			const int Y = JsonInt(pEntry, "y");
			const int W = JsonInt(pEntry, "w");
			const int H = JsonInt(pEntry, "h");

			// 字体烘焙的字形包围盒随图标外轮廓变化（不再是固定 48×48），
			// 但每个格子必须完整落在网格内，且不超过格子尺寸。
			EXPECT_GT(W, 1) << pIconName;
			EXPECT_GT(H, 1) << pIconName;
			EXPECT_LE(W, CellSize) << pIconName;
			EXPECT_LE(H, CellSize) << pIconName;
			EXPECT_GE(X, 0) << pIconName;
			EXPECT_GE(Y, 0) << pIconName;
			EXPECT_LE(X + W, AtlasWidth) << pIconName;
			EXPECT_LE(Y + H, AtlasHeight) << pIconName;
		}

		json_value_free(pRoot);
	}
}

TEST(QmIconAtlas, DuotoneManifestDeclaresSecondaryMask)
{
	const std::string Json = ReadTextFile("data/qmclient/icons/qm_icons_duotone_msdf.json");
	EXPECT_NE(Json.find("\"distance_field\": \"mtsdf\""), std::string::npos);
	EXPECT_NE(Json.find("\"secondary_mask\": \"alpha\""), std::string::npos);
}

TEST(QmIconDiagnosticsContract, KeepsAtlasAndRendererCountersSeparated)
{
	const std::string Header = ReadTextFile("src/game/client/qm_icon_manager.h");
	const std::string IconManager = ReadTextFile("src/game/client/qm_icon_manager.cpp");
	const std::string Graphics = ReadTextFile("src/engine/client/graphics_threaded.cpp");
	const std::string GraphicsHeader = ReadTextFile("src/engine/client/graphics_threaded.h");

	EXPECT_NE(Header.find("struct SQmIconDiagnostics"), std::string::npos);
	EXPECT_NE(Header.find("m_MsdfManagerCallRunBuckets"), std::string::npos);
	EXPECT_NE(Header.find("SQmIconDiagnostics TakeDiagnostics() const;"), std::string::npos);
	EXPECT_NE(IconManager.find("void CQmIconManager::FinishMsdfManagerCallRun() const"), std::string::npos);
	EXPECT_NE(IconManager.find("SQmIconDiagnostics CQmIconManager::TakeDiagnostics() const"), std::string::npos);
	EXPECT_NE(IconManager.find("bool IconDiagnosticsEnabled()"), std::string::npos);
	EXPECT_NE(IconManager.find("if(!m_DiagnosticsEnabled)"), std::string::npos);
	EXPECT_NE(IconManager.find("m_DiagnosticsEnabled = IconDiagnosticsEnabled();"), std::string::npos);
	EXPECT_NE(IconManager.find("m_DiagnosticsEnabled && Atlas.m_Texture.IsValid() && !Atlas.m_Texture.IsNullTexture()"), std::string::npos);
	EXPECT_NE(IconManager.find("m_Diagnostics.m_ReloadAttempts++"), std::string::npos);
	EXPECT_NE(IconManager.find("m_Diagnostics.m_TextureLoads++"), std::string::npos);
	EXPECT_NE(IconManager.find("m_Diagnostics.m_TextureUnloads++"), std::string::npos);

	const size_t MsdfRender = Graphics.find("void CGraphics_Threaded::RenderTexturedMsdf");
	const size_t NextFunction = Graphics.find("int CGraphics_Threaded::CreateQuadContainer", MsdfRender);
	ASSERT_NE(MsdfRender, std::string::npos);
	ASSERT_NE(NextFunction, std::string::npos);
	const std::string MsdfRenderBody = Graphics.substr(MsdfRender, NextFunction - MsdfRender);
	EXPECT_NE(MsdfRenderBody.find("if(m_NumVertices > 0)"), std::string::npos);
	EXPECT_NE(MsdfRenderBody.find("if(m_MacosGraphicsDiagnosticsEnabled)"), std::string::npos);
	EXPECT_NE(MsdfRenderBody.find("m_MsdfFlushCount++"), std::string::npos);
	EXPECT_NE(MsdfRenderBody.find("m_MsdfCommandCount++"), std::string::npos);
	EXPECT_NE(MsdfRenderBody.find("#if defined(CONF_PLATFORM_MACOS)"), std::string::npos);
	EXPECT_NE(GraphicsHeader.find("bool m_MacosGraphicsDiagnosticsEnabled = false;"), std::string::npos);
	EXPECT_NE(GraphicsHeader.find("uint32_t m_MacosGraphicsDiagnosticFrameCount = 0;"), std::string::npos);
	EXPECT_NE(GraphicsHeader.find("double m_MacosGraphicsDiagnosticSubmitMsSum = 0.0;"), std::string::npos);
	EXPECT_NE(GraphicsHeader.find("double m_MacosFrameSerializationWaitMsSum = 0.0;"), std::string::npos);
	EXPECT_NE(GraphicsHeader.find("uint64_t m_MacosFrameSerializationWaitCount = 0;"), std::string::npos);
	EXPECT_NE(Graphics.find("sample_frames=120"), std::string::npos);
	EXPECT_NE(Graphics.find("submit_duration_ms_sum"), std::string::npos);
	EXPECT_NE(Graphics.find("submit_duration_ms_avg"), std::string::npos);
	EXPECT_NE(Graphics.find("const bool PreviousMacosDiagnostics = m_MacosGraphicsDiagnosticsEnabled;"), std::string::npos);
	EXPECT_NE(Graphics.find("if(PreviousMacosDiagnostics)"), std::string::npos);
	EXPECT_NE(Graphics.find("msdf_commands_sum"), std::string::npos);
	EXPECT_NE(Graphics.find("msdf_flushes_sum"), std::string::npos);
	EXPECT_NE(Graphics.find("frame_serialization_wait_count"), std::string::npos);
	EXPECT_NE(Graphics.find("frame_serialization_wait_ms_avg"), std::string::npos);
	EXPECT_NE(Graphics.find("if(!MacosDiagnostics)\n\t{\n\t\tif(PreviousMacosDiagnostics)"), std::string::npos);
	EXPECT_NE(Graphics.find("unlimited_config=%d"), std::string::npos);
	EXPECT_NE(Graphics.find("gfx_refresh_rate=%d"), std::string::npos);
	EXPECT_NE(Graphics.find("cl_refresh_rate=%d"), std::string::npos);
	EXPECT_NE(Graphics.find("cl_refresh_rate_inactive=%d"), std::string::npos);
	EXPECT_NE(Graphics.find("dbg_graphs=%d"), std::string::npos);
	EXPECT_NE(Graphics.find("async_render_old=%d"), std::string::npos);
	const size_t Shutdown = Graphics.find("void CGraphics_Threaded::Shutdown()");
	const size_t NextFunctionAfterShutdown = Graphics.find("int CGraphics_Threaded::GetNumScreens() const", Shutdown);
	ASSERT_NE(Shutdown, std::string::npos);
	ASSERT_NE(NextFunctionAfterShutdown, std::string::npos);
	const std::string ShutdownBody = Graphics.substr(Shutdown, NextFunctionAfterShutdown - Shutdown);
	EXPECT_NE(ShutdownBody.find("if(m_pBackend == nullptr)"), std::string::npos);
	EXPECT_NE(ShutdownBody.find("m_pCommandBuffer->m_CommandCount > 0"), std::string::npos);
	EXPECT_NE(ShutdownBody.find("KickCommandBuffer();"), std::string::npos);
	EXPECT_NE(ShutdownBody.find("m_pBackend->WaitForIdle();"), std::string::npos);
	EXPECT_NE(ShutdownBody.find("m_pCommandBuffer = nullptr;"), std::string::npos);
}

TEST(QmVulkanRenderTargetDestroy, GuardsPausedRenderingAndActiveRenderPass)
{
	const std::string Source = ReadTextFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");

	// 延迟销毁队列成员（渲染通道期间收到的销毁请求）
	EXPECT_NE(Source.find("std::vector<std::pair<size_t, VkImage>> m_vPendingRenderTargetDestroy;"), std::string::npos);

	// Cmd_RenderTarget_Destroy：暂停时直接销毁，活跃渲染通道期间延迟到 End 后处理
	const size_t DestroyFn = Source.find("[[nodiscard]] bool Cmd_RenderTarget_Destroy");
	const size_t BeginFn = Source.find("[[nodiscard]] bool Cmd_RenderTarget_Begin", DestroyFn);
	ASSERT_NE(DestroyFn, std::string::npos);
	ASSERT_NE(BeginFn, std::string::npos);
	const std::string DestroySection = Source.substr(DestroyFn, BeginFn - DestroyFn);
	EXPECT_NE(DestroySection.find("if(m_RenderingPaused)"), std::string::npos);
	EXPECT_NE(DestroySection.find("DestroyRenderTarget(m_vRenderTargets[pCommand->m_TargetId]);"), std::string::npos);
	EXPECT_NE(DestroySection.find("m_vPendingRenderTargetDestroy.emplace_back"), std::string::npos);
	EXPECT_NE(DestroySection.find("SubmitCurrentCommandsAndRestartSwapPass"), std::string::npos);

	// Cmd_RenderTarget_Begin：暂停时跳过
	const size_t EndFn = Source.find("[[nodiscard]] bool Cmd_RenderTarget_End", BeginFn);
	ASSERT_NE(EndFn, std::string::npos);
	const std::string BeginSection = Source.substr(BeginFn, EndFn - BeginFn);
	EXPECT_NE(BeginSection.find("if(m_RenderingPaused)"), std::string::npos);

	// Cmd_RenderTarget_End：暂停时跳过，并在结束后统一处理延迟销毁队列
	const size_t ReadbackFn = Source.find("[[nodiscard]] bool Cmd_RenderTarget_Readback", EndFn);
	ASSERT_NE(ReadbackFn, std::string::npos);
	const std::string EndSection = Source.substr(EndFn, ReadbackFn - EndFn);
	EXPECT_NE(EndSection.find("if(m_RenderingPaused)"), std::string::npos);
	EXPECT_NE(EndSection.find("m_vPendingRenderTargetDestroy.empty()"), std::string::npos);
	EXPECT_NE(EndSection.find("m_vPendingRenderTargetDestroy.clear();"), std::string::npos);

	// Cmd_RenderTarget_Readback / Draw：暂停时跳过
	const size_t DrawFn = Source.find("[[nodiscard]] bool Cmd_RenderTarget_Draw", ReadbackFn);
	ASSERT_NE(DrawFn, std::string::npos);
	EXPECT_NE(Source.substr(ReadbackFn, DrawFn - ReadbackFn).find("if(m_RenderingPaused)"), std::string::npos);
	const size_t CaptureFn = Source.find("[[nodiscard]] bool Cmd_RenderTarget_CaptureBackbuffer", DrawFn);
	ASSERT_NE(CaptureFn, std::string::npos);
	EXPECT_NE(Source.substr(DrawFn, CaptureFn - DrawFn).find("if(m_RenderingPaused)"), std::string::npos);

	// Cmd_RenderTarget_CaptureBackbuffer：暂停时跳过（并入既有守卫条件）
	const size_t BlurPassFn = Source.find("[[nodiscard]] bool Cmd_RenderTarget_GaussianBlurPass", CaptureFn);
	ASSERT_NE(BlurPassFn, std::string::npos);
	EXPECT_NE(Source.substr(CaptureFn, BlurPassFn - CaptureFn).find("m_RenderingPaused || !SupportsBackbufferCapture()"), std::string::npos);

	// Cmd_RenderTarget_GaussianBlurPass：暂停时跳过
	const size_t NextFn = Source.find("[[nodiscard]] bool Cmd_TextTextures_Create", BlurPassFn);
	ASSERT_NE(NextFn, std::string::npos);
	EXPECT_NE(Source.substr(BlurPassFn, NextFn - BlurPassFn).find("if(m_RenderingPaused)"), std::string::npos);
}

TEST(QmIconAtlasContract, IconDrawsPreserveGlyphAspectRatio)
{
	// manifest 存的是每个字形自己的紧贴框（宽高比各异），绘制必须等比适配调用方方框，
	// 否则每个图标都会按自己的宽高比被拉伸——历史症状就是「图标不是 1:1」。
	const CUIRect Square{10.0f, 20.0f, 32.0f, 32.0f};

	// 宽字形：宽度填满、高度按比例收窄并垂直居中
	const CUIRect Wide = QmIconAspectFittedRect(Square, 60, 44);
	EXPECT_FLOAT_EQ(Wide.w, 32.0f);
	EXPECT_NEAR(Wide.h, 32.0f * 44.0f / 60.0f, 0.001f);
	EXPECT_NEAR(Wide.y, Square.y + (Square.h - Wide.h) * 0.5f, 0.001f);
	EXPECT_NEAR(Wide.x, Square.x, 0.001f);

	// 高字形：高度填满、宽度按比例收窄并水平居中
	const CUIRect Tall = QmIconAspectFittedRect(Square, 44, 60);
	EXPECT_FLOAT_EQ(Tall.h, 32.0f);
	EXPECT_NEAR(Tall.w, 32.0f * 44.0f / 60.0f, 0.001f);
	EXPECT_NEAR(Tall.x, Square.x + (Square.w - Tall.w) * 0.5f, 0.001f);

	// 等比方框原样返回；适配结果永不超出原方框
	const CUIRect Same = QmIconAspectFittedRect(Square, 48, 48);
	EXPECT_FLOAT_EQ(Same.w, Square.w);
	EXPECT_FLOAT_EQ(Same.h, Square.h);
	for(const CUIRect &Fitted : {Wide, Tall, Same})
	{
		EXPECT_GE(Fitted.x, Square.x - 0.001f);
		EXPECT_GE(Fitted.y, Square.y - 0.001f);
		EXPECT_LE(Fitted.x + Fitted.w, Square.x + Square.w + 0.001f);
		EXPECT_LE(Fitted.y + Fitted.h, Square.y + Square.h + 0.001f);
	}

	// 核心断言：绘制宽高比 == 字形宽高比
	EXPECT_NEAR(Wide.w / Wide.h, 60.0f / 44.0f, 0.001f);
	EXPECT_NEAR(Tall.w / Tall.h, 44.0f / 60.0f, 0.001f);

	// 非方形调用方方框同样等比适配（例如媒体岛眨眼用的压缩方框）
	const CUIRect Squashed{0.0f, 0.0f, 88.0f, 44.0f};
	const CUIRect FittedInSquashed = QmIconAspectFittedRect(Squashed, 60, 44);
	EXPECT_NEAR(FittedInSquashed.w / FittedInSquashed.h, 60.0f / 44.0f, 0.001f);
}

TEST(QmIconAtlasContract, MorphFrameBlendSelectsAdjacentFrames)
{
	// 端点必须精确落在首/末帧上：否则动画结束交回静态图标时会有形状跳变。
	constexpr int Frames = 8;
	const SQmIconMorphFrameBlend Start = QmIconMorphFrameBlend(0.0f, Frames);
	EXPECT_EQ(Start.m_Index0, 0);
	EXPECT_FLOAT_EQ(Start.m_Alpha0, 1.0f);
	EXPECT_FLOAT_EQ(Start.m_Alpha1, 0.0f);

	const SQmIconMorphFrameBlend End = QmIconMorphFrameBlend(1.0f, Frames);
	EXPECT_EQ(End.m_Index0, Frames - 1);
	EXPECT_EQ(End.m_Index1, Frames - 1);
	EXPECT_FLOAT_EQ(End.m_Alpha0, 1.0f);
	EXPECT_FLOAT_EQ(End.m_Alpha1, 0.0f);

	// 弹簧会 over/undershoot，越界进度必须被夹紧。
	const SQmIconMorphFrameBlend Under = QmIconMorphFrameBlend(-0.35f, Frames);
	EXPECT_EQ(Under.m_Index0, 0);
	EXPECT_FLOAT_EQ(Under.m_Alpha1, 0.0f);
	const SQmIconMorphFrameBlend Over = QmIconMorphFrameBlend(1.45f, Frames);
	EXPECT_EQ(Over.m_Index0, Frames - 1);
	EXPECT_FLOAT_EQ(Over.m_Alpha0, 1.0f);

	// 单帧退化：不得产生越界索引。
	const SQmIconMorphFrameBlend Single = QmIconMorphFrameBlend(0.5f, 1);
	EXPECT_EQ(Single.m_Index0, 0);
	EXPECT_EQ(Single.m_Index1, 0);
	EXPECT_FLOAT_EQ(Single.m_Alpha1, 0.0f);

	for(int Step = 0; Step <= 40; ++Step)
	{
		const SQmIconMorphFrameBlend Blend = QmIconMorphFrameBlend(Step / 40.0f, Frames);
		EXPECT_GE(Blend.m_Index0, 0);
		EXPECT_LT(Blend.m_Index1, Frames);
		EXPECT_GE(Blend.m_Index1, Blend.m_Index0);
		EXPECT_NEAR(Blend.m_Alpha0 + Blend.m_Alpha1, 1.0f, 1e-4f);
	}
}

TEST(QmIconAtlasContract, BoldAtlasCarriesMorphKeyFrames)
{
	// 眼睛 morph 的 MSDF 关键帧只随 Bold 图集烘焙；显示框必须在图集内，
	// 且首末帧的框与两个眼睛图标一致（端点与静态图标同尺寸基准）。
	const std::string Json = ReadTextFile("data/qmclient/icons/qm_icons_bold_msdf.json");
	ASSERT_FALSE(Json.empty());
	json_value *pRoot = JsonParse(Json.c_str(), Json.size());
	ASSERT_NE(pRoot, nullptr);

	const json_value *pAtlas = JsonObject(pRoot, "atlas");
	const json_value *pIcons = JsonObject(pRoot, "icons");
	const json_value *pFrames = JsonArray(pRoot, "morph_frames");
	const int AtlasWidth = JsonInt(pAtlas, "width");
	const int AtlasHeight = JsonInt(pAtlas, "height");
	const unsigned int FrameCount = pFrames->u.array.length;
	EXPECT_GE(FrameCount, 2u);
	EXPECT_LE(FrameCount, static_cast<unsigned int>(CQmIconAtlas::MORPH_FRAME_CAPACITY));

	const json_value *pEye = JsonObject(pIcons, "eye");
	const json_value *pEyeSlash = JsonObject(pIcons, "eye-slash");

	double PreviousProgress = -1.0;
	for(unsigned int Index = 0; Index < FrameCount; ++Index)
	{
		const json_value *pFrame = pFrames->u.array.values[Index];
		ASSERT_NE(pFrame, nullptr);
		ASSERT_EQ(pFrame->type, json_object);
		const int X = JsonInt(pFrame, "x");
		const int Y = JsonInt(pFrame, "y");
		const int W = JsonInt(pFrame, "w");
		const int H = JsonInt(pFrame, "h");
		const double Progress = JsonDouble(pFrame, "progress");
		EXPECT_GE(X, 0);
		EXPECT_GE(Y, 0);
		EXPECT_GT(W, 0);
		EXPECT_GT(H, 0);
		EXPECT_LE(X + W, AtlasWidth);
		EXPECT_LE(Y + H, AtlasHeight);
		EXPECT_GT(Progress, PreviousProgress) << Index;
		PreviousProgress = Progress;
	}
	// 进度必须覆盖 [0,1] 两端。
	EXPECT_NEAR(JsonDouble(pFrames->u.array.values[0], "progress"), 0.0, 1e-6);
	EXPECT_NEAR(JsonDouble(pFrames->u.array.values[FrameCount - 1], "progress"), 1.0, 1e-6);

	const json_value *pFirst = pFrames->u.array.values[0];
	const json_value *pLast = pFrames->u.array.values[FrameCount - 1];
	EXPECT_NEAR(JsonInt(pFirst, "w"), JsonInt(pEye, "w"), 2);
	EXPECT_NEAR(JsonInt(pFirst, "h"), JsonInt(pEye, "h"), 2);
	EXPECT_NEAR(JsonInt(pLast, "w"), JsonInt(pEyeSlash, "w"), 4);
	EXPECT_NEAR(JsonInt(pLast, "h"), JsonInt(pEyeSlash, "h"), 4);
}
