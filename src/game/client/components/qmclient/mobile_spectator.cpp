#include "mobile_spectator.h"

#include "media_paths.h"
#include "mobile_back.h"
#include "mobile_touch_rules.h"
#include "mobile_vote_bar.h"

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

#include <base/system.h>

#include <engine/client.h>
#include <engine/console.h>
#include <engine/demo.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/UiTokens.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

using namespace qm_mobile_spectator;

void CQmMobileSpectator::OnReset()
{
	m_Touch.Cancel();
	m_Join.Reset();
	m_vVoteBarFingers.clear();
	m_ScoreboardSwipeFinger.reset();
	m_IslandFinger.reset();
}

vec2 CQmMobileSpectator::ScreenSize() const
{
	return vec2(400.0f * Graphics()->ScreenAspect(), 400.0f);
}

bool CQmMobileSpectator::BaseControlsAvailable() const
{
	if(!Enabled() || (Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK))
		return false;
#if defined(CONF_VIDEORECORDER)
	if(IVideo::Current() != nullptr)
		return false;
#endif
	IEngineGraphics *pEngineGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	const bool WindowActive = pEngineGraphics == nullptr || pEngineGraphics->WindowActive() != 0;
	return WindowActive && !GameClient()->m_Menus.IsActive() &&
	       !GameClient()->m_GameConsole.IsActive() && !GameClient()->m_Spectator.IsActive() &&
	       !GameClient()->m_Emoticon.IsActive() && !GameClient()->m_HudEditor.IsActive() &&
	       !GameClient()->m_Motd.IsActive() &&
	       !GameClient()->m_ImportantAlert.IsActive() && !GameClient()->m_PieMenu.IsActive() && !Ui()->IsPopupOpen();
}

bool CQmMobileSpectator::ControlsAvailable() const
{
	return BaseControlsAvailable() && !GameClient()->m_Chat.IsActive() && !GameClient()->m_Scoreboard.IsActive();
}

void CQmMobileSpectator::PerformBack()
{
	CGameClient *pGameClient = GameClient();
	const std::chrono::nanoseconds Now = time_get_nanoseconds();
	const bool QuickSecond = m_LastBackTime != std::chrono::nanoseconds::zero() &&
				 Now - m_LastBackTime < std::chrono::milliseconds(QM_MOBILE_BACK_QUIT_WINDOW_MS);
	m_LastBackTime = Now;

	switch(QmMobileBackTarget(Ui()->IsPopupOpen(), pGameClient->m_Chat.IsActive(), pGameClient->m_Scoreboard.IsActive(),
		pGameClient->m_Spectator.IsActive(), pGameClient->m_Menus.IsActive(), QuickSecond))
	{
	case EQmMobileBackTarget::POPUP:
		Ui()->ClosePopupMenus();
		break;
	case EQmMobileBackTarget::CHAT:
		pGameClient->m_Chat.DisableMode();
		break;
	case EQmMobileBackTarget::SCOREBOARD:
		pGameClient->m_Scoreboard.SetActive(false);
		break;
	case EQmMobileBackTarget::SPECTATOR:
		pGameClient->m_Spectator.OnRelease();
		break;
	case EQmMobileBackTarget::MENU_CLOSE:
		pGameClient->m_Menus.SetActive(false);
		break;
	case EQmMobileBackTarget::MENU_OPEN:
		pGameClient->m_Menus.SetActive(true);
		break;
	case EQmMobileBackTarget::QUIT:
		pGameClient->m_Menus.SetActive(true);
		pGameClient->m_Menus.ShowQuitPopup();
		break;
	case EQmMobileBackTarget::NONE:
		break;
	}
}

void CQmMobileSpectator::PerformFreeView()
{
	GameClient()->m_Spectator.Spectate(SPEC_FREEVIEW);
	ShowToast(Localize("Free view"));
}

void CQmMobileSpectator::ShowToast(const char *pText)
{
	str_copy(m_aToastText, pText, sizeof(m_aToastText));
	m_ToastUntil = time_get_nanoseconds() + std::chrono::milliseconds(2400);
}

void CQmMobileSpectator::DrawButtonSurface(const CUIRect &Rect, const ColorRGBA &Color, float Rounding)
{
	// 模糊每帧只准备一次（CUi 内部按帧缓存），所以每个按钮各画一次并不额外捕获画面；
	// 底板始终带模糊，按钮压在场景上时后面的地图/特效不会把图标吃掉。
	Ui()->RenderGaussianBlur(Rect, 1.0f, IGraphics::CORNER_ALL, Rounding);
	Rect.Draw(Color, IGraphics::CORNER_ALL, Rounding);
}

void CQmMobileSpectator::RenderIsland(const vec2 &Screen)
{
	// 底部观战灵动岛：常驻显示当前取景对象（相当于不会消失的提示），点一下回自由视角。
	// 宽度跟着文字走，两侧不留空白；命中判定用同一矩形，避免和显示错位。
	// HUD 编辑器里它也是一个元素：BeginTransform 让用户能移动/缩放，跑完后命中区跟着走。
	char aBuf[128];
	const int SpectatorId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
	if(GameClient()->m_Snap.m_SpecInfo.m_Active && SpectatorId >= 0 && SpectatorId < MAX_CLIENTS && GameClient()->m_aClients[SpectatorId].m_Active)
		str_format(aBuf, sizeof(aBuf), Localize("Following: %s"), GameClient()->m_aClients[SpectatorId].m_aName);
	else
		str_copy(aBuf, Localize("Free view"));
	constexpr float IslandFontSize = 10.0f;
	const float TextWidth = TextRender()->TextWidth(IslandFontSize, aBuf);
	const float IslandWidth = std::clamp(TextWidth + 30.0f, 120.0f, Screen.x * 0.6f);
	const CUIRect Nominal = {(Screen.x - IslandWidth) * 0.5f, Screen.y - 46.0f, IslandWidth, 32.0f};

	const auto Scope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::MobileIsland, Nominal);
	DrawButtonSurface(Nominal, ui_token::color::SURFACE_OVERLAY, Nominal.h * 0.5f);
	SLabelProperties IslandProps;
	IslandProps.m_EllipsisAtEnd = true;
	IslandProps.m_MaxWidth = Nominal.w - 16.0f;
	CUIRect Label = Nominal;
	Ui()->DoLabel(&Label, aBuf, IslandFontSize, TEXTALIGN_MC, IslandProps);
	GameClient()->m_HudEditor.EndTransform(Scope);

	m_IslandRect = Scope.m_Applied ? Scope.m_VisibleRect : Nominal;
}

void CQmMobileSpectator::RenderToast(const vec2 &Screen)
{
	if(m_aToastText[0] == '\0' || m_ToastUntil == std::chrono::nanoseconds::zero())
		return;
	const std::chrono::nanoseconds Now = time_get_nanoseconds();
	if(Now >= m_ToastUntil)
	{
		m_aToastText[0] = '\0';
		m_ToastUntil = std::chrono::nanoseconds::zero();
		return;
	}
	// 最后 400ms 淡出，避免提示突然消失。
	const float Remaining = std::chrono::duration<float>(m_ToastUntil - Now).count();
	const float Alpha = std::clamp(Remaining / 0.4f, 0.0f, 1.0f);

	// 只包住文字：不做固定宽度的大底板，字号也比按钮标签小一号。
	const float FontSize = 10.0f;
	const float TextWidth = TextRender()->TextWidth(FontSize, m_aToastText);
	const float ToastWidth = std::clamp(TextWidth + 26.0f, 96.0f, Screen.x * 0.6f);
	CUIRect Toast = {(Screen.x - ToastWidth) * 0.5f, 12.0f, ToastWidth, 28.0f};
	// 顶部投票条占着中间时把提示让到它下面。
	if(MobileVoteBarVisible())
		Toast.y = 96.0f;

	DrawButtonSurface(Toast, ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f * Alpha), 10.0f);
	TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha));
	Ui()->DoLabel(&Toast, m_aToastText, FontSize, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

bool CQmMobileSpectator::MobileVoteBarVisible() const
{
	// 计分板打开时投票面板由计分板自己画（且那一层本来就可交互），不重复叠一条。
	return QmMobileVoteBarVisible(QmUiMobilePlatform(), Client()->State() == IClient::STATE_ONLINE,
		       GameClient()->m_Voting.IsVoting(), GameClient()->m_Menus.IsActive(), GameClient()->m_GameConsole.IsActive(),
		       GameClient()->m_HudEditor.IsActive()) &&
	       !GameClient()->m_Chat.IsActive() && !GameClient()->m_Scoreboard.IsActive() && !GameClient()->m_Spectator.IsActive();
}

void CQmMobileSpectator::RenderMobileVoteBar(const SQmMobileVoteBarLayout &Layout)
{
	CVoting &Voting = GameClient()->m_Voting;
	DrawButtonSurface(Layout.m_Panel, ui_token::color::NEUTRAL_BLACK.WithMultipliedAlpha(0.45f), 10.0f);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), Localize("%ds left"), maximum(0, Voting.SecondsLeft()));
	CUIRect Description = Layout.m_Description;
	CUIRect Time;
	const float TimeWidth = minimum(80.0f, TextRender()->TextWidth(Layout.m_FontSize, aBuf));
	Description.VSplitRight(TimeWidth, &Description, &Time);
	SLabelProperties Props;
	Props.m_EllipsisAtEnd = true;
	Props.m_MaxWidth = Description.w;
	Ui()->DoLabel(&Description, Voting.VoteDescription(), Layout.m_FontSize, TEXTALIGN_ML, Props);
	Ui()->DoLabel(&Time, aBuf, Layout.m_FontSize, TEXTALIGN_MR);

	str_format(aBuf, sizeof(aBuf), "%s %s", Localize("Reason:"), Voting.VoteReason());
	Props.m_MaxWidth = Layout.m_Reason.w;
	Ui()->DoLabel(&Layout.m_Reason, aBuf, Layout.m_SmallFontSize, TEXTALIGN_ML, Props);

	// 进度条：是和否按票数比例铺满，和计分板里的投票面板同一语义。
	Layout.m_Bars.Draw(ui_token::color::TINT_HUD_BAR, IGraphics::CORNER_ALL, Layout.m_Bars.h / 2.0f);
	const int Total = Voting.TotalCount();
	if(Total > 0)
	{
		if(Voting.YesCount() > 0)
		{
			CUIRect YesArea;
			Layout.m_Bars.VSplitLeft(Layout.m_Bars.w * Voting.YesCount() / Total, &YesArea, nullptr);
			YesArea.Draw(ui_token::color::SUCCESS.WithMultipliedAlpha(0.85f), IGraphics::CORNER_ALL, YesArea.h / 2.0f);
		}
		if(Voting.NoCount() > 0)
		{
			CUIRect NoArea;
			Layout.m_Bars.VSplitRight(Layout.m_Bars.w * Voting.NoCount() / Total, nullptr, &NoArea);
			NoArea.Draw(ui_token::color::DANGER.WithMultipliedAlpha(0.85f), IGraphics::CORNER_ALL, NoArea.h / 2.0f);
		}
	}

	for(int Index = 0; Index < 2; ++Index)
	{
		const int Choice = Index == 0 ? 1 : -1;
		const CUIRect &Button = Index == 0 ? Layout.m_Yes : Layout.m_No;
		const bool Selected = Voting.TakenChoice() == Choice;
		const ColorRGBA ChoiceColor = Choice == 1 ? ui_token::color::SUCCESS : ui_token::color::DANGER;
		DrawButtonSurface(Button, Selected ? ChoiceColor.WithAlpha(0.35f) : ui_token::color::SURFACE_GLASS, 8.0f);
		TextRender()->TextColor(Selected ? ChoiceColor : TextRender()->DefaultTextColor());
		Ui()->DoLabel(&Button, Choice == 1 ? Localize("Vote yes") : Localize("Vote no"), Layout.m_ButtonFontSize, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}

bool CQmMobileSpectator::ScoreboardOnlyControls() const
{
	// 计分板是自己打开的底部面板，触控层这时只留关闭按钮，别的按钮不再叠在面板上。
	return BaseControlsAvailable() && GameClient()->m_Scoreboard.IsActive() && !GameClient()->m_Chat.IsActive();
}

bool CQmMobileSpectator::FreeViewActive() const
{
	if(GameClient()->m_RankGhost.IsViewModeActive())
		return GameClient()->m_RankGhost.ViewCameraMode() == CRankGhost::EViewCameraMode::FREE;
	if(GameClient()->m_MultiViewActivated)
		return false;
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return GameClient()->m_DemoSpecId == SPEC_FREEVIEW;
	const auto &SpecInfo = GameClient()->m_Snap.m_SpecInfo;
	return SpecInfo.m_Active && SpecInfo.m_SpectatorId == SPEC_FREEVIEW && !SpecInfo.m_UsePosition;
}

void CQmMobileSpectator::OnNewSnapshot()
{
	if(!Enabled())
		return;
	const auto *pInfo = GameClient()->m_Snap.m_pLocalInfo;
	if(m_Join.Update(Client()->State() == IClient::STATE_ONLINE, pInfo != nullptr,
		   pInfo != nullptr && pInfo->m_Team == TEAM_SPECTATORS, time_get() / (double)time_freq()))
		GameClient()->SendSwitchTeam(TEAM_SPECTATORS);
}

void CQmMobileSpectator::PerformAction(int Action)
{
	const char *apCommands[] = {"spectate_previous", "+spectate", "spectate_next"};
	if(Action >= PREVIOUS && Action <= NEXT)
	{
		if(Client()->State() == IClient::STATE_DEMOPLAYBACK && (Action == PREVIOUS || Action == NEXT))
			// Demo 的录制者视角也可直接切换到玩家，复用正常旁观排序。
			GameClient()->m_Spectator.SpectateNext(Action == PREVIOUS);
		else
			Console()->ExecuteLineStroked(1, apCommands[Action], IConsole::CLIENT_ID_UNSPECIFIED);
	}
	else if(Action == NEAREST_TEE)
	{
		// 视野中心最近的 Tee：拖着画面看人时一下就跟上去，不用回列表按 ID 找。
		SQmNearestTeeCandidate aCandidates[MAX_CLIENTS];
		int NumCandidates = 0;
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
			const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
			if(!ClientData.m_Active || pInfo == nullptr)
				continue;
			SQmNearestTeeCandidate &Candidate = aCandidates[NumCandidates++];
			Candidate.m_ClientId = ClientId;
			Candidate.m_Active = true;
			// 自己与纯旁观者没有实体，不作为「最近的 Tee」候选。
			Candidate.m_Local = GameClient()->IsLocalClientId(ClientId) || pInfo->m_Team == TEAM_SPECTATORS;
			Candidate.m_Pos = ClientData.m_RenderPos;
		}
		const int NearestId = QmNearestTeeClientId(aCandidates, NumCandidates, GameClient()->m_Camera.m_Center);
		if(NearestId < 0)
		{
			ShowToast(Localize("No tee nearby"));
		}
		else
		{
			GameClient()->m_Scoreboard.SpectatePlayer(NearestId);
			char aBuf[96];
			str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Spectate"), GameClient()->m_aClients[NearestId].m_aName);
			ShowToast(aBuf);
		}
	}
	else if(Action == SCOREBOARD)
	{
		// +scoreboard 是 stroke 命令：按下走 1、松开走 0，两者都作用在同一个开关上。
		const bool Open = !GameClient()->m_Scoreboard.IsActive();
		if(Open)
			m_ScoreboardToggleActivationTime = time_get_nanoseconds();
		Console()->ExecuteLineStroked(Open ? 1 : 0, "+scoreboard", IConsole::CLIENT_ID_UNSPECIFIED);
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Scoreboard"), Localize(Open ? "On" : "Off"));
		ShowToast(aBuf);
	}
	else if(Action == ENTITY_LAYER)
	{
		g_Config.m_ClOverlayEntities = NextEntityLayerValue(g_Config.m_ClOverlayEntities);
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "%s %d%%", Localize("Entity layer"), g_Config.m_ClOverlayEntities);
		ShowToast(aBuf);
	}
	else if(Action == CAMERA_MODE)
	{
		g_Config.m_QmMobileCameraMode = g_Config.m_QmMobileCameraMode == CAMERA_MODE_DRAG ? CAMERA_MODE_JOYSTICK : CAMERA_MODE_DRAG;
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Camera"),
			Localize(g_Config.m_QmMobileCameraMode == CAMERA_MODE_DRAG ? "Drag" : "Joystick"));
		ShowToast(aBuf);
	}
	else if(Action == MENU)
		GameClient()->m_Menus.SetActive(true);
	else if(Action == CHAT)
	{
		m_ChatToggleActivationTime = time_get_nanoseconds();
		GameClient()->m_Chat.EnableMode(0);
	}
	else if(Action == PAUSE && Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		if(DemoPlayer()->BaseInfo()->m_Paused)
			DemoPlayer()->Unpause();
		else
			DemoPlayer()->Pause();
	}
}

bool CQmMobileSpectator::OnTouchState(std::vector<IInput::CTouchFingerState> &vFingers)
{
	if(qm_mobile_spectator::Enabled() && GameClient()->m_Chat.IsActive())
	{
		// 聊天激活时触控整体停用，只保留“再点原聊天按钮关闭”这一条出路。
		m_Touch.Cancel();
		const vec2 Screen = ScreenSize();
		const auto Geometry = Layout(Screen);
		for(auto It = vFingers.begin(); It != vFingers.end(); ++It)
		{
			if(It->m_PressTime <= m_ChatToggleActivationTime)
				continue;
			if(Geometry.m_aButtons[CHAT].Inside(It->m_Position * Screen))
			{
				GameClient()->m_Chat.DisableMode();
				vFingers.erase(It);
				break;
			}
		}
		return false;
	}
	if(Enabled() && GameClient()->m_Scoreboard.IsActive() && !GameClient()->m_Chat.IsActive())
	{
		// 计分板打开时触控层只保留关闭入口：面板本来就把屏幕占满，别的按钮不该再叠上去。
		m_Touch.Cancel();
		if(!ScoreboardOnlyControls())
			return false;
		const vec2 Screen = ScreenSize();
		// 面板外轻点抬起后关闭：位移够小、时间够短才算点击，滑动交给滚动/系统手势。
		if(m_ScoreboardOutsideFinger.has_value())
		{
			const IInput::CTouchFinger OutsideFinger = m_ScoreboardOutsideFinger.value();
			const bool StillDown = std::any_of(vFingers.begin(), vFingers.end(), [&](const IInput::CTouchFingerState &State) { return State.m_Finger == OutsideFinger; });
			if(!StillDown)
			{
				const std::chrono::nanoseconds Held = time_get_nanoseconds() - m_ScoreboardOutsidePressTime;
				float MaxMove = 0.0f;
				for(const auto &State : vFingers)
					MaxMove = maximum(MaxMove, distance(State.m_Position, m_ScoreboardOutsideStart));
				m_ScoreboardOutsideFinger.reset();
				if(qm_mobile_touch::OutsideTapClosesPanel(MaxMove, std::chrono::duration_cast<std::chrono::milliseconds>(Held).count()))
					GameClient()->m_Scoreboard.SetActive(false);
			}
		}
		// 下滑关闭只认从面板顶部起手的手势：列表区域的竖向拖动是滚动，不能当成关闭。
		constexpr float SwipeStartZone = 0.30f;
		constexpr float SwipeCloseDistance = 0.12f;
		for(auto It = vFingers.begin(); It != vFingers.end();)
		{
			// 打开计分板的那一根手指不能立刻又把面板关掉。
			const bool Fresh = It->m_PressTime > m_ScoreboardToggleActivationTime;
			const vec2 Position = It->m_Position * Screen;
			// 点在面板外 = 关掉计分板，但必须是一次「轻点」：按下就关会让三指下滑截图、
			// 从边缘起手的手势全部误触发。这里只记录候选，等它抬起时再判断位移与时长。
			if(Fresh && !m_ScoreboardOutsideFinger.has_value() && qm_mobile_touch::ShouldArmOutsideTap(static_cast<int>(vFingers.size()), GameClient()->m_Scoreboard.MobilePanelContains(It->m_Position)))
			{
				m_ScoreboardOutsideFinger = It->m_Finger;
				m_ScoreboardOutsideStart = It->m_Position;
				m_ScoreboardOutsidePressTime = time_get_nanoseconds();
			}
			if(Fresh && !m_ScoreboardSwipeFinger.has_value() && It->m_Position.y < SwipeStartZone)
			{
				m_ScoreboardSwipeFinger = It->m_Finger;
				m_ScoreboardSwipeStartY = It->m_Position.y;
			}
			++It;
		}
		if(m_ScoreboardSwipeFinger.has_value())
		{
			const IInput::CTouchFinger SwipeFinger = m_ScoreboardSwipeFinger.value();
			const auto SwipeIt = std::find_if(vFingers.begin(), vFingers.end(), [&](const IInput::CTouchFingerState &State) { return State.m_Finger == SwipeFinger; });
			if(SwipeIt == vFingers.end())
				m_ScoreboardSwipeFinger.reset();
			else if(SwipeIt->m_Position.y - m_ScoreboardSwipeStartY > SwipeCloseDistance)
			{
				GameClient()->m_Scoreboard.SetActive(false);
				m_ScoreboardSwipeFinger.reset();
			}
		}
		return false;
	}
	if(!ControlsAvailable())
	{
		m_Touch.Cancel();
		return false;
	}
	const vec2 Screen = ScreenSize();

	// 左边缘右滑 = 返回。手指数不参与镜头：命中的那根手指从列表里摘掉。
	// 从边缘起手、横向位移超过阈值就触发；纵向限位避开底部摇杆。
	{
		constexpr float EdgeBandX = 0.06f;
		constexpr float EdgeSwipeMinX = 0.07f;
		constexpr float EdgeSwipeMinY = 0.10f;
		constexpr float EdgeSwipeMaxY = 0.55f;
		if(m_EdgeSwipeFinger.has_value())
		{
			const IInput::CTouchFinger SwipeFinger = m_EdgeSwipeFinger.value();
			const auto SwipeIt = std::find_if(vFingers.begin(), vFingers.end(), [&](const IInput::CTouchFingerState &State) { return State.m_Finger == SwipeFinger; });
			if(SwipeIt == vFingers.end())
			{
				m_EdgeSwipeFinger.reset();
			}
			else if(SwipeIt->m_Position.x - m_EdgeSwipeStartX >= EdgeSwipeMinX)
			{
				PerformBack();
				m_EdgeSwipeFinger.reset();
				vFingers.erase(SwipeIt);
			}
		}
		if(!m_EdgeSwipeFinger.has_value())
		{
			for(const auto &Finger : vFingers)
			{
				if(Finger.m_Position.x <= EdgeBandX && Finger.m_Position.y >= EdgeSwipeMinY && Finger.m_Position.y <= EdgeSwipeMaxY)
				{
					m_EdgeSwipeFinger = Finger.m_Finger;
					m_EdgeSwipeStartX = Finger.m_Position.x;
					break;
				}
			}
		}
	}

	// 底部灵动岛：点一下回自由视角（原「自由视角」按钮的职责），一次按下只触发一次。
	{
		if(m_IslandFinger.has_value())
		{
			const IInput::CTouchFinger IslandFinger = m_IslandFinger.value();
			const bool StillDown = std::any_of(vFingers.begin(), vFingers.end(), [&](const IInput::CTouchFingerState &State) { return State.m_Finger == IslandFinger; });
			if(!StillDown)
				m_IslandFinger.reset();
		}
		const CUIRect Island = m_IslandRect.w > 0.0f ? m_IslandRect : Layout(Screen).m_Island;
		for(auto It = vFingers.begin(); It != vFingers.end();)
		{
			if(!Island.Inside(It->m_Position * Screen))
			{
				++It;
				continue;
			}
			if(!m_IslandFinger.has_value())
			{
				m_IslandFinger = It->m_Finger;
				PerformFreeView();
			}
			It = vFingers.erase(It);
		}
	}

	// 顶部投票条：两个大按钮优先命中。落在横幅上的手指归投票条，
	// 不再参与镜头拖动；只在新按下那一帧投票，长按不会连发。
	m_vVoteBarFingers.erase(std::remove_if(m_vVoteBarFingers.begin(), m_vVoteBarFingers.end(), [&](const IInput::CTouchFinger &Finger) {
		return std::none_of(vFingers.begin(), vFingers.end(), [&](const IInput::CTouchFingerState &State) { return State.m_Finger == Finger; });
	}),
		m_vVoteBarFingers.end());
	if(MobileVoteBarVisible())
	{
		const SQmMobileVoteBarLayout VoteLayout = QmMobileVoteBarLayout(Screen);
		for(auto It = vFingers.begin(); It != vFingers.end();)
		{
			const vec2 Position = It->m_Position * Screen;
			if(!VoteLayout.m_Panel.Inside(Position))
			{
				++It;
				continue;
			}
			const bool NewPress = std::find(m_vVoteBarFingers.begin(), m_vVoteBarFingers.end(), It->m_Finger) == m_vVoteBarFingers.end();
			if(NewPress)
			{
				m_vVoteBarFingers.push_back(It->m_Finger);
				if(VoteLayout.m_Yes.Inside(Position))
					GameClient()->m_Voting.Vote(1);
				else if(VoteLayout.m_No.Inside(Position))
					GameClient()->m_Voting.Vote(-1);
			}
			It = vFingers.erase(It);
		}
	}

	const bool WasMoving = m_Touch.JoystickDirection() != vec2(0.0f, 0.0f);
	// 拖动取景时摇杆不参与命中：左上/左下的大片空白都应该是可以拖画面的地方。
	const bool DragCameraActive = g_Config.m_QmMobileCameraMode == CAMERA_MODE_DRAG;
	const unsigned Actions = m_Touch.Update(vFingers, Layout(Screen, Client()->State() == IClient::STATE_DEMOPLAYBACK, !DragCameraActive), Screen, Client()->GlobalTime());
	for(int i = 0; i < NUM_ACTIONS; ++i)
		if((Actions & (1U << i)) != 0)
			PerformAction(i);
	if(m_Touch.MapTapped() && FreeViewActive() && ControlsAvailable())
		GameClient()->m_Spectator.SpectateClosest();
	if(m_Touch.ZoomFactor() != 1.0f)
	{
		// 连续倍率转为现有缩放命令的档位，复用自动旁观、多人取景和缩放限制。
		char aCommand[64];
		str_format(aCommand, sizeof(aCommand), "zoom+ %.6f", std::log(m_Touch.ZoomFactor()) / std::log(CCamera::ZOOM_STEP));
		Console()->ExecuteLine(aCommand, IConsole::CLIENT_ID_UNSPECIFIED);
	}
	// 取景方式：轮盘用左下摇杆推进，拖动用空白区域单指拖画面；两种都直接改 m_aMousePos。
	const vec2 Direction = DragCameraActive ? vec2(0.0f, 0.0f) : m_Touch.JoystickDirection();
	// 1:1 跟手：位移按当前缩放下的可见世界尺寸换算（CalcScreenParams 与摄像机渲染同一来源）。
	vec2 WorldExtent(0.0f, 0.0f);
	Graphics()->CalcScreenParams(Graphics()->GameScreenAspect(), GameClient()->m_Camera.m_Zoom, &WorldExtent.x, &WorldExtent.y);
	const vec2 DragDelta = DragCameraActive ? DragPan(m_Touch.PanDelta(), WorldExtent) : vec2(0.0f, 0.0f);
	const bool CameraMoving = Direction != vec2(0.0f, 0.0f) || DragDelta != vec2(0.0f, 0.0f);
	if(CameraMoving && ControlsAvailable() &&
		(Client()->State() == IClient::STATE_DEMOPLAYBACK || GameClient()->m_Snap.m_SpecInfo.m_Active))
	{
		auto &Controls = GameClient()->m_Controls;
		if(!WasMoving)
		{
			if((Client()->State() == IClient::STATE_DEMOPLAYBACK && GameClient()->m_DemoSpecId != SPEC_FREEVIEW) ||
				GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW ||
				(GameClient()->m_RankGhost.IsViewModeActive() && GameClient()->m_RankGhost.ViewCameraMode() != CRankGhost::EViewCameraMode::FREE))
				GameClient()->m_Spectator.Spectate(SPEC_FREEVIEW);
		}
		// 等旁观目标切换完成，再按自由视角的世界坐标移动，避免跟随期间改变瞄准偏移。
		if(GameClient()->m_Snap.m_SpecInfo.m_Active && !GameClient()->m_Snap.m_SpecInfo.m_UsePosition)
		{
			if(DragCameraActive)
				Controls.m_aMousePos[g_Config.m_ClDummy] += DragDelta;
			else
				Controls.m_aMousePos[g_Config.m_ClDummy] += Movement(Direction, GameClient()->m_Camera.m_Zoom, Client()->RenderFrameTime());
			Controls.m_aMouseInputType[g_Config.m_ClDummy] = CControls::EMouseInputType::RELATIVE;
			Controls.ClampMousePos();
		}
	}
	vFingers.clear();
	return true;
}

void CQmMobileSpectator::ExportFile(const char *pPath)
{
#if defined(CONF_PLATFORM_ANDROID)
	if(pPath == nullptr || pPath[0] == '\0')
		return;
	if(!ExportAndroidMediaFile(pPath))
		ShowToast(Localize("Export failed"));
	else
		ShowToast(Localize("Saved to Downloads/QmClient"));
#else
	(void)pPath;
#endif
}

void CQmMobileSpectator::ShowMediaFiles(bool Videos)
{
#if defined(CONF_PLATFORM_ANDROID)
	// 现代路径：目录浏览交给系统文件管理器，不再自绘文件列表对话框。
	(void)Videos;
	if(!OpenAndroidDownloadsFolder())
		ShowToast(Localize("Export failed"));
	return;
#endif

#if defined(CONF_PLATFORM_ANDROID)
	using namespace qmclient::media_paths;
	const auto Kind = Videos ? EKind::VIDEOS : EKind::DEMOS;
	const std::string Root = CompletePath(Storage(), Directory(Storage(), g_Config, Kind));
	std::array<std::string, RECORDER_MAX> aExcluded;
	std::array<const char *, RECORDER_MAX> apExcluded;
	size_t Count = 0;
	for(int i = 0; i < RECORDER_MAX; ++i)
	{
		if(!DemoRecorder(i)->IsRecording())
			continue;
		aExcluded[Count] = CompletePath(Storage(), DemoRecorder(i)->CurrentFilename());
		apExcluded[Count] = aExcluded[Count].c_str();
		++Count;
	}
	if(Root.empty() || !ShowAndroidMediaFiles(Root.c_str(), Videos, apExcluded.data(), Count))
	{
		char aMessage[IO_MAX_PATH_LENGTH + 64];
		str_format(aMessage, sizeof(aMessage), Localize("Failed to open the directory '%s'.", "Editor"), Root.c_str());
		Client()->AddWarning(SWarning(Localize("Error"), aMessage));
	}
#else
	(void)Videos;
#endif
}

void CQmMobileSpectator::OnRender()
{
	if(qm_mobile_spectator::Enabled() && GameClient()->m_Chat.IsActive())
	{
		// 其它触控按钮隐藏，但仍画出原聊天按钮：它现在就是关闭聊天的入口。
		m_Touch.Cancel();
		const vec2 Screen = ScreenSize();
		Graphics()->MapScreen(0.0f, 0.0f, Screen.x, Screen.y);
		const auto Geometry = Layout(Screen);
		const CUIRect &ChatButton = Geometry.m_aButtons[CHAT];
		DrawButtonSurface(ChatButton, ui_token::color::NEUTRAL_BLACK.WithMultipliedAlpha(0.32f), 12.0f);
		SLabelProperties Props;
		Props.m_StopAtEnd = true;
		Props.m_MinimumFontSize = 6.0f;
		GameClient()->m_TouchControls.RenderButtonLabel({CTouchControls::CButtonLabel::EType::ICON, FontIcons::FONT_ICON_COMMENT}, ChatButton, 18.0f, TEXTALIGN_MC, Props);
		return;
	}
	if(Enabled() && GameClient()->m_Scoreboard.IsActive() && !GameClient()->m_Chat.IsActive())
	{
		// 计分板打开时不画任何按钮：点面板外关闭，面板本身不叠控件。
		m_Touch.Cancel();
		if(!ScoreboardOnlyControls())
			return;
		const vec2 Screen = ScreenSize();
		Graphics()->MapScreen(0.0f, 0.0f, Screen.x, Screen.y);
		// 面板盖住了普通按钮层，但“计分板: 开”的提示仍要在这一层画出来。
		RenderToast(Screen);
		return;
	}
	// HUD 编辑器打开时触控层不吃触摸，但灵动岛仍要画出来：
	// 编辑器靠 render 时的 BeginTransform 发现元素，不画就注册不到、也没法拖它。
	if(qm_mobile_spectator::Enabled() && GameClient()->m_HudEditor.IsActive() && Client()->State() == IClient::STATE_ONLINE)
	{
		m_Touch.Cancel();
		const vec2 EditorScreen = ScreenSize();
		Graphics()->MapScreen(0.0f, 0.0f, EditorScreen.x, EditorScreen.y);
		RenderIsland(EditorScreen);
		return;
	}
	if(!ControlsAvailable())
	{
		m_Touch.Cancel();
		return;
	}
	const vec2 Screen = ScreenSize();
	const auto Geometry = Layout(Screen, Client()->State() == IClient::STATE_DEMOPLAYBACK);
	Graphics()->MapScreen(0.0f, 0.0f, Screen.x, Screen.y);
	const bool DragCamera = g_Config.m_QmMobileCameraMode == CAMERA_MODE_DRAG;
	if(FreeViewActive() && !g_Config.m_QmRenderCursorSpec)
	{
		// 轻点地图以画面中央准星选人，摇杆移动和捏合不会移动该选人基准。
		const vec2 Center = Screen * 0.5f;
		const ColorRGBA Color = ui_token::color::TEXT_SECONDARY;
		Graphics()->DrawRect(Center.x - 5.0f, Center.y - 0.5f, 3.0f, 1.0f, Color, 0, 0.0f);
		Graphics()->DrawRect(Center.x + 2.0f, Center.y - 0.5f, 3.0f, 1.0f, Color, 0, 0.0f);
		Graphics()->DrawRect(Center.x - 0.5f, Center.y - 5.0f, 1.0f, 3.0f, Color, 0, 0.0f);
		Graphics()->DrawRect(Center.x - 0.5f, Center.y + 2.0f, 1.0f, 3.0f, Color, 0, 0.0f);
	}
	// 拖动取景时整块画面都是操作面，摇杆不再绘制，也避免误以为只能推摇杆。
	if(!DragCamera)
	{
		DrawButtonSurface(Geometry.m_Joystick, ui_token::color::NEUTRAL_BLACK.WithMultipliedAlpha(0.18f), 52.0f);
		const vec2 Knob = Geometry.m_Joystick.Center() + m_Touch.JoystickDirection() * 34.0f;
		const CUIRect KnobRect = {Knob.x - 18.0f, Knob.y - 18.0f, 36.0f, 36.0f};
		KnobRect.Draw(ui_token::color::NEUTRAL_WHITE.WithMultipliedAlpha(0.32f), IGraphics::CORNER_ALL, 18.0f);
	}
	// 顶部投票条压在触控按钮下方、不抢按钮位置（横幅居中偏上）。
	if(MobileVoteBarVisible())
		RenderMobileVoteBar(QmMobileVoteBarLayout(Screen));

	RenderIsland(Screen);
	using namespace FontIcons;
	const char *apLabels[NUM_ACTIONS] = {
		FONT_ICON_BACKWARD_STEP, FONT_ICON_USERS, FONT_ICON_FORWARD_STEP, FONT_ICON_EYE,
		FONT_ICON_LIST_UL, FONT_ICON_LAYER_GROUP, FONT_ICON_ARROWS_UP_DOWN,
		FONT_ICON_GEAR, FONT_ICON_COMMENT,
		Client()->State() == IClient::STATE_DEMOPLAYBACK && DemoPlayer()->BaseInfo()->m_Paused ? FONT_ICON_PLAY : FONT_ICON_PAUSE};
	const ColorRGBA Accent = Ui()->QmControlTheme().m_Accent;
	for(int i = 0; i < NUM_ACTIONS; ++i)
	{
		if(i == PAUSE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
			continue;
		const auto &Rect = Geometry.m_aButtons[i];
		// 取景按钮在拖动模式下点亮，等同于“当前状态”指示，不额外加文案。
		const bool ActiveButton = i == CAMERA_MODE && DragCamera;
		DrawButtonSurface(Rect, ActiveButton ? Accent.WithAlpha(0.45f) : ui_token::color::NEUTRAL_BLACK.WithMultipliedAlpha(0.25f), 12.0f);
		SLabelProperties Props;
		Props.m_StopAtEnd = true;
		Props.m_MinimumFontSize = 6.0f;
		if(i == ENTITY_LAYER)
		{
			// 图标让出下半格写百分比：0 = 只看地图，100 = 实体层铺满，50 = 两者叠加。
			CUIRect IconRect = Rect;
			IconRect.h *= 0.78f;
			IconRect.y -= Rect.h * 0.10f;
			GameClient()->m_TouchControls.RenderButtonLabel({CTouchControls::CButtonLabel::EType::ICON, apLabels[i]}, IconRect, 15.0f, TEXTALIGN_MC, Props);
			char aBuf[8];
			str_format(aBuf, sizeof(aBuf), "%d%%", g_Config.m_ClOverlayEntities);
			CUIRect TextRect = Rect;
			TextRect.y += Rect.h * 0.56f;
			TextRect.h *= 0.44f;
			Ui()->DoLabel(&TextRect, aBuf, 7.5f, TEXTALIGN_MC);
		}
		else
		{
			GameClient()->m_TouchControls.RenderButtonLabel({CTouchControls::CButtonLabel::EType::ICON, apLabels[i]}, Rect, 18.0f, TEXTALIGN_MC, Props);
		}
	}
	RenderToast(Screen);
}
