// QmNewUi 菜单源码合同：名牌显示域：他人模式隐藏本机身份行、开发者徽章顺序、强钩行布局预留、预览/游戏全范围参考帧。
// 运行时行为保留在 qm_new_ui_menu_branch_test.cpp。
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
#include <string>

TEST(QmNewUiMenuSettingsNameplateContract, NameplateOthersModeSuppressesLocalIdentityRows)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string UpdateCoordXAlignFrameState = FunctionBody(Source, "void CNamePlates::UpdateCoordXAlignFrameState");
	const std::string RenderNamePlateGame = FunctionBody(Source, "void CNamePlates::RenderNamePlateGame");

	EXPECT_EQ(UpdateCoordXAlignFrameState.find("FrameState.m_LocalRoundedX = RoundCoordToCentitiles(GameClient()->m_LocalCharacterPos.x / 32.0f);\n\tFrameState.m_LocalAligned = true;"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("std::array<SCoordXAlignReference, NUM_DUMMIES> aLocalRefs{};"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("if(aLocalRefs[i].m_ClientId == ClientId)"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("CoordXAlignState.m_ReferenceClientId != ReferenceClientId"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("FrameState.m_aClientAligned[ReferenceClientId] = true;"), std::string::npos);
	EXPECT_EQ(UpdateCoordXAlignFrameState.find("if(ClientId == FrameState.m_LocalClientId"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("if(CoordXAlignState.m_Aligned)\n\t\t{"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("FrameState.m_LocalAligned = true;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("const bool IsAnyLocalClient = GameClient()->IsLocalClientId(ClientId);"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("IsAnyLocalClient &&\n\t\tm_pData->m_CoordXAlignFrame.m_aClientAligned[ClientId];"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("CoordXAlignState.m_Aligned || m_pData->m_CoordXAlignFrame.m_LocalAligned"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("IsLocalClient &&\n\t\tm_pData->m_CoordXAlignFrame.m_LocalAligned"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("const bool OwnNameplateScopeVisible"), std::string::npos);
	// 录像机/禅模式重构后取值统一走 NameplateRenderValue(ConfigManager(), &...)：
	// 录制中读回接管前的真实值，未接管时与直接读 g_Config 等价。
	// 昵称可见性本身由 QmNameplateNameScope 单元测试覆盖；这里只锁定两件无法从
	// 纯函数观察到的事实：取值仍经 NameplateRenderValue，且判定委托给纯函数。
	EXPECT_NE(RenderNamePlateGame.find("NameplateRenderValue(ConfigManager(), &g_Config.m_QmNameplateShowScope)"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("ShouldShowQmNameplateName(NameplateScope, pPlayerInfo->m_Local, GameClient()->IsLocalClientId(ClientId))"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowClientId = Data.m_ShowName && (g_Config.m_Debug || g_Config.m_ClNamePlatesIds) && !HideIdentity;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowClan = Data.m_ShowName && g_Config.m_ClNamePlatesClan && !HideIdentity;"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("const bool NameplateScopeAllowsCoords"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("const bool CoordModuleAllowsCoords = IsAnyLocalClient ? g_Config.m_QmNameplateCoordsOwn : g_Config.m_QmNameplateCoords;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("const bool ShowLocalAlignedCoordX = CoordModuleAllowsCoords && CoordXAlignHintEnabled && LocalCoordXAligned;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowCoordX = (CoordModuleAllowsCoords && g_Config.m_QmNameplateCoordX != 0) || ShowLocalAlignedCoordX;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowCoordY = CoordModuleAllowsCoords && g_Config.m_QmNameplateCoordY != 0;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowCoords = CoordModuleAllowsCoords || ShowLocalAlignedCoordX;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("TrackCoordXAlign &&\n\t\tGameClient()->m_Snap.m_LocalClientId >= 0"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_CoordXAligned = IsAnyLocalClient ? LocalCoordXAligned : CoordXAlignState.m_Aligned;"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("!IsAnyLocalClient &&\n\t\tGameClient()->m_Snap.m_LocalClientId >= 0"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("if(Data.m_ShowName && !HideIdentity && g_Config.m_TcWarList && g_Config.m_TcWarListShowClan"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_Local = pPlayerInfo->m_Local;"), std::string::npos);
}

TEST(QmNewUiMenuSettingsNameplateContract, NameplateStrongHookRowReservesLayoutWithoutContentWidth)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string RangeSize = FunctionBody(Source, "vec2 RangeSize(");
	const std::string AddHookRow = FunctionBody(Source, "void AddHookRow(");
	const std::string RenderNamePlateGame = FunctionBody(Source, "void CNamePlates::RenderNamePlateGame");

	EXPECT_NE(Source.find("bool m_ReserveHookStrongWeakRow;"), std::string::npos);
	EXPECT_NE(Source.find("bool m_ReserveLineHeight = false;"), std::string::npos);
	EXPECT_NE(Source.find("bool ReserveLineHeight() const { return m_ReserveLineHeight; }"), std::string::npos);
	EXPECT_NE(Source.find("class CNamePlatePartHookStrongWeakRowReserve"), std::string::npos);
	EXPECT_NE(RangeSize.find("else if(Part.ReserveLineHeight())\n\t\t\t{"), std::string::npos);
	EXPECT_NE(RangeSize.find("LineSize.y = std::max(LineSize.y, Part.Size().y + Part.Padding().y);"), std::string::npos);
	EXPECT_NE(AddHookRow.find("AddPart<CNamePlatePartHookStrongWeakRowReserve>(This);"), std::string::npos);
	EXPECT_LT(AddHookRow.find("AddPart<CNamePlatePartHookStrongWeakRowReserve>(This);"), AddHookRow.find("AddPart<CNamePlatePartHookStrongWeak>(This);"));
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ReserveHookStrongWeakRow = g_Config.m_Debug || g_Config.m_ClNamePlatesStrong > 0;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowHookStrongWeak = false;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowHookStrongWeak = g_Config.m_Debug || (g_Config.m_ClNamePlatesStrong > 0 && ShouldShowQmHookStrongWeakScope(g_Config.m_QmNameplateHookStrongWeakScope, false, Strong, Weak));"), std::string::npos);
}

TEST(QmNewUiMenuSettingsNameplateContract, NameplateGameUsesFullScopeReferenceFrame)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string RenderNamePlateGame = FunctionBody(Source, "void CNamePlates::RenderNamePlateGame");
	const std::string ResetNamePlates = FunctionBody(Source, "void CNamePlates::ResetNamePlates");

	EXPECT_NE(Source.find("CNamePlate m_aNamePlateFrameReferences[MAX_CLIENTS];"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("CNamePlate *pLayoutReference = nullptr;"), std::string::npos);
	// NameplatePartiallyHidden 现在的含义是「六档里没选到全体」，取值同样经
	// NameplateRenderValue 读回接管前的真实值。
	EXPECT_NE(RenderNamePlateGame.find("const bool NameplatePartiallyHidden = NameplateRenderValue(ConfigManager(), &g_Config.m_QmNameplateShowScope) != QM_NAMEPLATE_SHOW_SCOPE_ALL;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("if(Alpha > 0.0f && NameplateFreeMoveEnabled() && NameplatePartiallyHidden)"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("CNamePlateData FrameData = Data;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("FrameData.m_ShowName = true;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("const bool FrameShowLocalAlignedCoordX = CoordModuleAllowsCoords && CoordXAlignHintEnabled && LocalCoordXAligned;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("FrameData.m_ShowCoords = CoordModuleAllowsCoords || FrameShowLocalAlignedCoordX;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("CNamePlate &FrameNamePlate = m_pData->m_aNamePlateFrameReferences[ClientId];"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("FrameNamePlate.Update(*GameClient(), FrameData);"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("pLayoutReference = &FrameNamePlate;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("NamePlate.Render(*GameClient(), Position - vec2(0.0f, (float)g_Config.m_ClNamePlatesOffset), pLayoutReference);"), std::string::npos);
	EXPECT_NE(ResetNamePlates.find("for(CNamePlate &NamePlate : m_pData->m_aNamePlateFrameReferences)\n\t\tNamePlate.Reset(*GameClient());"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowDirection = !pPlayerInfo->m_Local;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowDirection = true;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowDirection = pPlayerInfo->m_Local;"), std::string::npos);
}
