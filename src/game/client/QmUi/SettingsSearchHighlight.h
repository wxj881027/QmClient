#ifndef GAME_CLIENT_QMUI_SETTINGSSEARCHHIGHLIGHT_H
#define GAME_CLIENT_QMUI_SETTINGSSEARCHHIGHLIGHT_H

#include <engine/textrender.h>

namespace qm_settings_highlight
{
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
				m_pTextRender->TextColor(ColorRGBA(1.0f, 1.0f, 0.0f, m_PreviousColor.a));
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
