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
#include <game/client/components/qmclient/practice_menu.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <iterator>

// 快速练习独立卡片（qm:fast_practice）：主世界影子开关 + 从属的不透明度滑杆 +
// HUD 计时开关 + 快捷菜单全部条目的绑定行。
// 卡片只读写配置与绑定表，不持有练习状态：分支世界的推进、计时与影子绘制都在
// CFastPractice 里，设置页关闭时也不会改变任何练习行为。
namespace
{
	// 绑定行与菜单条目共用同一份清单（qm_practice_menu::s_aFastPracticeEntries），
	// 卡片不另外维护第二份命令表，新增条目时绑定行自动跟上。
	constexpr int FAST_PRACTICE_ENTRY_COUNT = (int)std::size(qm_practice_menu::s_aFastPracticeEntries);
}

void CMenus::RenderQmFastPracticeContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;

	// 主控行：主世界影子是分支世界之外的另一条时间线，服务器眼中的自己停在
	// 开练习那一刻。关掉它可以从画面里彻底去掉这个“另一个我”。
	static const void *s_ShowMainWorldCheckboxId = &s_ShowMainWorldCheckboxId;
	if(Flow.Control("fast-practice-show-main-world", LineHeight, Row))
		RenderQmFunctionCheckbox(s_ShowMainWorldCheckboxId, "fast-practice-show-main-world", Localize("Show main-world ghost"), &g_Config.m_QmFastPracticeShowMainWorld, &Row, PrewarmOnly);

	// 同一开关的按键入口：控制台 toggle 直接改同一个配置项，跑图时不必回设置页。
	static CButtonContainer s_ReaderButtonMainWorld, s_ClearButtonMainWorld;
	if(Flow.Control("fast-practice-bind-main-world", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		RenderQmHudKeyBindRow(Row, s_ReaderButtonMainWorld, s_ClearButtonMainWorld, Localize("Toggle main-world ghost"), "toggle qm_fast_practice_show_main_world 0 1", LineHeight, BodySize, 0.0f, LabelWidth);

	Flow.Space(ui_token::settings::RowGap(LineSpacing));
	// 约定 A：不透明度只在影子可见时存在，隐藏时不占高、不可交互。
	static SettingsDependentRows s_MainWorldAlphaRows;
	if(const auto MainWorldAlphaRows = s_MainWorldAlphaRows.Reveal(Ui(), Flow, g_Config.m_QmFastPracticeShowMainWorld != 0, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
	{
		static int s_MainWorldAlphaInputId;
		CUIRect LabelColumn, ControlColumn;
		if(Flow.Control("fast-practice-main-world-alpha", LineHeight, Row))
		{
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "fast-practice-main-world-alpha", &LabelColumn, Localize("Main-world ghost opacity"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
			GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_MainWorldAlphaInputId, &Row, &g_Config.m_QmFastPracticeMainWorldAlpha, &LabelColumn);
			RenderQmSettingsSliderWithValueInput(&s_MainWorldAlphaInputId, ControlColumn, &g_Config.m_QmFastPracticeMainWorldAlpha, 0, 100, "%", PrewarmOnly);
		}
		Flow.Space(ui_token::settings::RowGap(LineSpacing));
	}

	// 计时与 HUD 上那一行共用 qm_fast_practice_timer：卡片只管开关，时间本身由
	// 分支世界推进的 tick 累计。
	static const void *s_TimerCheckboxId = &s_TimerCheckboxId;
	if(Flow.Control("fast-practice-timer", LineHeight, Row))
		RenderQmFunctionCheckbox(s_TimerCheckboxId, "fast-practice-timer", Localize("Show practice timer"), &g_Config.m_QmFastPracticeTimer, &Row, PrewarmOnly);

	// 绑定行：按钮容器按条目序号固定分配，条目增减时序号与 id 一起变化，
	// 不会把旧行的改键状态挂到别的命令上。
	static CButtonContainer s_aReaderButtons[FAST_PRACTICE_ENTRY_COUNT];
	static CButtonContainer s_aClearButtons[FAST_PRACTICE_ENTRY_COUNT];
	for(int EntryIndex = 0; EntryIndex < FAST_PRACTICE_ENTRY_COUNT; ++EntryIndex)
	{
		const auto &Entry = qm_practice_menu::s_aFastPracticeEntries[EntryIndex];
		char aRowId[64];
		str_format(aRowId, sizeof(aRowId), "fast-practice-bind-%d", EntryIndex);
		if(!Flow.Control(aRowId, LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
			continue;
		RenderQmHudKeyBindRow(Row, s_aReaderButtons[EntryIndex], s_aClearButtons[EntryIndex], Localize(Entry.m_pName), Entry.m_pCommand, LineHeight, BodySize, 0.0f, LabelWidth);
	}
	Flow.Space(ui_token::settings::RowGap(LineSpacing));

	// 说明行统一取小字档，与自杀保护卡同一套测量/绘制写法。
	const float HintSize = CurrentSettingsContentMetrics().m_SmallSize;
	Flow.Text("fast-practice-hint", Localize("Fast practice runs a private copy of the map, so the server still sees you where you entered. The ghost above is that server-side you; other players are controlled by \"Show others\". Bind rows send the same commands as the practice menu, so a bound key never needs the chat box"), 0.0f, HintSize, ui_token::settings::RowGap(LineSpacing), [this](const char *pText, float Width, float FontSize) { return TextRender()->TextBoundingBox(FontSize, pText, -1, Width).m_H + FontSize * 0.25f; }, [this, HintSize](const CUIRect &Hint, const char *pText) {
		SLabelProperties Props;
		Props.m_MaxWidth = Hint.w;
		Props.m_EllipsisAtEnd = true;
		TextRender()->TextColor(ui_token::color::TEXT_TIP);
		Ui()->DoLabel(&Hint, pText, HintSize, TEXTALIGN_ML, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor()); });
}

namespace qm_card_catalog
{
	bool BuildFastPracticeCard(const SQmCardBuildContext &Ctx, SSettingsCardDefinition &Out)
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
			QmCardRenderHook::RenderQmFastPracticeContent(pMenus, Content, LineHeight, BodySize, LineSpacing, LabelWidth, PrewarmOnly);
		};

		Out = {};
		Out.m_Spec = {"qm:fast_practice", Localize("Fast practice"), qm_card_registry::ResolveLocalizedDescription("qm:fast_practice")};
		Out.m_Render = [Render, ReadOnly](CUIRect Content) { Render(Content, ReadOnly); };
		Out.m_RenderMeasured = [Render, ReadOnly](CUIRect &Content) { Render(Content, ReadOnly); };
		UseDeclaredModuleContent(Ctx, Out);
		return true;
	}
} // namespace qm_card_catalog
