/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "hud_editor.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/components/controls.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr float EPSILON = 0.001f;

float Clamp01(float Value)
{
	return std::clamp(Value, 0.0f, 1.0f);
}
}

CHudEditor::CHudEditor()
{
	ResetRuntimeState();
}

void CHudEditor::ResetRuntimeState()
{
	m_DraggingElement = -1;
	m_DragGrabOffset = vec2(0.0f, 0.0f);
	m_vVisibleElements.clear();
}

void CHudEditor::OnReset()
{
	SetActive(false);
	ResetRuntimeState();
}

void CHudEditor::OnRelease()
{
	ResetRuntimeState();
}

void CHudEditor::OnStateChange(int NewState, int OldState)
{
	if((OldState == IClient::STATE_ONLINE || OldState == IClient::STATE_DEMOPLAYBACK) &&
		NewState != IClient::STATE_ONLINE && NewState != IClient::STATE_DEMOPLAYBACK)
	{
		SetActive(false);
	}
}

void CHudEditor::OnUpdate()
{
	m_vVisibleElements.clear();
}

bool CHudEditor::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);
	return true;
}

bool CHudEditor::OnInput(const IInput::CEvent &Event)
{
	if(!m_Active)
		return false;

	if((Event.m_Flags & IInput::FLAG_PRESS) != 0 && Event.m_Key == KEY_ESCAPE)
	{
		SetActive(false);
		return true;
	}

	Ui()->OnInput(Event);
	return true;
}

void CHudEditor::SetActive(bool Active)
{
	if(m_Active == Active)
		return;

	m_Active = Active;
	m_InteractionUiActive = false;
	Ui()->SetHotItem(nullptr);
	Ui()->SetActiveItem(nullptr);
	if(!m_Active)
	{
		ResetRuntimeState();
		if(m_DirtyLayout)
			SaveLayoutConfig();
	}
}

void CHudEditor::UpdateVisibleRect(EHudEditorElement Element, const CUIRect &RenderedRect)
{
	const int VisibleIndex = FindVisibleElementIndex(Element);
	if(VisibleIndex < 0 || RenderedRect.w <= 0.0f || RenderedRect.h <= 0.0f)
		return;

	float ScreenX0 = 0.0f;
	float ScreenY0 = 0.0f;
	float ScreenX1 = 0.0f;
	float ScreenY1 = 0.0f;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float ScreenW = maximum(EPSILON, ScreenX1 - ScreenX0);
	const float ScreenH = maximum(EPSILON, ScreenY1 - ScreenY0);

	const CUIRect *pUiScreen = Ui()->Screen();
	if(pUiScreen == nullptr || pUiScreen->w <= 0.0f || pUiScreen->h <= 0.0f)
		return;

	SVisibleElement &Visible = m_vVisibleElements[VisibleIndex];
	Visible.m_Rect = {
		pUiScreen->x + (RenderedRect.x - ScreenX0) * pUiScreen->w / ScreenW,
		pUiScreen->y + (RenderedRect.y - ScreenY0) * pUiScreen->h / ScreenH,
		RenderedRect.w * pUiScreen->w / ScreenW,
		RenderedRect.h * pUiScreen->h / ScreenH};

	const SElementState &State = this->State(Element);
	const float Scale = std::clamp(State.m_HasCustom ? State.m_ScalePercent / 100.0f : 1.0f, MIN_SCALE_PERCENT / 100.0f, MAX_SCALE_PERCENT / 100.0f);
	Visible.m_BaseWidth = Visible.m_Rect.w / Scale;
	Visible.m_BaseHeight = Visible.m_Rect.h / Scale;
}

const char *CHudEditor::ElementToken(EHudEditorElement Element)
{
	switch(Element)
	{
	case EHudEditorElement::HudMain: return "hud_main";
	case EHudEditorElement::HudPlayerState: return "hud_player_state";
	case EHudEditorElement::GameTimer: return "game_timer";
	case EHudEditorElement::PauseNotification: return "pause_notification";
	case EHudEditorElement::SuddenDeath: return "sudden_death";
	case EHudEditorElement::ScoreHud: return "score_hud";
	case EHudEditorElement::WarmupTimer: return "warmup_timer";
	case EHudEditorElement::DummyActions: return "dummy_actions";
	case EHudEditorElement::DummyMiniMap: return "dummy_minimap";
	case EHudEditorElement::TextInfo: return "text_info";
	case EHudEditorElement::SpectatorCount: return "spectator_count";
	case EHudEditorElement::MovementInfo: return "movement_info";
	case EHudEditorElement::MapProgressBar: return "map_progress_bar";
	case EHudEditorElement::SpectatorHud: return "spectator_hud";
	case EHudEditorElement::LocalTime: return "local_time";
	case EHudEditorElement::LegacyMediaInfo: return "legacy_media_info";
	case EHudEditorElement::MediaIsland: return "media_island";
	case EHudEditorElement::Voting: return "voting";
	case EHudEditorElement::Chat: return "chat";
	case EHudEditorElement::VoiceOverlay: return "voice_overlay";
	case EHudEditorElement::InputOverlay: return "input_overlay";
	case EHudEditorElement::Count: break;
	}
	return "";
}

int CHudEditor::ElementFromToken(const char *pToken)
{
	for(int i = 0; i < ELEMENT_COUNT; ++i)
	{
		const auto Element = static_cast<EHudEditorElement>(i);
		if(str_comp(ElementToken(Element), pToken) == 0)
			return i;
	}
	return -1;
}

void CHudEditor::ParseLayoutConfig(const char *pConfig)
{
	for(SElementState &State : m_aElementStates)
		State = SElementState{};

	if(pConfig == nullptr || pConfig[0] == '\0')
		return;

	char aBuffer[sizeof(g_Config.m_QmHudEditorLayout)];
	str_copy(aBuffer, pConfig, sizeof(aBuffer));

	char *pEntry = aBuffer;
	while(pEntry != nullptr && pEntry[0] != '\0')
	{
		char *pNextEntry = const_cast<char *>(str_find(pEntry, ";"));
		if(pNextEntry != nullptr)
		{
			*pNextEntry = '\0';
			++pNextEntry;
		}

		char *pColon = const_cast<char *>(str_find(pEntry, ":"));
		if(pColon != nullptr)
		{
			*pColon = '\0';
			const int ElementIndex = ElementFromToken(pEntry);
			if(ElementIndex >= 0)
			{
				int aValues[3] = {};
				int ValueCount = 0;
				char *pValue = pColon + 1;
				while(pValue != nullptr && pValue[0] != '\0' && ValueCount < 3)
				{
					char *pNextValue = const_cast<char *>(str_find(pValue, ","));
					if(pNextValue != nullptr)
					{
						*pNextValue = '\0';
						++pNextValue;
					}
					aValues[ValueCount++] = str_toint(pValue);
					pValue = pNextValue;
				}

				if(ValueCount == 3)
				{
					SElementState &State = m_aElementStates[ElementIndex];
					State.m_HasCustom = true;
					State.m_PosXPermille = std::clamp(aValues[0], 0, POSITION_SCALE);
					State.m_PosYPermille = std::clamp(aValues[1], 0, POSITION_SCALE);
					State.m_ScalePercent = std::clamp(aValues[2], MIN_SCALE_PERCENT, MAX_SCALE_PERCENT);
				}
			}
		}

		pEntry = pNextEntry;
	}
}

void CHudEditor::SyncLayoutConfig()
{
	if(m_LayoutLoaded && str_comp(m_aLayoutCache, g_Config.m_QmHudEditorLayout) == 0)
		return;

	ParseLayoutConfig(g_Config.m_QmHudEditorLayout);
	str_copy(m_aLayoutCache, g_Config.m_QmHudEditorLayout, sizeof(m_aLayoutCache));
	m_LayoutLoaded = true;
}

void CHudEditor::SaveLayoutConfig()
{
	char aSerialized[sizeof(g_Config.m_QmHudEditorLayout)] = {};
	bool First = true;
	for(int i = 0; i < ELEMENT_COUNT; ++i)
	{
		const SElementState &State = m_aElementStates[i];
		if(!State.m_HasCustom)
			continue;

		char aEntry[96];
		str_format(aEntry, sizeof(aEntry), "%s%s:%d,%d,%d",
			First ? "" : ";",
			ElementToken(static_cast<EHudEditorElement>(i)),
			State.m_PosXPermille,
			State.m_PosYPermille,
			State.m_ScalePercent);
		str_append(aSerialized, aEntry, sizeof(aSerialized));
		First = false;
	}

	str_copy(g_Config.m_QmHudEditorLayout, aSerialized, sizeof(g_Config.m_QmHudEditorLayout));
	str_copy(m_aLayoutCache, g_Config.m_QmHudEditorLayout, sizeof(m_aLayoutCache));
	ConfigManager()->Save();
	m_DirtyLayout = false;
}

CHudEditor::SElementState &CHudEditor::EnsureState(EHudEditorElement Element)
{
	SyncLayoutConfig();
	return m_aElementStates[static_cast<int>(Element)];
}

const CHudEditor::SElementState &CHudEditor::State(EHudEditorElement Element) const
{
	return m_aElementStates[static_cast<int>(Element)];
}

void CHudEditor::ClampStateToScreen(SElementState &State, float BaseWidth, float BaseHeight) const
{
	const CUIRect *pScreen = Ui()->Screen();
	if(pScreen == nullptr || pScreen->w <= 0.0f || pScreen->h <= 0.0f)
		return;

	const float Scale = std::clamp(State.m_ScalePercent / 100.0f, MIN_SCALE_PERCENT / 100.0f, MAX_SCALE_PERCENT / 100.0f);
	const float Width = BaseWidth * Scale;
	const float Height = BaseHeight * Scale;
	const float XNorm = Clamp01(State.m_PosXPermille / (float)POSITION_SCALE);
	const float YNorm = Clamp01(State.m_PosYPermille / (float)POSITION_SCALE);
	float X = pScreen->x + XNorm * pScreen->w;
	float Y = pScreen->y + YNorm * pScreen->h;

	const float MaxX = Width >= pScreen->w ? pScreen->x : pScreen->x + pScreen->w - Width;
	const float MaxY = Height >= pScreen->h ? pScreen->y : pScreen->y + pScreen->h - Height;
	X = std::clamp(X, pScreen->x, MaxX);
	Y = std::clamp(Y, pScreen->y, MaxY);

	State.m_PosXPermille = std::clamp(round_to_int((X - pScreen->x) / pScreen->w * POSITION_SCALE), 0, POSITION_SCALE);
	State.m_PosYPermille = std::clamp(round_to_int((Y - pScreen->y) / pScreen->h * POSITION_SCALE), 0, POSITION_SCALE);
}

CHudEditor::STransformScope CHudEditor::BeginTransform(EHudEditorElement Element, const CUIRect &DefaultRect, bool Scalable, bool ApplyMapScreen)
{
	STransformScope Scope;
	if(DefaultRect.w <= 0.0f || DefaultRect.h <= 0.0f)
		return Scope;

	SyncLayoutConfig();

	float ScreenX0 = 0.0f;
	float ScreenY0 = 0.0f;
	float ScreenX1 = 0.0f;
	float ScreenY1 = 0.0f;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float ScreenW = maximum(EPSILON, ScreenX1 - ScreenX0);
	const float ScreenH = maximum(EPSILON, ScreenY1 - ScreenY0);

	const CUIRect *pUiScreen = Ui()->Screen();
	if(pUiScreen == nullptr || pUiScreen->w <= 0.0f || pUiScreen->h <= 0.0f)
		return Scope;

	const float DefaultNormX = Clamp01((DefaultRect.x - ScreenX0) / ScreenW);
	const float DefaultNormY = Clamp01((DefaultRect.y - ScreenY0) / ScreenH);
	const float BaseUiWidth = DefaultRect.w * pUiScreen->w / ScreenW;
	const float BaseUiHeight = DefaultRect.h * pUiScreen->h / ScreenH;

	const SElementState &SavedState = State(Element);
	const float Scale = std::clamp(SavedState.m_HasCustom ? SavedState.m_ScalePercent / 100.0f : 1.0f, MIN_SCALE_PERCENT / 100.0f, MAX_SCALE_PERCENT / 100.0f);
	const float NormX = SavedState.m_HasCustom ? Clamp01(SavedState.m_PosXPermille / (float)POSITION_SCALE) : DefaultNormX;
	const float NormY = SavedState.m_HasCustom ? Clamp01(SavedState.m_PosYPermille / (float)POSITION_SCALE) : DefaultNormY;
	float AnchorX = ScreenX0 + NormX * ScreenW;
	float AnchorY = ScreenY0 + NormY * ScreenH;

	const float Width = DefaultRect.w * Scale;
	const float Height = DefaultRect.h * Scale;
	const float MaxX = Width >= ScreenW ? ScreenX0 : ScreenX0 + ScreenW - Width;
	const float MaxY = Height >= ScreenH ? ScreenY0 : ScreenY0 + ScreenH - Height;
	AnchorX = std::clamp(AnchorX, ScreenX0, MaxX);
	AnchorY = std::clamp(AnchorY, ScreenY0, MaxY);

	SVisibleElement Visible;
	Visible.m_Element = Element;
	Visible.m_Rect = {
			pUiScreen->x + (AnchorX - ScreenX0) * pUiScreen->w / ScreenW,
			pUiScreen->y + (AnchorY - ScreenY0) * pUiScreen->h / ScreenH,
			BaseUiWidth * Scale,
			BaseUiHeight * Scale};
	Visible.m_BaseWidth = BaseUiWidth;
	Visible.m_BaseHeight = BaseUiHeight;
	Visible.m_Scalable = Scalable;
	m_vVisibleElements.push_back(Visible);
	Scope.m_TargetRect = {AnchorX, AnchorY, Width, Height};

	const bool Transformed =
		std::fabs(AnchorX - DefaultRect.x) > EPSILON ||
		std::fabs(AnchorY - DefaultRect.y) > EPSILON ||
		std::fabs(Scale - 1.0f) > EPSILON;
	if(!Transformed || !ApplyMapScreen)
		return Scope;

	Scope.m_Applied = true;
	Scope.m_ScreenX0 = ScreenX0;
	Scope.m_ScreenY0 = ScreenY0;
	Scope.m_ScreenX1 = ScreenX1;
	Scope.m_ScreenY1 = ScreenY1;

	const float NewScreenX0 = DefaultRect.x - (AnchorX - ScreenX0) / Scale;
	const float NewScreenY0 = DefaultRect.y - (AnchorY - ScreenY0) / Scale;
	Graphics()->MapScreen(NewScreenX0, NewScreenY0, NewScreenX0 + ScreenW / Scale, NewScreenY0 + ScreenH / Scale);
	return Scope;
}

void CHudEditor::EndTransform(const STransformScope &Scope)
{
	if(!Scope.m_Applied)
		return;

	Graphics()->MapScreen(Scope.m_ScreenX0, Scope.m_ScreenY0, Scope.m_ScreenX1, Scope.m_ScreenY1);
}

int CHudEditor::FindVisibleElementIndex(EHudEditorElement Element) const
{
	for(size_t i = 0; i < m_vVisibleElements.size(); ++i)
	{
		if(m_vVisibleElements[i].m_Element == Element)
			return static_cast<int>(i);
	}
	return -1;
}

int CHudEditor::FindHoveredVisibleElement() const
{
	const vec2 Mouse(Ui()->MouseX(), Ui()->MouseY());
	for(int i = (int)m_vVisibleElements.size() - 1; i >= 0; --i)
	{
		if(m_vVisibleElements[i].m_Rect.Inside(Mouse))
			return i;
	}
	return -1;
}

void CHudEditor::UpdateInteractionUi()
{
	if(m_InteractionUiActive)
		return;

	Ui()->StartCheck();
	Ui()->Update();
	m_InteractionUiActive = true;
}

void CHudEditor::OnRender()
{
	if(!m_Active)
		return;

	const CUIRect *pUiScreen = Ui()->Screen();
	if(pUiScreen == nullptr)
		return;

	UpdateInteractionUi();

	const int HoveredIndex = FindHoveredVisibleElement();
	if(m_DraggingElement >= 0)
	{
		const bool MouseReleased = !Ui()->MouseButton(0) && Ui()->LastMouseButton(0);
		if(MouseReleased || FindVisibleElementIndex(static_cast<EHudEditorElement>(m_DraggingElement)) < 0)
		{
			m_DraggingElement = -1;
			m_DragGrabOffset = vec2(0.0f, 0.0f);
			if(m_DirtyLayout)
				SaveLayoutConfig();
		}
	}

	if(HoveredIndex >= 0 && m_DraggingElement < 0 && Ui()->MouseButtonClicked(0) && Ui()->ActiveItem() == nullptr)
	{
		m_DraggingElement = static_cast<int>(m_vVisibleElements[HoveredIndex].m_Element);
		m_DragGrabOffset = vec2(Ui()->MouseX() - m_vVisibleElements[HoveredIndex].m_Rect.x, Ui()->MouseY() - m_vVisibleElements[HoveredIndex].m_Rect.y);
	}

	if(m_DraggingElement >= 0 && Ui()->MouseButton(0))
	{
		const int VisibleIndex = FindVisibleElementIndex(static_cast<EHudEditorElement>(m_DraggingElement));
		if(VisibleIndex >= 0)
		{
			const SVisibleElement &Visible = m_vVisibleElements[VisibleIndex];
			SElementState &State = EnsureState(Visible.m_Element);
			State.m_HasCustom = true;
			const float Scale = std::clamp(State.m_ScalePercent / 100.0f, MIN_SCALE_PERCENT / 100.0f, MAX_SCALE_PERCENT / 100.0f);
			const float Width = Visible.m_BaseWidth * Scale;
			const float Height = Visible.m_BaseHeight * Scale;
			const float MaxX = Width >= pUiScreen->w ? pUiScreen->x : pUiScreen->x + pUiScreen->w - Width;
			const float MaxY = Height >= pUiScreen->h ? pUiScreen->y : pUiScreen->y + pUiScreen->h - Height;
			const float X = std::clamp(Ui()->MouseX() - m_DragGrabOffset.x, pUiScreen->x, MaxX);
			const float Y = std::clamp(Ui()->MouseY() - m_DragGrabOffset.y, pUiScreen->y, MaxY);
			State.m_PosXPermille = std::clamp(round_to_int((X - pUiScreen->x) / pUiScreen->w * POSITION_SCALE), 0, POSITION_SCALE);
			State.m_PosYPermille = std::clamp(round_to_int((Y - pUiScreen->y) / pUiScreen->h * POSITION_SCALE), 0, POSITION_SCALE);
			m_DirtyLayout = true;
		}
	}

	if(HoveredIndex >= 0)
	{
		const SVisibleElement &Visible = m_vVisibleElements[HoveredIndex];
		SElementState &State = EnsureState(Visible.m_Element);
		int DeltaScale = 0;
		if(Visible.m_Scalable && Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
			DeltaScale += 5;
		if(Visible.m_Scalable && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
			DeltaScale -= 5;
		if(DeltaScale != 0)
		{
			State.m_HasCustom = true;
			State.m_ScalePercent = std::clamp(State.m_ScalePercent + DeltaScale, MIN_SCALE_PERCENT, MAX_SCALE_PERCENT);
			ClampStateToScreen(State, Visible.m_BaseWidth, Visible.m_BaseHeight);
			m_DirtyLayout = true;
			SaveLayoutConfig();
		}
	}

	float PrevX0 = 0.0f;
	float PrevY0 = 0.0f;
	float PrevX1 = 0.0f;
	float PrevY1 = 0.0f;
	Graphics()->GetScreen(&PrevX0, &PrevY0, &PrevX1, &PrevY1);
	Graphics()->MapScreen(pUiScreen->x, pUiScreen->y, pUiScreen->x + pUiScreen->w, pUiScreen->y + pUiScreen->h);

	for(size_t i = 0; i < m_vVisibleElements.size(); ++i)
	{
		const bool Hovered = static_cast<int>(i) == HoveredIndex;
		const bool Dragging = m_DraggingElement >= 0 && static_cast<int>(m_vVisibleElements[i].m_Element) == m_DraggingElement;
		if(!Hovered && !Dragging)
			continue;

		const ColorRGBA FillColor = Dragging ? ColorRGBA(1.0f, 0.75f, 0.15f, 0.10f) : (Hovered ? ColorRGBA(0.35f, 0.75f, 1.0f, 0.10f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.04f));
		const ColorRGBA BorderColor = Dragging ? ColorRGBA(1.0f, 0.82f, 0.20f, 0.95f) : (Hovered ? ColorRGBA(0.35f, 0.80f, 1.0f, 0.90f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.55f));
		const float BorderSize = Dragging ? 2.5f : 1.5f;
		m_vVisibleElements[i].m_Rect.Draw(FillColor, IGraphics::CORNER_ALL, 6.0f);

		CUIRect Line = m_vVisibleElements[i].m_Rect;
		Line.HSplitTop(BorderSize, &Line, nullptr);
		Line.Draw(BorderColor, IGraphics::CORNER_NONE, 0.0f);
		Line = m_vVisibleElements[i].m_Rect;
		Line.HSplitBottom(BorderSize, nullptr, &Line);
		Line.Draw(BorderColor, IGraphics::CORNER_NONE, 0.0f);
		Line = m_vVisibleElements[i].m_Rect;
		Line.VSplitLeft(BorderSize, &Line, nullptr);
		Line.Draw(BorderColor, IGraphics::CORNER_NONE, 0.0f);
		Line = m_vVisibleElements[i].m_Rect;
		Line.VSplitRight(BorderSize, nullptr, &Line);
		Line.Draw(BorderColor, IGraphics::CORNER_NONE, 0.0f);
	}

	constexpr float HelpFontSize = 6.0f;
	constexpr float HelpPaddingX = 8.0f;
	constexpr float HelpPaddingY = 5.0f;
	constexpr float HelpLineHeight = 8.0f;
	const char *apHelpLines[] = {
		Localize("Drag HUD modules with the left mouse button"),
		Localize("Use the mouse wheel on a hovered module to scale it by 5%"),
		Localize("Press Esc to exit the HUD editor"),
	};
	float HelpWidth = 0.0f;
	for(const char *pLine : apHelpLines)
		HelpWidth = maximum(HelpWidth, TextRender()->TextWidth(HelpFontSize, pLine, -1, -1.0f));
	const float HelpHeight = HelpPaddingY * 2.0f + HelpLineHeight * (float)std::size(apHelpLines);
	const float HelpX = pUiScreen->x + (pUiScreen->w - (HelpWidth + HelpPaddingX * 2.0f)) * 0.5f;
	const float HelpY = pUiScreen->y + pUiScreen->h - HelpHeight - 10.0f;
	Graphics()->DrawRect(HelpX, HelpY, HelpWidth + HelpPaddingX * 2.0f, HelpHeight, ColorRGBA(0.03f, 0.04f, 0.06f, 0.78f), IGraphics::CORNER_ALL, 6.0f);
	for(size_t i = 0; i < std::size(apHelpLines); ++i)
	{
		TextRender()->Text(HelpX + HelpPaddingX, HelpY + HelpPaddingY + HelpLineHeight * i, HelpFontSize, apHelpLines[i], -1.0f);
	}

	RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);

	Graphics()->MapScreen(PrevX0, PrevY0, PrevX1, PrevY1);
	Ui()->FinishCheck();
	m_InteractionUiActive = false;
}
