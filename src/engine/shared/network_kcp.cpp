/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "config.h"
#include "network.h"

#include <base/system.h>

#include <algorithm>
#include <utility>

extern "C" {
#include <engine/external/kcp/ikcp.h>
}

static constexpr unsigned char s_aKcpMagic[] = {'Q', 'K', 'C', 'P'};
static constexpr unsigned char s_KcpVersion = 1;

static uint32_t KcpNowMs()
{
	return (uint32_t)(time_get() * 1000 / time_freq());
}

CNetKcpSession::~CNetKcpSession()
{
	Reset();
}

CNetKcpSession::CNetKcpSession(CNetKcpSession &&Other) noexcept
{
	*this = std::move(Other);
}

CNetKcpSession &CNetKcpSession::operator=(CNetKcpSession &&Other) noexcept
{
	if(this != &Other)
	{
		Reset();
		m_pKcp = Other.m_pKcp;
		m_Socket = Other.m_Socket;
		m_PeerAddr = Other.m_PeerAddr;
		m_Conv = Other.m_Conv;
		m_LastInputTime = Other.m_LastInputTime;
		m_LastOutputTime = Other.m_LastOutputTime;
		m_BytesSent = Other.m_BytesSent;
		m_BytesRecv = Other.m_BytesRecv;
		m_PacketsSent = Other.m_PacketsSent;
		m_PacketsRecv = Other.m_PacketsRecv;
		m_FastRetransmits = Other.m_FastRetransmits;
		m_TimeoutRetransmits = Other.m_TimeoutRetransmits;
		m_LastXmit = Other.m_LastXmit;
		m_LastRttMs = Other.m_LastRttMs;
		m_LastJitterMs = Other.m_LastJitterMs;
		m_LastWindow = Other.m_LastWindow;
		if(m_pKcp)
		{
			m_pKcp->user = this;
		}
		Other.m_pKcp = nullptr;
		Other.Reset();
	}
	return *this;
}

bool CNetKcpSession::Init(NETSOCKET Socket, const NETADDR &PeerAddr, uint32_t Conv)
{
	if(Socket == nullptr || Conv == 0)
		return false;

	IKCPCB *pKcp = ikcp_create(Conv, this);
	if(!pKcp)
		return false;
	if(ikcp_setmtu(pKcp, NET_KCP_MTU) != 0)
	{
		ikcp_release(pKcp);
		return false;
	}

	Reset();
	m_pKcp = pKcp;
	m_Socket = Socket;
	m_PeerAddr = PeerAddr;
	m_Conv = Conv;
	m_LastInputTime = time_get();
	m_LastOutputTime = m_LastInputTime;
	ikcp_setoutput(m_pKcp, KcpOutput);
	ApplyTuning();
	return true;
}

void CNetKcpSession::Reset()
{
	if(m_pKcp)
	{
		ikcp_release(m_pKcp);
		m_pKcp = nullptr;
	}
	m_Socket = nullptr;
	m_PeerAddr = {};
	m_Conv = 0;
	m_LastInputTime = 0;
	m_LastOutputTime = 0;
	m_BytesSent = 0;
	m_BytesRecv = 0;
	m_PacketsSent = 0;
	m_PacketsRecv = 0;
	m_FastRetransmits = 0;
	m_TimeoutRetransmits = 0;
	m_LastXmit = 0;
	m_LastRttMs = -1;
	m_LastJitterMs = 0;
	m_LastWindow = 0;
}

void CNetKcpSession::ApplyTuning()
{
	if(!m_pKcp)
		return;

	ikcp_nodelay(m_pKcp, 1, 10, 2, 0);
	ikcp_wndsize(m_pKcp, 64, 128);
	m_pKcp->rx_minrto = 30;
	m_pKcp->fastlimit = 5;
	m_pKcp->dead_link = 20;
}

void CNetKcpSession::SetPeerAddress(const NETADDR &PeerAddr)
{
	m_PeerAddr = PeerAddr;
}

bool CNetKcpSession::IsPeerAddress(const NETADDR &Addr) const
{
	return m_pKcp && m_PeerAddr == Addr;
}

bool CNetKcpSession::IsPeerAddressNoPort(const NETADDR &Addr) const
{
	return m_pKcp && net_addr_comp_noport(&m_PeerAddr, &Addr) == 0;
}

bool CNetKcpSession::IsKcpPacket(const unsigned char *pData, int DataSize)
{
	uint32_t Conv;
	const unsigned char *pPayload;
	int PayloadSize;
	return UnpackHeader(pData, DataSize, &Conv, &pPayload, &PayloadSize);
}

bool CNetKcpSession::UnpackHeader(const unsigned char *pData, int DataSize, uint32_t *pConv, const unsigned char **ppPayload, int *pPayloadSize)
{
	if(DataSize <= NET_KCP_HEADER_SIZE || !pData)
		return false;
	if(mem_comp(pData, s_aKcpMagic, sizeof(s_aKcpMagic)) != 0 || pData[4] != s_KcpVersion)
		return false;

	uint32_t Conv = 0;
	Conv |= (uint32_t)pData[5] << 24;
	Conv |= (uint32_t)pData[6] << 16;
	Conv |= (uint32_t)pData[7] << 8;
	Conv |= (uint32_t)pData[8];
	if(Conv == 0)
		return false;

	if(pConv)
		*pConv = Conv;
	if(ppPayload)
		*ppPayload = pData + NET_KCP_HEADER_SIZE;
	if(pPayloadSize)
		*pPayloadSize = DataSize - NET_KCP_HEADER_SIZE;
	return true;
}

int CNetKcpSession::PacketOutput(void *pUser, CNetPacketConstruct *pPacket, SECURITY_TOKEN SecurityToken, bool Sixup)
{
	return static_cast<CNetKcpSession *>(pUser)->SendPacket(pPacket, SecurityToken, Sixup);
}

bool CNetKcpSession::Input(const NETADDR &Addr, const unsigned char *pData, int DataSize, bool AllowRebind)
{
	if(!m_pKcp)
		return false;

	uint32_t Conv;
	const unsigned char *pPayload;
	int PayloadSize;
	if(!UnpackHeader(pData, DataSize, &Conv, &pPayload, &PayloadSize) || Conv != m_Conv)
		return false;

	const bool PeerAddressChanged = m_PeerAddr != Addr;
	if(PeerAddressChanged)
	{
		if(!AllowRebind || net_addr_comp_noport(&m_PeerAddr, &Addr) != 0)
			return false;
	}

	if(ikcp_input(m_pKcp, (const char *)pPayload, PayloadSize) < 0)
		return false;
	if(PeerAddressChanged)
		m_PeerAddr = Addr;

	m_LastInputTime = time_get();
	m_BytesRecv += (uint64_t)DataSize;
	m_PacketsRecv++;
	UpdateStats();
	return true;
}

int CNetKcpSession::Send(const void *pData, int DataSize)
{
	if(!m_pKcp || DataSize <= 0 || DataSize > NET_MAX_PACKETSIZE)
		return -1;
	if(PendingSegments() >= NET_KCP_MAX_PENDING_SEGMENTS)
		return -1;

	const int Result = ikcp_send(m_pKcp, (const char *)pData, DataSize);
	if(Result < 0)
		return Result;

	if(PendingSegments() >= NET_KCP_SOFT_PENDING_SEGMENTS)
		ikcp_flush(m_pKcp);
	Update();
	return 0;
}

int CNetKcpSession::SendPacket(CNetPacketConstruct *pPacket, SECURITY_TOKEN SecurityToken, bool Sixup)
{
	unsigned char aBuffer[NET_MAX_PACKETSIZE];
	const int PackedSize = CNetBase::PackPacket(aBuffer, sizeof(aBuffer), pPacket, SecurityToken, Sixup);
	if(PackedSize < 0)
		return -1;
	return Send(aBuffer, PackedSize);
}

int CNetKcpSession::Recv(void *pData, int MaxSize)
{
	if(!m_pKcp || !pData || MaxSize <= 0)
		return -1;
	const int Result = ikcp_recv(m_pKcp, (char *)pData, MaxSize);
	if(Result >= 0)
	{
		UpdateStats();
	}
	return Result;
}

int CNetKcpSession::PeekSize() const
{
	if(!m_pKcp)
		return -1;
	return ikcp_peeksize(m_pKcp);
}

void CNetKcpSession::Update()
{
	if(!m_pKcp)
		return;

	ikcp_update(m_pKcp, KcpNowMs());
	UpdateStats();
}

void CNetKcpSession::Flush()
{
	if(!m_pKcp)
		return;
	ikcp_flush(m_pKcp);
	UpdateStats();
}

bool CNetKcpSession::TimedOut(int TimeoutSeconds) const
{
	return m_pKcp &&
	       TimeoutSeconds > 0 &&
	       m_LastInputTime > 0 &&
	       time_get() - m_LastInputTime > time_freq() * TimeoutSeconds;
}

int CNetKcpSession::PendingSegments() const
{
	if(!m_pKcp)
		return 0;
	return ikcp_waitsnd(m_pKcp);
}

SNetTransportStats CNetKcpSession::Stats() const
{
	SNetTransportStats Stats;
	Stats.m_RttMs = m_LastRttMs;
	Stats.m_JitterMs = m_LastJitterMs;
	Stats.m_ResendCount = (int)std::min<uint64_t>(m_FastRetransmits + m_TimeoutRetransmits, 0x7fffffff);
	if(m_PacketsSent > 0)
		Stats.m_LossPermille = (int)std::min<uint64_t>((uint64_t)Stats.m_ResendCount * 1000 / m_PacketsSent, 1000);
	Stats.m_SendQueueDepth = PendingSegments();
	Stats.m_RecvQueueDepth = m_pKcp ? (int)(m_pKcp->nrcv_que + m_pKcp->nrcv_buf) : 0;
	Stats.m_BandwidthDown = (int)std::min<uint64_t>(m_BytesRecv, 0x7fffffff);
	Stats.m_BandwidthUp = (int)std::min<uint64_t>(m_BytesSent, 0x7fffffff);
	Stats.m_SessionId = m_Conv;
	return Stats;
}

void CNetKcpSession::UpdateStats()
{
	if(!m_pKcp)
		return;

	m_LastRttMs = m_pKcp->rx_srtt > 0 ? (int)m_pKcp->rx_srtt : -1;
	m_LastJitterMs = (int)m_pKcp->rx_rttval;
	const int OldWindow = m_LastWindow;
	m_LastWindow = (int)m_pKcp->cwnd;
	if(OldWindow > 0 && m_LastWindow < OldWindow)
	{
		m_FastRetransmits += (uint64_t)(OldWindow - m_LastWindow);
	}
	if(m_pKcp->xmit > (IUINT32)m_LastXmit)
	{
		m_TimeoutRetransmits += m_pKcp->xmit - (IUINT32)m_LastXmit;
	}
	m_LastXmit = m_pKcp->xmit;
}

int CNetKcpSession::KcpOutput(const char *pData, int DataSize, IKCPCB *pKcp, void *pUser)
{
	CNetKcpSession *pSession = static_cast<CNetKcpSession *>(pUser);
	if(!pSession || !pSession->m_Socket || !pData || DataSize <= 0 || DataSize > NET_KCP_MTU)
		return -1;

	unsigned char aBuffer[NET_MAX_PACKETSIZE];
	mem_copy(aBuffer, s_aKcpMagic, sizeof(s_aKcpMagic));
	aBuffer[4] = s_KcpVersion;
	aBuffer[5] = (pSession->m_Conv >> 24) & 0xff;
	aBuffer[6] = (pSession->m_Conv >> 16) & 0xff;
	aBuffer[7] = (pSession->m_Conv >> 8) & 0xff;
	aBuffer[8] = pSession->m_Conv & 0xff;
	mem_copy(aBuffer + NET_KCP_HEADER_SIZE, pData, DataSize);

	const int Sent = net_udp_send(pSession->m_Socket, &pSession->m_PeerAddr, aBuffer, DataSize + NET_KCP_HEADER_SIZE);
	if(Sent >= 0)
	{
		pSession->m_LastOutputTime = time_get();
		pSession->m_BytesSent += (uint64_t)(DataSize + NET_KCP_HEADER_SIZE);
		pSession->m_PacketsSent++;
	}
	(void)pKcp;
	return Sent;
}
