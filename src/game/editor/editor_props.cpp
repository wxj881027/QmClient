#include "editor.h"
#include "enums.h"

#include <engine/textrender.h>

#include <game/client/qm_icon_manager.h>
#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/sound.h>

using namespace FontIcons;

int CEditor::DoProperties(CUIRect *pToolbox, CProperty *pProps, int *pIds, int *pNewVal, const std::vector<ColorRGBA> &vColors)
{
	auto Res = DoPropertiesWithState<int>(pToolbox, pProps, pIds, pNewVal, vColors);
	return Res.m_Value;
}

template<typename E>
SEditResult<E> CEditor::DoPropertiesWithState(CUIRect *pToolBox, CProperty *pProps, int *pIds, int *pNewVal, const std::vector<ColorRGBA> &vColors)
{
	int Change = -1;
	EEditState State = EEditState::NONE;

	for(int i = 0; pProps[i].m_pName; i++)
	{
		const ColorRGBA *pColor = i >= (int)vColors.size() ? &ms_DefaultPropColor : &vColors[i];

		CUIRect Slot;
		pToolBox->HSplitTop(13.0f, &Slot, pToolBox);
		CUIRect Label, Shifter;
		Slot.VSplitMid(&Label, &Shifter);
		Shifter.HMargin(1.0f, &Shifter);
		Ui()->DoLabel(&Label, pProps[i].m_pName, 10.0f, TEXTALIGN_ML);

		if(pProps[i].m_Type == PROPTYPE_INT)
		{
			CUIRect Inc, Dec;

			Shifter.VSplitRight(10.0f, &Shifter, &Inc);
			Shifter.VSplitLeft(10.0f, &Dec, &Shifter);
			auto NewValueRes = UiDoValueSelector((char *)&pIds[i], &Shifter, "", pProps[i].m_Value, pProps[i].m_Min, pProps[i].m_Max, 1, 1.0f, Localize("Use left mouse button to drag and change the value. Hold shift to be more precise. Right click to edit as text.", "Editor"), false, false, 0, pColor);
			int NewValue = NewValueRes.m_Value;
			if(NewValue != pProps[i].m_Value || (NewValueRes.m_State != EEditState::NONE && NewValueRes.m_State != EEditState::EDITING))
			{
				*pNewVal = NewValue;
				if(NewValueRes.m_State != EEditState::NONE)
				{
					Change = i;
				}
				State = NewValueRes.m_State;
			}
			if(DoButton_QmIcon((char *)&pIds[i] + 1, EQmIcon::MINUS, FONT_ICON_MINUS, 0, &Dec, BUTTONFLAG_LEFT, Localize("Decrease value.", "Editor"), IGraphics::CORNER_L, 7.0f))
			{
				*pNewVal = std::clamp(pProps[i].m_Value - 1, pProps[i].m_Min, pProps[i].m_Max);
				Change = i;
				State = EEditState::ONE_GO;
			}
			if(DoButton_QmIcon(((char *)&pIds[i]) + 2, EQmIcon::PLUS, FONT_ICON_PLUS, 0, &Inc, BUTTONFLAG_LEFT, Localize("Increase value.", "Editor"), IGraphics::CORNER_R, 7.0f))
			{
				*pNewVal = std::clamp(pProps[i].m_Value + 1, pProps[i].m_Min, pProps[i].m_Max);
				Change = i;
				State = EEditState::ONE_GO;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_BOOL)
		{
			CUIRect No, Yes;
			Shifter.VSplitMid(&No, &Yes);
			if(DoButton_Ex(&pIds[i], Localize("No", "Editor"), !pProps[i].m_Value, &No, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			{
				*pNewVal = 0;
				Change = i;
				State = EEditState::ONE_GO;
			}
			if(DoButton_Ex(((char *)&pIds[i]) + 1, Localize("Yes", "Editor"), pProps[i].m_Value, &Yes, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			{
				*pNewVal = 1;
				Change = i;
				State = EEditState::ONE_GO;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_ANGLE_SCROLL)
		{
			CUIRect Inc, Dec;
			Shifter.VSplitRight(10.0f, &Shifter, &Inc);
			Shifter.VSplitLeft(10.0f, &Dec, &Shifter);
			const bool Shift = Input()->ShiftIsPressed();
			int Step = Shift ? 1 : 45;

			auto NewValueRes = UiDoValueSelector(&pIds[i], &Shifter, "", pProps[i].m_Value, pProps[i].m_Min, pProps[i].m_Max, Shift ? 1 : 45, Shift ? 1.0f : 10.0f, Localize("Use left mouse button to drag and change the value. Hold shift to be more precise. Right click to edit as text.", "Editor"), false, false, 0);
			int NewValue = NewValueRes.m_Value;
			if(DoButton_QmIcon(&pIds[i] + 1, EQmIcon::MINUS, FONT_ICON_MINUS, 0, &Dec, BUTTONFLAG_LEFT, Localize("Decrease value.", "Editor"), IGraphics::CORNER_L, 7.0f))
			{
				NewValue = (std::ceil((pProps[i].m_Value / (float)Step)) - 1) * Step;
				if(NewValue < 0)
					NewValue += 360;
				NewValueRes.m_State = EEditState::ONE_GO;
			}
			if(DoButton_QmIcon(&pIds[i] + 2, EQmIcon::PLUS, FONT_ICON_PLUS, 0, &Inc, BUTTONFLAG_LEFT, Localize("Increase value.", "Editor"), IGraphics::CORNER_R, 7.0f))
			{
				NewValue = (pProps[i].m_Value + Step) / Step * Step;
				NewValueRes.m_State = EEditState::ONE_GO;
			}

			if(NewValue != pProps[i].m_Value || (NewValueRes.m_State != EEditState::NONE && NewValueRes.m_State != EEditState::EDITING))
			{
				*pNewVal = NewValue % 360;
				if(NewValueRes.m_State != EEditState::NONE)
				{
					Change = i;
				}
				if(State != EEditState::ONE_GO)
					State = NewValueRes.m_State;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_COLOR)
		{
			const auto &&SetColor = [&](ColorRGBA NewColor) {
				const int NewValue = NewColor.PackAlphaLast();
				if(NewValue != pProps[i].m_Value || m_ColorPickerPopupContext.m_State != EEditState::EDITING)
				{
					*pNewVal = NewValue;
					if(m_ColorPickerPopupContext.m_State != EEditState::NONE)
					{
						Change = i;
					}
					State = m_ColorPickerPopupContext.m_State;
				}
			};
			DoColorPickerButton(&pIds[i], &Shifter, ColorRGBA::UnpackAlphaLast<ColorRGBA>(pProps[i].m_Value), SetColor);
		}
		else if(pProps[i].m_Type == PROPTYPE_IMAGE)
		{
			const char *pName;
			if(pProps[i].m_Value < 0)
				pName = Localize("None", "Editor");
			else
				pName = Map()->m_vpImages[pProps[i].m_Value]->m_aName;

			if(DoButton_Ex(&pIds[i], pName, 0, &Shifter, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL))
				PopupSelectImageInvoke(pProps[i].m_Value, Ui()->MouseX(), Ui()->MouseY());

			int Result = PopupSelectImageResult();
			if(Result >= -1)
			{
				*pNewVal = Result;
				Change = i;
				State = EEditState::ONE_GO;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_SHIFT)
		{
			CUIRect Left, Right, Up, Down;
			Shifter.VSplitMid(&Left, &Up, 2.0f);
			Left.VSplitLeft(10.0f, &Left, &Shifter);
			Shifter.VSplitRight(10.0f, &Shifter, &Right);
			Shifter.Draw(ColorRGBA(1, 1, 1, 0.5f), IGraphics::CORNER_NONE, 0.0f);
			Ui()->DoLabel(&Shifter, "X", 10.0f, TEXTALIGN_MC);
			Up.VSplitLeft(10.0f, &Up, &Shifter);
			Shifter.VSplitRight(10.0f, &Shifter, &Down);
			Shifter.Draw(ColorRGBA(1, 1, 1, 0.5f), IGraphics::CORNER_NONE, 0.0f);
			Ui()->DoLabel(&Shifter, "Y", 10.0f, TEXTALIGN_MC);
			if(DoButton_QmIcon(&pIds[i], EQmIcon::MINUS, FONT_ICON_MINUS, 0, &Left, BUTTONFLAG_LEFT, Localize("Shift left.", "Editor"), IGraphics::CORNER_L, 7.0f))
			{
				*pNewVal = (int)EShiftDirection::LEFT;
				Change = i;
				State = EEditState::ONE_GO;
			}
			if(DoButton_QmIcon(((char *)&pIds[i]) + 3, EQmIcon::PLUS, FONT_ICON_PLUS, 0, &Right, BUTTONFLAG_LEFT, Localize("Shift right.", "Editor"), IGraphics::CORNER_R, 7.0f))
			{
				*pNewVal = (int)EShiftDirection::RIGHT;
				Change = i;
				State = EEditState::ONE_GO;
			}
			if(DoButton_QmIcon(((char *)&pIds[i]) + 1, EQmIcon::MINUS, FONT_ICON_MINUS, 0, &Up, BUTTONFLAG_LEFT, Localize("Shift up.", "Editor"), IGraphics::CORNER_L, 7.0f))
			{
				*pNewVal = (int)EShiftDirection::UP;
				Change = i;
				State = EEditState::ONE_GO;
			}
			if(DoButton_QmIcon(((char *)&pIds[i]) + 2, EQmIcon::PLUS, FONT_ICON_PLUS, 0, &Down, BUTTONFLAG_LEFT, Localize("Shift down.", "Editor"), IGraphics::CORNER_R, 7.0f))
			{
				*pNewVal = (int)EShiftDirection::DOWN;
				Change = i;
				State = EEditState::ONE_GO;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_SOUND)
		{
			const char *pName;
			if(pProps[i].m_Value < 0)
				pName = Localize("None", "Editor");
			else
				pName = Map()->m_vpSounds[pProps[i].m_Value]->m_aName;

			if(DoButton_Ex(&pIds[i], pName, 0, &Shifter, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL))
				PopupSelectSoundInvoke(pProps[i].m_Value, Ui()->MouseX(), Ui()->MouseY());

			int Result = PopupSelectSoundResult();
			if(Result >= -1)
			{
				*pNewVal = Result;
				Change = i;
				State = EEditState::ONE_GO;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_AUTOMAPPER)
		{
			const char *pName;
			if(pProps[i].m_Value < 0 || pProps[i].m_Min < 0 || pProps[i].m_Min >= (int)Map()->m_vpImages.size())
				pName = Localize("None", "Editor");
			else
				pName = Map()->m_vpImages[pProps[i].m_Min]->m_AutoMapper.GetConfigName(pProps[i].m_Value);

			if(DoButton_Ex(&pIds[i], pName, 0, &Shifter, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL))
				PopupSelectConfigAutoMapInvoke(pProps[i].m_Value, Ui()->MouseX(), Ui()->MouseY());

			int Result = PopupSelectConfigAutoMapResult();
			if(Result >= -1)
			{
				*pNewVal = Result;
				Change = i;
				State = EEditState::ONE_GO;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_AUTOMAPPER_REFERENCE)
		{
			const char *pName;
			if(pProps[i].m_Value < 0)
				pName = Localize("None", "Editor");
			else
				pName = Localize(AUTOMAP_REFERENCE_NAMES[pProps[i].m_Value], "Editor");

			if(DoButton_Ex(&pIds[i], pName, 0, &Shifter, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL))
				PopupSelectAutoMapReferenceInvoke(pProps[i].m_Value, Ui()->MouseX(), Ui()->MouseY());

			const int Result = PopupSelectAutoMapReferenceResult();
			if(Result >= -1)
			{
				*pNewVal = Result;
				Change = i;
				State = EEditState::ONE_GO;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_ENVELOPE)
		{
			CUIRect Inc, Dec;
			char aBuf[8];
			int CurValue = pProps[i].m_Value;

			Shifter.VSplitRight(10.0f, &Shifter, &Inc);
			Shifter.VSplitLeft(10.0f, &Dec, &Shifter);

			if(CurValue <= 0 || CurValue > (int)Map()->m_vpEnvelopes.size())
			{
				str_copy(aBuf, Localize("None:", "Editor"));
			}
			else if(Map()->m_vpEnvelopes[CurValue - 1]->m_aName[0])
			{
				str_format(aBuf, sizeof(aBuf), "%s:", Map()->m_vpEnvelopes[CurValue - 1]->m_aName);
				if(!str_endswith(aBuf, ":"))
				{
					aBuf[sizeof(aBuf) - 2] = ':';
					aBuf[sizeof(aBuf) - 1] = '\0';
				}
			}
			else
			{
				aBuf[0] = '\0';
			}

			auto NewValueRes = UiDoValueSelector((char *)&pIds[i], &Shifter, aBuf, CurValue, 0, Map()->m_vpEnvelopes.size(), 1, 1.0f, Localize("Select the envelope.", "Editor"), false, false, IGraphics::CORNER_NONE);
			int NewVal = NewValueRes.m_Value;
			if(NewVal != CurValue || (NewValueRes.m_State != EEditState::NONE && NewValueRes.m_State != EEditState::EDITING))
			{
				*pNewVal = NewVal;
				if(NewValueRes.m_State != EEditState::NONE)
				{
					Change = i;
				}
				State = NewValueRes.m_State;
			}

			if(DoButton_QmIcon((char *)&pIds[i] + 1, EQmIcon::MINUS, FONT_ICON_MINUS, 0, &Dec, BUTTONFLAG_LEFT, Localize("Select previous envelope.", "Editor envelope selector"), IGraphics::CORNER_L, 7.0f))
			{
				*pNewVal = pProps[i].m_Value - 1;
				Change = i;
				State = EEditState::ONE_GO;
			}
			if(DoButton_QmIcon(((char *)&pIds[i]) + 2, EQmIcon::PLUS, FONT_ICON_PLUS, 0, &Inc, BUTTONFLAG_LEFT, Localize("Select next envelope.", "Editor envelope selector"), IGraphics::CORNER_R, 7.0f))
			{
				*pNewVal = pProps[i].m_Value + 1;
				Change = i;
				State = EEditState::ONE_GO;
			}
		}
	}

	return SEditResult<E>{State, static_cast<E>(Change)};
}

template SEditResult<ECircleShapeProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<ERectangleShapeProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<EGroupProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<ELayerProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<ELayerQuadsProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<ETilesProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<ETilesCommonProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<ELayerSoundsProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<EQuadProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<EQuadPointProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<ESoundProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
