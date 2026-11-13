#include "QmCardCatalog.h"

#include <engine/config.h>
#include <engine/input.h>
#include <engine/shared/localization.h>

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/friend_code.h>
#include <game/client/components/qmclient/friend_code_publication.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <array>

struct SQmFriendCodeUiState
{
	std::array<CButtonContainer, 3> m_aButtons;
	CFriendList m_Preview;
	bool m_HasPreview = false;
	int m_Replace = 0;
	qm_friend_code::CImportPublication m_Publication;
	bool m_ConfirmPending = false;
	CUi::SConfirmPopupContext m_Confirm;
	uint64_t m_LastFrame = 0;
	const char *m_pStatus = nullptr;
};

void qm_card_catalog::QmCardRenderHook::RenderFriendCodeControls(CMenus *pMenus, CUIRect &Content, bool ReadOnly)
{
	if(!qm_friend_code::TryCreateUiState(pMenus->m_pFriendCodeUiState))
		return;
	auto &State = *pMenus->m_pFriendCodeUiState;
	const auto Metrics = pMenus->CurrentSettingsContentMetrics();
	const bool Interactive = !ReadOnly && !pMenus->Ui()->RenderOnly() && !pMenus->Ui()->MeasurementOnly() && !pMenus->Ui()->BackgroundRender();
	const uint64_t Frame = pMenus->Ui()->PopupSourceFrame();
	const auto Publish = [&] {
		const bool Saved = State.m_Publication.TryPublish(Frame, Interactive, [pMenus] { pMenus->Client()->ServerBrowserUpdate(); }, [pMenus] { pMenus->FriendlistOnUpdate(); }, [pMenus] { return pMenus->Kernel()->RequestInterface<IConfigManager>()->Save(); });
		State.m_pStatus = Saved ? Localizable("Friends imported and saved.") : Localizable("Friends imported for this session, but saving settings failed.");
	};
	const auto Apply = [&](qm_friend_code::EImportMode Mode) {
		if(!State.m_Publication.TryCommit(State.m_HasPreview, State.m_Replace,
			   [&] { return qm_friend_code::Import(*pMenus->GameClient()->Friends(), State.m_Preview, Mode); }))
		{
			State.m_pStatus = Localizable("Cannot import: invalid data or too many friends or categories.");
			return;
		}
		Publish();
	};
	if(Interactive)
	{
		if(State.m_Publication.Pending())
			Publish();
		if(State.m_ConfirmPending)
		{
			// 离页后不补执行旧确认；同帧的测量与重绘不会消耗结果。
			if(Frame > State.m_LastFrame + 1)
			{
				pMenus->Ui()->ClosePopupMenu(&State.m_Confirm);
				State.m_ConfirmPending = false;
			}
			else if(State.m_Confirm.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
			{
				State.m_ConfirmPending = false;
				Apply(qm_friend_code::EImportMode::REPLACE);
			}
			else if(State.m_Confirm.m_Result == CUi::SConfirmPopupContext::CANCELED || !pMenus->Ui()->IsPopupOpen(&State.m_Confirm))
				State.m_ConfirmPending = false;
			else
				pMenus->Ui()->RefreshPopupMenuSource(&State.m_Confirm, true, Frame);
		}
		State.m_LastFrame = Frame;
	}

	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	const auto Button = [&](int Index, const char *pId, const char *pLabel) {
		bool Clicked = false;
		if(Flow.Control(pId, Metrics.m_ButtonHeight, Row))
		{
			Clicked = pMenus->DoSettingsButton_Menu(CMenus::SETTINGS_QMCLIENT, CMenus::QMCLIENT_SETTINGS_TAB_FUNCTION, CMenus::QMCLIENT_SETTINGS_TAB_FUNCTION, &State.m_aButtons[Index], pId, pLabel, 0, &Row, Metrics);
		}
		Flow.Space(Metrics.m_LineSpacing);
		return Clicked && Interactive && !State.m_ConfirmPending && (Index != 2 || !State.m_Publication.Pending());
	};
	if(Button(0, "friend-code-copy", Localize("Copy friend code")))
	{
		std::string Code;
		CFriendList Captured;
		if(qm_friend_code::TryCapture(*pMenus->GameClient()->Friends(), Captured) && qm_friend_code::Encode(Captured, Code))
		{
			pMenus->Input()->SetClipboardText(Code.c_str());
			State.m_pStatus = Localizable("Friend code copied, including categories, notes and pins.");
		}
		else
			State.m_pStatus = Localizable("Could not encode friend data.");
	}
	if(Button(1, "friend-code-paste", Localize("Paste friend code")))
	{
		State.m_HasPreview = qm_friend_code::Decode(pMenus->Input()->GetClipboardText(), State.m_Preview);
		State.m_Replace = 0;
		State.m_pStatus = State.m_HasPreview ? nullptr : Localizable("Invalid, damaged or unsupported friend code.");
	}
	if(Flow.Control("friend-code-replace", Metrics.m_LineHeight, Row))
		pMenus->RenderQmFunctionCheckbox(&State.m_Replace, "friend-code-replace", Localize("Replace all friends"), &State.m_Replace, &Row, !Interactive || State.m_ConfirmPending);
	Flow.Space(Metrics.m_LineSpacing);
	if(Button(2, "friend-code-import", Localize("Import friend code")))
	{
		if(!State.m_HasPreview)
			State.m_pStatus = Localizable("Paste a friend code first.");
		else if(!State.m_Replace)
			Apply(qm_friend_code::EImportMode::MERGE);
		else
		{
			State.m_Confirm.Reset();
			State.m_Confirm.DefaultButtons();
			State.m_Confirm.m_Tone = CUi::SConfirmPopupContext::ETone::DANGER;
			str_copy(State.m_Confirm.m_aMessage, Localize("Replace your entire friend list, categories, notes and pins with the pasted code?"));
			pMenus->Ui()->ShowPopupConfirm(pMenus->Ui()->MouseX(), pMenus->Ui()->MouseY(), &State.m_Confirm);
			pMenus->Ui()->RefreshPopupMenuSource(&State.m_Confirm, true, State.m_LastFrame);
			State.m_ConfirmPending = true;
		}
	}
	// 用较长的固定说明测量换行空间，状态变化无需重建整个好友卡片。
	const char *pHelp = Localize("Import merges friends and keeps local duplicates by default. Codes contain private names and notes.");
	Flow.Text("friend-code-status", pHelp, 0.0f, Metrics.m_BodySize, Metrics.m_LineSpacing, [pMenus](const char *pText, float Width, float Size) { return pMenus->TextRender()->TextBoundingBox(Size, pText, -1, Width).m_H + Size * 0.25f; }, [&](const CUIRect &Rect, const char *pText) {
			char aPreview[128];
			str_format(aPreview, sizeof(aPreview), Localize("Ready to import: %d friends, %d categories."), static_cast<int>(State.m_Preview.m_vFriends.size()), static_cast<int>(State.m_Preview.m_vCategories.size()));
			if(State.m_pStatus)
				pText = Localize(State.m_pStatus);
			else if(State.m_HasPreview)
				pText = aPreview;
			pMenus->Ui()->DoLabel(&Rect, pText, Metrics.m_BodySize, TEXTALIGN_ML, {.m_MaxWidth = Rect.w, .m_EnableWidthCheck = false}); }, 3 * Metrics.m_LineHeight);
	Flow.Space(Metrics.m_LineSpacing);
}
