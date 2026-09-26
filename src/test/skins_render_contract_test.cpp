// Skins 静态源码合同：skins_render_contract_test.cpp.
// 源码合同测试：皮肤资源、菜单集成和队列策略。运行时行为保留在 skins_test.cpp.
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/gfx/image_loader.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/skins.h>
#include <game/client/render.h>

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>
#include <test/test.h>

#include <cstdlib>
#include <fstream>
#include <limits>
#include <list>
#include <sstream>

extern CDataContainer *g_pData;

TEST(SkinsContract, ManagedTeeRenderInfoSkipsInvalidSixupSkinNames)
{
	std::ifstream File(TestSourcePath("src/game/client/gameclient.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("NormalizeSixupSkinName(SkinDescriptor.m_aSkinName"), std::string::npos);
	EXPECT_NE(Source.find("CSkin::IsValidName(pManagedTeeRenderInfo->m_SkinDescriptor.m_aSkinName)"), std::string::npos);
	EXPECT_NE(Source.find("m_Skins.FindOrNullptr(CSkin::IsValidName(SkinDescriptor.m_aSkinName) ? SkinDescriptor.m_aSkinName : \"default\")"), std::string::npos);
}

TEST(SkinsContract, ManagedTeeRenderInfoDefersUnloadedSkinInsteadOfApplyingFallback)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t RefreshSkinPos = Source.find("void CGameClient::RefreshSkin(const std::shared_ptr<CManagedTeeRenderInfo> &pManagedTeeRenderInfo)");
	ASSERT_NE(RefreshSkinPos, std::string::npos);
	const size_t RefreshSkinsPos = Source.find("void CGameClient::RefreshSkins(int SkinDescriptorFlags)", RefreshSkinPos);
	ASSERT_NE(RefreshSkinsPos, std::string::npos);
	const std::string RefreshSkinBody = Source.substr(RefreshSkinPos, RefreshSkinsPos - RefreshSkinPos);

	EXPECT_NE(RefreshSkinBody.find("pManagedTeeRenderInfo->SetDescriptorRenderInfoReady(false);"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("m_Skins.FindOrNullptr(CSkin::IsValidName(SkinDescriptor.m_aSkinName) ? SkinDescriptor.m_aSkinName : \"default\")"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("bool SixReady = false;"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("SixReady = TeeInfo.SixDescriptorReady();"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("SevenReady = TeeInfo.SevenDescriptorReady();"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("DescriptorRenderInfoReady = SixReady || SevenReady;"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("pManagedTeeRenderInfo->SetDescriptorRenderInfoReady(DescriptorRenderInfoReady);"), std::string::npos);
	EXPECT_EQ(RefreshSkinBody.find("TeeInfo.Apply(m_Skins.Find("), std::string::npos);
}

TEST(SkinsContract, SkinRefreshDoesNotFloodPendingQueueBeforeVisibleRequests)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const size_t RefreshPos = Source.find("void CSkins::Refresh(TSkinLoadedCallback &&SkinLoadedCallback)");
	ASSERT_NE(RefreshPos, std::string::npos);
	const size_t LoadingStatsPos = Source.find("CSkins::CSkinLoadingStats CSkins::LoadingStats() const", RefreshPos);
	ASSERT_NE(LoadingStatsPos, std::string::npos);
	const std::string RefreshBody = Source.substr(RefreshPos, LoadingStatsPos - RefreshPos);

	EXPECT_NE(RefreshBody.find("if(pSkinContainer->m_pLoadJob)"), std::string::npos);
	EXPECT_EQ(RefreshBody.find("pSkinContainer->m_pSkin->m_OriginalSkin.Unload(Graphics());"), std::string::npos);
	EXPECT_EQ(RefreshBody.find("pSkinContainer->m_pSkin->m_ColorableSkin.Unload(Graphics());"), std::string::npos);
	EXPECT_EQ(RefreshBody.find("pSkinContainer->m_pSkin.reset();"), std::string::npos);
	EXPECT_EQ(RefreshBody.find("pSkinContainer->SetState(CSkinContainer::EState::PENDING, ESettingsResourcePriority::VISIBLE);"), std::string::npos);
	EXPECT_NE(RefreshBody.find("if(pSkinContainer->m_State != CSkinContainer::EState::LOADED)"), std::string::npos);
	EXPECT_NE(RefreshBody.find("pSkinContainer->SetState(pSkinContainer->DetermineInitialState());"), std::string::npos);
	EXPECT_NE(RefreshBody.find("LoadSkinDirect(\"default\");"), std::string::npos);
}

TEST(SkinsContract, SkinTransitionDefersKeyUntilDescriptorRenderInfoIsReady)
{
	const std::string Header = ReadTestSourceFile("src/game/client/render.h");
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t UpdateRenderInfoPos = Source.find("void CGameClient::CClientData::UpdateRenderInfo()");
	ASSERT_NE(UpdateRenderInfoPos, std::string::npos);
	const size_t UpdateTransitionPos = Source.find("void CGameClient::CClientData::UpdateSkinChangeTransition", UpdateRenderInfoPos);
	ASSERT_NE(UpdateTransitionPos, std::string::npos);
	const std::string UpdateRenderInfoBody = Source.substr(UpdateRenderInfoPos, UpdateTransitionPos - UpdateRenderInfoPos);

	EXPECT_NE(Header.find("bool DescriptorRenderInfoReady() const"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("const bool DescriptorRenderInfoReady = m_pSkinInfo->DescriptorRenderInfoReady();"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("if(!DescriptorRenderInfoReady"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("m_RenderInfo.Valid()"), std::string::npos);
	EXPECT_LT(UpdateRenderInfoBody.find("if(!DescriptorRenderInfoReady"), UpdateRenderInfoBody.find("UpdateSkinChangeTransition(NewRenderInfo, RenderSkinDescriptor);"));
}

TEST(SkinsContract, SkinTransitionUsesDefaultKeyWhenInitialDescriptorIsNotReady)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t UpdateRenderInfoPos = Source.find("void CGameClient::CClientData::UpdateRenderInfo()");
	ASSERT_NE(UpdateRenderInfoPos, std::string::npos);
	const size_t UpdateTransitionPos = Source.find("void CGameClient::CClientData::UpdateSkinChangeTransition", UpdateRenderInfoPos);
	ASSERT_NE(UpdateTransitionPos, std::string::npos);
	const std::string UpdateRenderInfoBody = Source.substr(UpdateRenderInfoPos, UpdateTransitionPos - UpdateRenderInfoPos);

	EXPECT_NE(UpdateRenderInfoBody.find("CSkinDescriptor RenderSkinDescriptor = SkinDescriptor;"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("const bool DescriptorRenderInfoReady = m_pSkinInfo->DescriptorRenderInfoReady();"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("if(!DescriptorRenderInfoReady && MayReusePreviousRenderInfo && PreviousRenderInfoAlive && m_RenderInfo.Valid())"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("else if(!DescriptorRenderInfoReady)"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("const float OriginalSize = NewRenderInfo.m_Size;"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("BuildDefaultSkinDescriptor(RenderSkinDescriptor, SkinDescriptor.m_Flags);"), std::string::npos);
	// 默认皮肤不可绘制时先复用上一份渲染信息，不能直接 Reset 成白块。
	EXPECT_NE(UpdateRenderInfoBody.find("if(!ApplyDefaultSkin(m_pGameClient, NewRenderInfo, SkinDescriptor.m_Flags))"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("if(PreviousRenderInfo.Valid() && PreviousRenderInfoAlive)"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("NewRenderInfo = PreviousRenderInfo;"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("CopySkinColorsOnly(NewRenderInfo, SkinProperties);"), std::string::npos);
	// Reset 只允许作为「连上一份渲染信息都不可用」时的最后手段，不能在失败分支里无条件执行。
	EXPECT_NE(UpdateRenderInfoBody.find("else\n\t\t\t{\n\t\t\t\tNewRenderInfo.Reset();\n\t\t\t}"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("UpdateSkinChangeTransition(NewRenderInfo, RenderSkinDescriptor);"), std::string::npos);
	EXPECT_EQ(UpdateRenderInfoBody.find("UpdateSkinChangeTransition(NewRenderInfo, SkinDescriptor);"), std::string::npos);
}

TEST(SkinsContract, StaleTextureHandlesAreNotTreatedAsDrawable)
{
	// 句柄 IsValid() 为真、也不是 null 贴图，但纹理已经不在（设备重建、槽位释放、贴图被卸载）：
	// 这种句柄交给绘制会变成没有贴图的实心块，必须判为不可绘制。
	EXPECT_FALSE(CTeeRenderInfo::IsLiveDrawableTextureState(true, false, false));
	EXPECT_TRUE(CTeeRenderInfo::IsLiveDrawableTextureState(true, false, true));
	// 本来就不可绘制的句柄，是否分配过纹理都不改变结论。
	EXPECT_FALSE(CTeeRenderInfo::IsLiveDrawableTextureState(true, true, true));
	EXPECT_FALSE(CTeeRenderInfo::IsLiveDrawableTextureState(true, true, false));
	EXPECT_FALSE(CTeeRenderInfo::IsLiveDrawableTextureState(false, false, true));
	EXPECT_FALSE(CTeeRenderInfo::IsLiveDrawableTextureState(false, false, false));
	// 与旧判据的关系：旧判据只看「合法且非 null」，新判据在其上再要求纹理仍然分配着。
	for(const bool IsValid : {false, true})
	{
		for(const bool IsNull : {false, true})
		{
			for(const bool IsAllocated : {false, true})
			{
				const bool Drawable = CTeeRenderInfo::IsDrawableTextureState(IsValid, IsNull);
				EXPECT_EQ(CTeeRenderInfo::IsLiveDrawableTextureState(IsValid, IsNull, IsAllocated), Drawable && IsAllocated);
			}
		}
	}
}

TEST(SkinsContract, SevenSkinRenderingIsRestrictedToOnlineServerControlledAppearance)
{
	const std::string GameClientSource = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const std::string LocalDescriptorBody = FunctionBody(GameClientSource, "void CGameClient::CClientData::BuildLocalSkinDescriptor");
	ASSERT_FALSE(LocalDescriptorBody.empty());
	EXPECT_NE(LocalDescriptorBody.find("m_pGameClient->m_pClient->State() == IClient::STATE_ONLINE"), std::string::npos);
	EXPECT_NE(LocalDescriptorBody.find("m_pGameClient->ShouldUseServerControlledLocalSkin()"), std::string::npos);
	// 协议版本必须参与判定：仅凭服务器白名单会把 0.6 服务器渲染成 0.7 皮肤。
	EXPECT_NE(LocalDescriptorBody.find("m_pGameClient->m_pClient->IsSixup()"), std::string::npos);
	EXPECT_NE(LocalDescriptorBody.find("ResolveServerSkinProtocol("), std::string::npos);
	EXPECT_NE(LocalDescriptorBody.find("Protocol == EServerSkinProtocol::SEVEN"), std::string::npos);
	EXPECT_NE(LocalDescriptorBody.find("Protocol == EServerSkinProtocol::SIX && m_Active"), std::string::npos);
	EXPECT_NE(LocalDescriptorBody.find("CSkinDescriptor::FLAG_SEVEN"), std::string::npos);
	EXPECT_NE(LocalDescriptorBody.find("CSkinDescriptor::FLAG_SIX"), std::string::npos);
	EXPECT_NE(GameClientSource.find("if(Client()->State() != IClient::STATE_ONLINE)\n\t\treturn false;"), std::string::npos);

	const std::string DescriptorBody = FunctionBody(GameClientSource, "CSkinDescriptor CGameClient::CClientData::ToSkinDescriptor");
	ASSERT_FALSE(DescriptorBody.empty());
	EXPECT_NE(DescriptorBody.find("m_pGameClient->m_pClient->State() == IClient::STATE_ONLINE"), std::string::npos);
	EXPECT_NE(DescriptorBody.find("ResolveServerSkinProtocol("), std::string::npos);
	EXPECT_NE(DescriptorBody.find("Protocol == EServerSkinProtocol::SEVEN"), std::string::npos);
	EXPECT_NE(DescriptorBody.find("Protocol == EServerSkinProtocol::SIX && m_Active"), std::string::npos);

	const std::string BrowserSource = ReadTestSourceFile("src/game/client/components/menus_browser.cpp");
	EXPECT_EQ(BrowserSource.find("m_Skins7.FindSkinPart"), std::string::npos);
	EXPECT_NE(BrowserSource.find("GetTeeRenderInfo(vec2(Skin.w, Skin.h), \"default\", false, 0, 0)"), std::string::npos);

	const std::string UpdateRenderInfoBody = FunctionBody(GameClientSource, "void CGameClient::CClientData::UpdateRenderInfo");
	ASSERT_FALSE(UpdateRenderInfoBody.empty());
	EXPECT_NE(GameClientSource.find("bool HasDrawableSevenSkin(const CTeeRenderInfo &Info)"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("const bool TargetUsesSevenSkin"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("const bool PreviousRenderInfoUsesSevenSkin = HasDrawableSevenSkin(m_RenderInfo);"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("const bool MayReusePreviousRenderInfo = TargetUsesSevenSkin || !PreviousRenderInfoUsesSevenSkin;"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("if(!DescriptorRenderInfoReady && !TargetUsesSevenSkin && PreviousRenderInfoUsesSevenSkin)"), std::string::npos);
	// 默认皮肤也不可绘制时必须沿用上一份可绘制渲染信息，不能直接 Reset 成白块。
	EXPECT_NE(UpdateRenderInfoBody.find("if(!ApplyDefaultSkin(m_pGameClient, NewRenderInfo, SkinDescriptor.m_Flags))"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("if(PreviousRenderInfo.Valid() && PreviousRenderInfoAlive)"), std::string::npos);

	const std::string TransitionBody = FunctionBody(GameClientSource, "void CGameClient::CClientData::UpdateSkinChangeTransition");
	ASSERT_FALSE(TransitionBody.empty());
	EXPECT_NE(TransitionBody.find("const bool HidesPreviousSevenSkin"), std::string::npos);
	EXPECT_NE(TransitionBody.find("HasDrawableSevenSkin(m_RenderInfo)"), std::string::npos);
	EXPECT_NE(TransitionBody.find("m_SkinTransitionPreviousRenderInfo.Reset();"), std::string::npos);
}

TEST(SkinsContract, StreamerFallbackCancelsAnyRealSkinTransition)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t UpdateTransitionPos = Source.find("void CGameClient::CClientData::UpdateSkinChangeTransition");
	ASSERT_NE(UpdateTransitionPos, std::string::npos);
	const size_t ProgressPos = Source.find("float CGameClient::CClientData::SkinChangeTransitionProgress", UpdateTransitionPos);
	ASSERT_NE(ProgressPos, std::string::npos);
	const std::string Body = Source.substr(UpdateTransitionPos, ProgressPos - UpdateTransitionPos);
	const size_t StreamerGuard = Body.find("if(m_pGameClient != nullptr && m_pGameClient->ShouldHideStreamerSkin(m_ClientId))");
	const size_t ResolveAction = Body.find("ResolveSkinChangeTransitionAction(");
	ASSERT_NE(StreamerGuard, std::string::npos);
	ASSERT_NE(ResolveAction, std::string::npos);
	EXPECT_LT(StreamerGuard, ResolveAction);
	EXPECT_NE(Body.find("m_SkinTransitionPreviousRenderInfo.Reset();", StreamerGuard), std::string::npos);
	EXPECT_NE(Body.find("m_SkinTransitionStart.reset();", StreamerGuard), std::string::npos);
}

TEST(SkinsContract, StreamerSkinPrivacyStateChangesRefreshActiveManagedClientsImmediately)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t OnUpdatePos = Source.find("void CGameClient::OnUpdate()");
	ASSERT_NE(OnUpdatePos, std::string::npos);
	const size_t OnUpdateEnd = Source.find("int CGameClient::RenderThrottleRefreshRate() const", OnUpdatePos);
	ASSERT_NE(OnUpdateEnd, std::string::npos);
	const std::string OnUpdateBody = Source.substr(OnUpdatePos, OnUpdateEnd - OnUpdatePos);
	EXPECT_NE(OnUpdateBody.find("RefreshStreamerSkinPrivacyAfterStateChange();"), std::string::npos);

	const size_t RefreshPos = Source.find("void CGameClient::RefreshStreamerSkinPrivacyAfterStateChange()");
	ASSERT_NE(RefreshPos, std::string::npos);
	const size_t RefreshEnd = Source.find("int CGameClient::RenderThrottleRefreshRate() const", RefreshPos);
	ASSERT_NE(RefreshEnd, std::string::npos);
	const std::string RefreshBody = Source.substr(RefreshPos, RefreshEnd - RefreshPos);
	EXPECT_NE(RefreshBody.find("m_LastStreamerHideSkins == g_Config.m_QmStreamerHideSkins"), std::string::npos);
	EXPECT_NE(RefreshBody.find("m_LastStreamerFriendsRevision == FriendsRevision"), std::string::npos);
	EXPECT_NE(RefreshBody.find("m_aLastStreamerLocalIds[0] == m_aLocalIds[0]"), std::string::npos);
	EXPECT_NE(RefreshBody.find("Friends()->IsFriend(ClientData.m_aName, ClientData.m_aClan, true)"), std::string::npos);
	EXPECT_NE(RefreshBody.find("ClientData.UpdateRenderInfo();"), std::string::npos);
}

TEST(SkinsContract, SkinTransitionKeepsPreviousSkinBaseWhileDescriptorIsPending)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t UpdateRenderInfoPos = Source.find("void CGameClient::CClientData::UpdateRenderInfo()");
	ASSERT_NE(UpdateRenderInfoPos, std::string::npos);
	const size_t UpdateTransitionPos = Source.find("void CGameClient::CClientData::UpdateSkinChangeTransition", UpdateRenderInfoPos);
	ASSERT_NE(UpdateTransitionPos, std::string::npos);
	const std::string UpdateRenderInfoBody = Source.substr(UpdateRenderInfoPos, UpdateTransitionPos - UpdateRenderInfoPos);

	EXPECT_NE(UpdateRenderInfoBody.find("const bool DescriptorRenderInfoReady = m_pSkinInfo->DescriptorRenderInfoReady();"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("if(!DescriptorRenderInfoReady && MayReusePreviousRenderInfo && PreviousRenderInfoAlive && m_RenderInfo.Valid())"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("NewRenderInfo = m_RenderInfo;"), std::string::npos);
	EXPECT_EQ(UpdateRenderInfoBody.find("return;\n\t\t}"), std::string::npos);
	EXPECT_LT(UpdateRenderInfoBody.find("if(!DescriptorRenderInfoReady && MayReusePreviousRenderInfo && PreviousRenderInfoAlive && m_RenderInfo.Valid())"), UpdateRenderInfoBody.find("// force team colors"));
	EXPECT_LT(UpdateRenderInfoBody.find("// force team colors"), UpdateRenderInfoBody.find("UpdateSkinChangeTransition(NewRenderInfo, RenderSkinDescriptor);"));
}

TEST(SkinsContract, SkinTransitionKeepsPendingSkinColorsWhileReusingPreviousSkinBase)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t UpdateRenderInfoPos = Source.find("void CGameClient::CClientData::UpdateRenderInfo()");
	ASSERT_NE(UpdateRenderInfoPos, std::string::npos);
	const size_t UpdateTransitionPos = Source.find("void CGameClient::CClientData::UpdateSkinChangeTransition", UpdateRenderInfoPos);
	ASSERT_NE(UpdateTransitionPos, std::string::npos);
	const std::string UpdateRenderInfoBody = Source.substr(UpdateRenderInfoPos, UpdateTransitionPos - UpdateRenderInfoPos);

	EXPECT_NE(Source.find("void CopySkinColorsOnly(CTeeRenderInfo &Target, const CTeeRenderInfo &Source)"), std::string::npos);
	EXPECT_NE(Source.find("Target.m_CustomColoredSkin = Source.m_CustomColoredSkin;"), std::string::npos);
	EXPECT_NE(Source.find("Target.m_aSixup[Dummy].m_aUseCustomColors[Part] = Source.m_aSixup[Dummy].m_aUseCustomColors[Part];"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("const CTeeRenderInfo SkinProperties = NewRenderInfo;"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("NewRenderInfo = m_RenderInfo;"), std::string::npos);
	EXPECT_NE(UpdateRenderInfoBody.find("CopySkinColorsOnly(NewRenderInfo, SkinProperties);"), std::string::npos);
	EXPECT_LT(UpdateRenderInfoBody.find("NewRenderInfo = m_RenderInfo;"), UpdateRenderInfoBody.find("CopySkinColorsOnly(NewRenderInfo, SkinProperties);"));
}

TEST(SkinsContract, ManagedTeeRenderInfoAllowsTeeworldsCompatibilitySkinWithoutSixBody)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t RefreshSkinPos = Source.find("void CGameClient::RefreshSkin(const std::shared_ptr<CManagedTeeRenderInfo> &pManagedTeeRenderInfo)");
	ASSERT_NE(RefreshSkinPos, std::string::npos);
	const size_t RefreshSkinsPos = Source.find("void CGameClient::RefreshSkins(int SkinDescriptorFlags)", RefreshSkinPos);
	ASSERT_NE(RefreshSkinsPos, std::string::npos);
	const std::string RefreshSkinBody = Source.substr(RefreshSkinPos, RefreshSkinsPos - RefreshSkinPos);

	EXPECT_NE(RefreshSkinBody.find("bool SixReady = false;"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("bool SevenReady = false;"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("SixReady = TeeInfo.SixDescriptorReady();"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("SevenReady = TeeInfo.SevenDescriptorReady();"), std::string::npos);
	EXPECT_NE(RefreshSkinBody.find("DescriptorRenderInfoReady = SixReady || SevenReady;"), std::string::npos);
	EXPECT_EQ(RefreshSkinBody.find("DescriptorRenderInfoReady = DescriptorRenderInfoReady && SevenReady;"), std::string::npos);
}

TEST(SkinsContract, ManagedTeeReadinessCoversAllSelectableTextureVariantsAndDummies)
{
	const std::string Header = ReadTestSourceFile("src/game/client/render.h");
	EXPECT_NE(Header.find("return AreTextureVariantsDrawable(m_OriginalRenderSkin.m_Body, m_ColorableRenderSkin.m_Body);"), std::string::npos);
	EXPECT_NE(Header.find("std::all_of(std::begin(m_aSixup), std::end(m_aSixup)"), std::string::npos);
	EXPECT_NE(Header.find("protocol7::SKINPART_BODY"), std::string::npos);
	EXPECT_NE(Header.find("protocol7::SKINPART_HANDS"), std::string::npos);
	EXPECT_NE(Header.find("protocol7::SKINPART_FEET"), std::string::npos);
	EXPECT_NE(Header.find("protocol7::SKINPART_EYES"), std::string::npos);
	EXPECT_NE(Header.find("Sixup.RequiredPartTextureVariantsDrawable()"), std::string::npos);
}

TEST(SkinsContract, SixupCompletedJobsAreConsumedEveryUpdate)
{
	const std::string Header = ReadTestSourceFile("src/game/client/components/skins7.h");
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins7.cpp");
	EXPECT_NE(Header.find("void OnUpdate() override;"), std::string::npos);
	const size_t OnUpdatePos = Source.find("void CSkins7::OnUpdate()");
	const size_t InitPlaceholderPos = Source.find("void CSkins7::InitPlaceholderSkinParts()", OnUpdatePos);
	ASSERT_NE(OnUpdatePos, std::string::npos);
	ASSERT_NE(InitPlaceholderPos, std::string::npos);
	const std::string OnUpdateBody = Source.substr(OnUpdatePos, InitPlaceholderPos - OnUpdatePos);
	EXPECT_NE(OnUpdateBody.find("ProcessCompletedJobs();"), std::string::npos);
	const size_t ProcessPos = Source.find("void CSkins7::ProcessCompletedJobs()");
	const size_t ScanDataPos = Source.find("class CSkinScanData", ProcessPos);
	ASSERT_NE(ProcessPos, std::string::npos);
	ASSERT_NE(ScanDataPos, std::string::npos);
	const std::string ProcessBody = Source.substr(ProcessPos, ScanDataPos - ProcessPos);
	EXPECT_NE(ProcessBody.find("GpuUploadLimiter()->CanUpload(2)"), std::string::npos);
	EXPECT_NE(ProcessBody.find("Part.m_OriginalTexture = Graphics()->LoadTextureRaw"), std::string::npos);
	EXPECT_NE(ProcessBody.find("Part.m_ColorableTexture = Graphics()->LoadTextureRawMove"), std::string::npos);
	const size_t FirstUploadCount = ProcessBody.find("GpuUploadLimiter()->OnUploaded();");
	ASSERT_NE(FirstUploadCount, std::string::npos);
	EXPECT_NE(ProcessBody.find("GpuUploadLimiter()->OnUploaded();", FirstUploadCount + 1), std::string::npos);
	EXPECT_LT(ProcessBody.find("Iter = m_PendingSkinPartJobs.erase(Iter);"), ProcessBody.find("m_SkinLoadedCallback();"));
	EXPECT_NE(Source.find("void CSkins7::RebuildSkins()"), std::string::npos);
	EXPECT_NE(Source.find("RebuildSkins();\n\t\tm_Loading = false;"), std::string::npos);
	const std::string GameClientSource = ReadTestSourceFile("src/game/client/gameclient.cpp");
	EXPECT_NE(GameClientSource.find("const auto ProgressCallback = [this, SkinStartLoadTime]()"), std::string::npos);
	EXPECT_NE(GameClientSource.find("m_Skins7.Refresh([this, ProgressCallback]()"), std::string::npos);
	EXPECT_NE(GameClientSource.find("RefreshSkin(pManagedTeeRenderInfo);"), std::string::npos);
}

TEST(SkinsContract, ManagedTeeRefreshClearsTextureBranchesMissingFromDescriptor)
{
	CTeeRenderInfo Info;
	Info.m_aSixup[0].m_aUseCustomColors[protocol7::SKINPART_BODY] = true;
	Info.ResetMissingDescriptorBranches(CSkinDescriptor::FLAG_SIX);
	EXPECT_FALSE(Info.m_aSixup[0].m_aUseCustomColors[protocol7::SKINPART_BODY]);

	Info.m_SkinMetrics.m_Body.m_Width = 42;
	Info.ResetMissingDescriptorBranches(CSkinDescriptor::FLAG_SEVEN);
	EXPECT_EQ(Info.m_SkinMetrics.m_Body.m_Width, std::numeric_limits<int>::lowest());

	Info.m_aSixup[0].m_aUseCustomColors[protocol7::SKINPART_BODY] = true;
	Info.m_SkinMetrics.m_Body.m_Width = 42;
	Info.ResetMissingDescriptorBranches(CSkinDescriptor::FLAG_SIX | CSkinDescriptor::FLAG_SEVEN);
	EXPECT_TRUE(Info.m_aSixup[0].m_aUseCustomColors[protocol7::SKINPART_BODY]);
	EXPECT_EQ(Info.m_SkinMetrics.m_Body.m_Width, 42);

	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t RefreshSkinPos = Source.find("void CGameClient::RefreshSkin(const std::shared_ptr<CManagedTeeRenderInfo> &pManagedTeeRenderInfo)");
	ASSERT_NE(RefreshSkinPos, std::string::npos);
	const size_t RefreshSkinsPos = Source.find("void CGameClient::RefreshSkins(int SkinDescriptorFlags)", RefreshSkinPos);
	ASSERT_NE(RefreshSkinsPos, std::string::npos);
	const std::string RefreshSkinBody = Source.substr(RefreshSkinPos, RefreshSkinsPos - RefreshSkinPos);
	EXPECT_NE(RefreshSkinBody.find("TeeInfo.ResetMissingDescriptorBranches(SkinDescriptor.m_Flags);"), std::string::npos);
}

TEST(SkinsContract, TeamTeeGlowConfigAndTeePageUiAreRegistered)
{
	const std::string Config = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string SettingsSource = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmTeamTeeGlow, qm_team_tee_glow, 0, 0, 1"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmTeamTeeGlowTeam0Mode, qm_team_tee_glow_team0_mode, 1, 0, 3"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_COL(QmTeamTeeGlowColor, qm_team_tee_glow_color, 0xFFFFFFFF"), std::string::npos);
	// 外发光是 Tee 外观设置：入口必须挂在 Tee 设置页（皮肤队列面板同栏），不得放回外观页。
	EXPECT_NE(SettingsSource.find("DoSettingsButton_CheckBox(SETTINGS_TEE, -1, &g_Config.m_QmTeamTeeGlow"), std::string::npos);
	EXPECT_NE(SettingsSource.find("Localize(\"Team tee glow\")"), std::string::npos);
	EXPECT_NE(SettingsSource.find("std::clamp(g_Config.m_QmTeamTeeGlowTeam0Mode, 0, 3)"), std::string::npos);
	EXPECT_NE(SettingsSource.find("&g_Config.m_QmTeamTeeGlowColor"), std::string::npos);
	EXPECT_EQ(SettingsSource.find("m_AppearanceSettingsTab == APPEARANCE_TAB_TEE"), std::string::npos);
	EXPECT_EQ(ReadTestSourceFile("src/game/client/QmUi/QmCardRegistry.cpp").find("deck:appearance-tee-glow"), std::string::npos);
	// Tee 页卡片体系：皮肤列表全宽独立，皮肤队列与外发光各占左右半宽卡片。
	const std::string CardRegistrySource = ReadTestSourceFile("src/game/client/QmUi/QmCardRegistry.cpp");
	EXPECT_NE(CardRegistrySource.find("\"deck:tee-skin-queue\", \"tee\", ECardColumn::Left, 1"), std::string::npos);
	EXPECT_NE(CardRegistrySource.find("\"deck:tee-glow\", \"tee\", ECardColumn::Right, 1"), std::string::npos);
	EXPECT_NE(SettingsSource.find("AddCard(QueueSpec,"), std::string::npos);
	EXPECT_NE(SettingsSource.find("AddCard(GlowSpec,"), std::string::npos);
}

TEST(SkinsContract, WarListGlowKeepsPriorityOverTeamTeeGlow)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/players.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CPlayers::RenderPlayer(");

	// warlist 外发光优先，未命中 warlist 时才回落到队伍外发光；两者共用 RenderTeeGlow 三层绘制。
	const size_t WarList = Body.find("GetWarListTeeGlowColor(GameClient(), ClientId, WarListGlowColor)");
	ASSERT_NE(WarList, std::string::npos);
	const size_t TeamGlow = Body.find("GetTeamTeeGlowColor(GameClient(), ClientId, RenderInfo, TeamGlowColor)");
	ASSERT_NE(TeamGlow, std::string::npos);
	EXPECT_LT(WarList, TeamGlow);
	EXPECT_NE(Body.find("RenderTeeGlow(RenderTools(), &State, RenderInfo"), std::string::npos);
}

TEST(SkinsContract, TeamTeeGlowUsesTeamColorsForTeamedPlayers)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/players.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "static bool GetTeamTeeGlowColor(");

	EXPECT_NE(Body.find("g_Config.m_QmTeamTeeGlow"), std::string::npos);
	EXPECT_NE(Body.find("VANILLA_TEAM_SUPER"), std::string::npos);
	EXPECT_NE(Body.find("GetDDTeamColor(Team, 0.75f)"), std::string::npos);
	EXPECT_NE(Body.find("g_Config.m_QmTeamTeeGlowTeam0Mode"), std::string::npos);
	EXPECT_NE(Body.find("normalized_golden_angle"), std::string::npos);
	// team0 的 tee 自身颜色模式必须覆盖 0.7 sixup 部件色，避免 0.7 玩家恒为白光。
	EXPECT_NE(Body.find("m_aSixup[g_Config.m_ClDummy]"), std::string::npos);
	EXPECT_NE(Body.find("m_aUseCustomColors[protocol7::SKINPART_BODY]"), std::string::npos);
	// 彩虹相位在回放中必须取 demo 时间轴（可复现），而非本地时钟。
	EXPECT_NE(Body.find("IClient::STATE_DEMOPLAYBACK"), std::string::npos);
	EXPECT_NE(Body.find("pDemoInfo->m_CurrentTick"), std::string::npos);
}
