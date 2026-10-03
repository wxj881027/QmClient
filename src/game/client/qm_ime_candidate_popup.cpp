// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "qm_ime_candidate_popup.h"

#include "QmUi/QmAnimResolve.h"
#include "QmUi/QmImeCandidateLayout.h"
#include "QmUi/QmMotion.h"
#include "QmUi/QmTheme.h"
#include "QmUi/QmTree.h"
#include "QmUi/UiSurface.h"
#include "gameclient.h"
#include "lineinput.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
	constexpr float IME_CONTENT_TIME_SCALE = 0.40f;

	struct SImeTextMetrics
	{
		float m_Width = 0.0f;
		float m_Height = 0.0f;
		float m_VisualTop = 0.0f;
		float m_VisualHeight = 0.0f;
		float m_DrawOffsetX = 0.0f;
	};

	struct SImeCandidateMetrics
	{
		SImeTextMetrics m_Num;
		SImeTextMetrics m_Text;
	};

	struct SImeCandidateRow
	{
		std::array<SImeCandidateMetrics, qm_ime_overlay::MAX_CANDIDATES> m_aMetrics;
		std::array<qm_ime_overlay::SCandidateMeasure, qm_ime_overlay::MAX_CANDIDATES> m_aMeasures{};
		qm_ime_overlay::SCandidateLayoutConfig m_LayoutConfig;
		qm_ime_overlay::SQmImeCandidateViewport m_Viewport;
		qm_ime_overlay::SCandidateRowLayout m_TargetLayout;
		SImeTextMetrics m_PageTextMetrics;
		char m_aPageText[16] = "";
		float m_TrailingWidth = 0.0f;
		float m_Height = 0.0f;
		int m_SelectedIndex = -1;
	};

	struct SImePresentationTarget
	{
		CUIRect m_Rect = {};
		float m_Radius = 0.0f;
		float m_Alpha = 0.0f;
		float m_CandidateAlpha = 0.0f;
		float m_CandidateScale = 1.0f;
	};

	struct SImeResolvedPresentation
	{
		CUIRect m_Rect = {};
		float m_Radius = 0.0f;
		float m_Alpha = 0.0f;
		float m_CandidateAlpha = 0.0f;
		float m_CandidateScale = 1.0f;
	};

	bool HasPopupContent(const SQmImePopupState &State)
	{
		return QmImeHasPopupContent(State);
	}

	ColorRGBA WithAlpha(ColorRGBA Color, float Alpha)
	{
		Color.a *= Alpha;
		return Color;
	}

	uint64_t ImePresentationNodeKey(const char *pScope)
	{
		static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("qm_ime_presentation_state"));
		return BuildUiAnimNodeKey(s_BaseKey, static_cast<uint64_t>(str_quickhash(pScope)));
	}

	SImeTextMetrics MeasureImeText(ITextRender *pTextRender, float FontSize, const char *pText, const qm_theme::SImeTheme &Ime)
	{
		SImeTextMetrics Metrics;
		if(pTextRender == nullptr || pText == nullptr || pText[0] == '\0')
			return Metrics;

		float TextHeight = 0.0f;
		float VisualTop = 0.0f;
		float VisualBottom = 0.0f;
		STextSizeProperties TextSizeProps{};
		TextSizeProps.m_pHeight = &TextHeight;
		TextSizeProps.m_pVisualTop = &VisualTop;
		TextSizeProps.m_pVisualBottom = &VisualBottom;
		const float AdvanceWidth = pTextRender->TextWidth(FontSize, pText, -1, -1.0f, TEXTFLAG_DISALLOW_NEWLINE, TextSizeProps);
		Metrics.m_Width = maximum(0.0f, AdvanceWidth) + 2.0f * Ime.m_TextSafePaddingX;
		Metrics.m_Height = maximum(0.0f, TextHeight);
		Metrics.m_VisualTop = VisualTop;
		Metrics.m_VisualHeight = maximum(0.0f, VisualBottom - VisualTop);
		Metrics.m_DrawOffsetX = Ime.m_TextSafePaddingX;
		return Metrics;
	}

	void DrawImeText(ITextRender *pTextRender, float VisualX, float RectY, float RectH, float FontSize, const char *pText, const SImeTextMetrics &Metrics, ColorRGBA Color, float Alpha, float Scale, float MaxWidth = -1.0f)
	{
		if(pTextRender == nullptr || pText == nullptr || pText[0] == '\0' || Scale <= 0.0f)
			return;

		pTextRender->TextColor(WithAlpha(Color, Alpha));
		CTextCursor Cursor;
		const float VisualHeight = (Metrics.m_VisualHeight > 0.0f ? Metrics.m_VisualHeight : Metrics.m_Height) * Scale;
		const float TextY = RectY + (RectH - VisualHeight) * 0.5f - Metrics.m_VisualTop * Scale;
		Cursor.SetPosition(vec2(VisualX + Metrics.m_DrawOffsetX * Scale, TextY));
		Cursor.m_FontSize = FontSize * Scale;
		Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_DISALLOW_NEWLINE;
		if(MaxWidth >= 0.0f && MaxWidth + 0.01f < Metrics.m_Width)
		{
			Cursor.m_LineWidth = maximum(0.01f, MaxWidth - 2.0f * Metrics.m_DrawOffsetX) * Scale;
			Cursor.m_MaxLines = 1;
			Cursor.m_Flags |= TEXTFLAG_ELLIPSIS_AT_END | TEXTFLAG_STOP_AT_END;
		}
		pTextRender->TextEx(&Cursor, pText);
	}

	int CandidatePageCount(const SQmImePopupState &State)
	{
		if(State.m_PageCount > 1)
			return State.m_PageCount;
		return 0;
	}

	int CandidatePageIndex(const SQmImePopupState &State)
	{
		if(State.m_PageCount <= 0)
			return -1;
		return std::clamp(State.m_PageIndex, 0, State.m_PageCount - 1);
	}

	SImeCandidateRow MeasureCandidateRow(ITextRender *pTextRender, const SQmImePopupState &State, int CandidateStart, const qm_theme::SImeTheme &Ime, float MaxPanelWidth)
	{
		SImeCandidateRow Row;
		const int CandidateCount = minimum((int)State.m_vCandidates.size(), qm_ime_overlay::MAX_CANDIDATES);
		Row.m_SelectedIndex = qm_ime_overlay::NormalizeSelectedCandidateIndex(State.m_SelectedIndex, CandidateCount);
		Row.m_Viewport = qm_ime_overlay::BuildCandidateViewport(CandidateCount, Row.m_SelectedIndex, CandidateStart);
		const int PageCount = CandidatePageCount(State);
		if(PageCount > 1)
			str_format(Row.m_aPageText, sizeof(Row.m_aPageText), "%d/%d", CandidatePageIndex(State) + 1, PageCount);
		else if(Row.m_Viewport.m_Count < CandidateCount)
			str_copy(Row.m_aPageText, ">");
		if(Row.m_aPageText[0] != '\0')
		{
			Row.m_PageTextMetrics = MeasureImeText(pTextRender, Ime.m_FontComposition, Row.m_aPageText, Ime);
			Row.m_TrailingWidth = maximum(Ime.m_TrailingWidth, Row.m_PageTextMetrics.m_Width + Ime.m_CompositionTextPaddingX * 2.0f);
		}

		const float CandidatePaddingX = maximum(Ime.m_SelectedPaddingX, Ime.m_CandidatePaddingX);
		float TextHeight = MeasureImeText(pTextRender, Ime.m_FontCandidate, "国g", Ime).m_VisualHeight;
		for(int Offset = 0; Offset < Row.m_Viewport.m_Count; ++Offset)
		{
			const int CandidateIndex = Row.m_Viewport.m_Start + Offset;
			SImeCandidateMetrics &Metrics = Row.m_aMetrics[Offset];
			char aNum[4];
			str_format(aNum, sizeof(aNum), "%d", (CandidateIndex + 1) % 10);
			Metrics.m_Num = MeasureImeText(pTextRender, Ime.m_FontCandidate, aNum, Ime);
			Metrics.m_Text = MeasureImeText(pTextRender, Ime.m_FontCandidate, State.m_vCandidates[CandidateIndex].c_str(), Ime);
			TextHeight = maximum(TextHeight, maximum(Metrics.m_Num.m_VisualHeight, Metrics.m_Text.m_VisualHeight));
			Row.m_aMeasures[Offset].m_FixedWidth = 2.0f * CandidatePaddingX + Metrics.m_Num.m_Width + Ime.m_CandidateNumPaddingX;
			Row.m_aMeasures[Offset].m_TextWidth = Metrics.m_Text.m_Width;
		}
		Row.m_LayoutConfig.m_Gap = Ime.m_CandidateGap;
		Row.m_LayoutConfig.m_TrailingWidth = Row.m_TrailingWidth;
		Row.m_LayoutConfig.m_PaddingX = Ime.m_PaddingX;
		Row.m_LayoutConfig.m_MinPanelWidth = Ime.m_MinWidth;
		Row.m_LayoutConfig.m_MaxPanelWidth = MaxPanelWidth;
		Row.m_LayoutConfig.m_MinTextWidth = MeasureImeText(pTextRender, Ime.m_FontCandidate, "国…", Ime).m_Width;
		Row.m_TargetLayout = qm_ime_overlay::BuildCandidateRowLayout(Row.m_aMeasures, Row.m_Viewport.m_Count, Row.m_LayoutConfig);
		Row.m_Height = maximum(Ime.m_RowHeight, TextHeight + 2.0f * Ime.m_TextSafePaddingY);
		return Row;
	}

	void DrawCandidateRow(ITextRender *pTextRender, const SQmImePopupState &State, const SImeCandidateRow &Row,
		const CUIRect &Panel, const qm_theme::SImeTheme &Ime, float Alpha, float Scale)
	{
		if(Alpha <= 0.0f)
			return;
		const auto Layout = qm_ime_overlay::BuildCandidateRowLayoutForPanel(Row.m_aMeasures, Row.m_Viewport.m_Count, Row.m_LayoutConfig, Panel);
		const auto Presentation = qm_ime_overlay::BuildCandidateRowPresentation(Layout, Panel, Row.m_Height, Ime.m_PaddingX, Ime.m_PaddingY, Scale);
		const float ContentScale = Presentation.m_Scale;
		const float CandidatePaddingX = maximum(Ime.m_SelectedPaddingX, Ime.m_CandidatePaddingX);
		for(int CellIndex = 0; CellIndex < Layout.m_Count; ++CellIndex)
		{
			const int CandidateIndex = Row.m_Viewport.m_Start + CellIndex;
			const auto &Cell = Layout.m_aCells[CellIndex];
			const CUIRect CellRect = Presentation.Transform({Cell.m_X, 0.0f, Cell.m_Width, Row.m_Height});
			const bool Selected = CandidateIndex == Row.m_SelectedIndex;
			const auto &Metrics = Row.m_aMetrics[CellIndex];
			char aNum[4];
			str_format(aNum, sizeof(aNum), "%d", (CandidateIndex + 1) % 10);
			const float NumX = CellRect.x + CandidatePaddingX * ContentScale;
			const float TextX = NumX + (Metrics.m_Num.m_Width + Ime.m_CandidateNumPaddingX) * ContentScale;
			DrawImeText(pTextRender, NumX, CellRect.y, CellRect.h, Ime.m_FontCandidate, aNum, Metrics.m_Num, Selected ? Ime.m_TextSelected : Ime.m_TextMuted, Alpha, ContentScale);
			DrawImeText(pTextRender, TextX, CellRect.y, CellRect.h, Ime.m_FontCandidate, State.m_vCandidates[CandidateIndex].c_str(), Metrics.m_Text, Selected ? Ime.m_TextSelected : Ime.m_Text, Alpha, ContentScale, Cell.m_TextWidth);
		}

		if(Row.m_TrailingWidth > 0.0f)
		{
			const CUIRect MoreLocal = {Layout.m_ContentWidth - Row.m_TrailingWidth, 0.0f, Row.m_TrailingWidth, Row.m_Height};
			const CUIRect More = Presentation.Transform(MoreLocal);
			CUIRect Divider = MoreLocal;
			Divider.x += 0.4f;
			Divider.y += 2.0f;
			Divider.w = 0.35f;
			Divider.h = maximum(0.0f, Divider.h - 4.0f);
			Divider = Presentation.Transform(Divider);
			Divider.Draw(WithAlpha(Ime.m_PanelBorder, Alpha * 1.25f), IGraphics::CORNER_ALL, 0.25f);
			DrawImeText(pTextRender, More.x + (More.w - Row.m_PageTextMetrics.m_Width * ContentScale) * 0.5f,
				More.y, More.h, Ime.m_FontComposition, Row.m_aPageText, Row.m_PageTextMetrics, Ime.m_TextMuted, Alpha, ContentScale);
		}
	}
} // namespace

void CQmImeCandidatePopup::Reset()
{
	m_LastState = {};
	m_ContentTransition.Reset();
	m_Presentation = {};
	m_CandidateStart = 0;
	m_WasVisible = false;
	++m_PresenceGeneration;
	if(m_PresenceGeneration == 0)
		m_PresenceGeneration = 1;
}

void CQmImeCandidatePopup::Render(CGameClient *pGameClient, const SQmImePopupState &State)
{
	if(pGameClient == nullptr || pGameClient->Graphics() == nullptr || pGameClient->TextRender() == nullptr)
		return;

	const bool TargetVisible = HasPopupContent(State);
	if(TargetVisible)
		m_LastState = State;
	if(!TargetVisible && !m_WasVisible)
		return;

	const SQmImePopupState &DrawState = TargetVisible ? State : m_LastState;
	if(!HasPopupContent(DrawState))
		return;

	IGraphics *pGraphics = pGameClient->Graphics();
	ITextRender *pTextRender = pGameClient->TextRender();
	const qm_theme::SImeTheme &Ime = qm_theme::ImeTheme(true);
	const float UserOpacity = std::clamp(g_Config.m_QmImeOpacity, 0, 100) / 100.0f;
	const unsigned OldRenderFlags = pTextRender->GetRenderFlags();
	pTextRender->SetRenderFlags(OldRenderFlags | TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);

	float OldScreenX0, OldScreenY0, OldScreenX1, OldScreenY1;
	pGraphics->GetScreen(&OldScreenX0, &OldScreenY0, &OldScreenX1, &OldScreenY1);

	const float Height = Ime.m_ScreenHeight;
	const float Width = Height * pGraphics->ScreenAspect();
	const int ScreenWidth = maximum(pGraphics->ScreenWidth(), 1);
	const int ScreenHeight = maximum(pGraphics->ScreenHeight(), 1);
	const float PixelSize = Height / (float)ScreenHeight;
	const float Margin = Ime.m_ScreenMargin;
	const float ScreenMaxPanelWidth = maximum(1.0f, Width - 2.0f * Margin);

	pGraphics->MapScreen(0.0f, 0.0f, Width, Height);

	const SImeCandidateRow CandidateRow = MeasureCandidateRow(pTextRender, DrawState, m_CandidateStart, Ime, ScreenMaxPanelWidth);
	m_CandidateStart = CandidateRow.m_Viewport.m_Start;
	const float PanelWidth = CandidateRow.m_TargetLayout.m_PanelWidth;
	const float CandidateRowHeight = CandidateRow.m_Height;
	const float PanelHeight = 2.0f * Ime.m_PaddingY + CandidateRowHeight;

	vec2 Anchor = DrawState.m_AnchorScreen / vec2((float)ScreenWidth, (float)ScreenHeight) * vec2(Width, Height);
	const float PopupGap = 2.2f;
	vec2 Position = vec2(Anchor.x, Anchor.y + PopupGap);
	const float AboveY = Anchor.y - PanelHeight - PopupGap;
	if(Position.y + PanelHeight + Margin > Height && AboveY >= Margin)
		Position.y = AboveY;

	// 跟随光标的位置与尺寸分开动画，靠近右边缘时再按当前宽度约束。
	Position.x = std::clamp(Position.x, Margin, maximum(Margin, Width - Margin));
	Position.y = std::clamp(Position.y, Margin, maximum(Margin, Height - PanelHeight - Margin));

	const auto PixelAlign = [](float Value, float UiToPixel) {
		return UiToPixel > 0.0f ? std::round(Value * UiToPixel) / UiToPixel : Value;
	};
	Position.x = PixelAlign(Position.x, ScreenWidth / Width);
	Position.y = PixelAlign(Position.y, ScreenHeight / Height);

	CUiV2AnimationRuntime &AnimRuntime = pGameClient->UiRuntimeV2()->AnimRuntime();
	CUiV2Tree &Tree = pGameClient->UiRuntimeV2()->Tree();
	SUiAnimTransition PresenceTransition;
	PresenceTransition.m_DurationSec = 0.16f;
	PresenceTransition.m_Easing = EEasing::EASE_OUT;
	PresenceTransition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
	const uint64_t PopupKey = BuildUiAnimNodeKey(str_quickhash("qm_ime_popup"), m_PresenceGeneration);
	const SUiPresenceResult Presence = Tree.ResolvePresence(AnimRuntime, PopupKey, TargetVisible, PresenceTransition);
	const uint64_t CapsuleNode = ImePresentationNodeKey("capsule");
	const uint64_t CandidatesNode = ImePresentationNodeKey("candidates");
	const uint64_t SelectedNode = ImePresentationNodeKey("selected");
	m_ContentTransition.Update(AnimRuntime, ImePresentationNodeKey("candidate_contents"), DrawState, m_CandidateStart,
		qm_motion::NormalizeMotionLevel(g_Config.m_QmUiMotionLevel) > 0);

	SImePresentationTarget TargetPresentation;
	TargetPresentation.m_Rect = {Position.x, Position.y, PanelWidth, PanelHeight};
	TargetPresentation.m_Radius = PanelHeight * 0.5f;
	TargetPresentation.m_Alpha = TargetVisible ? 1.0f : 0.0f;
	TargetPresentation.m_CandidateAlpha = TargetVisible ? 1.0f : 0.0f;
	TargetPresentation.m_CandidateScale = TargetVisible ? 1.0f : 0.84f;
	if(!TargetVisible)
		TargetPresentation.m_Rect.y -= 0.8f;

	SUiSpringConfig CapsuleSpring;
	CapsuleSpring.m_Stiffness = 430.0f;
	CapsuleSpring.m_Damping = 38.0f;
	CapsuleSpring.m_RestEpsilon = 0.02f;
	CapsuleSpring.m_RestVelocity = 0.14f;
	SUiSpringConfig ResizeSpring = CapsuleSpring;
	ResizeSpring.m_Stiffness = 180.0f;
	ResizeSpring.m_Damping = 28.0f;
	SUiSpringConfig ContentSpring;
	ContentSpring.m_Stiffness = 520.0f / (IME_CONTENT_TIME_SCALE * IME_CONTENT_TIME_SCALE);
	ContentSpring.m_Damping = 44.0f / IME_CONTENT_TIME_SCALE;
	ContentSpring.m_RestEpsilon = 0.012f;
	ContentSpring.m_RestVelocity = 0.20f;
	SUiSpringConfig SelectedSpring;
	SelectedSpring.m_Stiffness = 620.0f;
	SelectedSpring.m_Damping = 52.0f;
	SelectedSpring.m_RestEpsilon = 0.006f;
	SelectedSpring.m_RestVelocity = 0.08f;

	if(!m_Presentation.m_Initialized)
	{
		const CUIRect &InitialRect = TargetPresentation.m_Rect;
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::POS_X, InitialRect.x);
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::POS_Y, InitialRect.y);
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::WIDTH, InitialRect.w);
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::HEIGHT, InitialRect.h);
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::SCALE, InitialRect.h * 0.5f);
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, CandidatesNode, EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, CandidatesNode, EUiAnimProperty::SCALE, 0.84f);
		m_Presentation.m_Initialized = true;
	}

	m_Presentation.m_TargetX = TargetPresentation.m_Rect.x;
	m_Presentation.m_TargetY = TargetPresentation.m_Rect.y;
	m_Presentation.m_TargetWidth = TargetPresentation.m_Rect.w;
	m_Presentation.m_TargetHeight = TargetPresentation.m_Rect.h;
	m_Presentation.m_TargetRadius = TargetPresentation.m_Radius;
	m_Presentation.m_TargetAlpha = TargetPresentation.m_Alpha;
	m_Presentation.m_TargetCandidateAlpha = TargetPresentation.m_CandidateAlpha;
	m_Presentation.m_TargetCandidateScale = TargetPresentation.m_CandidateScale;

	SImeResolvedPresentation Presentation;
	Presentation.m_Rect.x = ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::POS_X, m_Presentation.m_TargetX, CapsuleSpring, 3, 0.01f);
	Presentation.m_Rect.y = ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::POS_Y, m_Presentation.m_TargetY, CapsuleSpring, 3, 0.01f);
	Presentation.m_Rect.w = std::max(0.0f, ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::WIDTH, m_Presentation.m_TargetWidth, ResizeSpring, 3, 0.01f));
	Presentation.m_Rect.h = std::max(0.0f, ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::HEIGHT, m_Presentation.m_TargetHeight, ResizeSpring, 3, 0.01f));
	Presentation.m_Radius = ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::SCALE, m_Presentation.m_TargetRadius, ResizeSpring, 3, 0.01f);
	Presentation.m_Alpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::ALPHA, m_Presentation.m_TargetAlpha, ContentSpring, 3, 0.004f), 0.0f, 1.0f);
	Presentation.m_CandidateAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, CandidatesNode, EUiAnimProperty::ALPHA, m_Presentation.m_TargetCandidateAlpha, ContentSpring, 2, 0.004f), 0.0f, 1.0f);
	Presentation.m_CandidateScale = ResolveUiPresentationStateValue(AnimRuntime, CandidatesNode, EUiAnimProperty::SCALE, m_Presentation.m_TargetCandidateScale, ContentSpring, 2, 0.004f);

	if(!Presence.m_Render)
	{
		m_WasVisible = false;
		m_ContentTransition.Reset();
		m_Presentation = {};
		m_CandidateStart = 0;
		pTextRender->SetRenderFlags(OldRenderFlags);
		pGraphics->MapScreen(OldScreenX0, OldScreenY0, OldScreenX1, OldScreenY1);
		return;
	}
	m_WasVisible = TargetVisible || Presence.m_Render;
	const float PresentationAlpha = Presentation.m_Alpha;
	const float CandidateAlpha = Presentation.m_CandidateAlpha;
	const float Alpha = minimum(Presence.m_Alpha, PresentationAlpha);
	const float CandidateDrawAlpha = Alpha * CandidateAlpha;

	const CUIRect Panel = qm_ime_overlay::FitCandidatePanel(Presentation.m_Rect,
		{Margin, Margin, ScreenMaxPanelWidth, Height - 2.0f * Margin});
	const qm_ime_overlay::SCandidateRowLayout CandidateLayout = qm_ime_overlay::BuildCandidateRowLayoutForPanel(CandidateRow.m_aMeasures, CandidateRow.m_Viewport.m_Count, CandidateRow.m_LayoutConfig, Panel);
	CUIRect PanelDropA = Panel;
	PanelDropA.x += Ime.m_ShadowX;
	PanelDropA.y += Ime.m_ShadowY * 0.65f;
	SRoundedSurfaceParams SurfaceParams;
	SurfaceParams.m_Radius = Presentation.m_Radius;
	SurfaceParams.m_PixelSize = PixelSize;
	DrawRoundedSurface(pGraphics, PanelDropA, WithAlpha(Ime.m_PanelShadow, Alpha * 0.46f), ColorRGBA(), SurfaceParams);
	CUIRect PanelDropB = Panel;
	PanelDropB.y += Ime.m_ShadowY * 1.7f;
	DrawRoundedSurface(pGraphics, PanelDropB, WithAlpha(Ime.m_PanelShadow, Alpha * 0.28f), ColorRGBA(), SurfaceParams);

	SurfaceParams.m_BorderWidth = Ime.m_BorderInset;
	const ColorRGBA PanelBackground = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmImeBgColor));
	if(UserOpacity < 0.999f && g_Config.m_QmGaussianBlur != 0)
		pGameClient->Ui()->RenderGaussianBlur(Panel, Alpha * UserOpacity, SurfaceParams.m_Corners, SurfaceParams.m_Radius);
	DrawRoundedSurface(pGraphics, Panel, WithAlpha(PanelBackground, Alpha * UserOpacity), WithAlpha(Ime.m_PanelBorder, Alpha * UserOpacity), SurfaceParams);
	CUIRect PanelContent;
	Panel.Margin(Ime.m_BorderInset, &PanelContent);

	CUIRect PanelTopLine = PanelContent;
	PanelTopLine.h = 0.45f;
	PanelTopLine.x += Presentation.m_Radius * 0.35f;
	PanelTopLine.w = maximum(0.0f, PanelTopLine.w - Presentation.m_Radius * 0.70f);
	if(PanelTopLine.w > 0.0f)
		PanelTopLine.Draw(WithAlpha(ColorRGBA(1.0f, 1.0f, 1.0f, 0.11f), Alpha), IGraphics::CORNER_T, 0.0f);

	const ColorRGBA OldTextColor = pTextRender->GetTextColor();
	const ColorRGBA OldOutlineColor = pTextRender->GetTextOutlineColor();
	pTextRender->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.0f);

	if(CandidateLayout.m_Count > 0)
	{
		// 候选数量保持不变，长词按当前宽度省略；胶囊展开后自然恢复完整文字。
		const qm_ime_overlay::SCandidateRowPresentation RowPresentation = qm_ime_overlay::BuildCandidateRowPresentation(CandidateLayout, Panel,
			CandidateRowHeight, Ime.m_PaddingX, Ime.m_PaddingY, Presentation.m_CandidateScale);
		for(int CellIndex = 0; CellIndex < CandidateLayout.m_Count; ++CellIndex)
		{
			if(m_CandidateStart + CellIndex != CandidateRow.m_SelectedIndex)
				continue;
			const qm_ime_overlay::SCandidateCellLayout &Cell = CandidateLayout.m_aCells[CellIndex];
			CUIRect SelectedRect = {Cell.m_X, 0.75f, Cell.m_Width, CandidateRowHeight - 1.5f};
			if(m_Presentation.m_TargetSelectedWidth <= 0.0f)
			{
				SetUiPresentationStateValue(AnimRuntime, SelectedNode, EUiAnimProperty::POS_X, SelectedRect.x);
				SetUiPresentationStateValue(AnimRuntime, SelectedNode, EUiAnimProperty::POS_Y, SelectedRect.y);
				SetUiPresentationStateValue(AnimRuntime, SelectedNode, EUiAnimProperty::WIDTH, SelectedRect.w);
				SetUiPresentationStateValue(AnimRuntime, SelectedNode, EUiAnimProperty::HEIGHT, SelectedRect.h);
			}
			m_Presentation.m_TargetSelectedX = SelectedRect.x;
			m_Presentation.m_TargetSelectedY = SelectedRect.y;
			m_Presentation.m_TargetSelectedWidth = SelectedRect.w;
			m_Presentation.m_TargetSelectedHeight = SelectedRect.h;
			CUIRect DrawRect;
			DrawRect.x = ResolveUiPresentationStateValue(AnimRuntime, SelectedNode, EUiAnimProperty::POS_X, m_Presentation.m_TargetSelectedX, SelectedSpring, 2, 0.01f);
			DrawRect.y = ResolveUiPresentationStateValue(AnimRuntime, SelectedNode, EUiAnimProperty::POS_Y, m_Presentation.m_TargetSelectedY, SelectedSpring, 2, 0.01f);
			DrawRect.w = ResolveUiPresentationStateValue(AnimRuntime, SelectedNode, EUiAnimProperty::WIDTH, m_Presentation.m_TargetSelectedWidth, SelectedSpring, 2, 0.01f);
			DrawRect.h = ResolveUiPresentationStateValue(AnimRuntime, SelectedNode, EUiAnimProperty::HEIGHT, m_Presentation.m_TargetSelectedHeight, SelectedSpring, 2, 0.01f);
			const float CandidateRight = CandidateLayout.m_ContentWidth - CandidateRow.m_TrailingWidth;
			DrawRect.x = std::clamp(DrawRect.x, 0.0f, CandidateRight);
			DrawRect.w = std::clamp(DrawRect.w, 0.0f, CandidateRight - DrawRect.x);
			DrawRect = RowPresentation.Transform(DrawRect);
			SRoundedSurfaceParams CandidateSurfaceParams;
			CandidateSurfaceParams.m_Radius = maximum(1.0f, DrawRect.h * 0.5f);
			CandidateSurfaceParams.m_PixelSize = PixelSize;
			DrawRoundedSurface(pGraphics, DrawRect, WithAlpha(Ime.m_SelectedBg, CandidateDrawAlpha), ColorRGBA(), CandidateSurfaceParams);
			break;
		}

		const auto &vLayers = m_ContentTransition.Layers();
		const int CurrentLayer = m_ContentTransition.CurrentLayerIndex();
		for(int i = 0; i < (int)vLayers.size(); ++i)
		{
			const auto &Layer = vLayers[i];
			if(i == CurrentLayer || !Layer.m_Active)
				continue;
			const SImeCandidateRow PreviousRow = MeasureCandidateRow(pTextRender, Layer.m_State, Layer.m_CandidateStart, Ime, ScreenMaxPanelWidth);
			DrawCandidateRow(pTextRender, Layer.m_State, PreviousRow, Panel, Ime, CandidateDrawAlpha * Layer.m_Alpha, Presentation.m_CandidateScale);
		}
		DrawCandidateRow(pTextRender, DrawState, CandidateRow, Panel, Ime, CandidateDrawAlpha * vLayers[CurrentLayer].m_Alpha, Presentation.m_CandidateScale);
	}
	pTextRender->TextColor(OldTextColor);
	pTextRender->TextOutlineColor(OldOutlineColor);
	pTextRender->SetRenderFlags(OldRenderFlags);
	pGraphics->MapScreen(OldScreenX0, OldScreenY0, OldScreenX1, OldScreenY1);
}
