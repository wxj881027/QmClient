#include "QmCardCatalogInternal.h"
#include "QmCardCatalogSkinMetrics.h"

#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/SettingsCardContentBridge.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

// 两张皮肤卡共用标准卡片外观，构造、测量和预布局输入均由本模块负责。
namespace qm_card_catalog
{
	bool BuildSkinCard(const SQmCardBuildContext &Ctx, const qm_module::EQmModuleId Id, SSettingsCardDefinition &Out)
	{
		using qm_module::EQmModuleId;
		CMenus *pMenus = Ctx.m_pMenus;
		const SSettingsContentMetrics Metrics = Ctx.m_Metrics;
		const float LineHeight = Metrics.m_LineHeight;
		const float BodySize = Metrics.m_BodySize;
		const float LineSpacing = Metrics.m_LineSpacing;
		const float LabelWidth = Ctx.m_LabelWidth;
		const bool ReadOnly = Ctx.m_ReadOnly;

		switch(Id)
		{
		case EQmModuleId::SkinAppearance:
			MakeModuleCard(
				Ctx,
				Id,
				"qm:skin_appearance",
				"Tee appearance",
				"Configure Tee appearance and skins",
				[pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { QmCardRenderHook::RenderQmVisualSkinAppearanceContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); },
				Out);
			UseDeclaredModuleContent(Ctx, Out);
			return true;
		case EQmModuleId::SkinTransition:
			MakeModuleCard(
				Ctx,
				Id,
				"qm:skin_transition",
				"Skin transition animation",
				"Configure hammer skin steal and skin transition animations",
				[pMenus, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly](CUIRect &Content) { QmCardRenderHook::RenderQmVisualSkinTransitionContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, ReadOnly); },
				Out);
			UseDeclaredModuleContent(Ctx, Out);
			return true;
		default:
			return false;
		}
	}
} // namespace qm_card_catalog

// 保留菜单内容助手，通过 QmCardRenderHook 桥接供分类页与搜索页复用。
void CMenus::RenderQmVisualSkinAppearanceContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	const SSettingsContentMetrics Metrics = CurrentSettingsContentMetrics();
	CSettingsContentLayoutFlow Flow(Content);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmSkinOutlineLocal, "Skin outline for self and dummy", Localize("Skin outline for self and dummy"), &g_Config.m_QmSkinOutlineLocal);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmSkinOutlineOthers, "Skin outline for other players", Localize("Skin outline for other players"), &g_Config.m_QmSkinOutlineOthers);
	CUIRect HueToggle;
	if(Flow.Control("Cycle custom Tee hue", LineHeight, HueToggle))
	{
		if(DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, &g_Config.m_QmCycleTeeHue, "Cycle custom Tee hue", Localize("Cycle custom Tee hue"), g_Config.m_QmCycleTeeHue, &HueToggle) && Flow.Input() && !Ui()->RenderOnly())
			g_Config.m_QmCycleTeeHue ^= 1;
		char aHueTooltip[512];
		str_format(aHueTooltip, sizeof(aHueTooltip), "%s\n%s", Localize("Only affects custom Tee colors."), Localize("When TClient rainbow Tee is enabled, this feature has no effect."));
		GameClient()->m_Tooltips.DoToolTip(&g_Config.m_QmCycleTeeHue, &HueToggle, aHueTooltip);
	}
	Flow.Space(LineSpacing);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmCycleTeeHueDummy, "Also apply to dummy", Localize("Also apply to dummy"), &g_Config.m_QmCycleTeeHueDummy);
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmEmoticonShadow, "Emoticon shadow", Localize("Emoticon shadow"), &g_Config.m_QmEmoticonShadow);
	if(!qm_card_catalog::QmCardRenderHook::Personalization(this, Content, "qm:skin_appearance", Metrics, PrewarmOnly))
		return;

	CUIRect Details;
	if(!Flow.Control("skin-appearance-details", ResolveQmVisualSkinAppearanceHeight(Metrics, Content.w), Details))
		return;
	const CSettingsContentLayoutScope InternalGeometry;
	const SSettingsSkinAppearanceLayout Layout = ResolveSettingsSkinAppearanceLayout(Details, Metrics);
	CUIRect Outline = Layout.m_Outline;
	CUIRect Hue = Layout.m_Hue;
	CUIRect Shadow = Layout.m_Shadow;
	const auto Slider = [&](CUIRect &Group, const char *pId, const char *pText, const void *pSliderId, int *pValue, int Min, int Max, const char *pSuffix) {
		CUIRect Row, Label, Control;
		ConsumeSettingsControlRow(Group, CurrentSettingsContentMetrics(), &Row);
		Group.HSplitTop(LineSpacing, nullptr, &Group);
		SplitSettingsControlRow(Row, Metrics, &Label, &Control);
		SLabelProperties Props;
		Props.m_DisallowNewline = true;
		Props.m_StopAtEnd = true;
		Props.m_MinimumFontSize = 6.0f;
		Props.m_MaxWidth = Label.w;
		RenderQmVisualLabel(pId, &Label, pText, BodySize, TEXTALIGN_ML, Props);
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(pSliderId, &Row, pValue, &Label);
		RenderQmSettingsSliderWithValueInput(pSliderId, Control, pValue, Min, Max, pSuffix, PrewarmOnly);
	};
	static CButtonContainer s_OutlineColor;
	DoLine_ColorPicker(&s_OutlineColor, Metrics, &Outline, Localize("Skin outline color"), &g_Config.m_QmSkinOutlineColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmSkinOutlineColor)), false);
	static int s_WidthId, s_AlphaId;
	Slider(Outline, "qmclient-skin-outline-width", Localize("Skin outline width"), &s_WidthId, &g_Config.m_QmSkinOutlineWidth, 1, 6, "");
	Slider(Outline, "qmclient-skin-outline-opacity", Localize("Skin outline opacity"), &s_AlphaId, &g_Config.m_QmSkinOutlineAlpha, 0, 100, "%");

	static int s_HueSpeedId;
	int DisabledSpeed = g_Config.m_QmCycleTeeHueSpeed;
	if(!g_Config.m_QmCycleTeeHue)
		TextRender()->TextColor(ColorRGBA(0.8f, 0.8f, 0.8f, 0.55f));
	Slider(Hue, "qmclient-cycle-tee-hue-speed", Localize("Hue speed"), &s_HueSpeedId, g_Config.m_QmCycleTeeHue ? &g_Config.m_QmCycleTeeHueSpeed : &DisabledSpeed, 0, 360, "°/s");
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	static int s_ProjectileDurationId;
	Slider(Shadow, "qmclient-emoticon-projectile-duration", Localize("Emoticon projectile duration"), &s_ProjectileDurationId, &g_Config.m_QmEmoticonProjectileDuration, 1, 10, " s");
}
void CMenus::RenderQmVisualSkinTransitionContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row, LabelColumn, ControlColumn;
	RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHammerSwapSkin, "Hammer skin steal", Localize("Hammer skin steal"), &g_Config.m_QmHammerSwapSkin);

	const bool HasToggle = Flow.Control("skin-transition-enabled", LineHeight, Row);
	Flow.Space(LineSpacing);
	if(HasToggle && DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_VISUAL, QMCLIENT_SETTINGS_TAB_VISUAL, &g_Config.m_QmSkinChangeTransition, "Skin transition animation", Localize("Skin transition animation"), g_Config.m_QmSkinChangeTransition, &Row))
		g_Config.m_QmSkinChangeTransition ^= 1;

	if(!g_Config.m_QmSkinChangeTransition)
		return;

	if(!qm_card_catalog::QmCardRenderHook::Personalization(this, Content, "qm:skin_transition", CurrentSettingsContentMetrics(), PrewarmOnly))
		return;

	auto RenderDropDown = [&](const char *pTextId, const char *pText, int *pValue, int MaxValue, const char **ppNames, int NumNames, CUi::SDropDownState &State, CScrollRegion &ScrollRegion) {
		const bool HasRow = Flow.Control(pTextId, LineHeight, Row);
		Flow.Space(LineSpacing);
		if(!HasRow)
			return;
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		RenderQmVisualLabel(pTextId, &LabelColumn, pText, BodySize);
		State.m_SelectionPopupContext.m_pScrollRegion = &ScrollRegion;
		const int NewValue = DoSettingsDropDown(&ControlColumn, std::clamp(*pValue, 0, MaxValue), ppNames, NumNames, State, {}, pValue, nullptr, &Row);
		if(*pValue != NewValue)
			*pValue = NewValue;
	};
	static CUi::SDropDownState s_SkinTransitionTypeDropDownState;
	static CScrollRegion s_SkinTransitionTypeDropDownScrollRegion;
	const char *apSkinTransitionTypeNames[] = {Localize("Afterimage pop"), Localize("Smooth fade"), Localize("Slide left"), Localize("Spin pop"), Localize("Brightness shift"), Localize("Glitch"), Localize("Elastic")};
	RenderDropDown("qmclient-skin-transition-type", Localize("Skin transition type"), &g_Config.m_QmSkinChangeTransitionType, 6, apSkinTransitionTypeNames, std::size(apSkinTransitionTypeNames), s_SkinTransitionTypeDropDownState, s_SkinTransitionTypeDropDownScrollRegion);
	static CUi::SDropDownState s_SkinTransitionScopeDropDownState;
	static CScrollRegion s_SkinTransitionScopeDropDownScrollRegion;
	static std::vector<const char *> s_SkinTransitionScopeDropDownNames;
	s_SkinTransitionScopeDropDownNames = {Localize("Self only"), Localize("Local"), Localize("All players")};
	RenderDropDown("qmclient-skin-transition-range", Localize("Animation range"), &g_Config.m_QmSkinChangeTransitionScope, 2, s_SkinTransitionScopeDropDownNames.data(), (int)s_SkinTransitionScopeDropDownNames.size(), s_SkinTransitionScopeDropDownState, s_SkinTransitionScopeDropDownScrollRegion);

	if(Flow.Control("skin-transition-duration", LineHeight, Row))
	{
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		SLabelProperties DurationLabelProps;
		DurationLabelProps.m_DisallowNewline = true;
		DurationLabelProps.m_StopAtEnd = true;
		DurationLabelProps.m_MinimumFontSize = 6.0f;
		RenderQmVisualLabel("qmclient-skin-transition-duration", &LabelColumn, Localize("Skin transition duration"), BodySize, TEXTALIGN_ML, DurationLabelProps);
		static int s_QmSkinChangeTransitionMsInputId;
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_QmSkinChangeTransitionMsInputId, &Row, &g_Config.m_QmSkinChangeTransitionMs, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(&s_QmSkinChangeTransitionMsInputId, ControlColumn, &g_Config.m_QmSkinChangeTransitionMs, 0, 2000, "ms", PrewarmOnly);
	}
	Flow.Space(LineSpacing);

	static CUi::SDropDownState s_SkinTransitionEasingDropDownState;
	static CScrollRegion s_SkinTransitionEasingDropDownScrollRegion;
	const char *apSkinTransitionEasingNames[] = {Localize("Ease out cubic"), Localize("Elastic back"), Localize("Linear"), Localize("Ease in out quad")};
	RenderDropDown("qmclient-skin-transition-easing", Localize("Skin transition easing"), &g_Config.m_QmSkinChangeTransitionEasing, 3, apSkinTransitionEasingNames, std::size(apSkinTransitionEasingNames), s_SkinTransitionEasingDropDownState, s_SkinTransitionEasingDropDownScrollRegion);

	if(Flow.Control("skin-transition-intensity", LineHeight, Row))
	{
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		RenderQmVisualLabel("qmclient-skin-transition-intensity", &LabelColumn, Localize("Skin transition intensity"), BodySize);
		static int s_QmSkinChangeTransitionIntensityInputId;
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_QmSkinChangeTransitionIntensityInputId, &Row, &g_Config.m_QmSkinChangeTransitionIntensity, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(&s_QmSkinChangeTransitionIntensityInputId, ControlColumn, &g_Config.m_QmSkinChangeTransitionIntensity, 0, 300, "%", PrewarmOnly);
	}
	Flow.Space(LineSpacing);
}
