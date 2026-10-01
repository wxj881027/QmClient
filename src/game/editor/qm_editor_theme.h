#ifndef GAME_EDITOR_QM_EDITOR_THEME_H
#define GAME_EDITOR_QM_EDITOR_THEME_H

#include <base/color.h>

#include <algorithm>

namespace QmEditorTheme
{
	inline constexpr ColorRGBA WORKSPACE{0.075f, 0.080f, 0.080f, 1.0f};
	inline constexpr ColorRGBA PANEL{0.115f, 0.122f, 0.122f, 1.0f};
	inline constexpr ColorRGBA HEADER{0.145f, 0.153f, 0.153f, 1.0f};
	inline constexpr ColorRGBA BORDER{0.225f, 0.240f, 0.240f, 1.0f};
	inline constexpr ColorRGBA CONTROL{0.180f, 0.192f, 0.192f, 1.0f};
	inline constexpr ColorRGBA INPUT{0.085f, 0.093f, 0.093f, 1.0f};
	inline constexpr ColorRGBA ACCENT{0.24f, 0.72f, 0.61f, 1.0f};
	inline constexpr ColorRGBA TEXT{0.91f, 0.93f, 0.92f, 1.0f};
	inline constexpr ColorRGBA TEXT_MUTED{0.61f, 0.66f, 0.64f, 1.0f};
	inline constexpr ColorRGBA WARNING{0.91f, 0.66f, 0.29f, 1.0f};
	inline constexpr float CORNER_RADIUS = 2.0f;
	inline constexpr float ROW_HEIGHT = 18.0f;
	inline constexpr float FONT_SIZE = 10.0f;

	inline ColorRGBA ButtonColor(int Checked, bool Hovered, bool Pressed)
	{
		if(Checked < 0)
			return ColorRGBA(0.13f, 0.14f, 0.14f, 1.0f);
		if(Checked == 8)
			return ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);

		ColorRGBA Color = CONTROL;
		switch(Checked)
		{
		case 1: Color = ColorRGBA(0.12f, 0.34f, 0.29f, 1.0f); break;
		case 2: Color = ColorRGBA(0.25f, 0.22f, 0.31f, 1.0f); break;
		case 3: Color = ColorRGBA(0.36f, 0.29f, 0.43f, 1.0f); break;
		case 4: Color = ColorRGBA(0.34f, 0.27f, 0.16f, 1.0f); break;
		case 5: Color = ColorRGBA(0.46f, 0.35f, 0.17f, 1.0f); break;
		case 6: Color = ColorRGBA(0.22f, 0.25f, 0.23f, 1.0f); break;
		case 7: Color = ColorRGBA(0.14f, 0.40f, 0.33f, 1.0f); break;
		case 9: Color = ColorRGBA(0.47f, 0.17f, 0.18f, 1.0f); break;
		default: break;
		}
		const float Brightness = Pressed ? -0.035f : Hovered ? 0.075f :
								       0.0f;
		Color.r = std::clamp(Color.r + Brightness, 0.0f, 1.0f);
		Color.g = std::clamp(Color.g + Brightness, 0.0f, 1.0f);
		Color.b = std::clamp(Color.b + Brightness, 0.0f, 1.0f);
		return Color;
	}

	inline ColorRGBA ButtonTextColor(int Checked)
	{
		return Checked < 0 ? TEXT_MUTED : TEXT;
	}
}

#endif
