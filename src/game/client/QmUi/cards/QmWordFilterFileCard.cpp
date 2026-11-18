#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/components/chat.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

void CMenus::RenderQmWordFilterFileContent(CUIRect &Content, bool Streamer, float LineHeight, float LineSpacing, float BodySize)
{
	CSettingsContentLayoutFlow Flow(Content);

	// 计数与状态说明行取小字档；测量与绘制共用同一个字号变量，不再借正文字号。
	const float SmallSize = CurrentSettingsContentMetrics().m_SmallSize;
	static CLineInputBuffered<1024> s_aPathInputs[2];
	static CButtonContainer s_aReloadButtons[2];
	const int Index = Streamer ? 1 : 0;
	auto &Input = s_aPathInputs[Index];
	char *pPath = Streamer ? g_Config.m_QmStreamerWordsFile : g_Config.m_QmBlockWordsFile;
	if(Flow.Input() && !Ui()->RenderOnly() && !Input.IsActive() && str_comp(Input.GetString(), pPath) != 0)
		Input.Set(pPath);
	CUIRect Row;
	// 行距由行自己声明：声明阶段记到上一行末尾，行后不再补 Space。
	if(Flow.Control("word-filter-file-label", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
		Ui()->DoLabel(&Row, Localize("Word list file"), BodySize, TEXTALIGN_ML);
	IUiContext Context = SettingsUiContext(Streamer ? "streamer_word_file" : "chat_word_file", CurrentSettingsContentMetrics().m_UiScale);
	if(Flow.Control("word-filter-file-path", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)) && ui_widget::InputField(Context, &Input, Row, "", BodySize) && Flow.Input() && !Ui()->RenderOnly())
	{
		str_copy(pPath, Input.GetString(), sizeof(g_Config.m_QmBlockWordsFile));
		GameClient()->m_Chat.ReloadWordFilterFile(Streamer, false);
	}
	if(Flow.Control("word-filter-file-reload", LineHeight, Row, ui_token::settings::RowGap(LineSpacing)))
	{
		CUIRect Reload, Count;
		// 重新加载是图标类动作：方形按钮与行高同高，右侧留给词数说明。
		Row.VSplitLeft(std::min(Row.h, Row.w), &Reload, &Count);
		if(Ui()->DoButton_QmIcon(&s_aReloadButtons[Index], EQmIcon::ARROW_ROTATE_RIGHT, FontIcons::FONT_ICON_ARROW_ROTATE_RIGHT, 0, &Reload, BUTTONFLAG_LEFT) != 0 && Flow.Input() && !Ui()->RenderOnly())
			GameClient()->m_Chat.ReloadWordFilterFile(Streamer);
		GameClient()->m_Tooltips.DoToolTip(&s_aReloadButtons[Index], &Reload, Localize("Reload"));
		const auto &File = GameClient()->m_Chat.WordFilterFile(Streamer);
		char aCount[128];
		str_format(aCount, sizeof(aCount), Localize("%d words loaded"), static_cast<int>(File.Words().size()));
		Count.VMargin(5.0f, &Count);
		Ui()->DoLabel(&Count, aCount, SmallSize, TEXTALIGN_ML);
	}
	// Flow.Text 只有行后间距、没有「行前间距」参数：状态行之上这一档仍由上一行补，
	// 与其它含 Flow.Text 的卡片同一种写法。
	Flow.Space(ui_token::settings::RowGap(LineSpacing));
	const auto &File = GameClient()->m_Chat.WordFilterFile(Streamer);
	const char *pStatus = File.Error().empty() ? Localize("UTF-8 text, one word per line (max 2 MiB).") : Localize(File.Error().c_str());
	Flow.Text("word-filter-file-status", pStatus, 0.0f, SmallSize, ui_token::settings::RowGap(LineSpacing), [this](const char *pText, float Width, float Size) { return TextRender()->TextBoundingBox(Size, pText, -1, Width).m_H; }, [this, SmallSize](const CUIRect &Rect, const char *pText) { Ui()->DoLabel(&Rect, pText, SmallSize, TEXTALIGN_ML, {.m_MaxWidth = Rect.w}); }, LineHeight);
}
