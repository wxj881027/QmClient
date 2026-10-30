#include "network.h"

#include <base/log.h>
#include <base/system.h>

#include <algorithm>

static int IndexFromNetType(int NetType)
{
	switch(NetType)
	{
	case NETTYPE_IPV6:
		return 0;
	case NETTYPE_IPV4:
		return 1;
	}
	return -1;
}

static const char *IndexToSystem(int Index)
{
	switch(Index)
	{
	case 0:
		return "stun/6";
	case 1:
		return "stun/4";
	}
	dbg_assert_failed("invalid index %d", Index);
}

static int RetryWaitSeconds(int NumUnsuccessfulTries)
{
	return (1 << std::clamp(NumUnsuccessfulTries, 0, 9));
}

// 连续发送失败达到该次数后停止该地址族的 STUN 尝试：发送层持续失败基本意味着
// 本机没有该地址族的路由（典型：无 IPv6 出口），指数退避重试只会长期刷日志。
// Refresh()/FeedStunServer() 重新武装时会清零计数。
static constexpr int STUN_SEND_FAILURES_BEFORE_GIVEUP = 3;

int64_t CStun::CDefaultOperations::Now()
{
	return time_get();
}

int64_t CStun::CDefaultOperations::Frequency()
{
	return time_freq();
}

void CStun::CDefaultOperations::RandomFill(void *pData, unsigned Size)
{
	secure_random_fill(pData, Size);
}

int CStun::CDefaultOperations::Send(NETSOCKET Socket, const NETADDR *pAddr, const void *pData, int Size)
{
	return net_udp_send(Socket, pAddr, pData, Size);
}

CStun::CProtocol::CProtocol(int Index, NETSOCKET Socket, IOperations *pOperations) :
	m_Index(Index),
	m_Socket(Socket),
	m_pOperations(pOperations)
{
	mem_zero(&m_StunServer, sizeof(NETADDR));
	m_pOperations->RandomFill(m_Stun.m_aSecret, sizeof(m_Stun.m_aSecret));
}

void CStun::CProtocol::FeedStunServer(NETADDR StunServer)
{
	if(m_HaveStunServer && net_addr_comp(&m_StunServer, &StunServer) == 0)
	{
		return;
	}
	m_HaveStunServer = true;
	m_StunServer = StunServer;
	m_NumUnsuccessfulTries = 0;
	Refresh();
}

void CStun::CProtocol::Refresh()
{
	m_NextTry = m_pOperations->Now();
	m_NumSendFailures = 0;
}

void CStun::CProtocol::Update()
{
	int64_t Now = m_pOperations->Now();
	if(m_NextTry == -1 || Now < m_NextTry || !m_HaveStunServer)
	{
		return;
	}
	m_NextTry = Now + RetryWaitSeconds(m_NumUnsuccessfulTries) * m_pOperations->Frequency();
	m_NumUnsuccessfulTries += 1;
	unsigned char aBuf[32];
	m_pOperations->RandomFill(m_Stun.m_aSecret, sizeof(m_Stun.m_aSecret));
	int Size = StunMessagePrepare(aBuf, sizeof(aBuf), m_Stun);
	if(m_pOperations->Send(m_Socket, &m_StunServer, aBuf, Size) == -1)
	{
		m_NumSendFailures += 1;
		// 首次失败照常记录便于诊断；连续失败达阈值则止损：停止重试直到
		// Refresh()/FeedStunServer() 重新武装，并把未成功次数推到
		// HaveTriedALittle 阈值，让 GetConnectivity 尽快报告 UNREACHABLE。
		if(m_NumSendFailures == 1)
		{
			log_debug(IndexToSystem(m_Index), "couldn't send stun request");
		}
		else if(m_NumSendFailures >= STUN_SEND_FAILURES_BEFORE_GIVEUP)
		{
			log_debug(IndexToSystem(m_Index), "couldn't send stun request %d times in a row, giving up until refresh", m_NumSendFailures);
			m_NextTry = -1;
			m_NumUnsuccessfulTries = std::max(m_NumUnsuccessfulTries, 5);
		}
		return;
	}
	m_NumSendFailures = 0;
}

bool CStun::CProtocol::OnPacket(NETADDR Addr, unsigned char *pData, int DataSize)
{
	if(m_NextTry < 0 || !m_HaveStunServer || DataSize < 0 || net_addr_comp(&Addr, &m_StunServer) != 0)
	{
		return false;
	}
	bool Success;
	NETADDR StunAddr;
	if(StunMessageParse(pData, DataSize, &m_Stun, &Success, &StunAddr))
	{
		return false;
	}
	m_LastResponse = m_pOperations->Now();
	if(!Success)
	{
		m_HaveAddr = false;
		log_debug(IndexToSystem(m_Index), "got error response");
		return true;
	}
	m_NextTry = -1;
	m_NumUnsuccessfulTries = -1;
	m_HaveAddr = true;
	m_Addr = StunAddr;

	char aStunAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&StunAddr, aStunAddr, sizeof(aStunAddr), true);
	log_debug(IndexToSystem(m_Index), "got address: %s", aStunAddr);
	return true;
}

CONNECTIVITY CStun::CProtocol::GetConnectivity(NETADDR *pGlobalAddr)
{
	if(!m_HaveStunServer)
	{
		return CONNECTIVITY::UNKNOWN;
	}
	int64_t Now = m_pOperations->Now();
	int64_t Freq = m_pOperations->Frequency();
	bool HaveTriedALittle = m_NumUnsuccessfulTries >= 5 && (m_LastResponse == -1 || Now - m_LastResponse >= 30 * Freq);
	if(m_LastResponse == -1 && !HaveTriedALittle)
	{
		return CONNECTIVITY::CHECKING;
	}
	else if(HaveTriedALittle)
	{
		return CONNECTIVITY::UNREACHABLE;
	}
	else if(!m_HaveAddr)
	{
		return CONNECTIVITY::REACHABLE;
	}
	else
	{
		*pGlobalAddr = m_Addr;
		return CONNECTIVITY::ADDRESS_KNOWN;
	}
}

CStun::CStun(NETSOCKET Socket, IOperations *pOperations) :
	m_aProtocols{CProtocol(0, Socket, pOperations ? pOperations : &m_DefaultOperations), CProtocol(1, Socket, pOperations ? pOperations : &m_DefaultOperations)}
{
}

void CStun::FeedStunServer(NETADDR StunServer)
{
	int Index = IndexFromNetType(StunServer.type);
	if(Index < 0)
	{
		return;
	}
	m_aProtocols[Index].FeedStunServer(StunServer);
}

void CStun::Refresh()
{
	for(auto &Protocol : m_aProtocols)
	{
		Protocol.Refresh();
	}
}

void CStun::Update()
{
	for(auto &Protocol : m_aProtocols)
	{
		Protocol.Update();
	}
}

bool CStun::OnPacket(NETADDR Addr, unsigned char *pData, int DataSize)
{
	int Index = IndexFromNetType(Addr.type);
	if(Index < 0)
	{
		return false;
	}
	return m_aProtocols[Index].OnPacket(Addr, pData, DataSize);
}

CONNECTIVITY CStun::GetConnectivity(int NetType, NETADDR *pGlobalAddr)
{
	int Index = IndexFromNetType(NetType);
	dbg_assert(Index != -1, "invalid nettype");
	return m_aProtocols[Index].GetConnectivity(pGlobalAddr);
}
