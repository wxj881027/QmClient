#include "QmCardCatalogInternal.h"

#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

// 自杀保护独立卡片（qm:self_kill_protection）：开关 + 绑定行 + 自动开启延迟。
// 状态本体是 qm_self_kill_enabled（默认关闭），可用卡内开关或绑定命令
// qm_self_kill_off / qm_self_kill_on 手动切换（带 Echo 回显）。
// 自动开启是可选项：默认 0 分钟 = 永不自动开启，只有玩家显式设了分钟数，
// 才会在进图后到点自动拦截；本卡只负责渲染，不含决策逻辑。
void CMenus::RenderQmSelfKillProtectionContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row, LabelColumn, ControlColumn;
	// 卡内直接提供开关与两行绑定控件，交互与绑定卡/功能卡键位行一致（点击改键、Esc 取消、垃圾桶清除）。
	// 开关显示的是"现在是否真的在拦"，而不只是配置里的默认值：
	// 自动开启把保护打开之后，卡片必须同步显示成勾选状态，否则玩家会以为它没生效。
	static const void *s_EnabledCheckboxId = &s_EnabledCheckboxId;
	if(Flow.Control("self-kill-protection-enabled", LineHeight, Row))
	{
		const int ConfigValue = g_Config.m_QmSelfKillEnabled;
		g_Config.m_QmSelfKillEnabled = GameClient()->m_QmSelfKillProtection.Enabled() ? 1 : 0;
		if(RenderQmFunctionCheckbox(s_EnabledCheckboxId, "self-kill-protection-enabled", Localize("Enable self-kill protection"), &g_Config.m_QmSelfKillEnabled, &Row, PrewarmOnly))
			GameClient()->SetQmSelfKillProtection(g_Config.m_QmSelfKillEnabled != 0, false);
		else
			// 没点就是没改：把临时值换回配置原值，自动开启的状态不会被顺手写进存档。
			g_Config.m_QmSelfKillEnabled = ConfigValue;
	}
	Flow.Space(LineSpacing);

	static CButtonContainer s_ReaderButtonSelfKillOff, s_ClearButtonSelfKillOff;
	static CButtonContainer s_ReaderButtonSelfKillOn, s_ClearButtonSelfKillOn;
	if(Flow.Control("self-kill-protection-bind-off", LineHeight, Row))
		RenderQmHudKeyBindRow(Row, s_ReaderButtonSelfKillOff, s_ClearButtonSelfKillOff, Localize("Self-kill off"), "qm_self_kill_off", LineHeight, BodySize, 0.0f, LabelWidth);
	Flow.Space(LineSpacing);
	if(Flow.Control("self-kill-protection-bind-on", LineHeight, Row))
		RenderQmHudKeyBindRow(Row, s_ReaderButtonSelfKillOn, s_ClearButtonSelfKillOn, Localize("Self-kill on"), "qm_self_kill_on", LineHeight, BodySize, 0.0f, LabelWidth);
	Flow.Space(LineSpacing);

	// 自动开启：0 = 永不自动开启（默认），大于 0 表示进图后第几分钟自动拦下 K 键。
	// 与其它卡的数值行同一套标签 + 滑杆 + 数值输入布局。
	static int s_AutoDelayInputId;
	if(Flow.Control("self-kill-protection-auto-delay", LineHeight, Row))
	{
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "self-kill-protection-auto-delay", &LabelColumn, Localize("Auto-enable after"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_AutoDelayInputId, &Row, &g_Config.m_QmSelfKillAutoDelay, &LabelColumn);
		RenderQmSettingsSliderWithValueInput(&s_AutoDelayInputId, ControlColumn, &g_Config.m_QmSelfKillAutoDelay, 0, 60, "min", PrewarmOnly);
	}
	Flow.Space(LineSpacing);

	Flow.Text("self-kill-protection-hint", Localize("To prevent accidental kills while racing, toggle the switch above or use the two bind rows. To self-kill, enable first, then press K. Auto-enable counts from the moment you join a game; 0 minutes keeps it off. Server commands are not affected"), 0.0f, BodySize, LineSpacing, [this](const char *pText, float Width, float FontSize) { return TextRender()->TextBoundingBox(FontSize, pText, -1, Width).m_H + FontSize * 0.25f; }, [this, BodySize](const CUIRect &Hint, const char *pText) {
			SLabelProperties Props;
			Props.m_MaxWidth = Hint.w;
			Props.m_EllipsisAtEnd = true;
			TextRender()->TextColor(ui_token::color::TEXT_TIP);
			Ui()->DoLabel(&Hint, pText, BodySize, TEXTALIGN_ML, Props);
			TextRender()->TextColor(TextRender()->DefaultTextColor()); });
}

namespace qm_card_catalog
{
	bool BuildSelfKillProtectionCard(const SQmCardBuildContext &Ctx, SSettingsCardDefinition &Out)
	{
		if(Ctx.m_pMenus == nullptr)
			return false;

		CMenus *pMenus = Ctx.m_pMenus;
		const float LineHeight = Ctx.m_Metrics.m_LineHeight;
		const float BodySize = Ctx.m_Metrics.m_BodySize;
		const float LineSpacing = Ctx.m_Metrics.m_LineSpacing;
		const float LabelWidth = Ctx.m_LabelWidth;
		const bool ReadOnly = Ctx.m_ReadOnly;
		const auto Render = [pMenus, LineHeight, BodySize, LineSpacing, LabelWidth](CUIRect &Content, const bool PrewarmOnly) {
			QmCardRenderHook::RenderQmSelfKillProtectionContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
		};

		Out = {};
		Out.m_Spec = {"qm:self_kill_protection", Localize("Client-side self-kill protection"), qm_card_registry::ResolveLocalizedDescription("qm:self_kill_protection")};
		Out.m_Render = [Render, ReadOnly](CUIRect Content) { Render(Content, ReadOnly); };
		Out.m_RenderMeasured = [Render, ReadOnly](CUIRect &Content) { Render(Content, ReadOnly); };
		UseDeclaredModuleContent(Ctx, Out);
		return true;
	}
} // namespace qm_card_catalog
