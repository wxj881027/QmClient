/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "config.h"
#include "network.h"

#include <base/system.h>
#include <base/types.h>

#include <engine/shared/protocol7.h>

#include <algorithm>

static constexpr int CNET_QOS_MAX_ATTEMPTS = 3;
static constexpr int CNET_QOS_RETRY_DELAY_SECONDS = 2;
// 控制包、STUN 包或无效包不能在单次 Recv 调用里长期占用主线程。
// 达到上限后由下一帧继续读取，UDP socket 本身不会丢弃尚未读取的数据。
static constexpr int CNET_MAX_DATAGRAMS_PER_RECV = 64;

bool CNetClient::Open(NETADDR BindAddr, bool LowLatency)
{
	// open socket
	NETSOCKET Socket;
	Socket = net_udp_create(BindAddr);
	if(!Socket)
		return false;
	Close();
	// clean it
	*this = CNetClient{};

	// init
	m_Socket = Socket;
	m_LowLatency = LowLatency;
	m_QosStatus = LowLatency ? ENetQosStatus::PENDING : ENetQosStatus::DISABLED;
	m_pStun = new CStun(m_Socket);
	m_Connection.Init(m_Socket, false);
	m_TokenCache.Init(m_Socket);

	return true;
}

void CNetClient::Close()
{
	if(!m_Socket)
	{
		return;
	}
	DeactivateKcp();
	m_QosPeerValidated = false;
	ResetQos();
	if(m_pStun)
	{
		delete m_pStun;
		m_pStun = nullptr;
	}
	net_udp_close(m_Socket);
	m_Socket = nullptr;
}

void CNetClient::Disconnect(const char *pReason)
{
	DeactivateKcp();
	m_QosPeerValidated = false;
	ResetQos();
	m_Connection.Disconnect(pReason);
}

void CNetClient::Drop(const char *pReason)
{
	DeactivateKcp();
	m_QosPeerValidated = false;
	ResetQos();
	m_Connection.Drop(pReason);
}

void CNetClient::ResetQos()
{
	net_qos_remove_socket(m_Qos);
	m_Qos = nullptr;
	m_QosAttempted = false;
	m_QosAttemptCount = 0;
	m_QosNextAttempt = 0;
	m_QosStatus = m_LowLatency ? ENetQosStatus::PENDING : ENetQosStatus::DISABLED;
}

const char *CNetClient::QosStatusName() const
{
	switch(m_QosStatus)
	{
	case ENetQosStatus::DISABLED: return "disabled";
	case ENetQosStatus::PENDING: return "pending";
	case ENetQosStatus::ACTIVE: return "active";
	case ENetQosStatus::UNAVAILABLE: return "unavailable";
	case ENetQosStatus::FAILED: return "failed";
	}
	dbg_assert_failed("invalid QoS status");
}

void CNetClient::SetLowLatency(bool LowLatency)
{
	if(m_LowLatency == LowLatency)
		return;
	m_LowLatency = LowLatency;
	ResetQos();
	if(m_LowLatency)
		TryConfigureQos();
}

void CNetClient::TryConfigureQos()
{
	if(!m_LowLatency || !m_QosPeerValidated || !m_Socket)
		return;
	if(m_QosAttempted && (m_QosNextAttempt == 0 || time_get() < m_QosNextAttempt))
		return;
	if(m_QosAttemptCount >= CNET_QOS_MAX_ATTEMPTS)
		return;
	const NETADDR *pPeerAddr = m_Connection.PeerAddress();
	if((pPeerAddr->type & (NETTYPE_IPV4 | NETTYPE_IPV6)) == 0)
		return;

	// qWAVE 由系统策略决定实际标记；Windows IPv4 仍保留原有低延迟 TOS，IPv6 回退为 Best Effort。
	m_QosAttempted = true;
	m_QosAttemptCount++;
	m_QosNextAttempt = 0;
	m_Qos = net_qos_add_socket(m_Socket, pPeerAddr, &m_QosStatus);
	if(!m_Qos && m_QosStatus == ENetQosStatus::FAILED && m_QosAttemptCount < CNET_QOS_MAX_ATTEMPTS)
		m_QosNextAttempt = time_get() + (int64_t)CNET_QOS_RETRY_DELAY_SECONDS * time_freq();
}

void CNetClient::Update()
{
	if(!m_Socket)
		return;

	if(m_Transport == ENetTransport::KCP)
	{
		m_Kcp.Update();
		m_TransportStats = m_Kcp.Stats();
		if(m_Kcp.TimedOut(g_Config.m_ConnTimeout))
		{
			Disconnect("KCP timeout");
			return;
		}
	}

	m_Connection.Update();
	TryConfigureQos();
	if(m_Connection.State() == CNetConnection::EState::ERROR)
		Disconnect(m_Connection.ErrorString());
	if(m_pStun)
		m_pStun->Update();
	m_TokenCache.Update();
}

void CNetClient::Connect(const NETADDR *pAddr, int NumAddrs)
{
	DeactivateKcp();
	m_QosPeerValidated = false;
	ResetQos();
	m_Connection.Connect(pAddr, NumAddrs);
}

void CNetClient::Connect7(const NETADDR *pAddr, int NumAddrs)
{
	DeactivateKcp();
	m_QosPeerValidated = false;
	ResetQos();
	m_Connection.Connect7(pAddr, NumAddrs);
}

void CNetClient::ResetErrorString()
{
	m_Connection.ResetErrorString();
}

int CNetClient::Recv(CNetChunk *pChunk, SECURITY_TOKEN *pResponseToken, bool Sixup)
{
	if(!m_Socket)
		return 0;

	int NumDatagrams = 0;
	while(true)
	{
		// Unpack next chunk from stored packet if available
		if(m_PacketChunkUnpacker.UnpackNextChunk(pChunk))
		{
			if(m_Connection.State() != CNetConnection::EState::OFFLINE)
				return 1;
			m_PacketChunkUnpacker.Reset();
		}
		if(FetchKcpChunk(pChunk, pResponseToken, Sixup))
			return 1;
		if(NumDatagrams >= CNET_MAX_DATAGRAMS_PER_RECV)
			return 0;

		// TODO: empty the recvinfo
		NETADDR Addr;
		unsigned char *pData;
		int Bytes = net_udp_recv(m_Socket, &Addr, &pData);

		// no more packets for now
		if(Bytes <= 0)
			break;
		++NumDatagrams;

		if(m_pStun && m_pStun->OnPacket(Addr, pData, Bytes))
		{
			continue;
		}

		if(CNetKcpSession::IsKcpPacket(pData, Bytes))
		{
			if(m_Transport == ENetTransport::KCP &&
				m_Connection.State() != CNetConnection::EState::OFFLINE &&
				m_Connection.State() != CNetConnection::EState::ERROR &&
				m_Kcp.Input(Addr, pData, Bytes, true))
			{
				const NETADDR PreviousPeerAddr = *m_Connection.PeerAddress();
				const bool PeerAddressUpdated = m_Connection.UpdatePeerAddressForRebind(Addr) && PreviousPeerAddr != *m_Connection.PeerAddress();
				m_Kcp.SetPeerAddress(Addr);
				if(PeerAddressUpdated)
				{
					ResetQos();
					TryConfigureQos();
				}
				m_TransportStats = m_Kcp.Stats();
			}
			continue;
		}

		SECURITY_TOKEN Token;
		if(CNetBase::UnpackPacket(pData, Bytes, &m_RecvBuffer, Sixup, true, &Token, pResponseToken) == 0)
		{
			if(Sixup)
			{
				Addr.type |= NETTYPE_TW7;
			}
			if(m_RecvBuffer.m_Flags & NET_PACKETFLAG_CONNLESS)
			{
				pChunk->m_Flags = NETSENDFLAG_CONNLESS;
				pChunk->m_ClientId = -1;
				pChunk->m_Address = Addr;
				pChunk->m_DataSize = m_RecvBuffer.m_DataSize;
				pChunk->m_pData = m_RecvBuffer.m_aChunkData;
				if(m_RecvBuffer.m_Flags & NET_PACKETFLAG_EXTENDED)
				{
					pChunk->m_Flags |= NETSENDFLAG_EXTENDED;
					mem_copy(pChunk->m_aExtraData, m_RecvBuffer.m_aExtraData, sizeof(pChunk->m_aExtraData));
				}
				return 1;
			}
			else
			{
				const bool Control = (m_RecvBuffer.m_Flags & NET_PACKETFLAG_CONTROL) != 0;
				if(Sixup &&
					Control &&
					m_RecvBuffer.m_DataSize >= 1 + (int)sizeof(SECURITY_TOKEN) &&
					m_RecvBuffer.m_aChunkData[0] == protocol7::NET_CTRLMSG_TOKEN)
				{
					m_TokenCache.AddToken(&Addr, *pResponseToken);
				}
				if(m_Connection.State() != CNetConnection::EState::OFFLINE &&
					m_Connection.State() != CNetConnection::EState::ERROR &&
					m_Connection.Feed(&m_RecvBuffer, &Addr, Token, *pResponseToken))
				{
					m_QosPeerValidated = true;
					// 首个有效响应确定服务器地址后再创建仅针对该目标的 QoS flow。
					TryConfigureQos();
					if(!Control &&
						m_RecvBuffer.m_DataSize > 0 &&
						m_RecvBuffer.m_NumChunks > 0)
					{
						m_PacketChunkUnpacker.FeedPacket(Addr, m_RecvBuffer, &m_Connection, 0);
					}
				}
			}
		}
	}
	return 0;
}

bool CNetClient::FetchKcpChunk(CNetChunk *pChunk, SECURITY_TOKEN *pResponseToken, bool Sixup)
{
	if(m_Transport != ENetTransport::KCP || !m_Kcp.IsActive())
		return false;

	const int Size = m_Kcp.PeekSize();
	if(Size <= 0 || Size > NET_MAX_PACKETSIZE)
		return false;

	unsigned char aBuffer[NET_MAX_PACKETSIZE];
	const int Bytes = m_Kcp.Recv(aBuffer, sizeof(aBuffer));
	if(Bytes <= 0)
		return false;

	SECURITY_TOKEN Token;
	*pResponseToken = NET_SECURITY_TOKEN_UNKNOWN;
	if(CNetBase::UnpackPacket(aBuffer, Bytes, &m_RecvBuffer, Sixup, true, &Token, pResponseToken) != 0)
		return false;

	NETADDR Addr = *m_Connection.PeerAddress();
	if(m_RecvBuffer.m_Flags & NET_PACKETFLAG_CONNLESS)
	{
		pChunk->m_Flags = NETSENDFLAG_CONNLESS;
		pChunk->m_ClientId = -1;
		pChunk->m_Address = Addr;
		pChunk->m_DataSize = m_RecvBuffer.m_DataSize;
		pChunk->m_pData = m_RecvBuffer.m_aChunkData;
		if(m_RecvBuffer.m_Flags & NET_PACKETFLAG_EXTENDED)
		{
			pChunk->m_Flags |= NETSENDFLAG_EXTENDED;
			mem_copy(pChunk->m_aExtraData, m_RecvBuffer.m_aExtraData, sizeof(pChunk->m_aExtraData));
		}
		return true;
	}

	const bool Control = (m_RecvBuffer.m_Flags & NET_PACKETFLAG_CONTROL) != 0;
	if(m_Connection.State() != CNetConnection::EState::OFFLINE &&
		m_Connection.State() != CNetConnection::EState::ERROR &&
		m_Connection.Feed(&m_RecvBuffer, &Addr, Token, *pResponseToken))
	{
		if(!Control &&
			m_RecvBuffer.m_DataSize > 0 &&
			m_RecvBuffer.m_NumChunks > 0)
		{
			m_PacketChunkUnpacker.FeedPacket(Addr, m_RecvBuffer, &m_Connection, 0);
			return m_PacketChunkUnpacker.UnpackNextChunk(pChunk);
		}
	}
	return false;
}

int CNetClient::Send(CNetChunk *pChunk)
{
	pChunk->AssertSizeSanity();

	if(pChunk->m_Flags & NETSENDFLAG_CONNLESS)
	{
		// send connectionless packet
		if(pChunk->m_Address.type & NETTYPE_TW7)
		{
			m_TokenCache.SendPacketConnless(pChunk);
		}
		else
		{
			CNetBase::SendPacketConnless(m_Socket, &pChunk->m_Address, pChunk->m_pData, pChunk->m_DataSize,
				pChunk->m_Flags & NETSENDFLAG_EXTENDED, pChunk->m_aExtraData);
		}
	}
	else
	{
		int Flags = 0;
		dbg_assert(pChunk->m_ClientId == 0, "erroneous client id");

		if(pChunk->m_Flags & NETSENDFLAG_VITAL)
			Flags = NET_CHUNKFLAG_VITAL;

		if(m_Transport == ENetTransport::KCP && Flags == 0)
		{
			if((pChunk->m_Flags & NETSENDFLAG_FLUSH) == 0 || !m_Connection.HasPendingPacketData())
				return SendLegacyBypass(pChunk);
			if(pChunk->m_DataSize > NET_MAX_CHUNK_SIZE)
				return SendLegacyBypass(pChunk);
			if(m_Kcp.PendingSegments() >= NET_KCP_MAX_PENDING_SEGMENTS)
				return -1;

			const int Result = m_Connection.QueueChunk(0, pChunk->m_DataSize, pChunk->m_pData);
			if(Result < 0)
				return Result;
			const int FlushResult = m_Connection.Flush();
			m_Kcp.Flush();
			if(FlushResult < 0)
			{
				return FlushResult;
			}
			return 0;
		}

		if(m_Transport == ENetTransport::KCP && m_Kcp.PendingSegments() >= NET_KCP_MAX_PENDING_SEGMENTS)
		{
			return -1;
		}

		const int Result = m_Connection.QueueChunk(Flags, pChunk->m_DataSize, pChunk->m_pData);
		if(Result < 0)
			return Result;

		if(pChunk->m_Flags & NETSENDFLAG_FLUSH)
		{
			const int FlushResult = m_Connection.Flush();
			if(m_Transport == ENetTransport::KCP)
				m_Kcp.Flush();
			if(FlushResult < 0)
				return FlushResult;
		}
	}
	return 0;
}

int CNetClient::SendLegacyBypass(CNetChunk *pChunk)
{
	dbg_assert(pChunk->m_ClientId == 0, "erroneous client id");
	if(pChunk->m_DataSize > NET_MAX_CHUNK_SIZE)
	{
		dbg_msg("netclient", "chunk payload too big. %d. dropping chunk", pChunk->m_DataSize);
		return -1;
	}

	CNetPacketConstruct Construct;
	mem_zero(&Construct, sizeof(Construct));
	Construct.m_Ack = m_Connection.AckSequence();
	Construct.m_NumChunks = 1;
	Construct.m_DataSize = 0;

	CNetChunkHeader Header;
	Header.m_Flags = 0;
	Header.m_Size = pChunk->m_DataSize;
	Header.m_Sequence = -1;
	unsigned char *pChunkData = Header.Pack(Construct.m_aChunkData, m_Connection.m_Sixup ? 6 : 4);
	mem_copy(pChunkData, pChunk->m_pData, pChunk->m_DataSize);
	Construct.m_DataSize = (int)(pChunkData + pChunk->m_DataSize - Construct.m_aChunkData);

	CNetBase::SendPacket(m_Socket, const_cast<NETADDR *>(m_Connection.PeerAddress()), &Construct, m_Connection.SecurityToken(), m_Connection.m_Sixup);
	return 0;
}

int CNetClient::State() const
{
	if(m_Connection.State() == CNetConnection::EState::ONLINE)
		return NETSTATE_ONLINE;
	if(m_Connection.State() == CNetConnection::EState::OFFLINE)
		return NETSTATE_OFFLINE;
	return NETSTATE_CONNECTING;
}

int CNetClient::Flush()
{
	const int Result = m_Connection.Flush();
	if(m_Transport == ENetTransport::KCP)
		m_Kcp.Flush();
	return Result;
}

bool CNetClient::ActivateKcp(uint32_t Conv)
{
	if(!m_Socket || m_Connection.State() == CNetConnection::EState::OFFLINE || m_Connection.State() == CNetConnection::EState::ERROR)
		return false;
	if(!m_Kcp.Init(m_Socket, *m_Connection.PeerAddress(), Conv))
		return false;
	m_Transport = ENetTransport::KCP;
	m_TransportStats = m_Kcp.Stats();
	m_Connection.SetPacketOutput(CNetKcpSession::PacketOutput, &m_Kcp);
	return true;
}

void CNetClient::DeactivateKcp()
{
	m_Connection.SetPacketOutput(nullptr, nullptr);
	m_Kcp.Reset();
	m_Transport = ENetTransport::LEGACY;
	m_TransportStats = {};
}

SNetTransportStats CNetClient::TransportStats() const
{
	if(m_Transport == ENetTransport::KCP)
		return m_Kcp.Stats();
	SNetTransportStats Stats = m_TransportStats;
	Stats.m_RttMs = m_Connection.LastRttMs();
	Stats.m_LossPermille = (int)std::clamp(m_Connection.PacketLoss() * 10.0f, 0.0f, 1000.0f);
	Stats.m_ResendCount = m_Connection.PendingResendCount();
	Stats.m_SendQueueDepth = Stats.m_ResendCount;
	return Stats;
}

bool CNetClient::GotProblems(int64_t MaxLatency) const
{
	return time_get() - m_Connection.LastRecvTime() > MaxLatency;
}

float CNetClient::PacketLoss() const
{
	return m_Connection.PacketLoss();
}

int CNetClient::PendingResendCount() const
{
	return m_Connection.PendingResendCount();
}

const char *CNetClient::ErrorString() const
{
	return m_Connection.ErrorString();
}

void CNetClient::FeedStunServer(NETADDR StunServer)
{
	if(m_pStun)
		m_pStun->FeedStunServer(StunServer);
}

void CNetClient::RefreshStun()
{
	if(m_pStun)
		m_pStun->Refresh();
}

CONNECTIVITY CNetClient::GetConnectivity(int NetType, NETADDR *pGlobalAddr)
{
	if(!m_pStun)
		return CONNECTIVITY::UNKNOWN;
	return m_pStun->GetConnectivity(NetType, pGlobalAddr);
}
