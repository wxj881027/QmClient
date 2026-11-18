#ifndef GAME_CLIENT_QMUI_SETTINGSSEARCHHIGHLIGHT_H
#define GAME_CLIENT_QMUI_SETTINGSSEARCHHIGHLIGHT_H

#include "UiSurfaceText.h"

#include <engine/textrender.h>

namespace qm_settings_highlight
{
	// 搜索命中的字色按当前表面亮度二选一；组件内部不再写死纯黄。
	inline ColorRGBA ResolveMatchColor()
	{
		const ColorRGBA Surface = CUiScopedSurfaceText::CurrentSurface();
		return UiSurfaceLuminance(CompositeUiSurface(Surface)) >= 0.55f ?
			       ui_token::color::SEARCH_MATCH_ON_LIGHT :
			       ui_token::color::SEARCH_MATCH_ON_DARK;
	}

	// 在绘制时覆盖字色，使已缓存的标签也能随搜索状态切换颜色。
	class CScopedLabelColor
	{
		ITextRender *m_pTextRender;
		ColorRGBA m_PreviousColor;

	public:
		CScopedLabelColor(ITextRender *pTextRender, bool Highlight) :
			m_pTextRender(Highlight ? pTextRender : nullptr)
		{
			if(m_pTextRender)
			{
				m_PreviousColor = m_pTextRender->GetTextColor();
				m_pTextRender->TextColor(ResolveMatchColor().WithAlpha(m_PreviousColor.a));
			}
		}

		~CScopedLabelColor()
		{
			if(m_pTextRender)
				m_pTextRender->TextColor(m_PreviousColor);
		}

		CScopedLabelColor(const CScopedLabelColor &) = delete;
		CScopedLabelColor &operator=(const CScopedLabelColor &) = delete;
	};
}

#endif // GAME_CLIENT_QMUI_SETTINGSSEARCHHIGHLIGHT_H
