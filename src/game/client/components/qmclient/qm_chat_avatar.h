#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_CHAT_AVATAR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_CHAT_AVATAR_H

#include <engine/image.h>

#include <game/client/render.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace QmChatAvatar
{
	inline constexpr int SIZE = 80;
	using TImage = std::array<uint8_t, SIZE * SIZE * 4>;

	enum ESprite
	{
		BODY,
		BODY_OUTLINE,
		FEET,
		FEET_OUTLINE,
		EYES,
		DECORATION,
		DECORATION_OUTLINE,
		MARKING,
		SHADOW,
		UPPER_OUTLINE,
		NUM_SPRITES,
	};

	struct SSprite
	{
		int m_Width = 0;
		int m_Height = 0;
		std::vector<uint8_t> m_vRgba;

		bool Empty() const { return m_vRgba.empty(); }
	};

	// 素材在皮肤加载时缩小并只读共享，旧消息不依赖纹理句柄或皮肤缓存存活。
	struct SSource
	{
		std::array<SSprite, NUM_SPRITES> m_aSprites;
	};

	struct SSnapshot
	{
		std::array<std::shared_ptr<const SSource>, protocol7::NUM_SKINPARTS> m_apParts;
		std::array<ColorRGBA, protocol7::NUM_SKINPARTS> m_aColors;
		bool m_Sixup = false;
	};

	template<typename TSprite>
	inline SSprite CopySprite(const CImageInfo &Image, const TSprite &Sprite)
	{
		SSprite Result;
		if(Image.m_pData == nullptr || Image.m_Format != CImageInfo::FORMAT_RGBA ||
			Sprite.m_pSet->m_Gridx <= 0 || Sprite.m_pSet->m_Gridy <= 0)
			return Result;
		const int UnitX = Image.m_Width / Sprite.m_pSet->m_Gridx;
		const int UnitY = Image.m_Height / Sprite.m_pSet->m_Gridy;
		const int X = Sprite.m_X * UnitX, Y = Sprite.m_Y * UnitY;
		const int Width = Sprite.m_W * UnitX, Height = Sprite.m_H * UnitY;
		if(Width <= 0 || Height <= 0 || X < 0 || Y < 0 ||
			(size_t)X + (size_t)Width > Image.m_Width || (size_t)Y + (size_t)Height > Image.m_Height)
			return Result;
		const float Scale = std::min(1.0f, 64.0f / std::max(Width, Height));
		Result.m_Width = std::max(1, (int)std::round(Width * Scale));
		Result.m_Height = std::max(1, (int)std::round(Height * Scale));
		Result.m_vRgba.resize(Result.m_Width * Result.m_Height * 4);
		for(int y = 0; y < Result.m_Height; ++y)
		{
			for(int x = 0; x < Result.m_Width; ++x)
			{
				// 固定四点采样，超大皮肤也不遍历整张原图。
				float aPremultiplied[4] = {};
				for(int sy = 0; sy < 2; ++sy)
				{
					for(int sx = 0; sx < 2; ++sx)
					{
						const int SourceX = X + std::min(Width - 1, (int)((x + 0.25f + sx * 0.5f) * Width / Result.m_Width));
						const int SourceY = Y + std::min(Height - 1, (int)((y + 0.25f + sy * 0.5f) * Height / Result.m_Height));
						const uint8_t *pPixel = Image.m_pData + (SourceY * Image.m_Width + SourceX) * 4;
						for(int Channel = 0; Channel < 3; ++Channel)
							aPremultiplied[Channel] += pPixel[Channel] * pPixel[3];
						aPremultiplied[3] += pPixel[3];
					}
				}
				uint8_t *pTarget = Result.m_vRgba.data() + (y * Result.m_Width + x) * 4;
				for(int Channel = 0; Channel < 3; ++Channel)
					pTarget[Channel] = aPremultiplied[3] > 0 ? (uint8_t)std::round(aPremultiplied[Channel] / aPremultiplied[3]) : 0;
				pTarget[3] = (uint8_t)std::round(aPremultiplied[3] / 4);
			}
		}
		return Result;
	}

	inline std::shared_ptr<const SSnapshot> Capture(const CTeeRenderInfo &Info, int Dummy = 0)
	{
		Dummy = std::clamp(Dummy, 0, NUM_DUMMIES - 1);
		const auto &Sixup = Info.m_aSixup[Dummy];
		const auto &pSixupBody = (Sixup.m_aUseCustomColors[protocol7::SKINPART_BODY] ?
						  Sixup.m_apChatAvatarColorable :
						  Sixup.m_apChatAvatarOriginal)[protocol7::SKINPART_BODY];
		const auto &pSix = (Info.m_CustomColoredSkin ? Info.m_ColorableRenderSkin : Info.m_OriginalRenderSkin).m_QmChatAvatar;
		if(!pSixupBody && !pSix)
			return nullptr;
		auto pSnapshot = std::make_shared<SSnapshot>();
		pSnapshot->m_aColors.fill(ColorRGBA(1, 1, 1, 1));
		if(pSixupBody)
		{
			pSnapshot->m_Sixup = true;
			for(int Part = 0; Part < protocol7::NUM_SKINPARTS; ++Part)
			{
				pSnapshot->m_apParts[Part] = (Sixup.m_aUseCustomColors[Part] ? Sixup.m_apChatAvatarColorable : Sixup.m_apChatAvatarOriginal)[Part];
				pSnapshot->m_aColors[Part] = Sixup.m_aColors[Part];
			}
		}
		else
		{
			pSnapshot->m_apParts[protocol7::SKINPART_BODY] = pSix;
			pSnapshot->m_aColors[protocol7::SKINPART_BODY] = Info.m_ColorBody;
			pSnapshot->m_aColors[protocol7::SKINPART_FEET] = Info.m_ColorFeet;
		}
		return pSnapshot;
	}

	inline void Blend(TImage &Image, int x, int y, ColorRGBA Color)
	{
		uint8_t *pPixel = Image.data() + (y * SIZE + x) * 4;
		const float Alpha = std::clamp(Color.a, 0.0f, 1.0f);
		const float aRgb[] = {Color.r, Color.g, Color.b};
		for(int Channel = 0; Channel < 3; ++Channel)
			pPixel[Channel] = (uint8_t)std::clamp(std::round(aRgb[Channel] * 255 * Alpha + pPixel[Channel] * (1 - Alpha)), 0.0f, 255.0f);
	}

	inline void DrawSprite(TImage &Image, const SSource *pSource, ESprite SpriteId, ColorRGBA Color, float CenterX, float CenterY, float Width, float Height, bool Flip = false)
	{
		if(pSource == nullptr || pSource->m_aSprites[SpriteId].Empty())
			return;
		const SSprite &Sprite = pSource->m_aSprites[SpriteId];
		const float Left = CenterX - Width / 2, Top = CenterY - Height / 2;
		for(int y = std::max(0, (int)std::floor(Top)); y < std::min(SIZE, (int)std::ceil(Top + Height)); ++y)
		{
			for(int x = std::max(0, (int)std::floor(Left)); x < std::min(SIZE, (int)std::ceil(Left + Width)); ++x)
			{
				const float u = (x + 0.5f - Left) / Width;
				const float v = (y + 0.5f - Top) / Height;
				if(u < 0 || u >= 1 || v < 0 || v >= 1)
					continue;
				const int SourceX = std::clamp((int)((Flip ? 1 - u : u) * Sprite.m_Width), 0, Sprite.m_Width - 1);
				const int SourceY = std::clamp((int)(v * Sprite.m_Height), 0, Sprite.m_Height - 1);
				const uint8_t *pPixel = Sprite.m_vRgba.data() + (SourceY * Sprite.m_Width + SourceX) * 4;
				Blend(Image, x, y, ColorRGBA(pPixel[0] / 255.0f * Color.r, pPixel[1] / 255.0f * Color.g, pPixel[2] / 255.0f * Color.b, pPixel[3] / 255.0f * Color.a));
			}
		}
	}

	inline void DrawEllipse(TImage &Image, float CenterX, float CenterY, float RadiusX, float RadiusY, ColorRGBA Color)
	{
		for(int y = std::max(0, (int)(CenterY - RadiusY - 1)); y < std::min(SIZE, (int)(CenterY + RadiusY + 1)); ++y)
			for(int x = std::max(0, (int)(CenterX - RadiusX - 1)); x < std::min(SIZE, (int)(CenterX + RadiusX + 1)); ++x)
			{
				const float dx = (x + 0.5f - CenterX) / RadiusX, dy = (y + 0.5f - CenterY) / RadiusY;
				const float Coverage = std::clamp((1 - std::sqrt(dx * dx + dy * dy)) * std::min(RadiusX, RadiusY) + 0.5f, 0.0f, 1.0f);
				Blend(Image, x, y, Color.WithAlpha(Color.a * Coverage));
			}
	}

	inline TImage Render(const SSnapshot *pSnapshot, const std::string &Sender)
	{
		TImage Image{};
		// 名字只用于稳定选择备用配色；不使用运行时随机状态。
		unsigned Palette = 0;
		for(unsigned char Character : Sender)
			Palette = (Palette * 33 + Character) % 360;
		const ColorRGBA Accent = color_cast<ColorRGBA>(ColorHSLA(Palette / 360.0f, 0.58f, 0.62f));
		for(size_t Offset = 0; Offset < Image.size(); Offset += 4)
		{
			Image[Offset] = (uint8_t)(Accent.r * 46 + 22);
			Image[Offset + 1] = (uint8_t)(Accent.g * 46 + 22);
			Image[Offset + 2] = (uint8_t)(Accent.b * 46 + 22);
			Image[Offset + 3] = 255;
		}
		const SSource *pBody = pSnapshot ? pSnapshot->m_apParts[protocol7::SKINPART_BODY].get() : nullptr;
		if(pBody && !pBody->m_aSprites[BODY].Empty())
		{
			const ColorRGBA White(1, 1, 1, 1);
			const ColorRGBA BodyColor = pSnapshot->m_aColors[protocol7::SKINPART_BODY];
			const ColorRGBA FeetColor = pSnapshot->m_aColors[protocol7::SKINPART_FEET];
			const bool Sixup = pSnapshot->m_Sixup;
			const SSource *pFeet = Sixup ? pSnapshot->m_apParts[protocol7::SKINPART_FEET].get() : pBody;
			const float FeetWidth = Sixup ? 64.0f / 2.1f : 64.0f;
			const float FeetHeight = Sixup ? 64.0f / 2.1f : 32.0f;
			for(int Pass = 0; Pass < 2; ++Pass)
			{
				const bool Outline = Pass == 0;
				const ESprite FeetSprite = Outline ? FEET_OUTLINE : FEET;
				const ColorRGBA FootColor = Outline && Sixup ? White : FeetColor;
				DrawSprite(Image, pFeet, FeetSprite, FootColor, 33, 54, FeetWidth, FeetHeight);
				if(Sixup)
					DrawSprite(Image, pSnapshot->m_apParts[protocol7::SKINPART_DECORATION].get(), Outline ? DECORATION_OUTLINE : DECORATION,
						pSnapshot->m_aColors[protocol7::SKINPART_DECORATION], 40, 40, 64, 64);
				DrawSprite(Image, pBody, Outline ? BODY_OUTLINE : BODY, Outline && Sixup ? White : BodyColor, 40, 40, 64, 64);
				if(!Outline)
				{
					if(Sixup)
					{
						const ColorRGBA Marking = pSnapshot->m_aColors[protocol7::SKINPART_MARKING];
						DrawSprite(Image, pSnapshot->m_apParts[protocol7::SKINPART_MARKING].get(), MARKING, ColorRGBA(Marking.r * Marking.a, Marking.g * Marking.a, Marking.b * Marking.a, Marking.a), 40, 40, 64, 64);
						DrawSprite(Image, pBody, SHADOW, White, 40, 40, 64, 64);
						DrawSprite(Image, pBody, UPPER_OUTLINE, White, 40, 40, 64, 64);
						DrawSprite(Image, pSnapshot->m_apParts[protocol7::SKINPART_EYES].get(), EYES, pSnapshot->m_aColors[protocol7::SKINPART_EYES], 44, 36.8f, 38.4f, 19.2f);
					}
					else
					{
						DrawSprite(Image, pBody, EYES, BodyColor, 40, 36.8f, 25.6f, 25.6f);
						DrawSprite(Image, pBody, EYES, BodyColor, 49, 36.8f, 25.6f, 25.6f, true);
					}
				}
				DrawSprite(Image, pFeet, FeetSprite, FootColor, 47, 54, FeetWidth, FeetHeight);
			}
		}
		else
		{
			const ColorRGBA Outline(0.06f, 0.07f, 0.09f, 1);
			DrawEllipse(Image, 29, 58, 15, 9, Outline);
			DrawEllipse(Image, 40, 38, 26, 26, Outline);
			DrawEllipse(Image, 40, 37, 23, 23, Accent);
			DrawEllipse(Image, 29, 58, 12, 6, Accent);
			DrawEllipse(Image, 52, 58, 15, 9, Outline);
			DrawEllipse(Image, 52, 58, 12, 6, Accent);
			DrawEllipse(Image, 40, 35, 6, 9, ColorRGBA(1, 1, 1, 1));
			DrawEllipse(Image, 50, 35, 6, 9, ColorRGBA(1, 1, 1, 1));
			DrawEllipse(Image, 42, 36, 2.5f, 4, Outline);
			DrawEllipse(Image, 52, 36, 2.5f, 4, Outline);
		}
		for(int y = 0; y < SIZE; ++y)
			for(int x = 0; x < SIZE; ++x)
			{
				const float dx = x + 0.5f - SIZE / 2, dy = y + 0.5f - SIZE / 2;
				Image[(y * SIZE + x) * 4 + 3] = (uint8_t)std::round(std::clamp(SIZE / 2.0f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f) * 255);
			}
		return Image;
	}
}

#endif
