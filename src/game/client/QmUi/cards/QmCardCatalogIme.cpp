#include "QmCardCatalog.h"
#include "QmColorGradientEditor.h"

#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmImeAppearance.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/key_binder.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/ime/qm_ime_typing.h>
#include <game/client/gameclient.h>
#include <game/client/qm_ime_candidate_popup.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <array>

namespace
{
	struct SImeColorControls
	{
		unsigned *m_pColor;
		unsigned m_DefaultColor;
		char *m_pGradient;
		int m_GradientSize;
		int *m_pType;
		int *m_pAngle;
		int *m_pCenterX;
		int *m_pCenterY;
		int *m_pRange;
		int *m_pReverse;
	};

	class CImePrewarmScope
	{
		CUi *m_pUi;

	public:
		CImePrewarmScope(CUi *pUi, bool Prewarm) :
			m_pUi(Prewarm && pUi != nullptr && !pUi->RenderOnly() ? pUi : nullptr)
		{
			if(m_pUi != nullptr)
				m_pUi->BeginRenderOnly();
		}
		~CImePrewarmScope()
		{
			if(m_pUi != nullptr)
				m_pUi->EndRenderOnly();
		}
	};
}

void CMenus::RenderQmFunctionImeContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	const CImePrewarmScope Prewarm(Ui(), PrewarmOnly || !Flow.Input());
	const bool ReadOnly = Ui()->RenderOnly() || !Flow.Input();
	const auto &Metrics = CurrentSettingsContentMetrics();
	const float RowHeight = std::max(LineHeight, Metrics.m_ButtonHeight);
	{
		CUIRect ToggleRow;
		if(Flow.Control("Auto manage IME while typing", RowHeight, ToggleRow))
			RenderQmFunctionCheckbox(&g_Config.m_QmImeAutoManage, "Auto manage IME while typing", Localize("Auto manage IME while typing"), &g_Config.m_QmImeAutoManage, &ToggleRow, ReadOnly);
		Flow.Space(LineSpacing);
	}
	{
		CUIRect ToggleRow;
		if(Flow.Control("New IME", RowHeight, ToggleRow))
			RenderQmFunctionCheckbox(&g_Config.m_QmNewIme, "New IME", Localize("New IME"), &g_Config.m_QmNewIme, &ToggleRow, ReadOnly);
		Flow.Space(LineSpacing);
	}
	CUIRect Row, Label, Control;
	const auto NextRow = [&](const char *pId) {
		const bool HasRow = Flow.Control(pId, RowHeight, Row);
		Flow.Space(LineSpacing);
		if(HasRow)
			SplitSettingsControlRow(Row, Metrics, &Label, &Control);
		return HasRow;
	};
	const auto DrawLabel = [&](const char *pText) {
		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Label, pText, BodySize, TEXTALIGN_ML, Props);
	};
	if(NextRow("ime-engine"))
	{
		DrawLabel(Localize("IME engine"));
#if defined(CONF_FAMILY_WINDOWS)
		static std::array<CButtonContainer, 2> s_aEngineButtons;
		const char *apEngines[] = {Localize("System input"), Localize("Built-in Pinyin")};
		const int Engine = DoSegmentedChoice(s_aEngineButtons.data(), apEngines, 2, std::clamp(g_Config.m_QmImeEngine, 0, 1), Control, 5.0f, nullptr, BodySize, &g_Config.m_QmImeEngine);
		if(!ReadOnly)
			g_Config.m_QmImeEngine = Engine;
#else
		Ui()->DoLabel(&Control, Localize("System input"), BodySize, TEXTALIGN_ML);
#endif
	}
#if defined(CONF_FAMILY_WINDOWS)
	const char *pEngineHelp = Localize("Built-in input supports Chinese Pinyin and English suggestions with a US keyboard layout on Windows. Use system input for other layouts or input methods.");
#else
	const char *pEngineHelp = Localize("Built-in Pinyin is only available on Windows. This platform uses system input.");
#endif
	Flow.Text("ime-engine-help", pEngineHelp, 0.0f, Metrics.m_SmallSize, LineSpacing, [this](const char *pText, float Width, float Size) { return TextRender()->TextBoundingBox(Size, pText, -1, Width).m_H + Size * 0.25f; }, [this, &Metrics](const CUIRect &Help, const char *pText) {
			SLabelProperties Props;
			Props.m_MaxWidth = Help.w;
			Ui()->DoLabel(&Help, pText, Metrics.m_SmallSize, TEXTALIGN_ML, Props); }, LineHeight);
#if defined(CONF_FAMILY_WINDOWS)
	if(g_Config.m_QmImeEngine != 0)
	{
		DoSettingsToggleGroup(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, Content,
			{
				{&g_Config.m_QmImeDomainWords, "Game domain words", Localize("Game domain words")},
				{&g_Config.m_QmImeLearning, "Learn committed words", Localize("Learn committed words")},
				{&g_Config.m_QmImeEnglishCompletion, "English word suggestions", Localize("English word suggestions")},
				{&g_Config.m_QmImeShiftSwitch, "Tap Shift to switch language", Localize("Tap Shift to switch language")},
			},
			Metrics, !ReadOnly);
		Flow.Text("ime-english-help", Localize("English: Tab accepts the selected suggestion; Space keeps your typed text."), 0.0f, Metrics.m_SmallSize, LineSpacing, [this](const char *pText, float Width, float Size) { return TextRender()->TextBoundingBox(Size, pText, -1, Width).m_H + Size * 0.25f; }, [this, &Metrics](const CUIRect &Help, const char *pText) {
				SLabelProperties Props;
				Props.m_MaxWidth = Help.w;
				Ui()->DoLabel(&Help, pText, Metrics.m_SmallSize, TEXTALIGN_ML, Props); }, LineHeight);
		if(NextRow("ime-switch-shortcut"))
		{
			DrawLabel(Localize("IME language switch key"));
			const auto ToBindModifiers = [](int Mask) {
				return ((Mask & QM_IME_KEYMASK_CONTROL) ? 1 << KeyModifier::CTRL : 0) |
				       ((Mask & QM_IME_KEYMASK_ALT) ? 1 << KeyModifier::ALT : 0) |
				       ((Mask & QM_IME_KEYMASK_SHIFT) ? 1 << KeyModifier::SHIFT : 0) |
				       ((Mask & QM_IME_KEYMASK_GUI) ? 1 << KeyModifier::GUI : 0);
			};
			static CButtonContainer s_SwitchReader, s_SwitchClear;
			const CBindSlot Current(g_Config.m_QmImeSwitchKey, ToBindModifiers(g_Config.m_QmImeSwitchModifiers));
			const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&s_SwitchReader, &s_SwitchClear, &Control, Current, false, BodySize);
			if(!ReadOnly && !Result.m_Aborted && Result.m_Bind != Current)
			{
				const int Mask = Result.m_Bind.m_ModifierMask;
				int Modifiers = ((Mask & (1 << KeyModifier::CTRL)) ? QM_IME_KEYMASK_CONTROL : 0) |
						((Mask & (1 << KeyModifier::ALT)) ? QM_IME_KEYMASK_ALT : 0) |
						((Mask & (1 << KeyModifier::SHIFT)) ? QM_IME_KEYMASK_SHIFT : 0) |
						((Mask & (1 << KeyModifier::GUI)) ? QM_IME_KEYMASK_GUI : 0);
				Modifiers &= ~CQmImeModeShortcut::ModifierOf(Result.m_Bind.m_Key);
				g_Config.m_QmImeSwitchKey = Result.m_Bind.m_Key;
				g_Config.m_QmImeSwitchModifiers = Modifiers;
			}
		}
		if(NextRow("ime-domain-max"))
		{
			DrawLabel(Localize("Domain candidates per page"));
			RenderQmSettingsSliderWithValueInput(&g_Config.m_QmImeDomainMax, Control, &g_Config.m_QmImeDomainMax, 0, 5, "", ReadOnly);
		}
	}
#endif
	// 确认状态属于卡片，分类页与搜索页共用；只在输入轮消费结果。
	static CUi::SConfirmPopupContext s_ClearLearnedConfirm;
	static bool s_ClearLearnedPending = false;
	if(!ReadOnly && s_ClearLearnedPending)
	{
		if(s_ClearLearnedConfirm.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
		{
			s_ClearLearnedPending = false;
			Console()->ExecuteLine("qm_ime_clear_learned");
		}
		else if(s_ClearLearnedConfirm.m_Result == CUi::SConfirmPopupContext::CANCELED)
			s_ClearLearnedPending = false;
	}
	if(NextRow("ime-clear-learned"))
	{
		static CButtonContainer s_ClearLearned;
		if(DoButton_Menu(&s_ClearLearned, Localize("Clear learned words"), 0, &Row) && !ReadOnly)
		{
			s_ClearLearnedConfirm.Reset();
			s_ClearLearnedConfirm.DefaultButtons();
			s_ClearLearnedConfirm.m_Tone = CUi::SConfirmPopupContext::ETone::DANGER;
			str_copy(s_ClearLearnedConfirm.m_aTitle, Localize("Clear learned words"));
			str_copy(s_ClearLearnedConfirm.m_aMessage, Localize("Delete all locally learned input words? Current game words will remain available."));
			Ui()->ShowPopupConfirm(Ui()->MouseX(), Ui()->MouseY(), &s_ClearLearnedConfirm);
			s_ClearLearnedPending = true;
		}
	}
	if(NextRow("ime-appearance-title"))
	{
		Ui()->DoLabel(&Row, Localize("IME appearance"), BodySize, TEXTALIGN_ML);
	}
	CUIRect Preview;
	const bool HasPreview = Flow.Control("ime-preview", RowHeight * 4.0f, Preview);
	Flow.Space(LineSpacing);
	if(HasPreview && !PrewarmOnly && Flow.Drawing())
	{
		DrawRoundedSurface(Ui(), Preview, ColorRGBA(0.5f, 0.5f, 0.5f, 0.12f), ColorRGBA(), 4.0f);
		QmImeRenderStylePreview(GameClient(), Preview);
	}

	// 目录拥有控件状态，分类页与搜索页打开的是同一组编辑器。
	static int s_Role = 0;
	static CUi::SDropDownState s_RoleDropdown;
	static CScrollRegion s_RoleScroll;
	s_RoleDropdown.m_SelectionPopupContext.m_pScrollRegion = &s_RoleScroll;
	const char *apRoles[] = {Localize("IME background color"), Localize("IME text color"), Localize("IME selected text color"), Localize("IME selection background color")};
	if(NextRow("ime-color-role"))
	{
		DrawLabel(Localize("Edit color"));
		const int NewRole = DoSettingsDropDown(&Control, s_Role, apRoles, std::size(apRoles), s_RoleDropdown);
		if(!ReadOnly)
			s_Role = std::clamp(NewRole, 0, 3);
	}
	const std::array<SImeColorControls, 4> aControls = {{
		{&g_Config.m_QmImeBgColor, DefaultConfig::QmImeBgColor, g_Config.m_QmImeBgGradient, sizeof(g_Config.m_QmImeBgGradient),
			&g_Config.m_QmImeBgGradientType, &g_Config.m_QmImeBgGradientAngle, &g_Config.m_QmImeBgGradientCenterX, &g_Config.m_QmImeBgGradientCenterY, &g_Config.m_QmImeBgGradientRange, &g_Config.m_QmImeBgGradientReverse},
		{&g_Config.m_QmImeTextColor, DefaultConfig::QmImeTextColor, g_Config.m_QmImeTextGradient, sizeof(g_Config.m_QmImeTextGradient),
			&g_Config.m_QmImeTextGradientType, &g_Config.m_QmImeTextGradientAngle, &g_Config.m_QmImeTextGradientCenterX, &g_Config.m_QmImeTextGradientCenterY, &g_Config.m_QmImeTextGradientRange, &g_Config.m_QmImeTextGradientReverse},
		{&g_Config.m_QmImeSelectedTextColor, DefaultConfig::QmImeSelectedTextColor, g_Config.m_QmImeSelectedTextGradient, sizeof(g_Config.m_QmImeSelectedTextGradient),
			&g_Config.m_QmImeSelectedTextGradientType, &g_Config.m_QmImeSelectedTextGradientAngle, &g_Config.m_QmImeSelectedTextGradientCenterX, &g_Config.m_QmImeSelectedTextGradientCenterY, &g_Config.m_QmImeSelectedTextGradientRange, &g_Config.m_QmImeSelectedTextGradientReverse},
		{&g_Config.m_QmImeSelectedColor, DefaultConfig::QmImeSelectedColor, g_Config.m_QmImeSelectedGradient, sizeof(g_Config.m_QmImeSelectedGradient),
			&g_Config.m_QmImeSelectedGradientType, &g_Config.m_QmImeSelectedGradientAngle, &g_Config.m_QmImeSelectedGradientCenterX, &g_Config.m_QmImeSelectedGradientCenterY, &g_Config.m_QmImeSelectedGradientRange, &g_Config.m_QmImeSelectedGradientReverse},
	}};
	const auto &Settings = aControls[s_Role];
	static std::array<CButtonContainer, 4> s_aReset, s_aAdd, s_aRemove;
	static std::array<std::array<unsigned, CMessageGradient::MAX_COLORS>, 4> s_aColors;
	static std::array<int, 4> s_aOpacityIds;
	const bool HasAlpha = s_Role != 0;
	const SQmGradientPaletteBinding Palette{Settings.m_pColor, Settings.m_pGradient, Settings.m_GradientSize, HasAlpha};
	if(Flow.Control("ime-color-reset", ResolveSettingsColorRowLayout(Content, Metrics, false).m_ConsumedHeight, Row))
	{
		const auto ColorRow = ResolveSettingsColorRowLayout(Row, Metrics, false);
		CUIRect ColorLabel = ColorRow.m_LabelRect;
		ColorLabel.w = ColorRow.m_ColorButtonRect.x + ColorRow.m_ColorButtonRect.w - ColorLabel.x;
		SLabelProperties ColorLabelProps;
		ColorLabelProps.m_MaxWidth = ColorLabel.w;
		ColorLabelProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&ColorLabel, apRoles[s_Role], BodySize, TEXTALIGN_ML, ColorLabelProps);
		if(DoButton_Menu(&s_aReset[s_Role], Localize("Reset"), 0, &ColorRow.m_ResetButtonRect) && !ReadOnly)
		{
			*Settings.m_pColor = Settings.m_DefaultColor;
			CMessageGradient::Reset(Settings.m_pGradient, Settings.m_GradientSize);
			*Settings.m_pType = 0;
			*Settings.m_pAngle = 0;
			*Settings.m_pCenterX = *Settings.m_pCenterY = 50;
			*Settings.m_pRange = 100;
			*Settings.m_pReverse = 0;
			if(s_Role == 0)
				g_Config.m_QmImeOpacity = DefaultConfig::QmImeOpacity;
		}
	}
	DoColorGradientPalette(&Content, Settings.m_pColor, Settings.m_pGradient, Settings.m_GradientSize,
		&s_aAdd[s_Role], &s_aRemove[s_Role], s_aColors[s_Role].data(), Metrics, false, HasAlpha);

	if(NextRow("ime-opacity"))
	{
		DrawLabel(Localize("Opacity"));
		int Opacity = HasAlpha ? Palette.Opacity() : g_Config.m_QmImeOpacity;
		const int OldOpacity = Opacity;
		RenderQmSettingsSliderWithValueInput(&s_aOpacityIds[s_Role], Control, &Opacity, 0, 100, "%", ReadOnly);
		if(Opacity != OldOpacity && !ReadOnly)
		{
			if(HasAlpha)
				Palette.SetOpacity(Opacity);
			else
				g_Config.m_QmImeOpacity = Opacity;
		}
	}
	static std::array<SQmGradientGeometryState, 4> s_aGeometryStates;
	const SQmGradientGeometryBinding Geometry = {Settings.m_pType, Settings.m_pAngle, Settings.m_pCenterX, Settings.m_pCenterY, Settings.m_pRange, Settings.m_pReverse};
	DoColorGradientGeometry(Content, Geometry, s_aGeometryStates[s_Role], Metrics, LabelWidth);
	if(NextRow("ime-font-size"))
	{
		DrawLabel(Localize("IME font size"));
		RenderQmSettingsSliderWithValueInput(&g_Config.m_QmImeFontSize, Control, &g_Config.m_QmImeFontSize, 75, 200, "%", ReadOnly);
	}
	if(NextRow("ime-reset-all"))
	{
		static CButtonContainer s_ResetAll;
		if(DoButton_Menu(&s_ResetAll, Localize("Reset IME appearance"), 0, &Row) && !ReadOnly)
		{
			for(const auto &Entry : aControls)
			{
				*Entry.m_pColor = Entry.m_DefaultColor;
				CMessageGradient::Reset(Entry.m_pGradient, Entry.m_GradientSize);
				*Entry.m_pType = 0;
				*Entry.m_pAngle = 0;
				*Entry.m_pCenterX = *Entry.m_pCenterY = 50;
				*Entry.m_pRange = 100;
				*Entry.m_pReverse = 0;
			}
			g_Config.m_QmImeOpacity = DefaultConfig::QmImeOpacity;
			g_Config.m_QmImeFontSize = DefaultConfig::QmImeFontSize;
		}
	}
}
