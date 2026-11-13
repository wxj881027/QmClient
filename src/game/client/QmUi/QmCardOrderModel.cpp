#include "QmCardOrderModel.h"

#include <base/system.h>

#include <algorithm>
#include <utility>

namespace qm_card_order
{
	namespace
	{
		bool FormatColumn(const int Column, std::string &Out)
		{
			switch(Column)
			{
			case 0:
				Out = "full";
				return true;
			case 1:
				Out = "left";
				return true;
			case 2:
				Out = "right";
				return true;
			default:
				if(Column < 0)
					return false;
				Out = std::to_string(Column);
				return true;
			}
		}

		bool ParseColumn(const char *pColumn, bool AllowNumericColumn, int *pOutColumn)
		{
			if(pColumn == nullptr || pOutColumn == nullptr)
				return false;
			if(str_comp(pColumn, "full") == 0)
			{
				*pOutColumn = 0;
				return true;
			}
			if(str_comp(pColumn, "left") == 0)
			{
				*pOutColumn = 1;
				return true;
			}
			if(str_comp(pColumn, "right") == 0)
			{
				*pOutColumn = 2;
				return true;
			}
			if(!AllowNumericColumn)
				return false;
			int Column = 0;
			if(!str_toint(pColumn, &Column))
				return false;
			if(Column < 0)
				return false;
			*pOutColumn = Column;
			return true;
		}

		bool SameTab(const char *pA, const char *pB)
		{
			const char *pSafeA = pA != nullptr ? pA : "";
			const char *pSafeB = pB != nullptr ? pB : "";
			return str_comp(pSafeA, pSafeB) == 0;
		}

		int FindStableId(const std::vector<const char *> &vStableIds, const std::string &Id)
		{
			for(size_t Index = 0; Index < vStableIds.size(); ++Index)
			{
				if(vStableIds[Index] != nullptr && Id == vStableIds[Index])
					return static_cast<int>(Index);
			}
			return -1;
		}

		// 迁移只接受目标卡的完整、可逆序列化字段。CModel::Parse 是普通配置加载的容错入口，
		// 会修正负 order 并忽略多余字段，不能直接用于迁移判定。
		bool HasStrictKnownEntries(const char *pSerialized, const std::vector<const char *> &vStableIds)
		{
			if(pSerialized == nullptr)
				return false;
			std::vector<bool> vSeen(vStableIds.size(), false);
			const char *pTokenStart = pSerialized;
			while(*pTokenStart != '\0')
			{
				while(*pTokenStart == ';')
					++pTokenStart;
				if(*pTokenStart == '\0')
					break;
				const char *pTokenEnd = pTokenStart;
				while(*pTokenEnd != '\0' && *pTokenEnd != ';')
					++pTokenEnd;
				const std::string Token(pTokenStart, pTokenEnd - pTokenStart);
				pTokenStart = pTokenEnd;

				const bool PipeFormat = Token.find('|') != std::string::npos;
				const size_t FirstSeparator = PipeFormat ? Token.find('|') : Token.rfind(':');
				if(FirstSeparator == std::string::npos)
					continue;
				const std::string Id = PipeFormat ? Token.substr(0, FirstSeparator) : Token.substr(0, Token.rfind(':', FirstSeparator - 1));
				const int StableIdIndex = FindStableId(vStableIds, Id);
				if(StableIdIndex < 0)
					continue;

				std::string ColumnText;
				std::string OrderText;
				if(PipeFormat)
				{
					const size_t ColumnSeparator = Token.find('|', FirstSeparator + 1);
					const size_t OrderSeparator = ColumnSeparator != std::string::npos ? Token.find('|', ColumnSeparator + 1) : std::string::npos;
					if(ColumnSeparator == std::string::npos || OrderSeparator == std::string::npos || Token.find('|', OrderSeparator + 1) != std::string::npos ||
						FirstSeparator == 0 || ColumnSeparator == FirstSeparator + 1 || OrderSeparator == ColumnSeparator + 1 || OrderSeparator + 1 >= Token.size())
						return false;
					ColumnText = Token.substr(ColumnSeparator + 1, OrderSeparator - ColumnSeparator - 1);
					OrderText = Token.substr(OrderSeparator + 1);
				}
				else
				{
					const size_t OrderSeparator = FirstSeparator;
					const size_t ColumnSeparator = OrderSeparator > 0 ? Token.rfind(':', OrderSeparator - 1) : std::string::npos;
					if(ColumnSeparator == std::string::npos || ColumnSeparator == 0 || OrderSeparator == ColumnSeparator + 1 || OrderSeparator + 1 >= Token.size())
						return false;
					ColumnText = Token.substr(ColumnSeparator + 1, OrderSeparator - ColumnSeparator - 1);
					OrderText = Token.substr(OrderSeparator + 1);
				}

				int Column = 0;
				int Order = 0;
				if(vSeen[StableIdIndex] || !ParseColumn(ColumnText.c_str(), true, &Column) || !str_toint(OrderText.c_str(), &Order) || Order < 0)
					return false;
				vSeen[StableIdIndex] = true;
			}
			return true;
		}
	}

	bool SerializeMergedReplacingPrefix(const char *pExistingGlobalOrder, const char *pStableIdPrefix, const std::vector<SEntry> &vReplacementEntries, char *pOut, int OutSize)
	{
		if(pOut == nullptr || OutSize <= 0 || pStableIdPrefix == nullptr)
			return false;
		pOut[0] = '\0';

		std::string Serialized;
		const auto AppendToken = [&](const char *pToken) {
			if(pToken == nullptr || pToken[0] == '\0')
				return;
			if(!Serialized.empty())
				Serialized.push_back(';');
			Serialized.append(pToken);
		};
		if(pExistingGlobalOrder != nullptr && pExistingGlobalOrder[0] != '\0')
		{
			const int PrefixLength = str_length(pStableIdPrefix);
			const char *pTokenStart = pExistingGlobalOrder;
			while(*pTokenStart != '\0')
			{
				while(*pTokenStart == ';')
					++pTokenStart;
				if(*pTokenStart == '\0')
					break;
				const char *pTokenEnd = pTokenStart;
				while(*pTokenEnd != '\0' && *pTokenEnd != ';')
					++pTokenEnd;
				const char *pStableIdEnd = pTokenStart;
				while(pStableIdEnd < pTokenEnd && *pStableIdEnd != '|')
					++pStableIdEnd;
				const size_t StableIdLength = static_cast<size_t>(pStableIdEnd - pTokenStart);
				const bool MatchesPrefix = StableIdLength >= static_cast<size_t>(PrefixLength) && str_comp_num(pTokenStart, pStableIdPrefix, PrefixLength) == 0;
				if(!MatchesPrefix)
				{
					if(!Serialized.empty())
						Serialized.push_back(';');
					Serialized.append(pTokenStart, pTokenEnd - pTokenStart);
				}
				pTokenStart = pTokenEnd;
			}
		}

		for(const SEntry &Entry : vReplacementEntries)
		{
			if(Entry.m_pStableId == nullptr)
				continue;
			std::string Column;
			if(!FormatColumn(Entry.m_Column, Column))
				return false;
			std::string SerializedEntry = Entry.m_pStableId;
			SerializedEntry.push_back('|');
			SerializedEntry.append(Entry.m_pDefaultTab != nullptr ? Entry.m_pDefaultTab : "");
			SerializedEntry.push_back('|');
			SerializedEntry.append(Column);
			SerializedEntry.push_back('|');
			SerializedEntry.append(std::to_string(Entry.m_OrderInColumn));
			AppendToken(SerializedEntry.c_str());
		}

		if(!Serialized.empty())
			Serialized.push_back(';');
		if(static_cast<int>(Serialized.size()) >= OutSize)
			return false;
		str_copy(pOut, Serialized.c_str(), OutSize);
		return true;
	}

	void CModel::SetEntries(std::vector<SEntry> Entries)
	{
		m_vOwnedTabs.clear();
		m_vEntries.clear();
		m_vEntries.reserve(Entries.size());
		for(SEntry Entry : Entries)
		{
			if(Entry.m_pDefaultTab != nullptr)
			{
				m_vOwnedTabs.emplace_back(Entry.m_pDefaultTab);
				Entry.m_pDefaultTab = m_vOwnedTabs.back().c_str();
			}
			m_vEntries.push_back(Entry);
		}
		m_Dirty = true;
		++m_LayoutRevision;
		BuildStateIndex(); // 维护 stableId→index 注册表（让位 lerp O(1) 地基）
	}

	void CModel::SwapContent(CModel &Other) noexcept
	{
		if(this == &Other)
			return;
		m_vEntries.swap(Other.m_vEntries);
		m_vOwnedTabs.swap(Other.m_vOwnedTabs);
		m_StableIdToState.swap(Other.m_StableIdToState);
		std::swap(m_Dirty, Other.m_Dirty);
		++m_LayoutRevision;
		++m_StateIndexRevision;
		++Other.m_LayoutRevision;
		++Other.m_StateIndexRevision;
	}

	int CModel::FindByStableId(const char *pStableId) const
	{
		return StateIndexForStableId(pStableId);
	}

	void CModel::Move(const char *pStableId, int ToColumn, int ToOrder)
	{
		const int Idx = FindByStableId(pStableId);
		if(Idx < 0)
			return;
		const char *pTab = m_vEntries[Idx].m_pDefaultTab;
		const int FromColumn = m_vEntries[Idx].m_Column;
		m_vEntries[Idx].m_Column = ToColumn;
		// 重建目标列 order（erase + insert 语义）
		std::vector<int> vOthers;
		for(size_t i = 0; i < m_vEntries.size(); ++i)
		{
			if((int)i == Idx || m_vEntries[i].m_Column != ToColumn || !SameTab(m_vEntries[i].m_pDefaultTab, pTab))
				continue;
			vOthers.push_back((int)i);
		}
		std::stable_sort(vOthers.begin(), vOthers.end(), [&](int a, int b) {
			return m_vEntries[a].m_OrderInColumn < m_vEntries[b].m_OrderInColumn;
		});
		ToOrder = std::clamp(ToOrder, 0, (int)vOthers.size());
		vOthers.insert(vOthers.begin() + ToOrder, Idx);
		for(int o = 0; o < (int)vOthers.size(); ++o)
			m_vEntries[vOthers[o]].m_OrderInColumn = o;
		if(FromColumn != ToColumn)
		{
			std::vector<int> vSource;
			for(size_t i = 0; i < m_vEntries.size(); ++i)
			{
				if((int)i == Idx || m_vEntries[i].m_Column != FromColumn || !SameTab(m_vEntries[i].m_pDefaultTab, pTab))
					continue;
				vSource.push_back((int)i);
			}
			std::stable_sort(vSource.begin(), vSource.end(), [&](int a, int b) {
				return m_vEntries[a].m_OrderInColumn < m_vEntries[b].m_OrderInColumn;
			});
			for(int o = 0; o < (int)vSource.size(); ++o)
				m_vEntries[vSource[o]].m_OrderInColumn = o;
		}
		m_Dirty = true;
		++m_LayoutRevision;
		// Move 仅改 column/order，不改 stableId 集合与 vector 位置，state index 注册表无需重建
	}

	void CModel::MoveToTab(const char *pStableId, const char *pToTab, int ToColumn, int ToOrder)
	{
		const int Idx = FindByStableId(pStableId);
		if(Idx < 0 || pToTab == nullptr)
			return;
		const char *pFromTab = m_vEntries[Idx].m_pDefaultTab;
		const int FromColumn = m_vEntries[Idx].m_Column;
		m_vOwnedTabs.emplace_back(pToTab);
		m_vEntries[Idx].m_pDefaultTab = m_vOwnedTabs.back().c_str();
		m_vEntries[Idx].m_Column = ToColumn;
		// 重建目标 tab+column order（跨页组件编辑器 erase + insert 语义）
		std::vector<int> vOthers;
		for(int i : ColumnIndices(pToTab, ToColumn))
		{
			if(i != Idx)
				vOthers.push_back(i);
		}
		ToOrder = std::clamp(ToOrder, 0, (int)vOthers.size());
		vOthers.insert(vOthers.begin() + ToOrder, Idx);
		for(int o = 0; o < (int)vOthers.size(); ++o)
			m_vEntries[vOthers[o]].m_OrderInColumn = o;
		if(!SameTab(pFromTab, pToTab) || FromColumn != ToColumn)
		{
			std::vector<int> vSource;
			for(size_t i = 0; i < m_vEntries.size(); ++i)
			{
				if((int)i == Idx || m_vEntries[i].m_Column != FromColumn || !SameTab(m_vEntries[i].m_pDefaultTab, pFromTab))
					continue;
				vSource.push_back((int)i);
			}
			std::stable_sort(vSource.begin(), vSource.end(), [&](int a, int b) {
				return m_vEntries[a].m_OrderInColumn < m_vEntries[b].m_OrderInColumn;
			});
			for(int o = 0; o < (int)vSource.size(); ++o)
				m_vEntries[vSource[o]].m_OrderInColumn = o;
		}
		m_Dirty = true;
		++m_LayoutRevision;
		// MoveToTab 仅改 tab/column/order，不改 stableId 集合与 vector 位置，state index 注册表无需重建
	}

	void CModel::NormalizeColumns()
	{
		std::vector<int> vIdx(m_vEntries.size());
		for(size_t i = 0; i < m_vEntries.size(); ++i)
			vIdx[i] = (int)i;
		std::stable_sort(vIdx.begin(), vIdx.end(), [&](int a, int b) {
			const char *pTabA = m_vEntries[a].m_pDefaultTab != nullptr ? m_vEntries[a].m_pDefaultTab : "";
			const char *pTabB = m_vEntries[b].m_pDefaultTab != nullptr ? m_vEntries[b].m_pDefaultTab : "";
			const int TabCmp = str_comp(pTabA, pTabB);
			if(TabCmp != 0)
				return TabCmp < 0;
			if(m_vEntries[a].m_Column != m_vEntries[b].m_Column)
				return m_vEntries[a].m_Column < m_vEntries[b].m_Column;
			return m_vEntries[a].m_OrderInColumn < m_vEntries[b].m_OrderInColumn;
		});
		const char *pCurTab = nullptr;
		int CurCol = -1;
		int CurOrder = 0;
		bool Changed = false;
		for(int i : vIdx)
		{
			const char *pEntryTab = m_vEntries[i].m_pDefaultTab != nullptr ? m_vEntries[i].m_pDefaultTab : "";
			if(pCurTab == nullptr || str_comp(pEntryTab, pCurTab) != 0 || m_vEntries[i].m_Column != CurCol)
			{
				pCurTab = pEntryTab;
				CurCol = m_vEntries[i].m_Column;
				CurOrder = 0;
			}
			if(m_vEntries[i].m_OrderInColumn != CurOrder)
			{
				m_vEntries[i].m_OrderInColumn = CurOrder;
				Changed = true;
			}
			CurOrder++;
		}
		if(Changed)
		{
			m_Dirty = true;
			++m_LayoutRevision;
		}
	}

	std::vector<int> CModel::ColumnIndices(int Column) const
	{
		std::vector<int> v;
		for(size_t i = 0; i < m_vEntries.size(); ++i)
		{
			if(m_vEntries[i].m_Column == Column)
				v.push_back((int)i);
		}
		std::stable_sort(v.begin(), v.end(), [&](int a, int b) {
			return m_vEntries[a].m_OrderInColumn < m_vEntries[b].m_OrderInColumn;
		});
		return v;
	}

	std::vector<int> CModel::ColumnIndices(const char *pTab, int Column) const
	{
		std::vector<int> v;
		for(size_t i = 0; i < m_vEntries.size(); ++i)
		{
			const SEntry &E = m_vEntries[i];
			if(E.m_Column != Column)
				continue;
			// tab 为可变位置维度：仅当卡有归属 tab 且与查询 tab 一致时命中（tab=nullptr 的卡不归属任何页）
			if(E.m_pDefaultTab == nullptr || pTab == nullptr || str_comp(E.m_pDefaultTab, pTab) != 0)
				continue;
			v.push_back((int)i);
		}
		std::stable_sort(v.begin(), v.end(), [&](int a, int b) {
			return m_vEntries[a].m_OrderInColumn < m_vEntries[b].m_OrderInColumn;
		});
		return v;
	}

	std::vector<std::string> CModel::StableIdOrder(const char *pStableIdPrefix, const char *pTab, int Column) const
	{
		std::vector<std::string> vOrder;
		for(int Index : ColumnIndices(pTab, Column))
		{
			const SEntry &Entry = m_vEntries[Index];
			if(Entry.m_pStableId == nullptr)
				continue;
			if(pStableIdPrefix != nullptr && pStableIdPrefix[0] != '\0' && str_startswith(Entry.m_pStableId, pStableIdPrefix) == nullptr)
				continue;
			vOrder.emplace_back(Entry.m_pStableId);
		}
		return vOrder;
	}

	void CModel::BuildStateIndex()
	{
		m_StableIdToState.clear();
		for(size_t i = 0; i < m_vEntries.size(); ++i)
		{
			if(m_vEntries[i].m_pStableId == nullptr)
				continue;
			// 首次出现的 stableId 入表（重复 id 取首次，与 Parse 容错语义一致）
			m_StableIdToState.try_emplace(m_vEntries[i].m_pStableId, (int)i);
		}
		++m_StateIndexRevision;
	}

	int CModel::StateIndexForStableId(const char *pStableId) const
	{
		if(pStableId == nullptr)
			return -1;
		const auto It = m_StableIdToState.find(pStableId);
		if(It == m_StableIdToState.end())
			return -1;
		return It->second;
	}

	bool CModel::Serialize(char *pBuf, int BufSize) const
	{
		if(pBuf == nullptr || BufSize <= 0)
			return false;
		pBuf[0] = '\0';
		std::string Serialized;
		for(const SEntry &E : m_vEntries)
		{
			if(E.m_pStableId == nullptr)
				continue;
			std::string Column;
			if(!FormatColumn(E.m_Column, Column))
				return false;
			Serialized.append(E.m_pStableId);
			Serialized.push_back('|');
			Serialized.append(E.m_pDefaultTab != nullptr ? E.m_pDefaultTab : "");
			Serialized.push_back('|');
			Serialized.append(Column);
			Serialized.push_back('|');
			Serialized.append(std::to_string(E.m_OrderInColumn));
			Serialized.push_back(';');
		}
		if(static_cast<int>(Serialized.size()) >= BufSize)
			return false;
		str_copy(pBuf, Serialized.c_str(), BufSize);
		return true;
	}

	bool CModel::Parse(const char *pStr, const std::vector<const char *> &vValidIds, bool RejectDuplicateValidIds)
	{
		if(pStr == nullptr)
			return false;
		m_vEntries.clear();
		m_vOwnedTabs.clear();
		std::vector<bool> vSeen(vValidIds.size(), false);
		bool DuplicateValidId = false;
		const char *pTokenStart = pStr;
		while(*pTokenStart != '\0')
		{
			while(*pTokenStart == ';')
				++pTokenStart;
			if(*pTokenStart == '\0')
				break;
			const char *pTokenEnd = pTokenStart;
			while(*pTokenEnd != '\0' && *pTokenEnd != ';')
				++pTokenEnd;
			const std::string Token(pTokenStart, pTokenEnd - pTokenStart);
			pTokenStart = pTokenEnd;

			std::string Id;
			std::string Tab;
			std::string ColumnText;
			std::string OrderText;
			const bool PipeFormat = Token.find('|') != std::string::npos;
			if(PipeFormat)
			{
				const size_t TabSeparator = Token.find('|');
				const size_t ColumnSeparator = Token.find('|', TabSeparator + 1);
				const size_t OrderSeparator = ColumnSeparator != std::string::npos ? Token.find('|', ColumnSeparator + 1) : std::string::npos;
				if(ColumnSeparator == std::string::npos || OrderSeparator == std::string::npos)
					continue;
				Id = Token.substr(0, TabSeparator);
				Tab = Token.substr(TabSeparator + 1, ColumnSeparator - TabSeparator - 1);
				ColumnText = Token.substr(ColumnSeparator + 1, OrderSeparator - ColumnSeparator - 1);
				const size_t ExtraSeparator = Token.find('|', OrderSeparator + 1);
				OrderText = Token.substr(OrderSeparator + 1, ExtraSeparator - OrderSeparator - 1);
			}
			else
			{
				// 兼容旧 "id:col:order"。从末尾拆两段，避免 stableId 中的 ':' 被误判为字段分隔。
				const size_t OrderSeparator = Token.rfind(':');
				const size_t ColumnSeparator = OrderSeparator != std::string::npos && OrderSeparator > 0 ? Token.rfind(':', OrderSeparator - 1) : std::string::npos;
				if(ColumnSeparator == std::string::npos || OrderSeparator == std::string::npos)
					continue;
				Id = Token.substr(0, ColumnSeparator);
				ColumnText = Token.substr(ColumnSeparator + 1, OrderSeparator - ColumnSeparator - 1);
				OrderText = Token.substr(OrderSeparator + 1);
			}
			// 校验 id 在 vValidIds
			int ValidIdx = -1;
			for(size_t i = 0; i < vValidIds.size(); ++i)
			{
				if(vValidIds[i] != nullptr && Id == vValidIds[i])
				{
					ValidIdx = (int)i;
					break;
				}
			}
			if(ValidIdx < 0)
				continue;
			if(vSeen[ValidIdx]) // 重复项在容错解析中跳过，严格迁移解析会拒绝。
			{
				DuplicateValidId = true;
				continue;
			}
			// 解析 column/order（先校验 column，再设 vSeen——避免非法 column 占用 id 导致后续合法被当重复）
			int Col = 0;
			int Order = 0;
			if(!ParseColumn(ColumnText.c_str(), true, &Col))
				continue;
			if(!str_toint(OrderText.c_str(), &Order))
				continue;
			vSeen[ValidIdx] = true;
			SEntry E;
			E.m_pStableId = vValidIds[ValidIdx];
			if(PipeFormat && !Tab.empty())
			{
				m_vOwnedTabs.emplace_back(Tab);
				E.m_pDefaultTab = m_vOwnedTabs.back().c_str();
			}
			E.m_Column = Col;
			E.m_OrderInColumn = Order < 0 ? 0 : Order;
			m_vEntries.push_back(E);
		}
		m_Dirty = true;
		++m_LayoutRevision;
		BuildStateIndex(); // 解析后重建 stableId→index 注册表
		return !RejectDuplicateValidIds || !DuplicateValidId;
	}

	bool CModel::LoadExplicit(const char *pStr, const std::vector<SEntry> &vDefaults)
	{
		std::vector<const char *> vValidIds;
		vValidIds.reserve(vDefaults.size());
		for(const SEntry &Default : vDefaults)
		{
			if(Default.m_pStableId != nullptr)
				vValidIds.push_back(Default.m_pStableId);
		}
		return pStr != nullptr && pStr[0] != '\0' && Parse(pStr, vValidIds) && Count() > 0;
	}

	bool CModel::LoadMerged(const char *pStr, const std::vector<SEntry> &vDefaults)
	{
		CModel ParsedModel;
		const bool Parsed = ParsedModel.LoadExplicit(pStr, vDefaults);

		std::vector<SEntry> vMerged;
		vMerged.reserve(vDefaults.size());
		for(const SEntry &Default : vDefaults)
		{
			if(Default.m_pStableId == nullptr)
				continue;
			const int ParsedIndex = ParsedModel.FindByStableId(Default.m_pStableId);
			if(ParsedIndex < 0)
			{
				vMerged.push_back(Default);
				continue;
			}

			SEntry Entry = ParsedModel.Entry(ParsedIndex);
			if(Entry.m_pDefaultTab == nullptr)
				Entry.m_pDefaultTab = Default.m_pDefaultTab;
			vMerged.push_back(Entry);
		}

		SetEntries(vMerged);
		NormalizeColumns();
		ClearDirty();
		return Parsed;
	}

	bool TabContainsOnlyStableIds(const CModel &Model, const char *pTab, const std::vector<const char *> &vAllowedStableIds)
	{
		for(int EntryIndex = 0; EntryIndex < Model.Count(); ++EntryIndex)
		{
			const SEntry &Entry = Model.Entry(EntryIndex);
			if(!SameTab(Entry.m_pDefaultTab, pTab))
				continue;
			const bool Allowed = std::any_of(vAllowedStableIds.begin(), vAllowedStableIds.end(), [&](const char *pAllowedStableId) {
				return pAllowedStableId != nullptr && Entry.m_pStableId != nullptr && str_comp(Entry.m_pStableId, pAllowedStableId) == 0;
			});
			if(!Allowed)
				return false;
		}
		return true;
	}

	EExplicitLayoutStatus ClassifyExplicitLayout(const char *pSerialized, const std::vector<SEntry> &vExpectedLayout, const std::vector<const char *> &vRequiredStableIds)
	{
		if(pSerialized == nullptr || pSerialized[0] == '\0' || vExpectedLayout.empty())
			return EExplicitLayoutStatus::INVALID;
		std::vector<const char *> vValidIds;
		vValidIds.reserve(vExpectedLayout.size());
		for(size_t Index = 0; Index < vExpectedLayout.size(); ++Index)
		{
			const SEntry &Expected = vExpectedLayout[Index];
			if(Expected.m_pStableId == nullptr)
				return EExplicitLayoutStatus::INVALID;
			for(size_t Previous = 0; Previous < Index; ++Previous)
			{
				if(str_comp(vExpectedLayout[Previous].m_pStableId, Expected.m_pStableId) == 0)
					return EExplicitLayoutStatus::INVALID;
			}
			vValidIds.push_back(Expected.m_pStableId);
		}
		if(!HasStrictKnownEntries(pSerialized, vValidIds))
			return EExplicitLayoutStatus::INVALID;
		CModel Explicit;
		if(!Explicit.Parse(pSerialized, vValidIds, true))
			return EExplicitLayoutStatus::INVALID;
		for(const char *pRequiredStableId : vRequiredStableIds)
		{
			if(pRequiredStableId == nullptr || Explicit.FindByStableId(pRequiredStableId) < 0)
				return EExplicitLayoutStatus::INVALID;
		}
		for(const SEntry &Expected : vExpectedLayout)
		{
			const int Index = Explicit.FindByStableId(Expected.m_pStableId);
			if(Index < 0)
				continue;
			const SEntry &Current = Explicit.Entry(Index);
			if((Current.m_pDefaultTab != nullptr && !SameTab(Current.m_pDefaultTab, Expected.m_pDefaultTab)) || Current.m_Column != Expected.m_Column || Current.m_OrderInColumn != Expected.m_OrderInColumn)
				return EExplicitLayoutStatus::NOT_MATCH;
		}
		return EExplicitLayoutStatus::MATCH;
	}

	bool MigrateExactLayout(CModel &Model, const char *pTab, const std::vector<SEntry> &vExpectedLayout, const std::vector<SEntry> &vTargetLayout, const std::vector<const char *> &vAllowedStableIds)
	{
		if(pTab == nullptr || pTab[0] == '\0' || vExpectedLayout.empty() || vExpectedLayout.size() != vTargetLayout.size() || !TabContainsOnlyStableIds(Model, pTab, vAllowedStableIds))
			return false;
		auto LayoutMatches = [&](const CModel &CheckModel, const std::vector<SEntry> &vLayout) {
			for(const SEntry &Expected : vLayout)
			{
				int Occurrences = 0;
				for(int EntryIndex = 0; EntryIndex < CheckModel.Count(); ++EntryIndex)
				{
					const SEntry &Entry = CheckModel.Entry(EntryIndex);
					if(Entry.m_pStableId != nullptr && str_comp(Entry.m_pStableId, Expected.m_pStableId) == 0)
						++Occurrences;
				}
				if(Occurrences != 1)
					return false;
				const int ModelIndex = CheckModel.FindByStableId(Expected.m_pStableId);
				if(ModelIndex < 0)
					return false;
				const SEntry &Current = CheckModel.Entry(ModelIndex);
				if(!SameTab(Current.m_pDefaultTab, Expected.m_pDefaultTab) || Current.m_Column != Expected.m_Column || Current.m_OrderInColumn != Expected.m_OrderInColumn)
					return false;
			}
			return true;
		};
		for(size_t Index = 0; Index < vExpectedLayout.size(); ++Index)
		{
			const SEntry &Expected = vExpectedLayout[Index];
			const SEntry &Target = vTargetLayout[Index];
			if(Expected.m_pStableId == nullptr || Target.m_pStableId == nullptr || str_comp(Expected.m_pStableId, Target.m_pStableId) != 0 ||
				!SameTab(Expected.m_pDefaultTab, pTab) || !SameTab(Target.m_pDefaultTab, pTab) || Expected.m_Column < 0 || Expected.m_OrderInColumn < 0 || Target.m_Column < 0 || Target.m_OrderInColumn < 0)
				return false;
			for(size_t Previous = 0; Previous < Index; ++Previous)
			{
				if(str_comp(vExpectedLayout[Previous].m_pStableId, Expected.m_pStableId) == 0)
					return false;
			}
		}
		for(size_t Index = 0; Index < vExpectedLayout.size(); ++Index)
		{
			const SEntry &Expected = vExpectedLayout[Index];
			const SEntry &Target = vTargetLayout[Index];
			int Occurrences = 0;
			for(int EntryIndex = 0; EntryIndex < Model.Count(); ++EntryIndex)
			{
				const SEntry &Entry = Model.Entry(EntryIndex);
				if(Entry.m_pStableId != nullptr && str_comp(Entry.m_pStableId, Expected.m_pStableId) == 0)
					++Occurrences;
			}
			if(Occurrences != 1)
				return false;
			const int ModelIndex = Model.FindByStableId(Expected.m_pStableId);
			if(ModelIndex < 0)
				return false;
			const SEntry &Current = Model.Entry(ModelIndex);
			const bool MatchesExpected = SameTab(Current.m_pDefaultTab, Expected.m_pDefaultTab) && Current.m_Column == Expected.m_Column && Current.m_OrderInColumn == Expected.m_OrderInColumn;
			const bool MatchesTarget = SameTab(Current.m_pDefaultTab, Target.m_pDefaultTab) && Current.m_Column == Target.m_Column && Current.m_OrderInColumn == Target.m_OrderInColumn;
			if(!MatchesExpected && !MatchesTarget)
				return false;
		}
		std::vector<SEntry> vCandidateEntries;
		vCandidateEntries.reserve(Model.Count());
		for(int EntryIndex = 0; EntryIndex < Model.Count(); ++EntryIndex)
			vCandidateEntries.push_back(Model.Entry(EntryIndex));
		CModel Candidate;
		Candidate.SetEntries(vCandidateEntries);
		for(const SEntry &Target : vTargetLayout)
			Candidate.MoveToTab(Target.m_pStableId, Target.m_pDefaultTab, Target.m_Column, Target.m_OrderInColumn);
		if(!LayoutMatches(Candidate, vTargetLayout))
			return false;
		vCandidateEntries.clear();
		for(int EntryIndex = 0; EntryIndex < Candidate.Count(); ++EntryIndex)
			vCandidateEntries.push_back(Candidate.Entry(EntryIndex));
		Model.SetEntries(std::move(vCandidateEntries));
		return true;
	}

} // namespace qm_card_order
