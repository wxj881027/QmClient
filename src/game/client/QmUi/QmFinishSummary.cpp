#include "QmFinishSummary.h"

#include <base/color.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/qmclient/finish_summary_data.h>
#include <game/localization.h>

#include <algorithm>
#include <cstdlib>
#include <string>

namespace
{
	using namespace QmFinishSummary;

	void Label(ITextRender &TextRender, const CUIRect &Rect, float FontSize, const char *pText, const ColorRGBA &Color, float Alignment = 0.0f, int MaxLines = 1)
	{
		if(Rect.w <= 0.0f || Rect.h <= 0.0f || Color.a <= 0.001f)
			return;
		const float Width = std::min(Rect.w, TextRender.TextWidth(FontSize, pText));
		CTextCursor Cursor;
		Cursor.m_FontSize = FontSize;
		Cursor.m_LineWidth = Rect.w;
		Cursor.m_MaxLines = MaxLines;
		Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
		Cursor.SetPosition(vec2(Rect.x + (Rect.w - Width) * Alignment, Rect.y));
		TextRender.TextColor(Color);
		TextRender.TextEx(&Cursor, pText);
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
		case EStatus::STALE:
			return Localize("Last sync");
		default:
			return Localize("No data");
		}
	}

	bool HasValue(const SValue &Value)
	{
		return Value.m_Value >= 0 && (Value.m_Status == EStatus::READY || Value.m_Status == EStatus::STALE);
	}

	std::string ValueText(const SValue &Value)
	{
		return HasValue(Value) ? Number(Value.m_Value) : StatusText(Value.m_Status);
	}

	std::string RankText(const SValue &Rank, const SValue *pTotal = nullptr)
	{
		const bool HasRank = Rank.m_Value > 0 && HasValue(Rank);
		std::string Text = HasRank ? "#" + Number(Rank.m_Value) : ValueText(Rank);
		if(HasRank && pTotal && pTotal->m_Status == EStatus::READY && pTotal->m_Value > 0)
			Text += "/" + Number(pTotal->m_Value);
		return Text;
	}

	void RankRow(IGraphics &Graphics, ITextRender &TextRender, const CUIRect &Rect, float Scale, const char *pLabel,
		const SValue &Rank, const std::string &Text, const ColorRGBA &Color, float Alpha, bool Divider)
	{
		const bool HasRank = Rank.m_Value > 0 && HasValue(Rank);
		Label(TextRender, {Rect.x, Rect.y, Rect.w * 0.47f, Rect.h}, 5.2f * Scale, pLabel, ColorRGBA(1, 1, 1, 0.72f * Alpha));
		Label(TextRender, {Rect.x + Rect.w * 0.45f, Rect.y - 1.0f * Scale, Rect.w * 0.55f, Rect.h},
			(HasRank ? 9.0f : 4.5f) * Scale, Text.c_str(), Color, 1.0f);
		if(Rank.m_Status == EStatus::STALE)
			Label(TextRender, {Rect.x + Rect.w * 0.5f, Rect.y + 9.0f * Scale, Rect.w * 0.5f, Rect.h}, 3.5f * Scale, StatusText(Rank.m_Status), ColorRGBA(1, 1, 1, 0.6f * Alpha), 1.0f);
		if(Divider)
			Graphics.DrawRect(Rect.x, Rect.y + Rect.h - 2.5f * Scale, Rect.w, 0.3f * Scale, ColorRGBA(1, 1, 1, 0.12f * Alpha), 0, 0.0f);
	}
}

void qm_finish_summary::CContentCache::Update(const QmFinishSummary::SSnapshot &Snapshot)
{
	if(m_Valid && m_Id == Snapshot.m_Id && m_Revision == Snapshot.m_Revision && m_Language == g_Config.m_ClLanguagefile)
		return;
	m_Valid = true;
	m_Id = Snapshot.m_Id;
	m_Revision = Snapshot.m_Revision;
	m_Language = g_Config.m_ClLanguagefile;

	const std::string Category = Snapshot.m_Category.empty() ? Localize("No data") : Localize(Snapshot.m_Category.c_str());
	m_Type = Category;
	if(Snapshot.m_Stars >= 0)
	{
		m_Type += " · ";
		for(int i = 0; i < Snapshot.m_MaxStars; ++i)
			m_Type += i < Snapshot.m_Stars ? "★" : "☆";
	}
	char aTime[64];
	str_time((Snapshot.m_TimeMs + 5) / 10, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
	const std::string Time(aTime);
	const size_t Dot = Time.find_last_of('.');
	m_TimeMajor = Dot == std::string::npos ? Time : Time.substr(0, Dot);
	m_TimeMinor = Dot == std::string::npos ? "" : Time.substr(Dot);
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
	m_Difference.clear();
	if(Snapshot.m_Comparison == EComparison::IMPROVED || Snapshot.m_Comparison == EComparison::SLOWER)
	{
		str_time((std::abs(Snapshot.m_DifferenceMs) + 5) / 10, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
		char aDifference[128];
		str_format(aDifference, sizeof(aDifference), Snapshot.m_Comparison == EComparison::IMPROVED ? Localize("Improved +%s") : Localize("Slower -%s"), aTime);
		m_Difference = aDifference;
	}
	m_LocalRank = RankText(Snapshot.m_LocalRank, &Snapshot.m_LocalRankTotal);
	m_ChinaRank = RankText(Snapshot.m_ChinaRank);
	m_Rank = RankText(Snapshot.m_Rank);
	m_Completion = Category + " " + Localize("Completed") + " ";
	if(HasValue(Snapshot.m_CompletedMaps) && HasValue(Snapshot.m_TotalMaps))
		m_Completion += ValueText(Snapshot.m_CompletedMaps) + "/" + ValueText(Snapshot.m_TotalMaps);
	else
		m_Completion += HasValue(Snapshot.m_CompletedMaps) ? ValueText(Snapshot.m_TotalMaps) : ValueText(Snapshot.m_CompletedMaps);
	m_Points = std::string(Localize("Total points")) + " " + ValueText(Snapshot.m_Points);
	m_Gain = Snapshot.m_PointsGained.m_Status == EStatus::READY ? "+" + Number(Snapshot.m_PointsGained.m_Value) : ValueText(Snapshot.m_PointsGained);
}

void qm_finish_summary::Render(IGraphics &Graphics, ITextRender &TextRender, const CUIRect &Rect,
	const QmFinishSummary::SSnapshot &Snapshot, CContentCache &Content, float Alpha)
{
	using namespace QmFinishSummary;
	if(Alpha <= 0.001f || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;
	Content.Update(Snapshot);
	const ColorRGBA SavedColor = TextRender.GetTextColor();
	const ColorRGBA SavedOutline = TextRender.GetTextOutlineColor();
	const EFontPreset SavedFont = TextRender.GetFontPreset();
	TextRender.SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender.TextOutlineColor(0, 0, 0, 0.2f * Alpha);
	const float Scale = std::min(Rect.w / 340.0f, Rect.h / 142.8f);
	const float Pad = 16.0f * Scale;
	const float X = Rect.x + Pad;
	const float Y = Rect.y + 25.0f * Scale;
	const float Width = Rect.w - 2.0f * Pad;
	const float Col = Width / 3.0f;
	const float FooterY = Rect.y + Rect.h - 35.0f * Scale;
	const ColorRGBA White(0.98f, 0.99f, 1.0f, Alpha);
	const ColorRGBA Muted(0.92f, 0.95f, 1.0f, 0.76f * Alpha);
	const ColorRGBA Gold(1.0f, 0.88f, 0.42f, Alpha);

	// 三栏与底部信息条按参考图保持固定占位。
	Graphics.DrawRect(X + Col, Y - 3.0f * Scale, 0.3f * Scale, FooterY - Y - 13.0f * Scale, ColorRGBA(1, 1, 1, 0.22f * Alpha), 0, 0.0f);
	Graphics.DrawRect(X + 2.0f * Col, Y - 3.0f * Scale, 0.3f * Scale, FooterY - Y - 13.0f * Scale, ColorRGBA(1, 1, 1, 0.22f * Alpha), 0, 0.0f);
	Graphics.DrawRect(X, FooterY, Width, 0.35f * Scale, ColorRGBA(1, 1, 1, 0.25f * Alpha), 0, 0.0f);
	Label(TextRender, {X, Y, Col - 12.0f * Scale, 28.0f * Scale}, 12.0f * Scale, Snapshot.m_Map.c_str(), White, 0.0f, 2);
	Label(TextRender, {X, Y + 29.0f * Scale, Col - 12.0f * Scale, 8.0f * Scale}, 6.0f * Scale,
		Snapshot.m_Mapper.empty() ? Localize("No data") : Snapshot.m_Mapper.c_str(), Muted);
	Label(TextRender, {X, Y + 49.0f * Scale, Col - 10.0f * Scale, 8.0f * Scale}, 5.4f * Scale, Content.m_Type.c_str(), White);
	Label(TextRender, {X, Y + 61.0f * Scale, Col - 10.0f * Scale, 8.0f * Scale}, 4.8f * Scale, Snapshot.m_FinishedAt.c_str(), Muted);

	const CUIRect Center{X + Col + 7.0f * Scale, Y, Col - 14.0f * Scale, FooterY - Y};
	Label(TextRender, Center, 5.6f * Scale, Localize("Time to finish"), Muted, 0.5f);
	const char *pMajor = Content.m_TimeMajor.c_str();
	const char *pMinor = Content.m_TimeMinor.c_str();
	float TimeSize = 23.5f * Scale;
	float TotalTimeWidth = TextRender.TextWidth(TimeSize, pMajor) + TextRender.TextWidth(TimeSize * 0.58f, pMinor);
	if(TotalTimeWidth > Center.w)
		TimeSize *= Center.w / TotalTimeWidth;
	TotalTimeWidth = TextRender.TextWidth(TimeSize, pMajor) + TextRender.TextWidth(TimeSize * 0.58f, pMinor);
	const float TimeX = Center.x + (Center.w - TotalTimeWidth) * 0.5f;
	TextRender.TextColor(White);
	TextRender.Text(TimeX, Y + 14.0f * Scale, TimeSize, pMajor);
	TextRender.Text(TimeX + TextRender.TextWidth(TimeSize, pMajor), Y + 14.0f * Scale + TimeSize * 0.42f, TimeSize * 0.58f, pMinor);
	const char *pComparison = Content.m_Comparison.c_str();
	const bool PersonalBest = Snapshot.m_Comparison == EComparison::FIRST || Snapshot.m_Comparison == EComparison::BEST || Snapshot.m_Comparison == EComparison::IMPROVED;
	const ColorRGBA ComparisonColor = PersonalBest ? Gold : Muted;
	const float BadgeWidth = std::min(Center.w, TextRender.TextWidth(5.2f * Scale, pComparison) + 12.0f * Scale);
	const CUIRect Badge{Center.x + (Center.w - BadgeWidth) * 0.5f, Y + 48.0f * Scale, BadgeWidth, 10.0f * Scale};
	Graphics.DrawRect(Badge.x, Badge.y, Badge.w, Badge.h, ComparisonColor.WithAlpha(0.16f * Alpha), IGraphics::CORNER_ALL, Badge.h * 0.5f);
	Label(TextRender, {Badge.x + 3.0f * Scale, Badge.y + 2.0f * Scale, Badge.w - 6.0f * Scale, Badge.h}, 5.2f * Scale, pComparison, ComparisonColor, 0.5f);
	if(Snapshot.m_Comparison == EComparison::IMPROVED || Snapshot.m_Comparison == EComparison::SLOWER)
	{
		const bool Improved = Snapshot.m_Comparison == EComparison::IMPROVED;
		Label(TextRender, {Center.x, Y + 63.0f * Scale, Center.w, 8.0f * Scale}, 5.2f * Scale, Content.m_Difference.c_str(),
			Improved ? ColorRGBA(0.55f, 0.96f, 0.73f, Alpha) : ColorRGBA(1.0f, 0.66f, 0.60f, Alpha), 0.5f);
	}

	const bool Ddnet = Snapshot.m_Community == ECommunity::DDNET;
	const float RankX = X + 2.0f * Col + 12.0f * Scale;
	const float RankWidth = Col - 12.0f * Scale;
	const float RowHeight = 23.5f * Scale;
	RankRow(Graphics, TextRender, {RankX, Y + 2.0f * Scale, RankWidth, RowHeight}, Scale, Localize("Server rank"), Snapshot.m_LocalRank, Content.m_LocalRank, Gold, Alpha, true);
	if(Ddnet)
		RankRow(Graphics, TextRender, {RankX, Y + RowHeight + 2.0f * Scale, RankWidth, RowHeight}, Scale, Localize("China rank"), Snapshot.m_ChinaRank, Content.m_ChinaRank, White, Alpha, true);
	RankRow(Graphics, TextRender, {RankX, Y + (Ddnet ? 2.0f : 1.0f) * RowHeight + 2.0f * Scale, RankWidth, RowHeight}, Scale,
		Ddnet ? Localize("Global rank") : Localize("Axiom rank"), Snapshot.m_Rank, Content.m_Rank, White, Alpha, false);

	const float FooterTextY = FooterY + 13.0f * Scale;
	Label(TextRender, {X, FooterTextY, Col - 12.0f * Scale, 11.0f * Scale}, 8.5f * Scale, Snapshot.m_Player.c_str(), White);
	Label(TextRender, {Center.x, FooterTextY, Center.w, 9.0f * Scale}, 5.2f * Scale, Content.m_Completion.c_str(), White, 0.5f);
	const CUIRect Bar{Center.x + 6.0f * Scale, FooterTextY + 11.0f * Scale, Center.w - 12.0f * Scale, 1.6f * Scale};
	Graphics.DrawRect(Bar.x, Bar.y, Bar.w, Bar.h, ColorRGBA(1, 1, 1, 0.2f * Alpha), IGraphics::CORNER_ALL, Bar.h * 0.5f);
	if(HasValue(Snapshot.m_CompletedMaps) && HasValue(Snapshot.m_TotalMaps) && Snapshot.m_TotalMaps.m_Value > 0)
	{
		const float Progress = std::clamp(static_cast<float>(Snapshot.m_CompletedMaps.m_Value) / Snapshot.m_TotalMaps.m_Value, 0.0f, 1.0f);
		Graphics.DrawRect(Bar.x, Bar.y, Bar.w * Progress, Bar.h, Gold, IGraphics::CORNER_ALL, Bar.h * 0.5f);
		if(Snapshot.m_CompletedMaps.m_Status == EStatus::STALE || Snapshot.m_TotalMaps.m_Status == EStatus::STALE)
			Label(TextRender, {Center.x, FooterTextY + 16.0f * Scale, Center.w, 5.0f * Scale}, 3.5f * Scale, Localize("Last sync"), Muted, 0.5f);
	}
	const float GainWidth = 29.0f * Scale;
	Label(TextRender, {RankX, FooterTextY, RankWidth - GainWidth - 3.0f * Scale, 12.0f * Scale}, 5.4f * Scale, Content.m_Points.c_str(), White, 1.0f);
	if(HasValue(Snapshot.m_Points) && Snapshot.m_Points.m_Status == EStatus::STALE)
		Label(TextRender, {RankX, FooterTextY + 11.0f * Scale, RankWidth - GainWidth - 3.0f * Scale, 5.0f * Scale}, 3.5f * Scale, Localize("Last sync"), Muted, 1.0f);
	const CUIRect GainBadge{RankX + RankWidth - GainWidth, FooterTextY - 1.5f * Scale, GainWidth, 10.5f * Scale};
	Graphics.DrawRect(GainBadge.x, GainBadge.y, GainBadge.w, GainBadge.h, Gold.WithAlpha(0.9f * Alpha), IGraphics::CORNER_ALL, GainBadge.h * 0.5f);
	Label(TextRender, {GainBadge.x + 2.0f * Scale, GainBadge.y + 2.3f * Scale, GainBadge.w - 4.0f * Scale, GainBadge.h},
		(Snapshot.m_PointsGained.m_Status == EStatus::READY ? 5.2f : 3.8f) * Scale, Content.m_Gain.c_str(), ColorRGBA(0.18f, 0.15f, 0.06f, Alpha), 0.5f);
	TextRender.SetFontPreset(SavedFont);
	TextRender.TextColor(SavedColor);
	TextRender.TextOutlineColor(SavedOutline);
}
