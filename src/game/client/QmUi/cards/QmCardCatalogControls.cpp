#include "QmCardCatalog.h"

#include <base/math.h>
#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmScroll.h>
#include <game/client/QmUi/SettingsCardCollapseState.h>
#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/binds.h>
#include <game/client/components/key_binder.h>
#include <game/client/components/menus.h>
#include <game/client/components/menus_settings_controls.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

using namespace FontIcons;

namespace
{
	constexpr float BIND_REVEAL_MARGIN = 10.0f;
}

void CMenusSettingsControls::SyncBindGroupExpanded(bool *pExpanded)
{
	const qm_card_collapse::CState &CollapseState = qm_card_collapse::CurrentState();
	for(int Index = 0; Index < (int)EBindOptionGroup::NUM; ++Index)
	{
		const EBindOptionGroup Group = static_cast<EBindOptionGroup>(Index);
		pExpanded[Index] = !CollapseState.IsCollapsed(BindGroupCardStableId(Group), Group == EBindOptionGroup::CUSTOM);
	}
}

const char *CMenusSettingsControls::BindGroupCardStableId(const EBindOptionGroup Group)
{
	static constexpr const char *s_apStableIds[(int)EBindOptionGroup::NUM] = {
		"deck:controls-movement",
		"deck:controls-weapon",
		"deck:controls-voting",
		"deck:controls-chat",
		"deck:controls-dummy",
		"deck:controls-miscellaneous",
		"deck:controls-custom",
	};
	const int Index = std::clamp((int)Group, 0, (int)EBindOptionGroup::NUM - 1);
	return s_apStableIds[Index];
}

void CMenusSettingsControls::DoSettingsControlsLabel(const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps) const
{
	GameClient()->m_Menus.DoSettingsLabel(CMenus::SETTINGS_CONTROLS, -1, pTextId, pRect, pText, Size, Align, LabelProps);
}

void CMenusSettingsControls::DoSettingsControlsMenuLabel(const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &Props, int MaxWidth) const
{
	GameClient()->m_Menus.DoSettingsMenuLabel(CMenus::SETTINGS_CONTROLS, -1, -1, pTextId, pRect, pText, Size, Align, Props, MaxWidth);
}

int CMenusSettingsControls::DoSettingsControlsCheckBox(const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect) const
{
	return GameClient()->m_Menus.DoSettingsButton_CheckBox(CMenus::SETTINGS_CONTROLS, -1, -1, pId, pTextId, pText, Checked, pRect);
}

bool CMenusSettingsControls::DoSettingsControlsNumericField(const char *pTextId, const void *pId, int *pOption, const CUIRect &Rect, const char *pLabel, int Min, int Max, const IScrollbarScale *pScale, unsigned Flags)
{
	const float BodySize = m_CardMetrics.m_BodySize;
	ui_widget::SNumericFieldOptions Options;
	Options.m_pLabel = pLabel;
	Options.m_pScale = pScale;
	Options.m_Flags = Flags;
	Options.m_FontSize = BodySize;
	Options.m_LabelAlign = TEXTALIGN_ML;
	Options.m_CommitPolicy = (Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE) != 0 ? ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT : ui_widget::EInputCommitPolicy::LIVE;
	if(GameClient()->m_Menus.PrepareSettingsNumericFieldLabel(CMenus::SETTINGS_CONTROLS, -1, -1, pTextId, Rect, pLabel, Flags, Options))
		return false;
	IUiContext Context = GameClient()->m_Menus.SettingsUiContext("settings_controls", BodySize / ui_token::font::BODY);
	return ui_widget::NumericField(Context, GameClient()->m_Menus.GetSettingsNumericFieldState(pId), pId, pOption, Min, Max, Rect, Options);
}

float CMenusSettingsControls::MeasureSettingsBindsHeight(EBindOptionGroup Group) const
{
	float Height = 0.0f;
	for(const CBindOption &BindOption : m_vBindOptions)
	{
		if(BindOption.m_Group != Group)
		{
			continue;
		}
		Height += m_CardMetrics.m_LineHeight * BindOption.m_vCurrentBinds.size() + m_CardMetrics.m_LineSpacing * (BindOption.m_vCurrentBinds.size() - 1) + 4.0f + m_CardMetrics.m_LineSpacing;
	}
	return Height;
}

void CMenusSettingsControls::RenderSettingsBinds(EBindOptionGroup Group, CUIRect View, bool ReadOnly)
{
	for(CBindOption &BindOption : m_vBindOptions)
	{
		if(BindOption.m_Group != Group)
		{
			continue;
		}

		CUIRect KeyReaders;
		View.HSplitTop(m_CardMetrics.m_LineHeight * BindOption.m_vCurrentBinds.size() + m_CardMetrics.m_LineSpacing * (BindOption.m_vCurrentBinds.size() - 1) + 4.0f, &KeyReaders, &View);
		View.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &View);
		if(!ReadOnly && m_pCardScrollRegion != nullptr && !m_pCardScrollRegion->AddRect(KeyReaders) && !m_SearchMatchReveal)
		{
			continue;
		}
		DrawRoundedSurface(Ui(), KeyReaders, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f), ColorRGBA(), 5.0f);
		KeyReaders.Margin(2.0f, &KeyReaders);

		CUIRect Label, AddButton;
		KeyReaders.VSplitLeft(KeyReaders.w / 3.0f, &Label, &KeyReaders);
		KeyReaders.VSplitLeft(5.0f, nullptr, &KeyReaders);
		KeyReaders.VSplitLeft(m_CardMetrics.m_LineHeight, &AddButton, &KeyReaders);
		AddButton.HSplitTop(m_CardMetrics.m_LineHeight, &AddButton, nullptr);
		KeyReaders.VSplitLeft(2.0f, nullptr, &KeyReaders);
		Label.HSplitTop(m_CardMetrics.m_LineHeight, &Label, nullptr);

		const auto SearchMatch = std::find(m_vSearchMatches.begin(), m_vSearchMatches.end(), &BindOption - m_vBindOptions.data());
		const bool SearchMatchSelected = SearchMatch != m_vSearchMatches.end() && m_CurrentSearchMatch == (int)(SearchMatch - m_vSearchMatches.begin());
		if(!ReadOnly && m_pCardScrollRegion != nullptr && SearchMatchSelected && m_SearchMatchReveal)
		{
			m_SearchMatchReveal = false;
			// Scroll to reveal search match
			CUIRect ScrollTarget;
			Label.HMargin(-BIND_REVEAL_MARGIN, &ScrollTarget);
			if(m_pCardScrollRegion != nullptr)
				m_pCardScrollRegion->AddRect(ScrollTarget, true);
		}
		SLabelProperties LabelProps = {.m_MaxWidth = Label.w, .m_EllipsisAtEnd = BindOption.m_Group == EBindOptionGroup::CUSTOM, .m_MinimumFontSize = 9.0f};
		if(SearchMatchSelected)
		{
			LabelProps.SetColor(ColorRGBA(0.1f, 0.1f, 1.0f, 1.0f));
		}
		else if(SearchMatch != m_vSearchMatches.end())
		{
			LabelProps.SetColor(ColorRGBA(0.4f, 0.4f, 0.9f, 1.0f));
		}
		const char *pBindLabel = BindOption.m_Group == EBindOptionGroup::CUSTOM ? BindOption.m_Command.c_str() : Localize(BindOption.m_pLabel);
		const char *pBindTextId = BindOption.m_Group == EBindOptionGroup::CUSTOM ? BindOption.m_Command.c_str() : BindOption.m_pLabel;
		DoSettingsControlsLabel(pBindTextId, &Label, pBindLabel, m_CardMetrics.m_BodySize, TEXTALIGN_ML, LabelProps);
		Ui()->DoButtonLogic(&BindOption.m_TooltipButtonId, 0, &Label, BUTTONFLAG_NONE);
		GameClient()->m_Tooltips.DoToolTip(&BindOption.m_TooltipButtonId, &Label, BindOption.m_Command.c_str());

		for(CBindSlotUiElement &CurrentBind : BindOption.m_vCurrentBinds)
		{
			CUIRect KeyReader;
			KeyReaders.HSplitTop(m_CardMetrics.m_LineHeight, &KeyReader, &KeyReaders);
			KeyReaders.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &KeyReaders);
			const bool ActivateKeyReader = BindOption.m_AddNewBindActivate && CurrentBind.m_Bind == EMPTY_BIND_SLOT;
			if(ReadOnly)
				continue;
			const CKeyBinder::CKeyReaderResult KeyReaderResult = GameClient()->m_KeyBinder.DoKeyReader(
				&CurrentBind.m_KeyReaderButton, &CurrentBind.m_KeyResetButton,
				&KeyReader, CurrentBind.m_Bind, ActivateKeyReader);
			if(ActivateKeyReader)
			{
				BindOption.m_AddNewBindActivate = false;
				// Scroll to reveal activated key reader
				CUIRect ScrollTarget;
				KeyReader.HMargin(-BIND_REVEAL_MARGIN, &ScrollTarget);
				if(m_pCardScrollRegion != nullptr)
					m_pCardScrollRegion->AddRect(ScrollTarget, true);
			}
			if(KeyReaderResult.m_Aborted)
			{
				BindOption.m_AddNewBind = false;
				if(CurrentBind.m_Bind == EMPTY_BIND_SLOT && (&CurrentBind - BindOption.m_vCurrentBinds.data()) > 0)
				{
					CurrentBind.m_ToBeDeleted = true;
					m_BindOptionsDirty = true;
				}
			}
			else if(KeyReaderResult.m_Bind != CurrentBind.m_Bind)
			{
				BindOption.m_AddNewBind = false;
				if(CurrentBind.m_Bind.m_Key != KEY_UNKNOWN || KeyReaderResult.m_Bind.m_Key == KEY_UNKNOWN)
				{
					GameClient()->m_Binds.Bind(CurrentBind.m_Bind.m_Key, "", false, CurrentBind.m_Bind.m_ModifierMask);
				}
				if(KeyReaderResult.m_Bind.m_Key != KEY_UNKNOWN)
				{
					GameClient()->m_Binds.Bind(KeyReaderResult.m_Bind.m_Key, BindOption.m_Command.c_str(), false, KeyReaderResult.m_Bind.m_ModifierMask);
				}
				m_BindOptionsDirty = true;
			}
		}
	}
}

float CMenusSettingsControls::MeasureSettingsMouseHeight() const
{
	// 灵敏度（游戏内）、最小/最大光标距离、灵敏度（界面）共 4 行
	return 4.0f * m_CardMetrics.m_LineHeight + 3.0f * m_CardMetrics.m_LineSpacing;
}

void CMenusSettingsControls::RenderSettingsMouse(CUIRect View)
{
	CUIRect Button;
	View.HSplitTop(m_CardMetrics.m_LineHeight, &Button, &View);
	DoSettingsControlsNumericField("controls-ingame-mouse-sens-label", &g_Config.m_InpMousesens, &g_Config.m_InpMousesens, Button, Localize("Ingame mouse sens."), 1, 500, &CUi::ms_LogarithmicScrollbarScale);

	View.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &View);
	View.HSplitTop(m_CardMetrics.m_LineHeight, &Button, &View);
	DoSettingsControlsNumericField("controls-ingame-mouse-min-distance-label", &g_Config.m_ClMouseMinDistance, &g_Config.m_ClMouseMinDistance, Button, Localize("Minimum cursor distance"), 0, 5000);

	View.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &View);
	View.HSplitTop(m_CardMetrics.m_LineHeight, &Button, &View);
	DoSettingsControlsNumericField("controls-ingame-mouse-max-distance-label", &g_Config.m_ClMouseMaxDistance, &g_Config.m_ClMouseMaxDistance, Button, Localize("Maximum cursor distance"), 0, 5000);

	View.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &View);
	View.HSplitTop(m_CardMetrics.m_LineHeight, &Button, &View);
	DoSettingsControlsNumericField("controls-ui-mouse-sens-label", &g_Config.m_UiMousesens, &g_Config.m_UiMousesens, Button, Localize("UI mouse sens."), 1, 500, &CUi::ms_LogarithmicScrollbarScale);
}

float CMenusSettingsControls::MeasureSettingsJoystickHeight(const float ContentWidth) const
{
	const bool HasJoystick = Input()->NumJoysticks() > 0 && Input()->GetActiveJoystick() != nullptr;
	const int AxisCount = HasJoystick ? Input()->GetActiveJoystick()->GetNumAxes() : 0;
	return ResolveSettingsControllerContentHeight(ContentWidth, g_Config.m_InpControllerEnable != 0, HasJoystick, g_Config.m_InpControllerAbsolute != 0, AxisCount, NUM_JOYSTICK_AXES, m_CardMetrics.m_LineHeight, m_CardMetrics.m_LineSpacing);
}

void CMenusSettingsControls::RenderSettingsJoystick(CUIRect View, bool ReadOnly)
{
	CUIRect Button;
	View.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &View);
	View.HSplitTop(m_CardMetrics.m_LineHeight, &Button, &View);
	const bool WasJoystickEnabled = g_Config.m_InpControllerEnable;
	if(DoSettingsControlsCheckBox(&g_Config.m_InpControllerEnable, "controls-enable-controller", Localize("Enable controller"), g_Config.m_InpControllerEnable, &Button))
	{
		g_Config.m_InpControllerEnable ^= 1;
	}
	if(!WasJoystickEnabled) // Use old value because this was used to allocate the available height
	{
		return;
	}

	const int NumJoysticks = Input()->NumJoysticks();
	if(NumJoysticks > 0)
	{
		// show joystick device selection if more than one available or just the joystick name if there is only one
		{
			CUIRect JoystickDropDown;
			View.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &View);
			View.HSplitTop(m_CardMetrics.m_LineHeight, &JoystickDropDown, &View);
			if(NumJoysticks > 1)
			{
				std::vector<std::string> vJoystickNames;
				std::vector<const char *> vpJoystickNames;
				vJoystickNames.resize(NumJoysticks);
				vpJoystickNames.resize(NumJoysticks);

				for(int i = 0; i < NumJoysticks; ++i)
				{
					char aJoystickName[256];
					str_format(aJoystickName, sizeof(aJoystickName), "%s %d: %s", Localize("Controller"), i, Input()->GetJoystick(i)->GetName());
					vJoystickNames[i] = aJoystickName;
					vpJoystickNames[i] = vJoystickNames[i].c_str();
				}

				const int CurrentJoystick = Input()->GetActiveJoystick()->GetIndex();
				CUi::SDropDownProperties JoystickDropDownProps;
				JoystickDropDownProps.m_pPopupViewport = Ui()->OutermostClipArea();
				const int NewJoystick = GameClient()->m_Menus.DoSettingsDropDown(&JoystickDropDown, CurrentJoystick, vpJoystickNames.data(), vpJoystickNames.size(), m_JoystickDropDownState, JoystickDropDownProps);
				if(NewJoystick != CurrentJoystick)
				{
					Input()->SetActiveJoystick(NewJoystick);
				}
			}
			else
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "%s 0: %s", Localize("Controller"), Input()->GetJoystick(0)->GetName());
				DoSettingsControlsLabel("controls-controller-device-label", &JoystickDropDown, aBuf, m_CardMetrics.m_BodySize, TEXTALIGN_ML);
			}
		}

		const bool WasAbsolute = g_Config.m_InpControllerAbsolute;
		GameClient()->m_Menus.DoSettingsLine_RadioMenu(CMenus::SETTINGS_CONTROLS, -1, -1, View, "controls-ingame-controller-mode-label", Localize("Ingame controller mode"),
			m_vJoystickIngameModeButtonContainers,
			{"controls-ingame-controller-mode-relative", "controls-ingame-controller-mode-absolute"},
			{Localize("Relative", "Ingame controller mode"), Localize("Absolute", "Ingame controller mode")},
			{0, 1},
			g_Config.m_InpControllerAbsolute,
			ResolveSettingsContentMetrics(View.w));

		if(!WasAbsolute) // Use old value because this was used to allocate the available height
		{
			View.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &View);
			View.HSplitTop(m_CardMetrics.m_LineHeight, &Button, &View);
			DoSettingsControlsNumericField("controls-ingame-controller-sens-label", &g_Config.m_InpControllerSens, &g_Config.m_InpControllerSens, Button, Localize("Ingame controller sens."), 1, 500,
				&CUi::ms_LogarithmicScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);
		}

		View.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &View);
		View.HSplitTop(m_CardMetrics.m_LineHeight, &Button, &View);
		DoSettingsControlsNumericField("controls-ui-controller-sens-label", &g_Config.m_UiControllerSens, &g_Config.m_UiControllerSens, Button, Localize("UI controller sens."), 1, 500,
			&CUi::ms_LogarithmicScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);

		View.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &View);
		View.HSplitTop(m_CardMetrics.m_LineHeight, &Button, &View);
		DoSettingsControlsNumericField("controls-controller-jitter-tolerance-label", &g_Config.m_InpControllerTolerance, &g_Config.m_InpControllerTolerance, Button, Localize("Controller jitter tolerance"), 0, 50);

		View.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &View);
		View.h = minimum(View.h, ResolveSettingsControllerAxisPickerHeight(Input()->GetActiveJoystick()->GetNumAxes(), NUM_JOYSTICK_AXES, m_CardMetrics.m_LineHeight, m_CardMetrics.m_LineSpacing));
		if(ReadOnly || m_pCardScrollRegion == nullptr || m_pCardScrollRegion->AddRect(View))
		{
			DrawRoundedSurface(Ui(), View, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f), ColorRGBA(), 5.0f);
			RenderJoystickAxisPicker(View, ReadOnly);
		}
	}
	else
	{
		View.HSplitTop(View.h - m_CardMetrics.m_LineHeight, nullptr, &View);
		View.HSplitTop(m_CardMetrics.m_LineHeight, &Button, &View);
		DoSettingsControlsLabel("controls-no-controller-label", &Button, Localize("No controller found. Plug in a controller."), m_CardMetrics.m_BodySize, TEXTALIGN_ML);
	}
}

void CMenusSettingsControls::RenderJoystickAxisPicker(CUIRect View, bool ReadOnly)
{
	const float AxisWidth = 0.2f * View.w;
	const float StatusWidth = 0.4f * View.w;
	const float AimBindWidth = 90.0f;
	const float SpacingV = (View.w - AxisWidth - StatusWidth - AimBindWidth) / 2.0f;

	CUIRect Row, Axis, Status, AimBind;
	View.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &View);
	View.HSplitTop(m_CardMetrics.m_LineHeight, &Row, &View);
	Row.VSplitLeft(AxisWidth, &Axis, &Row);
	Row.VSplitLeft(SpacingV, nullptr, &Row);
	Row.VSplitLeft(StatusWidth, &Status, &Row);
	Row.VSplitLeft(SpacingV, nullptr, &Row);
	Row.VSplitLeft(AimBindWidth, &AimBind, &Row);

	DoSettingsControlsLabel("controls-axis-header", &Axis, Localize("Axis"), m_CardMetrics.m_BodySize, TEXTALIGN_MC);
	DoSettingsControlsLabel("controls-axis-status-header", &Status, Localize("Status"), m_CardMetrics.m_BodySize, TEXTALIGN_MC);
	DoSettingsControlsLabel("controls-axis-aim-bind-header", &AimBind, Localize("Aim bind"), m_CardMetrics.m_BodySize, TEXTALIGN_MC);

	IInput::IJoystick *pJoystick = Input()->GetActiveJoystick();
	for(int i = 0; i < std::min<int>(pJoystick->GetNumAxes(), NUM_JOYSTICK_AXES); i++)
	{
		View.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &View);
		View.HSplitTop(m_CardMetrics.m_LineHeight, &Row, &View);
		if(!ReadOnly && m_pCardScrollRegion != nullptr && !m_pCardScrollRegion->AddRect(Row))
		{
			continue;
		}
		DrawRoundedSurface(Ui(), Row, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f), ColorRGBA(), 5.0f);
		Row.VSplitLeft(AxisWidth, &Axis, &Row);
		Row.VSplitLeft(SpacingV, nullptr, &Row);
		Row.VSplitLeft(StatusWidth, &Status, &Row);
		Row.VSplitLeft(SpacingV, nullptr, &Row);
		Row.VSplitLeft(AimBindWidth, &AimBind, &Row);

		const bool Active = g_Config.m_InpControllerX == i || g_Config.m_InpControllerY == i;

		// Axis label
		char aLabel[16];
		str_format(aLabel, sizeof(aLabel), "%d", i + 1);
		char aLabelId[32];
		str_format(aLabelId, sizeof(aLabelId), "controls-axis-%d-label", i + 1);
		SLabelProperties LabelProps;
		if(!Active)
		{
			LabelProps.SetColor(ColorRGBA(0.7f, 0.7f, 0.7f, 1.0f));
		}
		DoSettingsControlsLabel(aLabelId, &Axis, aLabel, m_CardMetrics.m_BodySize, TEXTALIGN_MC, LabelProps);

		// Axis status
		Status.HMargin(7.0f, &Status);
		RenderJoystickBar(&Status, (pJoystick->GetAxisValue(i) + 1.0f) / 2.0f, g_Config.m_InpControllerTolerance / 50.0f, Active);

		// Bind to X/Y
		CUIRect AimBindX, AimBindY;
		AimBind.VSplitMid(&AimBindX, &AimBindY);
		if(GameClient()->m_Menus.DoButton_CheckBox(&m_aaJoystickAxisCheckboxIds[i][0], "X", g_Config.m_InpControllerX == i, &AimBindX))
		{
			if(g_Config.m_InpControllerY == i)
				g_Config.m_InpControllerY = g_Config.m_InpControllerX;
			g_Config.m_InpControllerX = i;
		}
		if(GameClient()->m_Menus.DoButton_CheckBox(&m_aaJoystickAxisCheckboxIds[i][1], "Y", g_Config.m_InpControllerY == i, &AimBindY))
		{
			if(g_Config.m_InpControllerX == i)
				g_Config.m_InpControllerX = g_Config.m_InpControllerY;
			g_Config.m_InpControllerY = i;
		}
	}
}

void CMenusSettingsControls::RenderJoystickBar(const CUIRect *pRect, float Current, float Tolerance, bool Active)
{
	CUIRect Handle;
	pRect->VSplitLeft(pRect->h, &Handle, nullptr); // Slider size
	Handle.x += (pRect->w - Handle.w) * Current;

	pRect->Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Active ? 0.25f : 0.125f), IGraphics::CORNER_ALL, pRect->h / 2.0f);

	CUIRect ToleranceArea = *pRect;
	ToleranceArea.w *= Tolerance;
	ToleranceArea.x += (pRect->w - ToleranceArea.w) / 2.0f;
	const ColorRGBA ToleranceColor = Active ? ColorRGBA(0.8f, 0.35f, 0.35f, 1.0f) : ColorRGBA(0.7f, 0.5f, 0.5f, 1.0f);
	ToleranceArea.Draw(ToleranceColor, IGraphics::CORNER_ALL, ToleranceArea.h / 2.0f);

	const ColorRGBA SliderColor = Active ? ColorRGBA(0.95f, 0.95f, 0.95f, 1.0f) : ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f);
	Handle.Draw(SliderColor, IGraphics::CORNER_ALL, Handle.h / 2.0f);
}

void CMenusSettingsControls::PrepareSettingsCards(const SSettingsContentMetrics &Metrics, bool ReadOnly, CScrollRegion *pScrollRegion)
{
	m_CardMetrics = Metrics;
	m_pCardScrollRegion = pScrollRegion;
	SyncBindGroupExpanded(m_aBindGroupExpanded);
	if(!ReadOnly && (m_BindOptionsDirty || GameClient()->m_KeyBinder.IsActive() || m_BindOptionsRevision != GameClient()->m_Binds.Revision()))
	{
		UpdateBindOptions();
		m_BindOptionsDirty = false;
		m_BindOptionsRevision = GameClient()->m_Binds.Revision();
	}
}

uint64_t CMenusSettingsControls::SettingsCardsRevision(bool ReadOnly) const
{
	uint64_t CardLayoutRevision = str_quickhash("controls");
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (ReadOnly ? 1u : 0u);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ m_BindLayoutRevision;
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ str_quickhash(m_FilterInput.GetString());
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (uint64_t)m_vSearchMatches.size();
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (uint64_t)(g_Config.m_InpControllerEnable != 0);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (uint64_t)(g_Config.m_InpControllerAbsolute != 0);
	const int NumJoysticks = Input()->NumJoysticks();
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (uint64_t)maximum(0, NumJoysticks);
	if(NumJoysticks > 0)
	{
		const IInput::IJoystick *pActiveJoystick = Input()->GetActiveJoystick();
		if(pActiveJoystick != nullptr)
		{
			CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (uint64_t)maximum(0, pActiveJoystick->GetIndex());
			CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (uint64_t)maximum(0, pActiveJoystick->GetNumAxes());
		}
	}
	const bool HasControllerJoystick = NumJoysticks > 0 && Input()->GetActiveJoystick() != nullptr;

	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (uint64_t)HasControllerJoystick;
	for(bool Expanded : m_aBindGroupExpanded)
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (Expanded ? 1u : 0u);
	const bool HasCustomBinds = std::any_of(m_vBindOptions.begin(), m_vBindOptions.end(), [](const CBindOption &Option) { return Option.m_Group == EBindOptionGroup::CUSTOM; });
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (HasCustomBinds ? 1u : 0u);

	return CardLayoutRevision;
}

bool CMenusSettingsControls::BuildSettingsCard(const qm_card_catalog::SQmCardBuildContext &Ctx, const char *pStableId, SSettingsCardDefinition &Out)
{
	const auto *pDefault = qm_card_registry::FindByStableId(pStableId);
	if(pDefault == nullptr)
		return false;
	const bool ReadOnly = Ctx.m_ReadOnly;
	Out = {};
	Out.m_Spec = {pDefault->m_pStableId, Localize(pDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pDefault)};
	Out.m_MeasureRevision = SettingsCardsRevision(ReadOnly);
	if(str_comp(pStableId, "deck:controls-mouse") == 0)
	{
		Out.m_Measure = [this](float) { return MeasureSettingsMouseHeight(); };
		Out.m_Render = [this](CUIRect Rect) { RenderSettingsMouse(Rect); };
		return true;
	}
	if(str_comp(pStableId, "deck:controls-controller") == 0)
	{
		Out.m_Measure = [this](float Width) { return MeasureSettingsJoystickHeight(Width); };
		Out.m_Render = [this, ReadOnly](CUIRect Rect) { RenderSettingsJoystick(Rect, ReadOnly); };
		Out.m_PreLayoutInput = [this](CUIRect Content) {
			bool Changed = false;
			CUIRect Button;
			Content.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &Content);
			Content.HSplitTop(m_CardMetrics.m_LineHeight, &Button, &Content);
			const bool WasJoystickEnabled = g_Config.m_InpControllerEnable != 0;
			if(Ui()->DoButtonLogic(&g_Config.m_InpControllerEnable, 0, &Button, BUTTONFLAG_LEFT))
			{
				g_Config.m_InpControllerEnable ^= 1;
				Changed = true;
			}
			if(!WasJoystickEnabled)
				return Changed;

			const IInput::IJoystick *pActiveJoystick = Input()->GetActiveJoystick();
			if(Input()->NumJoysticks() <= 0 || pActiveJoystick == nullptr)
				return Changed;
			Content.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &Content);
			Content.HSplitTop(m_CardMetrics.m_LineHeight, nullptr, &Content);

			const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(Content.w);
			const bool WasAbsolute = g_Config.m_InpControllerAbsolute != 0;
			const SSettingsRadioRowLayout ModeLayout = ResolveSettingsRadioRowLayout(Content, 2, Metrics);
			CUIRect ModeButtons = ModeLayout.m_ButtonsRect;
			Content.HSplitTop(ModeLayout.m_Height, nullptr, &Content);
			const float ButtonWidth = ModeButtons.w / (float)m_vJoystickIngameModeButtonContainers.size();
			for(size_t Index = 0; Index < m_vJoystickIngameModeButtonContainers.size(); ++Index)
			{
				CUIRect ModeButton;
				ModeButtons.VSplitLeft(ButtonWidth, &ModeButton, &ModeButtons);
				if(Ui()->DoButtonLogic(&m_vJoystickIngameModeButtonContainers[Index], Index == (size_t)g_Config.m_InpControllerAbsolute, &ModeButton, BUTTONFLAG_LEFT))
				{
					g_Config.m_InpControllerAbsolute = (int)Index;
					Changed = true;
				}
			}
			if(!WasAbsolute)
			{
				Content.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &Content);
				Content.HSplitTop(m_CardMetrics.m_LineHeight, nullptr, &Content);
			}
			Content.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &Content);
			Content.HSplitTop(m_CardMetrics.m_LineHeight, nullptr, &Content);
			Content.HSplitTop(m_CardMetrics.m_LineSpacing, nullptr, &Content);
			Content.HSplitTop(m_CardMetrics.m_LineHeight, nullptr, &Content);
			return Changed;
		};
		return true;
	}
	const std::pair<EBindOptionGroup, const char *> aBindCards[] = {
		{EBindOptionGroup::MOVEMENT, "deck:controls-movement"}, {EBindOptionGroup::WEAPON, "deck:controls-weapon"},
		{EBindOptionGroup::VOTING, "deck:controls-voting"}, {EBindOptionGroup::CHAT, "deck:controls-chat"},
		{EBindOptionGroup::DUMMY, "deck:controls-dummy"}, {EBindOptionGroup::MISCELLANEOUS, "deck:controls-miscellaneous"}, {EBindOptionGroup::CUSTOM, "deck:controls-custom"}};
	for(const auto &[Group, pId] : aBindCards)
	{
		if(str_comp(pStableId, pId) != 0)
			continue;
		Out.m_Measure = [this, Group](float) { return m_aBindGroupExpanded[(int)Group] ? MeasureSettingsBindsHeight(Group) : 0.0f; };
		Out.m_Render = [this, Group, ReadOnly](CUIRect Rect) { RenderSettingsBinds(Group, Rect, ReadOnly); };
		Out.m_RenderWhenClipped = true;
		Out.m_IsCollapsed = [this, Group] { return !m_aBindGroupExpanded[(int)Group]; };
		if(Group == EBindOptionGroup::CUSTOM)
			Out.m_IsVisible = [this] { return std::any_of(m_vBindOptions.begin(), m_vBindOptions.end(), [](const CBindOption &Option) { return Option.m_Group == EBindOptionGroup::CUSTOM; }); };
		Out.m_PreLayoutHeaderInput = [this, Group](const SSettingsCardFrame &Frame, bool Collapsed) {
			const int Index = (int)Group;
			if(!Ui()->DoButtonLogic(&m_aBindGroupExpandButtons[Index], Collapsed, &Frame.m_HandleRect, BUTTONFLAG_LEFT))
				return false;
			if(!qm_card_collapse::SetCollapsed(BindGroupCardStableId(Group), m_aBindGroupExpanded[Index]))
				return false;
			m_aBindGroupExpanded[Index] = !m_aBindGroupExpanded[Index];
			return true;
		};
		Out.m_HeaderAction = [CardCtx = Ctx.m_UiContext](const SSettingsCardFrame &Frame, bool Collapsed) { RenderSettingsCardCollapseButton(CardCtx, Frame.m_HandleRect, Collapsed); };
		return true;
	}
	return false;
}

namespace qm_card_catalog
{
	uint64_t QmCardRenderHook::PrepareControlsCards(CMenus *pMenus, float ContentWidth, bool ReadOnly, CScrollRegion *pScrollRegion)
	{
		auto &Controls = pMenus->m_MenusSettingsControls;
		Controls.PrepareSettingsCards(ResolveSettingsContentMetrics(ContentWidth), ReadOnly, pScrollRegion);
		return Controls.SettingsCardsRevision(ReadOnly);
	}

	bool QmCardRenderHook::BuildControlsCard(const SQmCardBuildContext &Ctx, const char *pStableId, SSettingsCardDefinition &Out)
	{
		if(Ctx.m_pMenus == nullptr)
			return false;
		auto &Controls = Ctx.m_pMenus->m_MenusSettingsControls;
		Controls.PrepareSettingsCards(Ctx.m_Metrics, Ctx.m_ReadOnly, Ctx.m_pScrollRegion);
		return Controls.BuildSettingsCard(Ctx, pStableId, Out);
	}
}
