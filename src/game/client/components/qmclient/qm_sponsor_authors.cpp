#include "qm_sponsor_authors.h"

#include <base/math.h>

#include <game/client/QmUi/UiTokens.h>

#include <algorithm>
#include <cmath>

const std::array<QmSponsorAuthors::SAuthor, 4> &QmSponsorAuthors::Authors()
{
	static constexpr std::array<SAuthor, 4> s_aAuthors = {{
		{"qmclient-community-author-xuanmeng", "璇梦", "qwqdog_mie", false, ColorRGBA(), ColorRGBA()},
		{"qmclient-community-author-dyl", "DYL", "default_v2", true, ui_token::color::NEUTRAL_WHITE, ui_token::color::NEUTRAL_WHITE},
		{"qmclient-community-author-xiari", "夏日", "Miemiemiea", false, ColorRGBA(), ColorRGBA(), true},
		{"qmclient-community-author-qiuka", "丘卡", "Vaporeon", false, ColorRGBA(), ColorRGBA()},
	}};
	return s_aAuthors;
}

QmSponsorAuthors::SNamePresentation QmSponsorAuthors::NamePresentation(const SAuthor &Author, const CUIRect &NameBounds, const CUIRect &RedactedBounds, vec2 Mouse, bool PointerActive, bool WasRedacted)
{
	if(!Author.m_Glitch)
		return {Author.m_pName, 0.0f, false};
	// 替换文字变长后保留同一次悬停，离开文字区域即恢复，避免边缘反复闪回。
	const CUIRect &Hit = WasRedacted ? RedactedBounds : NameBounds;
	const bool Redacted = PointerActive && Mouse.x >= Hit.x && Mouse.x < Hit.x + Hit.w && Mouse.y >= Hit.y && Mouse.y < Hit.y + Hit.h;
	if(Redacted)
		return {"[数据删除]", 0.0f, true};
	const float Dx = std::max({NameBounds.x - Mouse.x, Mouse.x - NameBounds.x - NameBounds.w, 0.0f});
	const float Dy = std::max({NameBounds.y - Mouse.y, Mouse.y - NameBounds.y - NameBounds.h, 0.0f});
	const float Range = std::max(NameBounds.h * 5.0f, 1.0f);
	const float Proximity = PointerActive ? 1.0f - std::clamp(std::sqrt(Dx * Dx + Dy * Dy) / Range, 0.0f, 1.0f) : 0.0f;
	return {Author.m_pName, 0.12f + 0.88f * Proximity * Proximity, false};
}

QmSponsorAuthors::STeePresentation QmSponsorAuthors::TeePresentation(const SAuthor &Author, double TimeSec, int MotionLevel)
{
	STeePresentation Result;
	if(!Author.m_Glitch || MotionLevel < 2 || !std::isfinite(TimeSec))
		return Result;
	// 一个短故障段内离散跳帧，其余时间静止；时间取模，不随运行时长降低精度。
	double Phase = std::fmod(TimeSec, 3.2);
	if(Phase < 0.0)
		Phase += 3.2;
	if(Phase >= 0.36)
		return Result;
	static constexpr vec2 s_aOffsets[] = {{-0.045f, 0.015f}, {0.06f, -0.025f}, {-0.02f, 0.035f}, {0.04f, 0.0f}, {-0.055f, -0.02f}, {0.025f, 0.01f}};
	const int Frame = std::min(static_cast<int>(Phase / 0.06), 5);
	Result.m_Offset = s_aOffsets[Frame];
	Result.m_Scale = Frame % 2 == 0 ? 0.96f : 1.04f;
	return Result;
}

float QmSponsorAuthors::RowsHeight(float TeeSize, float LineSpacing, float LabelHeight)
{
	return maximum(0.0f, TeeSize) + maximum(0.0f, LineSpacing) + maximum(0.0f, LabelHeight);
}
