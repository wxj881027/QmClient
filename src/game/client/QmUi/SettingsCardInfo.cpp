// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "SettingsCardInfo.h"

#include "QmAnimResolve.h"
#include "QmCardWiki.h"
#include "QmCardWikiPanel.h"
#include "SettingsCard.h"
#include "UiContext.h"

#include <game/client/components/tooltips.h>
#include <game/client/qm_icon.h>
#include <game/client/ui.h>
#include <game/localization.h>

void RenderSettingsCardInfo(const IUiContext &Ctx, const SSettingsCardSpec &Spec, CUIRect Button, float DrawAlpha)
{
	if(Ctx.m_pUi == nullptr || Ctx.m_pTextRender == nullptr || !HasSettingsCardInfo(Spec) || Button.w <= 0.0f || Button.h <= 0.0f || DrawAlpha <= 0.0f)
		return;
	const uint64_t Key = BuildUiAnimNodeKey(Ctx.m_ScopeHash, str_quickhash(Spec.m_pStableId));
	const void *pId = reinterpret_cast<const void *>(static_cast<uintptr_t>(Key));
	RenderSettingsCardHeaderIcon(Ctx, Button, EQmIcon::INFO, FontIcons::FONT_ICON_INFO, DrawAlpha, pId);
	// 点击直接在这里登记：带「当前命中」标志的按钮逻辑在绘制阶段也能接收新的按下，
	// 而且命中矩形就是刚画出来的图标，不会再出现「点在图标上没反应」。
	if(HasCardWikiCopy(Spec.m_pStableId) &&
		Ctx.m_pUi->DoButtonLogic(pId, 0, &Button, BUTTONFLAG_LEFT | BUTTONFLAG_CURRENT_HIT) != 0)
		OpenCardWikiPopup(Ctx, Spec.m_pStableId, Spec.m_pTitle);
	// 提示刻意只留一句话：完整说明在面板里，长文本挂在卡片上会盖住标题，看起来像闪动。
	if(Ctx.m_pTooltips != nullptr)
	{
		const char *pHint = Spec.m_pInfo;
		if((pHint == nullptr || pHint[0] == '\0') && HasCardWikiCopy(Spec.m_pStableId))
			pHint = Localize("View details");
		if(pHint != nullptr && pHint[0] != '\0')
		{
			Ctx.m_pTooltips->DoInfoToolTipForRect(pId, &Button, pHint,
				ResolveSettingsCardInfoWidth(Ctx.m_pUi->Screen()->w, Ctx.m_UiScale),
				ui_token::font::BODY * std::max(0.1f, Ctx.m_UiScale));
		}
	}
}
