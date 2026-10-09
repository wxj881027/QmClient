// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "qm_ime_candidate_popup.h"

#include "QmUi/QmAnimResolve.h"
#include "QmUi/QmImeAppearance.h"
#include "QmUi/QmImeCandidateLayout.h"
#include "QmUi/QmInputMotion.h"
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

	void DrawImeText(ITextRender *pTextRender, float VisualX, float RectY, float RectH, float FontSize, const char *pText, const SImeTextMetrics &Metrics, ColorRGBA Color, float Alpha, float Scale, float MaxWidth = -1.0f, const SQmColorGradient *pGradient = nullptr)
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
		const float Width = MaxWidth >= 0.0f ? std::min(Metrics.m_Width, MaxWidth) : Metrics.m_Width;
		const SQmGradientTextPaint Paint = {pGradient, vec2(Cursor.m_X, TextY + Metrics.m_VisualTop * Scale),
			vec2(std::max(0.01f, Width - 2.0f * Metrics.m_DrawOffsetX) * Scale, VisualHeight), Alpha};
		if(pGradient != nullptr && pGradient->m_NumColors > 1)
		{
			Cursor.m_pfnColorSampler = SQmGradientTextPaint::Sample;
			Cursor.m_pColorSamplerContext = &Paint;
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
		const CUIRect &Panel, const SQmImeAppearance &Appearance, float Alpha, float Scale)
	{
		if(Alpha <= 0.0f)
			return;
		const auto &Ime = Appearance.m_Theme;
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
			DrawImeText(pTextRender, NumX, CellRect.y, CellRect.h, Ime.m_FontCandidate, aNum, Metrics.m_Num, Selected ? Ime.m_TextSelected : Ime.m_Text, Alpha * (Selected ? 1.0f : 0.62f), ContentScale, -1.0f, Selected ? &Appearance.m_SelectedText : &Appearance.m_Text);
			DrawImeText(pTextRender, TextX, CellRect.y, CellRect.h, Ime.m_FontCandidate, State.m_vCandidates[CandidateIndex].c_str(), Metrics.m_Text, Selected ? Ime.m_TextSelected : Ime.m_Text, Alpha, ContentScale, Cell.m_TextWidth, Selected ? &Appearance.m_SelectedText : &Appearance.m_Text);
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
				More.y, More.h, Ime.m_FontComposition, Row.m_aPageText, Row.m_PageTextMetrics, Ime.m_Text, Alpha * 0.62f, ContentScale, -1.0f, &Appearance.m_Text);
		}
	}
	void DrawImePanelSurface(CGameClient *pGameClient, const CUIRect &Panel, const SQmImeAppearance &Appearance,
		float Radius, float Alpha, float PixelSize)
	{
		IGraphics *pGraphics = pGameClient->Graphics();
		const auto &Ime = Appearance.m_Theme;
		const float UserOpacity = Appearance.m_BackgroundOpacity;
		CUIRect PanelDropA = Panel;
		PanelDropA.x += Ime.m_ShadowX;
		PanelDropA.y += Ime.m_ShadowY * 0.65f;
		SRoundedSurfaceParams SurfaceParams;
		SurfaceParams.m_Radius = Radius;
		SurfaceParams.m_PixelSize = PixelSize;
		DrawRoundedSurface(pGraphics, PanelDropA, WithAlpha(Ime.m_PanelShadow, Alpha * 0.46f), ColorRGBA(), SurfaceParams);
		CUIRect PanelDropB = Panel;
		PanelDropB.y += Ime.m_ShadowY * 1.7f;
		DrawRoundedSurface(pGraphics, PanelDropB, WithAlpha(Ime.m_PanelShadow, Alpha * 0.28f), ColorRGBA(), SurfaceParams);

		SurfaceParams.m_BorderWidth = Ime.m_BorderInset;
		if(Appearance.m_Background.HasTransparency() && g_Config.m_QmGaussianBlur != 0)
			pGameClient->Ui()->RenderGaussianBlur(Panel, Alpha * UserOpacity, SurfaceParams.m_Corners, SurfaceParams.m_Radius);
		DrawRoundedGradientSurface(pGraphics, Panel, Appearance.m_Background, Alpha, WithAlpha(Ime.m_PanelBorder, Alpha * UserOpacity), SurfaceParams);
		CUIRect PanelContent;
		Panel.Margin(Ime.m_BorderInset, &PanelContent);

		CUIRect PanelTopLine = PanelContent;
		PanelTopLine.h = 0.45f;
		PanelTopLine.x += Radius * 0.35f;
		PanelTopLine.w = maximum(0.0f, PanelTopLine.w - Radius * 0.70f);
		if(PanelTopLine.w > 0.0f)
			PanelTopLine.Draw(WithAlpha(ColorRGBA(1.0f, 1.0f, 1.0f, 0.11f), Alpha), IGraphics::CORNER_T, 0.0f);
	}
} // namespace

void CQmImeCandidatePopup::Reset()
{
	m_LastState = {};
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
	const SQmImeAppearance Appearance = QmImeAppearance(g_Config);
	const auto &Ime = Appearance.m_Theme;
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
	const int MotionLevel = g_Config.m_QmUiMotionLevel;
	CUiV2Tree &Tree = pGameClient->UiRuntimeV2()->Tree();
	SUiAnimTransition PresenceTransition;
	PresenceTransition.m_DurationSec = 0.10f;
	PresenceTransition.m_Easing = EEasing::EASE_OUT;
	PresenceTransition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
	const uint64_t PopupKey = BuildUiAnimNodeKey(str_quickhash("qm_ime_popup"), m_PresenceGeneration);
	if(qm_animation::NormalizeMotionLevel(MotionLevel) == 0)
		AnimRuntime.SetValue(PopupKey, EUiAnimProperty::ALPHA, TargetVisible ? 1.0f : 0.0f);
	const SUiPresenceResult Presence = Tree.ResolvePresence(AnimRuntime, PopupKey, TargetVisible, PresenceTransition);
	const uint64_t CapsuleNode = ImePresentationNodeKey("capsule");
	const uint64_t CandidatesNode = ImePresentationNodeKey("candidates");
	const uint64_t SelectedNode = ImePresentationNodeKey("selected");

	SImePresentationTarget TargetPresentation;
	TargetPresentation.m_Rect = {Position.x, Position.y, PanelWidth, PanelHeight};
	TargetPresentation.m_Radius = PanelHeight * 0.5f;
	TargetPresentation.m_Alpha = TargetVisible ? 1.0f : 0.0f;
	TargetPresentation.m_CandidateAlpha = TargetVisible ? 1.0f : 0.0f;
	TargetPresentation.m_CandidateScale = TargetVisible ? 1.0f : 0.94f;
	if(!TargetVisible)
	{
		TargetPresentation.m_Rect.y -= 0.8f;
		TargetPresentation.m_Rect.w *= 0.96f;
		TargetPresentation.m_Rect.h *= 0.90f;
		TargetPresentation.m_Radius = TargetPresentation.m_Rect.h * 0.5f;
	}

	const SUiSpringConfig &CapsuleSpring = qm_input_motion::FOLLOW;
	const SUiSpringConfig &ResizeSpring = qm_input_motion::RESIZE;
	SUiSpringConfig ContentSpring;
	ContentSpring.m_Stiffness = 520.0f / (IME_CONTENT_TIME_SCALE * IME_CONTENT_TIME_SCALE);
	ContentSpring.m_Damping = 44.0f / IME_CONTENT_TIME_SCALE;
	ContentSpring.m_RestEpsilon = 0.012f;
	ContentSpring.m_RestVelocity = 0.20f;
	const SUiSpringConfig &SelectedSpring = qm_input_motion::SELECTED;

	if(!m_Presentation.m_Initialized)
	{
		const CUIRect &InitialRect = TargetPresentation.m_Rect;
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::POS_X, InitialRect.x);
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::POS_Y, InitialRect.y);
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::WIDTH, InitialRect.w * 0.96f);
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::HEIGHT, InitialRect.h * 0.90f);
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::SCALE, InitialRect.h * 0.45f);
		SetUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, CandidatesNode, EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(AnimRuntime, CandidatesNode, EUiAnimProperty::SCALE, 0.94f);
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
	Presentation.m_Rect.x = qm_input_motion::ResolvePresentationValue(AnimRuntime, CapsuleNode, EUiAnimProperty::POS_X, m_Presentation.m_TargetX, CapsuleSpring, MotionLevel, 3);
	Presentation.m_Rect.y = qm_input_motion::ResolvePresentationValue(AnimRuntime, CapsuleNode, EUiAnimProperty::POS_Y, m_Presentation.m_TargetY, CapsuleSpring, MotionLevel, 3);
	Presentation.m_Rect.w = std::max(0.0f, qm_input_motion::ResolvePresentationValue(AnimRuntime, CapsuleNode, EUiAnimProperty::WIDTH, m_Presentation.m_TargetWidth, ResizeSpring, MotionLevel, 3));
	Presentation.m_Rect.h = std::max(0.0f, qm_input_motion::ResolvePresentationValue(AnimRuntime, CapsuleNode, EUiAnimProperty::HEIGHT, m_Presentation.m_TargetHeight, ResizeSpring, MotionLevel, 3));
	Presentation.m_Radius = qm_input_motion::ResolvePresentationValue(AnimRuntime, CapsuleNode, EUiAnimProperty::SCALE, m_Presentation.m_TargetRadius, ResizeSpring, MotionLevel, 3);
	Presentation.m_Alpha = std::clamp(qm_input_motion::ResolvePresentationValue(AnimRuntime, CapsuleNode, EUiAnimProperty::ALPHA, m_Presentation.m_TargetAlpha, ContentSpring, MotionLevel, 3), 0.0f, 1.0f);
	Presentation.m_CandidateAlpha = std::clamp(qm_input_motion::ResolvePresentationValue(AnimRuntime, CandidatesNode, EUiAnimProperty::ALPHA, m_Presentation.m_TargetCandidateAlpha, ContentSpring, MotionLevel, 2), 0.0f, 1.0f);
	Presentation.m_CandidateScale = qm_input_motion::ResolvePresentationValue(AnimRuntime, CandidatesNode, EUiAnimProperty::SCALE, m_Presentation.m_TargetCandidateScale, qm_input_motion::GLYPH, MotionLevel, 2);

	if(!Presence.m_Render)
	{
		m_WasVisible = false;
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
	DrawImePanelSurface(pGameClient, Panel, Appearance, Presentation.m_Radius, Alpha, PixelSize);

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
			DrawRect.x = qm_input_motion::ResolvePresentationValue(AnimRuntime, SelectedNode, EUiAnimProperty::POS_X, m_Presentation.m_TargetSelectedX, SelectedSpring, MotionLevel, 2);
			DrawRect.y = qm_input_motion::ResolvePresentationValue(AnimRuntime, SelectedNode, EUiAnimProperty::POS_Y, m_Presentation.m_TargetSelectedY, SelectedSpring, MotionLevel, 2);
			DrawRect.w = qm_input_motion::ResolvePresentationValue(AnimRuntime, SelectedNode, EUiAnimProperty::WIDTH, m_Presentation.m_TargetSelectedWidth, SelectedSpring, MotionLevel, 2);
			DrawRect.h = qm_input_motion::ResolvePresentationValue(AnimRuntime, SelectedNode, EUiAnimProperty::HEIGHT, m_Presentation.m_TargetSelectedHeight, SelectedSpring, MotionLevel, 2);
			const float CandidateRight = CandidateLayout.m_ContentWidth - CandidateRow.m_TrailingWidth;
			DrawRect.x = std::clamp(DrawRect.x, 0.0f, CandidateRight);
			DrawRect.w = std::clamp(DrawRect.w, 0.0f, CandidateRight - DrawRect.x);
			DrawRect = RowPresentation.Transform(DrawRect);
			SRoundedSurfaceParams CandidateSurfaceParams;
			CandidateSurfaceParams.m_Radius = maximum(1.0f, DrawRect.h * 0.5f);
			CandidateSurfaceParams.m_PixelSize = PixelSize;
			DrawRoundedGradientSurface(pGraphics, DrawRect, Appearance.m_Selection, CandidateDrawAlpha, ColorRGBA(), CandidateSurfaceParams);
			break;
		}

		// 打字和翻页直接显示最新候选；外框伸缩和选中背景继续使用各自的动画。
		DrawCandidateRow(pTextRender, DrawState, CandidateRow, Panel, Appearance, CandidateDrawAlpha, Presentation.m_CandidateScale);
	}
	pTextRender->TextColor(OldTextColor);
	pTextRender->TextOutlineColor(OldOutlineColor);
	pTextRender->SetRenderFlags(OldRenderFlags);
	pGraphics->MapScreen(OldScreenX0, OldScreenY0, OldScreenX1, OldScreenY1);
}

void QmImeRenderStylePreview(CGameClient *pGameClient, const CUIRect &Rect)
{
	if(pGameClient == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;
	IGraphics *pGraphics = pGameClient->Graphics();
	ITextRender *pTextRender = pGameClient->TextRender();
	if(pGraphics == nullptr || pTextRender == nullptr)
		return;
	SQmImePopupState State;
	State.m_Visible = true;
	State.m_vCandidates = {"你好", "你", "拟好", "您好", "泥号"};
	State.m_SelectedIndex = 0;
	State.m_PageIndex = 0;
	State.m_PageCount = 3;
	SQmImeAppearance Appearance = QmImeAppearance(g_Config);
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	pGraphics->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float Scale = std::abs(ScreenY1 - ScreenY0) / Appearance.m_Theme.m_ScreenHeight;
	auto &Ime = Appearance.m_Theme;
	for(float *pValue : {&Ime.m_FontCandidate, &Ime.m_FontComposition, &Ime.m_PaddingX, &Ime.m_PaddingY,
		&Ime.m_RowHeight, &Ime.m_MinWidth, &Ime.m_TrailingWidth, &Ime.m_CandidateGap, &Ime.m_CandidatePaddingX,
		&Ime.m_SelectedPaddingX, &Ime.m_CandidateNumPaddingX, &Ime.m_CompositionTextPaddingX,
		&Ime.m_TextSafePaddingX, &Ime.m_TextSafePaddingY, &Ime.m_ShadowX, &Ime.m_ShadowY, &Ime.m_BorderInset})
		*pValue *= Scale;
	const unsigned OldFlags = pTextRender->GetRenderFlags();
	const ColorRGBA OldColor = pTextRender->GetTextColor();
	const ColorRGBA OldOutline = pTextRender->GetTextOutlineColor();
	pTextRender->SetRenderFlags(OldFlags | TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
	pTextRender->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.0f);
	const auto Row = MeasureCandidateRow(pTextRender, State, 0, Ime, Rect.w);
	const float Height = Row.m_Height + Ime.m_PaddingY * 2.0f;
	const CUIRect Panel = {Rect.x + (Rect.w - Row.m_TargetLayout.m_PanelWidth) * 0.5f,
		Rect.y + (Rect.h - Height) * 0.5f, Row.m_TargetLayout.m_PanelWidth, Height};
	SRoundedSurfaceParams Params;
	Params.m_Radius = Height * 0.5f;
	Params.m_BorderWidth = Ime.m_BorderInset;
	Params.m_PixelSize = pGameClient->Ui()->PixelSize();
	DrawImePanelSurface(pGameClient, Panel, Appearance, Params.m_Radius, 1.0f, Params.m_PixelSize);
	const auto Layout = qm_ime_overlay::BuildCandidateRowLayoutForPanel(Row.m_aMeasures, Row.m_Viewport.m_Count, Row.m_LayoutConfig, Panel);
	const auto Presentation = qm_ime_overlay::BuildCandidateRowPresentation(Layout, Panel, Row.m_Height, Ime.m_PaddingX, Ime.m_PaddingY, 1.0f);
	if(Layout.m_Count > 0)
	{
		const auto &Cell = Layout.m_aCells[0];
		const CUIRect Selection = Presentation.Transform({Cell.m_X, 0.75f, Cell.m_Width, Row.m_Height - 1.5f});
		Params.m_BorderWidth = 0.0f;
		Params.m_Radius = Selection.h * 0.5f;
		DrawRoundedGradientSurface(pGraphics, Selection, Appearance.m_Selection, 1.0f, ColorRGBA(), Params);
	}
	DrawCandidateRow(pTextRender, State, Row, Panel, Appearance, 1.0f, 1.0f);
	pTextRender->TextColor(OldColor);
	pTextRender->TextOutlineColor(OldOutline);
	pTextRender->SetRenderFlags(OldFlags);
}
