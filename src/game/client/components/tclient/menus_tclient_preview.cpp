#include "menus_tclient_internal.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/components/menus.h>
#include <game/client/components/skins.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/skin.h>

#include <cmath>
#include <cstdlib>
#include <new>
#include <stdexcept>

namespace
{
	float s_Time = 0.0f;
	bool s_StartedTime = false;
}

void qm_tclient_menu::AdvancePreviewTime(float FrameTime)
{
	s_Time += FrameTime * (1.0f / 100.0f);
	if(!s_StartedTime)
	{
		s_StartedTime = true;
		s_Time = (float)rand() / (float)RAND_MAX;
	}
}

void CMenus::RenderDevSkin(vec2 RenderPos, float Size, const char *pSkinName, const char *pBackupSkin, bool CustomColors, int FeetColor, int BodyColor, int Emote, bool Rainbow, bool Cute, ColorRGBA ColorFeet, ColorRGBA ColorBody, const QmSponsorAuthors::STeePresentation *pGlitch)
{
	const CSkin *pSkin;
	try
	{
		// 皮肤系统会解析前缀和大小写。主皮肤尚不可用时才查询备用皮肤。
		CSkins &Skins = GameClient()->m_Skins;
		pSkin = Skins.FindOrNullptr(pSkinName);
		if(pSkin == nullptr)
			pSkin = Skins.Find(pBackupSkin);
	}
	catch(const std::bad_alloc &)
	{
		return;
	}
	catch(const std::length_error &)
	{
		return;
	}

	float DefTick = std::fmod(s_Time, 1.0f);

	CTeeRenderInfo SkinInfo;

	SkinInfo.m_OriginalRenderSkin = pSkin->m_OriginalSkin;
	SkinInfo.m_ColorableRenderSkin = pSkin->m_ColorableSkin;
	SkinInfo.m_SkinMetrics = pSkin->m_Metrics;
	SkinInfo.m_CustomColoredSkin = CustomColors;
	if(SkinInfo.m_CustomColoredSkin)
	{
		SkinInfo.m_ColorBody = color_cast<ColorRGBA>(ColorHSLA(BodyColor).UnclampLighting(ColorHSLA::DARKEST_LGT));
		SkinInfo.m_ColorFeet = color_cast<ColorRGBA>(ColorHSLA(FeetColor).UnclampLighting(ColorHSLA::DARKEST_LGT));
		if(ColorFeet.a != 0.0f)
		{
			SkinInfo.m_ColorBody = ColorBody;
			SkinInfo.m_ColorFeet = ColorFeet;
		}
	}
	else
	{
		SkinInfo.m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f);
		SkinInfo.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f);
	}
	if(Rainbow)
	{
		ColorRGBA Col = color_cast<ColorRGBA>(ColorHSLA(DefTick, 1.0f, 0.5f));
		SkinInfo.m_ColorBody = Col;
		SkinInfo.m_ColorFeet = Col;
	}
	SkinInfo.m_Size = Size;
	const CAnimState *pIdleState = CAnimState::GetIdle();
	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &SkinInfo, OffsetToMid);
	vec2 TeeRenderPos(RenderPos.x, RenderPos.y + OffsetToMid.y);
	struct SRestoreWhiteFeet
	{
		int m_Previous;
		~SRestoreWhiteFeet() { g_Config.m_QmWhiteFeet = m_Previous; }
	};
	const SRestoreWhiteFeet RestoreWhiteFeet{g_Config.m_QmWhiteFeet};
	g_Config.m_QmWhiteFeet = 0;
	if(pGlitch != nullptr)
	{
		const CUIRect Bounds{RenderPos.x - Size * 0.75f, RenderPos.y - Size * 0.5f, Size * 1.5f, Size};
		QmAuthorTee::Render(Ui(), RenderTools(), pIdleState, SkinInfo, Emote, Cute, TeeRenderPos, Bounds, *pGlitch);
	}
	else if(Cute)
		RenderTeeCute(pIdleState, &SkinInfo, Emote, vec2(1.0f, 0.0f), TeeRenderPos, true);
	else
		RenderTools()->RenderTee(pIdleState, &SkinInfo, Emote, vec2(1.0f, 0.0f), TeeRenderPos);
}

void CMenus::RenderTeeCute(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, bool CuteEyes, float Alpha)
{
	Dir = Ui()->MousePos() - Pos;
	if(pInfo->m_Size > 0.0f)
		Dir /= pInfo->m_Size;
	const float Length = length(Dir);
	if(Length > 1.0f)
		Dir /= Length;
	if(CuteEyes && Length < 0.4f)
		Emote = 2;
	RenderTools()->RenderTee(pAnim, pInfo, Emote, Dir, Pos, Alpha);
}
