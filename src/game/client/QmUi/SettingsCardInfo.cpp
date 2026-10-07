#include "SettingsCardInfo.h"

#include "QmAnimResolve.h"
#include "SettingsCard.h"
#include "UiContext.h"

#include <game/client/components/tooltips.h>
#include <game/client/qm_icon.h>
#include <game/client/ui.h>

void RenderSettingsCardInfo(const IUiContext &Ctx, const SSettingsCardSpec &Spec, CUIRect Button, float DrawAlpha)
{
	if(Ctx.m_pUi == nullptr || Ctx.m_pTextRender == nullptr || Spec.m_pInfo == nullptr || Spec.m_pInfo[0] == '\0' || Spec.m_pStableId == nullptr || Spec.m_pStableId[0] == '\0' || Button.w <= 0.0f || Button.h <= 0.0f || DrawAlpha <= 0.0f)
		return;
	RenderSettingsCardHeaderIcon(Ctx, Button, EQmIcon::INFO, FontIcons::FONT_ICON_INFO, DrawAlpha);
	// 只按可见区域悬浮，不注册点击或弹层；身份仅用于提示缓存，文字由提示组件持有。
	if(Ctx.m_pTooltips != nullptr)
	{
		const uint64_t Key = BuildUiAnimNodeKey(Ctx.m_ScopeHash, str_quickhash(Spec.m_pStableId));
		const void *pId = reinterpret_cast<const void *>(static_cast<uintptr_t>(Key));
		Ctx.m_pTooltips->DoInfoToolTipForRect(pId, &Button, Spec.m_pInfo,
			ResolveSettingsCardInfoWidth(Ctx.m_pUi->Screen()->w, Ctx.m_UiScale),
			ui_token::font::BODY * std::max(0.1f, Ctx.m_UiScale));
	}
}
