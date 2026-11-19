#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TEXT_EFFECT_RENDER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TEXT_EFFECT_RENDER_H

#include <game/client/components/nameplate_text_effects.h>
#include <game/client/render.h>

#include <algorithm>
#include <cmath>

// 同步借用文字接口与句柄；世界歌词和通用渲染入口共用同一份特效绘制。
inline void QmRenderTextContainerWithEffects(ITextRender &TextRender, const STextContainerIndex &TextContainerIndex, const SQmTextEffectRenderStyle &Style, float X, float Y)
{
	if(!TextContainerIndex.Valid())
		return;

	const float Alpha = std::clamp(Style.m_TextColor.a, 0.0f, 1.0f);
	if(Alpha <= 0.0f)
		return;

	const ColorRGBA EmptyText(0.0f, 0.0f, 0.0f, 0.0f);
	const bool BorderEnabled = (Style.m_Effects & QM_TEXT_EFFECT_BORDER) != 0 && Style.m_BorderColor.a > 0.0f && Style.m_BorderRange > 0.0f;
	const bool GlowEnabled = (Style.m_Effects & QM_TEXT_EFFECT_GLOW) != 0 && Style.m_GlowColor.a > 0.0f && Style.m_GlowRange > 0.0f;
	const bool RainbowEnabled = (Style.m_Effects & QM_TEXT_EFFECT_RAINBOW) != 0;
	// 自适应档位只削减外层特效圈数，本体永远绘制。
	const int BorderPasses = BorderEnabled ? std::clamp(round_to_int(Style.m_BorderRange), 1, 4) : 0;
	const int GlowPasses = GlowEnabled ? std::clamp(round_to_int(Style.m_GlowRange), 1, 6) : 0;
	const SQmNameplateEffectPasses Passes = QmNameplateEffectResolvePasses(Style.m_MaxEffectDraws, BorderPasses, GlowPasses);
	auto RenderOutlineOnly = [&](ColorRGBA Color, float OffsetX, float OffsetY) {
		TextRender.RenderTextContainer(TextContainerIndex, EmptyText, Color, X + OffsetX, Y + OffsetY);
	};
	static constexpr vec2 s_aBorderDirections[] = {
		vec2(1.0f, 0.0f),
		vec2(-1.0f, 0.0f),
		vec2(0.0f, 1.0f),
		vec2(0.0f, -1.0f),
		vec2(0.70710677f, 0.70710677f),
		vec2(-0.70710677f, 0.70710677f),
		vec2(0.70710677f, -0.70710677f),
		vec2(-0.70710677f, -0.70710677f),
	};
	static constexpr vec2 s_aGlowDirections[] = {
		vec2(1.0f, 0.0f),
		vec2(-1.0f, 0.0f),
		vec2(0.0f, 1.0f),
		vec2(0.0f, -1.0f),
	};

	if(GlowEnabled)
	{
		for(int Pass = 0; Pass < Passes.m_GlowPasses; ++Pass)
		{
			const float Radius = Style.m_GlowRange * (float)(Pass + 1) / (float)GlowPasses;
			const float PassAlpha = Style.m_GlowColor.a * Alpha * (1.0f - (float)Pass / (float)(GlowPasses + 1));
			const ColorRGBA Glow = Style.m_GlowColor.WithAlpha(PassAlpha);
			for(const vec2 &Dir : s_aGlowDirections)
				RenderOutlineOnly(Glow, Dir.x * Radius, Dir.y * Radius);
		}
	}

	ColorRGBA OutlineColor = Style.m_OutlineColor.WithMultipliedAlpha(Alpha);
	if(BorderEnabled)
		OutlineColor = Style.m_BorderColor.WithMultipliedAlpha(Alpha);
	if(BorderEnabled && Style.m_BorderRange > 1.0f)
	{
		for(int Pass = 0; Pass < Passes.m_BorderPasses; ++Pass)
		{
			const float Radius = (float)(Pass + 1);
			const float PassAlpha = OutlineColor.a * (1.0f - (float)Pass / (float)(BorderPasses + 1));
			const ColorRGBA Border = OutlineColor.WithAlpha(PassAlpha);
			for(const vec2 &Dir : s_aBorderDirections)
				RenderOutlineOnly(Border, Dir.x * Radius, Dir.y * Radius);
		}
	}

	ColorRGBA TextColor = Style.m_TextColor;
	if(RainbowEnabled)
	{
		const float Hue = std::fmod(Style.m_Time * 0.15f, 1.0f);
		TextColor = color_cast<ColorRGBA>(ColorHSLA(Hue, 0.7f, 0.65f, Alpha));
	}

	TextRender.RenderTextContainer(TextContainerIndex, TextColor, OutlineColor, X, Y);
}

#endif
