#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CROSS_SERVER_FOLLOW_LOGIC_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CROSS_SERVER_FOLLOW_LOGIC_H

#include <base/types.h>

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct SFollowTarget
{
	bool m_Active = false;
	std::string m_Name;
	std::string m_Clan;

	bool Matches(std::string_view Name, std::string_view Clan, bool IgnoreClan) const
	{
		return m_Active && Name == m_Name && (IgnoreClan || m_Clan.empty() || Clan == m_Clan);
	}
};

struct SFollowPlayerMatches
{
	int m_NameMatches = 0;
	int m_ClanMatches = 0;

	void Add(const SFollowTarget &Target, std::string_view Name, std::string_view Clan)
	{
		if(Name == Target.m_Name)
		{
			++m_NameMatches;
			m_ClanMatches += Clan == Target.m_Clan;
		}
	}
};

inline bool QmFollowSameServer(std::span<const NETADDR> Left, std::span<const NETADDR> Right)
{
	for(NETADDR Address : Left)
	{
		// 协议版本影响连接方式。同一 IP 和端口仍对应同一个服务器端点。
		Address.type &= ~NETTYPE_TW7;
		for(NETADDR Other : Right)
		{
			Other.type &= ~NETTYPE_TW7;
			if(Address == Other)
				return true;
		}
	}
	return false;
}

struct SFollowServerCandidate
{
	std::string m_ConnectAddress;
	std::string m_ServerName;
	std::string m_MapName;
	std::vector<NETADDR> m_vAddresses;
	bool m_PlayersFull = false;
	bool m_Passworded = false;
	SFollowPlayerMatches m_Players;
};

enum class EFollowAction
{
	NONE,
	NOTIFY_PENDING_SWITCH,
	NOTIFY_SERVER_FULL,
	NOTIFY_PASSWORD_REQUIRED,
	CONNECT,
};

struct SFollowDecision
{
	EFollowAction m_Action = EFollowAction::NONE;
	const SFollowServerCandidate *m_pServer = nullptr;
	int m_SecondsRemaining = 0;
};

class CCrossServerFollowEngine
{
	struct SRetry
	{
		std::vector<NETADDR> m_vAddresses;
		double m_NotBefore;
		bool m_PasswordRejected;
	};

	struct SDepartedServer
	{
		std::vector<NETADDR> m_vAddresses;
		double m_Expires;
	};

	SFollowTarget m_Target;
	bool m_ConfirmedTogether = false;
	std::vector<NETADDR> m_vLastKnownAddresses;
	std::vector<SDepartedServer> m_vDepartedServers;
	std::vector<NETADDR> m_vPendingAddresses;
	double m_PendingSince = 0.0;
	bool m_CountdownNotified = false;
	EFollowAction m_BlockNotice = EFollowAction::NONE;
	std::vector<NETADDR> m_vAttemptAddresses;
	std::vector<SRetry> m_vRetries;

	void ClearPending()
	{
		m_vPendingAddresses.clear();
		m_CountdownNotified = false;
		m_BlockNotice = EFollowAction::NONE;
	}

	void RecordConnectionAttempt(const std::vector<NETADDR> &vAddresses, double Now)
	{
		if(!m_vLastKnownAddresses.empty() && !QmFollowSameServer(m_vLastKnownAddresses, vAddresses))
			m_vDepartedServers.push_back({std::move(m_vLastKnownAddresses), Now + 30.0});
		m_vLastKnownAddresses = vAddresses;
		m_vAttemptAddresses = vAddresses;
		m_ConfirmedTogether = false;
	}

	const SFollowServerCandidate *ResolveTargetServer(std::span<const SFollowServerCandidate> Candidates,
		std::span<const NETADDR> CurrentAddresses, SFollowPlayerMatches CurrentPlayers, bool IgnoreClan)
	{
		const bool PreferClan = !IgnoreClan && !m_Target.m_Clan.empty();
		int NameMatches = CurrentPlayers.m_NameMatches;
		bool HasClanMatch = CurrentPlayers.m_ClanMatches > 0;
		const auto Eligible = [&](const SFollowServerCandidate &Server) {
			return !Server.m_ConnectAddress.empty() && !Server.m_vAddresses.empty() &&
			       !QmFollowSameServer(CurrentAddresses, Server.m_vAddresses) &&
			       std::none_of(m_vDepartedServers.begin(), m_vDepartedServers.end(), [&](const SDepartedServer &Departed) {
				       return QmFollowSameServer(Departed.m_vAddresses, Server.m_vAddresses);
			       });
		};
		for(const auto &Server : Candidates)
		{
			if(!Eligible(Server))
				continue;
			NameMatches += Server.m_Players.m_NameMatches;
			HasClanMatch |= Server.m_Players.m_ClanMatches > 0;
		}

		const bool RequireClan = PreferClan && HasClanMatch;
		if(PreferClan && !HasClanMatch && NameMatches != 1)
			return nullptr;
		if(!CurrentAddresses.empty() && (RequireClan ? CurrentPlayers.m_ClanMatches : CurrentPlayers.m_NameMatches) > 0)
		{
			m_ConfirmedTogether = true;
			m_vLastKnownAddresses.assign(CurrentAddresses.begin(), CurrentAddresses.end());
			std::erase_if(m_vDepartedServers, [&](const SDepartedServer &Departed) {
				return QmFollowSameServer(Departed.m_vAddresses, CurrentAddresses);
			});
			return nullptr;
		}

		const SFollowServerCandidate *pLastKnown = nullptr;
		const SFollowServerCandidate *pNew = nullptr;
		for(const auto &Server : Candidates)
		{
			if((RequireClan ? Server.m_Players.m_ClanMatches : Server.m_Players.m_NameMatches) == 0 || !Eligible(Server))
				continue;
			if(QmFollowSameServer(m_vLastKnownAddresses, Server.m_vAddresses))
				pLastKnown = &Server;
			else if(!pNew)
				pNew = &Server;
			else if(!QmFollowSameServer(pNew->m_vAddresses, Server.m_vAddresses))
				return nullptr;
		}
		return pNew ? pNew : pLastKnown;
	}

public:
	static constexpr double RETRY_SECONDS = 6.0;

	const SFollowTarget &Target() const { return m_Target; }

	void Start(std::string_view Name, std::string_view Clan, const NETADDR *pCurrentServerAddr = nullptr, bool TargetAlreadyInDifferentServer = false)
	{
		SFollowTarget Target{!Name.empty(), std::string(Name), std::string(Clan)};
		Stop();
		if(!Target.m_Active)
			return;
		m_Target = std::move(Target);
		if(pCurrentServerAddr && !TargetAlreadyInDifferentServer)
		{
			m_ConfirmedTogether = true;
			m_vLastKnownAddresses.push_back(*pCurrentServerAddr);
		}
	}

	void Stop() { *this = CCrossServerFollowEngine(); }
	void OnManualConnectOrDisconnect() { Stop(); }

	bool IsFollowingServer(std::span<const NETADDR> Addresses) const
	{
		return QmFollowSameServer(m_vPendingAddresses, Addresses) ||
		       QmFollowSameServer(m_vLastKnownAddresses, Addresses);
	}

	bool TryManualConnectToTarget(std::span<const NETADDR> Addresses, double Now)
	{
		const auto MatchesAll = [&](std::span<const NETADDR> Destination) {
			return !Addresses.empty() && std::all_of(Addresses.begin(), Addresses.end(), [&](const NETADDR &Address) {
				return QmFollowSameServer(Destination, std::span(&Address, 1));
			});
		};
		if(MatchesAll(m_vPendingAddresses))
			RecordConnectionAttempt(m_vPendingAddresses, Now);
		else if(MatchesAll(m_vLastKnownAddresses))
			RecordConnectionAttempt(m_vLastKnownAddresses, Now);
		else
			return false;
		return true;
	}

	void OnConnectionEstablished(const NETADDR *pConnectedAddress = nullptr)
	{
		if(pConnectedAddress)
			std::erase_if(m_vRetries, [pConnectedAddress](const SRetry &Retry) {
				return QmFollowSameServer(Retry.m_vAddresses, std::span(pConnectedAddress, 1));
			});
		m_vAttemptAddresses.clear();
	}

	void OnConnectionFailed(double Now, bool PasswordRejected)
	{
		if(m_vAttemptAddresses.empty())
			return;
		std::erase_if(m_vRetries, [&](const SRetry &Retry) { return QmFollowSameServer(Retry.m_vAddresses, m_vAttemptAddresses); });
		m_vRetries.push_back({std::move(m_vAttemptAddresses), Now + RETRY_SECONDS, PasswordRejected});
		m_vAttemptAddresses.clear();
	}

	SFollowDecision Step(double Now, int DelaySeconds, bool Connecting,
		std::span<const SFollowServerCandidate> Candidates, std::span<const NETADDR> CurrentAddresses,
		SFollowPlayerMatches CurrentPlayers = {}, bool IgnoreClan = false)
	{
		if(!m_Target.m_Active || Connecting)
			return {};

		m_ConfirmedTogether &= QmFollowSameServer(m_vLastKnownAddresses, CurrentAddresses);
		// 短时间连续换服时，保留各旧服的缓存排除，直到名单消失或超时。
		std::erase_if(m_vDepartedServers, [&](const SDepartedServer &Departed) {
			return Now >= Departed.m_Expires || std::none_of(Candidates.begin(), Candidates.end(), [&](const auto &Server) {
				return Server.m_Players.m_NameMatches > 0 && QmFollowSameServer(Departed.m_vAddresses, Server.m_vAddresses);
			});
		});

		const auto *pServer = ResolveTargetServer(Candidates, CurrentAddresses, CurrentPlayers, IgnoreClan);
		if(!pServer)
		{
			ClearPending();
			return {};
		}
		if(!QmFollowSameServer(m_vPendingAddresses, pServer->m_vAddresses))
		{
			ClearPending();
			m_PendingSince = Now;
		}
		m_vPendingAddresses = pServer->m_vAddresses;

		std::erase_if(m_vRetries, [Now](const SRetry &Retry) { return !Retry.m_PasswordRejected && Now >= Retry.m_NotBefore; });
		const auto Retry = std::find_if(m_vRetries.begin(), m_vRetries.end(), [&](const SRetry &Entry) {
			return QmFollowSameServer(Entry.m_vAddresses, pServer->m_vAddresses);
		});
		EFollowAction Block = EFollowAction::NONE;
		if(pServer->m_Passworded || (Retry != m_vRetries.end() && Retry->m_PasswordRejected))
			Block = EFollowAction::NOTIFY_PASSWORD_REQUIRED;
		else if(pServer->m_PlayersFull)
			Block = EFollowAction::NOTIFY_SERVER_FULL;
		const EFollowAction PreviousBlock = std::exchange(m_BlockNotice, Block);
		if(Block != EFollowAction::NONE)
			return Block == PreviousBlock ? SFollowDecision{} : SFollowDecision{Block, pServer};

		const double Remaining = m_PendingSince + (m_ConfirmedTogether ? std::max(0, DelaySeconds) : 0) - Now;
		if(Remaining > 0.0)
		{
			if(std::exchange(m_CountdownNotified, true))
				return {};
			return {EFollowAction::NOTIFY_PENDING_SWITCH, pServer, static_cast<int>(std::ceil(Remaining))};
		}
		if(Retry != m_vRetries.end())
			return {};

		RecordConnectionAttempt(pServer->m_vAddresses, Now);
		return {EFollowAction::CONNECT, pServer};
	}
};

#endif
