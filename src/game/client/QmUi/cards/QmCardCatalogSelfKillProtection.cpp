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

// 自杀保护独立卡片（qm:self_kill_protection）：绑定行 + 绑定用法说明。
// 状态本体是 qm_self_kill_enabled，通过可绑定命令 qm_self_kill_off / qm_self_kill_on
// 手动切换（带 Echo 回显）；本卡只负责渲染，不含决策逻辑。
void CMenus::RenderQmSelfKillProtectionContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	// 卡内直接提供两行绑定控件，交互与绑定卡/功能卡键位行一致（点击改键、Esc 取消、垃圾桶清除）。
	static CButtonContainer s_ReaderButtonSelfKillOff, s_ClearButtonSelfKillOff;
	static CButtonContainer s_ReaderButtonSelfKillOn, s_ClearButtonSelfKillOn;
	if(Flow.Control("self-kill-protection-bind-off", LineHeight, Row))
		RenderQmHudKeyBindRow(Row, s_ReaderButtonSelfKillOff, s_ClearButtonSelfKillOff, Localize("Self-kill off"), "qm_self_kill_off", LineHeight, BodySize, 0.0f, LabelWidth);
	Flow.Space(LineSpacing);
	if(Flow.Control("self-kill-protection-bind-on", LineHeight, Row))
		RenderQmHudKeyBindRow(Row, s_ReaderButtonSelfKillOn, s_ClearButtonSelfKillOn, Localize("Self-kill on"), "qm_self_kill_on", LineHeight, BodySize, 0.0f, LabelWidth);
	Flow.Space(LineSpacing);
	Flow.Text("self-kill-protection-hint", Localize("To prevent accidental kills while racing, use the two bind rows above to toggle. To self-kill, press the enable key first, then press K. Server commands are not affected"), 0.0f, BodySize, LineSpacing, [this](const char *pText, float Width, float FontSize) { return TextRender()->TextBoundingBox(FontSize, pText, -1, Width).m_H + FontSize * 0.25f; }, [this, BodySize](const CUIRect &Hint, const char *pText) {
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
