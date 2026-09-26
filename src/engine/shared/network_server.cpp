/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "config.h"
#include "netban.h"
#include "network.h"

#include <base/hash_ctxt.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/shared/compression.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>

#include <algorithm>

const int g_DummyMapCrc = 0x6AF73DAF;
const unsigned char g_aDummyMapData[] = {
	0x44, 0x41, 0x54, 0x41, 0x04, 0x00, 0x00, 0x00, 0x10, 0x01, 0x00, 0x00,
	0xF4, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0xAC, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00,
	0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
	0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x1C, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x3C, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
	0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
	0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x05, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
	0xFF, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
	0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x78, 0x9C, 0x63, 0x64,
	0x60, 0x60, 0x60, 0x44, 0xC2, 0x00, 0x00, 0x38, 0x00, 0x05, 0x78, 0x9C,
	0x63, 0x64, 0x60, 0x60, 0x60, 0x44, 0xC2, 0x00, 0x00, 0x38, 0x00, 0x05};

const char *NetTransportName(ENetTransport Transport)
{
	switch(Transport)
	{
	case ENetTransport::LEGACY:
		return "legacy";
	case ENetTransport::KCP:
		return "kcp";
	}
	return "unknown";
}

bool CNetServer::Open(NETADDR BindAddr, CNetBan *pNetBan, int MaxClients, int MaxClientsPerIp)
{
	// zero out the whole structure
	this->~CNetServer();
	new(this) CNetServer{};

	// open socket
	m_Socket = net_udp_create(BindAddr);
	if(!m_Socket)
		return false;

	m_Address = BindAddr;
	m_pNetBan = pNetBan;

	m_MaxClients = std::clamp(MaxClients, 1, (int)NET_MAX_CLIENTS);
	m_MaxClientsPerIp = MaxClientsPerIp;

	m_VConnNum = 0;
	m_VConnFirst = 0;

	secure_random_fill(m_aSecurityTokenSeed, sizeof(m_aSecurityTokenSeed));
	secure_random_fill(&m_FakeNetSeed, sizeof(m_FakeNetSeed));
	if(m_FakeNetSeed == 0)
		m_FakeNetSeed = 1;

	for(auto &Slot : m_aSlots)
		Slot.m_Connection.Init(m_Socket, true);

	return true;
}

int CNetServer::SetCallbacks(NETFUNC_NEWCLIENT pfnNewClient, NETFUNC_DELCLIENT pfnDelClient, void *pUser)
{
	m_pfnNewClient = pfnNewClient;
	m_pfnDelClient = pfnDelClient;
	m_pUser = pUser;
	return 0;
}

int CNetServer::SetCallbacks(NETFUNC_NEWCLIENT pfnNewClient, NETFUNC_NEWCLIENT_NOAUTH pfnNewClientNoAuth, NETFUNC_CLIENTREJOIN pfnClientRejoin, NETFUNC_DELCLIENT pfnDelClient, void *pUser)
{
	m_pfnNewClient = pfnNewClient;
	m_pfnNewClientNoAuth = pfnNewClientNoAuth;
	m_pfnClientRejoin = pfnClientRejoin;
	m_pfnDelClient = pfnDelClient;
	m_pUser = pUser;
	return 0;
}

void CNetServer::Close()
{
	if(!m_Socket)
	{
		return;
	}
	for(int i = 0; i < MaxClients(); ++i)
	{
		DeactivateKcp(i);
	}
	net_udp_close(m_Socket);
	m_Socket = nullptr;
}

void CNetServer::Drop(int ClientId, const char *pReason)
{
	// TODO: insert lots of checks here

	if(m_pfnDelClient)
		m_pfnDelClient(ClientId, pReason, m_pUser);

	DeactivateKcp(ClientId);
	m_aSlots[ClientId].m_Connection.Disconnect(pReason);
}

void CNetServer::Update()
{
	m_NumRecvPackets = 0;
	const int64_t Now = time_get();
	if(Now > m_BudgetStart + time_freq())
	{
		m_BudgetStart = Now;
		m_NumPreConnDecompress = 0;
		m_NumBanReplies = 0;
		m_NumVanConnReplies = 0;
	}

	for(int i = 0; i < MaxClients(); i++)
	{
		CSlot &Slot = m_aSlots[i];
		if(Slot.m_Transport == ENetTransport::KCP)
		{
			Slot.m_Kcp.Update();
			Slot.m_TransportStats = Slot.m_Kcp.Stats();
			if(Slot.m_Kcp.TimedOut(g_Config.m_ConnTimeout))
			{
				if(!Slot.m_Connection.m_TimeoutProtected)
				{
					Drop(i, "KCP timeout");
					continue;
				}

				// Preserve timeout-protected tees on KCP the same way legacy UDP does.
				Slot.m_Connection.SetTimedOut("Timeout");
				DeactivateKcp(i);
			}
		}

		Slot.m_Connection.Update();
		if(Slot.m_Connection.State() == CNetConnection::EState::ERROR &&
			(!Slot.m_Connection.m_TimeoutProtected ||
				!Slot.m_Connection.m_TimeoutSituation))
		{
			Drop(i, Slot.m_Connection.ErrorString());
		}
	}
}

void CNetServer::EndFlushBatch()
{
	m_FlushBatch = false;
	for(int ClientId = 0; ClientId < MaxClients(); ClientId++)
	{
		if(!m_aFlushPending[ClientId])
			continue;
		m_aFlushPending[ClientId] = false;
		if(m_aSlots[ClientId].m_Connection.State() != CNetConnection::EState::ONLINE)
			continue;

		m_aSlots[ClientId].m_Connection.Flush();
		if(m_aSlots[ClientId].m_Transport == ENetTransport::KCP && m_aSlots[ClientId].m_Kcp.IsActive())
			m_aSlots[ClientId].m_Kcp.Flush();
	}
}

SNetTransportStats CNetServer::ClientTransportStats(int ClientId) const
{
	const CSlot &Slot = m_aSlots[ClientId];
	SNetTransportStats Stats = Slot.m_Transport == ENetTransport::KCP ? Slot.m_Kcp.Stats() : Slot.m_TransportStats;
	if(Slot.m_Transport == ENetTransport::LEGACY)
	{
		Stats.m_LossPermille = (int)std::clamp(Slot.m_Connection.PacketLoss() * 10.0f, 0.0f, 1000.0f);
		Stats.m_ResendCount = Slot.m_Connection.PendingResendCount();
		Stats.m_RttMs = -1;
		Stats.m_SendQueueDepth = Stats.m_ResendCount;
	}
	Stats.m_SessionCount = KcpSessionCount();
	return Stats;
}

int CNetServer::KcpSessionCount() const
{
	int Count = 0;
	for(int ClientId = 0; ClientId < MaxClients(); ++ClientId)
	{
		if(m_aSlots[ClientId].m_Transport == ENetTransport::KCP &&
			m_aSlots[ClientId].m_Connection.State() != CNetConnection::EState::OFFLINE &&
			m_aSlots[ClientId].m_Connection.State() != CNetConnection::EState::ERROR)
		{
			++Count;
		}
	}
	return Count;
}

bool CNetServer::KcpConvInUse(uint32_t Conv) const
{
	if(Conv == 0)
		return true;
	for(int ClientId = 0; ClientId < MaxClients(); ++ClientId)
	{
		const CSlot &Slot = m_aSlots[ClientId];
		if(Slot.m_PendingKcpConv == Conv)
			return true;
		if(Slot.m_Kcp.IsActive() && Slot.m_Kcp.Conv() == Conv)
			return true;
	}
	return false;
}

uint32_t CNetServer::NewKcpConv()
{
	for(int Attempts = 0; Attempts < 32; ++Attempts)
	{
		uint32_t Conv;
		secure_random_fill(&Conv, sizeof(Conv));
		Conv &= 0x7fffffffu;
		Conv |= 1u;
		if(!KcpConvInUse(Conv))
			return Conv;
	}
	return 0;
}

bool CNetServer::ActivateKcp(int ClientId, uint32_t Conv)
{
	dbg_assert(ClientId >= 0 && ClientId < MaxClients(), "invalid client id");
	CSlot &Slot = m_aSlots[ClientId];
	if(!m_Socket || Slot.m_Connection.State() == CNetConnection::EState::OFFLINE || Slot.m_Connection.State() == CNetConnection::EState::ERROR)
		return false;
	if(!Slot.m_Kcp.Init(m_Socket, *Slot.m_Connection.PeerAddress(), Conv))
		return false;
	Slot.m_Transport = ENetTransport::KCP;
	Slot.m_TransportStats = Slot.m_Kcp.Stats();
	Slot.m_PendingKcpConv = 0;
	Slot.m_Connection.SetPacketOutput(CNetKcpSession::PacketOutput, &Slot.m_Kcp);
	return true;
}

void CNetServer::PrepareKcpUpgrade(int ClientId, uint32_t Conv)
{
	dbg_assert(ClientId >= 0 && ClientId < MaxClients(), "invalid client id");
	m_aSlots[ClientId].m_PendingKcpConv = Conv;
}

void CNetServer::DeactivateKcp(int ClientId)
{
	dbg_assert(ClientId >= 0 && ClientId < MaxClients(), "invalid client id");
	CSlot &Slot = m_aSlots[ClientId];
	Slot.m_Connection.SetPacketOutput(nullptr, nullptr);
	Slot.m_Kcp.Reset();
	Slot.m_Transport = ENetTransport::LEGACY;
	Slot.m_TransportStats = {};
	Slot.m_PendingKcpConv = 0;
}

SECURITY_TOKEN CNetServer::GetGlobalToken()
{
	static const NETADDR NULL_ADDR = {0};
	return GetToken(NULL_ADDR);
}
SECURITY_TOKEN CNetServer::GetToken(const NETADDR &Addr)
{
	SHA256_CTX Sha256;
	sha256_init(&Sha256);
	sha256_update(&Sha256, (unsigned char *)m_aSecurityTokenSeed, sizeof(m_aSecurityTokenSeed));
	sha256_update(&Sha256, (unsigned char *)&Addr, 20); // omit port, bad idea!

	SECURITY_TOKEN SecurityToken = ToSecurityToken(sha256_finish(&Sha256).data);

	if(SecurityToken == NET_SECURITY_TOKEN_UNKNOWN ||
		SecurityToken == NET_SECURITY_TOKEN_UNSUPPORTED)
		SecurityToken = 1;

	return SecurityToken;
}

SECURITY_TOKEN CNetServer::GetVanillaToken(const NETADDR &Addr)
{
	// vanilla token/gametick shouldn't be negative
	return absolute(GetToken(Addr));
}

void CNetServer::SendControl(NETADDR &Addr, int ControlMsg, const void *pExtra, int ExtraSize, SECURITY_TOKEN SecurityToken)
{
	CNetBase::SendControlMsg(m_Socket, &Addr, 0, ControlMsg, pExtra, ExtraSize, SecurityToken);
}

int CNetServer::NumClientsWithAddr(NETADDR Addr)
{
	int FoundAddr = 0;
	for(int i = 0; i < MaxClients(); ++i)
	{
		if(m_aSlots[i].m_Connection.State() == CNetConnection::EState::OFFLINE ||
			(m_aSlots[i].m_Connection.State() == CNetConnection::EState::ERROR &&
				(!m_aSlots[i].m_Connection.m_TimeoutProtected ||
					!m_aSlots[i].m_Connection.m_TimeoutSituation)))
			continue;

		if(!net_addr_comp_noport(&Addr, m_aSlots[i].m_Connection.PeerAddress()))
			FoundAddr++;
	}

	return FoundAddr;
}

bool CNetServer::Connlimit(NETADDR Addr)
{
	int64_t Now = time_get();
	int Oldest = 0;

	for(int i = 0; i < NET_CONNLIMIT_IPS; ++i)
	{
		if(!net_addr_comp_noport(&m_aSpamConns[i].m_Addr, &Addr))
		{
			m_aSpamConns[i].m_LastSeen = Now;
			if(m_aSpamConns[i].m_Time > Now - time_freq() * g_Config.m_SvConnlimitTime)
			{
				if(m_aSpamConns[i].m_Conns >= g_Config.m_SvConnlimit)
					return true;
			}
			else
			{
				m_aSpamConns[i].m_Time = Now;
				m_aSpamConns[i].m_Conns = 0;
			}
			m_aSpamConns[i].m_Conns++;
			return false;
		}

		if(m_aSpamConns[i].m_LastSeen < m_aSpamConns[Oldest].m_LastSeen)
			Oldest = i;
	}

	m_aSpamConns[Oldest].m_Addr = Addr;
	m_aSpamConns[Oldest].m_Time = Now;
	m_aSpamConns[Oldest].m_LastSeen = Now;
	m_aSpamConns[Oldest].m_Conns = 1;
	return false;
}

int CNetServer::TryAcceptClient(NETADDR &Addr, SECURITY_TOKEN SecurityToken, int Slot, bool VanillaAuth, bool Sixup, SECURITY_TOKEN Token)
{
	if(Sixup && !g_Config.m_SvSixup)
	{
		const char aMsg[] = "0.7 connections are not accepted at this time";
		CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, aMsg, sizeof(aMsg), SecurityToken, Sixup);
		return -1; // failed to add client?
	}

	// 官方 7131ad28b：Slot 不为 -1 表示这是已有连接的重连，跳过限流与找空槽
	const bool Reconnect = Slot != -1;
	if(Reconnect)
	{
		if(g_Config.m_Debug)
			dbg_msg("security", "client %d reconnect", Slot);
	}
	else
	{
		if(Connlimit(Addr))
		{
			const char aMsg[] = "Too many connections in a short time";
			CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, aMsg, sizeof(aMsg), SecurityToken, Sixup);
			return -1; // failed to add client
		}

		// check for sv_max_clients_per_ip
		if(NumClientsWithAddr(Addr) + 1 > m_MaxClientsPerIp)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Only %d players with the same IP are allowed", m_MaxClientsPerIp);
			CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, aBuf, str_length(aBuf) + 1, SecurityToken, Sixup);
			return -1; // failed to add client
		}

		Slot = -1;
		for(int i = 0; i < MaxClients(); i++)
		{
			if(m_aSlots[i].m_Connection.State() == CNetConnection::EState::OFFLINE)
			{
				Slot = i;
				break;
			}
		}

		if(Slot == -1)
		{
			const char aFullMsg[] = "This server is full";
			CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, aFullMsg, sizeof(aFullMsg), SecurityToken, Sixup);

			return -1; // failed to add client
		}
	}

	// init connection slot
	m_aSlots[Slot].m_Connection.DirectInit(Addr, SecurityToken, Token, Sixup);
	DeactivateKcp(Slot);

	if(VanillaAuth)
	{
		// client sequence is unknown if the auth was done
		// connection-less
		m_aSlots[Slot].m_Connection.SetUnknownSeq();
		// correct sequence
		m_aSlots[Slot].m_Connection.SetSequence(6);
	}

	if(g_Config.m_Debug)
	{
		dbg_msg("security", "client accepted %s", m_aSlots[Slot].m_Connection.PeerAddressString(true).data());
	}

	if(Reconnect)
		m_pfnClientRejoin(Slot, m_pUser, Sixup, VanillaAuth);
	else if(VanillaAuth)
		m_pfnNewClientNoAuth(Slot, m_pUser);
	else
		m_pfnNewClient(Slot, m_pUser, Sixup);

	return Slot; // done
}

void CNetServer::SendMsgs(NETADDR &Addr, const CPacker **ppMsgs, int Num)
{
	dbg_assert(Num > 0 && Num <= NET_MAX_PACKET_CHUNKS, "Number of messages invalid: %d", Num);

	CNetPacketConstruct Construct;
	mem_zero(&Construct, sizeof(Construct));
	unsigned char *pChunkData = &Construct.m_aChunkData[Construct.m_DataSize];

	for(int i = 0; i < Num; i++)
	{
		const CPacker *pMsg = ppMsgs[i];
		CNetChunkHeader Header;
		Header.m_Flags = NET_CHUNKFLAG_VITAL;
		Header.m_Size = pMsg->Size();
		Header.m_Sequence = i + 1;
		pChunkData = Header.Pack(pChunkData);
		mem_copy(pChunkData, pMsg->Data(), pMsg->Size());
		pChunkData += pMsg->Size();
		Construct.m_NumChunks++;
	}

	Construct.m_DataSize = (int)(pChunkData - Construct.m_aChunkData);
	CNetBase::SendPacket(m_Socket, &Addr, &Construct, NET_SECURITY_TOKEN_UNSUPPORTED);
}

// connection-less msg packet without token-support
void CNetServer::OnPreConnMsg(NETADDR &Addr, CNetPacketConstruct &Packet, int Slot)
{
	bool IsCtrl = Packet.m_Flags & NET_PACKETFLAG_CONTROL;
	int CtrlMsg = Packet.m_aChunkData[0];

	// 官方 7131ad28b：只处理新连接或还没完成 token 握手的连接
	const bool NewAuthOrRejoin = Slot == -1 || m_aSlots[Slot].m_Connection.SecurityToken() == NET_SECURITY_TOKEN_UNKNOWN ||
				     (IsCtrl && m_aSlots[Slot].m_Connection.SecurityToken() == NET_SECURITY_TOKEN_UNSUPPORTED);
	if(!NewAuthOrRejoin)
		return;

	if(IsCtrl && CtrlMsg == NET_CTRLMSG_CONNECT)
	{
		if(g_Config.m_SvVanillaAntiSpoof && g_Config.m_Password[0] == '\0')
		{
			const int64_t Now = time_get();
			if(Now > m_VConnFirst + time_freq())
			{
				m_VConnFirst = Now;
				m_VConnNum = 0;
			}
			m_VConnNum++;
			m_NumVanConnReplies++;
			if(g_Config.m_SvVanConnRepliesPerSecond != 0 &&
				m_NumVanConnReplies > g_Config.m_SvVanConnRepliesPerSecond)
				return;

			// detect flooding
			const bool Flooding = g_Config.m_SvVanConnPerSecond != 0 && m_VConnNum > g_Config.m_SvVanConnPerSecond;

			if(g_Config.m_Debug && Flooding)
			{
				dbg_msg("security", "vanilla connection flooding detected");
			}

			// simulate accept
			SendControl(Addr, NET_CTRLMSG_CONNECTACCEPT, nullptr, 0, NET_SECURITY_TOKEN_UNSUPPORTED);

			// Begin vanilla compatible token handshake
			// The idea is to pack a security token in the gametick
			// parameter of NETMSG_SNAPEMPTY. The Client then will
			// return the token/gametick in NETMSG_INPUT, allowing
			// us to validate the token.
			// https://github.com/eeeee/ddnet/commit/b8e40a244af4e242dc568aa34854c5754c75a39a

			// Before we can send NETMSG_SNAPEMPTY, the client needs
			// to load a map, otherwise it might crash. The map
			// should be as small as is possible and directly available
			// to the client. Therefore a dummy map is sent in the same
			// packet. To reduce the traffic we'll fallback to a default
			// map if there are too many connection attempts at once.

			// send mapchange + map data + con_ready + 3 x empty snap (with token)
			CPacker MapChangeMsg;
			MapChangeMsg.Reset();
			MapChangeMsg.AddInt((NETMSG_MAP_CHANGE << 1) | 1);
			if(Flooding)
			{
				// Fallback to dm1
				MapChangeMsg.AddString("dm1", 0);
				MapChangeMsg.AddInt(0xf2159e6e);
				MapChangeMsg.AddInt(5805);
			}
			else
			{
				// dummy map
				MapChangeMsg.AddString("dummy", 0);
				MapChangeMsg.AddInt(g_DummyMapCrc);
				MapChangeMsg.AddInt(sizeof(g_aDummyMapData));
			}

			CPacker MapDataMsg;
			MapDataMsg.Reset();
			MapDataMsg.AddInt((NETMSG_MAP_DATA << 1) | 1);
			if(Flooding)
			{
				// send empty map data to keep 0.6.4 support
				MapDataMsg.AddInt(1); // last chunk
				MapDataMsg.AddInt(0); // crc
				MapDataMsg.AddInt(0); // chunk index
				MapDataMsg.AddInt(0); // map size
				// no map data
			}
			else
			{
				// send dummy map data
				MapDataMsg.AddInt(1); // last chunk
				MapDataMsg.AddInt(g_DummyMapCrc); // crc
				MapDataMsg.AddInt(0); // chunk index
				MapDataMsg.AddInt(sizeof(g_aDummyMapData)); // map size
				MapDataMsg.AddRaw(g_aDummyMapData, sizeof(g_aDummyMapData)); // map data
			}

			CPacker ConReadyMsg;
			ConReadyMsg.Reset();
			ConReadyMsg.AddInt((NETMSG_CON_READY << 1) | 1);

			CPacker SnapEmptyMsg;
			SnapEmptyMsg.Reset();
			SnapEmptyMsg.AddInt((NETMSG_SNAPEMPTY << 1) | 1);
			SECURITY_TOKEN SecurityToken = GetVanillaToken(Addr);
			SnapEmptyMsg.AddInt(SecurityToken);
			SnapEmptyMsg.AddInt(SecurityToken + 1);

			// send all chunks/msgs in one packet
			const CPacker *apMsgs[] = {&MapChangeMsg, &MapDataMsg, &ConReadyMsg,
				&SnapEmptyMsg, &SnapEmptyMsg, &SnapEmptyMsg};
			SendMsgs(Addr, apMsgs, std::size(apMsgs));
		}
		else
		{
			// accept client directly
			SendControl(Addr, NET_CTRLMSG_CONNECTACCEPT, nullptr, 0, NET_SECURITY_TOKEN_UNSUPPORTED);

			TryAcceptClient(Addr, NET_SECURITY_TOKEN_UNSUPPORTED, Slot);
		}
	}
	else if(!IsCtrl && g_Config.m_SvVanillaAntiSpoof && g_Config.m_Password[0] == '\0')
	{
		// The chunk header is two bytes, three for vital chunks.
		if(Packet.m_DataSize < 2)
			return;
		CNetChunkHeader Header;
		unsigned char *pData = Header.Unpack(Packet.m_aChunkData);
		const int Remaining = Packet.m_DataSize - (int)(pData - Packet.m_aChunkData);
		if(Remaining < 0)
			return;
		CUnpacker Unpacker;
		Unpacker.Reset(pData, std::min(Header.m_Size, Remaining));
		int Msg = Unpacker.GetInt() >> 1;

		if(Msg == NETMSG_INPUT)
		{
			SECURITY_TOKEN SecurityToken = Unpacker.GetInt();
			if(SecurityToken == GetVanillaToken(Addr))
			{
				if(g_Config.m_Debug)
					dbg_msg("security", "new client (vanilla handshake)");
				// try to accept client skipping auth state
				TryAcceptClient(Addr, NET_SECURITY_TOKEN_UNSUPPORTED, Slot, true);
			}
			else if(g_Config.m_Debug)
			{
				dbg_msg("security", "invalid token (vanilla handshake)");
			}
		}
		else
		{
			if(g_Config.m_Debug)
			{
				dbg_msg("security", "invalid preconn msg %d", Msg);
			}
		}
	}
}

void CNetServer::OnTokenCtrlMsg(NETADDR &Addr, int ControlMsg, const CNetPacketConstruct &Packet, int Slot)
{
	if(ControlMsg == NET_CTRLMSG_CONNECT)
	{
		// response connection request with token
		SECURITY_TOKEN Token = GetToken(Addr);
		SendControl(Addr, NET_CTRLMSG_CONNECTACCEPT, SECURITY_TOKEN_MAGIC, sizeof(SECURITY_TOKEN_MAGIC), Token);

		if(g_Config.m_Debug && Slot != -1)
			dbg_msg("security", "client %d wants to reconnect (ddnet)", Slot);
	}
	else if(ControlMsg == NET_CTRLMSG_ACCEPT)
	{
		SECURITY_TOKEN Token = ToSecurityToken(&Packet.m_aChunkData[1]);
		if(Token == GetToken(Addr))
		{
			// correct token
			// try to accept client
			if(g_Config.m_Debug)
				dbg_msg("security", "new client (ddnet token)");
			TryAcceptClient(Addr, Token, Slot);
		}
		else
		{
			// invalid token
			if(g_Config.m_Debug)
				dbg_msg("security", "invalid token");
		}
	}
}

int CNetServer::OnSixupCtrlMsg(NETADDR &Addr, CNetChunk *pChunk, int ControlMsg, const CNetPacketConstruct &Packet, SECURITY_TOKEN &ResponseToken, SECURITY_TOKEN Token, int Slot)
{
	if(Packet.m_DataSize < 1 + (int)sizeof(SECURITY_TOKEN))
		return 0; // silently ignore

	ResponseToken = ToSecurityToken(Packet.m_aChunkData + 1);

	if(ControlMsg == protocol7::NET_CTRLMSG_TOKEN)
	{
		if(g_Config.m_Debug && Slot != -1)
			dbg_msg("security", "client %d wants to reconnect (0.7)", Slot);

		if(Packet.m_DataSize >= (int)NET_TOKENREQUEST_DATASIZE)
		{
			SendTokenSixup(Addr, ResponseToken);
			return 0;
		}

		// Is this behaviour safe to rely on?
		pChunk->m_Flags = 0;
		pChunk->m_ClientId = -1;
		pChunk->m_Address = Addr;
		pChunk->m_DataSize = 0;
		return 1;
	}
	else if(ControlMsg == NET_CTRLMSG_CONNECT)
	{
		SECURITY_TOKEN MyToken = GetToken(Addr);
		unsigned char aToken[sizeof(SECURITY_TOKEN)];
		mem_copy(aToken, &MyToken, sizeof(aToken));

		CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CONNECTACCEPT, aToken, sizeof(aToken), ResponseToken, true);
		if(Token == MyToken)
			TryAcceptClient(Addr, ResponseToken, Slot, false, true, Token);
	}

	return 0;
}

int CNetServer::GetClientSlot(const NETADDR &Addr)
{
	for(int i = 0; i < MaxClients(); i++)
	{
		if(m_aSlots[i].m_Connection.State() != CNetConnection::EState::OFFLINE &&
			m_aSlots[i].m_Connection.State() != CNetConnection::EState::ERROR &&
			net_addr_comp(m_aSlots[i].m_Connection.PeerAddress(), &Addr) == 0)
		{
			return i;
		}
	}
	return -1;
}

static bool IsDDNetControlMsg(const CNetPacketConstruct *pPacket)
{
	if(!(pPacket->m_Flags & NET_PACKETFLAG_CONTROL) || pPacket->m_DataSize < 1)
	{
		return false;
	}
	if(pPacket->m_aChunkData[0] == NET_CTRLMSG_CONNECT && pPacket->m_DataSize >= (int)(1 + sizeof(SECURITY_TOKEN_MAGIC) + sizeof(SECURITY_TOKEN)) && mem_comp(&pPacket->m_aChunkData[1], SECURITY_TOKEN_MAGIC, sizeof(SECURITY_TOKEN_MAGIC)) == 0)
	{
		// DDNet CONNECT
		return true;
	}
	if(pPacket->m_aChunkData[0] == NET_CTRLMSG_ACCEPT && pPacket->m_DataSize >= 1 + (int)sizeof(SECURITY_TOKEN))
	{
		// DDNet ACCEPT
		return true;
	}
	return false;
}

bool CNetServer::FakeNetEnabled() const
{
	return g_Config.m_SvNetFakeLoss > 0 ||
	       g_Config.m_SvNetFakeJitter > 0 ||
	       g_Config.m_SvNetFakeRtt > 0 ||
	       g_Config.m_SvNetFakeReorder > 0;
}

uint32_t CNetServer::FakeNetRandom()
{
	m_FakeNetSeed = m_FakeNetSeed * 1103515245u + 12345u;
	return m_FakeNetSeed;
}

bool CNetServer::FakeNetChance(int Percent)
{
	if(Percent <= 0)
		return false;
	if(Percent >= 100)
		return true;
	return (int)(FakeNetRandom() % 100) < Percent;
}

int CNetServer::FakeNetDelayMs()
{
	int DelayMs = std::max(0, g_Config.m_SvNetFakeRtt / 2);
	const int JitterMs = std::max(0, g_Config.m_SvNetFakeJitter);
	if(JitterMs > 0)
	{
		const int Spread = JitterMs * 2 + 1;
		DelayMs += (int)(FakeNetRandom() % Spread) - JitterMs;
	}
	return std::max(0, DelayMs);
}

bool CNetServer::FakeNetPopReadyPacket(NETADDR *pAddr, unsigned char *pData, int *pBytes)
{
	if(m_vFakeNetDelayedPackets.empty())
		return false;

	const int64_t Now = time_get();
	int Best = -1;
	int64_t BestTime = 0;
	for(size_t i = 0; i < m_vFakeNetDelayedPackets.size(); ++i)
	{
		const int64_t DeliverTime = m_vFakeNetDelayedPackets[i].m_DeliverTime;
		if(DeliverTime > Now)
			continue;
		if(Best == -1 || DeliverTime < BestTime)
		{
			Best = (int)i;
			BestTime = DeliverTime;
		}
	}

	if(Best < 0)
		return false;

	CFakeNetDelayedPacket Packet = m_vFakeNetDelayedPackets[Best];
	m_vFakeNetDelayedPackets.erase(m_vFakeNetDelayedPackets.begin() + Best);
	*pAddr = Packet.m_Addr;
	*pBytes = Packet.m_DataSize;
	mem_copy(pData, Packet.m_aData, Packet.m_DataSize);
	return true;
}

bool CNetServer::FakeNetQueuePacket(const NETADDR &Addr, const unsigned char *pData, int Bytes, int64_t DeliverTime)
{
	if((int)m_vFakeNetDelayedPackets.size() >= NET_FAKE_MAX_DELAYED_PACKETS)
	{
		++m_FakeNetDropped;
		return false;
	}

	CFakeNetDelayedPacket Packet;
	Packet.m_Addr = Addr;
	Packet.m_DataSize = Bytes;
	Packet.m_DeliverTime = DeliverTime;
	mem_copy(Packet.m_aData, pData, Bytes);
	m_vFakeNetDelayedPackets.push_back(Packet);
	++m_FakeNetDelayed;
	return true;
}

bool CNetServer::FakeNetFilterPacket(const NETADDR &Addr, const unsigned char *pData, int Bytes)
{
	if(!FakeNetEnabled())
		return false;

	if(FakeNetChance(g_Config.m_SvNetFakeLoss))
	{
		++m_FakeNetDropped;
		return true;
	}

	int DelayMs = FakeNetDelayMs();
	if(FakeNetChance(g_Config.m_SvNetFakeReorder))
	{
		DelayMs += std::max(1, g_Config.m_SvNetFakeJitter);
	}
	if(DelayMs <= 0)
		return false;

	const int64_t DeliverTime = time_get() + (int64_t)DelayMs * time_freq() / 1000;
	return FakeNetQueuePacket(Addr, pData, Bytes, DeliverTime);
}

int CNetServer::GetKcpClientSlot(uint32_t Conv, const NETADDR &Addr)
{
	for(int i = 0; i < MaxClients(); ++i)
	{
		CSlot &Slot = m_aSlots[i];
		if(Slot.m_Transport != ENetTransport::KCP || !Slot.m_Kcp.IsActive() || Slot.m_Kcp.Conv() != Conv)
			continue;
		if(Slot.m_Kcp.IsPeerAddress(Addr) || Slot.m_Kcp.IsPeerAddressNoPort(Addr))
			return i;
	}
	return -1;
}

int CNetServer::GetPendingKcpClientSlot(uint32_t Conv, const NETADDR &Addr)
{
	for(int i = 0; i < MaxClients(); ++i)
	{
		CSlot &Slot = m_aSlots[i];
		if(Slot.m_PendingKcpConv != Conv)
			continue;
		if(Slot.m_Connection.State() == CNetConnection::EState::OFFLINE || Slot.m_Connection.State() == CNetConnection::EState::ERROR)
			continue;
		if(net_addr_comp_noport(Slot.m_Connection.PeerAddress(), &Addr) == 0)
			return i;
	}
	return -1;
}

bool CNetServer::FeedKcpPacket(const NETADDR &Addr, const unsigned char *pData, int Bytes)
{
	uint32_t Conv;
	if(!CNetKcpSession::UnpackHeader(pData, Bytes, &Conv, nullptr, nullptr))
		return false;

	int ClientId = GetKcpClientSlot(Conv, Addr);
	if(ClientId < 0)
	{
		ClientId = GetPendingKcpClientSlot(Conv, Addr);
		if(ClientId >= 0 && !ActivateKcp(ClientId, Conv))
		{
			DeactivateKcp(ClientId);
			return false;
		}
	}
	if(ClientId < 0)
		return false;

	CSlot &Slot = m_aSlots[ClientId];
	if(!Slot.m_Kcp.Input(Addr, pData, Bytes, true))
		return false;

	if(Slot.m_Connection.UpdatePeerAddressForRebind(Addr))
	{
		Slot.m_Kcp.SetPeerAddress(Addr);
	}
	Slot.m_TransportStats = Slot.m_Kcp.Stats();
	return true;
}

bool CNetServer::FetchKcpChunk(CNetChunk *pChunk, SECURITY_TOKEN *pResponseToken)
{
	for(int ClientId = 0; ClientId < MaxClients(); ++ClientId)
	{
		CSlot &Slot = m_aSlots[ClientId];
		if(Slot.m_Transport != ENetTransport::KCP || !Slot.m_Kcp.IsActive())
			continue;

		const int Size = Slot.m_Kcp.PeekSize();
		if(Size <= 0 || Size > NET_MAX_PACKETSIZE)
			continue;

		unsigned char aBuffer[NET_MAX_PACKETSIZE];
		const int Bytes = Slot.m_Kcp.Recv(aBuffer, sizeof(aBuffer));
		if(Bytes <= 0)
			continue;

		bool Sixup = Slot.m_Connection.m_Sixup;
		SECURITY_TOKEN Token;
		*pResponseToken = NET_SECURITY_TOKEN_UNKNOWN;
		if(CNetBase::UnpackPacket(aBuffer, Bytes, &m_RecvBuffer, Sixup, true, &Token, pResponseToken) != 0)
			continue;

		NETADDR Addr = *Slot.m_Connection.PeerAddress();
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

		// 官方 7131ad28b：KCP 路径复用同一套控制消息分发，Slot 已知
		const bool Control = (m_RecvBuffer.m_Flags & NET_PACKETFLAG_CONTROL) != 0;
		if(Sixup && Control)
		{
			if(OnSixupCtrlMsg(Addr, pChunk, m_RecvBuffer.m_aChunkData[0], m_RecvBuffer, *pResponseToken, Token, ClientId) == 1)
				return true;
		}
		else if(IsDDNetControlMsg(&m_RecvBuffer) && Control)
		{
			OnTokenCtrlMsg(Addr, m_RecvBuffer.m_aChunkData[0], m_RecvBuffer, ClientId);
		}
		else
		{
			OnPreConnMsg(Addr, m_RecvBuffer, ClientId);
		}

		if(Slot.m_Connection.Feed(&m_RecvBuffer, &Addr, Token, *pResponseToken))
		{
			if(!Control &&
				m_RecvBuffer.m_DataSize > 0 &&
				m_RecvBuffer.m_NumChunks > 0)
			{
				m_PacketChunkUnpacker.FeedPacket(Addr, m_RecvBuffer, &Slot.m_Connection, ClientId);
				if(m_PacketChunkUnpacker.UnpackNextChunk(pChunk))
					return true;
			}
		}
	}
	return false;
}

/*
	TODO: chopp up this function into smaller working parts
*/
int CNetServer::Recv(CNetChunk *pChunk, SECURITY_TOKEN *pResponseToken)
{
	while(true)
	{
		// Unpack next chunk from stored packet if available
		if(m_PacketChunkUnpacker.UnpackNextChunk(pChunk))
		{
			if(m_aSlots[pChunk->m_ClientId].m_Connection.State() != CNetConnection::EState::OFFLINE)
				return 1;
			m_PacketChunkUnpacker.Reset();
		}
		if(FetchKcpChunk(pChunk, pResponseToken))
			return 1;

		// 达到接收批次上限后返回主循环，避免持续洪泛阻塞游戏 tick。
		if(g_Config.m_SvMaxPacketsPerRecv != 0 && m_NumRecvPackets >= g_Config.m_SvMaxPacketsPerRecv)
			break;

		// TODO: empty the recvinfo
		NETADDR Addr;
		unsigned char aFakeNetData[NET_MAX_PACKETSIZE];
		int FakeNetBytes = 0;
		unsigned char *pData;
		int Bytes;
		const bool FromFakeNet = FakeNetPopReadyPacket(&Addr, aFakeNetData, &FakeNetBytes);
		if(FromFakeNet)
		{
			pData = aFakeNetData;
			Bytes = FakeNetBytes;
		}
		else
		{
			Bytes = net_udp_recv(m_Socket, &Addr, &pData);
		}

		// no more packets for now
		if(Bytes <= 0)
			break;
		m_NumRecvPackets++;
		if(!FromFakeNet && FakeNetFilterPacket(Addr, pData, Bytes))
			continue;

		// check if we just should drop the packet
		char aBuf[128];
		if(NetBan() && NetBan()->IsBanned(&Addr, aBuf, sizeof(aBuf)))
		{
			if(g_Config.m_SvBanRepliesPerSecond == 0 || m_NumBanReplies < g_Config.m_SvBanRepliesPerSecond)
			{
				m_NumBanReplies++;
				CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, aBuf, str_length(aBuf) + 1, NET_SECURITY_TOKEN_UNSUPPORTED);
			}
			continue;
		}

		if(CNetKcpSession::IsKcpPacket(pData, Bytes))
		{
			FeedKcpPacket(Addr, pData, Bytes);
			continue;
		}

		// Check size and unpack packet flags early so we can determine the sixup
		// state correctly for connection-oriented packets before unpacking them.
		std::optional<int> Flags = CNetBase::UnpackPacketFlags(pData, Bytes);
		if(!Flags)
		{
			continue;
		}

		SECURITY_TOKEN Token;
		int Slot = (*Flags & NET_PACKETFLAG_CONNLESS) == 0 ? GetClientSlot(Addr) : -1;
		bool Sixup = Slot != -1 && m_aSlots[Slot].m_Connection.m_Sixup;
		bool AllowDecompression;
		if(Slot == -1)
		{
			AllowDecompression =
				g_Config.m_SvVanillaAntiSpoof &&
				g_Config.m_Password[0] == '\0' &&
				(g_Config.m_SvPreConnDecompressPerSecond == 0 ||
					m_NumPreConnDecompress < g_Config.m_SvPreConnDecompressPerSecond);
		}
		else if(Sixup)
		{
			AllowDecompression =
				Bytes >= NET_PACKETHEADERSIZE + (int)sizeof(SECURITY_TOKEN) &&
				ToSecurityToken(pData + NET_PACKETHEADERSIZE) == m_aSlots[Slot].m_Connection.m_Token;
		}
		else
		{
			AllowDecompression = true;
		}

		bool Decompressed = false;
		const int UnpackResult = CNetBase::UnpackPacket(pData, Bytes, &m_RecvBuffer, Sixup, AllowDecompression, &Token, pResponseToken, &Decompressed);
		if(Slot == -1 && Decompressed)
			m_NumPreConnDecompress++;
		if(UnpackResult == 0)
		{
			if(m_RecvBuffer.m_Flags & NET_PACKETFLAG_CONNLESS)
			{
				if(Sixup && Token != GetToken(Addr) && Token != GetGlobalToken())
				{
					continue;
				}

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
			else // connection-oriented packet
			{
				// 官方 7131ad28b：控制消息不再区分“已有连接/新连接”，
				// 统一按包类型处理，并把 Slot 传下去以支持重连
				const bool Control = (m_RecvBuffer.m_Flags & NET_PACKETFLAG_CONTROL) != 0;
				if(Sixup && Control)
				{
					// got 0.7 control msg
					if(OnSixupCtrlMsg(Addr, pChunk, m_RecvBuffer.m_aChunkData[0], m_RecvBuffer, *pResponseToken, Token, Slot) == 1)
						return 1;
				}
				else if(IsDDNetControlMsg(&m_RecvBuffer) && Control)
				{
					// got ddnet control msg
					OnTokenCtrlMsg(Addr, m_RecvBuffer.m_aChunkData[0], m_RecvBuffer, Slot);
				}
				else
				{
					// got connection-less ctrl or sys msg
					OnPreConnMsg(Addr, m_RecvBuffer, Slot);
				}

				if(Slot != -1 && m_aSlots[Slot].m_Connection.Feed(&m_RecvBuffer, &Addr, Token, *pResponseToken))
				{
					if(!Control &&
						m_RecvBuffer.m_DataSize > 0 &&
						m_RecvBuffer.m_NumChunks > 0)
					{
						m_PacketChunkUnpacker.FeedPacket(Addr, m_RecvBuffer, &m_aSlots[Slot].m_Connection, Slot);
					}
				}
			}
		}
	}
	return 0;
}

int CNetServer::Send(CNetChunk *pChunk)
{
	pChunk->AssertSizeSanity();

	if(pChunk->m_Flags & NETSENDFLAG_CONNLESS)
		return SendLegacy(pChunk);
	return SendClient(pChunk);
}

int CNetServer::SendClient(CNetChunk *pChunk)
{
	dbg_assert(
		pChunk->m_ClientId >= 0 && pChunk->m_ClientId < MaxClients(),
		"Invalid pChunk->m_ClientId: %d",
		pChunk->m_ClientId);

	switch(m_aSlots[pChunk->m_ClientId].m_Transport)
	{
	case ENetTransport::LEGACY:
		return SendLegacy(pChunk);
	case ENetTransport::KCP:
	{
		CSlot &Slot = m_aSlots[pChunk->m_ClientId];
		if(!g_Config.m_SvKcp || !Slot.m_Kcp.IsActive())
		{
			DeactivateKcp(pChunk->m_ClientId);
			return SendLegacy(pChunk);
		}
		if((pChunk->m_Flags & NETSENDFLAG_VITAL) == 0)
		{
			if((pChunk->m_Flags & NETSENDFLAG_FLUSH) == 0 || (!m_FlushBatch && !Slot.m_Connection.HasPendingPacketData()))
				return SendLegacyBypass(pChunk);
			if(pChunk->m_DataSize > NET_MAX_CHUNK_SIZE)
				return SendLegacyBypass(pChunk);
			if(Slot.m_Kcp.PendingSegments() >= NET_KCP_MAX_PENDING_SEGMENTS)
			{
				if(g_Config.m_SvKcpDebug)
					dbg_msg("netserver", "kcp queue full for client %d", pChunk->m_ClientId);
				return -1;
			}

			int Result = Slot.m_Connection.QueueChunk(0, pChunk->m_DataSize, pChunk->m_pData);
			if(Result == 0)
			{
				if(m_FlushBatch)
					m_aFlushPending[pChunk->m_ClientId] = true;
				else
				{
					Result = Slot.m_Connection.Flush();
					Slot.m_Kcp.Flush();
					if(Result >= 0)
						Result = 0;
				}
			}
			Slot.m_TransportStats = Slot.m_Kcp.Stats();
			return Result;
		}
		if(Slot.m_Kcp.PendingSegments() >= NET_KCP_MAX_PENDING_SEGMENTS)
		{
			if(g_Config.m_SvKcpDebug)
				dbg_msg("netserver", "kcp queue full for client %d", pChunk->m_ClientId);
			return -1;
		}

		int Flags = 0;
		if(pChunk->m_Flags & NETSENDFLAG_VITAL)
			Flags = NET_CHUNKFLAG_VITAL;
		int Result = Slot.m_Connection.QueueChunk(Flags, pChunk->m_DataSize, pChunk->m_pData);
		if(Result == 0 && (pChunk->m_Flags & NETSENDFLAG_FLUSH))
		{
			if(m_FlushBatch)
				m_aFlushPending[pChunk->m_ClientId] = true;
			else
			{
				Result = Slot.m_Connection.Flush();
				Slot.m_Kcp.Flush();
				if(Result >= 0)
					Result = 0;
			}
		}
		Slot.m_TransportStats = Slot.m_Kcp.Stats();
		return Result;
	}
	}
	return -1;
}

int CNetServer::SendLegacy(CNetChunk *pChunk)
{
	if(pChunk->m_Flags & NETSENDFLAG_CONNLESS)
	{
		// send connectionless packet
		CNetBase::SendPacketConnless(m_Socket, &pChunk->m_Address, pChunk->m_pData, pChunk->m_DataSize,
			pChunk->m_Flags & NETSENDFLAG_EXTENDED, pChunk->m_aExtraData);
	}
	else
	{
		int Flags = 0;
		dbg_assert(
			pChunk->m_ClientId >= 0 && pChunk->m_ClientId < MaxClients(),
			"Invalid pChunk->m_ClientId: %d",
			pChunk->m_ClientId);

		if(pChunk->m_Flags & NETSENDFLAG_VITAL)
			Flags = NET_CHUNKFLAG_VITAL;

		if(m_aSlots[pChunk->m_ClientId].m_Connection.QueueChunk(Flags, pChunk->m_DataSize, pChunk->m_pData) == 0)
		{
			if(pChunk->m_Flags & NETSENDFLAG_FLUSH)
			{
				if(m_FlushBatch)
					m_aFlushPending[pChunk->m_ClientId] = true;
				else
					m_aSlots[pChunk->m_ClientId].m_Connection.Flush();
			}
		}
	}
	return 0;
}

int CNetServer::SendLegacyBypass(CNetChunk *pChunk)
{
	if(pChunk->m_DataSize > NET_MAX_CHUNK_SIZE)
	{
		dbg_msg("netserver", "packet payload too big. %d. dropping packet", pChunk->m_DataSize);
		return -1;
	}

	dbg_assert(
		pChunk->m_ClientId >= 0 && pChunk->m_ClientId < MaxClients(),
		"Invalid pChunk->m_ClientId: %d",
		pChunk->m_ClientId);

	CNetPacketConstruct Construct;
	mem_zero(&Construct, sizeof(Construct));
	Construct.m_Ack = m_aSlots[pChunk->m_ClientId].m_Connection.AckSequence();
	Construct.m_NumChunks = 1;
	Construct.m_DataSize = 0;

	CNetChunkHeader Header;
	Header.m_Flags = 0;
	Header.m_Size = pChunk->m_DataSize;
	Header.m_Sequence = -1;
	unsigned char *pChunkData = Header.Pack(Construct.m_aChunkData, m_aSlots[pChunk->m_ClientId].m_Connection.m_Sixup ? 6 : 4);
	mem_copy(pChunkData, pChunk->m_pData, pChunk->m_DataSize);
	Construct.m_DataSize = (int)(pChunkData + pChunk->m_DataSize - Construct.m_aChunkData);

	CNetBase::SendPacket(m_Socket, const_cast<NETADDR *>(m_aSlots[pChunk->m_ClientId].m_Connection.PeerAddress()), &Construct, m_aSlots[pChunk->m_ClientId].m_Connection.SecurityToken(), m_aSlots[pChunk->m_ClientId].m_Connection.m_Sixup);
	return 0;
}

void CNetServer::SendTokenSixup(NETADDR &Addr, SECURITY_TOKEN Token)
{
	unsigned char aRequestTokenBuf[NET_TOKENREQUEST_DATASIZE] = {};
	WriteSecurityToken(aRequestTokenBuf, GetToken(Addr));
	const int Size = Token == NET_SECURITY_TOKEN_UNKNOWN ? sizeof(aRequestTokenBuf) : sizeof(SECURITY_TOKEN);
	CNetBase::SendControlMsg(m_Socket, &Addr, 0, protocol7::NET_CTRLMSG_TOKEN, aRequestTokenBuf, Size, Token, true);
}

void CNetServer::SetMaxClientsPerIp(int Max)
{
	m_MaxClientsPerIp = std::clamp<int>(Max, 1, NET_MAX_CLIENTS);
}

bool CNetServer::HasErrored(int ClientId)
{
	return m_aSlots[ClientId].m_Connection.State() == CNetConnection::EState::ERROR;
}

void CNetServer::ResumeOldConnection(int ClientId, int OrigId)
{
	m_aSlots[ClientId].m_Connection.ResumeConnection(ClientAddr(OrigId), m_aSlots[OrigId].m_Connection.SeqSequence(), m_aSlots[OrigId].m_Connection.AckSequence(), m_aSlots[OrigId].m_Connection.SecurityToken(), m_aSlots[OrigId].m_Connection.ResendBuffer(), m_aSlots[OrigId].m_Connection.m_Sixup);
	DeactivateKcp(ClientId);
	DeactivateKcp(OrigId);
	m_aSlots[OrigId].m_Connection.Reset();
}

void CNetServer::IgnoreTimeouts(int ClientId)
{
	m_aSlots[ClientId].m_Connection.m_TimeoutProtected = true;
}

void CNetServer::ResetErrorString(int ClientId)
{
	m_aSlots[ClientId].m_Connection.ResetErrorString();
}

const char *CNetServer::ErrorString(int ClientId)
{
	return m_aSlots[ClientId].m_Connection.ErrorString();
}
