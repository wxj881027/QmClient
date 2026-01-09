/* Q1menG Client - Pie Menu Component */

#include "pie_menu.h"

#include "game/localization.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/gameclient.h>
#include <game/client/render.h>

#include <cmath>

CPieMenu::CPieMenu()
{
	OnReset();
}

void CPieMenu::OnReset()
{
	m_State = EMenuState::INACTIVE;
	m_Active = false;
	m_TargetClientId = -1;
	m_SelectedOption = -1;
	m_AnimationProgress = 0.0f;
	m_MenuCenter = vec2(0, 0);
	m_OpenTime = 0;
	m_WasPressed = false;
	m_SelectorMouse = vec2(0, 0);
}

void CPieMenu::OnInit()
{
}

void CPieMenu::OnConsoleInit()
{
	Console()->Register("+pie_menu", "", CFGFLAG_CLIENT, ConKeyPieMenu, this, "Open pie menu");
}

void CPieMenu::OnRelease()
{
	if(m_Active)
	{
		CloseMenu();
	}
}

void CPieMenu::ConKeyPieMenu(IConsole::IResult *pResult, void *pUserData)
{
	CPieMenu *pSelf = (CPieMenu *)pUserData;
	
	if(pResult->GetInteger(0) != 0)
	{
		// Key pressed - open menu
		pSelf->OpenMenu();
	}
	else
	{
		// Key released - execute and close menu
		if(pSelf->m_Active)
		{
			if(pSelf->m_SelectedOption >= 0 && pSelf->m_SelectedOption < (int)EMenuOption::NUM_OPTIONS)
			{
				pSelf->ExecuteOption((EMenuOption)pSelf->m_SelectedOption);
			}
			pSelf->CloseMenu();
		}
	}
}

// ========== Player Detection ==========

int CPieMenu::FindNearestPlayer()
{
	if(!g_Config.m_QmPieMenuEnabled)
		return -1;

	// Get crosshair world position
	vec2 CursorWorldPos = GameClient()->m_CursorInfo.WorldTarget();
	
	int NearestClientId = -1;
	float NearestDistanceSq = (float)g_Config.m_QmPieMenuMaxDistance * g_Config.m_QmPieMenuMaxDistance;
	
	const int LocalClientId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];

	// Iterate through all players
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		// Skip invalid players
		if(!GameClient()->m_Snap.m_aCharacters[i].m_Active)
			continue;
		
		// Skip local player
		if(i == LocalClientId)
			continue;

		// Skip spectators
		if(GameClient()->m_Snap.m_apPlayerInfos[i] && 
		   GameClient()->m_Snap.m_apPlayerInfos[i]->m_Team == TEAM_SPECTATORS)
			continue;

		// Get player position
		vec2 PlayerPos = vec2(
			GameClient()->m_aClients[i].m_RenderPos.x,
			GameClient()->m_aClients[i].m_RenderPos.y
		);

		// Calculate squared distance (avoid sqrt for performance)
		float DistanceSq = length_squared(CursorWorldPos - PlayerPos);

		// Check if closer than current nearest
		if(DistanceSq < NearestDistanceSq)
		{
			NearestDistanceSq = DistanceSq;
			NearestClientId = i;
		}
	}

	return NearestClientId;
}

// ========== Menu State Management ==========

void CPieMenu::OpenMenu()
{
	if(m_Active)
		return;

	// Don't open if chat is active
	if(GameClient()->m_Chat.IsActive())
		return;

	// Don't open if console is active
	if(GameClient()->m_GameConsole.IsActive())
		return;

	// Find nearest player
	int TargetId = FindNearestPlayer();
	if(TargetId < 0)
	{
		// Show "No player nearby" message
		GameClient()->Echo(TCLocalize("没有玩家在准心附近"));
		return;
	}

	m_TargetClientId = TargetId;
	m_Active = true;
	m_State = EMenuState::OPENING;
	m_SelectedOption = -1;
	m_AnimationProgress = 0.0f;
	m_OpenTime = time_get();
	m_WasPressed = true;
	m_SelectorMouse = vec2(0, 0); // Reset selector mouse position

	// Set menu center to screen center
	m_MenuCenter = vec2(Graphics()->ScreenWidth() / 2.0f, Graphics()->ScreenHeight() / 2.0f);
}

void CPieMenu::CloseMenu()
{
	if(!m_Active)
		return;

	m_Active = false;
	m_State = EMenuState::CLOSING;
	m_TargetClientId = -1;
	m_SelectedOption = -1;
}

// ========== Input Handling ==========

bool CPieMenu::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	m_SelectorMouse += vec2(x, y);
	return true;
}

bool CPieMenu::OnInput(const IInput::CEvent &Event)
{
	if(!g_Config.m_QmPieMenuEnabled)
		return false;

	// Handle keyboard shortcuts (1-6) and ESC when menu is active
	if(m_Active && (Event.m_Flags & IInput::FLAG_PRESS))
	{
		if(Event.m_Key >= KEY_1 && Event.m_Key <= KEY_6)
		{
			int OptionIndex = Event.m_Key - KEY_1;
			if(OptionIndex < (int)EMenuOption::NUM_OPTIONS)
			{
				ExecuteOption((EMenuOption)OptionIndex);
				CloseMenu();
				return true;
			}
		}
		else if(Event.m_Key == KEY_ESCAPE)
		{
			CloseMenu();
			return true;
		}
	}

	return false;
}

void CPieMenu::UpdateSelection()
{
	if(!m_Active)
		return;

	// Check if mouse is in center (cancel zone)
	if(IsMouseInCenter())
	{
		m_SelectedOption = -1;
		return;
	}

	// Get hovered option
	m_SelectedOption = GetHoveredOption();
}

// ========== Rendering ==========

void CPieMenu::OnRender()
{
	if(!m_Active || !g_Config.m_QmPieMenuEnabled)
		return;

	// Update animation
	float TimeSinceOpen = (time_get() - m_OpenTime) / (float)time_freq();
	if(m_State == EMenuState::OPENING)
	{
		m_AnimationProgress = minimum(maximum(TimeSinceOpen / ANIMATION_DURATION, 0.0f), 1.0f);
		if(m_AnimationProgress >= 1.0f)
		{
			m_State = EMenuState::ACTIVE;
		}
	}

	// Update selection based on mouse position
	UpdateSelection();

	const vec2 ScreenCenter = m_MenuCenter;
	float Scale = mix(MIN_SCALE, MAX_SCALE, m_AnimationProgress);
	float ConfigScale = g_Config.m_QmPieMenuScale / 100.0f;  // User scale from config
	float Alpha = m_AnimationProgress * (g_Config.m_QmPieMenuOpacity / 100.0f);
	float InnerRadius = INNER_RADIUS * Scale * ConfigScale;
	float OuterRadius = OUTER_RADIUS * Scale * ConfigScale;

	Graphics()->MapScreen(0, 0, Graphics()->ScreenWidth(), Graphics()->ScreenHeight());

	// Render each sector (pie slice)
	for(int i = 0; i < (int)EMenuOption::NUM_OPTIONS; i++)
	{
		bool Highlighted = (i == m_SelectedOption);
		RenderSector(i, InnerRadius, OuterRadius, Highlighted, Alpha);
	}

	// Draw center circle for player name
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.15f, 0.15f, 0.2f, 0.9f * Alpha);
	Graphics()->DrawCircle(ScreenCenter.x, ScreenCenter.y, InnerRadius - 9.0f, 48);  // 5 * 1.8
	Graphics()->QuadsEnd();

	// Render center info (player name)
	RenderCenterInfo();
	
	// Render cursor
	RenderTools()->RenderCursor(ScreenCenter + m_SelectorMouse, 43.0f, Alpha);  // 24 * 1.8
}

void CPieMenu::RenderOverlay()
{
	// Not used - sectors provide the background
}

void CPieMenu::RenderSector(int Index, float InnerRadius, float OuterRadius, bool Highlighted, float Alpha)
{
	if(Index < 0 || Index >= (int)EMenuOption::NUM_OPTIONS)
		return;

	float HighlightScale = Highlighted ? 1.12f : 1.0f;
	float ActualOuterRadius = OuterRadius * HighlightScale;

	// Calculate sector angles
	float AnglePerSector = 360.0f / (int)EMenuOption::NUM_OPTIONS;
	float StartAngle = START_ANGLE + AnglePerSector * Index + SECTOR_GAP / 2.0f;
	float EndAngle = StartAngle + AnglePerSector - SECTOR_GAP;

	// Get option color
	ColorRGBA Color = GetOptionColor((EMenuOption)Index, Highlighted);

	// Draw sector using triangle fan
	const int Segments = 24;
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(Color.r, Color.g, Color.b, Color.a * Alpha);

	for(int i = 0; i < Segments; i++)
	{
		float Angle1 = StartAngle + (EndAngle - StartAngle) * (i / (float)Segments);
		float Angle2 = StartAngle + (EndAngle - StartAngle) * ((i + 1) / (float)Segments);

		float Rad1 = Angle1 * pi / 180.0f;
		float Rad2 = Angle2 * pi / 180.0f;

		vec2 Inner1 = m_MenuCenter + vec2(cos(Rad1), sin(Rad1)) * InnerRadius;
		vec2 Outer1 = m_MenuCenter + vec2(cos(Rad1), sin(Rad1)) * ActualOuterRadius;
		vec2 Inner2 = m_MenuCenter + vec2(cos(Rad2), sin(Rad2)) * InnerRadius;
		vec2 Outer2 = m_MenuCenter + vec2(cos(Rad2), sin(Rad2)) * ActualOuterRadius;

		IGraphics::CFreeformItem Freeform(
			Inner1.x, Inner1.y,
			Outer1.x, Outer1.y,
			Inner2.x, Inner2.y,
			Outer2.x, Outer2.y
		);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	}
	Graphics()->QuadsEnd();

	// Draw icon and text in sector center
	float MidRadius = (InnerRadius + ActualOuterRadius) / 2.0f;
	float MidAngle = (StartAngle + EndAngle) / 2.0f * pi / 180.0f;
	vec2 ItemPos = m_MenuCenter + vec2(cos(MidAngle), sin(MidAngle)) * MidRadius;

	// Draw icon
	const char *pIcon = GetOptionIcon((EMenuOption)Index);
	float IconSize = Highlighted ? 58.0f : 47.0f;  // 32/26 * 1.8
	
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
	float IconWidth = TextRender()->TextWidth(IconSize, pIcon);
	TextRender()->Text(ItemPos.x - IconWidth / 2.0f, ItemPos.y - IconSize / 2.0f - 18.0f, IconSize, pIcon);  // 10 * 1.8

	// Draw label below icon
	const char *pName = GetOptionName((EMenuOption)Index);
	float TextSize = Highlighted ? 29.0f : 23.0f;  // 16/13 * 1.8
	float TextWidth = TextRender()->TextWidth(TextSize, pName);
	TextRender()->Text(ItemPos.x - TextWidth / 2.0f, ItemPos.y + 14.0f, TextSize, pName);  // 8 * 1.8
}

void CPieMenu::RenderCenterInfo()
{
	if(m_TargetClientId < 0 || m_TargetClientId >= MAX_CLIENTS)
		return;

	// Draw player name in center - scaled 1.8x
	char aNameBuf[MAX_NAME_LENGTH];
	GameClient()->FormatStreamerName(m_TargetClientId, aNameBuf, sizeof(aNameBuf));
	const char *pName = aNameBuf;
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	
	float FontSize = 32.0f;  // 18 * 1.8
	float TextWidth = TextRender()->TextWidth(FontSize, pName);
	TextRender()->Text(m_MenuCenter.x - TextWidth / 2.0f, m_MenuCenter.y - FontSize / 2.0f, FontSize, pName);
}

// ========== Helper Methods ==========

vec2 CPieMenu::GetSectorPosition(int Index, float Radius) const
{
	float Angle = GetSectorAngle(Index);
	float Rad = Angle * pi / 180.0f;
	return m_MenuCenter + vec2(cos(Rad), sin(Rad)) * Radius;
}

float CPieMenu::GetSectorAngle(int Index) const
{
	float AnglePerSector = 360.0f / (int)EMenuOption::NUM_OPTIONS;
	return START_ANGLE + AnglePerSector * (Index + 0.5f);
}

const char *CPieMenu::GetOptionName(EMenuOption Option) const
{
	switch(Option)
	{
	case EMenuOption::FRIEND: return "好友";
	case EMenuOption::WHISPER: return "私聊";
	case EMenuOption::MENTION: return "提及";
	case EMenuOption::COPY_SKIN: return "复制皮肤";
	case EMenuOption::SWAP: return "交换";
	case EMenuOption::SPECTATE: return "观战";
	default: return "";
	}
}

const char *CPieMenu::GetOptionIcon(EMenuOption Option) const
{
	switch(Option)
	{
	case EMenuOption::FRIEND: return "♥"; // Heart
	case EMenuOption::WHISPER: return "✉";
	case EMenuOption::MENTION: return "➤";
	case EMenuOption::COPY_SKIN: return "⚡";
	case EMenuOption::SWAP: return "⇄";
	case EMenuOption::SPECTATE: return "👁";
	default: return "";
	}
}

ColorRGBA CPieMenu::GetOptionColor(EMenuOption Option, bool Highlighted) const
{
	// Get colors from config (RGBA format with alpha)
	unsigned int ConfigColor = 0;
	
	switch(Option)
	{
	case EMenuOption::FRIEND:
		ConfigColor = g_Config.m_QmPieMenuColorFriend;
		break;
	case EMenuOption::WHISPER:
		ConfigColor = g_Config.m_QmPieMenuColorWhisper;
		break;
	case EMenuOption::MENTION:
		ConfigColor = g_Config.m_QmPieMenuColorMention;
		break;
	case EMenuOption::COPY_SKIN:
		ConfigColor = g_Config.m_QmPieMenuColorCopySkin;
		break;
	case EMenuOption::SWAP:
		ConfigColor = g_Config.m_QmPieMenuColorSwap;
		break;
	case EMenuOption::SPECTATE:
		ConfigColor = g_Config.m_QmPieMenuColorSpectate;
		break;
	default:
		ConfigColor = 0x4D6680BF;
	}

	// Parse RGBA from config (format: 0xRRGGBBAA)
	ColorRGBA BaseColor(
		((ConfigColor >> 24) & 0xFF) / 255.0f,
		((ConfigColor >> 16) & 0xFF) / 255.0f,
		((ConfigColor >> 8) & 0xFF) / 255.0f,
		(ConfigColor & 0xFF) / 255.0f
	);

	if(Highlighted)
	{
		// Brighten and increase alpha when highlighted
		BaseColor.r = minimum(BaseColor.r * 1.3f, 1.0f);
		BaseColor.g = minimum(BaseColor.g * 1.3f, 1.0f);
		BaseColor.b = minimum(BaseColor.b * 1.3f, 1.0f);
		BaseColor.a = minimum(BaseColor.a * 1.2f, 1.0f);
	}

	return BaseColor;
}

bool CPieMenu::IsMouseInCenter() const
{
	float Scale = mix(MIN_SCALE, MAX_SCALE, m_AnimationProgress);
	float InnerRadius = INNER_RADIUS * Scale;

	return length(m_SelectorMouse) < InnerRadius;
}

int CPieMenu::GetHoveredOption() const
{
	float MouseAngle = atan2(m_SelectorMouse.y, m_SelectorMouse.x) * 180.0f / pi;
	
	// Normalize angle to 0-360 range
	while(MouseAngle < 0)
		MouseAngle += 360.0f;
	while(MouseAngle >= 360.0f)
		MouseAngle -= 360.0f;

	// Adjust for start angle
	float AdjustedAngle = MouseAngle - START_ANGLE;
	
	// Normalize adjusted angle to 0-360 range
	while(AdjustedAngle < 0)
		AdjustedAngle += 360.0f;
	while(AdjustedAngle >= 360.0f)
		AdjustedAngle -= 360.0f;

	// Calculate sector index
	float AnglePerSector = 360.0f / (int)EMenuOption::NUM_OPTIONS;
	int SectorIndex = (int)(AdjustedAngle / AnglePerSector);

	if(SectorIndex >= 0 && SectorIndex < (int)EMenuOption::NUM_OPTIONS)
		return SectorIndex;

	return -1;
}

// ========== Option Execution ==========

void CPieMenu::ExecuteOption(EMenuOption Option)
{
	if(m_TargetClientId < 0 || m_TargetClientId >= MAX_CLIENTS)
		return;

	const char *pPlayerName = GameClient()->m_aClients[m_TargetClientId].m_aName;
	const char *pPlayerClan = GameClient()->m_aClients[m_TargetClientId].m_aClan;

	switch(Option)
	{
	case EMenuOption::FRIEND:
	{
		// Toggle friend status using console command (more reliable)
		char aBuf[256];
		if(GameClient()->m_aClients[m_TargetClientId].m_Friend)
		{
			str_format(aBuf, sizeof(aBuf), "remove_friend \"%s\" \"%s\"", pPlayerName, pPlayerClan);
			Console()->ExecuteLine(aBuf);
			
			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), "Removed %s from friends", pPlayerName);
			GameClient()->m_Chat.AddLine(-2, 0, aMsg);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "add_friend \"%s\" \"%s\"", pPlayerName, pPlayerClan);
			Console()->ExecuteLine(aBuf);
			
			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), "Added %s to friends", pPlayerName);
			GameClient()->m_Chat.AddLine(-2, 0, aMsg);
		}
		break;
	}
	case EMenuOption::WHISPER:
	{
		// Open chat with whisper command
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "/w \"%s\" ", pPlayerName);
		GameClient()->m_Chat.EnableMode(0);
		GameClient()->m_Chat.m_Input.Set(aBuf);
		break;
	}
	case EMenuOption::MENTION:
	{
		// Insert player name in chat (for @mention)
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s: ", pPlayerName);
		GameClient()->m_Chat.EnableMode(0);
		GameClient()->m_Chat.m_Input.Set(aBuf);
		break;
	}
	case EMenuOption::COPY_SKIN:
	{
		// Copy player skin to local config (supports both main player and dummy)
		const auto &TargetClient = GameClient()->m_aClients[m_TargetClientId];
		const bool IsDummy = g_Config.m_ClDummy != 0;
		
		// Copy skin name to appropriate config
		if(IsDummy)
		{
			str_copy(g_Config.m_ClDummySkin, TargetClient.m_aSkinName, sizeof(g_Config.m_ClDummySkin));
			
			// Copy custom colors if used
			if(TargetClient.m_UseCustomColor)
			{
				g_Config.m_ClDummyUseCustomColor = 1;
				g_Config.m_ClDummyColorBody = TargetClient.m_ColorBody;
				g_Config.m_ClDummyColorFeet = TargetClient.m_ColorFeet;
			}
			else
			{
				g_Config.m_ClDummyUseCustomColor = 0;
			}
		}
		else
		{
			str_copy(g_Config.m_ClPlayerSkin, TargetClient.m_aSkinName, sizeof(g_Config.m_ClPlayerSkin));
			
			// Copy custom colors if used
			if(TargetClient.m_UseCustomColor)
			{
				g_Config.m_ClPlayerUseCustomColor = 1;
				g_Config.m_ClPlayerColorBody = TargetClient.m_ColorBody;
				g_Config.m_ClPlayerColorFeet = TargetClient.m_ColorFeet;
			}
			else
			{
				g_Config.m_ClPlayerUseCustomColor = 0;
			}
		}
		
		// Send skin change to server
		if(IsDummy)
			GameClient()->SendDummyInfo(false);
		else
			GameClient()->SendInfo(false);
		
		// Show notification
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "已复制 %s 的皮肤%s", pPlayerName, IsDummy ? " (Dummy)" : "");
		GameClient()->m_Chat.AddLine(-2, 0, aBuf);
		break;
	}
	case EMenuOption::SWAP:
	{
		// Check if target is in the same team
		const int LocalClientId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
		int LocalTeam = GameClient()->m_Teams.Team(LocalClientId);
		int TargetTeam = GameClient()->m_Teams.Team(m_TargetClientId);
		
		if(LocalTeam != TargetTeam)
		{
			GameClient()->m_Chat.AddLine(-2, 0, "Cannot swap: Player is not in your team");
			break;
		}
		
		// Execute swap command with player name
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "/swap \"%s\"", pPlayerName);
		GameClient()->m_Chat.SendChat(0, aBuf);
		break;
	}
	case EMenuOption::SPECTATE:
	{
		// Spectate the player
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "/spec \"%s\"", pPlayerName);
		GameClient()->m_Chat.SendChat(0, aBuf);
		break;
	}
	default:
		break;
	}
}
