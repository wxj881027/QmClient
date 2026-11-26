#ifndef GAME_CLIENT_QMUI_AUDIOPACKEDITORINPUT_H
#define GAME_CLIENT_QMUI_AUDIOPACKEDITORINPUT_H

#include <game/client/components/qmclient/audio_pack_candidates.h>
#include <game/client/ui.h>

namespace qm_audio_pack
{
	enum class EEditorList
	{
		CATALOG,
		CANDIDATES,
	};

	class CEditorListFocus
	{
		EEditorList m_Active = EEditorList::CATALOG;

	public:
		void Click(EEditorList List, bool Blocked)
		{
			if(!Blocked)
				m_Active = List;
		}
		bool Active(EEditorList List, bool Blocked) const { return !Blocked && m_Active == List; }
	};

	struct SCandidateListAction
	{
		int m_PreviewIndex = -1;
		bool m_Replace = false;
	};

	inline SCandidateListAction FinishCandidateList(CUiDoubleClickState &Clicks, int &Selection, const SFilteredCandidates &Filtered,
		int SelectedRow, int PreviewRow, bool BodyActivated)
	{
		if(PreviewRow >= 0)
		{
			Clicks.Reset();
			return {Filtered.m_vIndices[PreviewRow], false};
		}
		if(SelectedRow < 0 || SelectedRow >= (int)Filtered.m_vIndices.size())
			return {};
		Selection = Filtered.m_vIndices[SelectedRow];
		return {-1, BodyActivated};
	}
}

#endif
