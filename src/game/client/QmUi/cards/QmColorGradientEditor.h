#ifndef GAME_CLIENT_QMUI_CARDS_QMCOLORGRADIENTEDITOR_H
#define GAME_CLIENT_QMUI_CARDS_QMCOLORGRADIENTEDITOR_H

#include <game/client/QmUi/QmGradientGeometry.h>
#include <game/client/components/message_gradient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>

#include <algorithm>

// 只绑定配置；色标透明度与单色配置中的整体不透明度分别保存。
struct SQmGradientPaletteBinding
{
	unsigned *m_pBaseColor;
	char *m_pGradient;
	int m_GradientSize;
	bool m_BaseHasAlpha;

	int Load(unsigned *pColors) const
	{
		const int Count = CMessageGradient::Unpack(m_pGradient, pColors, CMessageGradient::MAX_COLORS);
		if(Count > 0)
			return Count;
		pColors[0] = ColorHSLA(*m_pBaseColor, m_BaseHasAlpha).WithAlpha(1.0f).Pack(true);
		return 1;
	}

	void Store(const unsigned *pColors, int Count) const
	{
		const float OverallAlpha = ColorHSLA(*m_pBaseColor, m_BaseHasAlpha).a;
		*m_pBaseColor = ColorHSLA(pColors[0], true).WithAlpha(OverallAlpha).Pack(m_BaseHasAlpha);
		if(Count == 1 && (pColors[0] >> 24) == 0xffu)
			CMessageGradient::Reset(m_pGradient, m_GradientSize);
		else
			CMessageGradient::Pack(pColors, Count, m_pGradient, m_GradientSize);
	}

	int Opacity() const
	{
		return round_to_int(ColorHSLA(*m_pBaseColor, m_BaseHasAlpha).a * 100.0f);
	}

	void SetOpacity(int Opacity) const
	{
		*m_pBaseColor = ColorHSLA(*m_pBaseColor, m_BaseHasAlpha).WithAlpha(std::clamp(Opacity, 0, 100) / 100.0f).Pack(m_BaseHasAlpha);
	}
};

struct SQmGradientGeometryState
{
	CUi::SDropDownState m_TypeDropdown;
	CScrollRegion m_TypeScroll;
};

#endif
