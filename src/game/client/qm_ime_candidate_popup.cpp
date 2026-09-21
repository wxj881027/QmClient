// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "qm_ime_candidate_popup.h"

#include "QmUi/QmAnimResolve.h"
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
#include <engine/shared/qm_ime_policy.h>
#include <engine/textrender.h>

#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
	constexpr int MAX_VISIBLE_CANDIDATES = 16;
	constexpr float IME_CONTENT_TIME_SCALE = 0.40f;

	struct SImeCandidateCell
	{
		int m_Index = -1;
		CUIRect m_Rect = {};
	};

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

	CUIRect ScaleRectAroundCenter(const CUIRect &Rect, float Scale)
	{
		Scale = std::max(0.01f, Scale);
		const float CenterX = Rect.x + Rect.w * 0.5f;
		const float CenterY = Rect.y + Rect.h * 0.5f;
		const float Width = Rect.w * Scale;
		const float Height = Rect.h * Scale;
		return {CenterX - Width * 0.5f, CenterY - Height * 0.5f, Width, Height};
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

	void DrawImeText(ITextRender *pTextRender, float VisualX, float RectY, float RectH, float FontSize, const char *pText, const SImeTextMetrics &Metrics, ColorRGBA Color, float Alpha)
	{
		if(pTextRender == nullptr || pText == nullptr || pText[0] == '\0')
			return;

		pTextRender->TextColor(WithAlpha(Color, Alpha));
		CTextCursor Cursor;
		const float VisualHeight = Metrics.m_VisualHeight > 0.0f ? Metrics.m_VisualHeight : Metrics.m_Height;
		const float TextY = RectY + (RectH - VisualHeight) * 0.5f - Metrics.m_VisualTop;
		Cursor.SetPosition(vec2(VisualX + Metrics.m_DrawOffsetX, TextY));
		Cursor.m_FontSize = FontSize;
		Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_DISALLOW_NEWLINE;
		pTextRender->TextEx(&Cursor, pText);
	}

	float CandidateCellWidth(const qm_theme::SImeTheme &Ime, const SImeCandidateMetrics &Metrics, bool Selected)
	{
		const float PaddingX = Selected ? Ime.m_SelectedPaddingX : Ime.m_CandidatePaddingX;
		return 2.0f * PaddingX + Metrics.m_Num.m_Width + Ime.m_CandidateNumPaddingX + Metrics.m_Text.m_Width;
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
	const qm_theme::SImeTheme &Ime = qm_theme::ImeTheme(true);
	const unsigned OldRenderFlags = pTextRender->GetRenderFlags();
	pTextRender->SetRenderFlags(OldRenderFlags | TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);

	const bool HasCandidates = !DrawState.m_vCandidates.empty();
	const int CandidateCount = minimum((int)DrawState.m_vCandidates.size(), MAX_VISIBLE_CANDIDATES);
	const int PageCount = CandidatePageCount(DrawState);

	float OldScreenX0, OldScreenY0, OldScreenX1, OldScreenY1;
	pGraphics->GetScreen(&OldScreenX0, &OldScreenY0, &OldScreenX1, &OldScreenY1);

	const float Height = Ime.m_ScreenHeight;
	const float Width = Height * pGraphics->ScreenAspect();
	const int ScreenWidth = maximum(pGraphics->ScreenWidth(), 1);
	const int ScreenHeight = maximum(pGraphics->ScreenHeight(), 1);
	const float PixelSize = Height / (float)ScreenHeight;
	const float Margin = Ime.m_ScreenMargin;
	const float ScreenMaxPanelWidth = maximum(Ime.m_MinWidth, Width - 2.0f * Margin);
	const float PreferredMaxPanelWidth = std::clamp(Ime.m_MaxWidth, Ime.m_MinWidth, ScreenMaxPanelWidth);

	pGraphics->MapScreen(0.0f, 0.0f, Width, Height);

	char aPageText[16] = "";
	SImeTextMetrics PageTextMetrics;
	float TrailingWidth = 0.0f;
	const auto SetTrailingText = [&](const char *pText) {
		str_copy(aPageText, pText, sizeof(aPageText));
		PageTextMetrics = MeasureImeText(pTextRender, Ime.m_FontComposition, aPageText, Ime);
		TrailingWidth = maximum(Ime.m_TrailingWidth, PageTextMetrics.m_Width + Ime.m_CompositionTextPaddingX * 2.0f);
	};
	if(PageCount > 1)
	{
		char aFormattedPageText[16];
		str_format(aFormattedPageText, sizeof(aFormattedPageText), "%d/%d", CandidatePageIndex(DrawState) + 1, PageCount);
		SetTrailingText(aFormattedPageText);
	}

	const int SelectedIndex = qm_ime_overlay::NormalizeSelectedCandidateIndex(DrawState.m_SelectedIndex, CandidateCount);
	// 固定视口尺寸仅作上限提示；实际窗口由宽度拟合 + sticky start 决定
	const qm_ime_overlay::SQmImeCandidateViewport SizeHint = qm_ime_overlay::BuildCandidateViewport(CandidateCount, SelectedIndex, m_CandidateStart);
	const int ViewportCountCap = CandidateCount > 0 ? minimum(CandidateCount, maximum(1, SizeHint.m_Count)) : 0;
	const bool MeasureSelectedForLayout = QmImeLayoutMeasureUsesSelectedPadding();

	std::array<SImeCandidateMetrics, MAX_VISIBLE_CANDIDATES> aCandidateMetrics;
	float CandidateTextHeight = MeasureImeText(pTextRender, Ime.m_FontCandidate, "国g", Ime).m_VisualHeight;
	for(int i = 0; i < CandidateCount; ++i)
	{
		char aNum[4];
		str_format(aNum, sizeof(aNum), "%d", (i + 1) % 10);
		aCandidateMetrics[i].m_Num = MeasureImeText(pTextRender, Ime.m_FontCandidate, aNum, Ime);
		aCandidateMetrics[i].m_Text = MeasureImeText(pTextRender, Ime.m_FontCandidate, DrawState.m_vCandidates[i].c_str(), Ime);
		CandidateTextHeight = maximum(CandidateTextHeight, maximum(aCandidateMetrics[i].m_Num.m_VisualHeight, aCandidateMetrics[i].m_Text.m_VisualHeight));
	}

	// 容量量测默认不用选中 padding，避免高亮第 N 个导致可见数量回流
	const auto CandidateCellWidthForLayout = [&](int Index) {
		return CandidateCellWidth(Ime, aCandidateMetrics[Index], MeasureSelectedForLayout && Index == SelectedIndex);
	};
	const auto CandidateNaturalWidthForWindow = [&](int Start, int Count) {
		float CandidateNaturalWidth = 0.0f;
		for(int Offset = 0; Offset < Count; ++Offset)
		{
			const int Index = Start + Offset;
			if(Index < 0 || Index >= CandidateCount)
				break;
			if(Offset > 0)
				CandidateNaturalWidth += Ime.m_CandidateGap;
			CandidateNaturalWidth += CandidateCellWidthForLayout(Index);
		}
		return CandidateNaturalWidth;
	};
	const float CandidatePanelWidthLimit = PreferredMaxPanelWidth;
	const auto FitVisibleCandidateCells = [&](float FitTrailingWidth, int &CandidateStart, int &CandidateDisplayCount) {
		CandidateDisplayCount = ViewportCountCap;
		CandidateStart = QmImeResolveCandidateWindowStart(CandidateCount, CandidateDisplayCount, SelectedIndex, m_CandidateStart);
		while(CandidateDisplayCount > 1)
		{
			CandidateStart = QmImeResolveCandidateWindowStart(CandidateCount, CandidateDisplayCount, SelectedIndex, m_CandidateStart);
			const float NeededWidth = CandidateNaturalWidthForWindow(CandidateStart, CandidateDisplayCount) + FitTrailingWidth + 2.0f * Ime.m_PaddingX;
			if(NeededWidth <= CandidatePanelWidthLimit)
				break;
			CandidateDisplayCount = maximum(1, CandidateDisplayCount - 1);
		}
		CandidateStart = QmImeResolveCandidateWindowStart(CandidateCount, CandidateDisplayCount, SelectedIndex, m_CandidateStart);
	};

	int CandidateStart = 0;
	int CandidateDisplayCount = CandidateCount;
	if(HasCandidates)
	{
		FitVisibleCandidateCells(TrailingWidth, CandidateStart, CandidateDisplayCount);
		if(PageCount <= 1 && CandidateDisplayCount < CandidateCount)
		{
			SetTrailingText(">");
			FitVisibleCandidateCells(TrailingWidth, CandidateStart, CandidateDisplayCount);
		}
	}
	// sticky：写回视口起点，避免每帧按选中项强行把 start 推到末尾
	m_CandidateStart = CandidateStart;

	const float CandidateWindowNaturalWidth = HasCandidates ? CandidateNaturalWidthForWindow(CandidateStart, CandidateDisplayCount) : 0.0f;
	float SelectedLayoutExtra = 0.0f;
	if(HasCandidates && SelectedIndex >= CandidateStart && SelectedIndex < CandidateStart + CandidateDisplayCount)
		SelectedLayoutExtra = QmImeSelectedLayoutExtraWidth(Ime.m_SelectedPaddingX, Ime.m_CandidatePaddingX);
	const float CandidateNaturalWidth = CandidateWindowNaturalWidth + TrailingWidth + SelectedLayoutExtra;
	const float ContentWidth = CandidateNaturalWidth;
	const float NeededPanelWidth = ContentWidth + 2.0f * Ime.m_PaddingX;
	const float PanelWidth = maximum(NeededPanelWidth, Ime.m_MinWidth);
	const float CandidateRowHeight = maximum(Ime.m_RowHeight, CandidateTextHeight + 2.0f * Ime.m_TextSafePaddingY);
	const float PanelHeight = 2.0f * Ime.m_PaddingY + CandidateRowHeight;

	vec2 Anchor = DrawState.m_AnchorScreen / vec2((float)ScreenWidth, (float)ScreenHeight) * vec2(Width, Height);
	const float PopupGap = 2.2f;
	vec2 Position = vec2(Anchor.x, Anchor.y + PopupGap);
	const float AboveY = Anchor.y - PanelHeight - PopupGap;
	if(Position.y + PanelHeight + Margin > Height && AboveY >= Margin)
		Position.y = AboveY;

	if(Position.x + PanelWidth + Margin > Width)
		Position.x = Width - PanelWidth - Margin;
	Position.x = std::clamp(Position.x, Margin, maximum(Margin, Width - PanelWidth - Margin));
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
		CUIRect InitialRect = TargetPresentation.m_Rect;
		InitialRect.w = std::clamp(Ime.m_MinWidth * 0.72f, 1.0f, TargetPresentation.m_Rect.w);
		InitialRect.h = std::max(1.0f, TargetPresentation.m_Rect.h * 0.82f);
		InitialRect.x = TargetPresentation.m_Rect.x + (TargetPresentation.m_Rect.w - InitialRect.w) * 0.5f;
		InitialRect.y = TargetPresentation.m_Rect.y + (TargetPresentation.m_Rect.h - InitialRect.h) * 0.5f;
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
	Presentation.m_Rect.w = std::max(0.0f, ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::WIDTH, m_Presentation.m_TargetWidth, CapsuleSpring, 3, 0.01f));
	Presentation.m_Rect.h = std::max(0.0f, ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::HEIGHT, m_Presentation.m_TargetHeight, CapsuleSpring, 3, 0.01f));
	Presentation.m_Radius = ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::SCALE, m_Presentation.m_TargetRadius, CapsuleSpring, 3, 0.01f);
	Presentation.m_Alpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::ALPHA, m_Presentation.m_TargetAlpha, ContentSpring, 3, 0.004f), 0.0f, 1.0f);
	Presentation.m_CandidateAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, CandidatesNode, EUiAnimProperty::ALPHA, m_Presentation.m_TargetCandidateAlpha, ContentSpring, 2, 0.004f), 0.0f, 1.0f);
	Presentation.m_CandidateScale = ResolveUiPresentationStateValue(AnimRuntime, CandidatesNode, EUiAnimProperty::SCALE, m_Presentation.m_TargetCandidateScale, ContentSpring, 2, 0.004f);

	if(!Presence.m_Render)
	{
		m_WasVisible = false;
		pTextRender->SetRenderFlags(OldRenderFlags);
		pGraphics->MapScreen(OldScreenX0, OldScreenY0, OldScreenX1, OldScreenY1);
		return;
	}
	m_WasVisible = TargetVisible || Presence.m_Render;
	const float PresentationAlpha = Presentation.m_Alpha;
	const float CandidateAlpha = Presentation.m_CandidateAlpha;
	const float Alpha = minimum(Presence.m_Alpha, PresentationAlpha);
	const float CandidateDrawAlpha = Alpha * CandidateAlpha;

	CUIRect Panel = Presentation.m_Rect;
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
	DrawRoundedSurface(pGraphics, Panel, WithAlpha(Ime.m_PanelBg, Alpha), WithAlpha(Ime.m_PanelBorder, Alpha), SurfaceParams);
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

	CUIRect Content;
	Panel.Margin(vec2(Ime.m_PaddingX, Ime.m_PaddingY), &Content);

	if(HasCandidates)
	{
		CUIRect CandidateLayer = ScaleRectAroundCenter(Content, Presentation.m_CandidateScale);
		CUIRect CandidateRow;
		CandidateLayer.HSplitTop(CandidateRowHeight, &CandidateRow, &CandidateLayer);
		CUIRect Candidates = CandidateRow;
		CUIRect More = {};
		if(TrailingWidth > 0.0f)
			CandidateRow.VSplitRight(TrailingWidth, &Candidates, &More);

		std::array<SImeCandidateCell, MAX_VISIBLE_CANDIDATES> aCells;
		int CellCount = 0;
		float CursorX = Candidates.x;
		const float Right = Candidates.x + Candidates.w;
		for(int Offset = 0; Offset < CandidateDisplayCount; ++Offset)
		{
			const int CandidateIndex = CandidateStart + Offset;
			if(Offset > 0)
				CursorX += Ime.m_CandidateGap;
			if(CursorX >= Right)
				break;

			const bool Selected = CandidateIndex == SelectedIndex;
			const float CellWidth = CandidateCellWidth(Ime, aCandidateMetrics[CandidateIndex], Selected);
			if(CursorX + CellWidth > Right && Offset > 0)
				break;

			SImeCandidateCell &Cell = aCells[CellCount++];
			Cell.m_Index = CandidateIndex;
			Cell.m_Rect = {CursorX, CandidateRow.y, CellWidth, CandidateRow.h};
			CursorX += CellWidth;
		}

		for(int CellIndex = 0; CellIndex < CellCount; ++CellIndex)
		{
			if(aCells[CellIndex].m_Index != SelectedIndex)
				continue;
			CUIRect SelectedRect = aCells[CellIndex].m_Rect;
			SelectedRect.y += 0.75f;
			SelectedRect.h -= 1.5f;
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
			SRoundedSurfaceParams CandidateSurfaceParams;
			CandidateSurfaceParams.m_Radius = maximum(1.0f, DrawRect.h * 0.5f);
			CandidateSurfaceParams.m_PixelSize = PixelSize;
			DrawRoundedSurface(pGraphics, DrawRect, WithAlpha(Ime.m_SelectedBg, CandidateDrawAlpha), ColorRGBA(), CandidateSurfaceParams);
			break;
		}

		for(int CellIndex = 0; CellIndex < CellCount; ++CellIndex)
		{
			const SImeCandidateCell &Cell = aCells[CellIndex];
			const bool Selected = Cell.m_Index == SelectedIndex;
			const float PaddingX = Selected ? Ime.m_SelectedPaddingX : Ime.m_CandidatePaddingX;
			const SImeCandidateMetrics &Metrics = aCandidateMetrics[Cell.m_Index];
			char aNum[4];
			str_format(aNum, sizeof(aNum), "%d", (Cell.m_Index + 1) % 10);
			const float NumX = Cell.m_Rect.x + PaddingX;
			const float TextX = NumX + Metrics.m_Num.m_Width + Ime.m_CandidateNumPaddingX;
			DrawImeText(pTextRender, NumX, CandidateRow.y, CandidateRow.h, Ime.m_FontCandidate, aNum, Metrics.m_Num, Selected ? Ime.m_TextSelected : Ime.m_TextMuted, CandidateDrawAlpha);
			DrawImeText(pTextRender, TextX, CandidateRow.y, CandidateRow.h, Ime.m_FontCandidate, DrawState.m_vCandidates[Cell.m_Index].c_str(), Metrics.m_Text, Selected ? Ime.m_TextSelected : Ime.m_Text, CandidateDrawAlpha);
		}

		if(TrailingWidth > 0.0f)
		{
			CUIRect Divider = More;
			Divider.x += 0.4f;
			Divider.y += 2.0f;
			Divider.w = 0.35f;
			Divider.h = maximum(0.0f, Divider.h - 4.0f);
			Divider.Draw(WithAlpha(Ime.m_PanelBorder, CandidateDrawAlpha * 1.25f), IGraphics::CORNER_ALL, 0.25f);

			DrawImeText(pTextRender,
				More.x + (More.w - PageTextMetrics.m_Width) * 0.5f,
				More.y,
				More.h,
				Ime.m_FontComposition,
				aPageText,
				PageTextMetrics,
				Ime.m_TextMuted,
				CandidateDrawAlpha);
		}
	}
	pTextRender->TextColor(OldTextColor);
	pTextRender->TextOutlineColor(OldOutlineColor);
	pTextRender->SetRenderFlags(OldRenderFlags);
	pGraphics->MapScreen(OldScreenX0, OldScreenY0, OldScreenX1, OldScreenY1);
}
