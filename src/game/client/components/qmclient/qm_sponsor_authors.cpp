#include "qm_sponsor_authors.h"

#include <base/math.h>

#include <game/client/QmUi/UiTokens.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

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

QmSponsorAuthors::STeePresentation QmSponsorAuthors::TeePresentation(const SAuthor &Author, double TimeSec)
{
	STeePresentation Result;
	if(!Author.m_Glitch)
		return Result;
	if(!std::isfinite(TimeSec))
		TimeSec = 0.0;
	// 故障始终存在；只在有界时间内计算相位，不使用全局 UI 动效等级。
	double Phase = std::fmod(TimeSec, 8.0);
	if(Phase < 0.0)
		Phase += 8.0;
	const float Pulse = 0.5f + 0.5f * std::sin(static_cast<float>(Phase * pi * 1.5));
	const uint32_t Frame = static_cast<uint32_t>(Phase * 18.0);
	const auto Noise = [](uint32_t Seed) {
		Seed ^= Seed >> 16;
		Seed *= 0x7feb352du;
		Seed ^= Seed >> 15;
		return static_cast<float>(Seed & 0xffffu) / 65535.0f;
	};
	Result.m_Glitch = true;
	Result.m_ChromaOffset = 0.035f + 0.035f * Pulse;
	Result.m_ChromaAlpha = 0.38f + 0.18f * Pulse;
	for(size_t Index = 0; Index < Result.m_aSliceOffsets.size(); ++Index)
	{
		const float Sign = (Index + Frame) % 2 == 0 ? -1.0f : 1.0f;
		const float Tear = Index == Frame % Result.m_aSliceOffsets.size() ? 0.035f : 0.0f;
		Result.m_aSliceOffsets[Index] = Sign * (0.018f + 0.04f * Noise(Frame * 37u + static_cast<uint32_t>(Index) * 101u + 1u) + Tear);
	}
	Result.m_ScanlineY = static_cast<float>(std::fmod(Phase * 0.5, 1.0));
	Result.m_ScanlineHeight = 0.035f;
	Result.m_ScanlineAlpha = 0.45f + 0.20f * Pulse;
	return Result;
}

float QmSponsorAuthors::RowsHeight(float TeeSize, float LineSpacing, float LabelHeight)
{
	return maximum(0.0f, TeeSize) + maximum(0.0f, LineSpacing) + maximum(0.0f, LabelHeight);
}
