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
		const auto Texture = g_pData->m_aImages[IMAGE_QM_SEARCH_LOGO].m_Id;
		if(Texture.IsValid())
		{
			pGraphics->TextureSet(Texture);
			pGraphics->QuadsBegin();
			pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			// 原图不改动，仅取透明留白内的有效区域，避免标题偏离视觉中心。
			pGraphics->QuadsSetSubset(204.0f / 2048.0f, 161.0f / 800.0f, 1709.0f / 2048.0f, 658.0f / 800.0f);
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
	Props.m_DisallowNewline = true;
	Props.m_EllipsisAtEnd = true;
	Props.m_MinimumFontSize = 10.0f * UiScale;
	const ColorRGBA PreviousColor = pTextRender->GetTextColor();
	pTextRender->TextColor(ui_token::color::TEXT_SECONDARY);
	pUi->DoLabel(&Subtitle, pText, 14.0f * UiScale, TEXTALIGN_MC, Props);
	pTextRender->TextColor(PreviousColor);
}
