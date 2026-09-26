// 请抬头享受阳光｜日子很好 我很我---------致咩子
#define CONF_TEST 1

#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>

#include <string>

// 设置帧调度器与自适应预算：暴露令牌字段、预算输入输出映射与压力降级。
// 本域同时包含对生产接口 SettingsAdaptiveBudgetStep 的行为断言与对预算日志字段的静态合同。
TEST(QmMonitoringFrameBudgetContract, SettingsFrameSchedulerExposesResourceAndTextBudgets)
{
	const std::string Header = ReadRepoFile("src/game/client/components/settings_resource_jobs.h");
	const std::string Source = ReadRepoFile("src/game/client/components/settings_resource_jobs.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");

	EXPECT_NE(Header.find("m_FrameId"), std::string::npos);
	EXPECT_NE(Header.find("m_aOperation"), std::string::npos);
	EXPECT_NE(Header.find("m_aPage"), std::string::npos);
	EXPECT_NE(Header.find("m_aTab"), std::string::npos);
	EXPECT_NE(Header.find("m_Subtab"), std::string::npos);
	EXPECT_NE(Header.find("m_aContext"), std::string::npos);
	EXPECT_NE(Header.find("m_TabSwitchFirstFrame"), std::string::npos);
	EXPECT_NE(Header.find("m_FramePressure"), std::string::npos);
	EXPECT_NE(Header.find("m_ResourceUploadTokens"), std::string::npos);
	EXPECT_NE(Header.find("m_TextContainerTokens"), std::string::npos);
	EXPECT_NE(Header.find("m_GlyphRasterizeTokens"), std::string::npos);
	EXPECT_NE(Header.find("m_GlyphUploadTokens"), std::string::npos);
	EXPECT_NE(Header.find("m_ParagraphLayoutTokens"), std::string::npos);
	EXPECT_NE(Header.find("m_MetadataLayoutTokens"), std::string::npos);
	EXPECT_NE(Header.find("m_PreviewArtifactTokens"), std::string::npos);
	EXPECT_NE(Header.find("m_TextureUploadTokens"), std::string::npos);
	EXPECT_NE(Header.find("m_CardDrawTokens"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_ResourceUploadTokens = Output.m_GpuUploadTokens;"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_TextContainerTokens = Output.m_TextPrebuildTokens;"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_GlyphRasterizeTokens"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_GlyphUploadTokens"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_ParagraphLayoutTokens"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_MetadataLayoutTokens"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_PreviewArtifactTokens"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_TextureUploadTokens"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_CardDrawTokens"), std::string::npos);
	EXPECT_NE(Menus.find("resource_upload_tokens=%d"), std::string::npos);
	EXPECT_NE(Menus.find("text_container_tokens=%d"), std::string::npos);
	EXPECT_NE(Menus.find("glyph_rasterize_tokens=%d"), std::string::npos);
	EXPECT_NE(Menus.find("glyph_upload_tokens=%d"), std::string::npos);
	EXPECT_NE(Menus.find("paragraph_layout_tokens=%d"), std::string::npos);
	EXPECT_NE(Menus.find("metadata_layout_tokens=%d"), std::string::npos);
	EXPECT_NE(Menus.find("preview_artifact_tokens=%d"), std::string::npos);
	EXPECT_NE(Menus.find("texture_upload_tokens=%d"), std::string::npos);
	EXPECT_NE(Menus.find("card_draw_tokens=%d"), std::string::npos);

	SSettingsAdaptiveBudgetState State;
	SSettingsAdaptiveBudgetInput Input;
	Input.m_FrameMsAverage = 2.0f;
	Input.m_FrameMsP95 = 3.0f;
	Input.m_BackgroundBacklog = 4;
	const SSettingsAdaptiveBudgetOutput Output = SettingsAdaptiveBudgetStep(Input, State);
	EXPECT_EQ(Output.m_ResourceUploadTokens, Output.m_GpuUploadTokens);
	EXPECT_EQ(Output.m_TextContainerTokens, Output.m_TextPrebuildTokens);
	EXPECT_GE(Output.m_GlyphRasterizeTokens, 1);
	EXPECT_GE(Output.m_GlyphUploadTokens, 1);
	EXPECT_GE(Output.m_ParagraphLayoutTokens, 1);
	EXPECT_GE(Output.m_MetadataLayoutTokens, 1);
	EXPECT_GE(Output.m_PreviewArtifactTokens, 1);
	EXPECT_GE(Output.m_TextureUploadTokens, 1);
	EXPECT_GE(Output.m_CardDrawTokens, 1);

	SSettingsAdaptiveBudgetInput PressureInput;
	PressureInput.m_FrameMsAverage = 20.0f;
	PressureInput.m_FrameMsP95 = 20.0f;
	PressureInput.m_TargetFrameMs = 8.333f;
	PressureInput.m_BackgroundBacklog = 4;
	const SSettingsAdaptiveBudgetOutput PressureOutput = SettingsAdaptiveBudgetStep(PressureInput, State);
	EXPECT_EQ(PressureOutput.m_ParagraphLayoutTokens, 0);
	EXPECT_LE(PressureOutput.m_PreviewArtifactTokens, 1);
}

TEST(QmMonitoringFrameBudgetContract, UiFrameSchedulerOwnsTextAndResourceBudgets)
{
	const std::string Assets = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Ingame = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Skins = ReadRepoFile("src/game/client/components/skins.cpp");

	EXPECT_NE(Menus.find("BeginSettingsUiFrameScheduler"), std::string::npos);
	EXPECT_NE(Menus.find("CurrentSettingsUiFrameBudget"), std::string::npos);
	EXPECT_NE(Assets.find("CurrentSettingsUiFrameBudget()"), std::string::npos);
	EXPECT_NE(Assets.find("AdaptiveBudget.m_MetadataLayoutTokens"), std::string::npos);
	EXPECT_NE(Assets.find("AdaptiveBudget.m_PreviewArtifactTokens"), std::string::npos);
	EXPECT_NE(Assets.find("AdaptiveBudget.m_TextureUploadTokens"), std::string::npos);
	EXPECT_NE(Ingame.find("GameClient()->FrameScheduler()->ComputeBudget(EFrameSchedulerConsumer::IngameServerInfo, TextBudgetInput)"), std::string::npos);
	EXPECT_NE(Ingame.find("m_IngameTextFrameBudget.m_TextContainerTokens"), std::string::npos);
	EXPECT_NE(Ingame.find("m_IngameTextFrameBudget.m_ParagraphLayoutTokens"), std::string::npos);
	EXPECT_NE(Skins.find("SettingsGpuUploadFrameBudgetForFrame()"), std::string::npos);
	EXPECT_NE(Skins.find("SettingsResourcePreviewConsumeUploadBudget(SkinPreviewUploadBudget"), std::string::npos);
}
