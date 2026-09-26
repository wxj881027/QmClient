// Skins 静态源码合同：skins_queue_contract_test.cpp.
// 源码合同测试：皮肤资源、菜单集成和队列策略。运行时行为保留在 skins_test.cpp.
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/gfx/image_loader.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/skins.h>
#include <game/client/render.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <fstream>
#include <limits>
#include <list>
#include <sstream>

extern CDataContainer *g_pData;

static std::string FunctionBody(const std::string &Source, const std::string &Signature)
{
	const size_t FunctionStart = Source.find(Signature);
	EXPECT_NE(FunctionStart, std::string::npos) << Signature;
	const size_t BodyStart = Source.find("{", FunctionStart);
	EXPECT_NE(BodyStart, std::string::npos) << Signature;
	int Depth = 0;
	for(size_t Index = BodyStart; Index < Source.size(); ++Index)
	{
		if(Source[Index] == '{')
			++Depth;
		else if(Source[Index] == '}')
		{
			--Depth;
			if(Depth == 0)
				return Source.substr(BodyStart, Index - BodyStart);
		}
	}
	ADD_FAILURE() << Signature;
	return {};
}

TEST(SkinsContract, SkinQueueIntervalUsesMilliseconds)
{
	const std::string Config = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const size_t UpdatePos = Source.find("void CSkins::UpdateSkinQueue(std::chrono::nanoseconds Now, int Dummy)");
	ASSERT_NE(UpdatePos, std::string::npos);
	const size_t UpdateEnd = Source.find("void CSkins::SyncSkinQueueFromMapPlayers(int Dummy)", UpdatePos);
	ASSERT_NE(UpdateEnd, std::string::npos);
	const std::string UpdateBody = Source.substr(UpdatePos, UpdateEnd - UpdatePos);

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmSkinQueueInterval, qm_skin_queue_interval, 600, 0, 120000"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmDummySkinQueueInterval, qm_dummy_skin_queue_interval, 600, 0, 120000"), std::string::npos);
	EXPECT_NE(Config.find("Skin queue switch interval (ms, 0=no timed rotation, random start only)"), std::string::npos);
	EXPECT_NE(Source.find("static constexpr int SKIN_QUEUE_INTERVAL_UNITS_PER_SECOND = 1000;"), std::string::npos);
	EXPECT_NE(UpdateBody.find("std::chrono::milliseconds(QueueInterval)"), std::string::npos);
	EXPECT_EQ(UpdateBody.find("QueueInterval * 1000"), std::string::npos);
	EXPECT_NE(UpdateBody.find("const int QueueInterval = SkinQueueIntervalVar(Dummy);"), std::string::npos);
	EXPECT_EQ(UpdateBody.find("maximum(1, SkinQueueIntervalVar"), std::string::npos);
	EXPECT_EQ(Source.find("SKIN_QUEUE_INTERVAL_UNITS_PER_SECOND = 10;"), std::string::npos);
	EXPECT_EQ(Config.find("皮肤队列切换间隔（0.1 秒）"), std::string::npos);

	const std::string ClientSource = ReadTestSourceFile("src/engine/client/client.cpp");
	EXPECT_EQ(ClientSource.find("g_Config.m_QmSkinQueueInterval *= 10"), std::string::npos);
	EXPECT_EQ(ClientSource.find("g_Config.m_QmDummySkinQueueInterval *= 10"), std::string::npos);
}

TEST(SkinsContract, SkinQueueRotationUsesExplicitEnableSwitchAndBoundedInterval)
{
	// Intent only: rotation has an explicit enable switch and a bounded (0..120000ms)
	// interval, where 0 means no timed rotation (random start events only). Update
	// gates on the enable flag. Widget/layout details are intentionally not asserted.
	const std::string Config = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Menus = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const size_t UpdatePos = Source.find("void CSkins::UpdateSkinQueue(std::chrono::nanoseconds Now, int Dummy)");
	ASSERT_NE(UpdatePos, std::string::npos);
	const size_t UpdateEnd = Source.find("void CSkins::SyncSkinQueueFromMapPlayers(int Dummy)", UpdatePos);
	ASSERT_NE(UpdateEnd, std::string::npos);
	const std::string UpdateBody = Source.substr(UpdatePos, UpdateEnd - UpdatePos);

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmSkinQueueEnabled, qm_skin_queue_enabled, 1, 0, 1"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmDummySkinQueueEnabled, qm_dummy_skin_queue_enabled, 1, 0, 1"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmSkinQueueLength, qm_skin_queue_length, 20, 0, 1024"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmDummySkinQueueLength, qm_dummy_skin_queue_length, 20, 0, 1024"), std::string::npos);
	EXPECT_NE(Menus.find("Localize(\"Enable skin queue rotation\")"), std::string::npos);
	EXPECT_NE(Menus.find("QueueIntervalOptions.m_pSuffix = \"ms\";"), std::string::npos);
	EXPECT_NE(Menus.find("ui_widget::NumericField(TeeSkinQueueIntervalCtx, &s_aQueueIntervalStates[QueueDummy], &QueueInterval, &QueueInterval, 0, 120000, IntervalInputGroup, QueueIntervalOptions);"), std::string::npos);
	EXPECT_EQ(Menus.find("QueueInterval = maximum(QueueIntervalInput.GetInteger(), 1);"), std::string::npos);
	EXPECT_NE(UpdateBody.find("!SkinQueueEnabledVar(Dummy)"), std::string::npos);
	EXPECT_NE(UpdateBody.find("m_aSkinQueueLastUpdate[Dummy].reset();"), std::string::npos);
	EXPECT_EQ(UpdateBody.find("QueueInterval <= 0"), std::string::npos);
}

TEST(SkinsContract, DisabledSkinQueuePreventsMapRotateAutoApply)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const size_t SyncPos = Source.find("void CSkins::SyncSkinQueueFromMapPlayers(int Dummy)");
	ASSERT_NE(SyncPos, std::string::npos);
	const size_t SyncEnd = Source.find("void CSkins::UpdateUnloadSkins", SyncPos);
	ASSERT_NE(SyncEnd, std::string::npos);
	const std::string SyncBody = Source.substr(SyncPos, SyncEnd - SyncPos);

	EXPECT_NE(SyncBody.find("m_aSkinQueueElapsed[Dummy] = 0ns;"), std::string::npos);
	EXPECT_NE(SyncBody.find("m_aSkinQueueLastUpdate[Dummy].reset();"), std::string::npos);
	EXPECT_NE(SyncBody.find("if(SkinQueueEnabledVar(Dummy))"), std::string::npos);
	EXPECT_NE(SyncBody.find("ApplySkinQueueCurrent(Dummy);"), std::string::npos);
	EXPECT_LT(SyncBody.find("if(SkinQueueEnabledVar(Dummy))"), SyncBody.find("ApplySkinQueueCurrent(Dummy);"));
}

TEST(SkinsContract, SkinQueueCatchUpAppliesOnlyFinalStepOncePerFrame)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const size_t UpdatePos = Source.find("void CSkins::UpdateSkinQueue(std::chrono::nanoseconds Now, int Dummy)");
	ASSERT_NE(UpdatePos, std::string::npos);
	const size_t UpdateEnd = Source.find("void CSkins::SyncSkinQueueFromMapPlayers(int Dummy)", UpdatePos);
	ASSERT_NE(UpdateEnd, std::string::npos);
	const std::string UpdateBody = Source.substr(UpdatePos, UpdateEnd - UpdatePos);

	EXPECT_NE(UpdateBody.find("const int64_t StepsElapsed ="), std::string::npos);
	EXPECT_NE(UpdateBody.find("m_aSkinQueueElapsed[Dummy] -= Interval * StepsElapsed;"), std::string::npos);
	EXPECT_NE(UpdateBody.find("QueueIndex = (QueueIndex + (int)(StepsElapsed % QueueActiveCount)) % QueueActiveCount;"), std::string::npos);
	EXPECT_NE(UpdateBody.find("ApplySkinQueueCurrent(Dummy);"), std::string::npos);
	EXPECT_EQ(UpdateBody.find("while(m_aSkinQueueElapsed[Dummy] >= Interval)"), std::string::npos);
}

TEST(SkinsContract, TeeRenderInfoValidityIncludesSixupBodyTexture)
{
	const std::string Header = ReadTestSourceFile("src/game/client/render.h");
	const size_t ValidPos = Header.find("bool Valid() const");
	ASSERT_NE(ValidPos, std::string::npos);
	const size_t ManagedInfoPos = Header.find("class CManagedTeeRenderInfo", ValidPos);
	ASSERT_NE(ManagedInfoPos, std::string::npos);
	const std::string ValidBody = Header.substr(ValidPos, ManagedInfoPos - ValidPos);

	EXPECT_NE(ValidBody.find("m_OriginalRenderSkin.m_Body"), std::string::npos);
	EXPECT_NE(ValidBody.find("IsDrawableTexture("), std::string::npos);
	EXPECT_NE(ValidBody.find("m_aSixup"), std::string::npos);
	EXPECT_NE(ValidBody.find("protocol7::SKINPART_BODY"), std::string::npos);
	EXPECT_NE(ValidBody.find("IsDrawableTexture(Sixup.PartTexture(protocol7::SKINPART_BODY))"), std::string::npos);
}

TEST(SkinsContract, TeeSettingsListUsesIdleBackgroundRequestsAfterVisibleSettle)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t RenderTeePos = Source.find("void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_NE(RenderTeePos, std::string::npos);
	const size_t RenderTeeEnd = Source.find("void CMenus::RenderSettingsAppearance", RenderTeePos);
	ASSERT_NE(RenderTeeEnd, std::string::npos);
	const std::string RenderTeeBody = Source.substr(RenderTeePos, RenderTeeEnd - RenderTeePos);

	EXPECT_NE(RenderTeeBody.find("RequestLoad(ESettingsResourcePriority::VISIBLE)"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("RequestLoad(ESettingsResourcePriority::PREFETCH)"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("static std::vector<size_t> s_vVisibleSkinIndices;"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("vVisibleSkinIndices.reserve(vSkinList.size())"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("vVisibleSkinIndices.push_back(i);"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("for(auto It = vVisibleSkinIndices.rbegin(); It != vVisibleSkinIndices.rend(); ++It)"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("std::binary_search(vVisibleSkinIndices.begin(), vVisibleSkinIndices.end(), BackgroundIndex)"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("std::find(vVisibleSkinIndices.begin(), vVisibleSkinIndices.end(), BackgroundIndex)"), std::string::npos);
	EXPECT_EQ(RenderTeeBody.find("std::find(vVisibleSkinIndices.begin(), vVisibleSkinIndices.end(), i)"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const bool RequestWindowScrollBlocked = SkinListScrollInteraction || s_SkinListScrollCooldownFrames > 0;"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const bool VisibleSourceSettled = VisibleSourceSettledCount == (int)vVisibleSkinIndices.size();"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const int DefaultBackgroundRequestBudget = Throughput.m_BackgroundRequestBudget;"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const auto BackgroundBudgetDecision = SettingsSkinBackgroundRequestBudgetDecision({"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("const int BackgroundRequestBudget = BackgroundBudgetDecision.m_RequestBudget;"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("vSkinList[BackgroundIndex].RequestLoad(ESettingsResourcePriority::BACKGROUND);"), std::string::npos);
	EXPECT_NE(RenderTeeBody.find("GameClient()->m_Skins.SetSettingsTeeVisibleSnapshot(VisibleSnapshot);"), std::string::npos);
}

TEST(SkinsContract, TeeSourcePathEmitsRequestAndFrameCapPerfLogs)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("event=source_request skin=%s priority=%s state=%s"), std::string::npos);
	EXPECT_NE(Source.find("event=source_wait skin=%s artifact=source reason=%s remaining_uploads=%d max_uploads=%d"), std::string::npos);
	EXPECT_NE(Source.find("event=frame_cap gpu_cap=%d finalize_cap=%d loading_visible_cap=%d loading_other_cap=%d"), std::string::npos);
	EXPECT_NE(Source.find("LogSettingsSkinSourceRequestEvent(pSkinContainer->Name(), Priority, pSkinContainer->m_State);"), std::string::npos);
	EXPECT_NE(Source.find("LogSettingsSkinFrameCapEvent(GameClient());"), std::string::npos);
}

TEST(SkinsContract, TeeSourcePathCapsActiveLoadingBeforeQueueFuse)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t StartLoadingPos = Source.find("void CSkins::UpdateStartLoading(CSkinLoadingStats &Stats)");
	ASSERT_NE(StartLoadingPos, std::string::npos);
	const size_t StartLoadingEnd = Source.find("CSkins::ESkinProcessResult CSkins::ProcessSkinContainer", StartLoadingPos);
	ASSERT_NE(StartLoadingEnd, std::string::npos);
	const std::string StartLoading = Source.substr(StartLoadingPos, StartLoadingEnd - StartLoadingPos);

	EXPECT_NE(StartLoading.find("const bool BackgroundDrainActive = TeeSettingsActive ? m_SettingsThroughputControllerOutput.m_BackgroundDrainActive"), std::string::npos);
	EXPECT_NE(StartLoading.find("const int CountFuseLimit = TeeSettingsActive ? m_SettingsThroughputControllerOutput.m_CountFuseLimit"), std::string::npos);
	EXPECT_NE(StartLoading.find("const int NormalLoadingWindow = TeeSettingsActive ? m_SettingsThroughputControllerOutput.m_NormalLoadingWindow"), std::string::npos);
	EXPECT_NE(StartLoading.find("const int VisibleLoadingWindow = TeeSettingsActive ? m_SettingsThroughputControllerOutput.m_VisibleLoadingWindow"), std::string::npos);
	EXPECT_NE(StartLoading.find("m_SettingsSourceAdmissionTelemetry.m_VisibleReserve = TeeSettingsActive ? m_SettingsThroughputControllerOutput.m_VisibleReserve : 8;"), std::string::npos);
	EXPECT_NE(StartLoading.find("const auto Admission = DetermineAdmission(pSkinContainer, Priority);"), std::string::npos);
	EXPECT_NE(StartLoading.find("const auto SourceAdmission = SettingsSkinSourceAdmissionDecision({"), std::string::npos);
	EXPECT_NE(StartLoading.find("Admission.m_pBlockReason = SettingsSkinSourceAdmissionBlockReasonName(SourceAdmission.m_BlockReason);"), std::string::npos);
	EXPECT_NE(StartLoading.find("const bool CountFuseApplies = Admission.m_CountFuseApplies;"), std::string::npos);
	EXPECT_NE(StartLoading.find("LogSettingsSkinSourceWaitEvent(pSkinContainer->Name(), Admission.m_pBlockReason"), std::string::npos);
}

TEST(SkinsContract, TeeBackgroundWindowUsesRealDecodeJobSaturationSignal)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t PreparePos = Source.find("void CSkins::PrepareSettingsThroughputForFrame()");
	ASSERT_NE(PreparePos, std::string::npos);
	const size_t NextFunctionPos = Source.find("void CSkins::ClampSkinQueueIndex(int Dummy)", PreparePos);
	ASSERT_NE(NextFunctionPos, std::string::npos);
	const std::string PrepareBody = Source.substr(PreparePos, NextFunctionPos - PreparePos);

	EXPECT_NE(PrepareBody.find("int LoadingJobsAwaitingResult = 0;"), std::string::npos);
	EXPECT_NE(PrepareBody.find("int LoadingJobsReadyForMainThread = 0;"), std::string::npos);
	EXPECT_NE(PrepareBody.find("if(!pSkinContainer->m_pLoadJob->Done())"), std::string::npos);
	EXPECT_NE(PrepareBody.find("const bool DecodeJobsSaturated ="), std::string::npos);
	EXPECT_NE(PrepareBody.find("LoadingJobsReadyForMainThread == 0"), std::string::npos);
	EXPECT_NE(PrepareBody.find("m_SettingsThroughputControllerOutput = SettingsSkinThroughputControllerStep({"), std::string::npos);
}

TEST(SkinsContract, TeeFinishLoadingKeepsPriorityBeforeBackgroundSweep)
{
	std::ifstream File(TestSourcePath("src/game/client/components/skins.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t FinishLoadingPos = Source.find("void CSkins::UpdateFinishLoading(");
	ASSERT_NE(FinishLoadingPos, std::string::npos);
	const size_t FinishLoadingEnd = Source.find("void CSkins::RefreshEventSkins()", FinishLoadingPos);
	ASSERT_NE(FinishLoadingEnd, std::string::npos);
	const std::string FinishLoading = Source.substr(FinishLoadingPos, FinishLoadingEnd - FinishLoadingPos);

	const size_t UsageListPos = FinishLoading.find("for(const std::string &SkinName : vUsageSnapshot)");
	const size_t DeferBackgroundPos = FinishLoading.find("if(SettingsSkinFinalizeShouldDeferBackgroundSweep(ProcessedHighPrioritySkin, SkinsProcessedThisFrame, MaxSkinsPerFrame))");
	const size_t BackgroundListPos = FinishLoading.find("for(const std::string &SkinName : vBackgroundSnapshot)");
	const size_t FallbackSweepPos = FinishLoading.find("for(auto &[_, pSkinContainer] : m_Skins)");

	ASSERT_NE(UsageListPos, std::string::npos);
	ASSERT_NE(DeferBackgroundPos, std::string::npos);
	ASSERT_NE(BackgroundListPos, std::string::npos);
	ASSERT_NE(FallbackSweepPos, std::string::npos);
	EXPECT_LT(UsageListPos, DeferBackgroundPos);
	EXPECT_LT(DeferBackgroundPos, BackgroundListPos);
	EXPECT_LT(BackgroundListPos, FallbackSweepPos);
}

TEST(SkinsContract, TeeSettingsListEmitsRequestWindowPerfLogs)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("controller_reason=%s"), std::string::npos);
	EXPECT_NE(Source.find("frame_time_avg_ms=%.3f render_frame_time_ms=%.3f admission_underfed=%d underfed_streak=%d"), std::string::npos);
	EXPECT_NE(Source.find("visible_reserve_effective=%d"), std::string::npos);
	EXPECT_NE(Source.find("GameClient()->m_Skins.SetSettingsTeeVisibleSnapshot(VisibleSnapshot);"), std::string::npos);
	EXPECT_NE(Source.find("event=work_drain page=settings:tee kind=merge count=%llu bytes=%d dur_ms=%.3f stop=%s source=list_drain_summary scope=session"), std::string::npos);
	EXPECT_NE(Source.find("uploads_done_total=%llu loaded_total=%llu uploads_per_sec=%.3f loaded_per_sec=%.3f"), std::string::npos);
	EXPECT_NE(Source.find("max_requested=%d max_pending=%d max_loading=%d max_real_inflight=%d count_fuse_limit=%d"), std::string::npos);
	EXPECT_NE(Source.find("total_requested=%llu total_admitted=%llu total_started=%llu"), std::string::npos);
	EXPECT_NE(Source.find("num_loading_window_waits=%d num_gpu_budget_waits=%d num_queue_fuse_waits=%d full_list_ready=%d final_real_inflight=%d"), std::string::npos);
	EXPECT_NE(Source.find("last_wait_reason=%s last_dynamic_decision=%s last_request_budget_block_reason=%s"), std::string::npos);
	EXPECT_NE(Source.find("event=admission_invariant_violation pending=%d loading=%d real_inflight=%d count_fuse_limit=%d"), std::string::npos);
	EXPECT_NE(Source.find("if(gs_TeeListDrainPerfSession.m_Active)\n\t\t\tLogTeeListDrainSummary(Client(), GameClient()->m_Skins, GameClient()->m_Skins.LoadingStats(), false, RefreshNowNs);"), std::string::npos);
	EXPECT_NE(Source.find("BeginTeeListDrainPerfSession(GameClient()->m_Skins, RefreshNowNs);"), std::string::npos);
	EXPECT_NE(Source.find("m_SettingsHighPrioritySettled = VisibleSourceSettled;"), std::string::npos);
	EXPECT_NE(Source.find("if(PerfDebugEnabled() &&"), std::string::npos);
	EXPECT_NE(Source.find("if(m_SettingsRuntimeMetadata.m_LastPage != SETTINGS_TEE)"), std::string::npos);
}

TEST(SkinsContract, SkinQueueRandomJoinConfigAndCommandAreRegistered)
{
	const std::string Config = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmSkinQueueRandomJoin, qm_skin_queue_random_join, 0, 0, 1"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmDummySkinQueueRandomJoin, qm_dummy_skin_queue_random_join, 0, 0, 1"), std::string::npos);
	EXPECT_NE(Source.find("Console()->Register(\"random_skin_queue\", \"\", CFGFLAG_CLIENT, ConRandomSkinQueue, this, \"Apply a random skin from the queue\")"), std::string::npos);
	EXPECT_NE(Source.find("Console()->Register(\"random_dummy_skin_queue\", \"\", CFGFLAG_CLIENT, ConRandomDummySkinQueue, this, \"Apply a random skin from the dummy queue\")"), std::string::npos);
}

TEST(SkinsContract, SkinQueueRandomStartAppliesWhenRotationStartsPerDummy)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const std::string Body = FunctionBody(Source, "void CSkins::UpdateSkinQueue(std::chrono::nanoseconds Now, int Dummy)");

	// 随机起点在轮换（重新）启动的首帧生效：上线进图、分身单独连接、重新启用队列都覆盖，
	// 因此随机化必须位于 UpdateSkinQueue 的在线首帧分支并按 dummy 取配置，而不是只在主连接进图钩子里。
	EXPECT_NE(Body.find("g_Config.m_QmDummySkinQueueRandomJoin : g_Config.m_QmSkinQueueRandomJoin"), std::string::npos);
	EXPECT_NE(Body.find("SkinQueueIndexVar(Dummy) = rand() % QueueActiveCount"), std::string::npos);
	EXPECT_NE(Body.find("m_aSkinQueueElapsed[Dummy] = 0ns;"), std::string::npos);
	const size_t FirstTick = Body.find("if(!m_aSkinQueueLastUpdate[Dummy].has_value())");
	const size_t RandomJoin = Body.find("RandomJoin");
	const size_t Apply = Body.find("ApplySkinQueueCurrent(Dummy);", FirstTick);
	ASSERT_NE(FirstTick, std::string::npos);
	ASSERT_NE(RandomJoin, std::string::npos);
	ASSERT_NE(Apply, std::string::npos);
	EXPECT_LT(FirstTick, RandomJoin);
	EXPECT_LT(RandomJoin, Apply);
	EXPECT_EQ(Source.find("void CSkins::OnMapLoad()"), std::string::npos);
}

TEST(SkinsContract, RandomSkinQueueIndexAppliesRandomBoundedEntry)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const std::string Body = FunctionBody(Source, "bool CSkins::RandomSkinQueueIndex(int Dummy)");

	// 空队列返回 false，非空时复用 ApplySkinQueueIndex 施加越界保护。
	EXPECT_NE(Body.find("return false;"), std::string::npos);
	EXPECT_NE(Body.find("ApplySkinQueueIndex(rand() % Queue.size(), Dummy)"), std::string::npos);
}

TEST(SkinsContract, TeeSkinQueuePanelExposesRandomControls)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");

	const size_t DiceButton = Source.find("Ui()->DoButton_QmIcon(&s_TeeRandomSkinQueueButton, EQmIcon::DICE_FIVE");
	ASSERT_NE(DiceButton, std::string::npos);
	const size_t RandomApply = Source.find("GameClient()->m_Skins.RandomSkinQueueIndex(QueueDummy)");
	ASSERT_NE(RandomApply, std::string::npos);
	EXPECT_LT(DiceButton, RandomApply);
	EXPECT_NE(Source.find("QueueDummy ? g_Config.m_QmDummySkinQueueRandomJoin : g_Config.m_QmSkinQueueRandomJoin"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Random skin on map join\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Apply a random skin from the queue\")"), std::string::npos);
}
