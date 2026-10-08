#ifndef GAME_CLIENT_QMUI_SETTINGSICONOPTIONS_H
#define GAME_CLIENT_QMUI_SETTINGSICONOPTIONS_H

#include "SettingsPageLayout.h"

#include <game/client/qm_icon_color_policy.h>

namespace qm_icon_settings
{
	inline int PresetIndex(int Preset)
	{
		return Preset == 4 ? 2 : Preset == 2 ? 1 :
						       0;
	}

	inline void SelectPreset(int Index, int &Preset, int &CustomEnabled)
	{
		// 修改预设不关闭独立开关，也不丢失旧配置中的自定义选择。
		CustomEnabled = CustomColorEnabled(Preset, CustomEnabled);
		Preset = Index == 2 ? 4 : Index == 1 ? 2 :
						       1;
	}

	inline void ToggleCustomColor(int &Preset, int &Enabled)
	{
		Enabled = !CustomColorEnabled(Preset, Enabled);
		if(Preset == 3)
			Preset = 1;
	}

	inline float ContentHeight(const SSettingsContentMetrics &Metrics, bool CustomEnabled, float Width)
	{
		const CUIRect View{0.0f, 0.0f, Width, 0.0f};
		const float ColorHeight = ResolveSettingsRadioRowLayout(View, 3, Metrics).m_Height;
		const float StyleHeight = ResolveSettingsRadioRowLayout(View, 4, Metrics).m_Height;
		return ResolveSettingsContentFlowHeight(Metrics, CustomEnabled ? std::initializer_list<float>{ColorHeight, Metrics.m_LineHeight, Metrics.m_ButtonHeight, Metrics.m_ButtonHeight, Metrics.m_ButtonHeight, StyleHeight} : std::initializer_list<float>{ColorHeight, Metrics.m_LineHeight, Metrics.m_ButtonHeight, Metrics.m_ButtonHeight, StyleHeight});
	}
}

#endif
