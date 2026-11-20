#include "QmFinishSummary.h"

#include "QmColorGradient.h"
#include "QmPieMenuRender.h"
#include "UiTokens.h"

#include <base/color.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/components/qmclient/finish_summary_data.h>
#include <game/client/render.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace
{
	using namespace QmFinishSummary;
	using namespace qm_finish_summary;

	class CTextPainter
	{
		IGraphics &m_Graphics;
		ITextRender &m_TextRender;
		CRenderTools &m_RenderTools;
		const CUIRect *m_pTextScreen;
		CUIRect m_TargetScreen;

		void MapTextScreen() const
		{
			if(m_pTextScreen)
				m_Graphics.MapScreen(m_pTextScreen->x, m_pTextScreen->y, m_pTextScreen->x + m_pTextScreen->w, m_pTextScreen->y + m_pTextScreen->h);
		}

		void RestoreTargetScreen() const
		{
			if(m_pTextScreen)
				m_Graphics.MapScreen(m_TargetScreen.x, m_TargetScreen.y, m_TargetScreen.x + m_TargetScreen.w, m_TargetScreen.y + m_TargetScreen.h);
		}

	public:
		CTextPainter(IGraphics &Graphics, ITextRender &TextRender, CRenderTools &RenderTools, const CUIRect *pTextScreen) :
			m_Graphics(Graphics), m_TextRender(TextRender), m_RenderTools(RenderTools), m_pTextScreen(pTextScreen)
		{
			float Right, Bottom;
			Graphics.GetScreen(&m_TargetScreen.x, &m_TargetScreen.y, &Right, &Bottom);
			m_TargetScreen.w = Right - m_TargetScreen.x;
			m_TargetScreen.h = Bottom - m_TargetScreen.y;
		}

		float Width(float FontSize, const char *pText) const
		{
			MapTextScreen();
			const float Width = m_TextRender.TextWidth(FontSize, pText);
			RestoreTargetScreen();
			return Width;
		}

		float Label(const CUIRect &Rect, float FontSize, const char *pText, const ColorRGBA &Color, float Alignment = 0.0f,
			int MaxLines = 1, const SQmColorGradient *pGradient = nullptr, const CUIRect *pGradientBounds = nullptr, const ColorRGBA *pGlow = nullptr) const
		{
			if(Rect.w <= 0.0f || Rect.h <= 0.0f || Color.a <= 0.001f)
				return 0.0f;
			const float TextWidth = std::min(Rect.w, Width(FontSize, pText));
			CTextCursor Cursor;
			Cursor.m_FontSize = FontSize;
			Cursor.m_LineWidth = Rect.w;
			Cursor.m_MaxLines = MaxLines;
			Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
			Cursor.SetPosition(vec2(Rect.x + (Rect.w - TextWidth) * Alignment, Rect.y));
			const CUIRect Bounds = pGradientBounds ? *pGradientBounds : CUIRect{Cursor.m_X, Cursor.m_Y, TextWidth, FontSize};
			const SQmGradientTextPaint Paint{pGradient, Bounds.TopLeft(), Bounds.Size(), Color.a};
			if(pGradient)
			{
				const auto Grid = QmGradientTextGrid(*pGradient, Bounds.Size(), FontSize);
				Cursor.m_pfnColorSampler = SQmGradientTextPaint::Sample;
				Cursor.m_pColorSamplerContext = &Paint;
				Cursor.m_ColorSamplerColumns = Grid[0];
				Cursor.m_ColorSamplerRows = Grid[1];
			}
			m_TextRender.TextColor(Color);
			MapTextScreen();
			STextContainerIndex Container;
			const unsigned SavedFlags = m_TextRender.GetRenderFlags();
			m_TextRender.SetRenderFlags(SavedFlags | TEXT_RENDER_FLAG_ONE_TIME_USE);
			m_TextRender.CreateTextContainer(Container, &Cursor, pText);
			m_TextRender.SetRenderFlags(SavedFlags);
			RestoreTargetScreen();
			if(Container.Valid())
			{
				SQmTextEffectRenderStyle Style;
				Style.m_OutlineColor = ui_token::finish_summary::TEXT_OUTLINE;
				if(pGlow)
				{
					Style.m_Effects = QM_TEXT_EFFECT_GLOW;
					Style.m_GlowRange = FontSize * 0.075f;
					Style.m_GlowColor = pGlow->WithAlpha(0.30f);
					m_RenderTools.RenderTextContainerWithEffects(Container, Style, 0.0f, 0.0f);
				}
				else
					m_TextRender.RenderTextContainer(Container, m_TextRender.DefaultTextColor(), Style.m_OutlineColor);
			}
			m_TextRender.DeleteTextContainer(Container);
			return Cursor.Height();
		}
	};

	SQmColorGradient NumberGradient(const ColorRGBA &Accent, float Progress)
	{
		const ColorRGBA Start = ui_token::finish_summary::GRADIENT_START;
		const ColorRGBA End = ui_token::finish_summary::GRADIENT_END;
		SQmColorGradient Gradient;
		Gradient.m_NumColors = 3;
		Gradient.m_HasPalette = true;
		Gradient.m_Direction = vec2(0.35f, 1.0f);
		const auto Interpolate = [Progress](ColorRGBA From, ColorRGBA To) {
			return ColorRGBA(mix(From.r, To.r, Progress), mix(From.g, To.g, Progress), mix(From.b, To.b, Progress), 1.0f);
		};
		Gradient.m_aColors[0] = Interpolate(Start, End);
		Gradient.m_aColors[1] = Interpolate(Start, Accent);
		Gradient.m_aColors[2] = Interpolate(ui_token::finish_summary::NUMBER_ACCENT_DEEP, Accent);
		return Gradient;
	}

	std::string Number(int64_t Value)
	{
		std::string Result = std::to_string(Value);
		for(int Position = static_cast<int>(Result.size()) - 3; Position > 0; Position -= 3)
			Result.insert(Position, ",");
		return Result;
	}

	const char *StatusText(EStatus Status)
	{
		switch(Status)
		{
		case EStatus::LOADING:
			return Localize("Loading...");
		case EStatus::PENDING:
			return Localize("Pending sync");
		default:
			return Localize("No data");
		}
	}

	bool HasValue(const SValue &Value)
	{
		return Value.m_Value >= 0 && (Value.m_Status == EStatus::READY || Value.m_Status == EStatus::STALE);
	}

	double Target(const SValue &Value)
	{
		return HasValue(Value) ? static_cast<double>(Value.m_Value) : -1.0;
	}

	std::string ValueText(const SValue &Value, int64_t Displayed)
	{
		return HasValue(Value) ? Number(Displayed) : StatusText(Value.m_Status);
	}

	std::string RankText(const SValue &Rank, int64_t Displayed, const SValue *pTotal = nullptr, int64_t DisplayedTotal = 0)
	{
		const bool HasRank = Rank.m_Value > 0 && HasValue(Rank);
		std::string Text = HasRank ? "#" + Number(Displayed) : ValueText(Rank, Displayed);
		if(HasRank && pTotal && HasValue(*pTotal) && pTotal->m_Value > 0)
			Text += "/" + Number(DisplayedTotal);
		return Text;
	}

	void DrawRankGain(IGraphics &Graphics, const CTextPainter &Painter, const CUIRect &Cell, const std::string &Label, float Scale, float Age, float Alpha)
	{
		const ColorRGBA Amber(1.0f, 209 / 255.0f, 71 / 255.0f, 1.0f);
		const float Sweep = std::min(Age / 0.18f, 1.0f);
		const float Settle = std::clamp((Age - 0.30f) / 0.45f, 0.0f, 1.0f);
		const float WashAlpha = Sweep * mix(36.0f, 12.0f, Settle) / 255.0f;
		Graphics.DrawRect(Cell.x, Cell.y, Cell.w, Cell.h, Amber.WithAlpha(WashAlpha * Alpha), 0, 0.0f);
		const float LineHeight = mix(2.0f, 1.0f, Settle) * Scale;
		const ColorRGBA Line = Amber.WithAlpha(Sweep * mix(220.0f, 90.0f, Settle) / 255.0f * Alpha);
		for(const float Y : {Cell.y, Cell.y + Cell.h})
			Graphics.DrawRect(Cell.x, Y - LineHeight * 0.5f, Cell.w * Sweep, LineHeight, Line, IGraphics::CORNER_ALL, LineHeight * 0.5f);

		const float Reveal = std::clamp((Age - 0.10f) / 0.20f, 0.0f, 1.0f);
		if(Reveal <= 0.0f)
			return;
		const float Ease = 1.0f - (1.0f - Reveal) * (1.0f - Reveal);
		const float BadgeScale = Scale * mix(0.85f, 1.0f, Ease);
		const float Width = std::min(132.0f * BadgeScale, Painter.Width(11.0f * BadgeScale, Label.c_str()) + 14.0f * BadgeScale);
		const CUIRect Badge{Cell.x + Cell.w - Width, Cell.y - 8.0f * BadgeScale, Width, 16.0f * BadgeScale};
		const float Opacity = Alpha * Ease;
		if(Reveal < 1.0f)
		{
			const float Radius = mix(3.0f, 14.0f, Ease) * Scale;
			qm_pie_menu_ui::DrawSector(&Graphics, vec2(Badge.x, Cell.y), Radius - Scale, Radius, 0.0f, 360.0f, 360.0f,
				Amber.WithAlpha((1.0f - Reveal) * Alpha * (180 / 255.0f)));
		}
		Graphics.DrawRect(Badge.x, Badge.y, Badge.w, Badge.h, Amber.WithAlpha(Opacity), IGraphics::CORNER_ALL, Badge.h * 0.5f);
		CUIRect Inner;
		Badge.Margin(BadgeScale, &Inner);
		Graphics.DrawRect(Inner.x, Inner.y, Inner.w, Inner.h, ColorRGBA(36 / 255.0f, 27 / 255.0f, 5 / 255.0f, Opacity * (230 / 255.0f)), IGraphics::CORNER_ALL, Inner.h * 0.5f);
		const CUIRect Text{Inner.x + BadgeScale, Badge.y + (2.0f + 3.0f * (1.0f - Ease)) * BadgeScale, Inner.w - 2.0f * BadgeScale, Badge.h};
		Painter.Label(Text, 11.0f * BadgeScale, Label.c_str(), ColorRGBA(1.0f, 246 / 255.0f, 214 / 255.0f, Opacity), 0.5f, 1, nullptr, nullptr, &Amber);
	}

	void RankCell(const CTextPainter &Painter, const CUIRect &Rect, float Scale, const char *pLabel,
		const SValue &Rank, const std::string &Text, const ColorRGBA &Color, float Alpha, float Progress)
	{
		const SQmColorGradient Gradient = NumberGradient(Color, Progress);
		Painter.Label(Rect, 12.0f * Scale, pLabel, ColorRGBA(0.92f, 0.95f, 1.0f, 0.72f * Alpha));
		Painter.Label({Rect.x, Rect.y + 18.0f * Scale, Rect.w, 23.0f * Scale},
			(HasValue(Rank) ? mix(14.0f, 18.0f, Progress) : 13.0f) * Scale, Text.c_str(), Color.WithAlpha(Alpha), 0.0f, 1, HasValue(Rank) ? &Gradient : nullptr, nullptr, HasValue(Rank) ? &Color : nullptr);
	}
}

void qm_finish_summary::CContentCache::Update(const QmFinishSummary::SSnapshot &Snapshot, double NowSeconds, bool FullyExpanded, int MotionLevel)
{
	const double Progress = HasValue(Snapshot.m_CompletedMaps) && HasValue(Snapshot.m_TotalMaps) && Snapshot.m_TotalMaps.m_Value > 0 ?
					std::clamp(static_cast<double>(Snapshot.m_CompletedMaps.m_Value) / Snapshot.m_TotalMaps.m_Value, 0.0, 1.0) :
					-1.0;
	const TNumberTargets Targets = {
		static_cast<double>((Snapshot.m_TimeMs + 5) / 10), static_cast<double>((std::abs(Snapshot.m_DifferenceMs) + 5) / 10),
		Target(Snapshot.m_LocalRank), Target(Snapshot.m_LocalRankTotal), Target(Snapshot.m_ChinaRank), Target(Snapshot.m_Rank),
		Target(Snapshot.m_CompletedMaps), Target(Snapshot.m_TotalMaps), Target(Snapshot.m_Points), Target(Snapshot.m_PointsGained), Progress};
	m_Presentation.Update(Snapshot.m_Id, NowSeconds, FullyExpanded, MotionLevel, Targets, Snapshot.m_Stars, Snapshot.m_RankPointsGained > 0);
	const bool Changed = !m_Valid || m_Id != Snapshot.m_Id || m_Revision != Snapshot.m_Revision || m_Language != g_Config.m_ClLanguagefile;
	std::array<int64_t, static_cast<size_t>(ENumber::COUNT)> aNumbers{};
	for(size_t i = 0; i < aNumbers.size(); ++i)
		aNumbers[i] = std::llround(m_Presentation.Number(static_cast<ENumber>(i)));
	const auto Value = [&](ENumber Number) { return aNumbers[static_cast<size_t>(Number)]; };
	const auto NumberChanged = [&](ENumber Number) { return Changed || Value(Number) != m_aDisplayedNumbers[static_cast<size_t>(Number)]; };

	if(Changed)
	{
		m_Valid = true;
		m_Id = Snapshot.m_Id;
		m_Revision = Snapshot.m_Revision;
		m_Language = g_Config.m_ClLanguagefile;
		if(Snapshot.m_RankPointsGained > 0)
		{
			char aRankGain[64];
			str_format(aRankGain, sizeof(aRankGain), Localize("+%s Rank"), Number(Snapshot.m_RankPointsGained).c_str());
			m_RankGain = aRankGain;
		}
		else
		{
			m_RankGain.clear();
		}
		m_Type = Snapshot.m_Category.empty() ? Localize("No data") : Localize(Snapshot.m_Category.c_str());
		if(Snapshot.m_Stars >= 0)
			m_Type += " ·";
		switch(Snapshot.m_Comparison)
		{
		case EComparison::FIRST:
			m_Comparison = Localize("First finish");
			break;
		case EComparison::BEST:
		case EComparison::IMPROVED:
			m_Comparison = Localize("Personal best");
			break;
		case EComparison::TIED:
			m_Comparison = Localize("Matched personal best");
			break;
		case EComparison::SLOWER:
			m_Comparison = Localize("Slower than personal best");
			break;
		case EComparison::UNKNOWN:
			m_Comparison = Localize("Personal best pending");
			break;
		}
	}
	if(NumberChanged(ENumber::TIME))
	{
		char aTime[64];
		str_time(Value(ENumber::TIME), TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
		const std::string Time(aTime);
		const size_t Dot = Time.find_last_of('.');
		m_TimeMajor = Dot == std::string::npos ? Time : Time.substr(0, Dot);
		m_TimeMinor = Dot == std::string::npos ? "" : Time.substr(Dot);
	}
	if(NumberChanged(ENumber::DIFFERENCE))
	{
		m_Difference.clear();
		if(Snapshot.m_Comparison == EComparison::IMPROVED || Snapshot.m_Comparison == EComparison::SLOWER)
		{
			char aTime[64];
			str_time(Value(ENumber::DIFFERENCE), TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
			char aDifference[128];
			str_format(aDifference, sizeof(aDifference), Snapshot.m_Comparison == EComparison::IMPROVED ? Localize("Improved -%s") : Localize("Slower +%s"), aTime);
			m_Difference = aDifference;
		}
	}
	if(NumberChanged(ENumber::LOCAL_RANK) || NumberChanged(ENumber::LOCAL_TOTAL))
		m_LocalRank = RankText(Snapshot.m_LocalRank, Value(ENumber::LOCAL_RANK), &Snapshot.m_LocalRankTotal, Value(ENumber::LOCAL_TOTAL));
	if(NumberChanged(ENumber::CHINA_RANK))
		m_ChinaRank = RankText(Snapshot.m_ChinaRank, Value(ENumber::CHINA_RANK));
	if(NumberChanged(ENumber::RANK))
		m_Rank = RankText(Snapshot.m_Rank, Value(ENumber::RANK));
	if(NumberChanged(ENumber::COMPLETED) || NumberChanged(ENumber::TOTAL))
	{
		const std::string Category = Snapshot.m_Category.empty() ? Localize("No data") : Localize(Snapshot.m_Category.c_str());
		m_Completion = Category + " " + Localize("Completed") + " ";
		if(HasValue(Snapshot.m_CompletedMaps) && HasValue(Snapshot.m_TotalMaps))
			m_Completion += Number(Value(ENumber::COMPLETED)) + "/" + Number(Value(ENumber::TOTAL));
		else
			m_Completion += HasValue(Snapshot.m_CompletedMaps) ? StatusText(Snapshot.m_TotalMaps.m_Status) : StatusText(Snapshot.m_CompletedMaps.m_Status);
	}
	if(NumberChanged(ENumber::POINTS))
		m_Points = std::string(Localize("Total points")) + " " + ValueText(Snapshot.m_Points, Value(ENumber::POINTS));
	if(NumberChanged(ENumber::GAIN))
		m_Gain = HasValue(Snapshot.m_PointsGained) ? "+" + Number(Value(ENumber::GAIN)) : StatusText(Snapshot.m_PointsGained.m_Status);
	m_aDisplayedNumbers = aNumbers;
}

bool qm_finish_summary::Render(IGraphics &Graphics, ITextRender &TextRender, CRenderTools &RenderTools, const CUIRect &Rect,
	const QmFinishSummary::SSnapshot &Snapshot, const CManagedTeeRenderInfo *pTeeRenderInfo, const CContentCache &Content,
	float Alpha, const CUIRect *pTextScreen)
{
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return false;
	if(Alpha <= 0.001f)
		return Content.m_Presentation.Complete();
	const CTextPainter Painter(Graphics, TextRender, RenderTools, pTextScreen);
	const ColorRGBA SavedColor = TextRender.GetTextColor();
	const ColorRGBA SavedOutline = TextRender.GetTextOutlineColor();
	const EFontPreset SavedFont = TextRender.GetFontPreset();
	TextRender.SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender.TextOutlineColor(0, 0, 0, 0.2f * Alpha);
	const float Scale = std::min(Rect.w / 480.0f, Rect.h / 216.0f);
	const float OriginX = Rect.x + (Rect.w - 480.0f * Scale) * 0.5f;
	const float OriginY = Rect.y + (Rect.h - 216.0f * Scale) * 0.5f;
	const auto Box = [&](float X, float Y, float Width, float Height) {
		return CUIRect{OriginX + X * Scale, OriginY + Y * Scale, Width * Scale, Height * Scale};
	};
	const auto FieldAlpha = [&](EField Field) { return Content.m_Presentation.FieldAlpha(Field) * Alpha; };
	const ColorRGBA White(0.98f, 0.99f, 1.0f, 1.0f);
	const ColorRGBA Muted(0.82f, 0.86f, 0.92f, 1.0f);
	const ColorRGBA Gold(1.0f, 0.88f, 0.42f, 1.0f);

	// 用时先占据中心，落到左上后再逐项展示成绩详情。
	const float Placement = Content.m_Presentation.TimePlacement();
	const CUIRect TimeBox = Box(40.0f + (16.0f - 40.0f) * Placement, 72.0f + (14.0f - 72.0f) * Placement,
		400.0f + (216.0f - 400.0f) * Placement, 60.0f);
	Painter.Label(TimeBox, 12.0f * Scale, Localize("Time to finish"), Muted.WithAlpha(Alpha), (1.0f - Placement) * 0.5f);
	const float TimeProgress = Content.m_Presentation.NumberColorProgress(ENumber::TIME);
	if(TimeProgress > 0.0f)
	{
		const char *pMajor = Content.m_TimeMajor.c_str();
		const char *pMinor = Content.m_TimeMinor.c_str();
		float TimeSize = mix(42.0f, 32.0f, Placement) * Scale * mix(0.78f, 1.0f, TimeProgress);
		float TimeWidth = Painter.Width(TimeSize, pMajor) + Painter.Width(TimeSize * 0.58f, pMinor);
		if(TimeWidth > TimeBox.w)
			TimeSize *= TimeBox.w / TimeWidth;
		TimeWidth = Painter.Width(TimeSize, pMajor) + Painter.Width(TimeSize * 0.58f, pMinor);
		const float TimeX = TimeBox.x + (TimeBox.w - TimeWidth) * (1.0f - Placement) * 0.5f;
		const float TimeY = TimeBox.y + 19.0f * Scale;
		const SQmColorGradient TimeGradient = NumberGradient(Gold, TimeProgress);
		const CUIRect TimeBounds{TimeX, TimeY, TimeWidth, TimeSize};
		const float MajorWidth = Painter.Width(TimeSize, pMajor);
		Painter.Label({TimeX, TimeY, MajorWidth + Scale, TimeSize}, TimeSize, pMajor, White.WithAlpha(Alpha), 0.0f, 1, &TimeGradient, &TimeBounds, &Gold);
		Painter.Label({TimeX + MajorWidth, TimeY + TimeSize * 0.42f, TimeBox.w - MajorWidth, TimeSize},
			TimeSize * 0.58f, pMinor, White.WithAlpha(Alpha), 0.0f, 1, &TimeGradient, &TimeBounds, &Gold);
	}

	const CUIRect MapBox = Box(250, 16, 214, 36);
	const float MapHeight = Painter.Label(MapBox, mix(13.0f, 17.0f, Content.m_Presentation.FieldAlpha(EField::MAP)) * Scale,
		Snapshot.m_Map.c_str(), White.WithAlpha(FieldAlpha(EField::MAP)), 0.0f, 2);
	const CUIRect AuthorBox{MapBox.x, MapBox.y + MapHeight + 1.5f * Scale, MapBox.w, 15.0f * Scale};
	Painter.Label(AuthorBox, 12.0f * Scale, Snapshot.m_Mapper.empty() ? Localize("No data") : Snapshot.m_Mapper.c_str(),
		Muted.WithAlpha(FieldAlpha(EField::MAPPER)), 1.0f);

	const CUIRect TypeRow = Box(250, 74, 214, 15);
	const float TypeSize = 12.0f * Scale;
	const int Stars = Snapshot.m_Stars < 0 ? 0 : Snapshot.m_MaxStars;
	const float StarWidth = Stars > 0 ? std::max(Painter.Width(TypeSize, "★"), Painter.Width(TypeSize, "☆")) + 1.0f * Scale : 0.0f;
	const float TypeWidth = std::max(0.0f, TypeRow.w - StarWidth * Stars - 2.0f * Scale);
	Painter.Label({TypeRow.x, TypeRow.y, TypeWidth, TypeRow.h}, TypeSize, Content.m_Type.c_str(), White.WithAlpha(FieldAlpha(EField::TYPE)));
	const float StarX = TypeRow.x + std::min(TypeWidth, Painter.Width(TypeSize, Content.m_Type.c_str())) + 2.0f * Scale;
	for(int i = 0; i < Stars; ++i)
	{
		const float Reveal = Content.m_Presentation.StarAppearance(i);
		const CUIRect Cell{StarX + i * StarWidth, TypeRow.y + (1.0f - Reveal) * 3.0f * Scale, StarWidth, TypeRow.h};
		Painter.Label(Cell, TypeSize * mix(0.72f, 1.0f, Reveal), i < Snapshot.m_Stars ? "★" : "☆",
			White.WithAlpha(Reveal * FieldAlpha(EField::TYPE)), 0.5f);
	}
	Painter.Label(Box(250, 94, 214, 15), 12.0f * Scale, Snapshot.m_FinishedAt.empty() ? Localize("No data") : Snapshot.m_FinishedAt.c_str(), Muted.WithAlpha(FieldAlpha(EField::FINISHED_AT)));

	const bool PersonalBest = Snapshot.m_Comparison == EComparison::FIRST || Snapshot.m_Comparison == EComparison::BEST || Snapshot.m_Comparison == EComparison::IMPROVED;
	const ColorRGBA ComparisonColor = PersonalBest ? Gold : Muted;
	const float BadgeWidth = std::min(216.0f * Scale, Painter.Width(12.0f * Scale, Content.m_Comparison.c_str()) + 14.0f * Scale);
	const CUIRect Badge{OriginX + 16.0f * Scale, OriginY + 76.0f * Scale, BadgeWidth, 18.0f * Scale};
	Graphics.DrawRect(Badge.x, Badge.y, Badge.w, Badge.h, ComparisonColor.WithAlpha(0.16f * FieldAlpha(EField::COMPARISON)), IGraphics::CORNER_ALL, Badge.h * 0.5f);
	Painter.Label({Badge.x + 7.0f * Scale, Badge.y + 2.0f * Scale, Badge.w - 14.0f * Scale, Badge.h}, 12.0f * Scale,
		Content.m_Comparison.c_str(), ComparisonColor.WithAlpha(FieldAlpha(EField::COMPARISON)));
	const ColorRGBA DifferenceColor = Snapshot.m_Comparison == EComparison::IMPROVED ? ui_token::finish_summary::DIFFERENCE_IMPROVED : ui_token::finish_summary::DIFFERENCE_REGRESSED;
	const SQmColorGradient DifferenceGradient = NumberGradient(DifferenceColor, Content.m_Presentation.NumberColorProgress(ENumber::DIFFERENCE));
	Painter.Label(Box(16, 99, 216, 15), 12.0f * Scale, Content.m_Difference.c_str(), DifferenceColor.WithAlpha(FieldAlpha(EField::DIFFERENCE)), 0.0f, 1, &DifferenceGradient);

	const auto DrawDivider = [&](float Y, EField Field) {
		const float Width = 448.0f * Content.m_Presentation.FieldAlpha(Field);
		const CUIRect Line = Box(240.0f - Width * 0.5f, Y, Width, 0.8f);
		if(Width > 0.0f)
			Graphics.DrawRect(Line.x, Line.y, Line.w, Line.h, White.WithAlpha(0.18f * FieldAlpha(Field)), 0, 0.0f);
	};
	DrawDivider(118.0f, EField::LOCAL_RANK);
	const bool Ddnet = Snapshot.m_Community == ECommunity::DDNET;
	DrawDivider(171.0f, EField::PLAYER);
	const auto RankGainTime = Content.m_Presentation.RankGainTime();
	if(Ddnet && RankGainTime && !Content.m_RankGain.empty())
		DrawRankGain(Graphics, Painter, Box(332, 118, 132, 53), Content.m_RankGain, Scale, *RankGainTime, Alpha);
	const float RankWidth = Ddnet ? 136.0f : 216.0f;
	RankCell(Painter, Box(16, 126, RankWidth, 39), Scale, Localize("Server rank"), Snapshot.m_LocalRank, Content.m_LocalRank, Gold, FieldAlpha(EField::LOCAL_RANK), Content.m_Presentation.NumberColorProgress(ENumber::LOCAL_RANK));
	if(Ddnet)
		RankCell(Painter, Box(174, 126, 132, 39), Scale, Localize("China rank"), Snapshot.m_ChinaRank, Content.m_ChinaRank, White, FieldAlpha(EField::CHINA_RANK), Content.m_Presentation.NumberColorProgress(ENumber::CHINA_RANK));
	RankCell(Painter, Box(Ddnet ? 332.0f : 250.0f, 126, RankWidth, 39), Scale,
		Ddnet ? Localize("Global rank") : Localize("Axiom rank"), Snapshot.m_Rank, Content.m_Rank, White, FieldAlpha(EField::RANK), Content.m_Presentation.NumberColorProgress(ENumber::RANK));

	const float PlayerAlpha = FieldAlpha(EField::PLAYER);
	if(pTeeRenderInfo && PlayerAlpha > 0.001f)
	{
		CTeeRenderInfo TeeInfo = pTeeRenderInfo->TeeRenderInfo();
		TeeInfo.m_Size = 30.0f * Scale;
		vec2 Offset;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, Offset);
		RenderTools.RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f),
			vec2(OriginX + 31.0f * Scale, OriginY + 192.0f * Scale + Offset.y), PlayerAlpha);
	}
	Painter.Label(Box(52, 183, 100, 18), 14.0f * Scale, Snapshot.m_Player.c_str(), White.WithAlpha(PlayerAlpha));
	const SQmColorGradient CompletionGradient = NumberGradient(Gold, Content.m_Presentation.NumberColorProgress(ENumber::COMPLETED));
	Painter.Label(Box(164, 180, 168, 16), 12.0f * Scale, Content.m_Completion.c_str(), White.WithAlpha(FieldAlpha(EField::COMPLETION)), 0.5f, 1, &CompletionGradient);
	const CUIRect Bar = Box(164, 201, 168, 3);
	Graphics.DrawRect(Bar.x, Bar.y, Bar.w, Bar.h, White.WithAlpha(0.20f * FieldAlpha(EField::COMPLETION)), IGraphics::CORNER_ALL, Bar.h * 0.5f);
	const float Progress = std::clamp(static_cast<float>(Content.m_Presentation.Number(ENumber::PROGRESS)), 0.0f, 1.0f);
	Graphics.DrawRect(Bar.x, Bar.y, Bar.w * Progress, Bar.h, Gold.WithAlpha(FieldAlpha(EField::COMPLETION)), IGraphics::CORNER_ALL, Bar.h * 0.5f);
	const SQmColorGradient PointsGradient = NumberGradient(Gold, Content.m_Presentation.NumberColorProgress(ENumber::POINTS));
	Painter.Label(Box(348, 179, 116, 16), 12.0f * Scale, Content.m_Points.c_str(), White.WithAlpha(FieldAlpha(EField::POINTS)), 1.0f, 1, &PointsGradient);
	const float GainWidth = std::min(116.0f * Scale, Painter.Width(12.0f * Scale, Content.m_Gain.c_str()) + 14.0f * Scale);
	const CUIRect GainBadge{OriginX + 464.0f * Scale - GainWidth, OriginY + 196.0f * Scale, GainWidth, 16.0f * Scale};
	Graphics.DrawRect(GainBadge.x, GainBadge.y, GainBadge.w, GainBadge.h, Gold.WithAlpha(0.9f * FieldAlpha(EField::POINTS)), IGraphics::CORNER_ALL, GainBadge.h * 0.5f);
	const SQmColorGradient GainGradient = NumberGradient(ui_token::finish_summary::GAIN_TEXT_DEEP, Content.m_Presentation.NumberColorProgress(ENumber::GAIN));
	Painter.Label({GainBadge.x + 7.0f * Scale, GainBadge.y + Scale, GainBadge.w - 14.0f * Scale, GainBadge.h},
		12.0f * Scale, Content.m_Gain.c_str(), ui_token::finish_summary::GAIN_TEXT_SOFT.WithAlpha(FieldAlpha(EField::POINTS)), 0.5f, 1, &GainGradient);
	TextRender.SetFontPreset(SavedFont);
	TextRender.TextColor(SavedColor);
	TextRender.TextOutlineColor(SavedOutline);
	return Content.m_Presentation.Complete();
}
