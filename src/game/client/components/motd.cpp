/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "motd.h"

#include "motd_message.h"

#include <base/log_color.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/important_alert.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <chrono>

CMotd::CMotd()
{
	m_aServerMotd[0] = '\0';
	m_ServerMotdTime = 0;
	m_ServerMotdUpdateTime = 0;
}

void CMotd::Clear()
{
	m_ServerMotdTime = 0;
	m_TouchRect.reset();
	Graphics()->DeleteQuadContainer(m_RectQuadContainer);
	TextRender()->DeleteTextContainer(m_TextContainerIndex);
}

bool CMotd::IsActive() const
{
	return time() < m_ServerMotdTime;
}

void CMotd::OnStateChange(int NewState, int OldState)
{
	if(OldState == IClient::STATE_ONLINE || OldState == IClient::STATE_OFFLINE)
		Clear();
}

void CMotd::OnWindowResize()
{
	Graphics()->DeleteQuadContainer(m_RectQuadContainer);
	TextRender()->DeleteTextContainer(m_TextContainerIndex);
}

void CMotd::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!IsActive())
		return;

	if(GameClient()->m_ImportantAlert.IsActive())
	{
		Clear();
		return;
	}

	const int MaxLines = 24;
	const float FontSize = 32.0f; // also the size of the margin and rect rounding
	const float ScreenHeight = 40.0f * FontSize; // multiple of the font size to get perfect alignment
	const float ScreenWidth = ScreenHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, ScreenWidth, ScreenHeight);

	const float RectHeight = (MaxLines + 2) * FontSize;
	const float RectWidth = 630.0f + 2.0f * FontSize;
	const float RectX = ScreenWidth / 2.0f - RectWidth / 2.0f;
	const float RectY = 160.0f;
	m_TouchRect = CUIRect{RectX / ScreenWidth, RectY / ScreenHeight, RectWidth / ScreenWidth, RectHeight / ScreenHeight};

	if(m_RectQuadContainer == -1)
	{
		Graphics()->SetColor(ui_token::color::SURFACE_ELEVATED);
		m_RectQuadContainer = Graphics()->CreateRectQuadContainer(RectX, RectY, RectWidth, RectHeight, FontSize, IGraphics::CORNER_ALL);
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	if(m_RectQuadContainer != -1)
	{
		Ui()->RenderGaussianBlur({RectX, RectY, RectWidth, RectHeight}, 1.0f, IGraphics::CORNER_ALL, FontSize);
		Graphics()->TextureClear();
		Graphics()->RenderQuadContainer(m_RectQuadContainer, -1);
	}

	if(!m_TextContainerIndex.Valid())
	{
		CTextCursor Cursor;
		Cursor.SetPosition(vec2(RectX + FontSize, RectY + FontSize));
		// TODO: Set TEXTFLAG_ELLIPSIS_AT_END when https://github.com/ddnet/ddnet/issues/11419 is fixed
		// Cursor.m_Flags |= TEXTFLAG_ELLIPSIS_AT_END;
		Cursor.m_FontSize = FontSize;
		Cursor.m_LineWidth = RectWidth - 2.0f * FontSize;
		Cursor.m_MaxLines = MaxLines;
		TextRender()->CreateTextContainer(m_TextContainerIndex, &Cursor, ServerMotd());
	}

	if(m_TextContainerIndex.Valid())
		TextRender()->RenderTextContainer(m_TextContainerIndex, TextRender()->DefaultTextColor(), TextRender()->DefaultTextOutlineColor());
}

void CMotd::OnMessage(int MsgType, void *pRawMsg)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	if(MsgType == NETMSGTYPE_SV_MOTD)
	{
		const CNetMsg_Sv_Motd *pMsg = static_cast<CNetMsg_Sv_Motd *>(pRawMsg);

		const std::string_view Message = DecodeMotdMessage(pMsg->m_pMessage, m_aServerMotd, sizeof(m_aServerMotd));
		const LOG_COLOR LogColor = color_cast<LOG_COLOR>(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor)));
		if(g_Config.m_ClPrintMotd)
		{
			ForEachMotdLine(Message, [&](std::string_view Line) {
				log_info_color(LogColor, "motd", "%.*s", static_cast<int>(Line.size()), Line.data());
			});
		}

		if(!IsActive())
			m_ShownSince = time_get_nanoseconds();
		m_ServerMotdUpdateTime = time();
		if(ShouldActivateMotdPopup(m_aServerMotd, g_Config.m_ClMotdTime, g_Config.m_QmHideJoinServerInfo != 0))
			m_ServerMotdTime = m_ServerMotdUpdateTime + time_freq() * g_Config.m_ClMotdTime;
		else
			m_ServerMotdTime = 0;
		TextRender()->DeleteTextContainer(m_TextContainerIndex);
	}
}

bool CMotd::OnInput(const IInput::CEvent &Event)
{
	if(IsActive() && Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		Clear();
		return true;
	}
	return false;
}

bool CMotd::OnTouchState(std::vector<IInput::CTouchFingerState> &vTouchFingerStates)
{
	if(m_DismissTouchFinger.has_value())
	{
		const auto DismissFingerState = std::find_if(vTouchFingerStates.begin(), vTouchFingerStates.end(), [&](const IInput::CTouchFingerState &State) {
			return State.m_Finger == *m_DismissTouchFinger;
		});
		if(DismissFingerState == vTouchFingerStates.end())
			m_DismissTouchFinger.reset();
		else
			vTouchFingerStates.erase(DismissFingerState);
	}

	if(!IsActive() ||
		GameClient()->m_Chat.IsActive() ||
		GameClient()->m_GameConsole.IsActive() ||
		GameClient()->m_Menus.IsActive() ||
		GameClient()->m_Emoticon.IsActive() ||
		GameClient()->m_Spectator.IsActive() ||
		!m_TouchRect.has_value())
	{
		return false;
	}

	const auto DismissFinger = std::find_if(vTouchFingerStates.begin(), vTouchFingerStates.end(), [&](const IInput::CTouchFingerState &State) {
		return State.m_PressTime > m_ShownSince && !m_TouchRect->Inside(State.m_Position);
	});
	if(DismissFinger == vTouchFingerStates.end())
		return false;

	Clear();
	m_DismissTouchFinger = DismissFinger->m_Finger;
	vTouchFingerStates.erase(DismissFinger);
	return false;
}
