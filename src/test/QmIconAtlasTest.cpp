// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <engine/shared/json.h>

#include <game/client/qm_icon_manager.h>

#include <gtest/gtest.h>

#include <array>
#include <string>

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

	const json_value *JsonObject(const json_value *pObject, const char *pName)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		EXPECT_NE(pValue, &json_value_none);
		EXPECT_EQ(pValue->type, json_object);
		return pValue;
	}
}

TEST(QmIconAtlas, RuntimeIconNamesAreStable)
{
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::STAR), "star");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::BOOKMARK), "bookmark");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::SEARCH), "magnifying-glass");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::CLOSE), "close");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::EYE), "eye");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::EYE_OFF), "eye-off");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::CHEVRON_DOWN), "chevron-down");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::PLUS), "plus");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::TRASH), "trash");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::SATELLITE_SWAP_INCOMING), "satellite-swap-incoming");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::SATELLITE_SWAP_OUTGOING), "satellite-swap-outgoing");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::SATELLITE_SWITCH), "satellite-switch");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::SATELLITE_MUTE), "satellite-mute");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::SATELLITE_CHECK), "satellite-check");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::SATELLITE_SPECTATOR_EYE), "satellite-spectator-eye");
	EXPECT_STREQ(CQmIconManager::IconName(EQmIcon::SATELLITE_SPECTATOR_EYE_CLOSED), "satellite-spectator-eye-closed");

	const std::string Menus = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string IconManager = ReadTextFile("src/game/client/qm_icon_manager.cpp");
	EXPECT_NE(Menus.find("RenderFavoriteMapsIcon"), std::string::npos);
	EXPECT_NE(Menus.find("EQmIcon::BOOKMARK"), std::string::npos);
	EXPECT_EQ(Menus.find("\xF0\x9F\x94\x96"), std::string::npos);
	EXPECT_NE(IconManager.find("str_comp(pName, \"bookmark\") == 0"), std::string::npos);
	EXPECT_NE(IconManager.find("return EQmIcon::BOOKMARK;"), std::string::npos);
}

TEST(QmIconAtlas, MsdfSelectionAndReloadPolicyKeepsAlphaFallbackUsable)
{
	EXPECT_EQ(NormalizeQmIconWeight(-1), 1);
	EXPECT_EQ(NormalizeQmIconWeight(0), 0);
	EXPECT_EQ(NormalizeQmIconWeight(1), 1);
	EXPECT_EQ(NormalizeQmIconWeight(2), 2);
	EXPECT_EQ(NormalizeQmIconWeight(3), 3);
	EXPECT_EQ(NormalizeQmIconWeight(4), 1);
	EXPECT_FALSE(QmIconWeightUsesBoldFontFallback(0));
	EXPECT_TRUE(QmIconWeightUsesBoldFontFallback(1));
	EXPECT_FALSE(QmIconWeightUsesBoldFontFallback(2));
	EXPECT_FALSE(QmIconWeightUsesBoldFontFallback(3));

	EXPECT_EQ(SelectQmIconAtlasType(false, false), EQmIconAtlasType::ALPHA);
	EXPECT_EQ(SelectQmIconAtlasType(false, true), EQmIconAtlasType::ALPHA);
	EXPECT_EQ(SelectQmIconAtlasType(true, false), EQmIconAtlasType::ALPHA);
	EXPECT_EQ(SelectQmIconAtlasType(true, true), EQmIconAtlasType::MSDF);

	EXPECT_TRUE(QmIconAtlasNeedsReload(false, EQmIconAtlasType::MSDF, EQmIconAtlasType::MSDF, 1, 1, 0, 0));
	EXPECT_TRUE(QmIconAtlasNeedsReload(true, EQmIconAtlasType::MSDF, EQmIconAtlasType::MSDF, 0, 1, 0, 0));
	EXPECT_FALSE(QmIconAtlasNeedsReload(true, EQmIconAtlasType::MSDF, EQmIconAtlasType::MSDF, 1, 1, 0, 4));
	EXPECT_TRUE(QmIconAtlasNeedsReload(true, EQmIconAtlasType::ALPHA, EQmIconAtlasType::MSDF, 1, 1, 1, 0));
	EXPECT_TRUE(QmIconAtlasNeedsReload(true, EQmIconAtlasType::MSDF, EQmIconAtlasType::ALPHA, 1, 1, 0, 2));
	EXPECT_TRUE(QmIconAtlasNeedsReload(true, EQmIconAtlasType::ALPHA, EQmIconAtlasType::ALPHA, 1, 1, 1, 2));
	EXPECT_FALSE(QmIconAtlasNeedsReload(true, EQmIconAtlasType::ALPHA, EQmIconAtlasType::ALPHA, 1, 1, 2, 2));
	EXPECT_TRUE(QmIconAtlasRetryCooldownActive(99, 100));
	EXPECT_FALSE(QmIconAtlasRetryCooldownActive(100, 100));
	EXPECT_FALSE(QmIconAtlasRetryCooldownActive(101, 100));
	EXPECT_TRUE(QmIconReloadCooldownActive(99, 100, true, 1, 2, true, 1, 2, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(100, 100, true, 1, 2, true, 1, 2, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(99, 100, false, 1, 2, true, 1, 2, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(99, 100, true, 1, 2, true, 0, 2, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(99, 100, true, 1, 2, true, 1, 4, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(99, 100, true, 1, 2, true, 1, 2, false));

	EXPECT_EQ(QmIconRefreshAction(false, false, false, false), EQmIconRefreshAction::NONE);
	EXPECT_EQ(QmIconRefreshAction(false, false, true, false), EQmIconRefreshAction::RETRY_MSDF);
	EXPECT_EQ(QmIconRefreshAction(false, false, true, true), EQmIconRefreshAction::NONE);
	EXPECT_EQ(QmIconRefreshAction(true, false, false, false), EQmIconRefreshAction::RELOAD);
	EXPECT_EQ(QmIconRefreshAction(true, false, true, true), EQmIconRefreshAction::RELOAD);
	EXPECT_EQ(QmIconRefreshAction(true, true, false, false), EQmIconRefreshAction::NONE);
	EXPECT_EQ(QmIconRefreshAction(true, true, true, false), EQmIconRefreshAction::NONE);

	// MSDF probe 失败但 alpha resident 可用时，冷却结束只允许 probe，不触发完整 reload。
	const SQmIconRefreshState AlphaResidentAfterMsdfFailure{false, false, true, false};
	EXPECT_EQ(QmIconRefreshAction(AlphaResidentAfterMsdfFailure), EQmIconRefreshAction::RETRY_MSDF);
	const SQmIconRefreshState AlphaResidentDuringMsdfCooldown{false, false, true, true};
	EXPECT_EQ(QmIconRefreshAction(AlphaResidentDuringMsdfCooldown), EQmIconRefreshAction::NONE);

	// 完整 reload 失败时，只有相同目标仍受 cooldown 阻塞；权重、DPI 或 capability 变化必须脱离 cooldown。
	EXPECT_EQ(QmIconRefreshAction({true, true, false, false}), EQmIconRefreshAction::NONE);
	EXPECT_EQ(QmIconRefreshAction({true, false, false, false}), EQmIconRefreshAction::RELOAD);
	EXPECT_EQ(QmIconRefreshAction({true, true, true, false}), EQmIconRefreshAction::NONE);
	EXPECT_EQ(QmIconMsdfRunBucket(1), 0u);
	EXPECT_EQ(QmIconMsdfRunBucket(2), 1u);
	EXPECT_EQ(QmIconMsdfRunBucket(3), 2u);
	EXPECT_EQ(QmIconMsdfRunBucket(4), 2u);
	EXPECT_EQ(QmIconMsdfRunBucket(8), 3u);
	EXPECT_EQ(QmIconMsdfRunBucket(16), 4u);
	EXPECT_EQ(QmIconMsdfRunBucket(32), 5u);
	EXPECT_EQ(QmIconMsdfRunBucket(64), 6u);
	EXPECT_EQ(QmIconMsdfRunBucket(65), 7u);
	EXPECT_TRUE(QmIconReloadCooldownActive(99, 100, true, 0, 1, true, 0, 1, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(99, 100, true, 1, 1, true, 0, 1, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(99, 100, true, 0, 1, true, 0, 2, true));
	EXPECT_FALSE(QmIconReloadCooldownActive(99, 100, true, 0, 1, true, 0, 1, false));

	// 候选 atlas 失败只能保留可用 resident atlas；没有 resident atlas 才需要完整清理后重试。
	EXPECT_TRUE(QmIconAtlasCanRetainOnReloadFailure(true, EQmIconAtlasType::ALPHA, true));
	EXPECT_FALSE(QmIconAtlasCanRetainOnReloadFailure(false, EQmIconAtlasType::ALPHA, true));
	EXPECT_TRUE(QmIconTextureCanCommit(true, false));
	EXPECT_FALSE(QmIconTextureCanCommit(false, false));
	EXPECT_FALSE(QmIconTextureCanCommit(true, true));
	EXPECT_FALSE(QmIconTextureCanCommit(false, true));

	EXPECT_FALSE(QmIconAtlasCanRetainOnReloadFailure(false, EQmIconAtlasType::ALPHA, false));
	EXPECT_FALSE(QmIconAtlasCanRetainOnReloadFailure(false, EQmIconAtlasType::MSDF, true));
	EXPECT_TRUE(QmIconAtlasCanRetainOnReloadFailure(true, EQmIconAtlasType::ALPHA, false));
	EXPECT_TRUE(QmIconAtlasCanRetainOnReloadFailure(true, EQmIconAtlasType::ALPHA, true));
	EXPECT_FALSE(QmIconAtlasCanRetainOnReloadFailure(true, EQmIconAtlasType::MSDF, false));
	EXPECT_TRUE(QmIconAtlasCanRetainOnReloadFailure(true, EQmIconAtlasType::MSDF, true));

	EXPECT_FALSE(QmIconAtlasMustDropMsdf(false, EQmIconAtlasType::ALPHA));
	EXPECT_TRUE(QmIconAtlasMustDropMsdf(false, EQmIconAtlasType::MSDF));
	EXPECT_FALSE(QmIconAtlasMustDropMsdf(true, EQmIconAtlasType::ALPHA));
	EXPECT_FALSE(QmIconAtlasMustDropMsdf(true, EQmIconAtlasType::MSDF));

	EXPECT_FALSE(QmIconAtlasNeedsMsdfProbe(false, false));
	EXPECT_FALSE(QmIconAtlasNeedsMsdfProbe(false, true));
	EXPECT_TRUE(QmIconAtlasNeedsMsdfProbe(true, false));
	EXPECT_FALSE(QmIconAtlasNeedsMsdfProbe(true, true));

	EXPECT_EQ(QmIconPreferredAtlasScale(0.5f), 1);
	EXPECT_EQ(QmIconPreferredAtlasScale(1.0f), 1);
	EXPECT_EQ(QmIconPreferredAtlasScale(1.499f), 1);
	EXPECT_EQ(QmIconPreferredAtlasScale(1.5f), 2);
	EXPECT_EQ(QmIconPreferredAtlasScale(2.999f), 2);
	EXPECT_EQ(QmIconPreferredAtlasScale(3.0f), 4);
	EXPECT_EQ(QmIconAtlasScaleFallbackOrder(1), (std::array<int, 3>{1, 2, 4}));
	EXPECT_EQ(QmIconAtlasScaleFallbackOrder(2), (std::array<int, 3>{2, 4, 1}));
	EXPECT_EQ(QmIconAtlasScaleFallbackOrder(4), (std::array<int, 3>{4, 2, 1}));
	EXPECT_FLOAT_EQ(QmIconPixelScale(0, 100.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmIconPixelScale(100, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmIconPixelScale(100, -1.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmIconPixelScale(200, 100.0f), 2.0f);

	const std::string Header = ReadTextFile("src/game/client/qm_icon_manager.h");
	const std::string Source = ReadTextFile("src/game/client/qm_icon_manager.cpp");
	const std::string GameClient = ReadTextFile("src/game/client/gameclient.cpp");
	EXPECT_NE(Header.find("LoadedType != DesiredType"), std::string::npos);
	EXPECT_NE(Source.find("QmIconAtlasNeedsReload(IsReady(), m_Atlas.Type(), DesiredType"), std::string::npos);
	EXPECT_NE(Source.find("QmIconAtlasNeedsMsdfProbe(MsdfSupported, m_MsdfManifestAvailable)"), std::string::npos);
	EXPECT_NE(Source.find("const SQmIconRefreshState RefreshState"), std::string::npos);
	EXPECT_NE(Source.find("QmIconRefreshAction(RefreshState)"), std::string::npos);
	EXPECT_NE(Source.find("RetryMsdfAtlas();"), std::string::npos);
	EXPECT_NE(Source.find("m_NextReloadAttemptTime"), std::string::npos);
	EXPECT_NE(Source.find("FinishMsdfManagerCallRun();"), std::string::npos);
	EXPECT_NE(Header.find("SQmIconDiagnostics"), std::string::npos);
	EXPECT_NE(Header.find("QmIconMsdfRunBucket"), std::string::npos);
	EXPECT_NE(GameClient.find("m_QmIconManager.RefreshForCurrentDpi();"), std::string::npos);
	EXPECT_NE(GameClient.find("LogQmIconDiagnostics(m_QmIconManager.TakeDiagnostics(), Client());"), std::string::npos);
	EXPECT_NE(GameClient.find("event=icon_summary sample_frames="), std::string::npos);
	EXPECT_NE(GameClient.find("m_QmIconManager.Shutdown();"), std::string::npos);
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

	const std::string Source = ReadTextFile("src/game/client/qm_icon_manager.cpp");
	const size_t DirectRender = Source.find("bool CQmIconManager::RenderIcon(EQmIcon Icon, const CUIRect &Rect, const ColorRGBA &Color) const");
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

TEST(QmIconAtlas, PhosphorWeightSourcesRemainSeparated)
{
	struct SSelectedIcon
	{
		const char *m_pPath;
		const char *m_pVariant;
	};
	const std::array<SSelectedIcon, 8> aSelectedIcons = {{
		{"datasrc/qm_icons/phosphor_bold/icon-eye.svg", "bold"},
		{"datasrc/qm_icons/phosphor_bold/icon-satellite-swap-incoming.svg", "bold"},
		{"datasrc/qm_icons/phosphor_fill/icon-eye.svg", "fill"},
		{"datasrc/qm_icons/phosphor_fill/icon-satellite-swap-incoming.svg", "fill"},
		{"datasrc/qm_icons/phosphor_regular/icon-eye.svg", "regular"},
		{"datasrc/qm_icons/phosphor_regular/icon-satellite-swap-incoming.svg", "regular"},
		{"datasrc/qm_icons/phosphor_thin/icon-eye.svg", "thin"},
		{"datasrc/qm_icons/phosphor_thin/icon-satellite-swap-incoming.svg", "thin"},
	}};

	for(const SSelectedIcon &Icon : aSelectedIcons)
	{
		const std::string Source = ReadTextFile(Icon.m_pPath);
		const std::string SourcePath = Icon.m_pPath;
		EXPECT_NE(Source.find("<svg"), std::string::npos) << Icon.m_pPath;
		EXPECT_NE(Source.find("viewBox=\"0 0 256 256\""), std::string::npos) << Icon.m_pPath;
		EXPECT_NE(SourcePath.find("phosphor_" + std::string(Icon.m_pVariant)), std::string::npos) << Icon.m_pPath;
	}

	const std::string License = ReadTextFile("datasrc/qm_icons/LICENSE_PHOSPHOR.txt");
	EXPECT_NE(License.find("MIT License"), std::string::npos);
	EXPECT_NE(License.find("Copyright (c) 2020 Phosphor Icons"), std::string::npos);
}

TEST(QmIconAtlas, GeneratedManifestsContainEveryRuntimeIcon)
{
	constexpr const char *apWeights[] = {"thin", "regular", "bold", "fill"};
	constexpr int aScales[] = {1, 2, 4};
	for(const char *pWeight : apWeights)
	{
		for(const int Scale : aScales)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "data/qmclient/icons/qm_icons_%s_%dx.json", pWeight, Scale);
			const std::string Json = ReadTextFile(aPath);
			ASSERT_FALSE(Json.empty()) << aPath;

			json_value *pRoot = JsonParse(Json.c_str(), Json.size());
			ASSERT_NE(pRoot, nullptr) << aPath;

			const json_value *pAtlas = JsonObject(pRoot, "atlas");
			const json_value *pIcons = JsonObject(pRoot, "icons");
			const int AtlasWidth = JsonInt(pAtlas, "width");
			const int AtlasHeight = JsonInt(pAtlas, "height");
			EXPECT_EQ(JsonInt(pRoot, "scale"), Scale);
			EXPECT_EQ(JsonInt(pAtlas, "padding"), 4 * Scale);

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

				EXPECT_EQ(W, 24 * Scale) << pIconName;
				EXPECT_EQ(H, 24 * Scale) << pIconName;
				EXPECT_GE(X, 0) << pIconName;
				EXPECT_GE(Y, 0) << pIconName;
				EXPECT_LE(X + W, AtlasWidth) << pIconName;
				EXPECT_LE(Y + H, AtlasHeight) << pIconName;
			}

			json_value_free(pRoot);
		}
	}
}

TEST(QmIconAtlas, GeneratedMsdfManifestsContainEveryRuntimeIcon)
{
	constexpr const char *apWeights[] = {"thin", "regular", "bold", "fill"};
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
		constexpr int Padding = 8;
		constexpr int CellSize = FieldSize + Padding * 2;
		const int IconCount = static_cast<int>(pIcons->u.object.length);
		EXPECT_GE(IconCount, static_cast<int>(EQmIcon::COUNT));
		int Columns = 1;
		while(Columns * Columns < IconCount)
			++Columns;
		const int Rows = (IconCount + Columns - 1) / Columns;
		EXPECT_EQ(JsonInt(pRoot, "version"), 2);
		EXPECT_STREQ(JsonString(pRoot, "kind"), "msdf");
		EXPECT_EQ(JsonInt(pRoot, "px_range"), 6);
		EXPECT_EQ(AtlasWidth, Columns * CellSize);
		EXPECT_EQ(AtlasHeight, Rows * CellSize);
		EXPECT_EQ(JsonInt(pAtlas, "padding"), Padding);

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

			EXPECT_EQ(W, FieldSize) << pIconName;
			EXPECT_EQ(H, FieldSize) << pIconName;
			EXPECT_GE(X, 0) << pIconName;
			EXPECT_GE(Y, 0) << pIconName;
			EXPECT_LE(X + W, AtlasWidth) << pIconName;
			EXPECT_LE(Y + H, AtlasHeight) << pIconName;
		}

		json_value_free(pRoot);
	}
}

TEST(QmIconAtlas, MsdfShadersUseDerivativeAntialiasingOnBothBackends)
{
	for(const char *pPath : {"data/shader/textured_msdf.frag", "data/shader/vulkan/textured_msdf.frag"})
	{
		const std::string Source = ReadTextFile(pPath);
		EXPECT_NE(Source.find("Median"), std::string::npos) << pPath;
		EXPECT_NE(Source.find("fwidth(TexCoord)"), std::string::npos) << pPath;
		EXPECT_NE(Source.find("ScreenPxRange"), std::string::npos) << pPath;
	}
}

TEST(QmIconAtlas, RuntimeLoaderRejectsIncompleteOrDuplicateKnownManifestEntries)
{
	const std::string Source = ReadTextFile("src/game/client/qm_icon_manager.cpp");
	const size_t LoadManifest = Source.find("bool CQmIconManager::LoadManifest(");
	const size_t PixelAlignedRect = Source.find("CUIRect CQmIconManager::PixelAlignedRect", LoadManifest);
	ASSERT_NE(LoadManifest, std::string::npos);
	ASSERT_NE(PixelAlignedRect, std::string::npos);
	const std::string Loader = Source.substr(LoadManifest, PixelAlignedRect - LoadManifest);
	EXPECT_NE(Loader.find("bool InvalidKnownEntry = false"), std::string::npos);
	EXPECT_NE(Loader.find("if(Entry.m_Valid)"), std::string::npos);
	EXPECT_NE(Loader.find("LoadedIconCount != static_cast<int>(EQmIcon::COUNT)"), std::string::npos);
	EXPECT_NE(Loader.find("QmIconTextureCanCommit(Texture.IsValid(), Texture.IsNullTexture())"), std::string::npos);
	EXPECT_NE(Loader.find("Atlas.m_LoadedIconCount = LoadedIconCount"), std::string::npos);
	EXPECT_NE(Loader.find("Atlas.m_Type = Msdf ? CQmIconAtlas::EType::MSDF : CQmIconAtlas::EType::ALPHA"), std::string::npos);

	const std::string Header = ReadTextFile("src/game/client/qm_icon_manager.h");
	EXPECT_NE(Header.find("m_LoadedIconCount == static_cast<int>(EQmIcon::COUNT)"), std::string::npos);
	EXPECT_NE(Header.find("void Swap(CQmIconAtlas &Other)"), std::string::npos);
}

TEST(QmIconAtlas, ReloadCommitsCandidateOnlyAfterCompleteLoad)
{
	const std::string Source = ReadTextFile("src/game/client/qm_icon_manager.cpp");
	const size_t Reload = Source.find("bool CQmIconManager::Reload()");
	const size_t LoadManifestForScale = Source.find("bool CQmIconManager::LoadManifestForScale", Reload);
	ASSERT_NE(Reload, std::string::npos);
	ASSERT_NE(LoadManifestForScale, std::string::npos);
	const std::string ReloadBody = Source.substr(Reload, LoadManifestForScale - Reload);
	EXPECT_NE(ReloadBody.find("CQmIconAtlas Candidate"), std::string::npos);
	EXPECT_NE(ReloadBody.find("if(!Success)"), std::string::npos);
	EXPECT_NE(ReloadBody.find("m_Atlas.Swap(Candidate);"), std::string::npos);
	EXPECT_NE(ReloadBody.find("QmIconAtlasCanRetainOnReloadFailure(IsReady(), m_Atlas.Type(), MsdfSupported)"), std::string::npos);
	EXPECT_NE(ReloadBody.find("if(!RetainedResidentAtlas)"), std::string::npos);

	const std::string Header = ReadTextFile("src/game/client/qm_icon_manager.h");
	EXPECT_NE(Header.find("void Swap(CQmIconAtlas &Other)"), std::string::npos);
}

TEST(QmIconAtlas, MsdfRetryDoesNotReloadTheResidentAlphaAtlas)
{
	const std::string Source = ReadTextFile("src/game/client/qm_icon_manager.cpp");
	const size_t Retry = Source.find("bool CQmIconManager::RetryMsdfAtlas()");
	const size_t LoadManifestForScale = Source.find("bool CQmIconManager::LoadManifestForScale", Retry);
	ASSERT_NE(Retry, std::string::npos);
	ASSERT_NE(LoadManifestForScale, std::string::npos);
	const std::string RetryBody = Source.substr(Retry, LoadManifestForScale - Retry);
	EXPECT_NE(RetryBody.find("LoadMsdfManifest(Candidate)"), std::string::npos);
	EXPECT_EQ(RetryBody.find("LoadManifestForScale"), std::string::npos);
	EXPECT_NE(RetryBody.find("m_Atlas.Swap(Candidate);"), std::string::npos);
}

TEST(QmIconAtlas, DiagnosticsKeepAtlasAndRendererCountersSeparated)
{
	const std::string Header = ReadTextFile("src/game/client/qm_icon_manager.h");
	const std::string IconManager = ReadTextFile("src/game/client/qm_icon_manager.cpp");
	const std::string Graphics = ReadTextFile("src/engine/client/graphics_threaded.cpp");
	const std::string GraphicsHeader = ReadTextFile("src/engine/client/graphics_threaded.h");
	const std::string GameClient = ReadTextFile("src/game/client/gameclient.cpp");

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
	EXPECT_NE(IconManager.find("m_Diagnostics.m_MsdfProbes++"), std::string::npos);
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
	EXPECT_NE(GraphicsHeader.find("double m_MacosMetalWaitForIdleMsSum = 0.0;"), std::string::npos);
	EXPECT_NE(GraphicsHeader.find("uint64_t m_MacosMetalWaitForIdleCount = 0;"), std::string::npos);
	EXPECT_NE(Graphics.find("sample_frames=120"), std::string::npos);
	EXPECT_NE(Graphics.find("submit_duration_ms_sum"), std::string::npos);
	EXPECT_NE(Graphics.find("submit_duration_ms_avg"), std::string::npos);
	EXPECT_NE(Graphics.find("const bool PreviousMacosDiagnostics = m_MacosGraphicsDiagnosticsEnabled;"), std::string::npos);
	EXPECT_NE(Graphics.find("if(PreviousMacosDiagnostics)"), std::string::npos);
	EXPECT_NE(Graphics.find("msdf_commands_sum"), std::string::npos);
	EXPECT_NE(Graphics.find("msdf_flushes_sum"), std::string::npos);
	EXPECT_NE(Graphics.find("metal_wait_for_idle_count"), std::string::npos);
	EXPECT_NE(Graphics.find("metal_wait_for_idle_ms_avg"), std::string::npos);
	EXPECT_NE(Graphics.find("if(!MacosDiagnostics)\n\t{\n\t\tif(PreviousMacosDiagnostics)"), std::string::npos);
	EXPECT_NE(Graphics.find("unlimited_config=%d"), std::string::npos);
	EXPECT_NE(Graphics.find("gfx_refresh_rate=%d"), std::string::npos);
	EXPECT_NE(Graphics.find("cl_refresh_rate=%d"), std::string::npos);
	EXPECT_NE(Graphics.find("cl_refresh_rate_inactive=%d"), std::string::npos);
	EXPECT_NE(Graphics.find("dbg_graphs=%d"), std::string::npos);
	EXPECT_NE(Graphics.find("async_render_old=%d"), std::string::npos);
	EXPECT_NE(GameClient.find("QmPerfLogPayloadForce(\"perf/icons\""), std::string::npos);
	EXPECT_NE(GameClient.find("if(QmPerfEnabled())\n\t\tLogQmIconDiagnostics(m_QmIconManager.TakeDiagnostics(), Client());"), std::string::npos);
	EXPECT_NE(GameClient.find("LogQmIconDiagnostics(m_QmIconManager.TakeDiagnostics(), Client());"), std::string::npos);
	EXPECT_NE(GameClient.find("msdf_manager_call_run_max"), std::string::npos);

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

TEST(QmIconAtlas, ConfiguredIconWeightUsesTheExistingContainerInvalidationPath)
{
	const std::string GameClient = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string Header = ReadTextFile("src/game/client/gameclient.h");
	const std::string Settings = ReadTextFile("src/game/client/components/menus_settings.cpp");

	EXPECT_NE(Header.find("void SyncQmUiIconWeight();"), std::string::npos);
	const size_t Sync = GameClient.find("void CGameClient::SyncQmUiIconWeight()");
	ASSERT_NE(Sync, std::string::npos);
	const size_t PrivacyRefresh = GameClient.find("void CGameClient::RefreshStreamerSkinPrivacyAfterStateChange", Sync);
	ASSERT_NE(PrivacyRefresh, std::string::npos);
	const std::string SyncBody = GameClient.substr(Sync, PrivacyRefresh - Sync);
	EXPECT_NE(SyncBody.find("TextRender()->SetIconFontWeight"), std::string::npos);
	EXPECT_NE(SyncBody.find("QmIconWeightUsesBoldFontFallback"), std::string::npos);
	EXPECT_NE(SyncBody.find("m_QmIconManager.RefreshForCurrentDpi();"), std::string::npos);
	EXPECT_NE(SyncBody.find("OnWindowResize();"), std::string::npos);
	EXPECT_NE(Settings.find("GameClient()->SyncQmUiIconWeight();"), std::string::npos);
}

TEST(QmIconAtlas, GlyphPathsUsePhosphorAndRestoreTextRenderState)
{
	const std::array<const char *, 3> apGlyphSources = {
		"src/game/client/QmUi/UiButtons.cpp",
		"src/game/client/QmUi/UiForms.cpp",
		"src/game/client/QmUi/SettingsCard.cpp",
	};
	for(const char *pPath : apGlyphSources)
	{
		const std::string Source = ReadTextFile(pPath);
		EXPECT_NE(Source.find("GetFontPreset"), std::string::npos) << pPath;
		EXPECT_NE(Source.find("SetFontPreset(PreviousPreset)"), std::string::npos) << pPath;
		EXPECT_NE(Source.find("SetRenderFlags(PreviousFlags)"), std::string::npos) << pPath;
		EXPECT_NE(Source.find("TextColor(PreviousColor)"), std::string::npos) << pPath;
	}

	const std::string SettingsCard = ReadTextFile("src/game/client/QmUi/SettingsCard.cpp");
	const size_t CollapseButton = SettingsCard.find("void RenderSettingsCardCollapseButton");
	ASSERT_NE(CollapseButton, std::string::npos);
	EXPECT_NE(SettingsCard.find("SetFontPreset(EFontPreset::ICON_FONT_BOLD)", CollapseButton), std::string::npos);
	EXPECT_NE(SettingsCard.find("FontIcons::FONT_ICON_CHEVRON_DOWN", CollapseButton), std::string::npos);
	EXPECT_NE(SettingsCard.find("FontIcons::FONT_ICON_CHEVRON_UP", CollapseButton), std::string::npos);

	const std::string Text = ReadTextFile("src/engine/client/text.cpp");
	EXPECT_NE(Text.find("if(m_FontPreset == EFontPreset::ICON_FONT)"), std::string::npos);
	EXPECT_NE(Text.find("m_pGlyphMap->SetFontPreset(EFontPreset::ICON_FONT);"), std::string::npos);
}

TEST(QmIconAtlas, VulkanMsdfPipelineLifecycleUsesFallbackOnlyBeforeCapabilityIsExposed)
{
	const std::string Source = ReadTextFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::string BackendHeader = ReadTextFile("src/engine/client/backend_sdl.h");
	const size_t Create = Source.find("m_TexturedMsdfPipelineValid = CreateTexturedMsdfGraphicsPipeline");
	const size_t Gaussian = Source.find("m_GaussianBlurPipelineValid = CreateGaussianBlurGraphicsPipeline", Create);
	ASSERT_NE(Create, std::string::npos);
	ASSERT_NE(Gaussian, std::string::npos);
	const std::string InitSection = Source.substr(Create, Gaussian - Create);
	EXPECT_NE(InitSection.find("m_TexturedMsdfPipeline.Destroy"), std::string::npos);
	EXPECT_NE(InitSection.find("falling back to alpha icon atlas"), std::string::npos);
	const size_t RequiredCheck = InitSection.find("if(m_TexturedMsdfPipelineRequired)");
	const size_t FallbackWarning = InitSection.find("falling back to alpha icon atlas");
	const size_t CapabilityEnable = InitSection.find("m_TexturedMsdfPipelineRequired = true");
	ASSERT_NE(RequiredCheck, std::string::npos);
	ASSERT_NE(FallbackWarning, std::string::npos);
	ASSERT_NE(CapabilityEnable, std::string::npos);
	EXPECT_NE(InitSection.find("return -1", RequiredCheck), std::string::npos);
	EXPECT_EQ(InitSection.substr(0, RequiredCheck).find("return -1"), std::string::npos);
	EXPECT_LT(RequiredCheck, FallbackWarning);
	EXPECT_LT(FallbackWarning, CapabilityEnable);
	EXPECT_NE(InitSection.find("m_TexturedMsdfPipelineRequired = true"), std::string::npos);
	EXPECT_NE(Source.find("m_TexturedMsdfPipelineValid = false"), std::string::npos);
	EXPECT_NE(Source.find("m_pBackendCapabilities = pCommand->m_pCapabilities"), std::string::npos);
	EXPECT_NE(Source.find("SyncTexturedMsdfCapability()"), std::string::npos);
	EXPECT_NE(Source.find("m_pBackendCapabilities->m_TexturedMsdf.store(m_TexturedMsdfPipelineValid"), std::string::npos);
	EXPECT_NE(BackendHeader.find("std::atomic<bool> m_TexturedMsdf{false};"), std::string::npos);
	EXPECT_NE(BackendHeader.find("m_Capabilities.m_TexturedMsdf.load(std::memory_order_acquire)"), std::string::npos);
	EXPECT_NE(Source.find("if(!m_TexturedMsdfPipelineValid)\n\t\t\treturn true;"), std::string::npos);
	EXPECT_NE(Source.find("if(RecreateSwapChain() != 0)\n\t\t\t\treturn false;"), std::string::npos);
	EXPECT_EQ(Source.find("m_pCapabilities->m_TexturedMsdf = m_TexturedMsdfPipelineValid"), std::string::npos);
	EXPECT_NE(Source.find("VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM, true, false"), std::string::npos);
}

TEST(QmIconAtlas, OpenGlMsdfCapabilityIsClearedWithProgramLifecycle)
{
	const std::string Backend = ReadTextFile("src/engine/client/backend/opengl/backend_opengl.cpp");
	const std::string BackendHeader = ReadTextFile("src/engine/client/backend/opengl/backend_opengl.h");
	const std::string ModernBackend = ReadTextFile("src/engine/client/backend/opengl/backend_opengl3.cpp");

	EXPECT_NE(BackendHeader.find("SBackendCapabilities *m_pBackendCapabilities = nullptr;"), std::string::npos);
	EXPECT_NE(Backend.find("m_pBackendCapabilities = pCommand->m_pCapabilities;"), std::string::npos);
	EXPECT_NE(Backend.find("m_pBackendCapabilities->m_TexturedMsdf.store(false, std::memory_order_release);"), std::string::npos);

	const size_t Shutdown = ModernBackend.find("void CCommandProcessorFragment_OpenGL3_3::Cmd_Shutdown");
	ASSERT_NE(Shutdown, std::string::npos);
	const size_t ProgramDelete = ModernBackend.find("m_pTexturedMsdfProgram->DeleteProgram();", Shutdown);
	ASSERT_NE(ProgramDelete, std::string::npos);
	const std::string ShutdownSection = ModernBackend.substr(Shutdown, ProgramDelete - Shutdown);
	EXPECT_NE(ShutdownSection.find("m_pBackendCapabilities->m_TexturedMsdf.store(false, std::memory_order_release);"), std::string::npos);
	EXPECT_NE(ShutdownSection.find("m_pBackendCapabilities = nullptr;"), std::string::npos);
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

TEST(QmIconAtlas, DiagnosticWindowKeepsTotalsPeaksAndResourceEvents)
{
	SQmIconDiagnosticsWindow Window;
	SQmIconDiagnostics Frame;
	Frame.m_AlphaIconDraws = 2;
	Frame.m_MsdfIconDraws = 5;
	Frame.m_MaxMsdfManagerCallRun = 3;
	Frame.m_MsdfManagerCallRunBuckets[2] = 1;
	for(int i = 0; i < 144; ++i)
		EXPECT_FALSE(Window.Add(Frame));
	EXPECT_EQ(Window.m_Frames, 144U);
	EXPECT_EQ(Window.m_Total.m_AlphaIconDraws, 288U);
	EXPECT_EQ(Window.m_Total.m_MsdfIconDraws, 720U);
	EXPECT_EQ(Window.m_Total.m_MsdfManagerCallRunBuckets[2], 144U);
	EXPECT_EQ(Window.m_Total.m_MaxMsdfManagerCallRun, 3U);
	EXPECT_EQ(Window.m_MaxMsdfDraws, 5U);
	Frame.m_MsdfIconDraws = 17;
	Frame.m_MaxMsdfManagerCallRun = 9;
	Frame.m_TextureLoadFailures = 1;
	EXPECT_TRUE(Window.Add(Frame));
	EXPECT_EQ(Window.m_Total.m_TextureLoadFailures, 1U);
	EXPECT_EQ(Window.m_MaxMsdfDraws, 17U);
	EXPECT_EQ(Window.m_Total.m_MaxMsdfManagerCallRun, 9U);
	Window = {};
	EXPECT_EQ(Window.m_Frames, 0U);
	EXPECT_EQ(Window.m_Total.m_MsdfIconDraws, 0U);
}
