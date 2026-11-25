#ifndef GAME_CLIENT_QMUI_CARDS_QMWARLISTCARDMODEL_H
#define GAME_CLIENT_QMUI_CARDS_QMWARLISTCARDMODEL_H

#include <game/client/components/qmclient/warlist_model.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace qm_warlist_card
{
	enum class EMatch
	{
		PLAYER,
		CLAN,
	};

	enum class ESource
	{
		ONLINE,
		FRIENDS,
		SELF,
		DUMMY,
	};

	constexpr bool HasPlayerSearch(ESource Source)
	{
		return Source == ESource::ONLINE || Source == ESource::FRIENDS;
	}

	inline bool MatchesCandidateFilter(ESource Source, const char *pName, const char *pClan, const char *pSearch)
	{
		return !HasPlayerSearch(Source) || pSearch[0] == '\0' || str_find_nocase(pName, pSearch) || str_find_nocase(pClan, pSearch);
	}

	struct SIdentity
	{
		EMatch m_Match;
		const char *m_pText;

		const char *Name() const { return m_Match == EMatch::PLAYER ? m_pText : ""; }
		const char *Clan() const { return m_Match == EMatch::CLAN ? m_pText : ""; }
		bool Valid() const { return m_pText[0] != '\0'; }
	};

	inline SIdentity CandidateIdentity(EMatch Match, const char *pName, const char *pClan)
	{
		return {Match, Match == EMatch::PLAYER ? pName : pClan};
	}

	inline SIdentity EntryIdentity(const CWarEntry &Entry)
	{
		// 同时带两个字段的历史条目按模型的实际匹配顺序回填。
		return CandidateIdentity(Entry.m_aName[0] == '\0' ? EMatch::CLAN : EMatch::PLAYER, Entry.m_aName, Entry.m_aClan);
	}

	inline bool MatchesEntryFilter(const CWarEntry &Entry, const char *pSearch, const CWarType *pGroup)
	{
		return (pGroup == nullptr || Entry.m_pWarType == pGroup) &&
		       (pSearch[0] == '\0' || str_find_nocase(EntryIdentity(Entry).m_pText, pSearch) ||
			       str_find_nocase(Entry.m_aReason, pSearch) || str_find_nocase(Entry.m_pWarType->m_aWarName, pSearch));
	}

	inline int GroupIndex(const CQmWarListModel &Model, const CWarType *pGroup)
	{
		const auto It = std::find(Model.m_WarTypes.begin(), Model.m_WarTypes.end(), pGroup);
		return It == Model.m_WarTypes.end() ? -1 : static_cast<int>(It - Model.m_WarTypes.begin());
	}

	inline CWarType *TargetGroup(const CQmWarListModel &Model, const CWarType *pPreferred)
	{
		int Index = GroupIndex(Model, pPreferred);
		if(Index < 0)
			Index = GroupIndex(Model, Model.FindActiveBuiltinWarType(WARLIST_BUILTIN_GROUP_ENEMY));
		if(Index < 0)
			Index = Model.m_WarTypes.size() > 1 ? 1 : 0;
		return Model.m_WarTypes.empty() ? nullptr : Model.m_WarTypes[Index];
	}

	class CEntryForm
	{
		int m_Index = -1;
		uint64_t m_Revision = 0;
		EMatch m_Match = EMatch::PLAYER;

	public:
		char m_aName[MAX_NAME_LENGTH] = "";
		char m_aClan[MAX_CLAN_LENGTH] = "";
		char m_aReason[MAX_WARLIST_REASON_LENGTH] = "";
		CWarType *m_pGroup = nullptr;

		int Selected(const CQmWarListModel &Model) const
		{
			return m_Revision == Model.Revision() && m_Index >= 0 && static_cast<size_t>(m_Index) < Model.m_vWarEntries.size() ? m_Index : -1;
		}
		EMatch Match() const { return m_Match; }
		SIdentity Identity() const { return CandidateIdentity(m_Match, m_aName, m_aClan); }

		void Exit()
		{
			m_Index = -1;
			m_Match = EMatch::PLAYER;
			m_aName[0] = m_aClan[0] = m_aReason[0] = '\0';
			m_pGroup = nullptr;
		}

		bool Synchronize(const CQmWarListModel &Model)
		{
			if(m_Index < 0 || Selected(Model) >= 0)
				return false;
			Exit();
			return true;
		}

		void ChangeMatch(EMatch Match)
		{
			if(m_Match == Match)
				return;
			m_Match = Match;
			m_aName[0] = m_aClan[0] = '\0';
		}

		void ToggleSelection(const CQmWarListModel &Model, int Index)
		{
			if(Index < 0 || static_cast<size_t>(Index) >= Model.m_vWarEntries.size())
				return;
			if(Index == Selected(Model))
			{
				Exit();
				return;
			}
			const auto &Entry = Model.m_vWarEntries[Index];
			const SIdentity Key = EntryIdentity(Entry);
			m_Index = Index;
			m_Revision = Model.Revision();
			m_Match = Key.m_Match;
			str_copy(m_aName, Key.Name());
			str_copy(m_aClan, Key.Clan());
			str_copy(m_aReason, Entry.m_aReason);
			m_pGroup = Entry.m_pWarType;
		}

		bool Prefill(const CQmWarListModel &Model, SIdentity Key, CWarType *pGroup)
		{
			Synchronize(Model);
			if(m_Index >= 0 || !Key.Valid())
				return false;
			m_Match = Key.m_Match;
			str_copy(m_aName, Key.Name());
			str_copy(m_aClan, Key.Clan());
			m_pGroup = pGroup;
			return true;
		}

		bool Submit(CQmWarListModel &Model)
		{
			// 过期编辑不能变成一次隐式新增。
			if(m_Index >= 0 && Selected(Model) < 0)
				return false;
			const SIdentity Key = Identity();
			CWarType *pGroup = TargetGroup(Model, m_pGroup);
			if(!Key.Valid() || pGroup == nullptr)
				return false;
			if(m_Index >= 0)
			{
				Model.UpdateWarEntry(m_Index, Key.Name(), Key.Clan(), m_aReason, pGroup);
				m_Revision = Model.Revision();
				return true;
			}
			if(!Model.AddWarEntry(Key.Name(), Key.Clan(), m_aReason, pGroup->m_aWarName))
				return false;
			m_aName[0] = m_aClan[0] = m_aReason[0] = '\0';
			return true;
		}

		// 群组修改不会移动条目。删除条目后把仍在编辑的索引移回正确位置。
		void AcceptRevision(const CQmWarListModel &Model, int PreviousSelection, int RemovedIndex = -1)
		{
			if(PreviousSelection < 0)
				return;
			if(PreviousSelection == RemovedIndex)
				Exit();
			else
			{
				m_Index = PreviousSelection - (RemovedIndex >= 0 && RemovedIndex < PreviousSelection ? 1 : 0);
				m_Revision = Model.Revision();
			}
		}
	};

	enum ESection
	{
		QUICK_ADD,
		GROUPS,
		ENTRIES,
		EDITOR,
		RULES,
		SECTION_COUNT,
	};

	struct SColumns
	{
		int m_Count;
		float m_Width;
		float m_Gap;

		int Column(ESection Section) const { return m_Count == 2 && Section >= ENTRIES ? 1 : 0; }

		float Place(const CUIRect &Block, const std::array<float, SECTION_COUNT> &aHeights, std::array<CUIRect, SECTION_COUNT> &aSections) const
		{
			std::array<float, 2> aY = {Block.y, Block.y};
			for(int i = 0; i < SECTION_COUNT; ++i)
			{
				const int Col = Column(static_cast<ESection>(i));
				aSections[i] = {Block.x + Col * (m_Width + m_Gap), aY[Col], m_Width, aHeights[i]};
				aY[Col] += aHeights[i] + m_Gap;
			}
			return std::max(0.0f, std::max(aY[0], aY[1]) - Block.y - m_Gap);
		}
	};

	inline SColumns ResolveColumns(float Width, float TwoColumnMinWidth, float Gap)
	{
		const int Columns = Width >= TwoColumnMinWidth ? 2 : 1;
		return {Columns, std::max(0.0f, (Width - (Columns - 1) * Gap) / Columns), Gap};
	}
} // namespace qm_warlist_card

#endif
