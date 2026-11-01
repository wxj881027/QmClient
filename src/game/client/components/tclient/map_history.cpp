#include "map_history.h"

#include <base/system.h>

#include <engine/external/json-parser/json.h>

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace QmMapHistory
{
	namespace
	{

		void SetError(char *pErr, size_t ErrSize, const char *pMessage)
		{
			if(pErr != nullptr && ErrSize > 0)
				str_copy(pErr, pMessage ? pMessage : "", ErrSize);
		}

		void EscapeJsonString(std::string_view In, std::string *pOut)
		{
			pOut->push_back('"');
			for(char C : In)
			{
				switch(C)
				{
				case '"': pOut->append("\\\""); break;
				case '\\': pOut->append("\\\\"); break;
				case '\b': pOut->append("\\b"); break;
				case '\f': pOut->append("\\f"); break;
				case '\n': pOut->append("\\n"); break;
				case '\r': pOut->append("\\r"); break;
				case '\t': pOut->append("\\t"); break;
				default:
					if((unsigned char)C < 0x20)
					{
						char aBuf[8];
						str_format(aBuf, sizeof(aBuf), "\\u%04x", (unsigned)(unsigned char)C);
						pOut->append(aBuf);
					}
					else
					{
						pOut->push_back(C);
					}
				}
			}
			pOut->push_back('"');
		}

		const char *JsonString(const json_value *pVal, const char *pDefault = "")
		{
			if(pVal == nullptr || pVal->type != json_string)
				return pDefault;
			return pVal->u.string.ptr;
		}

		int64_t JsonInt(const json_value *pVal, int64_t Default = 0)
		{
			if(pVal == nullptr)
				return Default;
			if(pVal->type == json_integer)
				return pVal->u.integer;
			if(pVal->type == json_double)
			{
				const double Value = pVal->u.dbl;
				// INT64_MAX转double会舍入到2^63，正边界必须排除等号。
				constexpr double LIMIT = 9223372036854775808.0;
				if(!std::isfinite(Value) || Value < -LIMIT || Value >= LIMIT)
					return Default;
				return static_cast<int64_t>(Value);
			}
			return Default;
		}

		bool JsonBool(const json_value *pVal, bool Default = false)
		{
			if(pVal == nullptr)
				return Default;
			if(pVal->type == json_boolean)
				return pVal->u.boolean != 0;
			if(pVal->type == json_integer)
				return pVal->u.integer != 0;
			return Default;
		}

		bool IsOlderRecord(const SMapHistoryRecord &A, const SMapHistoryRecord &B)
		{
			if(A.m_LastEnteredAt != B.m_LastEnteredAt)
				return A.m_LastEnteredAt < B.m_LastEnteredAt;
			return A.m_MapName < B.m_MapName;
		}

	} // namespace

	SMapHistoryRecord *CMapHistory::Find(std::string_view MapId)
	{
		for(SMapHistoryRecord &Record : m_vEntries)
		{
			if(Record.m_MapId == MapId)
				return &Record;
		}
		return nullptr;
	}

	const SMapHistoryRecord *CMapHistory::Find(std::string_view MapId) const
	{
		for(const SMapHistoryRecord &Record : m_vEntries)
		{
			if(Record.m_MapId == MapId)
				return &Record;
		}
		return nullptr;
	}

	SMapHistoryRecord &CMapHistory::RecordVisit(std::string_view MapName, std::string_view MapId, int64_t NowUnix, std::string_view Date)
	{
		const std::string StableId = MapId.empty() ? std::string(MapName) : std::string(MapId);
		if(SMapHistoryRecord *pRecord = Find(StableId))
		{
			// 字符串都准备成功后再发布，也允许输入借用旧记录自身。
			std::string NewName(MapName);
			std::string NewDate(Date);
			pRecord->m_MapName.swap(NewName);
			pRecord->m_LastPlayedDate.swap(NewDate);
			pRecord->m_LastEnteredAt = NowUnix;
			pRecord->m_Finished = false;
			pRecord->m_FinishTimeMs = 0;
			pRecord->m_PlayTimeMs = 0;
			return *pRecord;
		}

		SMapHistoryRecord Record;
		Record.m_MapName = std::string(MapName);
		Record.m_MapId = StableId;
		Record.m_LastEnteredAt = NowUnix;
		Record.m_LastPlayedDate = std::string(Date);
		m_vEntries.push_back(std::move(Record));
		return m_vEntries.back();
	}

	bool CMapHistory::UpdatePlayTime(std::string_view MapId, int64_t PlayTimeMs)
	{
		SMapHistoryRecord *pRecord = Find(MapId);
		if(pRecord == nullptr)
			return false;
		pRecord->m_PlayTimeMs = std::max<int64_t>(0, PlayTimeMs);
		if(!pRecord->m_Finished)
			pRecord->m_FinishTimeMs = 0;
		return true;
	}

	bool CMapHistory::AddDeath(std::string_view MapId, int Count)
	{
		SMapHistoryRecord *pRecord = Find(MapId);
		if(pRecord == nullptr || Count <= 0)
			return false;
		const int64_t CurrentDeaths = std::max<int64_t>(0, pRecord->m_DeathCount);
		const int64_t NewDeaths = std::min<int64_t>(std::numeric_limits<int>::max(), CurrentDeaths + Count);
		pRecord->m_DeathCount = (int)NewDeaths;
		return true;
	}

	bool CMapHistory::MarkFinished(std::string_view MapId, int64_t FinishTimeMs, int64_t PlayTimeMs)
	{
		SMapHistoryRecord *pRecord = Find(MapId);
		if(pRecord == nullptr)
			return false;
		pRecord->m_Finished = true;
		pRecord->m_FinishTimeMs = std::max<int64_t>(0, FinishTimeMs);
		pRecord->m_PlayTimeMs = std::max<int64_t>(0, PlayTimeMs);
		return true;
	}

	bool CMapHistory::Remove(std::string_view MapId)
	{
		const auto It = std::find_if(m_vEntries.begin(), m_vEntries.end(), [&](const SMapHistoryRecord &Record) {
			return Record.m_MapId == MapId;
		});
		if(It == m_vEntries.end())
			return false;
		m_vEntries.erase(It);
		return true;
	}

	int CMapHistory::ClearFinished()
	{
		const size_t OldSize = m_vEntries.size();
		m_vEntries.erase(std::remove_if(m_vEntries.begin(), m_vEntries.end(), [](const SMapHistoryRecord &Record) {
			return Record.m_Finished;
		}),
			m_vEntries.end());
		return (int)(OldSize - m_vEntries.size());
	}

	void CMapHistory::ApplyLimit(int MaxEntries)
	{
		if(MaxEntries <= 0)
			return;

		while((int)m_vEntries.size() > MaxEntries)
		{
			auto RemoveIt = m_vEntries.end();
			for(auto It = m_vEntries.begin(); It != m_vEntries.end(); ++It)
			{
				if(!It->m_Finished)
					continue;
				if(RemoveIt == m_vEntries.end() || IsOlderRecord(*It, *RemoveIt))
					RemoveIt = It;
			}
			if(RemoveIt == m_vEntries.end())
			{
				RemoveIt = std::min_element(m_vEntries.begin(), m_vEntries.end(), IsOlderRecord);
			}
			if(RemoveIt == m_vEntries.end())
				break;
			m_vEntries.erase(RemoveIt);
		}
	}

	std::vector<SMapHistoryRecord> CMapHistory::Sorted(EMapHistoryFilter Filter) const
	{
		std::vector<size_t> vIndices;
		SortedIndices(Filter, vIndices);
		std::vector<SMapHistoryRecord> vResult;
		vResult.reserve(vIndices.size());
		for(const size_t Index : vIndices)
			vResult.push_back(m_vEntries[Index]);
		return vResult;
	}

	void CMapHistory::SortedIndices(EMapHistoryFilter Filter, std::vector<size_t> &vResult) const
	{
		vResult.clear();
		vResult.reserve(m_vEntries.size());
		for(size_t Index = 0; Index < m_vEntries.size(); ++Index)
		{
			const SMapHistoryRecord &Record = m_vEntries[Index];
			if(Filter == EMapHistoryFilter::UNFINISHED && Record.m_Finished)
				continue;
			if(Filter == EMapHistoryFilter::FINISHED && !Record.m_Finished)
				continue;
			vResult.push_back(Index);
		}

		std::sort(vResult.begin(), vResult.end(), [&](size_t AIndex, size_t BIndex) {
			const SMapHistoryRecord &A = m_vEntries[AIndex];
			const SMapHistoryRecord &B = m_vEntries[BIndex];
			if(A.m_LastEnteredAt != B.m_LastEnteredAt)
				return A.m_LastEnteredAt > B.m_LastEnteredAt;
			return A.m_MapName < B.m_MapName;
		});
	}

	std::string CMapHistory::ToJson() const
	{
		std::string Out;
		Out.reserve(m_vEntries.size() * 192);
		Out.append("{\"version\":1,\"entries\":[");
		bool First = true;
		for(const SMapHistoryRecord &Record : m_vEntries)
		{
			if(Record.m_MapName.empty() || Record.m_MapId.empty())
				continue;
			if(!First)
				Out.push_back(',');
			First = false;
			Out.push_back('{');
			Out.append("\"map_name\":");
			EscapeJsonString(Record.m_MapName, &Out);
			Out.append(",\"map_id\":");
			EscapeJsonString(Record.m_MapId, &Out);
			Out.append(",\"last_entered\":");
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), "%" PRId64, Record.m_LastEnteredAt);
			Out.append(aBuf);
			Out.append(",\"last_played_date\":");
			EscapeJsonString(Record.m_LastPlayedDate, &Out);
			Out.append(",\"deaths\":");
			str_format(aBuf, sizeof(aBuf), "%d", Record.m_DeathCount);
			Out.append(aBuf);
			Out.append(",\"finished\":");
			Out.append(Record.m_Finished ? "true" : "false");
			Out.append(",\"finish_time_ms\":");
			str_format(aBuf, sizeof(aBuf), "%" PRId64, Record.m_FinishTimeMs);
			Out.append(aBuf);
			Out.append(",\"play_time_ms\":");
			str_format(aBuf, sizeof(aBuf), "%" PRId64, Record.m_PlayTimeMs);
			Out.append(aBuf);
			Out.push_back('}');
		}
		Out.append("]}");
		return Out;
	}

	bool CMapHistory::FromJson(std::string_view Json, char *pErr, size_t ErrSize)
	{
		try
		{
			json_settings Settings{};
			char aJsonErr[json_error_max];
			const std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(json_parse_ex(&Settings, Json.data(), Json.size(), aJsonErr), json_value_free);
			if(pRoot == nullptr)
			{
				SetError(pErr, ErrSize, aJsonErr);
				return false;
			}
			if(pRoot->type != json_object)
			{
				SetError(pErr, ErrSize, "root is not an object");
				return false;
			}

			const json_value &Entries = (*pRoot)["entries"];
			if(Entries.type != json_none && Entries.type != json_array)
			{
				SetError(pErr, ErrSize, "entries is not an array");
				return false;
			}

			// 全部记录和重复项合并完成后才发布；失败保留原名单及其借用。
			CMapHistory Loaded;
			if(Entries.type == json_array)
			{
				for(unsigned i = 0; i < Entries.u.array.length; ++i)
				{
					const json_value *pItem = Entries.u.array.values[i];
					if(pItem == nullptr || pItem->type != json_object)
						continue;
					SMapHistoryRecord Record;
					Record.m_MapName = JsonString(&(*pItem)["map_name"]);
					Record.m_MapId = JsonString(&(*pItem)["map_id"]);
					if(Record.m_MapId.empty())
						Record.m_MapId = Record.m_MapName;
					if(Record.m_MapName.empty() || Record.m_MapId.empty())
						continue;
					Record.m_LastEnteredAt = std::max<int64_t>(0, JsonInt(&(*pItem)["last_entered"]));
					Record.m_LastPlayedDate = JsonString(&(*pItem)["last_played_date"]);
					Record.m_DeathCount = (int)std::clamp<int64_t>(JsonInt(&(*pItem)["deaths"]), 0, std::numeric_limits<int>::max());
					Record.m_Finished = JsonBool(&(*pItem)["finished"]);
					Record.m_FinishTimeMs = std::max<int64_t>(0, JsonInt(&(*pItem)["finish_time_ms"]));
					Record.m_PlayTimeMs = std::max<int64_t>(0, JsonInt(&(*pItem)["play_time_ms"]));
					if(!Record.m_Finished)
						Record.m_FinishTimeMs = 0;
					Loaded.MergeLoadedRecord(Record);
				}
			}
			m_vEntries.swap(Loaded.m_vEntries);
			return true;
		}
		catch(const std::bad_alloc &)
		{
			SetError(pErr, ErrSize, "map history memory allocation failed");
			return false;
		}
		catch(const std::length_error &)
		{
			SetError(pErr, ErrSize, "map history entry capacity exceeded");
			return false;
		}
	}

	void CMapHistory::MergeLoadedRecord(const SMapHistoryRecord &Record)
	{
		SMapHistoryRecord *pExisting = Find(Record.m_MapId);
		if(pExisting == nullptr)
		{
			m_vEntries.push_back(Record);
			return;
		}

		pExisting->m_DeathCount = std::max(pExisting->m_DeathCount, Record.m_DeathCount);
		if(Record.m_LastEnteredAt >= pExisting->m_LastEnteredAt)
		{
			const int DeathCount = pExisting->m_DeathCount;
			*pExisting = Record;
			pExisting->m_DeathCount = std::max(DeathCount, Record.m_DeathCount);
		}
	}

} // namespace QmMapHistory
