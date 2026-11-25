#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FAVORITE_MAP_NOTE_INPUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FAVORITE_MAP_NOTE_INPUT_H

#include "favorite_map_note_edit.h"

#include <game/client/lineinput.h>

class CTClient;
class CUi;
class CUIRect;
struct IUiContext;

class CQmFavoriteMapNoteInput
{
	CQmFavoriteMapNoteEditor m_Editor;
	CLineInputBuffered<256> m_Input;
	std::string m_InitialDraft;

public:
	bool IsEditing() const { return m_Editor.IsEditing(); }
	bool IsEditing(std::string_view Map) const { return m_Editor.IsEditing(Map); }
	void ResetClicks() { m_Editor.ResetClicks(); }
	void PruneIfRemoved(CUi &Ui, const std::set<std::string> &FavoriteMaps);
	bool CancelOnEscape(CUi &Ui);
	void Cancel(CUi &Ui);
	std::optional<SQmFavoriteMapNoteChange> Commit(CUi &Ui);
	std::optional<SQmFavoriteMapNoteChange> Click(CUi &Ui, const void *pId, const CUIRect &Rect, std::string_view Map, const char *pNote);
	bool Render(const IUiContext &Context, const CUIRect &Rect, const void *pRemoveId, const char *pPlaceholder);
};

void QmFinishFavoriteMapNoteFrame(CUi &Ui, CTClient &TClient);

#endif
