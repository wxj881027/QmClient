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
#include <game/client/components/qmclient/friend_heart_icon.h>
#include <game/client/components/qmclient/spectator_friend_priority.h>
#include <game/client/components/qmclient/spectator_selector_layout.h>
#include <game/client/components/qmclient/spectator_tele_search.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	void DrawSolidFriendHeart(const CUi *pUi, float X, float Y, float Size, const ColorRGBA &Color)
	{
		ITextRender *pTextRender = pUi->TextRender();
		const ColorRGBA PreviousColor = pTextRender->GetTextColor();
		const unsigned PreviousFlags = pTextRender->GetRenderFlags();
		const EFontPreset PreviousPreset = pTextRender->GetFontPreset();

		CUIRect Rect;
		Rect.x = X;
		Rect.y = Y;
		Rect.w = Size;
		Rect.h = Size;

		pTextRender->TextColor(Color);
		pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
		pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH |
					    ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING |
					    ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
		pUi->DoLabel(&Rect, QM_FRIEND_HEART_ICON, Size, TEXTALIGN_MC);

		pTextRender->SetRenderFlags(PreviousFlags);
		pTextRender->SetFontPreset(PreviousPreset);
		pTextRender->TextColor(PreviousColor);
	}

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
	if(GameClient()->m_RankGhost.IsViewModeActive())
		return true;
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
	if(GameClient()->m_RankGhost.IsViewModeActive())
	{
		const int Count = GameClient()->m_RankGhost.ViewMemberCount();
		if(Count > 0)
		{
			const int Current = GameClient()->m_RankGhost.ViewCameraMode() == CRankGhost::EViewCameraMode::MEMBER ? GameClient()->m_RankGhost.ViewSelectedMember() : Reverse ? 0 :
																							   -1;
			Spectate((Current + Count + (Reverse ? -1 : 1)) % Count);
		}
		return;
	}
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
	if(pSelf->GameClient()->m_RankGhost.IsViewModeActive())
	{
		if(Input >= 0)
			pSelf->GameClient()->m_RankGhost.ViewToggleMultiMember(Input);
		return;
	}

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
bool CSpectator::PlaybackControlsActive() const
{
	if(!GameClient()->m_RankGhost.IsViewModeActive() || Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return false;
	if(GameClient()->m_Menus.IsActive() || GameClient()->m_GameConsole.IsActive() || GameClient()->m_HudEditor.IsActive())
		return false;
	// 原生选择器打开时归它（它的玩家列表、视角按钮与光标都只读 m_SelectorMouse）
	return !m_Active && !GameClient()->m_Chat.IsActive() && m_ReplayUi.PanelOpen();
}

bool CSpectator::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	const bool ViewMode = GameClient()->m_RankGhost.IsViewModeActive();
	const bool GhostFreeCamera = GhostFreeCameraCanPan();
	if(ViewMode && (GameClient()->m_Menus.IsActive() || GameClient()->m_GameConsole.IsActive() || GameClient()->m_Chat.IsActive()))
		return false;
	if(!m_Active && !GhostFreeCamera && !PlaybackControlsActive() && !(ViewMode && GameClient()->m_Menus.OnlineReplayPopupActive()))
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
		if(PlaybackControlsActive() || GameClient()->m_Menus.OnlineReplayPopupActive())
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

	// 截图等组合键继续交给绑定系统，不被 CP 输入框吞掉；普通 Ctrl+C 仍可复制输入内容。
	if((Event.m_Flags & (IInput::FLAG_PRESS | IInput::FLAG_RELEASE)) && CBinds::IsReservedShortcutChord(CBinds::GetModifierMask(Input())))
		return false;

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

	// 控制层与菜单分别处理 Esc，播放会话保持不变。
	if(Event.m_Key == KEY_ESCAPE && (Event.m_Flags & IInput::FLAG_RELEASE))
		m_ReplayUi.ReleaseEscape();
	if(GameClient()->m_RankGhost.IsViewModeActive() && !GameClient()->m_GameConsole.IsActive() && !GameClient()->m_Chat.IsActive() && !GameClient()->m_Menus.IsActive() && !Ui()->IsPopupOpen() && !GameClient()->m_Menus.OnlineReplayPopupActive())
	{
		if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE)
			return m_ReplayUi.PressEscape(Client()->LocalTime()) != COnlineReplayUiState::EAction::OPEN_MENU;
	}
	else
		m_ReplayUi.CancelEscape();

	if(!GameClient()->m_RankGhost.IsViewModeActive() && g_Config.m_ClSpectatorMouseclicks)
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

	if(!GameClient()->m_RankGhost.IsViewModeActive() && GameClient()->m_Camera.SpectatingPlayer() && GameClient()->m_Camera.CanUseAutoSpecCamera())
	{
		if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_MOUSE_2)
		{
			GameClient()->m_Camera.ResetAutoSpecCamera();
			return true;
		}
	}

	// 自由视角的旁观绑定优先；共用播放 HUD 同帧跳过此键，避免同时跳转时间轴。
	if((Event.m_Flags & IInput::FLAG_PRESS) && GameClient()->m_Menus.ClaimOnlineReplaySpectatorBind(Event.m_Key))
		return false;

	// QmClient：查看模式下鼠标归 UI 光标时吞掉鼠标键，点击控制条不再误触发射击/钩爪。
	// 鼠标事件交给共用播放 HUD，滚轮保持普通 demo 的倍速语义。
	if(PlaybackControlsActive() || (GameClient()->m_RankGhost.IsViewModeActive() && GameClient()->m_Menus.OnlineReplayPopupActive()))
	{
		const bool Consumed = Ui()->OnInput(Event);
		if(Consumed || (Event.m_Key >= KEY_MOUSE_1 && Event.m_Key <= KEY_MOUSE_WHEEL_DOWN))
			return true;
	}

	// 共用播放快捷键先取得输入，避免同一方向键同时触发旁观绑定。
	if(GameClient()->m_RankGhost.IsViewModeActive() && GameClient()->m_Menus.PlaybackShortcutsActive())
	{
		switch(Event.m_Key)
		{
		case KEY_P:
		case KEY_UP:
		case KEY_DOWN:
		case KEY_SPACE:
		case KEY_RETURN:
		case KEY_KP_ENTER:
		case KEY_K:
		case KEY_LEFT:
		case KEY_RIGHT:
		case KEY_J:
		case KEY_L:
		case KEY_HOME:
		case KEY_END:
		case KEY_PERIOD:
		case KEY_COMMA:
		case KEY_0:
		case KEY_1:
		case KEY_2:
		case KEY_3:
		case KEY_4:
		case KEY_5:
		case KEY_6:
		case KEY_7:
		case KEY_8:
		case KEY_9:
			return true;
		case KEY_C:
			return PlaybackControlsActive();
		default:
			break;
		}
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

	const bool ViewModeActive = GameClient()->m_RankGhost.IsViewModeActive();
	if(ViewModeActive && (GameClient()->m_Menus.IsActive() || GameClient()->m_GameConsole.IsActive() || GameClient()->m_Chat.IsActive() || GameClient()->m_Menus.OnlineReplayPopupActive()))
	{
		m_Active = false;
		m_SelectedSpectatorId = NO_SELECTION;
		m_TeleNumberInput.Deactivate();
	}
	const auto SelectedId = [&]() {
		if(ViewModeActive)
		{
			using EMode = CRankGhost::EViewCameraMode;
			const EMode Mode = GameClient()->m_RankGhost.ViewCameraMode();
			return Mode == EMode::FREE ? (int)SPEC_FREEVIEW : Mode == EMode::ALL_MEMBERS ? (int)MULTI_VIEW :
												       GameClient()->m_RankGhost.ViewSelectedMember();
		}
		return Client()->State() == IClient::STATE_DEMOPLAYBACK ? GameClient()->m_DemoSpecId : GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
	};
	const auto MultiViewActive = [&]() { return ViewModeActive ? SelectedId() == MULTI_VIEW : GameClient()->m_MultiViewActivated; };

	// QmClient：等自由视角状态真正生效后再设置位置，避免被跟随镜头覆盖查找目标。
	if(!ViewModeActive && m_TeleSearchPending)
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

	if(!ViewModeActive && !GameClient()->m_MultiViewActivated && m_MultiViewActivateDelay != 0.0f)
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
			if(ViewModeActive && m_SelectedSpectatorId != NO_SELECTION)
			{
				if(!MultiViewActive() || m_SelectedSpectatorId < 0)
					Spectate(m_SelectedSpectatorId);
			}
			else if(m_SelectedSpectatorId != NO_SELECTION)
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
			// 选择器收起；播放 HUD 由菜单组件统一绘制。
			return;
		}
	}

	if(!GameClient()->m_Snap.m_SpecInfo.m_Active && Client()->State() != IClient::STATE_DEMOPLAYBACK && !ViewModeActive)
	{
		m_Active = false;
		m_WasActive = false;
		m_TeleNumberInput.Deactivate();
		if(!ExtraAnimations)
		{
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
			return;
		}
	}

	if(WantActive)
	{
		m_WasActive = true;
		m_SelectedSpectatorId = NO_SELECTION;
	}

	// 成员描述与服务器快照分离；同一旁观布局读取不同来源。
	struct SDisplayPlayer
	{
		int m_Id;
		int m_DDTeam;
		bool m_Friend;
	};
	SDisplayPlayer aDisplayPlayers[CGhost::MAX_ACTIVE_GHOSTS];
	bool aIsFriend[CGhost::MAX_ACTIVE_GHOSTS];
	int aDisplayOrder[CGhost::MAX_ACTIVE_GHOSTS];
	int DisplayCount = 0;
	if(ViewModeActive)
	{
		DisplayCount = minimum(GameClient()->m_RankGhost.ViewMemberCount(), (int)CGhost::MAX_ACTIVE_GHOSTS);
		for(int i = 0; i < DisplayCount; ++i)
		{
			aDisplayPlayers[i] = {i, TEAM_FLOCK, false};
			aIsFriend[i] = false;
		}
	}
	else
		for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByDDTeamName)
		{
			if(!pInfo || pInfo->m_Team == TEAM_SPECTATORS)
				continue;
			const int Id = pInfo->m_ClientId;
			aDisplayPlayers[DisplayCount] = {Id, GameClient()->m_Teams.Team(Id), GameClient()->m_aClients[Id].m_Friend};
			aIsFriend[DisplayCount] = aDisplayPlayers[DisplayCount].m_Friend;
			++DisplayCount;
		}
	const int FriendCount = qm_spectator_friends::BuildFriendFirstOrder(aIsFriend, DisplayCount, aDisplayOrder, g_Config.m_QmSpectatorFriendsFirst != 0);

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
	int PerLine = 8;
	float BoxMove = -10.0f;
	float BoxOffset = 0.0f;

	if(DisplayCount > 128)
	{
		PerLine = (DisplayCount + 3) / 4;
		LineHeight = 500.0f / PerLine;
		FontSize = maximum(6.0f, LineHeight - 1.0f);
		TeeSizeMod = LineHeight / 60.0f;
		RoundRadius = 3.0f;
		BoxMove = 0.0f;
	}
	else if(DisplayCount > 96)
	{
		FontSize = 15.0f;
		LineHeight = 15.0f;
		TeeSizeMod = 0.3f;
		PerLine = 32;
		RoundRadius = 5.0f;
		BoxMove = 3.0f;
		BoxOffset = 6.0f;
	}
	else if(DisplayCount > 64)
	{
		FontSize = 16.0f;
		LineHeight = 19.0f;
		TeeSizeMod = 0.45f;
		PerLine = 24;
		RoundRadius = 6.0f;
		BoxMove = 3.0f;
		BoxOffset = 6.0f;
	}
	else if(DisplayCount > 32)
	{
		FontSize = 18.0f;
		LineHeight = 30.0f;
		TeeSizeMod = 0.7f;
		PerLine = 16;
		RoundRadius = 10.0f;
		BoxMove = 3.0f;
		BoxOffset = 6.0f;
	}
	if(DisplayCount > 16)
	{
		ObjWidth = 600.0f;
	}

	const vec2 ScreenSize = vec2(Width, Height);
	const float CenterX = Width / 2.0f;
	const float CenterY = Height / 2.0f + PanelOffsetY;
	const vec2 ScreenCenter = vec2(CenterX, CenterY);
	const float TitleHeight = std::clamp(LineHeight * 0.5f, 12.0f, 15.0f);
	const float TitleFontSize = TitleHeight * 0.8f;
	const float PlayersBottom = StartY + BoxMove + qm_spectator_friends::MaxColumnHeight(DisplayCount, FriendCount, PerLine, LineHeight, TitleHeight);
	const auto SelectorLayout = qm_spectator_layout::Build(ScreenCenter, ObjWidth, !ViewModeActive, PlayersBottom);
	const CUIRect &SpectatorRect = SelectorLayout.m_Panel;
	const CUIRect &SpectatorMouseRect = SelectorLayout.m_Mouse;

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
	m_SelectorMouse = qm_spectator_layout::ClampMouse(SelectorLayout, ScreenCenter, m_SelectorMouse);

	const bool MousePressed = WantActive && (Input()->KeyPress(KEY_MOUSE_1) || m_TouchState.m_PrimaryPressed);

	// draw selections
	if(SelectedId() == SPEC_FREEVIEW)
	{
		Graphics()->DrawRect(CenterX - (ObjWidth - 20.0f), CenterY - 280.0f, ((ObjWidth * 2.0f) / 3.0f) - 40.0f, 60.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * ContentAlpha), IGraphics::CORNER_ALL, 20.0f);
	}

	if(MultiViewActive())
	{
		Graphics()->DrawRect(CenterX - (ObjWidth - 20.0f) + (ObjWidth * 2.0f / 3.0f), CenterY - 280.0f, ((ObjWidth * 2.0f) / 3.0f) - 40.0f, 60.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * ContentAlpha), IGraphics::CORNER_ALL, 20.0f);
	}

	if((ViewModeActive || (Client()->State() == IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_LocalClientId >= 0)) && (ViewModeActive ? SelectedId() == 0 : GameClient()->m_DemoSpecId == SPEC_FOLLOW))
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
			if(!ViewModeActive)
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
			if(ViewModeActive)
				Spectate(MULTI_VIEW);
			else
				GameClient()->m_MultiViewActivated = true;
		}
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, (MultiViewSelected ? 1.0f : 0.5f) * ContentAlpha);
	TextRender()->Text(CenterX - (ObjWidth - 40.0f) + (ObjWidth * 2.0f / 3.0f), CenterY - 280.f + (60.f - BigFontSize) / 2.f, BigFontSize, Localize("Multi-View"), -1.0f);

	if((ViewModeActive || (Client()->State() == IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_LocalClientId >= 0)))
	{
		bool FollowSelected = false;
		if(WantActive && m_SelectorMouse.x >= -(ObjWidth - 20.0f) + (ObjWidth * 2.0f * 2.0f / 3.0f) && m_SelectorMouse.x <= -(ObjWidth - 20.0f) + (ObjWidth * 2.0f * 2.0f / 3.0f) + ((ObjWidth * 2.0f) / 3.0f) - 40.0f &&
			m_SelectorMouse.y >= -280.0f && m_SelectorMouse.y <= -220.0f)
		{
			m_SelectedSpectatorId = SPEC_FOLLOW;
			FollowSelected = true;
			if(MousePressed)
			{
				if(!ViewModeActive)
					GameClient()->m_MultiViewActivated = false;
				Spectate(m_SelectedSpectatorId);
			}
		}
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, (FollowSelected ? 1.0f : 0.5f) * ContentAlpha);
		TextRender()->Text(CenterX - (ObjWidth - 40.0f) + (ObjWidth * 2.0f * 2.0f / 3.0f), CenterY - 280.0f + (60.f - BigFontSize) / 2.f, BigFontSize, Localize("Follow"), -1.0f);
	}

	float x = -(ObjWidth - 35.0f), y = StartY;

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

		const SDisplayPlayer *pInfo = &aDisplayPlayers[aDisplayOrder[i]];
		const int DDTeam = pInfo->m_DDTeam;
		const bool StartsGroup = i % PerLine == 0 || (FriendCount > 0 && i == FriendCount);
		const bool EndsGroup = i + 1 == DisplayCount || (i + 1) % PerLine == 0 || (FriendCount > 0 && i + 1 == FriendCount);
		const int NextDDTeam = EndsGroup ? 0 : aDisplayPlayers[aDisplayOrder[i + 1]].m_DDTeam;

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

		if(SelectedId() == pInfo->m_Id)
		{
			Graphics()->DrawRect(CenterX + x - 10.0f + BoxOffset, CenterY + y + BoxMove, 270.0f - BoxOffset, LineHeight, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * ContentAlpha), IGraphics::CORNER_ALL, RoundRadius);
		}

		bool PlayerSelected = false;
		if(WantActive && m_SelectorMouse.x >= x - 10.0f && m_SelectorMouse.x < x + 260.0f &&
			m_SelectorMouse.y >= y - (LineHeight / 6.0f) && m_SelectorMouse.y < y + (LineHeight * 5.0f / 6.0f))
		{
			m_SelectedSpectatorId = pInfo->m_Id;
			PlayerSelected = true;
			if(MousePressed)
			{
				if(ViewModeActive)
				{
					if(MultiViewActive())
						GameClient()->m_RankGhost.ViewToggleMultiMember(m_SelectedSpectatorId);
					else
						Spectate(m_SelectedSpectatorId);
				}
				else if(GameClient()->m_MultiViewActivated)
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
			!GameClient()->m_Snap.m_aCharacters[pInfo->m_Id].m_Active)
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
		const int ClientId = pInfo->m_Id;
		const bool HideIdentity = !ViewModeActive && GameClient()->ShouldHideStreamerIdentity(ClientId);
		const bool IsFriend = pInfo->m_Friend;
		char aNameBuf[MAX_NAME_LENGTH];
		char aClanBuf[MAX_CLAN_LENGTH];
		if(ViewModeActive)
		{
			GameClient()->m_RankGhost.ViewMemberName(ClientId, aNameBuf, sizeof(aNameBuf));
			aClanBuf[0] = '\0';
		}
		else
		{
			GameClient()->FormatStreamerName(ClientId, aNameBuf, sizeof(aNameBuf));
			GameClient()->FormatStreamerClan(ClientId, aClanBuf, sizeof(aClanBuf));
		}
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
		if(!ViewModeActive && GameClient()->IsLocalClientId(ClientId))
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

		if(g_Config.m_ClShowIds && !HideIdentity && !ViewModeActive)
		{
			char aClientId[16];
			GameClient()->FormatClientId(ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
			TextRender()->TextEx(&NameCursor, aClientId);
		}

		TextRender()->TextEx(&NameCursor, aNameBuf);
		if(MultiViewActive())
		{
			if(ViewModeActive ? GameClient()->m_RankGhost.ViewMultiMemberSelected(pInfo->m_Id) : GameClient()->m_aMultiViewId[pInfo->m_Id])
			{
				TextRender()->TextColor(0.1f, 1.0f, 0.1f, (PlayerSelected ? 1.0f : 0.5f) * ContentAlpha);
				TextRender()->Text(CenterX + x + 50.0f + 180.0f, CenterY + y + BoxMove + (LineHeight - FontSize) / 2.f, FontSize - 3, "⬤", 220.0f);
			}
			else if(ViewModeActive || GameClient()->m_MultiViewTeam == DDTeam)
			{
				TextRender()->TextColor(1.0f, 0.1f, 0.1f, (PlayerSelected ? 1.0f : 0.5f) * ContentAlpha);
				TextRender()->Text(CenterX + x + 50.0f + 180.0f, CenterY + y + BoxMove + (LineHeight - FontSize) / 2.f, FontSize - 3, "◯", 220.0f);
			}
		}

		// flag
		if(!ViewModeActive && GameClient()->m_Snap.m_pGameInfoObj && (GameClient()->m_Snap.m_pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS) &&
			GameClient()->m_Snap.m_pGameDataObj && (GameClient()->m_Snap.m_pGameDataObj->m_FlagCarrierRed == pInfo->m_Id || GameClient()->m_Snap.m_pGameDataObj->m_FlagCarrierBlue == pInfo->m_Id))
		{
			Graphics()->BlendNormal();
			if(GameClient()->m_Snap.m_pGameDataObj->m_FlagCarrierBlue == pInfo->m_Id)
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

		CTeeRenderInfo TeeInfo;
		if(ViewModeActive)
			GameClient()->m_RankGhost.ViewMemberRenderInfo(pInfo->m_Id, &TeeInfo);
		else
			TeeInfo = GameClient()->m_aClients[pInfo->m_Id].m_RenderInfo;
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
			DrawSolidFriendHeart(Ui(), IconX, IconY, IconSize, FriendIconColor);
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
	if(!ViewModeActive)
		RenderTeleSearch(ScreenCenter, SelectorLayout.m_SearchRow, SelectorLayout.m_Status, ContentAlpha, MousePressed);
	RenderTools()->RenderCursor(ScreenCenter + m_SelectorMouse, 48.0f, ContentAlpha);
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

void CSpectator::RenderTeleSearch(vec2 Center, const CUIRect &RowRect, const CUIRect &StatusRect, float Alpha, bool MousePressed)
{
	const vec2 Mouse = Center + m_SelectorMouse;
	CUIRect Row = RowRect;
	CUIRect Label, Minus, Number, Plus, Find;
	Row.VSplitLeft(std::clamp(TextRender()->TextWidth(20.0f, Localize("Find CP")) + 16.0f, 100.0f, Row.w * 0.4f), &Label, &Row);
	Row.VSplitLeft(40.0f, &Minus, &Row);
	Row.VSplitLeft(8.0f, nullptr, &Row);
	Row.VSplitLeft(80.0f, &Number, &Row);
	Row.VSplitLeft(8.0f, nullptr, &Row);
	Row.VSplitLeft(40.0f, &Plus, &Row);
	Row.VSplitLeft(12.0f, nullptr, &Find);

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
	SLabelProperties StatusProps;
	StatusProps.m_MaxWidth = StatusRect.w;
	StatusProps.m_DisallowNewline = true;
	Ui()->DoLabel(&StatusRect, pStatus, 16.0f, TEXTALIGN_ML, StatusProps);
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
	m_ReplayUi.Reset();
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
	if(GameClient()->m_RankGhost.IsViewModeActive())
	{
		using EMode = CRankGhost::EViewCameraMode;
		if(SpectatorId == SPEC_FREEVIEW)
			GameClient()->m_RankGhost.ViewSetCameraMode(EMode::FREE);
		else if(SpectatorId == MULTI_VIEW)
			GameClient()->m_RankGhost.ViewSetCameraMode(EMode::ALL_MEMBERS);
		else if(SpectatorId == SPEC_FOLLOW)
			GameClient()->m_RankGhost.ViewSelectMember(0);
		else if(SpectatorId >= 0 && SpectatorId < GameClient()->m_RankGhost.ViewMemberCount())
			GameClient()->m_RankGhost.ViewSelectMember(SpectatorId);
		return;
	}
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
	if(GameClient()->m_RankGhost.IsViewModeActive())
	{
		int Closest = -1;
		float ClosestDistance = std::numeric_limits<float>::max();
		for(int Index = 0; Index < GameClient()->m_RankGhost.ViewMemberCount(); ++Index)
		{
			vec2 Position;
			if((GameClient()->m_RankGhost.ViewCameraMode() == CRankGhost::EViewCameraMode::MEMBER && Index == GameClient()->m_RankGhost.ViewSelectedMember()) || !GameClient()->m_RankGhost.ViewMemberPosition(Index, &Position))
				continue;
			const float Distance = distance(GameClient()->m_Camera.m_Center, Position);
			if(Distance < ClosestDistance)
			{
				Closest = Index;
				ClosestDistance = Distance;
			}
		}
		if(Closest >= 0)
			Spectate(Closest);
		return;
	}
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
