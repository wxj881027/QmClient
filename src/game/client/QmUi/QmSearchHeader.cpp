#include "QmSearchHeader.h"

#include "UiTokens.h"

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/ui.h>

void qm_search_header::Render(CUi *pUi, IGraphics *pGraphics, ITextRender *pTextRender, const SLayout &Layout, const char *pText, float UiScale, float OffsetY)
{
	if(Layout.m_Logo.h <= 0.0f)
		return;
	CUIRect Logo = Layout.m_Logo;
	CUIRect Subtitle = Layout.m_Subtitle;
	Logo.y += OffsetY;
	Subtitle.y += OffsetY;
	if(!pUi->DrawingSuppressed())
	{
		static constexpr int s_aImageIds[] = {IMAGE_QM_SEARCH_LOGO, IMAGE_QM_SEARCH_LOGO_SIGNED, IMAGE_QM_SEARCH_LOGO_SILVER_EAR};
		static_assert(sizeof(s_aImageIds) / sizeof(s_aImageIds[0]) == NUM_LOGOS);
		const int LogoIndex = std::clamp(Layout.m_LogoIndex, 0, NUM_LOGOS - 1);
		const SLogo &Source = s_aLogos[LogoIndex];
		const auto Texture = g_pData->m_aImages[s_aImageIds[LogoIndex]].m_Id;
		if(Texture.IsValid())
		{
			pGraphics->TextureSet(Texture);
			pGraphics->QuadsBegin();
			pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			// 原图不改动，仅取透明留白内的有效区域，避免标题偏离视觉中心。
			pGraphics->QuadsSetSubset(Source.m_Left / 2048.0f, Source.m_Top / 800.0f, Source.m_Right / 2048.0f, Source.m_Bottom / 800.0f);
			IGraphics::CQuadItem Quad(Logo.x, Logo.y, Logo.w, Logo.h);
			pGraphics->QuadsDrawTL(&Quad, 1);
			pGraphics->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
			pGraphics->QuadsEnd();
		}
		else
			pUi->DoLabel(&Logo, "栖梦 QimenG", 32.0f * UiScale, TEXTALIGN_MC);
	}
	SLabelProperties Props;
	Props.m_MaxWidth = Subtitle.w;
	Props.m_EnableWidthCheck = false;
	const ColorRGBA PreviousColor = pTextRender->GetTextColor();
	pTextRender->TextColor(ui_token::color::TEXT_SECONDARY);
	pUi->DoLabel(&Subtitle, pText, 14.0f * UiScale, TEXTALIGN_MC, Props);
	pTextRender->TextColor(PreviousColor);
}
