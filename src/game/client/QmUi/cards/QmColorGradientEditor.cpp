#include "QmColorGradientEditor.h"

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/components/menus.h>
#include <game/client/components/message_gradient.h>
#include <game/client/qm_icon.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <cstdint>
#include <string>

using namespace FontIcons;

bool CMenus::DoColorGradientPalette(CUIRect *pView, unsigned *pBaseColor, char *pGradient, int GradientSize,
	CButtonContainer *pAddButton, CButtonContainer *pRemoveButton, unsigned *pColorValues,
	const SSettingsContentMetrics &Metrics, bool CheckBoxSpacing, bool BaseHasAlpha)
{
	CSettingsContentLayoutFlow Flow(*pView);
	const float ResolvedButtonHeight = Metrics.m_ButtonHeight;
	const float ColorLineHeight = std::max(Metrics.m_LineHeight, ResolvedButtonHeight);
	const float BottomMargin = Metrics.m_LineSpacing;
	const float ColorButtonSpacing = Metrics.m_LineSpacing;
	const float ChangeButtonSize = ResolvedButtonHeight;
	const float BodySize = Metrics.m_BodySize;
	const bool ReadOnly = Ui()->RenderOnly();
	CUIRect ColorLine;
	const std::string Id = "gradient-palette-" + std::to_string(reinterpret_cast<std::uintptr_t>(pGradient));
	// 调色板行自带行距：声明阶段记到上一行末尾，行后不再补 Space。
	const bool HasRow = Flow.Control(Id.c_str(), ColorLineHeight, ColorLine, BottomMargin);
	if(!HasRow)
		return false;
	bool Changed = false;
	const SQmGradientPaletteBinding Binding{pBaseColor, pGradient, GradientSize, BaseHasAlpha};
	int NumColors = Binding.Load(pColorValues);
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
		const ColorHSLA PickedColor = DoButton_ColorPicker(&ColorButton, &pColorValues[ColorIndex], true);
		pColorValues[ColorIndex] = PickedColor.Pack(true);
		if(pColorValues[ColorIndex] != OldColor && !ReadOnly)
		{
			Binding.Store(pColorValues, NumColors);
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
		Binding.Store(pColorValues, NumColors);
		Changed = true;
	}
	if(DoButton_Menu_QmIcon(pAddButton, EQmIcon::PLUS, FONT_ICON_PLUS, CanAddColor ? 0 : -1, &AddButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, ui_token::radius::PILL, 0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), nullptr, BodySize) && CanAddColor && !ReadOnly)
	{
		pColorValues[NumColors] = pColorValues[NumColors - 1];
		++NumColors;
		Binding.Store(pColorValues, NumColors);
		Changed = true;
	}

	return Changed;
}

bool CMenus::DoColorGradientGeometry(CUIRect &Content, const SQmGradientGeometryBinding &Binding,
	SQmGradientGeometryState &State, const SSettingsContentMetrics &Metrics, float LabelWidth)
{
	CSettingsContentLayoutFlow Flow(Content);
	const bool ReadOnly = Ui()->RenderOnly();
	const auto Before = Binding.Values();
	const float RowHeight = std::max(Metrics.m_LineHeight, Metrics.m_ButtonHeight);
	const std::string Prefix = "gradient-geometry-" + std::to_string(reinterpret_cast<std::uintptr_t>(Binding.m_pType)) + "-";
	CUIRect Row, Label, Control;
	const auto NextRow = [&](const char *pId, const char *pText) {
		const std::string Id = Prefix + pId;
		// 行距同样由行自己声明，行后不再补 Space。
		const bool HasRow = Flow.Control(Id.c_str(), RowHeight, Row, ui_token::settings::RowGap(Metrics.m_LineSpacing));
		if(!HasRow)
			return false;
		SplitSettingsControlRow(Row, Metrics, &Label, &Control);
		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Label, pText, Metrics.m_BodySize, TEXTALIGN_ML, Props);
		return true;
	};
	if(NextRow("type", Localize("Gradient type")))
	{
		const char *apTypes[] = {Localize("Linear gradient"), Localize("Radial gradient"), Localize("Angular gradient"), Localize("Reflected gradient"), Localize("Diamond gradient")};
		State.m_TypeDropdown.m_SelectionPopupContext.m_pScrollRegion = &State.m_TypeScroll;
		const int Type = DoSettingsDropDown(&Control, std::clamp(*Binding.m_pType, 0, 4), apTypes, std::size(apTypes), State.m_TypeDropdown);
		if(Flow.Input() && !ReadOnly)
			*Binding.m_pType = Type;
	}
	const auto Slider = [&](const char *pId, const char *pText, int *pValue, int Min, int Max, const char *pUnit) {
		if(NextRow(pId, pText))
			RenderQmSettingsSliderWithValueInput(pValue, Control, pValue, Min, Max, pUnit, ReadOnly);
	};
	if(*Binding.m_pType == 1)
	{
		if(NextRow("radial-help", ""))
		{
			SLabelProperties Props;
			Props.m_MaxWidth = Row.w;
			Props.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&Row, Localize("Radial gradients spread from the center"), Metrics.m_BodySize, TEXTALIGN_ML, Props);
		}
	}
	else
		Slider("direction", Localize("Gradient direction"), Binding.m_pAngle, 0, 360, "°");
	Slider("center-x", Localize("Gradient horizontal position"), Binding.m_pCenterX, 0, 100, "%");
	Slider("center-y", Localize("Gradient vertical position"), Binding.m_pCenterY, 0, 100, "%");
	Slider("range", Localize("Gradient range"), Binding.m_pRange, 10, 200, "%");
	if(NextRow("reverse", ""))
	{
		if(DoButton_CheckBox_Common_WithLabelElement(Binding.m_pReverse, Localize("Reverse gradient colors"),
			   *Binding.m_pReverse ? "X" : "", &Row, BUTTONFLAG_LEFT, nullptr, !ReadOnly, Metrics.m_BodySize) &&
			Flow.Input() && !ReadOnly)
			*Binding.m_pReverse ^= 1;
	}
	return Before != Binding.Values();
}
