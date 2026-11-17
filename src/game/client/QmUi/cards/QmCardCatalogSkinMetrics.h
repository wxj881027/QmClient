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
	// 常用开关由外层声明；这里只测量展开后的颜色、描边和速度参数。
	// 组高 = N 行 + (N-1) 档行距：末行之后不再留行距。以前按 N × RowStep 测量，
	// 把「末行行距」也算进组里，组间距于是变成 行距 + SectionGap（「变色速度」
	// 上方比正常行距大三倍）。绘制侧每行仍是「本行 + 行距」，末行的尾巴会落在
	// 组矩形之外被自然吃掉，两边视觉一致。
	const float OutlineHeight = 3.0f * Metrics.m_LineHeight + 2.0f * Metrics.m_LineSpacing;
	const float HueHeight = Metrics.m_LineHeight;
	float Bottom;
	if(View.w >= 640.0f * Metrics.m_UiScale)
	{
		View.VSplitMid(&Layout.m_Outline, &Layout.m_Hue, Metrics.m_SectionGap);
		Layout.m_Outline.h = OutlineHeight;
		Layout.m_Hue.h = HueHeight;
		Bottom = View.y + std::max(OutlineHeight, HueHeight);
	}
	else
	{
		Layout.m_Outline = {View.x, View.y, View.w, OutlineHeight};
		Layout.m_Hue = {View.x, View.y + OutlineHeight + Metrics.m_SectionGap, View.w, HueHeight};
		Bottom = Layout.m_Hue.y + HueHeight;
	}
	Layout.m_Shadow = {View.x, Bottom + Metrics.m_SectionGap, View.w, Metrics.m_LineHeight};
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
