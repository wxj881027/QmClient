#include <engine/shared/network.h>

#include <gtest/gtest.h>

static int UnpackUncompressedPacket(int Size, CNetPacketConstruct *pPacket)
{
	unsigned char aBuffer[NET_MAX_PACKETSIZE] = {};
	aBuffer[0] = 0; // no flags, ack 0
	aBuffer[1] = 0; // ack 0
	aBuffer[2] = 1; // one chunk
	bool Sixup = false;
	return CNetBase::UnpackPacket(aBuffer, Size, pPacket, Sixup);
}

TEST(Network, UnpackMaximumUncompressedPacket)
{
	CNetPacketConstruct Packet;
	EXPECT_EQ(UnpackUncompressedPacket(NET_PACKETHEADERSIZE + (int)sizeof(Packet.m_aChunkData), &Packet), 0);
	EXPECT_EQ(Packet.m_DataSize, (int)sizeof(Packet.m_aChunkData));
	EXPECT_EQ(UnpackUncompressedPacket(NET_MAX_PACKETSIZE, &Packet), 0);
}

TEST(Network, UnpackOversizedUncompressedPacket)
{
	CNetPacketConstruct Packet;
	EXPECT_EQ(UnpackUncompressedPacket(NET_PACKETHEADERSIZE + (int)sizeof(Packet.m_aChunkData) + 1, &Packet), -1);
}

TEST(Network, UnpackChunks)
{
	constexpr int NumChunks = 3;
	const int aSizes[NumChunks] = {1, 100, 7};

	CNetPacketConstruct Packet = {};
	Packet.m_NumChunks = NumChunks;
	unsigned char *pData = Packet.m_aChunkData;
	for(int i = 0; i < NumChunks; i++)
	{
		CNetChunkHeader Header;
		Header.m_Flags = 0;
		Header.m_Size = aSizes[i];
		Header.m_Sequence = 0;
		pData = Header.Pack(pData);
		memset(pData, i, aSizes[i]);
		pData += aSizes[i];
	}
	Packet.m_DataSize = (int)(pData - Packet.m_aChunkData);

	CNetConnection Connection;
	Connection.m_Sixup = false;

	CPacketChunkUnpacker Unpacker;
	Unpacker.FeedPacket(NETADDR{}, Packet, &Connection, 0);
	for(int i = 0; i < NumChunks; i++)
	{
		CNetChunk Chunk;
		ASSERT_TRUE(Unpacker.UnpackNextChunk(&Chunk));
		EXPECT_EQ(Chunk.m_DataSize, aSizes[i]);
		for(int j = 0; j < Chunk.m_DataSize; j++)
			EXPECT_EQ(((const unsigned char *)Chunk.m_pData)[j], i);
	}
	CNetChunk Chunk;
	EXPECT_FALSE(Unpacker.UnpackNextChunk(&Chunk));
}

TEST(Network, UnpackChunksRejectsTruncatedHeadersAndPayloads)
{
	CNetConnection Connection;
	Connection.m_Sixup = false;

	const auto CheckInvalid = [&Connection](const unsigned char *pData, int DataSize, int NumChunks) {
		CNetPacketConstruct Packet = {};
		Packet.m_NumChunks = NumChunks;
		Packet.m_DataSize = DataSize;
		mem_copy(Packet.m_aChunkData, pData, DataSize);

		CPacketChunkUnpacker Unpacker;
		Unpacker.FeedPacket(NETADDR{}, Packet, &Connection, 0);
		CNetChunk Chunk;
		EXPECT_FALSE(Unpacker.UnpackNextChunk(&Chunk));
		EXPECT_FALSE(Unpacker.UnpackNextChunk(&Chunk));
	};

	// A non-vital header needs two bytes.
	const unsigned char aTruncatedHeader[] = {0};
	CheckInvalid(aTruncatedHeader, sizeof(aTruncatedHeader), 1);

	// A vital header needs its third sequence byte as well.
	const unsigned char aTruncatedVitalHeader[] = {NET_CHUNKFLAG_VITAL << 6, 0};
	CheckInvalid(aTruncatedVitalHeader, sizeof(aTruncatedVitalHeader), 1);

	// The header is complete, but its payload extends past the packet.
	CNetChunkHeader Header;
	Header.m_Flags = 0;
	Header.m_Size = 4;
	Header.m_Sequence = 0;
	unsigned char aOversizedPayload[NET_MAX_CHUNKHEADERSIZE] = {};
	unsigned char *pEnd = Header.Pack(aOversizedPayload);
	CheckInvalid(aOversizedPayload, (int)(pEnd - aOversizedPayload), 1);
}
