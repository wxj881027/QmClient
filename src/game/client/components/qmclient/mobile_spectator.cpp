#include "mobile_spectator.h"

#include "media_paths.h"

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

#include <game/client/gameclient.h>
#include <game/localization.h>

using namespace qm_mobile_spectator;

void CQmMobileSpectator::OnReset()
{
	m_Touch.Cancel();
	m_Join.Reset();
}

vec2 CQmMobileSpectator::ScreenSize() const
{
	return vec2(400.0f * Graphics()->ScreenAspect(), 400.0f);
}

bool CQmMobileSpectator::ControlsAvailable() const
{
	if(!Enabled() || (Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK))
		return false;
#if defined(CONF_VIDEORECORDER)
	if(IVideo::Current() != nullptr)
		return false;
#endif
	IEngineGraphics *pEngineGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	const bool WindowActive = pEngineGraphics == nullptr || pEngineGraphics->WindowActive() != 0;
	return WindowActive && !GameClient()->m_Menus.IsActive() && !GameClient()->m_Chat.IsActive() &&
	       !GameClient()->m_GameConsole.IsActive() && !GameClient()->m_Spectator.IsActive() &&
	       !GameClient()->m_Emoticon.IsActive() && !GameClient()->m_HudEditor.IsActive() &&
	       !GameClient()->m_Scoreboard.IsActive() && !GameClient()->m_Motd.IsActive() &&
	       !GameClient()->m_ImportantAlert.IsActive() && !GameClient()->m_PieMenu.IsActive() && !Ui()->IsPopupOpen();
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
	const char *apCommands[] = {"spectate_previous", "+spectate", "spectate_next", "spectate -1"};
	if(Action >= PREVIOUS && Action <= FREEVIEW)
	{
		if(Action == FREEVIEW)
			GameClient()->m_Spectator.Spectate(SPEC_FREEVIEW);
		else if(Client()->State() == IClient::STATE_DEMOPLAYBACK && (Action == PREVIOUS || Action == NEXT))
			// Demo 的录制者视角也可直接切换到玩家，复用正常旁观排序。
			GameClient()->m_Spectator.SpectateNext(Action == PREVIOUS);
		else
			Console()->ExecuteLineStroked(1, apCommands[Action], IConsole::CLIENT_ID_UNSPECIFIED);
	}
	else if(Action == MENU)
		GameClient()->m_Menus.SetActive(true);
	else if(Action == CHAT)
		GameClient()->m_Chat.EnableMode(0);
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
	if(!ControlsAvailable())
	{
		m_Touch.Cancel();
		return false;
	}
	const vec2 Screen = ScreenSize();
	const bool WasMoving = m_Touch.JoystickDirection() != vec2(0.0f, 0.0f);
	const unsigned Actions = m_Touch.Update(vFingers, Layout(Screen), Screen);
	for(int i = 0; i < NUM_ACTIONS; ++i)
		if((Actions & (1U << i)) != 0)
			PerformAction(i);
	if(m_Touch.ZoomFactor() != 1.0f)
	{
		// 连续倍率转为现有缩放命令的档位，复用自动旁观、多人取景和缩放限制。
		char aCommand[64];
		str_format(aCommand, sizeof(aCommand), "zoom+ %.6f", std::log(m_Touch.ZoomFactor()) / std::log(CCamera::ZOOM_STEP));
		Console()->ExecuteLine(aCommand, IConsole::CLIENT_ID_UNSPECIFIED);
	}
	const vec2 Direction = m_Touch.JoystickDirection();
	if(Direction != vec2(0.0f, 0.0f) && ControlsAvailable() &&
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
			Controls.m_aMousePos[g_Config.m_ClDummy] += Movement(Direction, GameClient()->m_Camera.m_Zoom, Client()->RenderFrameTime());
			Controls.m_aMouseInputType[g_Config.m_ClDummy] = CControls::EMouseInputType::RELATIVE;
			Controls.ClampMousePos();
		}
	}
	vFingers.clear();
	return true;
}

void CQmMobileSpectator::ShowMediaFiles(bool Videos)
{
#if defined(CONF_PLATFORM_ANDROID)
	using namespace qmclient::media_paths;
	const auto Kind = Videos ? EKind::VIDEOS : EKind::DEMOS;
	std::string Root;
	if(!TryOpenMedia<RECORDER_MAX>(Root, [&] { return CompletePath(Storage(), Directory(Storage(), g_Config, Kind)); }, [&](size_t i) { return DemoRecorder(static_cast<int>(i))->IsRecording(); }, [&](size_t i) { return CompletePath(Storage(), DemoRecorder(static_cast<int>(i))->CurrentFilename()); }, [&](const char *pRoot, const char *const *ppExcluded, size_t Count) { return ShowAndroidMediaFiles(pRoot, Videos, ppExcluded, Count); }))
	{
		try
		{
			char aMessage[IO_MAX_PATH_LENGTH + 64];
			str_format(aMessage, sizeof(aMessage), Localize("Failed to open the directory '%s'.", "Editor"), Root.c_str());
			Client()->AddWarning(SWarning(Localize("Error"), aMessage));
		}
		catch(...)
		{
			// 失败提示本身也可能分配失败，不能再抛回UI。
		}
	}
#else
	(void)Videos;
#endif
}

void CQmMobileSpectator::OnRender()
{
	if(!ControlsAvailable())
	{
		m_Touch.Cancel();
		return;
	}
	const vec2 Screen = ScreenSize();
	const auto Geometry = Layout(Screen);
	Graphics()->MapScreen(0.0f, 0.0f, Screen.x, Screen.y);
	Geometry.m_Joystick.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f), IGraphics::CORNER_ALL, 52.0f);
	const vec2 Knob = Geometry.m_Joystick.Center() + m_Touch.JoystickDirection() * 34.0f;
	const CUIRect KnobRect = {Knob.x - 18.0f, Knob.y - 18.0f, 36.0f, 36.0f};
	KnobRect.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.32f), IGraphics::CORNER_ALL, 18.0f);
	using namespace FontIcons;
	const char *apLabels[] = {FONT_ICON_BACKWARD_STEP, FONT_ICON_USERS, FONT_ICON_FORWARD_STEP, FONT_ICON_ARROWS_LEFT_RIGHT,
		FONT_ICON_GEAR, FONT_ICON_COMMENT,
		Client()->State() == IClient::STATE_DEMOPLAYBACK && DemoPlayer()->BaseInfo()->m_Paused ? FONT_ICON_PLAY : FONT_ICON_PAUSE};
	for(int i = 0; i < NUM_ACTIONS; ++i)
	{
		if(i == PAUSE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
			continue;
		const auto &Rect = Geometry.m_aButtons[i];
		Rect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 12.0f);
		SLabelProperties Props;
		Props.m_StopAtEnd = true;
		Props.m_MinimumFontSize = 6.0f;
		GameClient()->m_TouchControls.RenderButtonLabel({CTouchControls::CButtonLabel::EType::ICON, apLabels[i]}, Rect, 18.0f, TEXTALIGN_MC, Props);
	}
}
