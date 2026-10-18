// 卡片内副标题共享绘制入口；悬浮只改变文字可见性，始终消费同一份卡片几何。
#ifndef GAME_CLIENT_QMUI_SETTINGSCARDSUBTITLE_H
#define GAME_CLIENT_QMUI_SETTINGSCARDSUBTITLE_H

#include "QmAnimResolve.h"
#include "SettingsCardGeometry.h"
#include "SettingsPageLayout.h"
#include "UiContext.h"
#include "UiTokens.h"

#include <game/client/ui.h>

inline void RenderSettingsCardSubtitle(const IUiContext &Ctx, const SSettingsCardFrame &Frame, const SSettingsCardSpec &Spec, const bool PointerInside)
{
	if(Ctx.m_pUi == nullptr || Ctx.m_pUi->MeasurementOnly() || Spec.m_pSubtitle == nullptr || Spec.m_pSubtitle[0] == '\0' || Frame.m_SubtitleRect.w <= 0.0f || Frame.m_SubtitleRect.h <= 0.0f)
		return;
	float Alpha = PointerInside ? 1.0f : 0.0f;
	if(Ctx.m_pAnim != nullptr && !Ctx.m_pUi->RenderOnly())
	{
		const uint64_t SubtitleKey = BuildUiAnimNodeKey(
			Ctx.m_ScopeHash ^ str_quickhash("settings-card-subtitle"),
			Spec.m_pStableId != nullptr && Spec.m_pStableId[0] != '\0' ? str_quickhash(Spec.m_pStableId) : reinterpret_cast<uint64_t>(Spec.m_pSubtitle));
		Alpha = ResolveUiAnimValue(*Ctx.m_pAnim, SubtitleKey, EUiAnimProperty::ALPHA, Alpha, ui_token::motion::HOVER_FADE.m_DurationSec, ui_token::motion::HOVER_FADE.m_Easing);
	}
	if(Alpha <= 0.01f)
		return;
	SLabelProperties Props;
	Props.m_MaxWidth = Frame.m_SubtitleRect.w;
	Props.m_EllipsisAtEnd = true;
	const float Scale = Ctx.m_UiScale > 0.0f ? Ctx.m_UiScale : 1.0f;
	CUiScopedControlAlphaScale SubtitleFade(Ctx.m_pUi, Alpha);
	Ctx.m_pUi->DoLabel(&Frame.m_SubtitleRect, Spec.m_pSubtitle, ResolveSettingsSmallFontSize(Scale), TEXTALIGN_ML, Props);
}

#endif
