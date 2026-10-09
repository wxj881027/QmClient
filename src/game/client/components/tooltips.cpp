#include "tooltips.h"

#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiConfigHint.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>

CTooltips::CTooltips()
{
	CTooltips::OnReset();
}

void CTooltips::OnReset()
{
	m_Frame = 1;
	m_ConfigHelpInitialized = false;
	m_ConfigHelp.clear();
	m_Tooltips.clear();
	m_ConfigHints.clear();
	ClearActiveTooltip();
}

void CTooltips::SetActiveTooltip(CTooltip &Tooltip)
{
	if(!m_ActiveTooltip || QmTooltipMayReplace(Tooltip, m_ActiveTooltip->get(), m_Frame, m_ActiveTooltip->get().m_OnScreen && QmTooltipHovered(m_ActiveTooltip->get(), *Ui())))
		m_ActiveTooltip.emplace(Tooltip);
}

inline void CTooltips::ClearActiveTooltip()
{
	m_ActiveTooltip.reset();
	m_HoverState.Clear();
}

// TClient
void CTooltips::SetFadeTime(const void *pId, float Time)
{
	uintptr_t Id = reinterpret_cast<uintptr_t>(pId);
	const auto Iter = m_Tooltips.find(Id);
	if(Iter != m_Tooltips.end())
	{
		Iter->second.m_FadeTime = Time;
	}
}

void CTooltips::DoToolTip(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint)
{
	const bool Small = GameClient()->m_Menus.IsSettingsPageActive();
	DoToolTip(pId, pNearRect, pText, WidthHint, Small ? ResolveSettingsSmallFontSize(g_Config.m_QmUiScale / 100.0f) : 14.0f, Small, Small);
}

void CTooltips::DoToolTipForRect(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint)
{
	const bool Small = GameClient()->m_Menus.IsSettingsPageActive();
	DoToolTip(pId, pNearRect, pText, WidthHint, Small ? ResolveSettingsSmallFontSize(g_Config.m_QmUiScale / 100.0f) : 14.0f, Small, true);
}

void CTooltips::DoInfoToolTipForRect(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint, float FontSize)
{
	const bool Small = GameClient()->m_Menus.IsSettingsPageActive();
	DoToolTip(pId, pNearRect, pText, WidthHint, Small ? ResolveSettingsSmallFontSize(g_Config.m_QmUiScale / 100.0f) : FontSize, Small, true, true);
}

void CTooltips::DoSmallToolTip(const void *pId, const CUIRect *pNearRect, const char *pText, float FontSize, float WidthHint)
{
	const bool Small = GameClient()->m_Menus.IsSettingsPageActive();
	DoToolTip(pId, pNearRect, pText, WidthHint, Small ? FontSize : 14.0f, Small, true, true);
}

void CTooltips::DoConfigToolTip(const void *pId, const CUIRect *pNearRect, const void *pValue, const void *pSecondValue)
{
	if(Ui()->RenderOnly() || pId == nullptr || !Ui()->MouseHovered(pNearRect))
		return;
	const bool Small = GameClient()->m_Menus.IsSettingsPageActive();
	if(Small)
	{
		DoSettingsToolTipForConfig(pId, pNearRect, pValue, nullptr, pSecondValue);
		return;
	}
	const char *pCommand = QmUiConfigCommand(g_Config, pValue);
	const char *pSecondCommand = QmUiConfigCommand(g_Config, pSecondValue);
	if(pCommand == nullptr && pSecondCommand == nullptr)
		return;
	m_ConfigHints[reinterpret_cast<uintptr_t>(pId)].SetCommands(pCommand, pSecondCommand);
	DoToolTip(pId, pNearRect, nullptr, -1.0f, 14.0f, false, true, false, true);
}

void CTooltips::DoSettingsToolTipForConfig(const void *pId, const CUIRect *pRect, const void *pConfigValue, const CUIRect *pAnchor, const void *pSecondConfigValue)
{
	if(Ui()->RenderOnly() || pId == nullptr || !GameClient()->m_Menus.IsSettingsPageActive() || !Ui()->MouseHovered(pRect))
		return;
	if(!m_ConfigHelpInitialized)
	{
		if(ConfigManager() == nullptr)
			return;
		// 配置变量由 ConfigManager 持有，其生命周期覆盖客户端组件；只收集一次。
		ConfigManager()->PossibleConfigVariables("", CFGFLAG_CLIENT, [](const SConfigVariable *pVariable, void *pUser) {
			auto &Help = *static_cast<decltype(m_ConfigHelp) *>(pUser);
			const void *pValue = nullptr;
			switch(pVariable->m_Type)
			{
			case SConfigVariable::VAR_INT:
				pValue = static_cast<const SIntConfigVariable *>(pVariable)->m_pVariable;
				break;
			case SConfigVariable::VAR_COLOR:
				pValue = static_cast<const SColorConfigVariable *>(pVariable)->m_pVariable;
				break;
			case SConfigVariable::VAR_STRING:
				pValue = static_cast<const SStringConfigVariable *>(pVariable)->m_pStr;
				break;
			}
			Help.emplace(pValue, pVariable);
		}, &m_ConfigHelp);
		m_ConfigHelpInitialized = true;
	}
	const char *pCommand = QmUiConfigCommand(g_Config, pConfigValue);
	const char *pSecondCommand = QmUiConfigCommand(g_Config, pSecondConfigValue);
	if(pCommand == nullptr && pSecondCommand == nullptr)
		return;
	m_ConfigHints[reinterpret_cast<uintptr_t>(pId)].SetCommands(pCommand, pSecondCommand);
	auto Iter = m_ConfigHelp.find(pConfigValue);
	if(Iter == m_ConfigHelp.end())
		Iter = m_ConfigHelp.find(pSecondConfigValue);
	const char *pKey = nullptr;
	if(Iter != m_ConfigHelp.end())
	{
		const SConfigVariable &Variable = *Iter->second;
		pKey = Variable.m_pHelpLocalizeKey != nullptr ? Variable.m_pHelpLocalizeKey : Variable.m_pHelp;
	}
	const float FontSize = ResolveSettingsSmallFontSize(g_Config.m_QmUiScale / 100.0f);
	DoToolTip(pId, pRect, pKey != nullptr ? Localize(pKey) : nullptr, -1.0f, FontSize, true, true, true, true, pAnchor);
}

void CTooltips::DoToolTip(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint, const float FontSize, const bool SmallInstant, const bool HoverByRect, const bool Immediate, const bool Fallback, const CUIRect *pAnchor)
{
	if(Ui()->RenderOnly() || pNearRect->w <= 0.0f || pNearRect->h <= 0.0f)
		return;
	const uintptr_t Id = reinterpret_cast<uintptr_t>(pId);
	const auto [Entry, WasInserted] = m_Tooltips.try_emplace(Id);
	CTooltip &Tooltip = Entry->second;
	if(!WasInserted && !QmTooltipMayUpdate(Tooltip, m_Frame, Fallback, Tooltip.m_OnScreen && QmTooltipHovered(Tooltip, *Ui()), pAnchor != nullptr))
		return;
	auto Hint = m_ConfigHints.find(Id);
	if(Hint == m_ConfigHints.end())
	{
		if(const char *pCommand = QmUiConfigCommand(g_Config, pId); pCommand != nullptr)
		{
			Hint = m_ConfigHints.try_emplace(Id).first;
			Hint->second.SetCommands(pCommand);
		}
	}
	const bool HasConfigHint = Hint != m_ConfigHints.end();
	if(HasConfigHint)
	{
		// 专属说明只对当前帧有效，选项条件或页面改变后不沿用上一帧的文案。
		if(!QmTooltipRegistered(Tooltip, m_Frame))
			Hint->second.SetDescription("");
		if(Fallback)
			Hint->second.SetFallbackDescription(pText);
		else
			Hint->second.SetDescription(pText);
		pText = Hint->second.Text();
	}
	if(pText == nullptr || pText[0] == '\0')
		return;

	Tooltip.m_pId = pId;
	Tooltip.m_Rect = *pNearRect;
	Tooltip.m_Anchor = pAnchor != nullptr ? *pAnchor : *pNearRect;
	Tooltip.m_HasTextAnchor = pAnchor != nullptr;
	if(Tooltip.m_Text != pText)
		Tooltip.m_Text = pText;
	Tooltip.m_FontSize = std::max(1.0f, FontSize);
	Tooltip.m_WidthHint = WidthHint;
	Tooltip.m_HoverByRect = HoverByRect || HasConfigHint;
	Tooltip.m_Immediate = Immediate;
	Tooltip.m_SmallInstant = SmallInstant;
	Tooltip.m_Fallback = Fallback;
	Tooltip.m_RegisteredFrame = m_Frame;
	Tooltip.m_OnScreen = QmTooltipHovered(Tooltip, *Ui());

	if(Tooltip.m_OnScreen)
		SetActiveTooltip(Tooltip);
}

void CTooltips::OnRender()
{
	const uint64_t Frame = m_Frame++;
	if(!m_ActiveTooltip || !QmTooltipActive(m_ActiveTooltip->get(), Frame, *Ui()))
	{
		ClearActiveTooltip();
		return;
	}
	if(m_ActiveTooltip.has_value())
	{
		CTooltip &Tooltip = m_ActiveTooltip.value();

		// 只处理本帧最终命中的目标，多个重叠说明不会反复重置悬浮状态。
		const float VisibleSeconds = m_HoverState.Update(Tooltip, time_get() / static_cast<double>(time_freq()));
		if(VisibleSeconds < 0.0f)
			return;
		const bool Animate = QmTooltipAnimate(Tooltip, g_Config.m_QmTooltipAnimation && g_Config.m_QmUiMotionLevel > 0);
		const float SecondsFadeIn = !Animate || Tooltip.m_Immediate ? 0.0f : CQmTooltipHoverState::FADE_IN_SECONDS;
		const float AlphaFactor = SecondsFadeIn > 0.0f ? std::min(VisibleSeconds / SecondsFadeIn, 1.0f) : 1.0f;
		CUiScopedGaussianBlur GaussianBlurScope(Ui(), AlphaFactor);
		CQmTooltipTextScope TextScope(*TextRender());

		const float BaseFontSize = Tooltip.m_FontSize * (Tooltip.m_SmallInstant ? 1.0f : std::clamp(g_Config.m_QmTooltipFontSize, 10, 24) / 14.0f);
		const float UiScale = Tooltip.m_SmallInstant ? BaseFontSize / 10.0f : 1.0f;
		const float Margin = (Tooltip.m_SmallInstant ? 4.0f : 5.0f) * UiScale;
		const float BasePadding = (Tooltip.m_SmallInstant ? 3.0f : 5.0f) * UiScale;
		const CUIRect *pScreen = Ui()->Screen();
		const float MaxTextWidth = maximum(1.0f, pScreen->w - 2.0f * (Margin + BasePadding));
		const float WidthLimit = Tooltip.m_WidthHint > 0.0f ? Tooltip.m_WidthHint : 300.0f * UiScale;
		const float TextWidth = minimum(WidthLimit, MaxTextWidth);
		const STextBoundingBox BoundingBox = TextRender()->TextBoundingBox(BaseFontSize, Tooltip.m_Text.c_str(), -1, TextWidth);
		const CUIRect FixedRect = QmTooltipRect(Tooltip.m_Anchor, *pScreen, vec2(BoundingBox.m_W + 2 * BasePadding, BoundingBox.m_H + 2 * BasePadding), Margin);
		if(FixedRect.w <= 0.0f || FixedRect.h <= 0.0f)
			return;
		CUIRect Rect = QmTooltipAnimatedRect(FixedRect, *pScreen, QmTooltipScale(VisibleSeconds, Animate));
		const float Scale = FixedRect.w > 0.0f ? Rect.w / FixedRect.w : 1.0f;
		const float FontSize = BaseFontSize * Scale;
		const float Padding = std::min(BasePadding * Scale, std::min(Rect.w, Rect.h) * 0.5f);
		ColorRGBA Background = Tooltip.m_SmallInstant ? ColorRGBA(0.08f, 0.08f, 0.08f, 0.94f) : color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTooltipBackgroundColor, true));
		Background.a *= AlphaFactor;
		Rect.Draw(Background, IGraphics::CORNER_ALL, Tooltip.m_SmallInstant ? 3.0f * UiScale : Padding);
		Rect.Margin(Padding, &Rect);

		// 极窄视口或超长说明按可见行数收口，保留省略提示，避免文字溢出气泡。
		const int VisibleLines = QmTooltipVisibleLines(Rect.h, FontSize);
		const bool Truncated = QmTooltipTextTruncated(BoundingBox.m_H, BasePadding, FixedRect.h);
		CTextCursor Cursor = QmTooltipTextCursor(Rect, FontSize, TextWidth * Scale, Truncated ? std::max(1, VisibleLines - 1) : 0);

		STextContainerIndex TextContainerIndex;
		TextRender()->CreateTextContainer(TextContainerIndex, &Cursor, Tooltip.m_Text.c_str());

		if(TextContainerIndex.Valid())
		{
			ColorRGBA TextColor = Tooltip.m_SmallInstant ? ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f) : color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTooltipTextColor, true));
			TextColor.a *= AlphaFactor;
			ColorRGBA OutlineColor = TextRender()->DefaultTextOutlineColor();
			OutlineColor.a *= AlphaFactor;
			Ui()->ClipEnable(&Rect);
			if(!Truncated || VisibleLines > 1)
				TextRender()->RenderTextContainer(TextContainerIndex, TextColor, OutlineColor);
			if(Truncated)
			{
				CUIRect End = Rect;
				End.y += std::max(0, VisibleLines - 1) * FontSize;
				End.h = FontSize;
				const ColorRGBA OldColor = TextRender()->GetTextColor();
				TextRender()->TextColor(TextColor);
				Ui()->DoLabel(&End, "…", FontSize, TEXTALIGN_TL);
				TextRender()->TextColor(OldColor);
			}
			Ui()->ClipDisable();
		}

		TextRender()->DeleteTextContainer(TextContainerIndex);

		Tooltip.m_OnScreen = false;
	}
}
