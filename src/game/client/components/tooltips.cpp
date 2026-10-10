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
	m_CardLabelTexts.Clear();
	m_CardLabelHint = {};
	m_Context.reset();
	ClearActiveTooltip();
}

void CTooltips::SetActiveTooltip(CTooltip &Tooltip)
{
	if(!m_ActiveTooltip || QmTooltipMayReplace(Tooltip, m_ActiveTooltip->get(), m_Frame, m_ActiveTooltip->get().m_OnScreen && QmTooltipHovered(m_ActiveTooltip->get(), *Ui())))
		m_ActiveTooltip.emplace(Tooltip);
}

void CTooltips::ResetPresentation()
{
	m_HoverState.Clear();
	m_MotionState.Clear();
	m_DisplayTooltip = {};
	if(m_TextCache.Index().m_Index >= 0)
		m_TextCache.Clear(*TextRender());
}

void CTooltips::ClearActiveTooltip()
{
	m_ActiveTooltip.reset();
	m_CardLabelHint.m_OnScreen = false;
	ResetPresentation();
}

const char *CTooltips::PrepareCardLabel(const CUIRect *pRect, const char *pText, bool Render)
{
	if(pText == nullptr || (str_find(pText, "(") == nullptr && str_find(pText, "（") == nullptr))
		return pText;
	const SQmCardLabelText &Text = m_CardLabelTexts.Get(pText);
	if(Render && pRect != nullptr && Ui()->MouseHovered(pRect) && !Text.m_Hint.empty())
	{
		m_CardLabelHint.m_pId = &Text;
		m_CardLabelHint.m_Rect = m_CardLabelHint.m_Anchor = *pRect;
		m_CardLabelHint.m_Text = Text.m_Hint;
		m_CardLabelHint.m_SmallInstant = true;
		m_CardLabelHint.m_HoverByRect = true;
		m_CardLabelHint.m_Immediate = true;
		m_CardLabelHint.m_OnScreen = true;
		m_CardLabelHint.m_FontSize = ResolveSettingsSmallFontSize(g_Config.m_QmUiScale / 100.0f);
		m_CardLabelHint.m_RegisteredFrame = m_Frame;
	}
	return Text.m_Label.c_str();
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
	const double Now = time_get() / static_cast<double>(time_freq());
	const auto Context = GameClient()->m_Menus.TooltipContext();
	if(!m_Context || *m_Context != Context)
	{
		ResetPresentation();
		m_Context = Context;
	}
	if(!Ui()->Enabled() || Ui()->PointerInputBlocked())
	{
		ClearActiveTooltip();
		return;
	}

	const bool LabelHintActive = QmTooltipActive(m_CardLabelHint, Frame, *Ui());
	const CTooltip *pTarget = m_ActiveTooltip && QmTooltipActive(m_ActiveTooltip->get(), Frame, *Ui()) ? &m_ActiveTooltip->get() : nullptr;
	if(pTarget == nullptr && LabelHintActive)
		pTarget = &m_CardLabelHint;
	if(pTarget != nullptr)
	{
		// 只处理本帧最终目标，保留专属帮助和配置命令，再补充当前标签的括号说明。
		if(m_HoverState.Update(*pTarget, Now) < 0.0f)
			return;
		m_DisplayTooltip = *pTarget;
		if(LabelHintActive && m_DisplayTooltip.m_Text.find(m_CardLabelHint.m_Text) == std::string::npos)
		{
			m_DisplayTooltip.m_Text += '\n';
			m_DisplayTooltip.m_Text += m_CardLabelHint.m_Text;
		}
	}
	else
	{
		m_ActiveTooltip.reset();
		if(!m_HoverState.Retain(Now))
		{
			ResetPresentation();
			return;
		}
	}

	const CTooltip &Tooltip = m_DisplayTooltip;
	const float VisibleSeconds = m_HoverState.VisibleSeconds(Now);
	const bool MotionEnabled = g_Config.m_QmTooltipAnimation && g_Config.m_QmUiMotionLevel > 0;
	const bool Animate = QmTooltipAnimate(Tooltip, MotionEnabled);
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
	const CUIRect MovingRect = m_MotionState.Update(FixedRect, Now, MotionEnabled);
	CUIRect Rect = QmTooltipAnimatedRect(MovingRect, *pScreen, QmTooltipScale(VisibleSeconds, Animate));
	const float Scale = MovingRect.w > 0.0f ? Rect.w / MovingRect.w : 1.0f;
	const float FontSize = BaseFontSize * Scale;
	const float Padding = std::min(BasePadding * Scale, std::min(Rect.w, Rect.h) * 0.5f);
	ColorRGBA Background = Tooltip.m_SmallInstant ? ColorRGBA(0.08f, 0.08f, 0.08f, 0.94f) : color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTooltipBackgroundColor, true));
	Background.a *= AlphaFactor;
	Rect.Draw(Background, IGraphics::CORNER_ALL, Tooltip.m_SmallInstant ? 3.0f * UiScale : Padding);
	const CUIRect ClipRect = Rect;
	Rect.Margin(Padding, &Rect);

	const bool Truncated = QmTooltipTextTruncated(BoundingBox.m_H, BasePadding, FixedRect.h);
	const int VisibleLines = QmTooltipVisibleLines(FixedRect.h - 2 * BasePadding, BaseFontSize);
	const CTextCursor Cursor = QmTooltipTextCursor(Rect, FontSize, TextWidth * Scale, Truncated ? std::max(1, VisibleLines - 1) : 0);
	m_TextCache.Update(*TextRender(), Cursor, Tooltip.m_Text);
	if(m_TextCache.Index().Valid())
	{
		ColorRGBA TextColor = Tooltip.m_SmallInstant ? ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f) : color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTooltipTextColor, true));
		TextColor.a *= AlphaFactor;
		ColorRGBA OutlineColor = TextRender()->DefaultTextOutlineColor();
		OutlineColor.a *= AlphaFactor;
		// 位移使用绘制偏移；下伸字形和描边仍可使用气泡内边距。
		Ui()->ClipEnable(&ClipRect);
		if(!Truncated || VisibleLines > 1)
			TextRender()->RenderTextContainer(m_TextCache.Index(), TextColor, OutlineColor, Rect.x, Rect.y);
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
}
