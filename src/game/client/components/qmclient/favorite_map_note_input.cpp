#include "favorite_map_note_input.h"

#include <game/client/QmUi/UiForms.h>
#include <game/client/ui.h>

void CQmFavoriteMapNoteInput::PruneIfRemoved(CUi &Ui, const std::set<std::string> &FavoriteMaps)
{
	if(m_Editor.PruneIfRemoved(FavoriteMaps))
		Ui.ReleaseActiveTextInput(&m_Input);
}

bool CQmFavoriteMapNoteInput::CancelOnEscape(CUi &Ui)
{
	if(!IsEditing() || Ui.RenderOnly() || Ui.IsPopupOpen() || !Ui.ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		return false;
	Cancel(Ui);
	return true;
}

void CQmFavoriteMapNoteInput::Cancel(CUi &Ui)
{
	m_Editor.Cancel();
	Ui.ReleaseActiveTextInput(&m_Input);
}

std::optional<SQmFavoriteMapNoteChange> CQmFavoriteMapNoteInput::Commit(CUi &Ui)
{
	if(!IsEditing())
		return std::nullopt;
	// 手工或旧版存档里的备注可能超过输入容量。打开后原样提交时保留存档里的完整文本。
	const std::string_view Draft = m_Input.GetString();
	auto Change = m_Editor.Commit(Draft == m_InitialDraft ? std::string_view(m_Editor.OriginalNote()) : Draft);
	Ui.ReleaseActiveTextInput(&m_Input);
	return Change;
}

std::optional<SQmFavoriteMapNoteChange> CQmFavoriteMapNoteInput::Click(CUi &Ui, const void *pId, const CUIRect &Rect, std::string_view Map, const char *pNote)
{
	if(!Ui.DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_LEFT | BUTTONFLAG_CURRENT_HIT) ||
		!m_Editor.NoteClicked(Map, Ui.DoDoubleClickLogic(pId)))
		return std::nullopt;
	auto Change = Commit(Ui);
	m_Editor.BeginEdit(Map, pNote);
	m_Input.Set(pNote ? pNote : "");
	m_InitialDraft = m_Input.GetString();
	m_Input.SetScrollOffset(0.0f);
	m_Input.SetScrollOffsetChange(0.0f);
	return Change;
}

bool CQmFavoriteMapNoteInput::Render(const IUiContext &Context, const CUIRect &Rect, const void *pRemoveId, const char *pPlaceholder)
{
	CUi &Ui = *Context.m_pUi;
	const bool ClearCaptured = Ui.IsActiveItem(m_Input.GetClearButtonId());
	if(!Ui.RenderOnly() && !Ui.MouseButton(0) && m_Editor.TakePendingFocus())
	{
		// 按钮在第二次松手时确认。此时设置最后活动项，编辑框可在同帧接收焦点。
		Ui.SetActiveItem(&m_Input);
		m_Input.Activate(EInputPriority::UI);
		m_Input.SelectAll();
	}

	ui_widget::SInputFieldOptions Options;
	Options.m_pPlaceholder = pPlaceholder;
	Options.m_Clearable = true;
	Options.m_FontSize = 11.0f;
	const auto Result = ui_widget::InputField(Context, &m_Input, Rect, Options);
	if(Ui.RenderOnly())
		return false;
	if(Result.m_Submitted)
	{
		Ui.ConsumeHotkey(CUi::HOTKEY_ENTER);
		return true;
	}
	if(ClearCaptured && Result.m_Cleared)
		m_Input.Activate(EInputPriority::UI);
	// 清空和取消收藏都在松手时确认，按住时失焦不能先提交。
	return !m_Input.IsActive() && !Ui.IsActiveItem(m_Input.GetClearButtonId()) &&
	       !(pRemoveId != nullptr && Ui.IsActiveItem(pRemoveId));
}
