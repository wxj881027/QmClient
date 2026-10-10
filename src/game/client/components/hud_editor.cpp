/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "hud_editor.h"

#include "jump_hint_utils.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/controls.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float EPSILON = QmHudEditor::EPSILON;
	constexpr float HUD_EDITOR_EDGE_COINCIDENCE_DISTANCE = QmHudEditor::EDGE_COINCIDENCE_DISTANCE;

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
	m_aVisibleBounds = {};
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

void CHudEditor::BeginRenderFrame()
{
	for(QmHudEditor::CVisibleBounds &Bounds : m_aVisibleBounds)
		Bounds.BeginRenderFrame();
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

	if(m_JumpHintTextEditorActive)
	{
		if((Event.m_Flags & IInput::FLAG_PRESS) != 0 && Event.m_Key == KEY_ESCAPE)
		{
			CloseJumpHintTextEditor();
			return true;
		}
		if((Event.m_Flags & IInput::FLAG_PRESS) != 0 && (Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER))
		{
			if(Input()->ModifierIsPressed())
			{
				SaveJumpHintTextEditor();
			}
			else if(m_JumpHintTextInput.IsActive())
			{
				m_JumpHintTextInput.SetRange("\n", m_JumpHintTextInput.GetSelectionStart(), m_JumpHintTextInput.GetSelectionEnd());
			}
			return true;
		}
	}

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
		CloseJumpHintTextEditor();
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
	// 命中、拖动与下一帧恢复共同使用实际内容边界。
	Visible.m_StateOffsetX = (Visible.m_Rect.x - Visible.m_TargetUiRect.x) / Scale;
	Visible.m_StateOffsetY = (Visible.m_Rect.y - Visible.m_TargetUiRect.y) / Scale;
	m_aVisibleBounds[static_cast<int>(Element)].Observe(Visible.m_TransformRect, Visible.m_DeclaredVisibleRect, Visible.m_TargetUiRect, Visible.m_Rect);
}

const char *CHudEditor::ElementToken(EHudEditorElement Element)
{
	return QmHudEditor::ElementToken(Element);
}

int CHudEditor::ElementFromToken(const char *pToken)
{
	return QmHudEditor::ElementFromToken(pToken);
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
					State.m_PosXPermille = std::clamp(aValues[0], -2 * POSITION_SCALE, POSITION_SCALE);
					State.m_PosYPermille = std::clamp(aValues[1], -2 * POSITION_SCALE, POSITION_SCALE);
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

void CHudEditor::ResetLayoutConfig()
{
	for(SElementState &State : m_aElementStates)
		State = SElementState{};

	g_Config.m_QmHudEditorLayout[0] = '\0';
	m_aLayoutCache[0] = '\0';
	m_LayoutLoaded = true;
	m_DirtyLayout = false;
	m_DraggingElement = -1;
	m_DragGrabOffset = vec2(0.0f, 0.0f);
	ConfigManager()->Save();
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

void CHudEditor::ClampStateToScreen(SElementState &State, float BaseWidth, float BaseHeight, float StateOffsetX, float StateOffsetY) const
{
	const CUIRect *pScreen = Ui()->Screen();
	if(pScreen == nullptr || pScreen->w <= 0.0f || pScreen->h <= 0.0f)
		return;

	const float Scale = std::clamp(State.m_ScalePercent / 100.0f, MIN_SCALE_PERCENT / 100.0f, MAX_SCALE_PERCENT / 100.0f);
	const float Width = BaseWidth * Scale;
	const float Height = BaseHeight * Scale;
	const bool AnchorRight = State.m_PosXPermille >= POSITION_SCALE;
	const bool AnchorBottom = State.m_PosYPermille >= POSITION_SCALE;
	const float XNorm = QmHudEditor::ClampStoredAxisPosition(State.m_PosXPermille / (float)POSITION_SCALE);
	const float YNorm = QmHudEditor::ClampStoredAxisPosition(State.m_PosYPermille / (float)POSITION_SCALE);
	const float OffsetX = StateOffsetX * Scale;
	const float OffsetY = StateOffsetY * Scale;
	const float X = QmHudEditor::RestoreAxisAnchor(XNorm, Width, pScreen->x, pScreen->w, OffsetX);
	const float Y = QmHudEditor::RestoreAxisAnchor(YNorm, Height, pScreen->y, pScreen->h, OffsetY);

	State.m_PosXPermille = AnchorRight ? POSITION_SCALE : round_to_int(QmHudEditor::StoreAxisAnchor(X, Width, pScreen->x, pScreen->w, OffsetX) * POSITION_SCALE);
	State.m_PosYPermille = AnchorBottom ? POSITION_SCALE : round_to_int(QmHudEditor::StoreAxisAnchor(Y, Height, pScreen->y, pScreen->h, OffsetY) * POSITION_SCALE);
}

CHudEditor::STransformScope CHudEditor::BeginTransform(EHudEditorElement Element, const CUIRect &DefaultRect, bool Scalable, bool ApplyMapScreen)
{
	return BeginTransform(Element, DefaultRect, DefaultRect, QmHudEditor::SEdgeMargin{}, Scalable, ApplyMapScreen);
}

CHudEditor::STransformScope CHudEditor::BeginTransform(EHudEditorElement Element, const CUIRect &TransformRect, const CUIRect &VisibleRect, bool Scalable, bool ApplyMapScreen)
{
	return BeginTransform(Element, TransformRect, VisibleRect, QmHudEditor::SEdgeMargin{}, Scalable, ApplyMapScreen);
}

bool CHudEditor::ComputeTransformPlacement(EHudEditorElement Element, const CUIRect &TransformRect, const CUIRect &VisibleRect, bool Scalable, STransformScope &Scope, SVisibleElement *pVisible, const QmHudEditor::SEdgeMargin &EdgeMargin)
{
	const CUIRect EffectiveVisibleRect = m_aVisibleBounds[static_cast<int>(Element)].Resolve(TransformRect, VisibleRect);
	if(TransformRect.w <= 0.0f || TransformRect.h <= 0.0f || EffectiveVisibleRect.w <= 0.0f || EffectiveVisibleRect.h <= 0.0f)
		return false;

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
		return false;

	const float SafeLeft = maximum(0.0f, EdgeMargin.m_Left);
	const float SafeRight = maximum(0.0f, EdgeMargin.m_Right);
	const float SafeTop = maximum(0.0f, EdgeMargin.m_Top);
	const float SafeBottom = maximum(0.0f, EdgeMargin.m_Bottom);
	const float EffScreenX0 = ScreenX0 + SafeLeft;
	const float EffScreenY0 = ScreenY0 + SafeTop;
	const float EffScreenW = maximum(EPSILON, ScreenW - SafeLeft - SafeRight);
	const float EffScreenH = maximum(EPSILON, ScreenH - SafeTop - SafeBottom);

	const float DefaultNormX = Clamp01((TransformRect.x - ScreenX0) / ScreenW);
	const float DefaultNormY = Clamp01((TransformRect.y - ScreenY0) / ScreenH);
	const float BaseUiWidth = EffectiveVisibleRect.w * pUiScreen->w / ScreenW;
	const float BaseUiHeight = EffectiveVisibleRect.h * pUiScreen->h / ScreenH;
	const float TransformToVisibleOffsetX = EffectiveVisibleRect.x - TransformRect.x;
	const float TransformToVisibleOffsetY = EffectiveVisibleRect.y - TransformRect.y;

	const SElementState &SavedState = State(Element);
	const float Scale = std::clamp(SavedState.m_HasCustom ? SavedState.m_ScalePercent / 100.0f : 1.0f, MIN_SCALE_PERCENT / 100.0f, MAX_SCALE_PERCENT / 100.0f);
	const float NormX = SavedState.m_HasCustom ? QmHudEditor::ClampStoredAxisPosition(SavedState.m_PosXPermille / (float)POSITION_SCALE) : DefaultNormX;
	const float NormY = SavedState.m_HasCustom ? QmHudEditor::ClampStoredAxisPosition(SavedState.m_PosYPermille / (float)POSITION_SCALE) : DefaultNormY;

	const float TransformWidth = TransformRect.w * Scale;
	const float TransformHeight = TransformRect.h * Scale;
	const float VisibleWidth = EffectiveVisibleRect.w * Scale;
	const float VisibleHeight = EffectiveVisibleRect.h * Scale;
	const float VisibleOffsetX = TransformToVisibleOffsetX * Scale;
	const float VisibleOffsetY = TransformToVisibleOffsetY * Scale;
	// 默认布局保留设计留白；手动布局按真实可见屏幕边恢复，避免每次重进又被内推。
	const float AnchorX = SavedState.m_HasCustom ?
				      QmHudEditor::RestoreAxisAnchor(NormX, VisibleWidth, ScreenX0, ScreenW, VisibleOffsetX) :
				      QmHudEditor::SnapAxisToScreenEdgesEx(ScreenX0 + NormX * ScreenW, VisibleWidth, EffScreenX0, EffScreenW, VisibleOffsetX);
	const float AnchorY = SavedState.m_HasCustom ?
				      QmHudEditor::RestoreAxisAnchor(NormY, VisibleHeight, ScreenY0, ScreenH, VisibleOffsetY) :
				      QmHudEditor::SnapAxisToScreenEdgesEx(ScreenY0 + NormY * ScreenH, VisibleHeight, EffScreenY0, EffScreenH, VisibleOffsetY);

	Scope.m_TargetRect = {AnchorX, AnchorY, TransformWidth, TransformHeight};
	Scope.m_VisibleRect = {AnchorX + VisibleOffsetX, AnchorY + VisibleOffsetY, VisibleWidth, VisibleHeight};
	Scope.m_ScreenX0 = ScreenX0;
	Scope.m_ScreenY0 = ScreenY0;
	Scope.m_ScreenX1 = ScreenX1;
	Scope.m_ScreenY1 = ScreenY1;
	// 贴边判定只认重合：HUD 可见边与窗口边真正重合时才算贴边。
	// 邻近但未重合的元素保持普通圆角、不输出贴边锚定，也不施加贴边边距。
	Scope.m_AnchoredLeft = std::fabs(Scope.m_VisibleRect.x - ScreenX0) <= HUD_EDITOR_EDGE_COINCIDENCE_DISTANCE;
	Scope.m_AnchoredRight = std::fabs(Scope.m_VisibleRect.x + Scope.m_VisibleRect.w - ScreenX1) <= HUD_EDITOR_EDGE_COINCIDENCE_DISTANCE;
	Scope.m_AnchoredTop = std::fabs(Scope.m_VisibleRect.y - ScreenY0) <= HUD_EDITOR_EDGE_COINCIDENCE_DISTANCE;
	Scope.m_AnchoredBottom = std::fabs(Scope.m_VisibleRect.y + Scope.m_VisibleRect.h - ScreenY1) <= HUD_EDITOR_EDGE_COINCIDENCE_DISTANCE;
	// 可见边已经贴到窗口边时，该方向不再额外缩进边距：仍按原配置缩进会把元素推出屏幕。
	// m_Anchored* 已按同一套重合判定得出，调用方的 ApplyEdgeMargin 因此自动退化为恒等变换。
	Scope.m_EdgeMargin = {
		Scope.m_AnchoredLeft ? 0.0f : SafeLeft,
		Scope.m_AnchoredRight ? 0.0f : SafeRight,
		Scope.m_AnchoredTop ? 0.0f : SafeTop,
		Scope.m_AnchoredBottom ? 0.0f : SafeBottom};
	if(Scope.m_AnchoredLeft)
		Scope.m_Corners &= ~IGraphics::CORNER_L;
	if(Scope.m_AnchoredRight)
		Scope.m_Corners &= ~IGraphics::CORNER_R;
	if(Scope.m_AnchoredTop)
		Scope.m_Corners &= ~IGraphics::CORNER_T;
	if(Scope.m_AnchoredBottom)
		Scope.m_Corners &= ~IGraphics::CORNER_B;

	if(pVisible != nullptr)
	{
		pVisible->m_Element = Element;
		pVisible->m_TransformRect = TransformRect;
		pVisible->m_DeclaredVisibleRect = VisibleRect;
		pVisible->m_TargetUiRect = {
			pUiScreen->x + (AnchorX - ScreenX0) * pUiScreen->w / ScreenW,
			pUiScreen->y + (AnchorY - ScreenY0) * pUiScreen->h / ScreenH,
			TransformWidth * pUiScreen->w / ScreenW,
			TransformHeight * pUiScreen->h / ScreenH};
		pVisible->m_Rect = {
			pUiScreen->x + (AnchorX + VisibleOffsetX - ScreenX0) * pUiScreen->w / ScreenW,
			pUiScreen->y + (AnchorY + VisibleOffsetY - ScreenY0) * pUiScreen->h / ScreenH,
			BaseUiWidth * Scale,
			BaseUiHeight * Scale};
		pVisible->m_BaseWidth = BaseUiWidth;
		pVisible->m_BaseHeight = BaseUiHeight;
		pVisible->m_StateOffsetX = TransformToVisibleOffsetX * pUiScreen->w / ScreenW;
		pVisible->m_StateOffsetY = TransformToVisibleOffsetY * pUiScreen->h / ScreenH;
		pVisible->m_Scalable = Scalable;
	}
	return true;
}

CHudEditor::STransformScope CHudEditor::PreviewTransform(EHudEditorElement Element, const CUIRect &DefaultRect, bool Scalable)
{
	return PreviewTransform(Element, DefaultRect, DefaultRect, QmHudEditor::SEdgeMargin{}, Scalable);
}

CHudEditor::STransformScope CHudEditor::PreviewTransform(EHudEditorElement Element, const CUIRect &TransformRect, const CUIRect &VisibleRect, bool Scalable)
{
	return PreviewTransform(Element, TransformRect, VisibleRect, QmHudEditor::SEdgeMargin{}, Scalable);
}

CHudEditor::STransformScope CHudEditor::PreviewTransform(EHudEditorElement Element, const CUIRect &TransformRect, const CUIRect &VisibleRect, const QmHudEditor::SEdgeMargin &EdgeMargin, bool Scalable)
{
	STransformScope Scope;
	ComputeTransformPlacement(Element, TransformRect, VisibleRect, Scalable, Scope, nullptr, EdgeMargin);
	return Scope;
}

CHudEditor::STransformScope CHudEditor::BeginTransform(EHudEditorElement Element, const CUIRect &TransformRect, const CUIRect &VisibleRect, const QmHudEditor::SEdgeMargin &EdgeMargin, bool Scalable, bool ApplyMapScreen)
{
	STransformScope Scope;
	SVisibleElement Visible;
	if(!ComputeTransformPlacement(Element, TransformRect, VisibleRect, Scalable, Scope, &Visible, EdgeMargin))
		return Scope;
	m_vVisibleElements.push_back(Visible);

	const float Scale = TransformRect.w > EPSILON ? Scope.m_TargetRect.w / TransformRect.w : 1.0f;
	const bool Transformed =
		std::fabs(Scope.m_TargetRect.x - TransformRect.x) > EPSILON ||
		std::fabs(Scope.m_TargetRect.y - TransformRect.y) > EPSILON ||
		std::fabs(Scale - 1.0f) > EPSILON;
	if(!Transformed || !ApplyMapScreen)
		return Scope;

	Scope.m_Applied = true;
	const float ScreenW = maximum(EPSILON, Scope.m_ScreenX1 - Scope.m_ScreenX0);
	const float ScreenH = maximum(EPSILON, Scope.m_ScreenY1 - Scope.m_ScreenY0);

	const float NewScreenX0 = TransformRect.x - (Scope.m_TargetRect.x - Scope.m_ScreenX0) / Scale;
	const float NewScreenY0 = TransformRect.y - (Scope.m_TargetRect.y - Scope.m_ScreenY0) / Scale;
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

CHudEditor::SAlignmentReferences CHudEditor::BuildAlignmentReferences(EHudEditorElement DraggingElement) const
{
	SAlignmentReferences References;
	for(const SVisibleElement &Visible : m_vVisibleElements)
	{
		if(Visible.m_Element == DraggingElement || Visible.m_Rect.w <= 0.0f || Visible.m_Rect.h <= 0.0f)
			continue;
		dbg_assert(References.m_XCount < (int)References.m_aXReferences.size(), "too many HUD editor x alignment references");
		dbg_assert(References.m_YCount < (int)References.m_aYReferences.size(), "too many HUD editor y alignment references");
		if(References.m_XCount < (int)References.m_aXReferences.size())
			References.m_aXReferences[References.m_XCount++] = {Visible.m_Rect.x, Visible.m_Rect.w};
		if(References.m_YCount < (int)References.m_aYReferences.size())
			References.m_aYReferences[References.m_YCount++] = {Visible.m_Rect.y, Visible.m_Rect.h};
	}
	return References;
}

void CHudEditor::UpdateInteractionUi()
{
	if(m_InteractionUiActive)
		return;

	Ui()->StartCheck();
	Ui()->Update();
	m_InteractionUiActive = true;
}

void CHudEditor::OpenJumpHintTextEditor()
{
	char aDecoded[sizeof(g_Config.m_QmJumpHintText)];
	DecodeEscapedNewlines(g_Config.m_QmJumpHintText[0] != '\0' ? g_Config.m_QmJumpHintText : JUMP_HINT_DEFAULT_TEXT, aDecoded, sizeof(aDecoded));
	m_JumpHintTextInput.Set(aDecoded);
	m_JumpHintTextInput.SetEmptyText(Localize("Jump hint text"));
	m_JumpHintTextEditorActive = true;
	m_JumpHintTextEditorNeedsFocus = true;
	m_DraggingElement = -1;
	m_DragGrabOffset = vec2(0.0f, 0.0f);
}

void CHudEditor::SaveJumpHintTextEditor()
{
	char aEncoded[sizeof(g_Config.m_QmJumpHintText)];
	EncodeEscapedNewlines(m_JumpHintTextInput.GetString(), aEncoded, sizeof(aEncoded));
	if(aEncoded[0] == '\0')
		str_copy(aEncoded, JUMP_HINT_DEFAULT_TEXT, sizeof(aEncoded));
	str_copy(g_Config.m_QmJumpHintText, aEncoded, sizeof(g_Config.m_QmJumpHintText));
	ConfigManager()->Save();
	CloseJumpHintTextEditor();
}

void CHudEditor::CloseJumpHintTextEditor()
{
	if(!m_JumpHintTextEditorActive)
		return;

	m_JumpHintTextEditorActive = false;
	m_JumpHintTextEditorNeedsFocus = false;
	Ui()->ReleaseActiveTextInput(&m_JumpHintTextInput);
}

bool CHudEditor::HandleElementDoubleClick(EHudEditorElement Element)
{
	if(!Ui()->DoDoubleClickLogic(&m_aElementStates[static_cast<int>(Element)]))
		return false;

	if(Element != EHudEditorElement::JumpHint)
		return false;

	OpenJumpHintTextEditor();
	return true;
}

bool CHudEditor::DoJumpHintTextArea(CLineInput *pLineInput, const CUIRect *pRect, float FontSize)
{
	const bool Inside = Ui()->MouseHovered(pRect);
	bool Active = Ui()->ActiveItem() == pLineInput || pLineInput->IsActive();
	const bool Changed = pLineInput->WasChanged();
	const bool CursorChanged = pLineInput->WasCursorChanged();
	const bool ClickedOutside = (Ui()->MouseButtonClicked(0) || Ui()->MouseButtonClicked(1)) && !Inside;

	CUIRect Textbox;
	pRect->VMargin(3.0f, &Textbox);
	Textbox.HMargin(3.0f, &Textbox);

	bool JustGotActive = false;
	if(Ui()->CheckActiveItem(pLineInput))
	{
		if(Ui()->MouseButton(0))
		{
			if(pLineInput->IsActive() && (Input()->HasComposition() || Input()->GetCandidateCount()))
			{
				Input()->StopTextInput();
				Input()->StartTextInput();
			}
		}
		else
		{
			Ui()->SetActiveItem(nullptr);
		}
	}
	else if(Ui()->HotItem() == pLineInput)
	{
		if(Ui()->MouseButton(0))
		{
			if(!Active)
				JustGotActive = true;
			Ui()->SetActiveItem(pLineInput);
		}
	}

	if(Inside && !Ui()->MouseButton(0))
		Ui()->SetHotItem(pLineInput);

	if(Active && ClickedOutside)
	{
		Ui()->ReleaseActiveTextInput(pLineInput);
		Active = false;
	}
	if(Ui()->Enabled() && Active && !JustGotActive)
		pLineInput->Activate(EInputPriority::UI);
	else
		pLineInput->Deactivate();

	CLineInput::SMouseSelection *pMouseSelection = pLineInput->GetMouseSelection();
	if(Inside && !pMouseSelection->m_Selecting && Ui()->MouseButtonClicked(0))
	{
		pMouseSelection->m_Selecting = true;
		pMouseSelection->m_PressMouse = Ui()->MousePos();
		pMouseSelection->m_Offset = vec2(0.0f, 0.0f);
	}
	if(pMouseSelection->m_Selecting)
	{
		pMouseSelection->m_ReleaseMouse = Ui()->MousePos();
		if(!Ui()->MouseButton(0))
		{
			pMouseSelection->m_Selecting = false;
			if(Active)
				Input()->EnsureScreenKeyboardShown();
		}
	}

	pRect->Draw(CUi::ms_LightButtonColorFunction.GetColor(Active, Ui()->HotItem() == pLineInput), IGraphics::CORNER_ALL, 4.0f);
	Ui()->ClipEnable(pRect);
	pLineInput->Render(&Textbox, FontSize, TEXTALIGN_TL, Changed || CursorChanged, Textbox.w, 2.0f);
	Ui()->ClipDisable();
	pLineInput->SetScrollOffset(0.0f);
	pLineInput->SetScrollOffsetChange(0.0f);

	return Changed;
}

void CHudEditor::RenderJumpHintTextEditor(const CUIRect &Screen)
{
	constexpr float PopupWidth = 260.0f;
	constexpr float PopupHeight = 150.0f;
	constexpr float Padding = 10.0f;
	constexpr float ButtonHeight = 20.0f;
	constexpr float ButtonWidth = 58.0f;
	constexpr float FontSize = 8.0f;

	Graphics()->DrawRect(Screen.x, Screen.y, Screen.w, Screen.h, ColorRGBA(0.0f, 0.0f, 0.0f, 0.42f), IGraphics::CORNER_NONE, 0.0f);

	CUIRect Popup{
		Screen.x + (Screen.w - PopupWidth) * 0.5f,
		Screen.y + (Screen.h - PopupHeight) * 0.5f,
		PopupWidth,
		PopupHeight};
	Popup.Draw(ColorRGBA(0.03f, 0.04f, 0.06f, 0.92f), IGraphics::CORNER_ALL, 7.0f);

	CUIRect Content;
	Popup.Margin(Padding, &Content);

	CUIRect Title;
	Content.HSplitTop(18.0f, &Title, &Content);
	Ui()->DoLabel(&Title, Localize("Position jump tip"), 10.0f, TEXTALIGN_ML);
	Content.HSplitTop(5.0f, nullptr, &Content);

	CUIRect EditBox;
	Content.HSplitBottom(ButtonHeight + 8.0f, &EditBox, &Content);
	DoJumpHintTextArea(&m_JumpHintTextInput, &EditBox, FontSize);

	if(m_JumpHintTextEditorNeedsFocus)
	{
		Ui()->SetActiveItem(&m_JumpHintTextInput);
		m_JumpHintTextEditorNeedsFocus = false;
	}

	CUIRect Buttons = Content;
	CUIRect CancelButton, SaveButton, ResetButton;
	Buttons.VSplitRight(ButtonWidth, &Buttons, &SaveButton);
	Buttons.VSplitRight(7.0f, &Buttons, nullptr);
	Buttons.VSplitRight(ButtonWidth, &Buttons, &CancelButton);
	Buttons.VSplitLeft(ButtonWidth, &ResetButton, nullptr);

	static CButtonContainer s_ResetButton;
	static CButtonContainer s_CancelButton;
	static CButtonContainer s_SaveButton;

	{
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
		ResetButton.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f * Ui()->ButtonColorMul(&s_ResetButton)), IGraphics::CORNER_ALL, 4.0f);
		CancelButton.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f * Ui()->ButtonColorMul(&s_CancelButton)), IGraphics::CORNER_ALL, 4.0f);
		SaveButton.Draw(ColorRGBA(0.25f, 0.62f, 1.0f, 0.22f * Ui()->ButtonColorMul(&s_SaveButton)), IGraphics::CORNER_ALL, 4.0f);
		Ui()->DoLabel(&ResetButton, Localize("Reset"), FontSize, TEXTALIGN_MC);
		Ui()->DoLabel(&CancelButton, Localize("Cancel"), FontSize, TEXTALIGN_MC);
		Ui()->DoLabel(&SaveButton, Localize("Save"), FontSize, TEXTALIGN_MC);

		if(Ui()->DoButtonLogic(&s_ResetButton, 0, &ResetButton, BUTTONFLAG_LEFT) != 0)
		{
			char aDecoded[sizeof(g_Config.m_QmJumpHintText)];
			DecodeEscapedNewlines(JUMP_HINT_DEFAULT_TEXT, aDecoded, sizeof(aDecoded));
			m_JumpHintTextInput.Set(aDecoded);
			Ui()->SetActiveItem(&m_JumpHintTextInput);
		}
		if(Ui()->DoButtonLogic(&s_CancelButton, 0, &CancelButton, BUTTONFLAG_LEFT) != 0)
			CloseJumpHintTextEditor();
		if(Ui()->DoButtonLogic(&s_SaveButton, 0, &SaveButton, BUTTONFLAG_LEFT) != 0)
			SaveJumpHintTextEditor();
	}
}

void CHudEditor::OnRender()
{
	if(!m_Active)
		return;

	const CUIRect *pUiScreen = Ui()->Screen();
	if(pUiScreen == nullptr)
		return;

	UpdateInteractionUi();

	if(m_JumpHintTextEditorActive)
	{
		float PrevX0 = 0.0f;
		float PrevY0 = 0.0f;
		float PrevX1 = 0.0f;
		float PrevY1 = 0.0f;
		Graphics()->GetScreen(&PrevX0, &PrevY0, &PrevX1, &PrevY1);
		Graphics()->MapScreen(pUiScreen->x, pUiScreen->y, pUiScreen->x + pUiScreen->w, pUiScreen->y + pUiScreen->h);
		RenderJumpHintTextEditor(*pUiScreen);
		RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);
		Graphics()->MapScreen(PrevX0, PrevY0, PrevX1, PrevY1);
		Ui()->FinishCheck();
		m_InteractionUiActive = false;
		return;
	}

	constexpr float ResetButtonWidth = 68.0f;
	constexpr float ResetButtonHeight = 18.0f;
	constexpr float ResetButtonMargin = 10.0f;
	CUIRect ResetButton{
		pUiScreen->x + pUiScreen->w - ResetButtonWidth - ResetButtonMargin,
		pUiScreen->y + ResetButtonMargin,
		ResetButtonWidth,
		ResetButtonHeight};
	static CButtonContainer s_ResetDefaultButton;
	const bool ResetButtonHovered = Ui()->MouseHovered(&ResetButton);
	const bool ResetDefaultClicked = Ui()->DoButtonLogic(&s_ResetDefaultButton, 0, &ResetButton, BUTTONFLAG_LEFT) != 0;
	if(ResetDefaultClicked)
		ResetLayoutConfig();

	const int HoveredIndex = ResetButtonHovered ? -1 : FindHoveredVisibleElement();
	bool ShowDragGuideX = false;
	bool ShowDragGuideY = false;
	float DragGuideX = 0.0f;
	float DragGuideY = 0.0f;
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
		const EHudEditorElement Element = m_vVisibleElements[HoveredIndex].m_Element;
		if(!HandleElementDoubleClick(Element))
		{
			m_DraggingElement = static_cast<int>(Element);
			m_DragGrabOffset = vec2(Ui()->MouseX() - m_vVisibleElements[HoveredIndex].m_Rect.x, Ui()->MouseY() - m_vVisibleElements[HoveredIndex].m_Rect.y);
		}
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
			const float VisibleOffsetX = Visible.m_StateOffsetX * Scale;
			const float VisibleOffsetY = Visible.m_StateOffsetY * Scale;
			const SAlignmentReferences References = BuildAlignmentReferences(Visible.m_Element);
			// 鼠标抓取的是可见矩形；转换为锚点后，吸附和存储都只使用这个锚点。
			const QmHudEditor::SSnapAxisResult SnapX = QmHudEditor::ResolveAxisSnapEx(Ui()->MouseX() - m_DragGrabOffset.x - VisibleOffsetX, Width, pUiScreen->x, pUiScreen->w, References.m_aXReferences.data(), References.m_XCount, VisibleOffsetX);
			const QmHudEditor::SSnapAxisResult SnapY = QmHudEditor::ResolveAxisSnapEx(Ui()->MouseY() - m_DragGrabOffset.y - VisibleOffsetY, Height, pUiScreen->y, pUiScreen->h, References.m_aYReferences.data(), References.m_YCount, VisibleOffsetY);
			const float X = SnapX.m_Position;
			const float Y = SnapY.m_Position;
			ShowDragGuideX = SnapX.m_HasGuide;
			ShowDragGuideY = SnapY.m_HasGuide;
			DragGuideX = SnapX.m_GuidePosition;
			DragGuideY = SnapY.m_GuidePosition;
			State.m_PosXPermille = round_to_int(QmHudEditor::StoreAxisAnchor(X, Width, pUiScreen->x, pUiScreen->w, VisibleOffsetX) * POSITION_SCALE);
			State.m_PosYPermille = round_to_int(QmHudEditor::StoreAxisAnchor(Y, Height, pUiScreen->y, pUiScreen->h, VisibleOffsetY) * POSITION_SCALE);
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
			ClampStateToScreen(State, Visible.m_BaseWidth, Visible.m_BaseHeight, Visible.m_StateOffsetX, Visible.m_StateOffsetY);
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

	if(m_DraggingElement >= 0 && (ShowDragGuideX || ShowDragGuideY))
	{
		const ColorRGBA GuideColor(1.0f, 0.82f, 0.20f, 0.70f);
		if(ShowDragGuideX)
			Graphics()->DrawRect(DragGuideX - 0.5f, pUiScreen->y, 1.0f, pUiScreen->h, GuideColor, IGraphics::CORNER_NONE, 0.0f);
		if(ShowDragGuideY)
			Graphics()->DrawRect(pUiScreen->x, DragGuideY - 0.5f, pUiScreen->w, 1.0f, GuideColor, IGraphics::CORNER_NONE, 0.0f);
	}

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
		Localize("Modules snap to screen edges, center lines and other HUD modules while dragging"),
		Localize("Use the mouse wheel on a hovered module to scale it by 5%"),
		Localize("Press Esc to exit the HUD editor"),
	};
	float HelpWidth = 0.0f;
	for(const char *pLine : apHelpLines)
		HelpWidth = maximum(HelpWidth, TextRender()->TextWidth(HelpFontSize, pLine, -1, -1.0f));
	const float HelpHeight = HelpPaddingY * 2.0f + HelpLineHeight * (float)std::size(apHelpLines);
	const float HelpX = pUiScreen->x + (pUiScreen->w - (HelpWidth + HelpPaddingX * 2.0f)) * 0.5f;
	const float HelpY = pUiScreen->y + pUiScreen->h - HelpHeight - 10.0f;
	const CUIRect HelpRect{HelpX, HelpY, HelpWidth + HelpPaddingX * 2.0f, HelpHeight};
	DrawRoundedSurface(Ui(), HelpRect, ColorRGBA(0.03f, 0.04f, 0.06f, 0.78f), ColorRGBA(), 6.0f);
	for(size_t i = 0; i < std::size(apHelpLines); ++i)
	{
		TextRender()->Text(HelpX + HelpPaddingX, HelpY + HelpPaddingY + HelpLineHeight * i, HelpFontSize, apHelpLines[i], -1.0f);
	}

	{
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
		ResetButton.Draw(ColorRGBA(0.03f, 0.04f, 0.06f, 0.78f), IGraphics::CORNER_ALL, 6.0f);
		Ui()->DoLabel(&ResetButton, Localize("Reset default"), 8.0f, TEXTALIGN_MC);
	}

	RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);

	Graphics()->MapScreen(PrevX0, PrevY0, PrevX1, PrevY1);
	Ui()->FinishCheck();
	m_InteractionUiActive = false;
}
