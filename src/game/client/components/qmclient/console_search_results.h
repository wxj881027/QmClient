#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SEARCH_RESULTS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SEARCH_RESULTS_H

#include "console_text.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

namespace QmConsoleSearch
{
	struct SMatch
	{
		int m_Pos;
		int m_Length;
		int m_StartLine;
		int m_EndLine;
		int m_EntryLine;
		SMatch(int Pos, int Length, int StartLine, int EndLine, int EntryLine) :
			m_Pos(Pos), m_Length(Length), m_StartLine(StartLine), m_EndLine(EndLine), m_EntryLine(EntryLine) {}
	};

	// 临时匹配不发布；这里只处理收集容器的申请失败，不包住文字或图形调用。
	inline bool TryCollect(const char *pText, const char *pQuery, std::vector<QmConsoleText::SRange> &vRanges)
	{
		try
		{
			QmConsoleText::CollectSearchMatches(pText, pQuery, vRanges);
			return true;
		}
		catch(const std::bad_alloc &)
		{
			vRanges.clear();
			return false;
		}
		catch(const std::length_error &)
		{
			vRanges.clear();
			return false;
		}
	}

	template<size_t QuerySize, typename TAllocator = std::allocator<SMatch>>
	class CResults
	{
		static_assert(QuerySize > 0);
		std::array<char, QuerySize> m_aRequested{};
		std::array<char, QuerySize> m_aPublished{};
		std::vector<SMatch, TAllocator> m_vMatches;
		int m_Current = -1;
		bool m_Ready = true;
		bool m_Pending = false;

	public:
		enum class EUpdate
		{
			FAILED,
			SAME_QUERY,
			CHANGED_QUERY,
		};
		class CBuilder
		{
			friend class CResults;
			std::vector<SMatch, TAllocator> m_vMatches;
			bool m_Valid = true;
			explicit CBuilder(const TAllocator &Allocator) :
				m_vMatches(Allocator) {}

		public:
			bool Append(int Pos, int Length, int StartLine, int EndLine, int EntryLine)
			{
				if(!m_Valid || m_vMatches.size() >= static_cast<size_t>(std::numeric_limits<int>::max()))
					return m_Valid = false;
				try
				{
					m_vMatches.emplace_back(Pos, Length, StartLine, EndLine, EntryLine);
					return true;
				}
				catch(const std::bad_alloc &)
				{
					return m_Valid = false;
				}
				catch(const std::length_error &)
				{
					return m_Valid = false;
				}
			}
		};

		explicit CResults(const TAllocator &Allocator = TAllocator()) :
			m_vMatches(Allocator) {}
		const char *RequestedQuery() const noexcept { return m_aRequested.data(); }
		const auto &Matches() const noexcept { return m_vMatches; }
		int CurrentIndex() const noexcept { return m_Current; }
		bool Ready() const noexcept { return m_Ready; }
		bool Pending() const noexcept { return m_Pending; }
		void Invalidate() noexcept
		{
			m_Ready = false;
			m_Pending = true;
		}
		void CancelPending() noexcept { m_Pending = false; }
		void Reset() noexcept
		{
			m_aRequested.fill('\0');
			m_aPublished.fill('\0');
			m_vMatches.clear();
			m_Current = -1;
			m_Ready = true;
			m_Pending = false;
		}
		const SMatch *CurrentMatch() const noexcept
		{
			return m_Ready && m_Current >= 0 && static_cast<size_t>(m_Current) < m_vMatches.size() ? &m_vMatches[m_Current] : nullptr;
		}
		bool SelectNext(int Direction) noexcept
		{
			if(!m_Ready || m_vMatches.empty())
				return false;
			const int64_t Next = static_cast<int64_t>(m_Current) + Direction;
			m_Current = Next < 0 ? static_cast<int>(m_vMatches.size()) - 1 : Next >= static_cast<int64_t>(m_vMatches.size()) ? 0 :
																	   static_cast<int>(Next);
			return true;
		}

		template<typename TBuild>
		EUpdate Prepare(const char *pQuery, TBuild &&Build)
		{
			const bool Changed = str_utf8_comp_nocase(pQuery, m_aPublished.data()) != 0;
			str_copy(m_aRequested.data(), pQuery, QuerySize);
			Invalidate();
			CBuilder Candidate(m_vMatches.get_allocator());
			// 测量等外部调用的异常仍由其owner处理，不能在这里掩盖资源失败。
			if(pQuery[0] != '\0' && (!Build(Candidate) || !Candidate.m_Valid))
				return EUpdate::FAILED;
			std::sort(Candidate.m_vMatches.begin(), Candidate.m_vMatches.end(), [](const SMatch &A, const SMatch &B) {
				return A.m_StartLine == B.m_StartLine ? A.m_Pos < B.m_Pos : A.m_StartLine > B.m_StartLine;
			});
			const int NextIndex = Candidate.m_vMatches.empty() ? -1 : Changed ? 0 :
											    std::clamp(m_Current, 0, static_cast<int>(Candidate.m_vMatches.size()) - 1);
			m_vMatches.swap(Candidate.m_vMatches);
			m_aPublished = m_aRequested;
			m_Current = NextIndex;
			m_Ready = true;
			m_Pending = false;
			return Changed ? EUpdate::CHANGED_QUERY : EUpdate::SAME_QUERY;
		}
	};
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SEARCH_RESULTS_H
