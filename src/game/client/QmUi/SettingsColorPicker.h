#ifndef GAME_CLIENT_QMUI_SETTINGSCOLORPICKER_H
#define GAME_CLIENT_QMUI_SETTINGSCOLORPICKER_H

#include <base/color.h>
#include <base/math.h>

#include <algorithm>

inline unsigned int PackSettingsAlphaColor(const unsigned int ColorValue, const int Opacity)
{
	return ColorHSLA(ColorValue).WithAlpha(std::clamp(Opacity / 100.0f, 0.0f, 1.0f)).Pack(true);
}

inline void UnpackSettingsAlphaColor(const unsigned int PackedColor, unsigned int &ColorValue, int &Opacity)
{
	const ColorHSLA Color(PackedColor, true);
	ColorValue = Color.Pack(false);
	Opacity = std::clamp(round_to_int(Color.a * 100.0f), 0, 100);
}

struct SSettingsAlphaColorPickerState
{
	unsigned int m_PackedColor = 0;

	void RefreshFromConfig(const unsigned int ColorValue, const int Opacity, const bool Editing)
	{
		// 弹窗在设置行之后处理输入，关闭后仍可能留有尚未回写的确认或取消结果。
		const bool PendingChange = m_Initialized && m_PackedColor != m_SyncedPackedColor;
		if(!m_Initialized || (!Editing && !PendingChange))
		{
			m_PackedColor = PackSettingsAlphaColor(ColorValue, Opacity);
			m_SyncedPackedColor = m_PackedColor;
			m_Initialized = true;
		}
	}

	bool ApplyToConfig(unsigned int &ColorValue, int &Opacity)
	{
		const bool Changed = m_PackedColor != PackSettingsAlphaColor(ColorValue, Opacity);
		if(Changed)
			UnpackSettingsAlphaColor(m_PackedColor, ColorValue, Opacity);
		m_SyncedPackedColor = m_PackedColor;
		return Changed;
	}

private:
	unsigned int m_SyncedPackedColor = 0;
	bool m_Initialized = false;
};

#endif
