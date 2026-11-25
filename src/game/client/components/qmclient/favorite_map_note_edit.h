#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FAVORITE_MAP_NOTE_EDIT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FAVORITE_MAP_NOTE_EDIT_H

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

struct SQmFavoriteMapNoteChange
{
	std::string m_Map;
	std::string m_Note;
};

class CQmFavoriteMapNoteEditor
{
	std::string m_EditingMap;
	std::string m_OriginalNote;
	std::string m_LastClickedNoteMap;
	bool m_PendingFocus = false;

public:
	bool IsEditing() const { return !m_EditingMap.empty(); }
	bool IsEditing(std::string_view Map) const { return IsEditing() && m_EditingMap == Map; }
	const std::string &OriginalNote() const { return m_OriginalNote; }

	bool NoteClicked(std::string_view Map, bool DoubleClicked)
	{
		if(DoubleClicked && m_LastClickedNoteMap == Map)
		{
			ResetClicks();
			return true;
		}
		m_LastClickedNoteMap = Map;
		return false;
	}

	void ResetClicks() { m_LastClickedNoteMap.clear(); }

	void BeginEdit(std::string_view Map, const char *pNote)
	{
		m_EditingMap = Map;
		m_OriginalNote = pNote ? pNote : "";
		m_PendingFocus = true;
	}

	bool TakePendingFocus() { return std::exchange(m_PendingFocus, false); }

	std::optional<SQmFavoriteMapNoteChange> Commit(std::string_view Draft)
	{
		std::optional<SQmFavoriteMapNoteChange> Change;
		if(IsEditing() && Draft != m_OriginalNote)
			Change = SQmFavoriteMapNoteChange{m_EditingMap, std::string(Draft)};
		Cancel();
		return Change;
	}

	void Cancel()
	{
		m_EditingMap.clear();
		m_OriginalNote.clear();
		m_PendingFocus = false;
	}

	bool PruneIfRemoved(const std::set<std::string> &FavoriteMaps)
	{
		if(!m_LastClickedNoteMap.empty() && !FavoriteMaps.contains(m_LastClickedNoteMap))
			ResetClicks();
		if(!IsEditing() || FavoriteMaps.contains(m_EditingMap))
			return false;
		Cancel();
		return true;
	}
};

#endif
