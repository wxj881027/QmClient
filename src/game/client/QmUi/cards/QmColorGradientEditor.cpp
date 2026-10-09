#include <game/client/components/menus.h>
#include <game/client/components/message_gradient.h>
#include <game/client/qm_icon.h>
#include <game/client/ui.h>

#include <algorithm>

using namespace FontIcons;

bool CMenus::DoColorGradientPalette(CUIRect *pView, unsigned *pBaseColor, char *pGradient, int GradientSize,
	CButtonContainer *pAddButton, CButtonContainer *pRemoveButton, unsigned *pColorValues,
	const SSettingsContentMetrics &Metrics, bool CheckBoxSpacing, bool Alpha)
{
	const float ResolvedButtonHeight = Metrics.m_ButtonHeight;
	const float ColorLineHeight = std::max(Metrics.m_LineHeight, ResolvedButtonHeight);
	const float BottomMargin = Metrics.m_LineSpacing;
	const float ColorButtonSpacing = Metrics.m_LineSpacing;
	const float ChangeButtonSize = ResolvedButtonHeight;
	const float BodySize = Metrics.m_BodySize;
	const bool ReadOnly = Ui()->RenderOnly();
	bool Changed = false;
	int NumColors = CMessageGradient::Unpack(pGradient, pColorValues, CMessageGradient::MAX_COLORS);
	if(NumColors <= 0)
	{
		NumColors = 1;
		pColorValues[0] = Alpha ? *pBaseColor >> 8 : *pBaseColor;
	}

	CUIRect ColorLine;
	pView->HSplitTop(ColorLineHeight, &ColorLine, pView);
	CUIRect ColorArea = ColorLine;
	if(CheckBoxSpacing)
		ColorArea.VSplitLeft(ColorLine.h + 5.0f, nullptr, &ColorArea);
	ColorArea.VSplitRight(ChangeButtonSize * 2.0f + ColorButtonSpacing, &ColorArea, &ColorLine);
	const float ColorButtonSize = std::min(ResolvedButtonHeight,
		std::max(1.0f, (ColorArea.w - ColorButtonSpacing * (NumColors - 1)) / NumColors));

	for(int ColorIndex = 0; ColorIndex < NumColors; ++ColorIndex)
	{
		CUIRect ColorButton;
		ColorArea.VSplitLeft(ColorButtonSize, &ColorButton, &ColorArea);
		ColorButton.HMargin((ColorButton.h - ColorButtonSize) / 2.0f, &ColorButton);
		if(ColorIndex < NumColors - 1)
			ColorArea.VSplitLeft(ColorButtonSpacing, nullptr, &ColorArea);
		const unsigned OldColor = pColorValues[ColorIndex];
		const ColorHSLA PickedColor = DoButton_ColorPicker(&ColorButton, &pColorValues[ColorIndex], false);
		pColorValues[ColorIndex] = PickedColor.Pack(false);
		if(pColorValues[ColorIndex] != OldColor && !ReadOnly)
		{
			*pBaseColor = Alpha ? (pColorValues[0] << 8) | (*pBaseColor & 0xffu) : pColorValues[0];
			if(NumColors == 1)
				CMessageGradient::Reset(pGradient, GradientSize);
			else
				CMessageGradient::Pack(pColorValues, NumColors, pGradient, GradientSize);
			Changed = true;
		}
	}

	CUIRect RemoveButton, AddButton;
	ColorLine.VSplitLeft(ChangeButtonSize, &RemoveButton, &ColorLine);
	ColorLine.VSplitLeft(ColorButtonSpacing, nullptr, &ColorLine);
	ColorLine.VSplitLeft(ChangeButtonSize, &AddButton, nullptr);
	RemoveButton.HMargin((RemoveButton.h - ChangeButtonSize) / 2.0f, &RemoveButton);
	AddButton.HMargin((AddButton.h - ChangeButtonSize) / 2.0f, &AddButton);
	const bool CanRemoveColor = NumColors > CMessageGradient::MIN_COLORS;
	const bool CanAddColor = NumColors < CMessageGradient::MAX_COLORS;
	if(DoButton_Menu_QmIcon(pRemoveButton, EQmIcon::MINUS, FONT_ICON_MINUS, CanRemoveColor ? 0 : -1, &RemoveButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, ui_token::radius::PILL, 0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), nullptr, BodySize) && CanRemoveColor && !ReadOnly)
	{
		--NumColors;
		*pBaseColor = Alpha ? (pColorValues[0] << 8) | (*pBaseColor & 0xffu) : pColorValues[0];
		if(NumColors == 1)
			CMessageGradient::Reset(pGradient, GradientSize);
		else
			CMessageGradient::Pack(pColorValues, NumColors, pGradient, GradientSize);
		Changed = true;
	}
	if(DoButton_Menu_QmIcon(pAddButton, EQmIcon::PLUS, FONT_ICON_PLUS, CanAddColor ? 0 : -1, &AddButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, ui_token::radius::PILL, 0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), nullptr, BodySize) && CanAddColor && !ReadOnly)
	{
		pColorValues[NumColors] = pColorValues[NumColors - 1];
		++NumColors;
		CMessageGradient::Pack(pColorValues, NumColors, pGradient, GradientSize);
		Changed = true;
	}

	pView->HSplitTop(BottomMargin, nullptr, pView);
	return Changed;
}
