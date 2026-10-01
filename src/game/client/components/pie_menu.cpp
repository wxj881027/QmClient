// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* Q1menG Client - Pie Menu Component */

#include "pie_menu.h"

#include "game/localization.h"

#include <base/math.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/friends.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/gameclient.h>
#include <game/client/render.h>

#include <cmath>
#include <limits>
#include <string>

CPieMenu::CPieMenu()
{
	OnReset();
}

void CPieMenu::OnReset()
{
	m_State = EMenuState::INACTIVE;
	m_Active = false;
	m_TargetClientId = -1;
	m_TargetName.clear();
	m_TargetClan.clear();
	m_SelectedOption = -1;
	m_SelectedRenameIndex = -1;
	m_AnimationProgress = 0.0f;
	m_MenuCenter = vec2(0, 0);
	m_OpenTime = 0;
	m_WasPressed = false;
	m_SelectorMouse = vec2(0, 0);
	m_vRenameQueue.clear();
	m_VisibleOptionCount = 0;
}

void CPieMenu::OnInit()
{
	qm_pie_menu::StartFollow(m_FollowState, g_Config.m_QmPieFollowName, g_Config.m_QmPieFollowClan);
}

void CPieMenu::OnConsoleInit()
{
	Console()->Register("+pie_menu", "", CFGFLAG_CLIENT, ConKeyPieMenu, this, "Open pie menu");
	Console()->Register("qm_pie_menu_stop_follow", "", CFGFLAG_CLIENT, ConStopFollow, this, "Stop following a player across servers");
}

void CPieMenu::OnRelease()
{
	if(m_Active)
	{
		CloseMenu();
	}
}

void CPieMenu::OnUpdate()
{
	if(m_Active && !g_Config.m_QmPieMenuEnabled)
		CloseMenu();
	UpdatePointsRequest();
	UpdateFollowState();
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
			if(pSelf->m_SelectedRenameIndex >= 0 && pSelf->m_SelectedRenameIndex < (int)pSelf->m_vRenameQueue.size())
			{
				pSelf->ExecuteRenameOption(pSelf->m_SelectedRenameIndex);
			}
			else if(pSelf->m_SelectedOption >= 0 && pSelf->m_SelectedOption < pSelf->VisibleOptionCount())
			{
				pSelf->ExecuteOption(pSelf->VisibleOption(pSelf->m_SelectedOption));
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

	const bool Spectating = GameClient()->m_Snap.m_SpecInfo.m_Active ||
				(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS);

	// Use crosshair target when playing, screen center when spectating.
	vec2 ReferencePos = Spectating ? GameClient()->m_Camera.m_Center : GameClient()->m_CursorInfo.WorldTarget();

	int NearestClientId = -1;
	float NearestDistanceSq = Spectating ? std::numeric_limits<float>::max() :
					       (float)g_Config.m_QmPieMenuMaxDistance * g_Config.m_QmPieMenuMaxDistance;

	// Iterate through all players
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		// Skip invalid players
		if(!GameClient()->m_Snap.m_aCharacters[i].m_Active)
			continue;

		// Both local connections belong to the user and are not interaction targets.
		if(GameClient()->IsLocalClientId(i))
			continue;

		// Skip spectators
		if(GameClient()->m_Snap.m_apPlayerInfos[i] &&
			GameClient()->m_Snap.m_apPlayerInfos[i]->m_Team == TEAM_SPECTATORS)
			continue;

		// Get player position
		vec2 PlayerPos = vec2(
			GameClient()->m_aClients[i].m_RenderPos.x,
			GameClient()->m_aClients[i].m_RenderPos.y);

		// Calculate squared distance (avoid sqrt for performance)
		float DistanceSq = length_squared(ReferencePos - PlayerPos);

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
	if(!g_Config.m_QmPieMenuEnabled || Client()->State() != IClient::STATE_ONLINE)
		return;

	const bool UseDummy = g_Config.m_ClDummy && Client()->DummyConnected();
	const int LocalClientId = GameClient()->m_aLocalIds[UseDummy ? 1 : 0];
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS)
		return;

	// Don't open if chat is active
	if(GameClient()->m_Chat.IsActive())
		return;

	// Don't open if console is active
	if(GameClient()->m_GameConsole.IsActive())
		return;

	RefreshVisibleOptions();
	RefreshRenameQueue();
	if(VisibleOptionCount() == 0 && m_vRenameQueue.empty())
		return;
	const int TargetId = VisibleOptionCount() > 0 ? FindNearestPlayer() : -1;
	if(TargetId < 0 && m_vRenameQueue.empty())
	{
		// Neither the other-player ring nor the self-rename ring can be used.
		GameClient()->Echo(Localize("No player nearby"));
		return;
	}

	m_TargetClientId = TargetId;
	m_TargetName = TargetId >= 0 ? GameClient()->m_aClients[TargetId].m_aName : "";
	m_TargetClan = TargetId >= 0 ? GameClient()->m_aClients[TargetId].m_aClan : "";
	m_Active = true;
	m_State = EMenuState::OPENING;
	m_SelectedOption = -1;
	m_SelectedRenameIndex = -1;
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
	m_SelectedRenameIndex = -1;
	m_vRenameQueue.clear();
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

	// 数字键依次对应当前可见扇区，第十项使用 0。
	if(m_Active && (Event.m_Flags & IInput::FLAG_PRESS))
	{
		if(Event.m_Key >= KEY_1 && Event.m_Key <= KEY_0)
		{
			if(!HasTargetPlayer())
				return true;

			int OptionIndex = Event.m_Key - KEY_1;
			if(OptionIndex >= 0 && OptionIndex < VisibleOptionCount())
			{
				ExecuteOption(VisibleOption(OptionIndex));
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

	m_SelectedOption = -1;
	m_SelectedRenameIndex = -1;
	RefreshVisibleOptions();

	const float Scale = MenuScale();
	const float InnerRadius = INNER_RADIUS * Scale;
	const float OuterRadius = OUTER_RADIUS * Scale;
	const float SecondaryInnerRadius = SECONDARY_INNER_RADIUS * Scale;
	const float SecondaryOuterRadius = SECONDARY_OUTER_RADIUS * Scale;
	const float MouseDistance = length(m_SelectorMouse);

	// Check if mouse is in center (cancel zone)
	if(MouseDistance < InnerRadius)
	{
		return;
	}

	// Secondary ring for rename queue.
	if(!m_vRenameQueue.empty() && MouseDistance >= SecondaryInnerRadius && MouseDistance <= SecondaryOuterRadius)
	{
		m_SelectedRenameIndex = GetHoveredRenameOption();
		return;
	}

	// Primary ring.
	if(HasTargetPlayer() && VisibleOptionCount() > 0 && MouseDistance <= OuterRadius)
	{
		m_SelectedOption = GetHoveredOption();
	}
}

void CPieMenu::RefreshRenameQueue()
{
	m_vRenameQueue.clear();

	if(g_Config.m_QmPieMenuRenameQueue[0] == '\0')
		return;

	const char *pCursor = g_Config.m_QmPieMenuRenameQueue;
	char aBuf[128];
	while((pCursor = str_next_token(pCursor, "|", aBuf, sizeof(aBuf))))
	{
		char *pTrimmed = (char *)str_utf8_skip_whitespaces(aBuf);
		str_utf8_trim_right(pTrimmed);
		if(pTrimmed[0] == '\0')
			continue;
		m_vRenameQueue.emplace_back(pTrimmed);
	}
}

void CPieMenu::RefreshVisibleOptions()
{
	std::array<bool, qm_pie_menu::OPTION_COUNT> Enabled{};
	Enabled[static_cast<size_t>(EMenuOption::FRIEND)] = g_Config.m_QmPieMenuFriendEnabled != 0;
	Enabled[static_cast<size_t>(EMenuOption::WHISPER)] = g_Config.m_QmPieMenuWhisperEnabled != 0;
	Enabled[static_cast<size_t>(EMenuOption::MENTION)] = g_Config.m_QmPieMenuMentionEnabled != 0;
	Enabled[static_cast<size_t>(EMenuOption::COPY_SKIN)] = g_Config.m_QmPieMenuCopySkinEnabled != 0;
	Enabled[static_cast<size_t>(EMenuOption::SWAP)] = g_Config.m_QmPieMenuSwapEnabled != 0;
	Enabled[static_cast<size_t>(EMenuOption::SPECTATE)] = g_Config.m_QmPieMenuSpectateEnabled != 0;
	Enabled[static_cast<size_t>(EMenuOption::INVITE_TEAM)] = g_Config.m_QmPieMenuInviteTeamEnabled != 0;
	Enabled[static_cast<size_t>(EMenuOption::JOIN_TEAM)] = g_Config.m_QmPieMenuJoinTeamEnabled != 0;
	Enabled[static_cast<size_t>(EMenuOption::FOLLOW)] = g_Config.m_QmPieMenuFollowEnabled != 0;
	Enabled[static_cast<size_t>(EMenuOption::SCORE)] = g_Config.m_QmPieMenuScoreEnabled != 0;
	m_VisibleOptionCount = qm_pie_menu::BuildVisibleOptions(Enabled, m_vVisibleOptions);
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

	m_MenuCenter = vec2(Graphics()->ScreenWidth() * 0.5f, Graphics()->ScreenHeight() * 0.5f);
	const vec2 ScreenCenter = m_MenuCenter;
	float Scale = MenuScale();
	float Alpha = m_AnimationProgress * (g_Config.m_QmPieMenuOpacity / 100.0f);
	float InnerRadius = INNER_RADIUS * Scale;
	float OuterRadius = OUTER_RADIUS * Scale;
	float SecondaryInnerRadius = SECONDARY_INNER_RADIUS * Scale;
	float SecondaryOuterRadius = SECONDARY_OUTER_RADIUS * Scale;

	Graphics()->MapScreen(0, 0, Graphics()->ScreenWidth(), Graphics()->ScreenHeight());

	// The primary ring only applies to another player.
	if(HasTargetPlayer() && VisibleOptionCount() > 0)
	{
		for(int i = 0; i < VisibleOptionCount(); i++)
		{
			bool Highlighted = (i == m_SelectedOption);
			RenderSector(i, InnerRadius, OuterRadius, Highlighted, Alpha);
		}
	}

	// Render secondary ring for rename queue.
	if(!m_vRenameQueue.empty())
	{
		const int SectorCount = (int)m_vRenameQueue.size();
		for(int i = 0; i < SectorCount; i++)
		{
			bool Highlighted = (i == m_SelectedRenameIndex);
			RenderRenameSector(i, SectorCount, SecondaryInnerRadius, SecondaryOuterRadius, Highlighted, Alpha);
		}
	}

	// Draw center circle for player name
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.15f, 0.15f, 0.2f, 0.9f * Alpha);
	Graphics()->DrawCircle(ScreenCenter.x, ScreenCenter.y, InnerRadius - 9.0f * Scale, 48);
	Graphics()->QuadsEnd();

	// Render center info (player name)
	RenderCenterInfo();

	// Render cursor
	RenderTools()->RenderCursor(ScreenCenter + m_SelectorMouse, 43.0f, Alpha); // 24 * 1.8
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CPieMenu::RenderOverlay()
{
	// Not used - sectors provide the background
}

void CPieMenu::RenderSector(int Index, float InnerRadius, float OuterRadius, bool Highlighted, float Alpha)
{
	if(Index < 0 || Index >= VisibleOptionCount())
		return;
	const EMenuOption Option = VisibleOption(Index);

	float HighlightScale = Highlighted ? 1.12f : 1.0f;
	float ActualOuterRadius = OuterRadius * HighlightScale;

	// Calculate sector angles
	float AnglePerSector = 360.0f / VisibleOptionCount();
	float StartAngle = START_ANGLE + AnglePerSector * Index + SECTOR_GAP / 2.0f;
	float EndAngle = StartAngle + AnglePerSector - SECTOR_GAP;

	// Get option color
	ColorRGBA Color = GetOptionColor(Option, Highlighted);

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
			Outer2.x, Outer2.y);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	}
	Graphics()->QuadsEnd();

	// Draw icon and text in sector center
	float MidRadius = (InnerRadius + ActualOuterRadius) / 2.0f;
	float MidAngle = (StartAngle + EndAngle) / 2.0f * pi / 180.0f;
	vec2 ItemPos = m_MenuCenter + vec2(cos(MidAngle), sin(MidAngle)) * MidRadius;

	// Draw icon
	const char *pIcon = GetOptionIcon(Option);
	const float Scale = OuterRadius / OUTER_RADIUS;
	const float AvailableWidth = maximum(1.0f, 1.35f * MidRadius * sinf(minimum(AnglePerSector - SECTOR_GAP, 180.0f) * pi / 360.0f));
	float IconSize = minimum((Highlighted ? 54.0f : 45.0f) * Scale, AvailableWidth * 0.65f);

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
	const EFontPreset PreviousFont = TextRender()->GetFontPreset();
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	float IconWidth = TextRender()->TextWidth(IconSize, pIcon);
	TextRender()->Text(ItemPos.x - IconWidth / 2.0f, ItemPos.y - IconSize / 2.0f - 14.0f * Scale, IconSize, pIcon);
	TextRender()->SetFontPreset(PreviousFont);

	// Draw label below icon
	const char *pName = GetOptionName(Option);
	float TextSize = (Highlighted ? 25.0f : 23.0f) * Scale;
	float TextWidth = TextRender()->TextWidth(TextSize, pName);
	if(TextWidth > AvailableWidth)
	{
		TextSize *= AvailableWidth / TextWidth;
		TextWidth = TextRender()->TextWidth(TextSize, pName);
	}
	TextRender()->Text(ItemPos.x - TextWidth / 2.0f, ItemPos.y + 14.0f * Scale, TextSize, pName);
}

void CPieMenu::RenderRenameSector(int Index, int SectorCount, float InnerRadius, float OuterRadius, bool Highlighted, float Alpha)
{
	if(Index < 0 || Index >= SectorCount || SectorCount <= 0 || Index >= (int)m_vRenameQueue.size())
		return;

	float HighlightScale = Highlighted ? 1.06f : 1.0f;
	float ActualOuterRadius = OuterRadius * HighlightScale;

	float AnglePerSector = 360.0f / SectorCount;
	float DynamicGap = minimum(SECTOR_GAP, AnglePerSector * 0.35f);
	float StartAngle = START_ANGLE + AnglePerSector * Index + DynamicGap / 2.0f;
	float EndAngle = START_ANGLE + AnglePerSector * (Index + 1) - DynamicGap / 2.0f;

	if(EndAngle <= StartAngle)
		return;

	ColorRGBA Color = Highlighted ? ColorRGBA(0.36f, 0.75f, 0.52f, 0.95f) : ColorRGBA(0.28f, 0.58f, 0.43f, 0.78f);

	const int Segments = maximum(8, (int)((EndAngle - StartAngle) / 6.0f));
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
			Outer2.x, Outer2.y);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	}
	Graphics()->QuadsEnd();

	const char *pRenameName = m_vRenameQueue[Index].c_str();
	float MidRadius = (InnerRadius + ActualOuterRadius) / 2.0f;
	float MidAngle = (StartAngle + EndAngle) / 2.0f * pi / 180.0f;
	vec2 ItemPos = m_MenuCenter + vec2(cos(MidAngle), sin(MidAngle)) * MidRadius;

	float TextSize = (Highlighted ? 22.0f : 18.0f) * (OuterRadius / SECONDARY_OUTER_RADIUS);
	if(SectorCount > 8)
		TextSize *= 0.92f;
	if(SectorCount > 12)
		TextSize *= 0.85f;

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
	float TextWidth = TextRender()->TextWidth(TextSize, pRenameName);
	const float AvailableWidth = maximum(1.0f, minimum(OuterRadius - InnerRadius, MidRadius * sinf(minimum(AnglePerSector - DynamicGap, 180.0f) * pi / 360.0f)));
	if(TextWidth > AvailableWidth)
	{
		TextSize *= AvailableWidth / TextWidth;
		TextWidth = TextRender()->TextWidth(TextSize, pRenameName);
	}
	TextRender()->Text(ItemPos.x - TextWidth / 2.0f, ItemPos.y - TextSize / 2.0f, TextSize, pRenameName);
}

void CPieMenu::RenderCenterInfo()
{
	const bool UseDummy = g_Config.m_ClDummy && Client()->DummyConnected();
	const int LocalClientId = GameClient()->m_aLocalIds[UseDummy ? 1 : 0];
	const bool ShowTarget = VisibleOptionCount() > 0 && HasTargetPlayer();
	const int DisplayClientId = ShowTarget ? m_TargetClientId : LocalClientId;
	if(DisplayClientId < 0 || DisplayClientId >= MAX_CLIENTS)
		return;

	// Draw the interaction target, or self when only the rename ring is available.
	char aNameBuf[MAX_NAME_LENGTH];
	GameClient()->FormatStreamerName(DisplayClientId, aNameBuf, sizeof(aNameBuf));
	const float Scale = MenuScale();
	const float AvailableWidth = INNER_RADIUS * Scale * 1.6f;
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, m_AnimationProgress * g_Config.m_QmPieMenuOpacity / 100.0f);
	auto CenteredText = [&](const char *pText, float Size, float Offset) {
		float FontSize = Size * Scale;
		const float Width = TextRender()->TextWidth(FontSize, pText);
		if(Width > AvailableWidth)
			FontSize *= AvailableWidth / Width;
		const float TextWidth = TextRender()->TextWidth(FontSize, pText);
		TextRender()->Text(m_MenuCenter.x - TextWidth * 0.5f, m_MenuCenter.y + Offset * Scale - FontSize * 0.5f, FontSize, pText);
	};
	CenteredText(aNameBuf, 32.0f, -12.0f);

	if(m_SelectedRenameIndex >= 0 && m_SelectedRenameIndex < (int)m_vRenameQueue.size())
	{
		char aRenamePreview[128];
		str_format(aRenamePreview, sizeof(aRenamePreview), Localize("Rename: %s"), m_vRenameQueue[m_SelectedRenameIndex].c_str());
		char aPreview[128];
		str_format(aPreview, sizeof(aPreview), "%s · %s", Localize("Self"), aRenamePreview);
		CenteredText(aPreview, 18.0f, 24.0f);
	}
	else if(ShowTarget)
	{
		char aPoints[96];
		if(FormatTargetScore(aPoints, sizeof(aPoints)))
			CenteredText(aPoints, 20.0f, 24.0f);
	}
}

// ========== Helper Methods ==========

float CPieMenu::MenuScale() const
{
	const float MaximumRadius = m_vRenameQueue.empty() ? OUTER_RADIUS * 1.12f : SECONDARY_OUTER_RADIUS * 1.06f;
	const float FitScale = minimum(Graphics()->ScreenWidth(), Graphics()->ScreenHeight()) * 0.46f / MaximumRadius;
	return mix(MIN_SCALE, MAX_SCALE, m_AnimationProgress) * minimum(g_Config.m_QmPieMenuScale / 100.0f, FitScale);
}

vec2 CPieMenu::GetSectorPosition(int Index, float Radius) const
{
	float Angle = GetSectorAngle(Index);
	float Rad = Angle * pi / 180.0f;
	return m_MenuCenter + vec2(cos(Rad), sin(Rad)) * Radius;
}

float CPieMenu::GetSectorAngle(int Index) const
{
	float AnglePerSector = 360.0f / maximum(1, VisibleOptionCount());
	return START_ANGLE + AnglePerSector * (Index + 0.5f);
}

const char *CPieMenu::GetOptionName(EMenuOption Option) const
{
	switch(Option)
	{
	case EMenuOption::FRIEND: return Localize("Friends");
	case EMenuOption::WHISPER: return Localize("Whisper");
	case EMenuOption::MENTION: return Localize("Mention");
	case EMenuOption::COPY_SKIN: return Localize("Copy skin");
	case EMenuOption::SWAP: return Localize("Swap");
	case EMenuOption::SPECTATE: return Localize("Spectate");
	case EMenuOption::INVITE_TEAM: return Localize("Invite to team");
	case EMenuOption::JOIN_TEAM: return Localize("Join team");
	case EMenuOption::FOLLOW: return IsFollowingTarget() ? Localize("Stop following") : Localize("Follow server");
	case EMenuOption::SCORE: return Localize("View points");
	default: return "";
	}
}

const char *CPieMenu::GetOptionIcon(EMenuOption Option) const
{
	switch(Option)
	{
	case EMenuOption::FRIEND: return FontIcons::FONT_ICON_HEART;
	case EMenuOption::WHISPER: return FontIcons::FONT_ICON_COMMENT;
	case EMenuOption::MENTION: return FontIcons::FONT_ICON_CHEVRON_RIGHT;
	case EMenuOption::COPY_SKIN: return FontIcons::FONT_ICON_COPY;
	case EMenuOption::SWAP: return FontIcons::FONT_ICON_ARROWS_LEFT_RIGHT;
	case EMenuOption::SPECTATE: return FontIcons::FONT_ICON_EYE;
	case EMenuOption::INVITE_TEAM: return FontIcons::FONT_ICON_USERS;
	case EMenuOption::JOIN_TEAM: return FontIcons::FONT_ICON_RIGHT_TO_BRACKET;
	case EMenuOption::FOLLOW: return IsFollowingTarget() ? FontIcons::FONT_ICON_STOP : FontIcons::FONT_ICON_NETWORK_WIRED;
	case EMenuOption::SCORE: return FontIcons::FONT_ICON_MAGNIFYING_GLASS;
	default: return "";
	}
}

ColorRGBA CPieMenu::GetOptionColor(EMenuOption Option, bool Highlighted) const
{
	// Get colors from config (HSLA format)
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
	case EMenuOption::INVITE_TEAM:
		ConfigColor = g_Config.m_QmPieMenuColorInviteTeam;
		break;
	case EMenuOption::JOIN_TEAM:
		ConfigColor = g_Config.m_QmPieMenuColorJoinTeam;
		break;
	case EMenuOption::FOLLOW:
		ConfigColor = g_Config.m_QmPieMenuColorFollow;
		break;
	case EMenuOption::SCORE:
		ConfigColor = g_Config.m_QmPieMenuColorScore;
		break;
	default:
		ConfigColor = 0x4D6680BF;
	}

	ColorRGBA BaseColor = color_cast<ColorRGBA>(ColorHSLA(ConfigColor, Option >= EMenuOption::INVITE_TEAM));

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
	float InnerRadius = INNER_RADIUS * MenuScale();

	return length(m_SelectorMouse) < InnerRadius;
}

bool CPieMenu::HasTargetPlayer() const
{
	if(Client()->State() != IClient::STATE_ONLINE || m_TargetClientId < 0 || m_TargetClientId >= MAX_CLIENTS)
		return false;
	const auto &Player = GameClient()->m_aClients[m_TargetClientId];
	return Player.m_Active && qm_pie_menu::MatchesPlayer(Player.m_aName, Player.m_aClan, m_TargetName.c_str(), m_TargetClan.c_str());
}

int CPieMenu::GetHoveredOption() const
{
	float MouseAngle = atan2(m_SelectorMouse.y, m_SelectorMouse.x) * 180.0f / pi;

	return qm_pie_menu::SectorAtAngle(MouseAngle, START_ANGLE, VisibleOptionCount());
}

int CPieMenu::GetHoveredRenameOption() const
{
	if(m_vRenameQueue.empty())
		return -1;

	float MouseAngle = atan2(m_SelectorMouse.y, m_SelectorMouse.x) * 180.0f / pi;

	while(MouseAngle < 0)
		MouseAngle += 360.0f;
	while(MouseAngle >= 360.0f)
		MouseAngle -= 360.0f;

	float AdjustedAngle = MouseAngle - START_ANGLE;
	while(AdjustedAngle < 0)
		AdjustedAngle += 360.0f;
	while(AdjustedAngle >= 360.0f)
		AdjustedAngle -= 360.0f;

	const int SectorCount = (int)m_vRenameQueue.size();
	float AnglePerSector = 360.0f / SectorCount;
	int SectorIndex = (int)(AdjustedAngle / AnglePerSector);

	if(SectorIndex >= 0 && SectorIndex < SectorCount)
		return SectorIndex;

	return -1;
}

// ========== Option Execution ==========
