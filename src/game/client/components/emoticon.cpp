/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "emoticon.h"

#include "chat.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/animstate.h>
#include <game/client/components/qmclient/emoticon_commands.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/collision.h>
#include <game/gamecore.h>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
	constexpr float s_SuperChargeRingThickness = 4.0f;
	constexpr int s_SuperChargeRingSegments = 72;
	constexpr float s_SuperChargeRingAnimationSeconds = 0.18f;

	void RenderChargeRing(IGraphics *pGraphics, vec2 Center, float OuterRadius, float Thickness, float Progress, ColorRGBA FilledColor, ColorRGBA EmptyColor, int Segments)
	{
		if(OuterRadius <= 0.0f || Thickness <= 0.0f || Segments <= 0)
			return;
		const float ClampedProgress = std::clamp(Progress, 0.0f, 1.0f);
		const float InnerRadius = std::max(0.0f, OuterRadius - Thickness);
		const float SegmentAngle = 2.0f * pi / (float)Segments;
		const float AngleOffset = -0.5f * pi;

		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(EmptyColor);
		for(int i = 0; i < Segments; ++i)
		{
			const vec2 Dir1 = direction(AngleOffset + i * SegmentAngle);
			const vec2 Dir2 = direction(AngleOffset + (i + 1) * SegmentAngle);
			const IGraphics::CFreeformItem Item(
				Center + Dir1 * InnerRadius, Center + Dir2 * InnerRadius,
				Center + Dir1 * OuterRadius, Center + Dir2 * OuterRadius);
			pGraphics->QuadsDrawFreeform(&Item, 1);
		}
		pGraphics->SetColor(FilledColor);
		const float FilledSegments = ClampedProgress * Segments;
		const int WholeSegments = std::clamp((int)std::floor(FilledSegments), 0, Segments);
		for(int i = 0; i < WholeSegments; ++i)
		{
			const vec2 Dir1 = direction(AngleOffset + i * SegmentAngle);
			const vec2 Dir2 = direction(AngleOffset + (i + 1) * SegmentAngle);
			const IGraphics::CFreeformItem Item(
				Center + Dir1 * InnerRadius, Center + Dir2 * InnerRadius,
				Center + Dir1 * OuterRadius, Center + Dir2 * OuterRadius);
			pGraphics->QuadsDrawFreeform(&Item, 1);
		}
		const float PartialSegment = FilledSegments - WholeSegments;
		if(PartialSegment > 0.0f && WholeSegments < Segments)
		{
			const float Angle1 = AngleOffset + WholeSegments * SegmentAngle;
			const float Angle2 = Angle1 + SegmentAngle * PartialSegment;
			const IGraphics::CFreeformItem Item(
				Center + direction(Angle1) * InnerRadius, Center + direction(Angle2) * InnerRadius,
				Center + direction(Angle1) * OuterRadius, Center + direction(Angle2) * OuterRadius);
			pGraphics->QuadsDrawFreeform(&Item, 1);
		}
		pGraphics->QuadsEnd();
	}
}

static uint64_t EmoticonPresentationNodeKey(const char *pScope)
{
	static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("qm_extra_emoticon_presentation"));
	return BuildUiAnimNodeKey(s_BaseKey, static_cast<uint64_t>(str_quickhash(pScope)));
}

static SUiSpringConfig EmoticonPresentationSpring()
{
	SUiSpringConfig Spring;
	Spring.m_Stiffness = 470.0f;
	Spring.m_Damping = 40.0f;
	Spring.m_RestEpsilon = 0.006f;
	Spring.m_RestVelocity = 0.08f;
	return Spring;
}

static int EmoticonClockwiseOrderFromTop(int Index, int Count)
{
	const float Angle = (2.0f * pi * Index) / Count;
	const float ClockwiseFromTop = std::fmod(Angle + pi / 2.0f + 2.0f * pi, 2.0f * pi);
	return std::clamp(static_cast<int>(std::round(ClockwiseFromTop / (2.0f * pi) * Count)), 0, Count - 1);
}

static float EmoticonStaggerReveal(int Index, int Count, float PresentationAlpha)
{
	if(Count <= 1)
		return PresentationAlpha;

	constexpr float MaxDelay = 0.42f;
	const int Order = EmoticonClockwiseOrderFromTop(Index, Count);
	const float Delay = MaxDelay * Order / (Count - 1);
	const float Denominator = std::max(0.001f, 1.0f - Delay);
	const float LocalT = std::clamp((PresentationAlpha - Delay) / Denominator, 0.0f, 1.0f);
	const float Inv = 1.0f - LocalT;
	return 1.0f - Inv * Inv * Inv * Inv;
}

CEmoticon::CEmoticon()
{
	m_RenderProjectiles.m_pEmoticon = this;
	OnReset();
}

void CEmoticon::ConKeyEmoticon(IConsole::IResult *pResult, void *pUserData)
{
	CEmoticon *pSelf = (CEmoticon *)pUserData;

	const bool Active = pResult->GetInteger(0) != 0;
	// 释放必须先处理，避免计分板等状态变化吞掉松开事件。
	if(!Active)
	{
		pSelf->SetActive(false);
		return;
	}
	if(!pSelf->GameClient()->m_Scoreboard.IsActive() &&
		!pSelf->GameClient()->m_Snap.m_SpecInfo.m_Active &&
		pSelf->Client()->State() == IClient::STATE_ONLINE &&
		!pSelf->GameClient()->m_BindWheel.IsActive())
		pSelf->SetActive(true);
}

void CEmoticon::ConSuperEmote(IConsole::IResult *pResult, void *pUserData)
{
	((CEmoticon *)pUserData)->SuperEmote(pResult->GetInteger(0));
}

void CEmoticon::ConToggleLaunchMode(IConsole::IResult *, void *pUserData)
{
	((CEmoticon *)pUserData)->ToggleLaunchMode();
}

void CEmoticon::ConLocalBlink(IConsole::IResult *, void *pUserData)
{
	((CEmoticon *)pUserData)->TriggerLocalBlink();
}

void CEmoticon::OnConsoleInit()
{
	Console()->Register("+emote", "", CFGFLAG_CLIENT, ConKeyEmoticon, this, "Open emote selector");
	// emote 与 shot_emote 共用同一套参数解析与 Emote 入口（远程 qm_emoticon_commands 的结构）。
	QmEmoticon::RegisterCommands(Console(), this);
	Console()->Register("super_emote", "i[emote-id]", CFGFLAG_CLIENT, ConSuperEmote, this, "Use large emote");
	Console()->Register("qm_blink", "", CFGFLAG_CLIENT, ConLocalBlink, this, "Blink the active local tee");
	Console()->Register("toggle_emote_launcher", "", CFGFLAG_CLIENT, ConToggleLaunchMode, this, "Toggle emote launcher");
}

void CEmoticon::OnReset()
{
	m_WasActive = false;
	m_Active = false;
	m_PresentationInitialized = false;
	m_SelectorMouse = vec2(0.0f, 0.0f);
	m_SelectedEmote = -1;
	m_SelectedEyeEmote = -1;
	m_LaunchModeActive = false;
	m_SuperCharge.Reset();
	m_SuperChargeProgress = 0.0f;
	m_SuperChargeRingEmote = -1;
	m_SuperChargeRingPhase = 0.0f;
	m_SuperChargeRingCharge = 0.0f;
	m_SuperChargeRingExitEmote = -1;
	m_SuperChargeRingExitPhase = 0.0f;
	m_SuperChargeRingExitCharge = 0.0f;
	m_LocalSuperHeadEmoticon = -1;
	m_LocalSuperHeadExpireTick = -1;
	std::fill(std::begin(m_aRemoteSuperHeadEmoticons), std::end(m_aRemoteSuperHeadEmoticons), -1);
	std::fill(std::begin(m_aRemoteSuperHeadExpireTicks), std::end(m_aRemoteSuperHeadExpireTicks), -1);
	for(auto &LocalBlinkState : m_aLocalBlinkStates)
		LocalBlinkState.Reset();
	m_TouchPressedOutside = false;
	m_SuperLaunchPending = false;
	for(auto &Projectile : m_aProjectiles)
		Projectile.m_Active = false;
}

void CEmoticon::OnRelease()
{
	SetActive(false);
}

void CEmoticon::UpdateSelection()
{
	if(length(m_SelectorMouse) > 170.0f)
		m_SelectorMouse = normalize(m_SelectorMouse) * 170.0f;
	const float SelectorAngle = angle(m_SelectorMouse);
	const auto PositiveMod = [](float x, float y) -> int { return static_cast<int>(std::fmod(x + y, y)); };
	m_SelectedEmote = length(m_SelectorMouse) > 110.0f ? PositiveMod(std::round(SelectorAngle / (2.0f * pi) * NUM_EMOTICONS), NUM_EMOTICONS) : -1;
	m_SelectedEyeEmote = m_SelectedEmote == -1 && length(m_SelectorMouse) > 40.0f ?
				     PositiveMod(std::round(SelectorAngle / (2.0f * pi) * NUM_EMOTES), NUM_EMOTES) :
				     -1;
	if(!GameClient()->m_GameInfo.m_AllowEyeWheel || !g_Config.m_ClEyeWheel || GameClient()->m_aLocalIds[g_Config.m_ClDummy] < 0)
		m_SelectedEyeEmote = -1;
	m_SuperChargeProgress = m_SuperCharge.Update(m_SelectedEmote, time_get(), time_freq());
}

void CEmoticon::SetActive(bool Active)
{
	if(Active == m_Active)
		return;
	if(Active)
	{
		m_Active = true;
		m_TouchPressedOutside = false;
		m_SuperCharge.Reset();
		UpdateSelection();
		return;
	}
	UpdateSelection();
	m_Active = false;
	m_WasActive = true;
	if(!m_TouchPressedOutside && Client()->State() == IClient::STATE_ONLINE &&
		!GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_pLocalCharacter)
	{
		if(m_SelectedEmote != -1)
		{
			m_SuperLaunchPending = m_SuperChargeProgress >= 1.0f;
			Emote(m_SelectedEmote);
		}
		if(m_SelectedEyeEmote != -1)
			EyeEmote(m_SelectedEyeEmote);
	}
}

void CEmoticon::ToggleLaunchMode()
{
	if(!m_Active)
		return;
	m_LaunchModeActive = !m_LaunchModeActive;
	GameClient()->Echo(m_LaunchModeActive ? "表情发射：开启" : "表情发射：关闭");
}

bool CEmoticon::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	m_SelectorMouse += vec2(x, y);
	UpdateSelection();
	return true;
}

bool CEmoticon::OnInput(const IInput::CEvent &Event)
{
	if(IsActive() && Event.m_Flags & IInput::FLAG_PRESS)
	{
		if(Event.m_Key == KEY_ESCAPE)
		{
			OnRelease();
			return true;
		}
		if(Event.m_Key == KEY_TAB)
		{
			ToggleLaunchMode();
			return true;
		}
	}
	return false;
}

void CEmoticon::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	static const float s_InnerOuterMouseBoundaryRadius = 110.0f;
	static const float s_OuterMouseLimitRadius = 170.0f;
	static const float s_InnerItemRadius = 70.0f;
	static const float s_OuterItemRadius = 150.0f;
	static const float s_InnerCircleRadius = 100.0f;
	static const float s_OuterCircleRadius = 190.0f;

	if(!m_Active)
	{
		if(m_TouchPressedOutside)
		{
			m_SelectedEmote = -1;
			m_SelectedEyeEmote = -1;
			m_TouchPressedOutside = false;
		}

		if(m_WasActive && m_SuperChargeRingEmote != -1)
		{
			m_SuperChargeRingExitEmote = m_SuperChargeRingEmote;
			m_SuperChargeRingExitPhase = m_SuperChargeRingPhase;
			m_SuperChargeRingExitCharge = m_SuperChargeRingCharge;
			m_SuperChargeRingEmote = -1;
			m_SuperChargeRingPhase = 0.0f;
			m_SuperChargeRingCharge = 0.0f;
		}
		m_WasActive = false;
		m_SuperCharge.Reset();
		m_SuperChargeProgress = 0.0f;
	}
	else
	{
		m_WasActive = true;
	}

	if(GameClient()->m_Snap.m_SpecInfo.m_Active || !GameClient()->m_Snap.m_pLocalCharacter)
	{
		m_Active = false;
		m_WasActive = false;
		return;
	}

	const CUIRect Screen = *Ui()->Screen();

	if(m_Active)
	{
		const bool WasTouchPressed = m_TouchState.m_AnyPressed;
		Ui()->UpdateTouchState(m_TouchState);
		if(m_TouchState.m_AnyPressed)
		{
			const vec2 TouchPos = (m_TouchState.m_PrimaryPosition - vec2(0.5f, 0.5f)) * Screen.Size();
			const float TouchCenterDistance = length(TouchPos);
			if(TouchCenterDistance <= s_OuterMouseLimitRadius)
			{
				m_SelectorMouse = TouchPos;
				UpdateSelection();
			}
			else if(TouchCenterDistance > s_OuterCircleRadius)
			{
				m_TouchPressedOutside = true;
			}
		}
		else if(WasTouchPressed)
		{
			SetActive(false);
		}
	}

	const bool ExtraAnimations = g_Config.m_QmExtraAnimations != 0 && GameClient()->UiRuntimeV2()->Enabled();
	float PresentationAlpha = m_Active ? 1.0f : 0.0f;
	float PresentationScale = m_Active ? 1.0f : 0.88f;
	if(ExtraAnimations)
	{
		CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
		const SUiSpringConfig Spring = EmoticonPresentationSpring();
		const uint64_t PanelNode = EmoticonPresentationNodeKey("panel");
		if(!m_PresentationInitialized)
		{
			SetUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::ALPHA, 0.0f);
			SetUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::SCALE, 0.88f);
			m_PresentationInitialized = true;
		}
		PresentationAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::ALPHA, m_Active ? 1.0f : 0.0f, Spring, 3, 0.004f), 0.0f, 1.0f);
		PresentationScale = std::max(0.01f, ResolveUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::SCALE, m_Active ? 1.0f : 0.88f, Spring, 3, 0.004f));
	}

	if(!m_Active && (!ExtraAnimations || PresentationAlpha <= 0.01f))
		return;

	if(m_Active)
		UpdateSelection();

	if(m_Active && m_SelectedEmote != -1)
	{
		if(m_SuperChargeRingEmote != m_SelectedEmote)
		{
			if(m_SuperChargeRingEmote != -1)
			{
				m_SuperChargeRingExitEmote = m_SuperChargeRingEmote;
				m_SuperChargeRingExitPhase = m_SuperChargeRingPhase;
				m_SuperChargeRingExitCharge = m_SuperChargeRingCharge;
			}
			m_SuperChargeRingEmote = m_SelectedEmote;
			m_SuperChargeRingPhase = 0.0f;
			m_SuperChargeRingCharge = 0.0f;
		}
		m_SuperChargeRingCharge = m_SuperChargeProgress;
	}
	else if(m_Active && m_SuperChargeRingEmote != -1)
	{
		m_SuperChargeRingExitEmote = m_SuperChargeRingEmote;
		m_SuperChargeRingExitPhase = m_SuperChargeRingPhase;
		m_SuperChargeRingExitCharge = m_SuperChargeRingCharge;
		m_SuperChargeRingEmote = -1;
		m_SuperChargeRingPhase = 0.0f;
		m_SuperChargeRingCharge = 0.0f;
	}

	const float RingAnimationStep = Client()->RenderFrameTime() / s_SuperChargeRingAnimationSeconds;
	if(m_SuperChargeRingEmote != -1)
		m_SuperChargeRingPhase = std::min(1.0f, m_SuperChargeRingPhase + RingAnimationStep);
	if(m_SuperChargeRingExitEmote != -1)
	{
		m_SuperChargeRingExitPhase = std::max(0.0f, m_SuperChargeRingExitPhase - RingAnimationStep);
		if(m_SuperChargeRingExitPhase <= 0.0f)
		{
			m_SuperChargeRingExitEmote = -1;
			m_SuperChargeRingExitCharge = 0.0f;
		}
	}

	const vec2 ScreenCenter = Screen.Center();
	const float EmoticonSelectorShadowOpacity = 0.24f * PresentationAlpha;
	const float EmoticonSelectorShadowOffsetX = 2.0f * PresentationScale;
	const float EmoticonSelectorShadowOffsetY = 3.0f * PresentationScale;

	Ui()->MapScreen();

	Graphics()->BlendNormal();

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(ui_token::color::SURFACE_OVERLAY.WithMultipliedAlpha(0.95f * PresentationAlpha));
	Graphics()->DrawCircle(ScreenCenter.x, ScreenCenter.y, s_OuterCircleRadius * PresentationScale, 64);
	Graphics()->SetColor(ui_token::color::ACCENT_PRIMARY_DIM.WithMultipliedAlpha(0.95f * PresentationAlpha));
	Graphics()->DrawCircle(ScreenCenter.x, ScreenCenter.y, s_InnerOuterMouseBoundaryRadius * PresentationScale, 64);
	Graphics()->QuadsEnd();

	Graphics()->WrapClamp();
	for(int Emote = 0; Emote < NUM_EMOTICONS; Emote++)
	{
		float Angle = 2.0f * pi * Emote / NUM_EMOTICONS;
		if(Angle > pi)
			Angle -= 2.0f * pi;

		Graphics()->TextureSet(GameClient()->m_EmoticonsSkin.m_aSpriteEmoticons[Emote]);
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		const float Reveal = EmoticonStaggerReveal(Emote, NUM_EMOTICONS, PresentationAlpha);
		const float ItemAlpha = PresentationAlpha * Reveal;
		const float ItemScale = PresentationScale * (0.70f + 0.30f * Reveal);
		const vec2 Nudge = direction(Angle) * s_OuterItemRadius * ItemScale;
		const float HoverPhase = Emote == m_SelectedEmote ? 1.0f : 0.0f;
		const float Size = (50.0f + HoverPhase * 30.0f) * ItemScale;
		if(g_Config.m_QmEmoticonShadow)
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(0.0f, 0.0f, 0.0f, EmoticonSelectorShadowOpacity);
			IGraphics::CQuadItem ShadowQuad(ScreenCenter.x + Nudge.x + EmoticonSelectorShadowOffsetX, ScreenCenter.y + Nudge.y + EmoticonSelectorShadowOffsetY, Size, Size);
			Graphics()->QuadsDraw(&ShadowQuad, 1);
			Graphics()->QuadsEnd();
			Graphics()->TextureSet(GameClient()->m_EmoticonsSkin.m_aSpriteEmoticons[Emote]);
			Graphics()->QuadsSetSubset(0, 0, 1, 1);
		}
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, ItemAlpha);
		IGraphics::CQuadItem QuadItem(ScreenCenter.x + Nudge.x, ScreenCenter.y + Nudge.y, Size, Size);
		Graphics()->QuadsDraw(&QuadItem, 1);
		Graphics()->QuadsEnd();
	}
	Graphics()->WrapNormal();

	auto RenderSuperChargeRing = [&](int Emote, float Phase, float Charge) {
		if(Emote < 0 || Phase <= 0.0f || Charge <= 0.0f)
			return;
		const float Angle = 2.0f * pi * Emote / (float)NUM_EMOTICONS;
		const vec2 Nudge = direction(Angle) * s_OuterItemRadius * PresentationScale;
		const float RingOuterRadius = 47.0f * PresentationScale * Phase;
		const float RingThickness = s_SuperChargeRingThickness * PresentationScale * Phase;
		const ColorRGBA FilledColor = Charge >= 1.0f ?
						      ColorRGBA(1.0f, 0.82f, 0.35f, 0.95f * PresentationAlpha) :
						      ColorRGBA(1.0f, 1.0f, 1.0f, 0.90f * PresentationAlpha);
		RenderChargeRing(Graphics(), ScreenCenter + Nudge, RingOuterRadius, RingThickness, Charge, FilledColor, FilledColor.WithMultipliedAlpha(0.28f), s_SuperChargeRingSegments);
	};
	RenderSuperChargeRing(m_SuperChargeRingExitEmote, m_SuperChargeRingExitPhase, m_SuperChargeRingExitCharge);
	RenderSuperChargeRing(m_SuperChargeRingEmote, m_SuperChargeRingPhase, m_SuperChargeRingCharge);

	if(GameClient()->m_GameInfo.m_AllowEyeWheel && g_Config.m_ClEyeWheel && GameClient()->m_aLocalIds[g_Config.m_ClDummy] >= 0)
	{
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(ui_token::color::SURFACE_HIGHLIGHT.WithMultipliedAlpha(2.0f * PresentationAlpha));
		Graphics()->DrawCircle(ScreenCenter.x, ScreenCenter.y, s_InnerCircleRadius * PresentationScale, 64);
		Graphics()->QuadsEnd();

		CTeeRenderInfo TeeInfo = GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_RenderInfo;

		for(int Emote = 0; Emote < NUM_EMOTES; Emote++)
		{
			float Angle = 2.0f * pi * Emote / NUM_EMOTES;
			if(Angle > pi)
				Angle -= 2.0f * pi;

			const float Reveal = EmoticonStaggerReveal(Emote, NUM_EMOTES, PresentationAlpha);
			const float ItemAlpha = PresentationAlpha * Reveal;
			const float ItemScale = PresentationScale * (0.76f + 0.24f * Reveal);
			const vec2 Nudge = direction(Angle) * s_InnerItemRadius * ItemScale;
			const float HoverPhase = Emote == m_SelectedEyeEmote ? 1.0f : 0.0f;
			TeeInfo.m_Size = (48.0f + HoverPhase * 18.0f) * ItemScale;
			RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, Emote, vec2(-1.0f, 0.0f), ScreenCenter + Nudge, ItemAlpha);
		}

		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(ui_token::color::SURFACE_ELEVATED.WithMultipliedAlpha(PresentationAlpha));
		Graphics()->DrawCircle(ScreenCenter.x, ScreenCenter.y, 30.0f * PresentationScale, 64);
		Graphics()->QuadsEnd();
	}
	else
	{
		m_SelectedEyeEmote = -1;
	}

	RenderTools()->RenderCursor(ScreenCenter + m_SelectorMouse * PresentationScale, 24.0f * PresentationScale, PresentationAlpha);
}

void CEmoticon::RenderProjectiles()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(Client()->State() == IClient::STATE_ONLINE)
	{
		SQmRealtimeMessage Message;
		while(GameClient()->m_QmClient.PopQmRealtimeEmoticon(Message))
		{
			if(Message.m_Event != EQmRealtimeEvent::EMOTICON || !Message.m_HasEmoticon ||
				Message.m_PlayerId < 0 || Message.m_PlayerId >= MAX_CLIENTS ||
				Message.m_Emoticon < 0 || Message.m_Emoticon >= NUM_EMOTICONS ||
				!GameClient()->m_aClients[Message.m_PlayerId].m_Active)
				continue;
			const int PlayerId = Message.m_PlayerId;
			// 与远程同一套判定：先按事件算出「该有什么效果」，特殊效果再过总开关、忽略名单
			// 与两个「显示他人表情」开关。每条事件都重置该玩家的头顶大表情，只有本次判定为
			// SUPER_HEAD 才重新点亮。
			const QmEmoticon::EEffect Effect = QmEmoticon::ResolveEffect(Message.m_Emoticon, Message.m_LaunchMode, Message.m_SuperLaunch);
			m_aRemoteSuperHeadEmoticons[PlayerId] = -1;
			m_aRemoteSuperHeadExpireTicks[PlayerId] = -1;
			if(Effect == QmEmoticon::EEffect::SUPER_HEAD || Effect == QmEmoticon::EEffect::PROJECTILE || Effect == QmEmoticon::EEffect::SUPER_PROJECTILE)
			{
				if(QmEmoticon::ResolveRemoteEffect(Message.m_Emoticon, Message.m_LaunchMode, Message.m_SuperLaunch,
					   g_Config.m_ClShowEmotes, GameClient()->m_aClients[PlayerId].m_EmoticonIgnore,
					   g_Config.m_QmShowOtherSuperEmotes, g_Config.m_QmShowOtherLaunchEmotes) == QmEmoticon::EEffect::NONE)
					continue;
				if(Effect == QmEmoticon::EEffect::SUPER_HEAD)
				{
					m_aRemoteSuperHeadEmoticons[PlayerId] = Message.m_Emoticon;
					m_aRemoteSuperHeadExpireTicks[PlayerId] = Client()->GameTick(g_Config.m_ClDummy) + 2 * Client()->GameTickSpeed();
					continue;
				}
				const vec2 Position = GameClient()->m_aClients[PlayerId].m_RenderPos - vec2(0.0f, 20.0f);
				const vec2 Direction = direction(GameClient()->m_aClients[PlayerId].m_RenderCur.m_Angle / 256.0f);
				SpawnProjectile(Position, Direction, Message.m_Emoticon, Effect == QmEmoticon::EEffect::SUPER_PROJECTILE, PlayerId);
				continue;
			}
			// 普通表情（没有特殊效果）：沿用本地既有行为，写到该玩家的头顶表情状态；
			// 是否显示仍由 players.cpp 的 cl_showemotes 判定，这里不改状态语义。
			GameClient()->m_aClients[PlayerId].m_Emoticon = Message.m_Emoticon;
			GameClient()->m_aClients[PlayerId].m_EmoticonStartTick = Client()->GameTick(g_Config.m_ClDummy);
			GameClient()->m_aClients[PlayerId].m_EmoticonStartFraction = Client()->IntraGameTickSincePrev(g_Config.m_ClDummy);
		}
	}

	float OldX0, OldY0, OldX1, OldY1;
	Graphics()->GetScreen(&OldX0, &OldY0, &OldX1, &OldY1);
	float Width, Height;
	Graphics()->CalcScreenParams(Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, &Width, &Height);
	const vec2 Center = GameClient()->m_Camera.m_Center;
	Graphics()->MapScreen(Center.x - Width / 2, Center.y - Height / 2, Center.x + Width / 2, Center.y + Height / 2);
	Graphics()->BlendNormal();
	const auto Solid = [this](int X, int Y) { return Collision()->CheckPoint(X * 32.0f + 16.0f, Y * 32.0f + 16.0f); };
	QmEmoticon::SPlayerBox aPlayerBoxes[MAX_CLIENTS];
	int NumPlayerBoxes = 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		if(GameClient()->m_aClients[ClientId].m_Active)
		{
			aPlayerBoxes[NumPlayerBoxes].m_ClientId = ClientId;
			aPlayerBoxes[NumPlayerBoxes].m_Pos = GameClient()->m_aClients[ClientId].m_RenderPos;
			aPlayerBoxes[NumPlayerBoxes].m_Half = CCharacterCore::PhysicalSize() * 0.5f;
			++NumPlayerBoxes;
		}

	for(auto &Projectile : m_aProjectiles)
	{
		if(!Projectile.m_Active || Projectile.m_Emoticon < 0 || Projectile.m_Emoticon >= NUM_EMOTICONS)
			continue;
		const QmEmoticon::CAlphaMask &Mask = m_aCollisionMasks[Projectile.m_Emoticon];
		Projectile.Update(std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f), Mask, Solid, aPlayerBoxes, NumPlayerBoxes);
		if(!Projectile.m_Active)
			continue;
		const float Fraction = std::clamp((float)(Projectile.m_Accumulator / CEmoticonProjectile::STEP), 0.0f, 1.0f);
		vec2 Position = mix(Projectile.m_PreviousPos, Projectile.m_Pos, Fraction);
		float Angle = mix(Projectile.m_PreviousAngle, Projectile.m_Angle, Fraction);
		// 插值可能切入墙角，重叠时使用已求解的位置和角度。
		if(Mask.Overlaps(Position, Projectile.Size(), Angle, Solid))
		{
			Position = Projectile.m_Pos;
			Angle = Projectile.m_Angle;
		}
		Graphics()->TextureSet(GameClient()->m_EmoticonsSkin.m_aSpriteEmoticons[Projectile.m_Emoticon]);
		Graphics()->QuadsBegin();
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		Graphics()->QuadsSetRotation(Angle);
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, std::clamp(Projectile.m_LifeTime * 2.0f, 0.0f, 1.0f));
		const float Size = Projectile.Size();
		IGraphics::CQuadItem Quad(Position.x, Position.y, Size, Size);
		Graphics()->QuadsDraw(&Quad, 1);
		Graphics()->QuadsEnd();
	}
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->MapScreen(OldX0, OldY0, OldX1, OldY1);
}

void CEmoticon::SpawnProjectile(vec2 Position, vec2 Direction, int Emoticon, bool Super, int OwnerClientId)
{
	if(Emoticon < 0 || Emoticon >= NUM_EMOTICONS)
		return;
	Direction = length(Direction) > 0.0001f ? normalize(Direction) : vec2(1.0f, 0.0f);
	CEmoticonProjectile *pProjectile = &m_aProjectiles[0];
	for(auto &Projectile : m_aProjectiles)
	{
		if(!Projectile.m_Active)
		{
			pProjectile = &Projectile;
			break;
		}
		if(Projectile.m_LifeTime < pProjectile->m_LifeTime)
			pProjectile = &Projectile;
	}
	pProjectile->Init(Position, Direction * 1200.0f + vec2(0.0f, -400.0f), Emoticon, Super ? 2.35f : 1.0f, OwnerClientId);
	pProjectile->m_Active = pProjectile->PlaceOutside(m_aCollisionMasks[Emoticon], [this](int X, int Y) {
		return Collision()->CheckPoint(X * 32.0f + 16.0f, Y * 32.0f + 16.0f);
	});
	if(Super)
		GameClient()->m_Effects.Explosion(Position, 0.9f);
	else
		GameClient()->m_Effects.HammerHit(Position, 0.65f, 0.0f);
}

void CEmoticon::Emote(int Emoticon, bool ForceLaunch)
{
	const QmEmoticon::EEffect Effect = QmEmoticon::ConsumeEffect(Emoticon, m_LaunchModeActive, m_SuperLaunchPending, ForceLaunch);
	if(Effect == QmEmoticon::EEffect::INVALID)
		return;
	const bool Launch = Effect == QmEmoticon::EEffect::PROJECTILE || Effect == QmEmoticon::EEffect::SUPER_PROJECTILE;
	const bool Super = Effect == QmEmoticon::EEffect::SUPER_HEAD || Effect == QmEmoticon::EEffect::SUPER_PROJECTILE;
	// 头顶大表情：本机自己也要记一份，供 players.cpp 放大绘制。
	if(Effect == QmEmoticon::EEffect::SUPER_HEAD)
	{
		m_LocalSuperHeadEmoticon = Emoticon;
		m_LocalSuperHeadExpireTick = Client()->GameTick(g_Config.m_ClDummy) + 2 * Client()->GameTickSpeed();
	}
	else
	{
		m_LocalSuperHeadEmoticon = -1;
		m_LocalSuperHeadExpireTick = -1;
	}
	const int LocalClientId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(Launch)
		SpawnProjectile(GameClient()->m_LocalCharacterPos - vec2(0.0f, 20.0f), GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy], Emoticon, Super, LocalClientId);

	CNetMsg_Cl_Emoticon Msg;
	Msg.m_Emoticon = Emoticon;
	Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);

	if(g_Config.m_ClDummyCopyMoves)
	{
		CMsgPacker MsgDummy(NETMSGTYPE_CL_EMOTICON, false);
		MsgDummy.AddInt(Emoticon);
		Client()->SendMsg(!g_Config.m_ClDummy, &MsgDummy, MSGFLAG_VITAL);
	}
	// 发射与头顶大表情都要广播（前者生成投射物，后者让别人的头顶表情放大）。
	if(LocalClientId >= 0 && Effect != QmEmoticon::EEffect::NONE)
		GameClient()->m_QmClient.SendQmAnonymousEmoticon(Emoticon, LocalClientId, Launch, Super);
}

void CEmoticon::SuperEmote(int Emoticon)
{
	m_SuperLaunchPending = true;
	Emote(Emoticon);
}

bool CEmoticon::IsLocalSuperHeadEmoticon(int ClientId, int Emoticon) const
{
	const int LocalClientId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(LocalClientId >= 0 && ClientId == LocalClientId && Emoticon == m_LocalSuperHeadEmoticon &&
		m_LocalSuperHeadExpireTick >= 0 && Client()->GameTick(g_Config.m_ClDummy) <= m_LocalSuperHeadExpireTick)
		return true;
	if(!g_Config.m_QmShowOtherSuperEmotes || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	return Emoticon == m_aRemoteSuperHeadEmoticons[ClientId] &&
	       m_aRemoteSuperHeadExpireTicks[ClientId] >= 0 &&
	       Client()->GameTick(g_Config.m_ClDummy) <= m_aRemoteSuperHeadExpireTicks[ClientId];
}

void CEmoticon::EyeEmote(int Emote)
{
	char aBuf[32];
	switch(Emote)
	{
	case EMOTE_NORMAL:
		str_format(aBuf, sizeof(aBuf), "/emote normal %d", g_Config.m_ClEyeDuration);
		break;
	case EMOTE_PAIN:
		str_format(aBuf, sizeof(aBuf), "/emote pain %d", g_Config.m_ClEyeDuration);
		break;
	case EMOTE_HAPPY:
		str_format(aBuf, sizeof(aBuf), "/emote happy %d", g_Config.m_ClEyeDuration);
		break;
	case EMOTE_SURPRISE:
		str_format(aBuf, sizeof(aBuf), "/emote surprise %d", g_Config.m_ClEyeDuration);
		break;
	case EMOTE_ANGRY:
		str_format(aBuf, sizeof(aBuf), "/emote angry %d", g_Config.m_ClEyeDuration);
		break;
	case EMOTE_BLINK:
		str_format(aBuf, sizeof(aBuf), "/emote blink %d", g_Config.m_ClEyeDuration);
		break;
	}
	GameClient()->m_Chat.SendChat(0, aBuf);
}

void CEmoticon::TriggerLocalBlink()
{
	const int Dummy = g_Config.m_ClDummy;
	const int ClientId = GameClient()->m_aLocalIds[Dummy];
	if(Client()->State() != IClient::STATE_ONLINE || ClientId < 0 || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return;

	m_aLocalBlinkStates[Dummy].Trigger(Client()->GameTick(Dummy));
}

bool CEmoticon::ShouldRenderLocalBlink(int ClientId) const
{
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		if(GameClient()->m_aLocalIds[Dummy] == ClientId && m_aLocalBlinkStates[Dummy].IsActive(Client()->GameTick(Dummy)))
			return true;
	}
	return false;
}
