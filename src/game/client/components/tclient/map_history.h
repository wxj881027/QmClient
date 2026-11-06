#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_MAP_HISTORY_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_MAP_HISTORY_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace QmMapHistory
{

	struct SMapHistoryRecord
	{
		std::string m_MapName;
		std::string m_MapId;
		int64_t m_LastEnteredAt = 0;
		std::string m_LastPlayedDate;
		int m_DeathCount = 0;
		bool m_Finished = false;
		int64_t m_FinishTimeMs = 0;
		int64_t m_PlayTimeMs = 0;
	};

	enum class EMapHistoryFilter
	{
		UNFINISHED,
		FINISHED,
		RECENT,
	};

	class CMapHistory
	{
	public:
		const std::vector<SMapHistoryRecord> &Entries() const { return m_vEntries; }
		size_t Size() const { return m_vEntries.size(); }
		void Clear() { m_vEntries.clear(); }

		SMapHistoryRecord *Find(std::string_view MapId);
		const SMapHistoryRecord *Find(std::string_view MapId) const;

		SMapHistoryRecord &RecordVisit(std::string_view MapName, std::string_view MapId, int64_t NowUnix, std::string_view Date);
		bool UpdatePlayTime(std::string_view MapId, int64_t PlayTimeMs);
		bool AddDeath(std::string_view MapId, int Count = 1);
		bool MarkFinished(std::string_view MapId, int64_t FinishTimeMs, int64_t PlayTimeMs);
		bool Remove(std::string_view MapId);
		int ClearFinished();
		void ApplyLimit(int MaxEntries);

		std::vector<SMapHistoryRecord> Sorted(EMapHistoryFilter Filter) const;
		// 索引借用 Entries() 的顺序；增删记录后应重新构建，输出容量可跨帧复用。
		void SortedIndices(EMapHistoryFilter Filter, std::vector<size_t> &vResult) const;
		std::string ToJson() const;
		bool FromJson(std::string_view Json, char *pErr = nullptr, size_t ErrSize = 0);

	private:
		std::vector<SMapHistoryRecord> m_vEntries;
	};

} // namespace QmMapHistory

#endif
