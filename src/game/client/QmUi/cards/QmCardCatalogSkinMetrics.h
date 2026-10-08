#ifndef GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGSKINMETRICS_H
#define GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGSKINMETRICS_H

#include <game/client/QmUi/SettingsPageLayout.h>

struct SSettingsSkinAppearanceLayout
{
	CUIRect m_Outline;
	CUIRect m_Hue;
	CUIRect m_Shadow;
	float m_Height = 0.0f;
};

inline SSettingsSkinAppearanceLayout ResolveSettingsSkinAppearanceLayout(const CUIRect &View, const SSettingsContentMetrics &Metrics)
{
	SSettingsSkinAppearanceLayout Layout;
	const float OutlineHeight = 5.0f * Metrics.m_RowStep;
	const float HueHeight = 3.0f * Metrics.m_RowStep;
	float Bottom;
	if(View.w >= 640.0f * Metrics.m_UiScale)
	{
		View.VSplitMid(&Layout.m_Outline, &Layout.m_Hue, Metrics.m_SectionGap);
		Layout.m_Outline.h = OutlineHeight;
		Layout.m_Hue.h = HueHeight;
		Bottom = View.y + OutlineHeight;
	}
	else
	{
		Layout.m_Outline = {View.x, View.y, View.w, OutlineHeight};
		Layout.m_Hue = {View.x, View.y + OutlineHeight + Metrics.m_SectionGap, View.w, HueHeight};
		Bottom = Layout.m_Hue.y + HueHeight;
	}
	Layout.m_Shadow = {View.x, Bottom + Metrics.m_SectionGap, View.w, 2.0f * Metrics.m_RowStep};
	Layout.m_Height = Layout.m_Shadow.y + Layout.m_Shadow.h - View.y;
	return Layout;
}

inline float ResolveQmVisualSkinAppearanceHeight(const SSettingsContentMetrics &Metrics, float Width = 0.0f)
{
	return ResolveSettingsSkinAppearanceLayout({0.0f, 0.0f, Width, 0.0f}, Metrics).m_Height;
}

inline float ResolveQmVisualSkinTransitionHeight(const SSettingsContentMetrics &Metrics, const bool Enabled)
{
	// 偷皮和动画开关始终可见，五行高级参数仅在开启动画时显示。
	return (2.0f + (Enabled ? 5.0f : 0.0f)) * Metrics.m_RowStep;
}

#endif // GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGSKINMETRICS_H
