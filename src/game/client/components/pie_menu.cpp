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

#include <game/client/QmUi/QmPieMenuRender.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

CPieMenu::CPieMenu()
{
	OnReset();
}

void CPieMenu::OnReset()
{
	m_Lifecycle.Cancel();
	m_TargetClientId = -1;
	m_TargetName.clear();
	m_TargetClan.clear();
	m_SelectedOption = -1;
	m_SelectedRenameIndex = -1;
	m_CommittedOption = -1;
	m_CommittedRenameIndex = -1;
	m_AnimationProgress = 0.0f;
	m_MenuCenter = vec2(0, 0);
	m_OpenTime = 0;
	m_CloseTime = 0;
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
	if(m_Lifecycle.IsVisible())
	{
		CloseMenu();
	}
}

void CPieMenu::OnUpdate()
{
	if(m_Lifecycle.IsVisible() && !g_Config.m_QmPieMenuEnabled)
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
		if(pSelf->m_Lifecycle.IsInteractive())
		{
			const int RenameIndex = pSelf->m_SelectedRenameIndex;
			const int OptionIndex = pSelf->m_SelectedOption;
			const bool Rename = RenameIndex >= 0 && RenameIndex < (int)pSelf->m_vRenameQueue.size();
			const bool Option = OptionIndex >= 0 && OptionIndex < pSelf->VisibleOptionCount();
			const EMenuOption Action = Option ? pSelf->VisibleOption(OptionIndex) : EMenuOption::FRIEND;
			pSelf->CloseMenu(Rename || Option);
			if(Rename)
				pSelf->ExecuteRenameOption(RenameIndex);
			else if(Option)
				pSelf->ExecuteOption(Action);
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
	if(m_Lifecycle.IsVisible() && m_Lifecycle.State() != EMenuState::CLOSING)
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
	m_Lifecycle.Open();
	m_SelectedOption = -1;
	m_SelectedRenameIndex = -1;
	m_CommittedOption = -1;
	m_CommittedRenameIndex = -1;
	m_AnimationProgress = 0.0f;
	m_OpenTime = time_get();
	m_CloseTime = 0;
	m_WasPressed = true;
	m_SelectorMouse = vec2(0, 0); // Reset selector mouse position

	// Set menu center to screen center
	m_MenuCenter = vec2(Graphics()->ScreenWidth() / 2.0f, Graphics()->ScreenHeight() / 2.0f);
}

void CPieMenu::CloseMenu(bool HasExecuted)
{
	if(!m_Lifecycle.IsVisible())
		return;

	if(HasExecuted && (m_SelectedOption >= 0 || m_SelectedRenameIndex >= 0))
	{
		if(!m_Lifecycle.BeginCommit())
			return;
		m_CommittedOption = m_SelectedOption;
		m_CommittedRenameIndex = m_SelectedRenameIndex;
		m_CloseTime = time_get();
	}
	else
	{
		m_Lifecycle.Cancel();
		m_TargetClientId = -1;
		m_SelectedOption = -1;
		m_SelectedRenameIndex = -1;
		m_CommittedOption = -1;
		m_CommittedRenameIndex = -1;
		m_vRenameQueue.clear();
	}
}

// ========== Input Handling ==========

bool CPieMenu::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Lifecycle.IsVisible() || m_Lifecycle.State() == EMenuState::CLOSING)
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
	if(m_Lifecycle.IsVisible() && (Event.m_Flags & IInput::FLAG_PRESS))
	{
		if(!m_Lifecycle.IsInteractive())
		{
			using EInputKind = qm_pie_menu::CMenuLifecycle::EInputKind;
			const EInputKind Kind = Event.m_Key == KEY_ESCAPE ? EInputKind::CANCEL : (Event.m_Key >= KEY_1 && Event.m_Key <= KEY_0) ? EInputKind::ACTION :
																		  EInputKind::OTHER;
			if(!m_Lifecycle.CapturesClosingInput(Kind))
				return false;
			if(Kind == EInputKind::CANCEL)
				CloseMenu();
			return true;
		}
	}

	if(m_Lifecycle.IsInteractive() && (Event.m_Flags & IInput::FLAG_PRESS))
	{
		if(Event.m_Key >= KEY_1 && Event.m_Key <= KEY_0)
		{
			if(!HasTargetPlayer())
				return true;

			int OptionIndex = Event.m_Key - KEY_1;
			if(OptionIndex >= 0 && OptionIndex < VisibleOptionCount())
			{
				const EMenuOption Action = VisibleOption(OptionIndex);
				m_SelectedOption = OptionIndex;
				m_SelectedRenameIndex = -1;
				CloseMenu(true);
				ExecuteOption(Action);
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
	if(!m_Lifecycle.IsVisible())
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
	Enabled[static_cast<size_t>(EMenuOption::COPY_NAME)] = g_Config.m_QmPieMenuCopyNameEnabled != 0;
	m_VisibleOptionCount = qm_pie_menu::BuildVisibleOptions(Enabled, m_vVisibleOptions);
}

// ========== Rendering ==========

void CPieMenu::OnRender()
{
	if(!m_Lifecycle.IsVisible() || !g_Config.m_QmPieMenuEnabled)
		return;

	// Update animation
	float IrisProgress = 1.0f;
	float CommitProgress = 0.0f;
	if(m_Lifecycle.State() == EMenuState::OPENING)
	{
		float TimeSinceOpen = (time_get() - m_OpenTime) / (float)time_freq();
		const float t = minimum(maximum(TimeSinceOpen / ANIMATION_DURATION, 0.0f), 1.0f);
		const float Inv = 1.0f - t;
		// 平滑三次缓出：前段迅猛旋开扩张，后段柔和卡入锁定
		m_AnimationProgress = 1.0f - Inv * Inv * Inv;
		IrisProgress = m_AnimationProgress;
		if(t >= 1.0f)
		{
			m_Lifecycle.FinishOpening();
		}
	}
	else if(m_Lifecycle.State() == EMenuState::CLOSING)
	{
		float TimeSinceClose = (time_get() - m_CloseTime) / (float)time_freq();
		const float t = minimum(maximum(TimeSinceClose / CLOSE_DURATION, 0.0f), 1.0f);
		CommitProgress = t;
		m_AnimationProgress = 1.0f - t;
		if(t >= 1.0f)
		{
			m_Lifecycle.Cancel();
			m_TargetClientId = -1;
			m_SelectedOption = -1;
			m_SelectedRenameIndex = -1;
			m_CommittedOption = -1;
			m_CommittedRenameIndex = -1;
			m_vRenameQueue.clear();
			return;
		}
	}
	else
	{
		m_AnimationProgress = 1.0f;
	}

	// Update selection based on mouse position (only when active)
	if(m_Lifecycle.State() != EMenuState::CLOSING)
		UpdateSelection();

	m_MenuCenter = vec2(Graphics()->ScreenWidth() * 0.5f, Graphics()->ScreenHeight() * 0.5f);
	const vec2 ScreenCenter = m_MenuCenter;
	const float Scale = MenuScale();
	const float Alpha = m_AnimationProgress * (g_Config.m_QmPieMenuOpacity / 100.0f);

	const float BaseInnerRadius = INNER_RADIUS * Scale;
	const float BaseOuterRadius = OUTER_RADIUS * Scale;
	const float BaseSecondaryInnerRadius = SECONDARY_INNER_RADIUS * Scale;
	const float BaseSecondaryOuterRadius = SECONDARY_OUTER_RADIUS * Scale;

	// 虹膜机械动态参数
	float PrimaryAngleOffset = 0.0f;
	float PrimarySpanFactor = 1.0f;
	float PrimaryBladeEdgeAlpha = 0.0f;
	float SectorInner = BaseInnerRadius;
	float SectorOuter = BaseOuterRadius;
	float PupilRadius = BaseInnerRadius - 9.0f * Scale;

	if(m_Lifecycle.State() == EMenuState::OPENING)
	{
		// 1. 虹膜叶片切向旋角 (Spiral Twist)：由 +32° 旋向 0°
		PrimaryAngleOffset = (1.0f - IrisProgress) * (1.0f - IrisProgress) * 32.0f;

		// 2. 虹膜孔径径向扩张 (Iris Aperture Dilation)：
		// 内径从近中心孔径 (22%) 迅速撑开到 100%，形成孔径舒张的虹膜核心感
		SectorInner = BaseInnerRadius * (0.22f + 0.78f * IrisProgress);
		// 外径随叶片滑移从内向外绽放舒展
		SectorOuter = mix(BaseInnerRadius * 0.65f, BaseOuterRadius, IrisProgress);

		// 3. 机械叶片扇幅扩展 (Blade Fan-out)：初始呈尖锐切片叶，随孔径撑开逐渐接合
		PrimarySpanFactor = 0.40f + 0.60f * IrisProgress;

		// 4. 机械刃口高光 (Blade Edge Highlight)：光圈展开过程中叶片边缘高亮滑移
		PrimaryBladeEdgeAlpha = (1.0f - IrisProgress) * 0.85f * Alpha;

		// 5. 中央瞳孔扩张：伴随光圈孔径从中心圆点扩散开
		PupilRadius = SectorInner - 7.0f * Scale;
	}
	else if(m_Lifecycle.State() == EMenuState::CLOSING)
	{
		// 击发收束：中央孔径与非选中扇区内缩
		PupilRadius *= (1.0f - CommitProgress * 0.35f);
	}

	Graphics()->MapScreen(0, 0, Graphics()->ScreenWidth(), Graphics()->ScreenHeight());

	// The primary ring only applies to another player.
	if(HasTargetPlayer() && VisibleOptionCount() > 0)
	{
		for(int i = 0; i < VisibleOptionCount(); i++)
		{
			bool Highlighted = (m_Lifecycle.State() == EMenuState::CLOSING ? (i == m_CommittedOption) : (i == m_SelectedOption));
			float SectorAlpha = Alpha;
			float ItemInner = SectorInner;
			float ItemOuter = SectorOuter;
			float ItemAngleOffset = PrimaryAngleOffset;
			float ItemSpanFactor = PrimarySpanFactor;
			float ItemBladeEdgeAlpha = PrimaryBladeEdgeAlpha;

			if(m_Lifecycle.State() == EMenuState::CLOSING)
			{
				if(Highlighted)
				{
					// 击发回馈：被选中的扇区向中心收束微震，保持高亮
					const float Pulse = 1.0f + 0.15f * std::sin(CommitProgress * pi);
					ItemOuter *= Pulse;
					SectorAlpha = (1.0f - CommitProgress * 0.5f) * (g_Config.m_QmPieMenuOpacity / 100.0f);
				}
				else
				{
					// 其余未选中扇区极速淡隐
					SectorAlpha *= maximum(0.0f, 1.0f - CommitProgress * 2.5f);
				}
			}
			if(SectorAlpha > 0.001f)
				RenderSector(i, ItemInner, ItemOuter, Highlighted, SectorAlpha, ItemAngleOffset, ItemSpanFactor, ItemBladeEdgeAlpha);
		}
	}

	// Render secondary ring for rename queue.
	if(!m_vRenameQueue.empty())
	{
		const int SectorCount = (int)m_vRenameQueue.size();
		float SecondaryInner = BaseSecondaryInnerRadius;
		float SecondaryOuter = BaseSecondaryOuterRadius;
		float SecondaryAngleOffset = 0.0f;
		float SecondarySpanFactor = 1.0f;

		if(m_Lifecycle.State() == EMenuState::OPENING)
		{
			// 次级重命名光圈：滞后 15% 呈级联波浪绽开
			float SecondaryIris = std::clamp((IrisProgress - 0.15f) / 0.85f, 0.0f, 1.0f);
			SecondaryInner = mix(SectorOuter, BaseSecondaryInnerRadius, SecondaryIris);
			SecondaryOuter = mix(SectorOuter + 16.0f * Scale, BaseSecondaryOuterRadius, SecondaryIris);
			SecondaryAngleOffset = (1.0f - SecondaryIris) * (1.0f - SecondaryIris) * 24.0f;
			SecondarySpanFactor = 0.45f + 0.55f * SecondaryIris;
		}

		for(int i = 0; i < SectorCount; i++)
		{
			bool Highlighted = (m_Lifecycle.State() == EMenuState::CLOSING ? (i == m_CommittedRenameIndex) : (i == m_SelectedRenameIndex));
			float SectorAlpha = Alpha;
			float ItemInner = SecondaryInner;
			float ItemOuter = SecondaryOuter;
			if(m_Lifecycle.State() == EMenuState::CLOSING)
			{
				if(Highlighted)
				{
					const float Pulse = 1.0f + 0.15f * std::sin(CommitProgress * pi);
					ItemOuter *= Pulse;
					SectorAlpha = (1.0f - CommitProgress * 0.5f) * (g_Config.m_QmPieMenuOpacity / 100.0f);
				}
				else
				{
					SectorAlpha *= maximum(0.0f, 1.0f - CommitProgress * 2.5f);
				}
			}
			if(SectorAlpha > 0.001f)
				RenderRenameSector(i, SectorCount, ItemInner, ItemOuter, Highlighted, SectorAlpha, SecondaryAngleOffset, SecondarySpanFactor);
		}
	}

	// Draw center circle for player name (aperture pupil)
	if(PupilRadius > 1.0f)
	{
		qm_pie_menu_ui::DrawDisc(Graphics(), ScreenCenter, PupilRadius, ColorRGBA(0.12f, 0.13f, 0.17f, 0.92f * Alpha));

		// 开合动画期间渲染虹膜内圈光晕 (Iris Aperture Ring)
		if(m_Lifecycle.State() == EMenuState::OPENING && IrisProgress < 0.98f)
		{
			qm_pie_menu_ui::DrawDisc(Graphics(), ScreenCenter, PupilRadius + 1.8f * Scale, ColorRGBA(0.35f, 0.70f, 1.0f, (1.0f - IrisProgress) * 0.55f * Alpha));
			qm_pie_menu_ui::DrawDisc(Graphics(), ScreenCenter, PupilRadius, ColorRGBA(0.12f, 0.13f, 0.17f, 0.92f * Alpha));
		}
	}

	// Render center info (player name)
	float CenterInfoAlpha = Alpha;
	if(m_Lifecycle.State() == EMenuState::OPENING)
	{
		CenterInfoAlpha *= std::clamp((IrisProgress - 0.35f) / 0.65f, 0.0f, 1.0f);
	}
	if(CenterInfoAlpha > 0.005f)
	{
		RenderCenterInfo(CenterInfoAlpha);
	}

	// Render cursor (hide cursor during closing commit)
	if(m_Lifecycle.State() != EMenuState::CLOSING)
		RenderTools()->RenderCursor(ScreenCenter + m_SelectorMouse, 43.0f, Alpha); // 24 * 1.8
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CPieMenu::RenderOverlay()
{
	// Not used - sectors provide the background
}

void CPieMenu::RenderSector(int Index, float InnerRadius, float OuterRadius, bool Highlighted, float Alpha, float AngleOffset, float SpanFactor, float BladeEdgeAlpha)
{
	if(Index < 0 || Index >= VisibleOptionCount())
		return;
	const EMenuOption Option = VisibleOption(Index);

	// Calculate sector angles
	float AnglePerSector = 360.0f / VisibleOptionCount();
	const float SectorGap = VisibleOptionCount() == 1 ? 0.0f : SECTOR_GAP;
	float BladeSpan = (AnglePerSector - SectorGap) * SpanFactor;
	float StartAngle = START_ANGLE + AngleOffset + AnglePerSector * Index + SectorGap / 2.0f;
	float EndAngle = StartAngle + BladeSpan;

	// 悬停只改变颜色，轮廓保持在原命中半径，避免凸入外侧改名环。
	const ColorRGBA Color = GetOptionColor(Option, Highlighted).WithMultipliedAlpha(Alpha);
	qm_pie_menu_ui::DrawSector(Graphics(), m_MenuCenter, InnerRadius, OuterRadius, StartAngle, EndAngle, SectorGap, Color);

	// 虹膜机械叶片边缘高光 (Iris Blade Leading Edge)
	if(BladeEdgeAlpha > 0.01f)
	{
		float RadLeading = StartAngle * pi / 180.0f;
		vec2 DirLeading = vec2(cos(RadLeading), sin(RadLeading));
		vec2 LineInner = m_MenuCenter + DirLeading * InnerRadius;
		vec2 LineOuter = m_MenuCenter + DirLeading * OuterRadius;
		vec2 Normal = vec2(-DirLeading.y, DirLeading.x) * 1.5f;

		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(0.85f, 0.95f, 1.0f, BladeEdgeAlpha);
		IGraphics::CFreeformItem EdgeItem(
			LineInner.x - Normal.x, LineInner.y - Normal.y,
			LineOuter.x - Normal.x, LineOuter.y - Normal.y,
			LineInner.x + Normal.x, LineInner.y + Normal.y,
			LineOuter.x + Normal.x, LineOuter.y + Normal.y);
		Graphics()->QuadsDrawFreeform(&EdgeItem, 1);
		Graphics()->QuadsEnd();
	}

	// 仅当叶片充分展开后绘制图标与标签，避免狭窄叶片挤压变形
	float ContentAlpha = Alpha * std::clamp(SpanFactor * 1.5f - 0.5f, 0.0f, 1.0f);
	if(ContentAlpha < 0.02f)
		return;

	// Draw icon and text in sector center
	float MidRadius = (InnerRadius + OuterRadius) / 2.0f;
	float MidAngle = (StartAngle + EndAngle) / 2.0f * pi / 180.0f;
	vec2 ItemPos = m_MenuCenter + vec2(cos(MidAngle), sin(MidAngle)) * MidRadius;

	// Draw icon
	const char *pIcon = GetOptionIcon(Option);
	const float Scale = OuterRadius / OUTER_RADIUS;
	const float AvailableWidth = maximum(1.0f, 1.35f * MidRadius * sinf(minimum(BladeSpan, 180.0f) * pi / 360.0f));
	float IconSize = minimum(45.0f * Scale, AvailableWidth * 0.65f);

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, ContentAlpha);
	const EFontPreset PreviousFont = TextRender()->GetFontPreset();
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	float IconWidth = TextRender()->TextWidth(IconSize, pIcon);
	TextRender()->Text(ItemPos.x - IconWidth / 2.0f, ItemPos.y - IconSize / 2.0f - 14.0f * Scale, IconSize, pIcon);
	TextRender()->SetFontPreset(PreviousFont);

	// Draw label below icon
	const char *pName = GetOptionName(Option);
	float TextSize = 23.0f * Scale;
	float TextWidth = TextRender()->TextWidth(TextSize, pName);
	if(TextWidth > AvailableWidth)
	{
		TextSize *= AvailableWidth / TextWidth;
		TextWidth = TextRender()->TextWidth(TextSize, pName);
	}
	TextRender()->Text(ItemPos.x - TextWidth / 2.0f, ItemPos.y + 14.0f * Scale, TextSize, pName);
}

void CPieMenu::RenderRenameSector(int Index, int SectorCount, float InnerRadius, float OuterRadius, bool Highlighted, float Alpha, float AngleOffset, float SpanFactor)
{
	if(Index < 0 || Index >= SectorCount || SectorCount <= 0 || Index >= (int)m_vRenameQueue.size())
		return;

	float AnglePerSector = 360.0f / SectorCount;
	float DynamicGap = SectorCount == 1 ? 0.0f : minimum(SECTOR_GAP, AnglePerSector * 0.35f);
	float BladeSpan = (AnglePerSector - DynamicGap) * SpanFactor;
	float StartAngle = START_ANGLE + AngleOffset + AnglePerSector * Index + DynamicGap / 2.0f;
	float EndAngle = StartAngle + BladeSpan;

	if(EndAngle <= StartAngle)
		return;

	const ColorRGBA Color = (Highlighted ? ColorRGBA(0.36f, 0.75f, 0.52f, 0.95f) : ColorRGBA(0.28f, 0.58f, 0.43f, 0.78f)).WithMultipliedAlpha(Alpha);
	qm_pie_menu_ui::DrawSector(Graphics(), m_MenuCenter, InnerRadius, OuterRadius, StartAngle, EndAngle, DynamicGap, Color);

	float ContentAlpha = Alpha * std::clamp(SpanFactor * 1.5f - 0.5f, 0.0f, 1.0f);
	if(ContentAlpha < 0.02f)
		return;

	const char *pRenameName = m_vRenameQueue[Index].c_str();
	float MidRadius = (InnerRadius + OuterRadius) / 2.0f;
	float MidAngle = (StartAngle + EndAngle) / 2.0f * pi / 180.0f;
	vec2 ItemPos = m_MenuCenter + vec2(cos(MidAngle), sin(MidAngle)) * MidRadius;

	float TextSize = 18.0f * (OuterRadius / SECONDARY_OUTER_RADIUS);
	if(SectorCount > 8)
		TextSize *= 0.92f;
	if(SectorCount > 12)
		TextSize *= 0.85f;

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, ContentAlpha);
	float TextWidth = TextRender()->TextWidth(TextSize, pRenameName);
	const float AvailableWidth = maximum(1.0f, minimum(OuterRadius - InnerRadius, MidRadius * sinf(minimum(BladeSpan, 180.0f) * pi / 360.0f)));
	if(TextWidth > AvailableWidth)
	{
		TextSize *= AvailableWidth / TextWidth;
		TextWidth = TextRender()->TextWidth(TextSize, pRenameName);
	}
	TextRender()->Text(ItemPos.x - TextWidth / 2.0f, ItemPos.y - TextSize / 2.0f, TextSize, pRenameName);
}

void CPieMenu::RenderCenterInfo(float Alpha)
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
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
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
	return minimum(g_Config.m_QmPieMenuScale / 100.0f, FitScale);
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
	case EMenuOption::COPY_NAME: return Localize("Copy name");
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
	case EMenuOption::COPY_NAME: return FontIcons::FONT_ICON_USER;
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
	case EMenuOption::COPY_NAME:
		ConfigColor = g_Config.m_QmPieMenuColorCopyName;
		break;
	default:
		ConfigColor = 0x4D6680BF;
	}

	return qm_pie_menu_ui::OptionColor(color_cast<ColorRGBA>(ColorHSLA(ConfigColor, Option >= EMenuOption::INVITE_TEAM)), Highlighted);
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
