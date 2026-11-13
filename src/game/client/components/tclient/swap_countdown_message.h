#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_SWAP_COUNTDOWN_MESSAGE_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_SWAP_COUNTDOWN_MESSAGE_H

#include <engine/client/enums.h>

#include <generated/protocol.h>

#include <limits>
#include <optional>
#include <string>
#include <vector>

enum class ESwapCountdownMessageAction
{
	None,
	Start,
	Cancel,
	Complete,
};

enum class ESwapCountdownMessageDirection
{
	Incoming,
	Outgoing,
};

bool ParseSwapCountdownMessage(const char *pText, ESwapCountdownMessageAction &Action, ESwapCountdownMessageDirection &Direction, char *pCounterpart, int CounterpartSize);
bool ParseSwapCompletionMessage(const char *pText, char *pFirst, int FirstSize, char *pSecond, int SecondSize);

inline std::optional<int> QmSwapCountdownDisplayId(int InstanceId, int Dummy)
{
	if(InstanceId <= 0 || Dummy < 0 || Dummy >= NUM_DUMMIES || InstanceId > (std::numeric_limits<int>::max() - Dummy) / NUM_DUMMIES)
		return std::nullopt;
	return InstanceId * NUM_DUMMIES + Dummy;
}

struct SSwapCountdownState
{
	std::string m_Counterpart;
	int m_StartTick = 0;
	int m_InstanceId = 0;
	bool m_Outgoing = false;
};

class CSwapCountdownTracker
{
	std::vector<SSwapCountdownState> m_vEntries;
	int m_NextInstanceId = 1;

public:
	bool Start(const char *pCounterpart, bool Outgoing, int StartTick) noexcept;
	void Cancel(const char *pCounterpart, bool Outgoing);
	void Remove(const char *pCounterpart);
	void Clear();
	const std::vector<SSwapCountdownState> &Entries() const { return m_vEntries; }
};

#endif
