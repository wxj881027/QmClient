#include "QmTree.h"
#include "QmVoteMenu.h"
#include "UiButtons.h"
#include "UiForms.h"
#include "UiSurface.h"

#include <engine/client.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/components/voting.h>
#include <game/client/gameclient.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>

void CMenus::RenderServerVoteList(CUIRect MainView)
{
	auto &List = m_QmVoteMenu.m_ServerList;
	auto &SearchInput = m_QmVoteMenu.m_ServerSearch;
	auto &Voting = GameClient()->m_Voting;

	const ColorRGBA PanelColor = MenuPanelColor();
	MainView.Draw(PanelColor, IGraphics::CORNER_ALL, 10.0f);
	CUiScopedSurfaceText SurfaceText(TextRender(), PanelColor);
	const ColorRGBA Ink = ResolveUiSurfaceForeground(SurfaceText.Surface());
	MainView.Margin(12.0f, &MainView);
	CUIRect Search, Bottom;
	MainView.HSplitTop(28.0f, &Search, &MainView);
	MainView.HSplitTop(8.0f, nullptr, &MainView);
	MainView.HSplitBottom(28.0f, &MainView, &Bottom);
	MainView.HSplitBottom(8.0f, &MainView, nullptr);

	IUiContext Ctx = ui_widget::ControlContext(Ui());
	Ctx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	Ctx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	Ctx.m_ScopeHash = MakeUiScopeHash("qm_server_vote_search");
	ui_widget::SInputFieldOptions InputOptions;
	InputOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
	InputOptions.m_Clearable = true;
	InputOptions.m_FontSize = 13.0f;
	InputOptions.m_pPlaceholder = Localize("Search");
	InputOptions.m_SearchHotkeyEnabled = !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive();
	if(m_ControlPageOpening)
	{
		m_ControlPageOpening = false;
		Ui()->SetActiveItem(&SearchInput);
		SearchInput.SelectAll();
	}
	const bool Searching = ui_widget::InputField(Ctx, &SearchInput, Search, InputOptions).m_Changed;
	if(List.NeedsUpdate(Voting.OptionsRevision(), SearchInput.GetString()))
	{
		// 行文本由列表持有，重建前释放旧行的交互 ID，保留输入框焦点。
		for(const auto &Description : List.Options())
		{
			if(Ui()->HotItem() == &Description)
				Ui()->SetHotItem(nullptr);
			if(Ui()->ActiveItem() == &Description)
				Ui()->SetActiveItem(nullptr);
		}
		List.Update(Voting.FirstOption(), Voting.OptionsRevision(), SearchInput.GetString());
	}

	auto Label = [&](CUIRect Rect, const char *pText, int Align = TEXTALIGN_ML) {
		SLabelProperties Props;
		Props.m_DisallowNewline = true;
		Props.m_MaxWidth = Rect.w;
		Props.m_StopAtEnd = true;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Rect, pText, 13.0f, Align, Props);
	};
	static CListBox s_Options;
	s_Options.SetItemColors(Ink.WithAlpha(0.12f), Ink.WithAlpha(0.08f), Ink.WithAlpha(0.05f));
	s_Options.SetActive(!Ui()->IsPopupOpen() && !SearchInput.IsActive() && !m_CallvoteReasonInput.IsActive());
	if(Searching)
		s_Options.ScrollToSelected();
	s_Options.DoStart(24.0f, (int)List.Visible().size(), 1, 3, List.SelectedRow(), &MainView, false);
	for(const int Index : List.Visible())
	{
		const auto &Description = List.Options()[Index];
		const CListboxItem Item = s_Options.DoNextItem(&Description);
		if(!Item.m_Visible)
			continue;
		auto Blur = Item.SuppressGaussianBlur();
		CUIRect Text = Item.m_Rect;
		Text.VMargin(8.0f, &Text);
		Label(Text, Description.c_str());
		GameClient()->m_Tooltips.DoToolTip(&Description, &Text, Description.c_str());
	}
	List.Select(s_Options.DoEnd());
	const bool Activated = s_Options.WasItemActivated();
	if(List.Visible().empty())
		Label(MainView, Voting.IsReceivingOptions() ? Localize("Loading…") : Localize("No results"), TEXTALIGN_MC);

	CUIRect CallButton, ForceButton, Reason;
	Bottom.VSplitRight(std::min(112.0f, Bottom.w * 0.3f), &Bottom, &CallButton);
	Bottom.VSplitRight(8.0f, &Bottom, nullptr);
	const bool CanForce = Client()->RconAuthed();
	if(CanForce)
	{
		Bottom.VSplitRight(std::min(112.0f, Bottom.w * 0.4f), &Bottom, &ForceButton);
		Bottom.VSplitRight(8.0f, &Bottom, nullptr);
	}
	Bottom.VSplitRight(std::min(240.0f, Bottom.w * 0.6f), &Bottom, &Reason);
	Bottom.VSplitRight(8.0f, &Bottom, nullptr);
	InputOptions = {};
	InputOptions.m_pPlaceholder = Localize("Reason (optional)");
	InputOptions.m_FontSize = 12.0f;
	Ctx.m_ScopeHash = MakeUiScopeHash("qm_server_vote_reason");
	if(Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed())
	{
		Ui()->SetActiveItem(&m_CallvoteReasonInput);
		m_CallvoteReasonInput.SelectAll();
	}
	ui_widget::InputField(Ctx, &m_CallvoteReasonInput, Reason, InputOptions);
	const int Option = List.SelectedOption(Voting.OptionsRevision());
	const bool CanCall = Option >= 0 && !Voting.IsReceivingOptions();
	if(Voting.IsReceivingOptions())
		Label(Bottom, Localize("Loading…"));
	else if(Option >= 0)
		Label(Bottom, List.Options()[Option].c_str());
	static CButtonContainer s_Call, s_Force;
	const bool Force = CanForce && ui_widget::SecondaryButton(Ctx, &s_Force, Localize("Force vote"), ForceButton, !CanCall);
	const bool Call = ui_widget::PrimaryButton(Ctx, &s_Call, Localize("Call vote"), CallButton, !CanCall);
	// 模式服可用 option 打开子菜单，由服务器决定当前是否允许执行。
	if(CanCall && (Call || Activated || Force))
	{
		Voting.ClearUnfinishedMapVoteChain();
		Voting.CallvoteOption(Option, m_CallvoteReasonInput.GetString(), Force);
		m_CallvoteReasonInput.Clear();
		if(g_Config.m_UiCloseWindowAfterChangingSetting)
			SetActive(false);
	}
}
