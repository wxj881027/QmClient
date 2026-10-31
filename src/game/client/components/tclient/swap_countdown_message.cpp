#include "swap_countdown_message.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/shared/protocol.h>

#include <algorithm>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{
	bool ExtractLeadingName(const char *pText, const char *pMarker, char *pOut, int OutSize)
	{
		if(OutSize <= 0)
			return false;
		pOut[0] = '\0';
		if(pText == nullptr || pMarker == nullptr)
			return false;

		const char *pMarkerPos = str_find_nocase(pText, pMarker);
		if(pMarkerPos == nullptr || pMarkerPos == pText)
			return false;

		const int NameLength = minimum<int>(pMarkerPos - pText, OutSize - 1);
		str_truncate(pOut, OutSize, pText, NameLength);
		return pOut[0] != '\0';
	}

	bool ExtractWrappedName(const char *pText, const char *pPrefix, const char *pSuffix, char *pOut, int OutSize)
	{
		if(OutSize <= 0)
			return false;
		pOut[0] = '\0';
		if(pText == nullptr || pPrefix == nullptr || pSuffix == nullptr)
			return false;

		const char *pNameStart = str_startswith_nocase(pText, pPrefix);
		const char *pNameEnd = pNameStart == nullptr ? nullptr : str_find_nocase(pNameStart, pSuffix);
		if(pNameStart == nullptr || pNameEnd == nullptr || pNameEnd == pNameStart)
			return false;

		const int NameLength = minimum<int>(pNameEnd - pNameStart, OutSize - 1);
		str_truncate(pOut, OutSize, pNameStart, NameLength);
		return pOut[0] != '\0';
	}

	bool ExtractTwoNames(const char *pText, const char *pMiddle, const char *pSuffix, char *pFirst, int FirstSize, char *pSecond, int SecondSize)
	{
		if(pText == nullptr || pMiddle == nullptr || pSuffix == nullptr || FirstSize <= 0 || SecondSize <= 0)
			return false;
		pFirst[0] = '\0';
		pSecond[0] = '\0';

		const char *pMiddlePos = str_find_nocase(pText, pMiddle);
		if(pMiddlePos == nullptr || pMiddlePos == pText)
			return false;
		const char *pSecondStart = pMiddlePos + str_length(pMiddle);
		const char *pSecondEnd = str_find_nocase(pSecondStart, pSuffix);
		if(pSecondEnd == nullptr || pSecondEnd == pSecondStart)
			return false;

		str_truncate(pFirst, FirstSize, pText, minimum<int>(pMiddlePos - pText, FirstSize - 1));
		str_truncate(pSecond, SecondSize, pSecondStart, minimum<int>(pSecondEnd - pSecondStart, SecondSize - 1));
		return pFirst[0] != '\0' && pSecond[0] != '\0';
	}
}

bool ParseSwapCompletionMessage(const char *pText, char *pFirst, int FirstSize, char *pSecond, int SecondSize)
{
	if(ExtractTwoNames(pText, " has swapped with ", ".", pFirst, FirstSize, pSecond, SecondSize))
		return true;
	if(ExtractTwoNames(pText, " and ", " have swapped", pFirst, FirstSize, pSecond, SecondSize))
		return true;
	return ExtractTwoNames(pText, " 与 ", " 已完成交换", pFirst, FirstSize, pSecond, SecondSize);
}

bool ParseSwapCountdownMessage(const char *pText, ESwapCountdownMessageAction &Action, ESwapCountdownMessageDirection &Direction, char *pCounterpart, int CounterpartSize)
{
	Action = ESwapCountdownMessageAction::None;
	Direction = ESwapCountdownMessageDirection::Incoming;
	if(CounterpartSize > 0)
		pCounterpart[0] = '\0';

	if(pText == nullptr)
		return false;

	if(str_startswith_nocase(pText, "You have requested to swap with "))
	{
		Action = ESwapCountdownMessageAction::Start;
		Direction = ESwapCountdownMessageDirection::Outgoing;
		return ExtractWrappedName(pText, "You have requested to swap with ", ". Use /cancelswap to cancel the request.", pCounterpart, CounterpartSize);
	}
	if(str_startswith(pText, "你已向 "))
	{
		Action = ESwapCountdownMessageAction::Start;
		Direction = ESwapCountdownMessageDirection::Outgoing;
		return ExtractWrappedName(pText, "你已向 ", " 发出交换请求", pCounterpart, CounterpartSize);
	}
	if(str_find_nocase(pText, "has requested to swap with you"))
	{
		Action = ESwapCountdownMessageAction::Start;
		return ExtractLeadingName(pText, " has requested to swap with you", pCounterpart, CounterpartSize);
	}
	if(str_find_nocase(pText, "请求与你交换位置"))
	{
		Action = ESwapCountdownMessageAction::Start;
		return ExtractLeadingName(pText, " 请求与你交换位置", pCounterpart, CounterpartSize);
	}
	if(str_find_nocase(pText, "has canceled swap with you"))
	{
		Action = ESwapCountdownMessageAction::Cancel;
		return ExtractLeadingName(pText, " has canceled swap with you", pCounterpart, CounterpartSize);
	}
	if(str_startswith_nocase(pText, "You have canceled swap with "))
	{
		Action = ESwapCountdownMessageAction::Cancel;
		Direction = ESwapCountdownMessageDirection::Outgoing;
		return ExtractWrappedName(pText, "You have canceled swap with ", ".", pCounterpart, CounterpartSize);
	}
	if(str_startswith(pText, "你已取消与 "))
	{
		Action = ESwapCountdownMessageAction::Cancel;
		Direction = ESwapCountdownMessageDirection::Outgoing;
		return ExtractWrappedName(pText, "你已取消与 ", " 的交换", pCounterpart, CounterpartSize);
	}
	if(str_find_nocase(pText, "已取消与你的交换"))
	{
		Action = ESwapCountdownMessageAction::Cancel;
		return ExtractLeadingName(pText, " 已取消与你的交换", pCounterpart, CounterpartSize);
	}
	char aFirst[MAX_NAME_LENGTH];
	char aSecond[MAX_NAME_LENGTH];
	if(ParseSwapCompletionMessage(pText, aFirst, sizeof(aFirst), aSecond, sizeof(aSecond)))
	{
		Action = ESwapCountdownMessageAction::Complete;
		return true;
	}

	return false;
}

bool CSwapCountdownTracker::Start(const char *pCounterpart, bool Outgoing, int StartTick) noexcept
{
	if(pCounterpart == nullptr || pCounterpart[0] == '\0' || !QmSwapCountdownDisplayId(m_NextInstanceId, NUM_DUMMIES - 1))
		return false;

	try
	{
		SSwapCountdownState Candidate;
		Candidate.m_Counterpart = pCounterpart;
		Candidate.m_StartTick = StartTick;
		Candidate.m_InstanceId = m_NextInstanceId;
		Candidate.m_Outgoing = Outgoing;
		// 先准备姓名和插入容量；后续移动及删除不会分配，失败不删旧请求或推进身份。
		static_assert(std::is_nothrow_move_constructible_v<SSwapCountdownState> && std::is_nothrow_move_assignable_v<SSwapCountdownState>);
		if(m_vEntries.size() == m_vEntries.capacity())
			m_vEntries.reserve(std::min<size_t>(MAX_CLIENTS + 2, std::max(m_vEntries.size() + 1, m_vEntries.size() * 2)));
		m_vEntries.erase(std::remove_if(m_vEntries.begin(), m_vEntries.end(), [&](const SSwapCountdownState &Entry) {
			return Entry.m_Outgoing == Outgoing && (Outgoing || str_comp_nocase(Entry.m_Counterpart.c_str(), Candidate.m_Counterpart.c_str()) == 0);
		}),
			m_vEntries.end());
		m_vEntries.insert(m_vEntries.begin(), std::move(Candidate));
		if(m_vEntries.size() > MAX_CLIENTS + 1)
			m_vEntries.erase(m_vEntries.begin() + MAX_CLIENTS + 1, m_vEntries.end());
		++m_NextInstanceId;
		return true;
	}
	catch(const std::bad_alloc &)
	{
	}
	catch(const std::length_error &)
	{
	}
	return false;
}

void CSwapCountdownTracker::Cancel(const char *pCounterpart, bool Outgoing)
{
	if(pCounterpart == nullptr)
		return;
	m_vEntries.erase(std::remove_if(m_vEntries.begin(), m_vEntries.end(), [pCounterpart, Outgoing](const SSwapCountdownState &Entry) {
		return Entry.m_Outgoing == Outgoing && str_comp_nocase(Entry.m_Counterpart.c_str(), pCounterpart) == 0;
	}),
		m_vEntries.end());
}

void CSwapCountdownTracker::Remove(const char *pCounterpart)
{
	if(pCounterpart == nullptr)
		return;
	m_vEntries.erase(std::remove_if(m_vEntries.begin(), m_vEntries.end(), [pCounterpart](const SSwapCountdownState &Entry) {
		return str_comp_nocase(Entry.m_Counterpart.c_str(), pCounterpart) == 0;
	}),
		m_vEntries.end());
}

void CSwapCountdownTracker::Clear()
{
	m_vEntries.clear();
}
