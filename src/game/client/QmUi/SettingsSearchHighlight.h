#ifndef GAME_CLIENT_QMUI_SETTINGSSEARCHHIGHLIGHT_H
#define GAME_CLIENT_QMUI_SETTINGSSEARCHHIGHLIGHT_H

#include <game/client/QmUi/UiTheme.h>
#include <game/client/ui_rect.h>

#include <algorithm>

// 搜索命中的行背景样式：填充取主题选中色，边框/文字取 accent，圆角按
// 行高裁到 BASE 以内，避免短行出现方角或超出控件轮廓。
namespace qm_settings_highlight
{
	struct SStyle
	{
		ColorRGBA m_Fill;
		ColorRGBA m_Border;
		ColorRGBA m_Text;
		float m_Radius;
	};

	inline SStyle Resolve(const SUiTheme &Theme, const CUIRect &RowRect)
	{
		SStyle Style;
		Style.m_Fill = Theme.m_Selected;
		Style.m_Border = Theme.m_Accent.WithAlpha(0.65f);
		Style.m_Text = Theme.m_Accent;
		Style.m_Radius = std::min(ui_token::radius::BASE, std::max(0.0f, RowRect.h) * 0.5f);
		return Style;
	}
}

#endif // GAME_CLIENT_QMUI_SETTINGSSEARCHHIGHLIGHT_H
