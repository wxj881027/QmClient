/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "spectator.h"

#include "camera.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/animstate.h>
#include <game/client/components/qmclient/spectator_friend_priority.h>
#include <game/client/components/qmclient/spectator_tele_search.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon_manager.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	uint64_t SpectatorPresentationNodeKey(const char *pScope)
	{
		static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("qm_extra_spectator_presentation"));
		return BuildUiAnimNodeKey(s_BaseKey, static_cast<uint64_t>(str_quickhash(pScope)));
	}

	SUiSpringConfig SpectatorPresentationSpring()
	{
		SUiSpringConfig Spring;
		Spring.m_Stiffness = 480.0f;
		Spring.m_Damping = 42.0f;
		Spring.m_RestEpsilon = 0.006f;
		Spring.m_RestVelocity = 0.08f;
		return Spring;
	}

	SUiSpringConfig SpectatorContentSpring(bool Opening)
	{
		SUiSpringConfig Spring = SpectatorPresentationSpring();
		if(!Opening)
		{
			constexpr float CloseTimeScale = 0.30f;
			Spring.m_Stiffness /= CloseTimeScale * CloseTimeScale;
			Spring.m_Damping /= CloseTimeScale;
			Spring.m_RestVelocity /= CloseTimeScale;
		}
		return Spring;
	}
} // namespace

bool CSpectator::CanChangeSpectatorId()
{
	// don't change SpectatorId when not spectating
	if(!GameClient()->m_Snap.m_SpecInfo.m_Active)
		return false;

	// stop follow mode from changing SpectatorId
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK && GameClient()->m_DemoSpecId == SPEC_FOLLOW)
		return false;

	return true;
}

void CSpectator::SpectateNext(bool Reverse)
{
	int CurIndex = -1;
	const CNetObj_PlayerInfo **paPlayerInfos = GameClient()->m_Snap.m_apInfoByDDTeamName;

	// m_SpectatorId may be uninitialized if m_Active is false
	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(paPlayerInfos[i] && paPlayerInfos[i]->m_ClientId == GameClient()->m_Snap.m_SpecInfo.m_SpectatorId)
			{
				CurIndex = i;
				break;
			}
		}
	}

	int Start;
	if(CurIndex != -1)
	{
		if(Reverse)
			Start = CurIndex - 1;
		else
			Start = CurIndex + 1;
	}
	else
	{
		if(Reverse)
			Start = -1;
		else
			Start = 0;
	}

	int Increment = Reverse ? -1 : 1;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		int PlayerIndex = (Start + i * Increment) % MAX_CLIENTS;
		// % in C++ takes the sign of the dividend, not divisor
		if(PlayerIndex < 0)
			PlayerIndex += MAX_CLIENTS;

		const CNetObj_PlayerInfo *pPlayerInfo = paPlayerInfos[PlayerIndex];
		if(pPlayerInfo && pPlayerInfo->m_Team != TEAM_SPECTATORS)
		{
			Spectate(pPlayerInfo->m_ClientId);
			break;
		}
	}
}

void CSpectator::ConKeySpectator(IConsole::IResult *pResult, void *pUserData)
{
	CSpectator *pSelf = (CSpectator *)pUserData;

	if(pSelf->GameClient()->m_Scoreboard.IsActive())
	{
		if(pResult->GetInteger(0) == 0)
		{
			pSelf->m_Active = false;
			pSelf->m_TeleNumberInput.Deactivate();
		}
		return;
	}

	// QmClient：影子查看模式下同样允许打开选择器（成员面板统一入口）
	if(pSelf->GameClient()->m_Snap.m_SpecInfo.m_Active || pSelf->Client()->State() == IClient::STATE_DEMOPLAYBACK ||
		pSelf->GameClient()->m_RankGhost.IsViewModeActive())
		pSelf->m_Active = pResult->GetInteger(0) != 0;
	else
		pSelf->m_Active = false;
	if(!pSelf->m_Active)
		pSelf->m_TeleNumberInput.Deactivate();
}

void CSpectator::ConSpectate(IConsole::IResult *pResult, void *pUserData)
{
	CSpectator *pSelf = (CSpectator *)pUserData;
	if(!pSelf->CanChangeSpectatorId())
		return;

	pSelf->Spectate(pResult->GetInteger(0));
}

void CSpectator::ConSpectateNext(IConsole::IResult *pResult, void *pUserData)
{
	CSpectator *pSelf = (CSpectator *)pUserData;
	if(!pSelf->CanChangeSpectatorId())
		return;

	pSelf->SpectateNext(false);
}

void CSpectator::ConSpectatePrevious(IConsole::IResult *pResult, void *pUserData)
{
	CSpectator *pSelf = (CSpectator *)pUserData;
	if(!pSelf->CanChangeSpectatorId())
		return;

	pSelf->SpectateNext(true);
}

void CSpectator::ConSpectateClosest(IConsole::IResult *pResult, void *pUserData)
{
	CSpectator *pSelf = (CSpectator *)pUserData;
	pSelf->SpectateClosest();
}

void CSpectator::ConMultiView(IConsole::IResult *pResult, void *pUserData)
{
	CSpectator *pSelf = (CSpectator *)pUserData;
	int Input = pResult->GetInteger(0);

	if(Input == -1)
		std::fill(std::begin(pSelf->GameClient()->m_aMultiViewId), std::end(pSelf->GameClient()->m_aMultiViewId), false); // remove everyone from multiview
	else if(Input < MAX_CLIENTS && Input >= 0)
		pSelf->GameClient()->m_aMultiViewId[Input] = !pSelf->GameClient()->m_aMultiViewId[Input]; // activate or deactivate one player from multiview
}

CSpectator::CSpectator()
{
	m_SelectorMouse = vec2(0.0f, 0.0f);
	OnReset();
}

void CSpectator::OnConsoleInit()
{
	Console()->Register("+spectate", "", CFGFLAG_CLIENT, ConKeySpectator, this, "Open spectator mode selector");
	Console()->Register("spectate", "i[spectator-id]", CFGFLAG_CLIENT, ConSpectate, this, "Switch spectator mode");
	Console()->Register("spectate_next", "", CFGFLAG_CLIENT, ConSpectateNext, this, "Spectate the next player");
	Console()->Register("spectate_previous", "", CFGFLAG_CLIENT, ConSpectatePrevious, this, "Spectate the previous player");
	Console()->Register("spectate_closest", "", CFGFLAG_CLIENT, ConSpectateClosest, this, "Spectate the closest player");
	Console()->Register("spectate_multiview", "i[id]", CFGFLAG_CLIENT, ConMultiView, this, "Add/remove Client-IDs to spectate them exclusively (-1 to reset)");
}

// QmClient：自由视角的鼠标平移不依赖选择器是否打开——查看模式下相机已脱离角色，
// 鼠标此时是镜头控制器（与原生自由旁观同语义）。
// 选择器在输入栈中排在菜单/HUD 编辑器/表情轮/饼菜单之前，这些 UI 打开时必须让出增量，
// 否则它们的光标会失灵；控制台与聊天排在前面，会先一步吞掉增量，无需在此判断
bool CSpectator::GhostFreeCameraCanPan() const
{
	if(!GameClient()->m_RankGhost.IsViewModeActive() ||
		GameClient()->m_RankGhost.ViewCameraMode() != CRankGhost::EViewCameraMode::FREE ||
		Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return false;
	return !GameClient()->m_Menus.IsActive() && !GameClient()->m_GameConsole.IsActive() &&
	       !GameClient()->m_HudEditor.IsActive() && !GameClient()->m_Emoticon.IsActive() &&
	       !GameClient()->m_PieMenu.IsActive() && !Ui()->IsPopupOpen();
}

// QmClient：查看模式下鼠标归谁用。控制面板显示时归 UI 光标（面板可点）；隐藏时归
// 镜头或选择器。与 demo 播放一致：UI 只在面板出现时才占用鼠标，屏幕上也只有一个光标。
bool CSpectator::GhostUiCursorActive() const
{
	if(!GameClient()->m_RankGhost.IsViewModeActive() || Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return false;
	if(GameClient()->m_Menus.IsActive() || GameClient()->m_GameConsole.IsActive() || GameClient()->m_HudEditor.IsActive())
		return false;
	// 原生选择器打开时归它（它的玩家列表、视角按钮与光标都只读 m_SelectorMouse）
	return !m_Active && m_GhostPanelOpen;
}

bool CSpectator::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	const bool ViewMode = GameClient()->m_RankGhost.IsViewModeActive();
	const bool GhostFreeCamera = GhostFreeCameraCanPan();
	if(!m_Active && !GhostFreeCamera && !GhostUiCursorActive())
		return false;

	// 镜头平移要用原始增量：ConvertMouseMove 换算的是菜单/UI 灵敏度（默认 200%），
	// 拿它推镜头会明显偏快；UI 光标与选择器光标才需要那套换算。
	const float RawX = x;
	const float RawY = y;
	Ui()->ConvertMouseMove(&x, &y, CursorType);

	if(ViewMode)
	{
		// 选择器打开：只驱动选择器，一个光标、一套命中判定
		if(m_Active)
		{
			m_SelectorMouse += vec2(x, y);
			return true;
		}

		// 控制面板显示：鼠标归面板，此时不平移镜头（否则点按钮的同时镜头也在动）
		if(m_GhostPanelOpen)
		{
			Ui()->OnCursorMove(x, y);
			return true;
		}

		// 面板隐藏 + 自由视角：鼠标是镜头控制器（用原始增量，不套菜单灵敏度）
		if(GhostFreeCamera)
		{
			GameClient()->m_RankGhost.ViewFreeCameraPan(RawX, RawY);
			return true;
		}
		return false;
	}

	m_SelectorMouse += vec2(x, y);
	return true;
}

bool CSpectator::OnInput(const IInput::CEvent &Event)
{
	if(IsActive() && Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		m_SelectedSpectatorId = NO_SELECTION;
		m_Active = false;
		m_TeleNumberInput.Deactivate();
		return true;
	}

	// QmClient：编号输入行激活时独占数字按键，回车直接查找。
	if(IsActive() && m_TeleNumberInput.IsActive())
	{
		if((Event.m_Flags & IInput::FLAG_PRESS) && (Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER))
			FindTele();
		else
		{
			const int Digit = qm_spectator_tele::DigitFromKey(Event.m_Key);
			if((Event.m_Flags & IInput::FLAG_TEXT) && m_IgnoreTeleNumberTextEvent)
				m_IgnoreTeleNumberTextEvent = false;
			else if((Event.m_Flags & IInput::FLAG_PRESS) && Digit >= 0 && !Input()->ModifierIsPressed() && !Input()->ShiftIsPressed())
			{
				// 默认按住 Shift 用于 HUD/其它语义，此时直接写入数字，避免录入符号或重复文本。
				const char aDigit[] = {static_cast<char>('0' + Digit), '\0'};
				m_TeleNumberInput.SetRange(aDigit, m_TeleNumberInput.GetSelectionStart(), m_TeleNumberInput.GetSelectionEnd());
				m_IgnoreTeleNumberTextEvent = true;
			}
			else
			{
				if(Event.m_Flags & (IInput::FLAG_PRESS | IInput::FLAG_RELEASE))
					m_IgnoreTeleNumberTextEvent = false;
				m_TeleNumberInput.ProcessInput(Event);
			}
			if(m_TeleNumberInput.WasChanged())
			{
				m_TeleSearchStatus = ETeleSearchStatus::IDLE;
				m_LastTeleNumber = 0;
			}
		}
		return true;
	}

	// QmClient：查看模式的 ESC 与 demo 播放同语义——单击开关控制面板，双击打开游戏菜单。
	// 单击必须立刻生效（不能等双击窗口），所以先切换再在双击时还原回切换前的状态，
	// 双击结束把 ESC 让给菜单（本组件在输入栈里排在菜单之前）。
	if(GameClient()->m_RankGhost.IsViewModeActive() && !GameClient()->m_GameConsole.IsActive())
	{
		if((Event.m_Flags & IInput::FLAG_PRESS) != 0 && Event.m_Key == KEY_ESCAPE)
		{
			const float Now = Client()->LocalTime();
			if(m_GhostEscapeArmed && Now - m_GhostEscapeLastTime <= 0.4f)
			{
				m_GhostEscapeArmed = false;
				m_GhostPanelOpen = m_GhostPanelOpenBeforeEscape;
				return false;
			}
			m_GhostPanelOpenBeforeEscape = m_GhostPanelOpen;
			m_GhostPanelOpen = !m_GhostPanelOpen;
			m_GhostEscapeArmed = true;
			m_GhostEscapeLastTime = Now;
			return true;
		}
		if(m_GhostEscapeArmed && Client()->LocalTime() - m_GhostEscapeLastTime > 0.4f)
			m_GhostEscapeArmed = false;
	}

	if(g_Config.m_ClSpectatorMouseclicks)
	{
		if(GameClient()->m_Snap.m_SpecInfo.m_Active && !IsActive() && !GameClient()->m_MultiViewActivated &&
			!Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive() && !GameClient()->m_Menus.IsActive())
		{
			if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_MOUSE_1)
			{
				if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
					Spectate(SPEC_FREEVIEW);
				else
					SpectateClosest();
				return true;
			}
		}
	}

	if(GameClient()->m_Camera.SpectatingPlayer() && GameClient()->m_Camera.CanUseAutoSpecCamera())
	{
		if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_MOUSE_2)
		{
			GameClient()->m_Camera.ResetAutoSpecCamera();
			return true;
		}
	}

	// QmClient：查看模式下鼠标归 UI 光标时吞掉鼠标键，点击控制条不再误触发射击/钩爪。
	// 滚轮不直接丢弃：它是相机面板的缩放控件（默认绑定还是 +prevweapon/+nextweapon，
	// 放过去会误切武器），转成 zoom+/- 既保留吞键意图，又让多人同框取景可调
	if(GhostUiCursorActive() && Event.m_Key >= KEY_MOUSE_1 && Event.m_Key <= KEY_MOUSE_WHEEL_RIGHT)
	{
		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			if(Event.m_Key == KEY_MOUSE_WHEEL_UP)
				Console()->ExecuteLine("zoom+", IConsole::CLIENT_ID_UNSPECIFIED);
			else if(Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
				Console()->ExecuteLine("zoom-", IConsole::CLIENT_ID_UNSPECIFIED);
		}
		return true;
	}

	return false;
}

void CSpectator::OnRelease()
{
	OnReset();
}

void CSpectator::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		m_TeleNumberInput.Deactivate();
		m_TeleSearchPending = false;
		return;
	}

	// QmClient：等自由视角状态真正生效后再设置位置，避免被跟随镜头覆盖查找目标。
	if(m_TeleSearchPending)
	{
		const auto &SpecInfo = GameClient()->m_Snap.m_SpecInfo;
		if(!SpecInfo.m_Active || GameClient()->m_MultiViewActivated)
			m_TeleSearchPending = false;
		else if(SpecInfo.m_SpectatorId == SPEC_FREEVIEW && !SpecInfo.m_UsePosition)
		{
			GameClient()->m_Camera.SetViewWorld(m_TeleSearchPosition);
			m_TeleSearchPending = false;
		}
	}

	if(!GameClient()->m_MultiViewActivated && m_MultiViewActivateDelay != 0.0f)
	{
		if(m_MultiViewActivateDelay <= Client()->LocalTime())
		{
			m_MultiViewActivateDelay = 0.0f;
			GameClient()->m_MultiViewActivated = true;
		}
	}

	const bool ExtraAnimations = g_Config.m_QmExtraAnimations != 0 && GameClient()->UiRuntimeV2()->Enabled();

	if(!m_Active)
	{
		m_TeleNumberInput.Deactivate();
		// closing the spectator menu
		if(m_WasActive)
		{
			if(m_SelectedSpectatorId != NO_SELECTION)
			{
				if(m_SelectedSpectatorId == MULTI_VIEW)
					GameClient()->m_MultiViewActivated = true;
				else if(m_SelectedSpectatorId == SPEC_FREEVIEW || m_SelectedSpectatorId == SPEC_FOLLOW)
					GameClient()->m_MultiViewActivated = false;

				if(!GameClient()->m_MultiViewActivated)
					Spectate(m_SelectedSpectatorId);

				if(GameClient()->m_MultiViewActivated && m_SelectedSpectatorId != MULTI_VIEW && GameClient()->m_Teams.Team(m_SelectedSpectatorId) != GameClient()->m_MultiViewTeam)
				{
					GameClient()->ResetMultiView();
					Spectate(m_SelectedSpectatorId);
					m_MultiViewActivateDelay = Client()->LocalTime() + 0.3f;
				}
			}
			m_WasActive = false;
		}
		if(!ExtraAnimations || !m_PresentationInitialized)
		{
			// 选择器收起时原生 HUD 不做动画，但查看模式的底部控制条仍要在
			RenderGhostControlBar();
			return;
		}
	}

	const bool ViewModeActive = GameClient()->m_RankGhost.IsViewModeActive();
	if(!GameClient()->m_Snap.m_SpecInfo.m_Active && Client()->State() != IClient::STATE_DEMOPLAYBACK && !ViewModeActive)
	{
		m_Active = false;
		m_WasActive = false;
		m_TeleNumberInput.Deactivate();
		if(!ExtraAnimations)
		{
			RenderGhostControlBar();
			return;
		}
	}

	const bool WantActive = m_Active;
	float PanelAlpha = WantActive ? 1.0f : 0.0f;
	float ContentAlpha = PanelAlpha;
	float PanelOffsetY = WantActive ? 0.0f : -10.0f;
	CUiV2AnimationRuntime *pAnimRuntime = ExtraAnimations ? &GameClient()->UiRuntimeV2()->AnimRuntime() : nullptr;
	const uint64_t PanelNode = SpectatorPresentationNodeKey("panel");
	if(pAnimRuntime != nullptr)
	{
		if(!m_PresentationInitialized)
		{
			SetUiPresentationStateValue(*pAnimRuntime, PanelNode, EUiAnimProperty::ALPHA, 0.0f);
			SetUiPresentationStateValue(*pAnimRuntime, PanelNode, EUiAnimProperty::COLOR_A, 0.0f);
			SetUiPresentationStateValue(*pAnimRuntime, PanelNode, EUiAnimProperty::POS_Y, -10.0f);
			m_PresentationInitialized = true;
		}
		const SUiSpringConfig Spring = SpectatorPresentationSpring();
		PanelAlpha = std::clamp(ResolveUiPresentationStateValue(*pAnimRuntime, PanelNode, EUiAnimProperty::ALPHA, WantActive ? 1.0f : 0.0f, Spring, 3, 0.004f), 0.0f, 1.0f);
		ContentAlpha = std::clamp(ResolveUiPresentationStateValue(*pAnimRuntime, PanelNode, EUiAnimProperty::COLOR_A, WantActive ? 1.0f : 0.0f, SpectatorContentSpring(WantActive), 3, 0.004f), 0.0f, 1.0f);
		PanelOffsetY = ResolveUiPresentationStateValue(*pAnimRuntime, PanelNode, EUiAnimProperty::POS_Y, WantActive ? 0.0f : -10.0f, Spring, 3, 0.01f);
		if(!WantActive && PanelAlpha <= 0.01f && !pAnimRuntime->HasActiveAnimation(PanelNode, EUiAnimProperty::ALPHA))
		{
			RenderGhostControlBar();
			return;
		}
	}

	if(WantActive)
	{
		m_WasActive = true;
		m_SelectedSpectatorId = NO_SELECTION;
	}

	// draw background
	float Width = 400 * 3.0f * Graphics()->ScreenAspect();
	float Height = 400 * 3.0f;
	float ObjWidth = 300.0f;
	float FontSize = 20.0f;
	float BigFontSize = 20.0f;
	float StartY = -190.0f;
	float LineHeight = 60.0f;
	float TeeSizeMod = 1.0f;
	float RoundRadius = 30.0f;
	bool MultiViewSelected = false;
	int TotalPlayers = 0;
	int PerLine = 8;
	float BoxMove = -10.0f;
	float BoxOffset = 0.0f;

	for(const auto &pInfo : GameClient()->m_Snap.m_apInfoByDDTeamName)
	{
		if(!pInfo || pInfo->m_Team == TEAM_SPECTATORS)
			continue;

		++TotalPlayers;
	}

	if(TotalPlayers > 96)
	{
		FontSize = 15.0f;
		LineHeight = 15.0f;
		TeeSizeMod = 0.3f;
		PerLine = 32;
		RoundRadius = 5.0f;
		BoxMove = 3.0f;
		BoxOffset = 6.0f;
	}
	else if(TotalPlayers > 64)
	{
		FontSize = 16.0f;
		LineHeight = 19.0f;
		TeeSizeMod = 0.45f;
		PerLine = 24;
		RoundRadius = 6.0f;
		BoxMove = 3.0f;
		BoxOffset = 6.0f;
	}
	else if(TotalPlayers > 32)
	{
		FontSize = 18.0f;
		LineHeight = 30.0f;
		TeeSizeMod = 0.7f;
		PerLine = 16;
		RoundRadius = 10.0f;
		BoxMove = 3.0f;
		BoxOffset = 6.0f;
	}
	if(TotalPlayers > 16)
	{
		ObjWidth = 600.0f;
	}

	const vec2 ScreenSize = vec2(Width, Height);
	const float CenterX = Width / 2.0f;
	const float CenterY = Height / 2.0f + PanelOffsetY;
	const vec2 ScreenCenter = vec2(CenterX, CenterY);
	CUIRect SpectatorRect = {CenterX - ObjWidth, CenterY - 300.0f, ObjWidth * 2.0f, 600.0f};
	CUIRect SpectatorMouseRect;
	SpectatorRect.Margin(20.0f, &SpectatorMouseRect);

	// QmClient: 影子旁观模式——右侧成员面板（点击锁定该成员视角）
	const bool ShowRankPanel = GameClient()->m_RankGhost.IsViewModeActive() && GameClient()->m_RankGhost.ViewMemberCount() > 0;
	const float RankPanelWidth = 280.0f;
	const CUIRect RankPanelRect = {CenterX + ObjWidth + 10.0f, CenterY - 300.0f, RankPanelWidth, 600.0f};
	if(ShowRankPanel)
	{
		// 触摸按下/释放判定区域扩展到成员面板
		const float X0 = minimum(SpectatorMouseRect.x, RankPanelRect.x);
		const float Y0 = minimum(SpectatorMouseRect.y, RankPanelRect.y);
		const float X1 = maximum(SpectatorMouseRect.x + SpectatorMouseRect.w, RankPanelRect.x + RankPanelRect.w);
		const float Y1 = maximum(SpectatorMouseRect.y + SpectatorMouseRect.h, RankPanelRect.y + RankPanelRect.h);
		SpectatorMouseRect = {X0, Y0, X1 - X0, Y1 - Y0};
	}

	const bool WasTouchPressed = m_TouchState.m_AnyPressed;
	if(WantActive)
		Ui()->UpdateTouchState(m_TouchState);
	if(WantActive && m_TouchState.m_AnyPressed)
	{
		const vec2 TouchPos = (m_TouchState.m_PrimaryPosition - vec2(0.5f, 0.5f)) * ScreenSize;
		if(SpectatorMouseRect.Inside(ScreenCenter + TouchPos))
		{
			m_SelectorMouse = TouchPos;
		}
	}
	else if(WantActive && WasTouchPressed)
	{
		const vec2 TouchPos = (m_TouchState.m_PrimaryPosition - vec2(0.5f, 0.5f)) * ScreenSize;
		if(!SpectatorRect.Inside(ScreenCenter + TouchPos))
		{
			OnRelease();
			return;
		}
	}

	Graphics()->MapScreen(0, 0, Width, Height);

	SpectatorRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f * PanelAlpha), IGraphics::CORNER_ALL, 20.0f);

	// clamp mouse position to selector area
	m_SelectorMouse.x = std::clamp(m_SelectorMouse.x, -(ObjWidth - 20.0f), ObjWidth - 20.0f);
	m_SelectorMouse.y = std::clamp(m_SelectorMouse.y, -280.0f, 280.0f);

	// QmClient：查看模式的主列表只列 Rank 1 影子成员。服务器玩家与回放无关，列出来只会
	// 让人分不清跟的是谁；成员行点击即锁定该成员视角。右侧那块重复的成员面板随之跳过。
	if(ViewModeActive)
	{
		const float RowHeight = 34.0f;
		const float RowX = -(ObjWidth - 35.0f);
		const bool RowPressed = WantActive && (Input()->KeyPress(KEY_MOUSE_1) || m_TouchState.m_PrimaryPressed);
		const int MemberCount = minimum(GameClient()->m_RankGhost.ViewMemberCount(), 12);
		for(int MemberIndex = 0; MemberIndex < MemberCount; MemberIndex++)
		{
			char aMemberName[MAX_NAME_LENGTH];
			if(!GameClient()->m_RankGhost.ViewMemberName(MemberIndex, aMemberName, sizeof(aMemberName)))
				continue;
			const float RowY = -190.0f + MemberIndex * RowHeight;
			const float RowW = (ObjWidth - 40.0f) * 2.0f;
			const bool Hovered = WantActive && m_SelectorMouse.x >= RowX && m_SelectorMouse.x <= RowX + RowW &&
					     m_SelectorMouse.y >= RowY && m_SelectorMouse.y <= RowY + RowHeight - 2.0f;
			const bool Selected = GameClient()->m_RankGhost.ViewSelectedMember() == MemberIndex;
			if(Hovered && RowPressed)
				GameClient()->m_RankGhost.ViewSelectMember(MemberIndex);

			ColorRGBA RowColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f * ContentAlpha);
			if(Selected)
				RowColor = ColorRGBA(0.30f, 0.62f, 1.0f, 0.45f * ContentAlpha);
			else if(Hovered)
				RowColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.15f * ContentAlpha);
			Graphics()->DrawRect(CenterX + RowX, CenterY + RowY, RowW, RowHeight - 2.0f, RowColor, IGraphics::CORNER_ALL, 10.0f);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, (Selected ? 1.0f : 0.7f) * ContentAlpha);
			TextRender()->Text(CenterX + RowX + 14.0f, CenterY + RowY + 8.0f, FontSize, aMemberName, -1.0f);
		}
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

		RenderTools()->RenderCursor(ScreenCenter + m_SelectorMouse, 48.0f, ContentAlpha);
		return;
	}

	// QmClient: 成员面板可见时光标允许进入右侧面板
	const bool OverRankPanel = ShowRankPanel && m_SelectorMouse.x >= ObjWidth + 10.0f;
	if(ShowRankPanel)
		m_SelectorMouse.x = std::clamp(m_SelectorMouse.x, -(ObjWidth - 20.0f), ObjWidth + RankPanelWidth - 10.0f);

	const bool MousePressed = WantActive && (Input()->KeyPress(KEY_MOUSE_1) || m_TouchState.m_PrimaryPressed);

	// QmClient: Rank 1 成员面板——点击行锁定该成员视角
	if(ShowRankPanel)
	{
		RankPanelRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f * PanelAlpha), IGraphics::CORNER_ALL, 20.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, ContentAlpha);
		TextRender()->Text(RankPanelRect.x + 20.0f, RankPanelRect.y + 22.0f, BigFontSize, Localize("Rank 1 members"), -1.0f);

		const int MemberCount = minimum(GameClient()->m_RankGhost.ViewMemberCount(), 12);
		for(int MemberIndex = 0; MemberIndex < MemberCount; MemberIndex++)
		{
			char aMemberName[MAX_NAME_LENGTH];
			if(!GameClient()->m_RankGhost.ViewMemberName(MemberIndex, aMemberName, sizeof(aMemberName)))
				continue;
			const CUIRect RowRect = {RankPanelRect.x + 15.0f, RankPanelRect.y + 65.0f + MemberIndex * 42.0f, RankPanelWidth - 30.0f, 34.0f};
			const bool Hovered = WantActive && m_SelectorMouse.x >= RowRect.x && m_SelectorMouse.x < RowRect.x + RowRect.w &&
					     m_SelectorMouse.y >= RowRect.y && m_SelectorMouse.y < RowRect.y + RowRect.h;
			const bool Selected = GameClient()->m_RankGhost.ViewSelectedMember() == MemberIndex;
			if(Hovered && MousePressed)
				GameClient()->m_RankGhost.ViewSelectMember(MemberIndex);
			ColorRGBA RowColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f * ContentAlpha);
			if(Selected)
				RowColor = ColorRGBA(0.30f, 0.62f, 1.0f, 0.45f * ContentAlpha);
			else if(Hovered)
				RowColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.15f * ContentAlpha);
			Graphics()->DrawRect(RowRect.x, RowRect.y, RowRect.w, RowRect.h, RowColor, IGraphics::CORNER_ALL, 10.0f);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, (Selected ? 1.0f : 0.65f) * ContentAlpha);
			TextRender()->Text(RowRect.x + 12.0f, RowRect.y + (RowRect.h - FontSize) / 2.0f, FontSize, aMemberName, -1.0f);
		}
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	// draw selections
	if((Client()->State() == IClient::STATE_DEMOPLAYBACK && GameClient()->m_DemoSpecId == SPEC_FREEVIEW) ||
		(Client()->State() != IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW))
	{
		Graphics()->DrawRect(CenterX - (ObjWidth - 20.0f), CenterY - 280.0f, ((ObjWidth * 2.0f) / 3.0f) - 40.0f, 60.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * ContentAlpha), IGraphics::CORNER_ALL, 20.0f);
	}

	if(GameClient()->m_MultiViewActivated)
	{
		Graphics()->DrawRect(CenterX - (ObjWidth - 20.0f) + (ObjWidth * 2.0f / 3.0f), CenterY - 280.0f, ((ObjWidth * 2.0f) / 3.0f) - 40.0f, 60.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * ContentAlpha), IGraphics::CORNER_ALL, 20.0f);
	}

	if(Client()->State() == IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_LocalClientId >= 0 && GameClient()->m_DemoSpecId == SPEC_FOLLOW)
	{
		Graphics()->DrawRect(CenterX - (ObjWidth - 20.0f) + (ObjWidth * 2.0f * 2.0f / 3.0f), CenterY - 280.0f, ((ObjWidth * 2.0f) / 3.0f) - 40.0f, 60.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * ContentAlpha), IGraphics::CORNER_ALL, 20.0f);
	}

	bool FreeViewSelected = false;
	if(WantActive && m_SelectorMouse.x >= -(ObjWidth - 20.0f) && m_SelectorMouse.x <= -(ObjWidth - 20.0f) + ((ObjWidth * 2.0f) / 3.0f) - 40.0f &&
		m_SelectorMouse.y >= -280.0f && m_SelectorMouse.y <= -220.0f)
	{
		m_SelectedSpectatorId = SPEC_FREEVIEW;
		FreeViewSelected = true;
		if(MousePressed)
		{
			GameClient()->m_MultiViewActivated = false;
			Spectate(m_SelectedSpectatorId);
		}
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, (FreeViewSelected ? 1.0f : 0.5f) * ContentAlpha);
	TextRender()->Text(CenterX - (ObjWidth - 40.0f), CenterY - 280.f + (60.f - BigFontSize) / 2.f, BigFontSize, Localize("Free-View"), -1.0f);

	if(WantActive && m_SelectorMouse.x >= -(ObjWidth - 20.0f) + (ObjWidth * 2.0f / 3.0f) && m_SelectorMouse.x <= -(ObjWidth - 20.0f) + (ObjWidth * 2.0f / 3.0f) + ((ObjWidth * 2.0f) / 3.0f) - 40.0f &&
		m_SelectorMouse.y >= -280.0f && m_SelectorMouse.y <= -220.0f)
	{
		m_SelectedSpectatorId = MULTI_VIEW;
		MultiViewSelected = true;
		if(MousePressed)
		{
			GameClient()->m_MultiViewActivated = true;
		}
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, (MultiViewSelected ? 1.0f : 0.5f) * ContentAlpha);
	TextRender()->Text(CenterX - (ObjWidth - 40.0f) + (ObjWidth * 2.0f / 3.0f), CenterY - 280.f + (60.f - BigFontSize) / 2.f, BigFontSize, Localize("Multi-View"), -1.0f);

	if(Client()->State() == IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_LocalClientId >= 0)
	{
		bool FollowSelected = false;
		if(WantActive && m_SelectorMouse.x >= -(ObjWidth - 20.0f) + (ObjWidth * 2.0f * 2.0f / 3.0f) && m_SelectorMouse.x <= -(ObjWidth - 20.0f) + (ObjWidth * 2.0f * 2.0f / 3.0f) + ((ObjWidth * 2.0f) / 3.0f) - 40.0f &&
			m_SelectorMouse.y >= -280.0f && m_SelectorMouse.y <= -220.0f)
		{
			m_SelectedSpectatorId = SPEC_FOLLOW;
			FollowSelected = true;
			if(MousePressed)
			{
				GameClient()->m_MultiViewActivated = false;
				Spectate(m_SelectedSpectatorId);
			}
		}
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, (FollowSelected ? 1.0f : 0.5f) * ContentAlpha);
		TextRender()->Text(CenterX - (ObjWidth - 40.0f) + (ObjWidth * 2.0f * 2.0f / 3.0f), CenterY - 280.0f + (60.f - BigFontSize) / 2.f, BigFontSize, Localize("Follow"), -1.0f);
	}

	float x = -(ObjWidth - 35.0f), y = StartY;

	const CNetObj_PlayerInfo *apDisplayPlayers[MAX_CLIENTS];
	bool aIsFriend[MAX_CLIENTS];
	int aDisplayOrder[MAX_CLIENTS];
	int DisplayCount = 0;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByDDTeamName)
	{
		if(!pInfo || pInfo->m_Team == TEAM_SPECTATORS)
			continue;
		apDisplayPlayers[DisplayCount] = pInfo;
		aIsFriend[DisplayCount] = GameClient()->m_aClients[pInfo->m_ClientId].m_Friend;
		++DisplayCount;
	}
	const int FriendCount = qm_spectator_friends::BuildFriendFirstOrder(aIsFriend, DisplayCount, aDisplayOrder);
	const float TitleHeight = std::clamp(LineHeight * 0.5f, 12.0f, 15.0f);
	const float TitleFontSize = TitleHeight * 0.8f;
	const auto DrawGroupTitle = [&](const char *pTitle, const ColorRGBA &TitleColor) {
		const float TitleLeft = CenterX + x - 10.0f + BoxOffset;
		const float TitleTop = CenterY + y + BoxMove;
		TextRender()->TextColor(TitleColor.WithMultipliedAlpha(ContentAlpha));
		TextRender()->Text(TitleLeft, TitleTop + (TitleHeight - TitleFontSize) / 2.0f, TitleFontSize, pTitle, -1.0f);
		Graphics()->DrawRect(TitleLeft, TitleTop + TitleHeight - 1.0f, 270.0f - BoxOffset, 1.0f,
			ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * ContentAlpha), IGraphics::CORNER_NONE, 0.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		y += TitleHeight;
	};

	int OldDDTeam = -1;

	for(int i = 0; i < DisplayCount; ++i)
	{
		const int Count = i + 1;
		if(Count == PerLine + 1 || (Count > PerLine + 1 && (Count - 1) % PerLine == 0))
		{
			x += 290.0f;
			y = StartY;
		}

		if(FriendCount > 0 && i == 0)
			DrawGroupTitle(Localize("Friends"), color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor)));
		else if(FriendCount > 0 && i == FriendCount)
			DrawGroupTitle(Localize("Others"), ColorRGBA(1.0f, 1.0f, 1.0f, 0.85f));

		const CNetObj_PlayerInfo *pInfo = apDisplayPlayers[aDisplayOrder[i]];
		const int DDTeam = GameClient()->m_Teams.Team(pInfo->m_ClientId);
		const bool StartsGroup = i % PerLine == 0 || (FriendCount > 0 && i == FriendCount);
		const bool EndsGroup = i + 1 == DisplayCount || (i + 1) % PerLine == 0 || (FriendCount > 0 && i + 1 == FriendCount);
		const int NextDDTeam = EndsGroup ? 0 : GameClient()->m_Teams.Team(apDisplayPlayers[aDisplayOrder[i + 1]]->m_ClientId);

		if(DDTeam != TEAM_FLOCK)
		{
			const ColorRGBA Color = GameClient()->GetDDTeamColor(DDTeam).WithAlpha(0.5f * ContentAlpha);
			int Corners = 0;
			if(StartsGroup || OldDDTeam != DDTeam)
				Corners |= IGraphics::CORNER_TL | IGraphics::CORNER_TR;
			if(EndsGroup || NextDDTeam != DDTeam)
				Corners |= IGraphics::CORNER_BL | IGraphics::CORNER_BR;
			Graphics()->DrawRect(CenterX + x - 10.0f + BoxOffset, CenterY + y + BoxMove, 270.0f - BoxOffset, LineHeight, Color, Corners, RoundRadius);
		}
		OldDDTeam = DDTeam;

		if((Client()->State() == IClient::STATE_DEMOPLAYBACK && GameClient()->m_DemoSpecId == pInfo->m_ClientId) ||
			(Client()->State() != IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == pInfo->m_ClientId))
		{
			Graphics()->DrawRect(CenterX + x - 10.0f + BoxOffset, CenterY + y + BoxMove, 270.0f - BoxOffset, LineHeight, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * ContentAlpha), IGraphics::CORNER_ALL, RoundRadius);
		}

		bool PlayerSelected = false;
		if(WantActive && !OverRankPanel && m_SelectorMouse.x >= x - 10.0f && m_SelectorMouse.x < x + 260.0f &&
			m_SelectorMouse.y >= y - (LineHeight / 6.0f) && m_SelectorMouse.y < y + (LineHeight * 5.0f / 6.0f))
		{
			m_SelectedSpectatorId = pInfo->m_ClientId;
			PlayerSelected = true;
			if(MousePressed)
			{
				if(GameClient()->m_MultiViewActivated)
				{
					if(GameClient()->m_MultiViewTeam == DDTeam)
					{
						GameClient()->m_aMultiViewId[m_SelectedSpectatorId] = !GameClient()->m_aMultiViewId[m_SelectedSpectatorId];
						if(!GameClient()->m_aMultiViewId[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId])
						{
							int NewClientId = GameClient()->FindFirstMultiViewId();
							if(NewClientId < MAX_CLIENTS && NewClientId >= 0)
							{
								GameClient()->CleanMultiViewId(NewClientId);
								GameClient()->m_aMultiViewId[NewClientId] = true;
								Spectate(NewClientId);
							}
						}
					}
					else
					{
						GameClient()->ResetMultiView();
						Spectate(m_SelectedSpectatorId);
						m_MultiViewActivateDelay = Client()->LocalTime() + 0.3f;
					}
				}
				else
				{
					Spectate(m_SelectedSpectatorId);
				}
			}
		}
		float TeeAlpha;
		float NameAlpha;
		if(Client()->State() == IClient::STATE_DEMOPLAYBACK &&
			!GameClient()->m_Snap.m_aCharacters[pInfo->m_ClientId].m_Active)
		{
			NameAlpha = 0.25f;
			TeeAlpha = 0.5f;
		}
		else
		{
			NameAlpha = PlayerSelected ? 1.0f : 0.5f;
			TeeAlpha = 1.0f;
		}
		NameAlpha *= ContentAlpha;
		TeeAlpha *= ContentAlpha;
		CTextCursor NameCursor;
		NameCursor.SetPosition(vec2(CenterX + x + 50.0f, CenterY + y + BoxMove + (LineHeight - FontSize) / 2.f));
		NameCursor.m_FontSize = FontSize;
		NameCursor.m_Flags |= TEXTFLAG_ELLIPSIS_AT_END;
		NameCursor.m_LineWidth = 180.0f;
		const int ClientId = pInfo->m_ClientId;
		const bool HideIdentity = GameClient()->ShouldHideStreamerIdentity(ClientId);
		const bool IsFriend = GameClient()->m_aClients[ClientId].m_Friend;
		char aNameBuf[MAX_NAME_LENGTH];
		char aClanBuf[MAX_CLAN_LENGTH];
		GameClient()->FormatStreamerName(ClientId, aNameBuf, sizeof(aNameBuf));
		GameClient()->FormatStreamerClan(ClientId, aClanBuf, sizeof(aClanBuf));
		bool IsSameClan = false;
		if(aClanBuf[0] != '\0')
		{
			const int LocalClientId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
			if(LocalClientId >= 0 && str_comp(aClanBuf, GameClient()->m_aClients[LocalClientId].m_aClan) == 0)
			{
				IsSameClan = true;
			}
		}

		ColorRGBA NameColor;
		if(GameClient()->IsLocalClientId(ClientId))
		{
			const float Time = Client()->GlobalTime();
			const float Hue = std::fmod(Time * 0.15f, 1.0f);
			NameColor = color_cast<ColorRGBA>(ColorHSLA(Hue, 0.7f, 0.65f, 1.0f));
		}
		else if(IsFriend)
		{
			NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor));
		}
		else if(IsSameClan)
		{
			NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor));
		}
		else
		{
			NameColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		}
		NameColor.a *= NameAlpha;
		TextRender()->TextColor(NameColor);

		if(g_Config.m_ClShowIds && !HideIdentity)
		{
			char aClientId[16];
			GameClient()->FormatClientId(ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
			TextRender()->TextEx(&NameCursor, aClientId);
		}

		TextRender()->TextEx(&NameCursor, aNameBuf);
		if(GameClient()->m_MultiViewActivated)
		{
			if(GameClient()->m_aMultiViewId[pInfo->m_ClientId])
			{
				TextRender()->TextColor(0.1f, 1.0f, 0.1f, (PlayerSelected ? 1.0f : 0.5f) * ContentAlpha);
				TextRender()->Text(CenterX + x + 50.0f + 180.0f, CenterY + y + BoxMove + (LineHeight - FontSize) / 2.f, FontSize - 3, "⬤", 220.0f);
			}
			else if(GameClient()->m_MultiViewTeam == DDTeam)
			{
				TextRender()->TextColor(1.0f, 0.1f, 0.1f, (PlayerSelected ? 1.0f : 0.5f) * ContentAlpha);
				TextRender()->Text(CenterX + x + 50.0f + 180.0f, CenterY + y + BoxMove + (LineHeight - FontSize) / 2.f, FontSize - 3, "◯", 220.0f);
			}
		}

		// flag
		if(GameClient()->m_Snap.m_pGameInfoObj && (GameClient()->m_Snap.m_pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS) &&
			GameClient()->m_Snap.m_pGameDataObj && (GameClient()->m_Snap.m_pGameDataObj->m_FlagCarrierRed == pInfo->m_ClientId || GameClient()->m_Snap.m_pGameDataObj->m_FlagCarrierBlue == pInfo->m_ClientId))
		{
			Graphics()->BlendNormal();
			if(GameClient()->m_Snap.m_pGameDataObj->m_FlagCarrierBlue == pInfo->m_ClientId)
				Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteFlagBlue);
			else
				Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteFlagRed);

			Graphics()->QuadsBegin();
			Graphics()->QuadsSetSubset(1, 0, 0, 1);

			float Size = LineHeight;
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, ContentAlpha);
			IGraphics::CQuadItem QuadItem(CenterX + x - LineHeight / 5.0f, CenterY + y - LineHeight / 3.0f, Size / 2.0f, Size);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		}

		CTeeRenderInfo TeeInfo = GameClient()->m_aClients[pInfo->m_ClientId].m_RenderInfo;
		TeeInfo.m_Size *= TeeSizeMod;

		const CAnimState *pIdleState = CAnimState::GetIdle();
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
		vec2 TeeRenderPos(CenterX + x + 20.0f, CenterY + y + BoxMove + LineHeight / 2.0f + OffsetToMid.y);

		RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos, TeeAlpha);

		float IconX = CenterX + x - TeeInfo.m_Size / 2.0f;
		const float IconY = CenterY + y + BoxMove + (LineHeight - FontSize) / 2.0f;
		const float IconSize = FontSize >= 10.0f ? FontSize - 2.0f : FontSize;
		if(IsFriend)
		{
			ColorRGBA FriendIconColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendHeartColor));
			FriendIconColor.a *= NameAlpha;
			Ui()->DrawQmIconAt(IconX, IconY, IconSize, EQmIcon::HEART, FontIcons::FONT_ICON_HEART, FriendIconColor);
			IconX += IconSize - 2.0f;
		}

		if(IsSameClan)
		{
			ColorRGBA TeamIconColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor));
			TeamIconColor.a *= NameAlpha;
			Ui()->DrawQmIconAt(IconX, IconY, IconSize, EQmIcon::USERS, FontIcons::FONT_ICON_USERS, TeamIconColor);
		}
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

		y += LineHeight;
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	// QmClient：按编号查找传送点的输入行（与远程一致，画在选择器内容之上、光标之下）。
	RenderTeleSearch(ScreenCenter, ContentAlpha, MousePressed);
	RenderTools()->RenderCursor(ScreenCenter + m_SelectorMouse, 48.0f, ContentAlpha);

	// QmClient：查看模式的底部控制条画在原生旁观面板之上（矮屏上两者可能重叠）
	RenderGhostControlBar();
}

// QmClient：查看模式底部控制条矩形（UI 坐标）。度量照 demo 播放器的控制条：
// 贴底、左边距 50、宽度上限 760、总高 70（进度条 15 + 按钮行 16 + 名字行 20 + 行距）
CUIRect CSpectator::GhostControlBarRect() const
{
	const CUIRect Screen = *Ui()->Screen();
	const float S = Screen.h / 1200.0f;
	const float LeftMargin = 50.0f * S;
	const float TotalHeight = 70.0f * S;
	const float BarW = minimum(760.0f * S, Screen.w - LeftMargin);
	return {LeftMargin, Screen.h - TotalHeight, BarW, TotalHeight};
}

// QmClient：查看模式底部播放控制条。布局参考 demo 播放器的控制条：视角切换、
// 进度定位、秒级/tick 级快进快退、倍速与方向指示；交互走原生 CUI 管线
// （DoButtonLogic + 原生 UI 光标），与记分板内嵌控件同一套实现。
// 成员清单由原生旁观选择器的右侧面板负责，这里不再重复画一份。
void CSpectator::RenderGhostControlBar()
{
	// 只在查看模式下出现，且由 ESC 控制显隐（与 demo 播放一致）；
	// demo 回放有自己的播放器 UI
	if(!m_GhostPanelOpen || !GameClient()->m_RankGhost.IsViewModeActive() || Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	CRankGhost *pRankGhost = &GameClient()->m_RankGhost;
	const CUIRect Screen = *Ui()->Screen();
	// S：把 1200 高的设计高度换算到 UI 坐标，保持视觉比例
	const float S = Screen.h / 1200.0f;
	const CUIRect BarBase = GhostControlBarRect();
	// 与 demo 播放器一致：控制条可拖动，位移跨帧保留、越界钳回屏幕内；
	// 圆角按当前贴住哪几条边动态裁剪
	static vec2 s_BarOffset = vec2(0.0f, 0.0f);
	CUIRect BarRect = BarBase;
	BarRect.x += s_BarOffset.x;
	BarRect.y += s_BarOffset.y;
	int BarCorners = IGraphics::CORNER_NONE;
	if(BarRect.x > 0.0f && BarRect.y > 0.0f)
		BarCorners |= IGraphics::CORNER_TL;
	if(BarRect.x < Screen.w - BarRect.w && BarRect.y > 0.0f)
		BarCorners |= IGraphics::CORNER_TR;
	if(BarRect.x > 0.0f && BarRect.y < Screen.h - BarRect.h)
		BarCorners |= IGraphics::CORNER_BL;
	if(BarRect.x < Screen.w - BarRect.w && BarRect.y < Screen.h - BarRect.h)
		BarCorners |= IGraphics::CORNER_BR;

	Ui()->MapScreen();
	Ui()->StartCheck();
	Ui()->Update();

	// 底板与行距照 demo 播放器控制条：圆角卡片 + 进度条 15 / 按钮行 16 / 名字行 20
	BarRect.Draw(ui_token::color::SURFACE_ELEVATED, BarCorners, ui_token::radius::CARD);

	const float RowMargin = 5.0f * S;
	CUIRect Body = BarRect;
	Body.Margin(RowMargin, &Body);
	CUIRect SeekRect, ButtonRow, NameRow;
	Body.HSplitTop(15.0f * S, &SeekRect, &ButtonRow);
	ButtonRow.HSplitTop(RowMargin, nullptr, &ButtonRow);
	ButtonRow.HSplitBottom(20.0f * S, &ButtonRow, &NameRow);
	NameRow.HSplitTop(4.0f * S, nullptr, &NameRow);

	// 只有鼠标真的归 UI 光标时才响应点击（选择器打开时鼠标归它，这里不能抢）
	const bool Interact = GhostUiCursorActive();

	// 拖动控制条：与 demo 播放器同一套 DoDraggableButtonLogic 逻辑——按住空白处
	// 拖动，位移超过 5px 才算拖动（轻点仍落到具体控件上，子控件后处理覆盖热项）
	{
		enum EDragOperation
		{
			OP_NONE,
			OP_DRAGGING,
			OP_CLICKED
		};
		static EDragOperation s_Operation = OP_NONE;
		static vec2 s_InitialMouse = vec2(0.0f, 0.0f);
		if(!Interact)
			s_Operation = OP_NONE;
		bool Clicked;
		bool Abrupted;
		if(int Result = Ui()->DoDraggableButtonLogic(&s_Operation, 8, &BarRect, &Clicked, &Abrupted))
		{
			if(s_Operation == OP_NONE && Result == 1)
			{
				s_InitialMouse = Ui()->MousePos();
				s_Operation = OP_CLICKED;
			}
			if(Clicked || Abrupted)
				s_Operation = OP_NONE;
			if(s_Operation == OP_CLICKED && length(Ui()->MousePos() - s_InitialMouse) > 5.0f)
			{
				s_Operation = OP_DRAGGING;
				s_InitialMouse -= s_BarOffset;
			}
			if(s_Operation == OP_DRAGGING)
			{
				s_BarOffset = Ui()->MousePos() - s_InitialMouse;
				s_BarOffset.x = std::clamp(s_BarOffset.x, -BarBase.x, Screen.w - BarRect.w - BarBase.x);
				s_BarOffset.y = std::clamp(s_BarOffset.y, -BarBase.y, Screen.h - BarRect.h - BarBase.y);
			}
		}
	}
	const auto DoBarButton = [&](CButtonContainer *pButton, int Flags, const CUIRect *pRect, int ButtonFlags) {
		return Interact && Ui()->DoButtonLogic(pButton, Flags, pRect, ButtonFlags);
	};

	// 文字按钮底色与 demo 播放器控制条同一套 token（SURFACE_OVERLAY / ACCENT_PRIMARY_DIM）
	const auto DrawTextButton = [&](CButtonContainer *pButton, const CUIRect &Rect, const char *pText, bool Active) {
		const bool Hovered = Ui()->HotItem() == pButton;
		Rect.Draw(Active ? ui_token::color::ACCENT_PRIMARY_DIM : ui_token::color::SURFACE_OVERLAY.WithMultipliedAlpha(Hovered ? 1.8f : 1.15f), IGraphics::CORNER_ALL, ui_token::radius::TIGHT);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, Active || Hovered ? 1.0f : 0.85f);
		Ui()->DoLabel(&Rect, pText, Rect.h * 0.62f, TEXTALIGN_MC);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	};

	CRankGhost::SViewState State;
	if(!pRankGhost->GetViewState(State))
	{
		RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);
		Ui()->FinishCheck();
		return;
	}

	// 进度条：与 demo 播放器同一套画法——底槽 + 已播填充 + 居中「当前 / 总」时间；
	// 拖动时先暂停、松手还原原来的播放状态（与 demo 播放器拖进度条的语义一致）
	{
		const float Rounding = 5.0f * S;
		SeekRect.Draw(ui_token::color::SURFACE_OVERLAY.WithMultipliedAlpha(1.15f), IGraphics::CORNER_ALL, Rounding);
		CUIRect FilledBar = SeekRect;
		FilledBar.w = 2.0f * Rounding + (FilledBar.w - 2.0f * Rounding) * std::clamp(State.m_Progress, 0.0f, 1.0f);
		FilledBar.Draw(ui_token::color::ACCENT_PRIMARY_DIM.WithMultipliedAlpha(1.9f), IGraphics::CORNER_ALL, Rounding);

		char aCurTime[32];
		str_time((int64_t)(State.m_CurSeconds * 100.0f), TIME_HOURS, aCurTime, sizeof(aCurTime));
		char aTotalTime[32];
		str_time((int64_t)(State.m_TotalSeconds * 100.0f), TIME_HOURS, aTotalTime, sizeof(aTotalTime));
		char aSeekLabel[128];
		str_format(aSeekLabel, sizeof(aSeekLabel), "%s / %s", aCurTime, aTotalTime);
		Ui()->DoLabel(&SeekRect, aSeekLabel, SeekRect.h * 0.70f, TEXTALIGN_MC);

		static CButtonContainer s_SeekBarId;
		static bool s_PausedBeforeSeeking = false;
		if(Ui()->CheckActiveItem(&s_SeekBarId))
		{
			if(!Ui()->MouseButton(0) || !Interact)
			{
				if(s_PausedBeforeSeeking)
					pRankGhost->ViewPlayPause();
				s_PausedBeforeSeeking = false;
				Ui()->SetActiveItem(nullptr);
			}
			else
			{
				pRankGhost->ViewSeek(std::clamp((Ui()->MouseX() - SeekRect.x - Rounding) / (SeekRect.w - 2.0f * Rounding), 0.0f, 1.0f));
			}
		}
		else if(Interact && Ui()->HotItem() == &s_SeekBarId && Ui()->MouseButton(0))
		{
			s_PausedBeforeSeeking = State.m_Playing;
			if(State.m_Playing)
				pRankGhost->ViewPlayPause();
			Ui()->SetActiveItem(&s_SeekBarId);
			pRankGhost->ViewSeek(std::clamp((Ui()->MouseX() - SeekRect.x - Rounding) / (SeekRect.w - 2.0f * Rounding), 0.0f, 1.0f));
		}
		if(Interact && Ui()->MouseInside(&SeekRect) && !Ui()->MouseButton(0))
			Ui()->SetHotItem(&s_SeekBarId);
	}

	// 按钮行：顺序与 demo 播放器控制条一致——播放/暂停 · 停止 · 秒级跳转（中间夹跳转时长）
	// · 逐 tick · 倍速；最右侧是查看模式特有的三种视角模式与方向指示（demo 播放器没有这两项）
	{
		const float Btn = ButtonRow.h;
		const float Gap = 5.0f * S;
		const float GroupGap = 15.0f * S;
		const CUIRect RowFull = ButtonRow;

		// 右侧控件（方向 + 三种视角模式）先按文字实际宽度预留，左侧按钮只排进
		// 剩余区域——固定宽度在长本地化文案下会溢出压到相邻按钮
		const char *apModeLabels[3] = {Localize("Follow"), Localize("Free-View"), Localize("Multi-View")};
		const CRankGhost::EViewCameraMode apModes[3] = {
			CRankGhost::EViewCameraMode::MEMBER,
			CRankGhost::EViewCameraMode::FREE,
			CRankGhost::EViewCameraMode::ALL_MEMBERS};
		const float ModeFont = Btn * 0.62f;
		const auto TextPaddedWidth = [&](const char *pText) {
			return TextRender()->TextWidth(ModeFont, pText, -1) + 14.0f * S;
		};
		CUIRect Right = RowFull;
		CUIRect DirectionRect;
		Right.VSplitRight(Btn, &Right, &DirectionRect);
		Right.VSplitRight(Gap, &Right, nullptr);
		CUIRect aModeRects[3];
		for(int i = 2; i >= 0; i--)
		{
			Right.VSplitRight(TextPaddedWidth(apModeLabels[i]), &Right, &aModeRects[i]);
			Right.VSplitRight(Gap, &Right, nullptr);
		}
		CUIRect Rest = RowFull;
		Rest.VSplitRight(RowFull.w - Right.w, &Rest, nullptr);
		CUIRect Button;

		static CButtonContainer s_PlayPauseButton;
		Rest.VSplitLeft(Btn, &Button, &Rest);
		if(Ui()->DoButton_QmIcon(&s_PlayPauseButton, State.m_Playing ? EQmIcon::PAUSE : EQmIcon::PLAY, State.m_Playing ? FontIcons::FONT_ICON_PAUSE : FontIcons::FONT_ICON_PLAY, 0, &Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Interact))
			pRankGhost->ViewPlayPause();

		static CButtonContainer s_StopButton;
		Rest.VSplitLeft(Gap, nullptr, &Rest);
		Rest.VSplitLeft(Btn, &Button, &Rest);
		if(Ui()->DoButton_QmIcon(&s_StopButton, EQmIcon::STOP, FontIcons::FONT_ICON_STOP, 0, &Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Interact))
		{
			if(State.m_Playing)
				pRankGhost->ViewPlayPause();
			pRankGhost->ViewSeek(0.0f);
		}

		// 秒级跳转：跳转时长在中间（demo 播放器是时长菜单，这里用同一位置的紧凑按钮循环 1s/5s/30s）
		static float s_SkipSeconds = 5.0f;
		const auto SeekBySeconds = [&](float Seconds) {
			const int TicksPerSecond = Client()->GameTickSpeed();
			pRankGhost->ViewSeek(std::clamp((GameClient()->m_Ghost.ManualPlaybackTick() + Seconds * TicksPerSecond) / (float)maximum(1, GameClient()->m_Ghost.ManualEndTick()), 0.0f, 1.0f));
		};

		static CButtonContainer s_SkipBackButton;
		Rest.VSplitLeft(GroupGap, nullptr, &Rest);
		Rest.VSplitLeft(Btn, &Button, &Rest);
		if(Ui()->DoButton_QmIcon(&s_SkipBackButton, EQmIcon::BACKWARD, FontIcons::FONT_ICON_BACKWARD, 0, &Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Interact))
			SeekBySeconds(-s_SkipSeconds);

		static CButtonContainer s_SkipDurationButton;
		Rest.VSplitLeft(Gap, nullptr, &Rest);
		{
			// 时长控件文案与 demo 播放器一致（"%s sec." 的既有翻译键），宽度按文字实测
			char aSeconds[8];
			str_format(aSeconds, sizeof(aSeconds), "%d", (int)s_SkipSeconds);
			char aDurBuf[16];
			str_format(aDurBuf, sizeof(aDurBuf), Localize("%s sec.", "Demo player duration"), aSeconds);
			Rest.VSplitLeft(maximum(TextPaddedWidth(aDurBuf), 46.0f * S), &Button, &Rest);
			const bool Clicked = DoBarButton(&s_SkipDurationButton, 0, &Button, BUTTONFLAG_LEFT) != 0;
			DrawTextButton(&s_SkipDurationButton, Button, aDurBuf, false);
			if(Clicked)
				s_SkipSeconds = s_SkipSeconds < 2.0f ? 5.0f : (s_SkipSeconds < 10.0f ? 30.0f : 1.0f);
		}

		static CButtonContainer s_SkipForwardButton;
		Rest.VSplitLeft(Gap, nullptr, &Rest);
		Rest.VSplitLeft(Btn, &Button, &Rest);
		if(Ui()->DoButton_QmIcon(&s_SkipForwardButton, EQmIcon::FORWARD, FontIcons::FONT_ICON_FORWARD, 0, &Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Interact))
			SeekBySeconds(s_SkipSeconds);

		// 逐 tick 微调
		const auto NudgeTick = [&](int Delta) {
			pRankGhost->ViewSeek(std::clamp((GameClient()->m_Ghost.ManualPlaybackTick() + Delta) / (float)maximum(1, GameClient()->m_Ghost.ManualEndTick()), 0.0f, 1.0f));
		};
		static CButtonContainer s_TickBackButton, s_TickForwardButton;
		Rest.VSplitLeft(GroupGap, nullptr, &Rest);
		Rest.VSplitLeft(Btn, &Button, &Rest);
		if(Ui()->DoButton_QmIcon(&s_TickBackButton, EQmIcon::BACKWARD_STEP, FontIcons::FONT_ICON_BACKWARD_STEP, 0, &Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Interact))
			NudgeTick(-1);
		Rest.VSplitLeft(Gap, nullptr, &Rest);
		Rest.VSplitLeft(Btn, &Button, &Rest);
		if(Ui()->DoButton_QmIcon(&s_TickForwardButton, EQmIcon::FORWARD_STEP, FontIcons::FONT_ICON_FORWARD_STEP, 0, &Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Interact))
			NudgeTick(1);

		// 倍速：与 demo 播放器一样是「减速 / 读数 / 加速」三件套
		static constexpr float s_aSpeeds[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
		const int NumSpeeds = (int)std::size(s_aSpeeds);
		const int SpeedIndex = [&]() {
			for(int i = 0; i < NumSpeeds; i++)
			{
				if(GameClient()->m_Ghost.ManualSpeed() == s_aSpeeds[i])
					return i;
			}
			return 2; // 1.0x
		}();
		static CButtonContainer s_SlowDownButton, s_SpeedUpButton;
		Rest.VSplitLeft(GroupGap, nullptr, &Rest);
		Rest.VSplitLeft(Btn, &Button, &Rest);
		if(Ui()->DoButton_QmIcon(&s_SlowDownButton, EQmIcon::CHEVRON_DOWN, FontIcons::FONT_ICON_CHEVRON_DOWN, 0, &Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Interact))
			GameClient()->m_Ghost.ManualSetSpeed(s_aSpeeds[maximum(0, SpeedIndex - 1)]);
		CUIRect SpeedRect;
		Rest.VSplitLeft(Gap, nullptr, &Rest);
		Rest.VSplitLeft(50.0f * S, &SpeedRect, &Rest);
		{
			char aSpeedBuf[16];
			str_format(aSpeedBuf, sizeof(aSpeedBuf), "×%g", (double)GameClient()->m_Ghost.ManualSpeed());
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.85f);
			Ui()->DoLabel(&SpeedRect, aSpeedBuf, SpeedRect.h * 0.85f, TEXTALIGN_MC);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		Rest.VSplitLeft(Gap, nullptr, &Rest);
		Rest.VSplitLeft(Btn, &Button, &Rest);
		if(Ui()->DoButton_QmIcon(&s_SpeedUpButton, EQmIcon::CHEVRON_UP, FontIcons::FONT_ICON_CHEVRON_UP, 0, &Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Interact))
			GameClient()->m_Ghost.ManualSetSpeed(s_aSpeeds[minimum(NumSpeeds - 1, SpeedIndex + 1)]);

		// 右侧：方向指示 + 三种视角模式（矩形已在上方预留，视觉顺序 跟随 / 自由视角 / 多人同框视角 / 方向）
		static CButtonContainer s_DirectionButton;
		{
			const bool DirOn = g_Config.m_QmRankGhostShowDirection != 0;
			const bool Clicked = DoBarButton(&s_DirectionButton, 0, &DirectionRect, BUTTONFLAG_LEFT) != 0;
			DrawTextButton(&s_DirectionButton, DirectionRect, Localize("Direction", "Rank 1 replay"), DirOn);
			if(Clicked)
				g_Config.m_QmRankGhostShowDirection ^= 1;
		}

		static CButtonContainer s_aModeButtons[3];
		const CRankGhost::EViewCameraMode CurrentMode = pRankGhost->ViewCameraMode();
		for(int i = 0; i < 3; i++)
		{
			const bool Clicked = DoBarButton(&s_aModeButtons[i], 0, &aModeRects[i], BUTTONFLAG_LEFT) != 0;
			DrawTextButton(&s_aModeButtons[i], aModeRects[i], apModeLabels[i], CurrentMode == apModes[i]);
			if(Clicked && CurrentMode != apModes[i])
				pRankGhost->ViewSetCameraMode(apModes[i]);
		}
	}

	// 名字行：回放对象 · 地图 · 当前跟随的成员（demo 播放器这里是 Demofile: <name>）
	{
		char aMember[64];
		if(!pRankGhost->ViewMemberName(pRankGhost->ViewSelectedMember(), aMember, sizeof(aMember)))
			aMember[0] = '\0';
		char aNameBuf[256];
		if(aMember[0] != '\0')
			str_format(aNameBuf, sizeof(aNameBuf), "%s · %s · %s", Localize("Rank 1 replay"), Client()->GetCurrentMap(), aMember);
		else
			str_format(aNameBuf, sizeof(aNameBuf), "%s · %s", Localize("Rank 1 replay"), Client()->GetCurrentMap());
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.9f);
		Ui()->DoLabel(&NameRow, aNameBuf, NameRow.h * 0.85f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	// 原生光标（与记分板解锁后的光标一致）
	RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);
	Ui()->FinishCheck();
}

void CSpectator::FindTele()
{
	if(!m_Active || (!GameClient()->m_Snap.m_SpecInfo.m_Active && Client()->State() != IClient::STATE_DEMOPLAYBACK))
		return;

	m_TeleSearchPending = false;
	const int Number = qm_spectator_tele::ParseNumber(m_TeleNumberInput.GetString());
	if(Number == 0)
	{
		m_TeleSearchStatus = ETeleSearchStatus::INVALID_NUMBER;
		return;
	}
	const int Width = Collision()->GetWidth();
	// 同编号重复查找时从上次位置继续，从而在多个同编号传送点之间循环。
	const int Index = qm_spectator_tele::FindNext(Collision()->TeleLayer(), Width, Collision()->GetHeight(), Number, Number == m_LastTeleNumber ? m_LastTeleIndex : -1);
	if(Index == -1)
	{
		m_TeleSearchStatus = ETeleSearchStatus::NOT_FOUND;
		return;
	}

	GameClient()->m_MultiViewActivated = false;
	m_MultiViewActivateDelay = 0.0f;
	Spectate(SPEC_FREEVIEW);
	m_SelectedSpectatorId = NO_SELECTION;
	m_LastTeleNumber = Number;
	m_LastTeleIndex = Index;
	m_TeleSearchStatus = ETeleSearchStatus::FOUND;
	m_TeleSearchPosition = vec2((Index % Width) * 32.0f + 16.0f, (Index / Width) * 32.0f + 16.0f);
	m_TeleSearchPending = true;
}

void CSpectator::RenderTeleSearch(vec2 Center, float Alpha, bool MousePressed)
{
	CUIRect Row = {Center.x - 280.0f, Center.y + 310.0f, 560.0f, 40.0f};
	CUIRect Label, Minus, Number, Plus, Find;
	Row.VSplitLeft(140.0f, &Label, &Row);
	Row.VSplitLeft(40.0f, &Minus, &Row);
	Row.VSplitLeft(8.0f, nullptr, &Row);
	Row.VSplitLeft(80.0f, &Number, &Row);
	Row.VSplitLeft(8.0f, nullptr, &Row);
	Row.VSplitLeft(40.0f, &Plus, &Row);
	Row.VSplitLeft(12.0f, nullptr, &Find);
	const vec2 Mouse = Center + m_SelectorMouse;

	const auto Button = [&](const CUIRect &Rect, const char *pText) {
		const bool Hovered = m_Active && Rect.Inside(Mouse);
		Rect.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, (Hovered ? 0.25f : 0.1f) * Alpha), IGraphics::CORNER_ALL, 8.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, (Hovered ? 1.0f : 0.8f) * Alpha);
		Ui()->DoLabel(&Rect, pText, 20.0f, TEXTALIGN_MC);
		return Hovered && MousePressed;
	};
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.8f * Alpha);
	Ui()->DoLabel(&Label, Localize("Find CP"), 20.0f, TEXTALIGN_ML);
	const bool Decrease = Button(Minus, "-");
	const bool Increase = Button(Plus, "+");
	if(Decrease || Increase)
	{
		char aNumber[4];
		str_format(aNumber, sizeof(aNumber), "%d", qm_spectator_tele::StepNumber(qm_spectator_tele::ParseNumber(m_TeleNumberInput.GetString()), Decrease ? -1 : 1));
		m_TeleNumberInput.Set(aNumber);
	}
	if(MousePressed)
	{
		if(Number.Inside(Mouse))
		{
			m_TeleNumberInput.Activate(EInputPriority::UI);
			if(m_TeleNumberInput.IsActive())
			{
				// 与共享 UI 的输入焦点同步，避免 Demo 控件更新时释放此输入框。
				Ui()->SetActiveItem(&m_TeleNumberInput);
				Ui()->SetActiveItem(nullptr);
			}
			m_TeleNumberInput.SelectAll();
			m_IgnoreTeleNumberTextEvent = false;
		}
		else
			m_TeleNumberInput.Deactivate();
	}
	const bool Changed = m_TeleNumberInput.WasChanged();
	const bool CursorChanged = m_TeleNumberInput.WasCursorChanged();
	if(Changed)
	{
		m_TeleSearchStatus = ETeleSearchStatus::IDLE;
		m_LastTeleNumber = 0;
	}
	Number.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, (m_TeleNumberInput.IsActive() ? 0.25f : 0.1f) * Alpha), IGraphics::CORNER_ALL, 8.0f);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
	m_TeleNumberInput.Render(&Number, 20.0f, TEXTALIGN_MC, Changed || CursorChanged, -1.0f, 0.0f);
	if(Button(Find, Localize("Find / Next")))
		FindTele();

	const char *pStatus = Localize("Enter a CP number from 1 to 255.");
	if(m_TeleSearchStatus == ETeleSearchStatus::FOUND)
		pStatus = Localize("Click again to find the next location.");
	else if(m_TeleSearchStatus == ETeleSearchStatus::NOT_FOUND)
		pStatus = Localize("There is no teleporter with that index on the map.");
	const bool Error = m_TeleSearchStatus == ETeleSearchStatus::INVALID_NUMBER || m_TeleSearchStatus == ETeleSearchStatus::NOT_FOUND;
	TextRender()->TextColor(Error ? ColorRGBA(1.0f, 0.5f, 0.5f, Alpha) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.6f * Alpha));
	const CUIRect Status = {Center.x - 280.0f, Center.y + 360.0f, 560.0f, 20.0f};
	Ui()->DoLabel(&Status, pStatus, 16.0f, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CSpectator::OnReset()
{
	m_WasActive = false;
	m_Active = false;
	m_PresentationInitialized = false;
	m_SelectedSpectatorId = NO_SELECTION;
	m_MultiViewActivateDelay = 0.0f;
	m_TouchState = {};
	// 查看模式控制面板随状态一并收起
	m_GhostPanelOpen = false;
	m_GhostEscapeArmed = false;
	m_GhostEscapeLastTime = -1.0f;
	m_GhostPanelOpenBeforeEscape = false;
	m_TeleNumberInput.Deactivate();
	m_TeleNumberInput.Set("1");
	m_IgnoreTeleNumberTextEvent = false;
	m_TeleSearchStatus = ETeleSearchStatus::IDLE;
	m_LastTeleNumber = 0;
	m_LastTeleIndex = -1;
	m_TeleSearchPending = false;
	m_TeleSearchPosition = vec2(0.0f, 0.0f);
}

void CSpectator::Spectate(int SpectatorId)
{
	if(SpectatorId != SPEC_FREEVIEW)
		m_TeleSearchPending = false;
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		GameClient()->m_DemoSpecId = std::clamp(SpectatorId, (int)SPEC_FOLLOW, MAX_CLIENTS - 1);
		// The tick must be rendered for the spectator mode to be updated, so we do it manually when demo playback is paused
		// TODO: https://github.com/ddnet/ddnet/issues/11681
		if(DemoPlayer()->BaseInfo()->m_Paused)
			GameClient()->m_Menus.DemoSeekTick(IDemoPlayer::TICK_CURRENT);
		return;
	}

	if(GameClient()->m_FastPractice.ConsumeSpectatorCommand())
		return;

	if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SpectatorId)
		return;

	if(Client()->IsSixup())
	{
		protocol7::CNetMsg_Cl_SetSpectatorMode Msg;
		if(SpectatorId == SPEC_FREEVIEW)
		{
			Msg.m_SpecMode = protocol7::SPEC_FREEVIEW;
			Msg.m_SpectatorId = -1;
		}
		else
		{
			Msg.m_SpecMode = protocol7::SPEC_PLAYER;
			Msg.m_SpectatorId = SpectatorId;
		}
		Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL, true);
		return;
	}
	CNetMsg_Cl_SetSpectatorMode Msg;
	Msg.m_SpectatorId = SpectatorId;
	Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);
}

void CSpectator::SpectateClosest()
{
	if(!CanChangeSpectatorId())
		return;

	const CGameClient::CSnapState &Snap = GameClient()->m_Snap;
	int SpectatorId = Snap.m_SpecInfo.m_SpectatorId;

	int NewSpectatorId = -1;

	vec2 CurPosition = GameClient()->m_Camera.m_Center;
	if(SpectatorId != SPEC_FREEVIEW)
	{
		const CNetObj_Character &CurCharacter = Snap.m_aCharacters[SpectatorId].m_Cur;
		CurPosition = vec2(CurCharacter.m_X, CurCharacter.m_Y);
	}

	int ClosestDistance = std::numeric_limits<int>::max();
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(ClientId == SpectatorId || !Snap.m_aCharacters[ClientId].m_Active || !Snap.m_apPlayerInfos[ClientId] || Snap.m_apPlayerInfos[ClientId]->m_Team == TEAM_SPECTATORS)
			continue;

		if(Client()->State() != IClient::STATE_DEMOPLAYBACK && ClientId == Snap.m_LocalClientId)
			continue;

		const CNetObj_Character &MaybeClosestCharacter = Snap.m_aCharacters[ClientId].m_Cur;
		int Distance = distance(CurPosition, vec2(MaybeClosestCharacter.m_X, MaybeClosestCharacter.m_Y));
		if(NewSpectatorId == -1 || Distance < ClosestDistance)
		{
			NewSpectatorId = ClientId;
			ClosestDistance = Distance;
		}
	}
	if(NewSpectatorId > -1)
		Spectate(NewSpectatorId);
}
