/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "render.h"

#include "animstate.h"

#include <base/math.h>

#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/shared/config.h>

#include <generated/client_data.h>
#include <generated/client_data7.h>
#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/gameclient.h>
#include <game/mapitems.h>

#include <cmath>

CSkinDescriptor::CSkinDescriptor()
{
	Reset();
}

void CSkinDescriptor::Reset()
{
	m_Flags = 0;
	m_aSkinName[0] = '\0';
	for(auto &Sixup : m_aSixup)
	{
		Sixup.Reset();
	}
}

bool CSkinDescriptor::IsValid() const
{
	return (m_Flags & (FLAG_SIX | FLAG_SEVEN)) != 0;
}

bool CSkinDescriptor::operator==(const CSkinDescriptor &Other) const
{
	if(m_Flags != Other.m_Flags)
	{
		return false;
	}

	if(m_Flags & FLAG_SIX)
	{
		if(str_comp(m_aSkinName, Other.m_aSkinName) != 0)
		{
			return false;
		}
	}

	if(m_Flags & FLAG_SEVEN)
	{
		for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
		{
			if(m_aSixup[Dummy] != Other.m_aSixup[Dummy])
			{
				return false;
			}
		}
	}

	return true;
}

void CSkinDescriptor::CSixup::Reset()
{
	for(auto &aSkinPartName : m_aaSkinPartNames)
	{
		aSkinPartName[0] = '\0';
	}
	m_BotDecoration = false;
	m_XmasHat = false;
}

bool CSkinDescriptor::CSixup::operator==(const CSixup &Other) const
{
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		if(str_comp(m_aaSkinPartNames[Part], Other.m_aaSkinPartNames[Part]) != 0)
		{
			return false;
		}
	}
	return m_BotDecoration == Other.m_BotDecoration &&
	       m_XmasHat == Other.m_XmasHat;
}

static ColorRGBA TeeOutlineRenderColor(const CTeeRenderInfo *pInfo, ColorRGBA DefaultColor, float Alpha)
{
	if(pInfo->m_TeeRenderFlags & TEE_CUSTOM_OUTLINE_COLOR)
		return pInfo->m_OutlineColor.WithAlpha(Alpha);
	return DefaultColor.WithAlpha(Alpha);
}

void CRenderTools::Init(IGraphics *pGraphics, ITextRender *pTextRender, CGameClient *pGameClient)
{
	m_pGraphics = pGraphics;
	m_pTextRender = pTextRender;
	m_pGameClient = pGameClient;
	m_TeeQuadContainerIndex = Graphics()->CreateQuadContainer(false);
	Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);

	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->QuadContainerAddSprite(m_TeeQuadContainerIndex, 64.f);
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->QuadContainerAddSprite(m_TeeQuadContainerIndex, 64.f);

	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->QuadContainerAddSprite(m_TeeQuadContainerIndex, 64.f * 0.4f);
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->QuadContainerAddSprite(m_TeeQuadContainerIndex, 64.f * 0.4f);
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->QuadContainerAddSprite(m_TeeQuadContainerIndex, 64.f * 0.4f);
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->QuadContainerAddSprite(m_TeeQuadContainerIndex, 64.f * 0.4f);
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->QuadContainerAddSprite(m_TeeQuadContainerIndex, 64.f * 0.4f);

	// Feet
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->QuadContainerAddSprite(m_TeeQuadContainerIndex, -32.f, -16.f, 64.f, 32.f);
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->QuadContainerAddSprite(m_TeeQuadContainerIndex, -32.f, -16.f, 64.f, 32.f);

	// Mirrored Feet
	Graphics()->QuadsSetSubsetFree(1, 0, 0, 0, 0, 1, 1, 1);
	Graphics()->QuadContainerAddSprite(m_TeeQuadContainerIndex, -32.f, -16.f, 64.f, 32.f);
	Graphics()->QuadsSetSubsetFree(1, 0, 0, 0, 0, 1, 1, 1);
	Graphics()->QuadContainerAddSprite(m_TeeQuadContainerIndex, -32.f, -16.f, 64.f, 32.f);

	Graphics()->QuadContainerUpload(m_TeeQuadContainerIndex);
}

void CRenderTools::RenderCursor(vec2 Center, float Size, float Alpha) const
{
	Graphics()->WrapClamp();
	Graphics()->TextureSet(g_pData->m_aImages[IMAGE_CURSOR].m_Id);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	IGraphics::CQuadItem QuadItem(Center.x, Center.y, Size, Size);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();
	Graphics()->WrapNormal();
}

void CRenderTools::RenderIcon(int ImageId, int SpriteId, const CUIRect *pRect, const ColorRGBA *pColor) const
{
	Graphics()->TextureSet(g_pData->m_aImages[ImageId].m_Id);
	Graphics()->QuadsBegin();
	Graphics()->SelectSprite(SpriteId);
	if(pColor)
		Graphics()->SetColor(pColor->r * pColor->a, pColor->g * pColor->a, pColor->b * pColor->a, pColor->a);
	IGraphics::CQuadItem QuadItem(pRect->x, pRect->y, pRect->w, pRect->h);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();
}

void CRenderTools::RenderTextContainerWithEffects(STextContainerIndex TextContainerIndex, const SQmTextEffectRenderStyle &Style, float X, float Y) const
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
	auto RenderOutlineOnly = [&](ColorRGBA Color, float OffsetX, float OffsetY) {
		TextRender()->RenderTextContainer(TextContainerIndex, EmptyText, Color, X + OffsetX, Y + OffsetY);
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
		const int GlowPasses = std::clamp(round_to_int(Style.m_GlowRange), 1, 6);
		for(int Pass = 0; Pass < GlowPasses; ++Pass)
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
		const int BorderPasses = std::clamp(round_to_int(Style.m_BorderRange), 1, 4);
		for(int Pass = 0; Pass < BorderPasses; ++Pass)
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

	TextRender()->RenderTextContainer(TextContainerIndex, TextColor, OutlineColor, X, Y);
}

void CRenderTools::GetRenderTeeAnimScaleAndBaseSize(const CTeeRenderInfo *pInfo, float &AnimScale, float &BaseSize)
{
	AnimScale = pInfo->m_Size * 1.0f / 64.0f;
	BaseSize = pInfo->m_Size;
}

void CRenderTools::GetRenderTeeBodyScale(float BaseSize, float &BodyScale)
{
	BodyScale = g_Config.m_ClFatSkins ? BaseSize * 1.3f : BaseSize;
	BodyScale /= 64.0f;
}

void CRenderTools::GetRenderTeeFeetScale(float BaseSize, float &FeetScaleWidth, float &FeetScaleHeight)
{
	FeetScaleWidth = BaseSize / 64.0f;
	FeetScaleHeight = (BaseSize / 2) / 32.0f;
}

void CRenderTools::GetRenderTeeBodySize(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 &BodyOffset, float &Width, float &Height)
{
	float AnimScale, BaseSize;
	GetRenderTeeAnimScaleAndBaseSize(pInfo, AnimScale, BaseSize);

	float BodyScale;
	GetRenderTeeBodyScale(BaseSize, BodyScale);

	Width = pInfo->m_SkinMetrics.m_Body.WidthNormalized() * 64.0f * BodyScale;
	Height = pInfo->m_SkinMetrics.m_Body.HeightNormalized() * 64.0f * BodyScale;
	BodyOffset.x = pInfo->m_SkinMetrics.m_Body.OffsetXNormalized() * 64.0f * BodyScale;
	BodyOffset.y = pInfo->m_SkinMetrics.m_Body.OffsetYNormalized() * 64.0f * BodyScale;
}

void CRenderTools::GetRenderTeeFeetSize(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 &FeetOffset, float &Width, float &Height)
{
	float AnimScale, BaseSize;
	GetRenderTeeAnimScaleAndBaseSize(pInfo, AnimScale, BaseSize);

	float FeetScaleWidth, FeetScaleHeight;
	GetRenderTeeFeetScale(BaseSize, FeetScaleWidth, FeetScaleHeight);

	Width = pInfo->m_SkinMetrics.m_Feet.WidthNormalized() * 64.0f * FeetScaleWidth;
	Height = pInfo->m_SkinMetrics.m_Feet.HeightNormalized() * 32.0f * FeetScaleHeight;
	FeetOffset.x = pInfo->m_SkinMetrics.m_Feet.OffsetXNormalized() * 64.0f * FeetScaleWidth;
	FeetOffset.y = pInfo->m_SkinMetrics.m_Feet.OffsetYNormalized() * 32.0f * FeetScaleHeight;
}

void CRenderTools::GetRenderTeeBodyBounds(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, float AssumedScale, float AnimScale, float &MinX, float &MinY, float &MaxX, float &MaxY)
{
	const vec2 BodyPos = vec2(pAnim->GetBody()->m_X, pAnim->GetBody()->m_Y) * AnimScale;
	vec2 BodyOffset;
	float BodyWidth, BodyHeight;
	GetRenderTeeBodySize(pAnim, pInfo, BodyOffset, BodyWidth, BodyHeight);
	MinX = -32.0f * AssumedScale + BodyPos.x + BodyOffset.x;
	MinY = -32.0f * AssumedScale + BodyPos.y + BodyOffset.y;
	MaxX = MinX + BodyWidth;
	MaxY = MinY + BodyHeight;
}

void CRenderTools::ExpandRenderTeeFeetBounds(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, float AssumedScale, float AnimScale, float &MinX, float &MaxX, float &MaxY)
{
	vec2 FeetOffset;
	float FeetWidth, FeetHeight;
	GetRenderTeeFeetSize(pAnim, pInfo, FeetOffset, FeetWidth, FeetHeight);
	const vec2 FeetPos[2] = {
		vec2(pAnim->GetFrontFoot()->m_X, pAnim->GetFrontFoot()->m_Y) * AnimScale,
		vec2(pAnim->GetBackFoot()->m_X, pAnim->GetBackFoot()->m_Y) * AnimScale,
	};
	for(const vec2 &FootPos : FeetPos)
	{
		const float FootMinX = -32.0f * AssumedScale + FootPos.x + FeetOffset.x;
		MinX = minimum(MinX, FootMinX);
		MaxX = maximum(MaxX, FootMinX + FeetWidth);
		MaxY = maximum(MaxY, -16.0f * AssumedScale + FootPos.y + FeetOffset.y + FeetHeight);
	}
}
void CRenderTools::GetRenderTeeOffsetToRenderedTee(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 &TeeOffsetToMid)
{
	if(pInfo->m_aSixup[g_Config.m_ClDummy].PartTexture(protocol7::SKINPART_BODY).IsValid())
	{
		TeeOffsetToMid = vec2(0.0f, pInfo->m_Size * 0.12f);
		return;
	}

	float AnimScale, BaseSize;
	GetRenderTeeAnimScaleAndBaseSize(pInfo, AnimScale, BaseSize);
	const float AssumedScale = BaseSize / 64.0f;
	float MinX, MinY, MaxX, MaxY;
	GetRenderTeeBodyBounds(pAnim, pInfo, AssumedScale, AnimScale, MinX, MinY, MaxX, MaxY);
	ExpandRenderTeeFeetBounds(pAnim, pInfo, AssumedScale, AnimScale, MinX, MaxX, MaxY);
	TeeOffsetToMid.x = 0.0f;
	TeeOffsetToMid.y = -(MinY + (MaxY - MinY) / 2.0f);
}

void CRenderTools::RenderTee(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, float Alpha) const
{
	RenderTee(pAnim, pInfo, Emote, Dir, Pos, Alpha, vec2(1.0f, 1.0f), vec2(1.0f, 1.0f), 0.0f, 0.0f);
}

void CRenderTools::RenderTee(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, int TeeRenderFlags, float Alpha) const
{
	CTeeRenderInfo Info = *pInfo;
	Info.m_TeeRenderFlags = (Info.m_TeeRenderFlags & ~TEE_PREVIEW_LAYER_ALL) | TeeRenderFlags;
	RenderTee(pAnim, &Info, Emote, Dir, Pos, Alpha);
}

void CRenderTools::RenderTeeWithSkinChangeTransition(const CAnimState *pAnim, const CTeeRenderInfo *pPreviousInfo, const CTeeRenderInfo *pCurrentInfo, int Emote, vec2 Dir, vec2 Pos, float Progress, float Alpha, vec2 BodyScale, vec2 FeetScale, float BodyAngle, float FeetAngle) const
{
	if(pCurrentInfo == nullptr)
	{
		return;
	}

	Progress = ClampSkinChangeTransitionProgress(Progress);
	if(pPreviousInfo == nullptr || !pPreviousInfo->Valid() || Progress >= 1.0f)
	{
		RenderTee(pAnim, pCurrentInfo, Emote, Dir, Pos, Alpha, BodyScale, FeetScale, BodyAngle, FeetAngle);
		return;
	}

	const SSkinChangeTransitionBlend Blend = ComputeSkinChangeTransitionBlend(Progress, BodyScale, FeetScale, g_Config.m_QmSkinChangeTransitionType, g_Config.m_QmSkinChangeTransitionEasing, g_Config.m_QmSkinChangeTransitionIntensity);
	if(Blend.m_PreviousAlpha > 0.0f)
	{
		RenderTee(pAnim, pPreviousInfo, Emote, Dir, Pos + Blend.m_PreviousPosOffset, Alpha * Blend.m_PreviousAlpha, Blend.m_PreviousBodyScale, Blend.m_PreviousFeetScale, BodyAngle + Blend.m_PreviousAngleOffset, FeetAngle + Blend.m_PreviousAngleOffset);
	}
	if(Blend.m_CurrentAlpha > 0.0f)
	{
		RenderTee(pAnim, pCurrentInfo, Emote, Dir, Pos + Blend.m_CurrentPosOffset, Alpha * Blend.m_CurrentAlpha, Blend.m_CurrentBodyScale, Blend.m_CurrentFeetScale, BodyAngle + Blend.m_CurrentAngleOffset, FeetAngle + Blend.m_CurrentAngleOffset);
	}
}

void CRenderTools::RenderTee(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, float Alpha, vec2 BodyScale, vec2 FeetScale, float BodyAngle, float FeetAngle) const
{
	const bool SixupBodyValid = CTeeRenderInfo::IsDrawableTexture(pInfo->m_aSixup[g_Config.m_ClDummy].PartTexture(protocol7::SKINPART_BODY));
	const bool SixBodyValid = CTeeRenderInfo::IsDrawableTexture(pInfo->m_CustomColoredSkin ? pInfo->m_ColorableRenderSkin.m_Body : pInfo->m_OriginalRenderSkin.m_Body);
	if(SixupBodyValid)
		RenderTee7(pAnim, pInfo, Emote, Dir, Pos, Alpha, BodyScale, FeetScale, BodyAngle, FeetAngle);
	else if(SixBodyValid)
		RenderTee6(pAnim, pInfo, Emote, Dir, Pos, Alpha, BodyScale, FeetScale, BodyAngle, FeetAngle);

	Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
	Graphics()->QuadsSetRotation(0);
}

void CRenderTools::RenderTee7(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, float Alpha, vec2 BodyScale, vec2 FeetScale, float BodyAngle, float FeetAngle) const
{
	vec2 Direction = Dir;
	vec2 Position = Pos;
	const bool IsBot = CTeeRenderInfo::IsDrawableTexture(pInfo->m_aSixup[g_Config.m_ClDummy].m_BotTexture);

	// first pass we draw the outline
	// second pass we draw the filling
	for(int Pass = 0; Pass < 2; Pass++)
	{
		bool OutLine = Pass == 0;
		if(OutLine && !HasTeePreviewLayer(pInfo->m_TeeRenderFlags, TEE_PREVIEW_LAYER_OUTLINE))
			continue;

		for(int Filling = 0; Filling < 2; Filling++)
		{
			float AnimScale = pInfo->m_Size * 1.0f / 64.0f;
			float BaseSize = pInfo->m_Size;
			if(Filling == 1)
			{
				vec2 BodyPos = Position + vec2(pAnim->GetBody()->m_X, pAnim->GetBody()->m_Y) * AnimScale;
				IGraphics::CQuadItem BodyItem(BodyPos.x, BodyPos.y, BaseSize * BodyScale.x, BaseSize * BodyScale.y);
				IGraphics::CQuadItem Item;
				const bool DrawBody = HasTeePreviewLayer(pInfo->m_TeeRenderFlags, OutLine ? TEE_PREVIEW_LAYER_BODY_OUTLINE : TEE_PREVIEW_LAYER_BODY);
				const bool DrawEyes = !OutLine && HasTeePreviewLayer(pInfo->m_TeeRenderFlags, TEE_PREVIEW_LAYER_EYES);

				if(DrawBody && IsBot && !OutLine)
				{
					IGraphics::CQuadItem BotItem(BodyPos.x + (2.f / 3.f) * AnimScale, BodyPos.y + (-16 + 2.f / 3.f) * AnimScale, BaseSize * BodyScale.x, BaseSize * BodyScale.y); // x+0.66, y+0.66 to correct some rendering bug

					// draw bot visuals (background)
					Graphics()->TextureSet(pInfo->m_aSixup[g_Config.m_ClDummy].m_BotTexture);
					Graphics()->QuadsBegin();
					Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
					Graphics()->SelectSprite7(client_data7::SPRITE_TEE_BOT_BACKGROUND);
					Item = BotItem;
					Graphics()->QuadsDraw(&Item, 1);
					Graphics()->QuadsEnd();

					// draw bot visuals (foreground)
					Graphics()->TextureSet(pInfo->m_aSixup[g_Config.m_ClDummy].m_BotTexture);
					Graphics()->QuadsBegin();
					Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
					Graphics()->SelectSprite7(client_data7::SPRITE_TEE_BOT_FOREGROUND);
					Item = BotItem;
					Graphics()->QuadsDraw(&Item, 1);
					Graphics()->SetColor(pInfo->m_aSixup[g_Config.m_ClDummy].m_BotColor.WithAlpha(Alpha));
					Graphics()->SelectSprite7(client_data7::SPRITE_TEE_BOT_GLOW);
					Item = BotItem;
					Graphics()->QuadsDraw(&Item, 1);
					Graphics()->QuadsEnd();
				}

				// draw decoration
				const IGraphics::CTextureHandle &DecorationTexture = pInfo->m_aSixup[g_Config.m_ClDummy].PartTexture(protocol7::SKINPART_DECORATION);
				if(DrawBody && CTeeRenderInfo::IsDrawableTexture(DecorationTexture))
				{
					Graphics()->TextureSet(DecorationTexture);
					Graphics()->QuadsBegin();
					Graphics()->QuadsSetRotation(pAnim->GetBody()->m_Angle * pi * 2 + BodyAngle);
					const ColorRGBA DecorationColor = pInfo->m_aSixup[g_Config.m_ClDummy].m_aColors[protocol7::SKINPART_DECORATION];
					Graphics()->SetColor(OutLine ? TeeOutlineRenderColor(pInfo, DecorationColor, Alpha) : DecorationColor.WithAlpha(Alpha));
					Graphics()->SelectSprite7(OutLine ? client_data7::SPRITE_TEE_DECORATION_OUTLINE : client_data7::SPRITE_TEE_DECORATION);
					Item = BodyItem;
					Graphics()->QuadsDraw(&Item, 1);
					Graphics()->QuadsEnd();
				}

				// draw body (behind marking)
				const IGraphics::CTextureHandle &BodyTexture = pInfo->m_aSixup[g_Config.m_ClDummy].PartTexture(protocol7::SKINPART_BODY);
				if(DrawBody)
				{
					Graphics()->TextureSet(BodyTexture);
					Graphics()->QuadsBegin();
					Graphics()->QuadsSetRotation(pAnim->GetBody()->m_Angle * pi * 2 + BodyAngle);
					if(OutLine)
					{
						Graphics()->SetColor(TeeOutlineRenderColor(pInfo, ColorRGBA(1.0f, 1.0f, 1.0f), Alpha));
						Graphics()->SelectSprite7(client_data7::SPRITE_TEE_BODY_OUTLINE);
					}
					else
					{
						Graphics()->SetColor(pInfo->m_aSixup[g_Config.m_ClDummy].m_aColors[protocol7::SKINPART_BODY].WithAlpha(Alpha));
						Graphics()->SelectSprite7(client_data7::SPRITE_TEE_BODY);
					}
					Item = BodyItem;
					Graphics()->QuadsDraw(&Item, 1);
					Graphics()->QuadsEnd();
				}

				// draw marking
				const IGraphics::CTextureHandle &MarkingTexture = pInfo->m_aSixup[g_Config.m_ClDummy].PartTexture(protocol7::SKINPART_MARKING);
				if(DrawBody && CTeeRenderInfo::IsDrawableTexture(MarkingTexture) && !OutLine)
				{
					Graphics()->TextureSet(MarkingTexture);
					Graphics()->QuadsBegin();
					Graphics()->QuadsSetRotation(pAnim->GetBody()->m_Angle * pi * 2 + BodyAngle);
					ColorRGBA MarkingColor = pInfo->m_aSixup[g_Config.m_ClDummy].m_aColors[protocol7::SKINPART_MARKING];
					Graphics()->SetColor(MarkingColor.r * MarkingColor.a, MarkingColor.g * MarkingColor.a, MarkingColor.b * MarkingColor.a, MarkingColor.a * Alpha);
					Graphics()->SelectSprite7(client_data7::SPRITE_TEE_MARKING);
					Item = BodyItem;
					Graphics()->QuadsDraw(&Item, 1);
					Graphics()->QuadsEnd();
				}

				// draw body (in front of marking)
				if(DrawBody && !OutLine)
				{
					Graphics()->TextureSet(BodyTexture);
					Graphics()->QuadsBegin();
					Graphics()->QuadsSetRotation(pAnim->GetBody()->m_Angle * pi * 2 + BodyAngle);
					Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
					for(int t = 0; t < 2; t++)
					{
						Graphics()->SelectSprite7(t == 0 ? client_data7::SPRITE_TEE_BODY_SHADOW : client_data7::SPRITE_TEE_BODY_UPPER_OUTLINE);
						Item = BodyItem;
						Graphics()->QuadsDraw(&Item, 1);
					}
					Graphics()->QuadsEnd();
				}

				// draw eyes
				const IGraphics::CTextureHandle &EyesTexture = pInfo->m_aSixup[g_Config.m_ClDummy].PartTexture(protocol7::SKINPART_EYES);
				if(DrawEyes && CTeeRenderInfo::IsDrawableTexture(EyesTexture))
				{
					Graphics()->TextureSet(EyesTexture);
					Graphics()->QuadsBegin();
					Graphics()->QuadsSetRotation(pAnim->GetBody()->m_Angle * pi * 2 + BodyAngle);
					if(IsBot)
					{
						Graphics()->SetColor(pInfo->m_aSixup[g_Config.m_ClDummy].m_BotColor.WithAlpha(Alpha));
						Emote = EMOTE_SURPRISE;
					}
					else
					{
						Graphics()->SetColor(pInfo->m_aSixup[g_Config.m_ClDummy].m_aColors[protocol7::SKINPART_EYES].WithAlpha(Alpha));
					}
					switch(Emote)
					{
					case EMOTE_PAIN:
						Graphics()->SelectSprite7(client_data7::SPRITE_TEE_EYES_PAIN);
						break;
					case EMOTE_HAPPY:
						Graphics()->SelectSprite7(client_data7::SPRITE_TEE_EYES_HAPPY);
						break;
					case EMOTE_SURPRISE:
						Graphics()->SelectSprite7(client_data7::SPRITE_TEE_EYES_SURPRISE);
						break;
					case EMOTE_ANGRY:
						Graphics()->SelectSprite7(client_data7::SPRITE_TEE_EYES_ANGRY);
						break;
					default:
						Graphics()->SelectSprite7(client_data7::SPRITE_TEE_EYES_NORMAL);
						break;
					}

					float EyeScale = BaseSize * 0.60f;
					float h = Emote == EMOTE_BLINK ? BaseSize * 0.15f / 2.0f : EyeScale / 2.0f;
					vec2 Offset = vec2(Direction.x * 0.125f, -0.05f + Direction.y * 0.10f) * BaseSize;
					IGraphics::CQuadItem QuadItem(BodyPos.x + Offset.x, BodyPos.y + Offset.y, EyeScale, h);
					Graphics()->QuadsDraw(&QuadItem, 1);
					Graphics()->QuadsEnd();
				}

				// draw xmas hat
				if(DrawBody && !OutLine && CTeeRenderInfo::IsDrawableTexture(pInfo->m_aSixup[g_Config.m_ClDummy].m_HatTexture))
				{
					Graphics()->TextureSet(pInfo->m_aSixup[g_Config.m_ClDummy].m_HatTexture);
					Graphics()->QuadsBegin();
					Graphics()->QuadsSetRotation(pAnim->GetBody()->m_Angle * pi * 2 + BodyAngle);
					Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
					int Flag = Direction.x < 0.0f ? IGraphics::SPRITE_FLAG_FLIP_X : 0;
					switch(pInfo->m_aSixup[g_Config.m_ClDummy].m_HatSpriteIndex)
					{
					case 0:
						Graphics()->SelectSprite7(client_data7::SPRITE_TEE_HATS_TOP1, Flag);
						break;
					case 1:
						Graphics()->SelectSprite7(client_data7::SPRITE_TEE_HATS_TOP2, Flag);
						break;
					case 2:
						Graphics()->SelectSprite7(client_data7::SPRITE_TEE_HATS_SIDE1, Flag);
						break;
					case 3:
						Graphics()->SelectSprite7(client_data7::SPRITE_TEE_HATS_SIDE2, Flag);
					}
					Item = BodyItem;
					Graphics()->QuadsDraw(&Item, 1);
					Graphics()->QuadsEnd();
				}
			}

			// draw feet
			const int FootLayer = Filling ? TEE_PREVIEW_LAYER_FRONT_FEET : TEE_PREVIEW_LAYER_BACK_FEET;
			const int FootOutlineLayer = Filling ? TEE_PREVIEW_LAYER_FRONT_FEET_OUTLINE : TEE_PREVIEW_LAYER_BACK_FEET_OUTLINE;
			if((OutLine && !HasTeePreviewLayer(pInfo->m_TeeRenderFlags, FootOutlineLayer)) ||
				(!OutLine && !HasTeePreviewLayer(pInfo->m_TeeRenderFlags, FootLayer)))
				continue;
			const IGraphics::CTextureHandle &FeetTexture = pInfo->m_aSixup[g_Config.m_ClDummy].PartTexture(protocol7::SKINPART_FEET);
			if(!CTeeRenderInfo::IsDrawableTexture(FeetTexture))
				continue;
			Graphics()->TextureSet(FeetTexture);
			Graphics()->QuadsBegin();
			const CAnimKeyframe *pFoot = Filling ? pAnim->GetFrontFoot() : pAnim->GetBackFoot();

			float w = (BaseSize / 2.1f) * FeetScale.x;
			float h = (BaseSize / 2.1f) * FeetScale.y;

			Graphics()->QuadsSetRotation(pFoot->m_Angle * pi * 2 + FeetAngle);

			if(OutLine)
			{
				Graphics()->SetColor(TeeOutlineRenderColor(pInfo, ColorRGBA(1.0f, 1.0f, 1.0f), Alpha));
				Graphics()->SelectSprite7(client_data7::SPRITE_TEE_FOOT_OUTLINE);
			}
			else
			{
				bool Indicate = !pInfo->m_GotAirJump && g_Config.m_ClAirjumpindicator;
				float ColorScale = 1.0f;
				if(Indicate)
					ColorScale = 0.5f;
				Graphics()->SetColor(
					pInfo->m_aSixup[g_Config.m_ClDummy].m_aColors[protocol7::SKINPART_FEET].r * ColorScale,
					pInfo->m_aSixup[g_Config.m_ClDummy].m_aColors[protocol7::SKINPART_FEET].g * ColorScale,
					pInfo->m_aSixup[g_Config.m_ClDummy].m_aColors[protocol7::SKINPART_FEET].b * ColorScale,
					pInfo->m_aSixup[g_Config.m_ClDummy].m_aColors[protocol7::SKINPART_FEET].a * Alpha);
				Graphics()->SelectSprite7(client_data7::SPRITE_TEE_FOOT);
			}

			IGraphics::CQuadItem QuadItem(Position.x + pFoot->m_X * AnimScale, Position.y + pFoot->m_Y * AnimScale, w, h);
			Graphics()->QuadsDraw(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}
	}
}

void CRenderTools::RenderTee6(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, float Alpha, vec2 BodyScale, vec2 FeetScale, float BodyAngle, float FeetAngle) const
{
	vec2 Direction = Dir;
	vec2 Position = Pos;

	const float TinyBodyScale = 0.7f;
	const float TinyFeetScale = 0.85f;
	float SizeMultiplier = (g_Config.m_TcTinyTeeSize / 100.0f);
	bool TinyTee = g_Config.m_TcTinyTees;
	if(!m_LocalTeeRender && !g_Config.m_TcTinyTeesOthers)
		TinyTee = false;

	const CSkin::CSkinTextures *pSkinTextures = pInfo->m_CustomColoredSkin ? &pInfo->m_ColorableRenderSkin : &pInfo->m_OriginalRenderSkin;

	// TClient：白脚皮肤只由配置与皮肤系统状态决定，与 Pass/Filling 无关。
	// 提到循环外解析，避免每个 Tee 每帧重复做 4 次皮肤名查找与 usage 记录。
	const CSkin *pWhiteFeetSkin = nullptr;
	if(g_Config.m_TcWhiteFeet && pInfo->m_CustomColoredSkin)
		pWhiteFeetSkin = GameClient()->m_Skins.FindOrNullptr(g_Config.m_TcWhiteFeetSkin);

	// first pass we draw the outline
	// second pass we draw the filling
	for(int Pass = 0; Pass < 2; Pass++)
	{
		int OutLine = Pass == 0 ? 1 : 0;
		if(OutLine && !HasTeePreviewLayer(pInfo->m_TeeRenderFlags, TEE_PREVIEW_LAYER_OUTLINE))
			continue;

		for(int Filling = 0; Filling < 2; Filling++)
		{
			float AnimScale, BaseSize;
			GetRenderTeeAnimScaleAndBaseSize(pInfo, AnimScale, BaseSize);

			if(TinyTee)
			{
				BaseSize *= TinyBodyScale * SizeMultiplier;
				AnimScale *= TinyBodyScale * SizeMultiplier;
			}

			if(Filling == 1)
			{
				vec2 BodyPos = Position + vec2(pAnim->GetBody()->m_X, pAnim->GetBody()->m_Y) * AnimScale;
				float RenderBodyScale;
				GetRenderTeeBodyScale(BaseSize, RenderBodyScale);
				if(HasTeePreviewLayer(pInfo->m_TeeRenderFlags, OutLine ? TEE_PREVIEW_LAYER_BODY_OUTLINE : TEE_PREVIEW_LAYER_BODY))
				{
					const IGraphics::CTextureHandle &BodyTexture = OutLine == 1 ? pSkinTextures->m_BodyOutline : pSkinTextures->m_Body;
					if(CTeeRenderInfo::IsDrawableTexture(BodyTexture))
					{
						Graphics()->QuadsSetRotation(pAnim->GetBody()->m_Angle * pi * 2 + BodyAngle);
						Graphics()->SetColor(OutLine ? TeeOutlineRenderColor(pInfo, pInfo->m_ColorBody, Alpha) : pInfo->m_ColorBody.WithAlpha(Alpha));
						Graphics()->TextureSet(BodyTexture);
						Graphics()->RenderQuadContainerAsSprite(m_TeeQuadContainerIndex, OutLine, BodyPos.x, BodyPos.y, RenderBodyScale * BodyScale.x, RenderBodyScale * BodyScale.y);
					}
				}

				// draw eyes
				if(Pass == 1 && HasTeePreviewLayer(pInfo->m_TeeRenderFlags, TEE_PREVIEW_LAYER_EYES))
				{
					int QuadOffset = 2;
					int EyeQuadOffset = 0;
					int TeeEye = 0;

					switch(Emote)
					{
					case EMOTE_PAIN:
						EyeQuadOffset = 0;
						TeeEye = SPRITE_TEE_EYE_PAIN - SPRITE_TEE_EYE_NORMAL;
						break;
					case EMOTE_HAPPY:
						EyeQuadOffset = 1;
						TeeEye = SPRITE_TEE_EYE_HAPPY - SPRITE_TEE_EYE_NORMAL;
						break;
					case EMOTE_SURPRISE:
						EyeQuadOffset = 2;
						TeeEye = SPRITE_TEE_EYE_SURPRISE - SPRITE_TEE_EYE_NORMAL;
						break;
					case EMOTE_ANGRY:
						EyeQuadOffset = 3;
						TeeEye = SPRITE_TEE_EYE_ANGRY - SPRITE_TEE_EYE_NORMAL;
						break;
					default:
						EyeQuadOffset = 4;
						break;
					}

					float EyeScale = BaseSize * 0.40f * BodyScale.x;
					float h = (Emote == EMOTE_BLINK ? BaseSize * 0.15f : BaseSize * 0.40f) * BodyScale.y;
					float EyeSeparation = (0.075f - 0.010f * absolute(Direction.x)) * BaseSize * BodyScale.x;
					vec2 Offset = vec2(Direction.x * 0.125f * BodyScale.x, (-0.05f + Direction.y * 0.10f) * BodyScale.y) * BaseSize;

					const IGraphics::CTextureHandle &EyesTexture = pSkinTextures->m_aEyes[TeeEye];
					if(CTeeRenderInfo::IsDrawableTexture(EyesTexture))
					{
						Graphics()->TextureSet(EyesTexture);
						Graphics()->RenderQuadContainerAsSprite(m_TeeQuadContainerIndex, QuadOffset + EyeQuadOffset, BodyPos.x - EyeSeparation + Offset.x, BodyPos.y + Offset.y, EyeScale / (64.f * 0.4f), h / (64.f * 0.4f));
						Graphics()->RenderQuadContainerAsSprite(m_TeeQuadContainerIndex, QuadOffset + EyeQuadOffset, BodyPos.x + EyeSeparation + Offset.x, BodyPos.y + Offset.y, -EyeScale / (64.f * 0.4f), h / (64.f * 0.4f));
					}
				}
			}

			const int FootLayer = Filling ? TEE_PREVIEW_LAYER_FRONT_FEET : TEE_PREVIEW_LAYER_BACK_FEET;
			const int FootOutlineLayer = Filling ? TEE_PREVIEW_LAYER_FRONT_FEET_OUTLINE : TEE_PREVIEW_LAYER_BACK_FEET_OUTLINE;
			if((OutLine && !HasTeePreviewLayer(pInfo->m_TeeRenderFlags, FootOutlineLayer)) ||
				(!OutLine && !HasTeePreviewLayer(pInfo->m_TeeRenderFlags, FootLayer)))
				continue;

			if(TinyTee)
			{
				BaseSize /= TinyBodyScale * SizeMultiplier;
				AnimScale /= TinyBodyScale * SizeMultiplier;
			}

			// draw feet
			const CAnimKeyframe *pFoot = Filling ? pAnim->GetFrontFoot() : pAnim->GetBackFoot();

			float w = BaseSize * FeetScale.x;
			float h = (BaseSize / 2) * FeetScale.y;

			if(TinyTee)
			{
				w *= TinyFeetScale * SizeMultiplier;
				h *= TinyFeetScale * SizeMultiplier;
			}

			int QuadOffset = 7;
			if(Dir.x < 0 && pInfo->m_FeetFlipped)
			{
				QuadOffset += 2;
			}

			Graphics()->QuadsSetRotation(pFoot->m_Angle * pi * 2 + FeetAngle);

			bool Indicate = !pInfo->m_GotAirJump && g_Config.m_ClAirjumpindicator;
			float ColorScale = 1.0f;

			if(!OutLine)
			{
				++QuadOffset;
				if(Indicate)
					ColorScale = 0.5f;
			}

			Graphics()->SetColor(OutLine ?
						     TeeOutlineRenderColor(pInfo, pInfo->m_ColorFeet, Alpha) :
						     ColorRGBA(pInfo->m_ColorFeet.r * ColorScale, pInfo->m_ColorFeet.g * ColorScale, pInfo->m_ColorFeet.b * ColorScale, Alpha));

			const IGraphics::CTextureHandle *pFeetTexture = OutLine == 1 ? &pSkinTextures->m_FeetOutline : &pSkinTextures->m_Feet;
			if(pWhiteFeetSkin != nullptr)
			{
				const IGraphics::CTextureHandle &WhiteFeetTexture = OutLine == 1 ? pWhiteFeetSkin->m_OriginalSkin.m_FeetOutline : pWhiteFeetSkin->m_OriginalSkin.m_Feet;
				if(CTeeRenderInfo::IsDrawableTexture(WhiteFeetTexture))
					pFeetTexture = &WhiteFeetTexture;
			}
			if(!CTeeRenderInfo::IsDrawableTexture(*pFeetTexture))
				continue;
			Graphics()->TextureSet(*pFeetTexture);

			Graphics()->RenderQuadContainerAsSprite(m_TeeQuadContainerIndex, QuadOffset, Position.x + pFoot->m_X * AnimScale, Position.y + pFoot->m_Y * AnimScale, w / 64.f, h / 32.f);
		}
	}
}
