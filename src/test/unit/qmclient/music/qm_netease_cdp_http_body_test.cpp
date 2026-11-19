#include <gtest/gtest.h>
#include <qm/hooks/netease/qm_netease_cdp_io.h>

#include <cstring>
#include <new>
#include <vector>

using namespace QmNeteaseCdp;

namespace
{
	struct SBodyChunk
	{
		EChunkStatus m_Status;
		std::string m_Bytes;
	};
	class CBodyScript
	{
		size_t m_Next = 0;

	public:
		bool m_Allowed = true;
		bool m_CancelOnRead = false;
		int m_Reads = 0;
		std::vector<SBodyChunk> m_vChunks;
		SIoChunk Read(void *pData, size_t Capacity)
		{
			++m_Reads;
			if(m_CancelOnRead)
				m_Allowed = false;
			if(m_Next >= m_vChunks.size())
				return {EChunkStatus::END, 0};
			const SBodyChunk &Chunk = m_vChunks[m_Next++];
			std::memcpy(pData, Chunk.m_Bytes.data(), std::min(Capacity, Chunk.m_Bytes.size()));
			return {Chunk.m_Status, Chunk.m_Bytes.size()};
		}
		bool Collect(std::string &Output, size_t Limit = 64)
		{
			return ReadCdpHttpBody(Output, Limit, [this] { return m_Allowed; }, [this](void *pData, size_t Capacity) { return Read(pData, Capacity); });
		}
	};
}

TEST(QmNeteaseCdpHttpBody, CompleteChunksPublishOnlyAfterEnd)
{
	CBodyScript Reader;
	Reader.m_vChunks = {{EChunkStatus::DATA, "["}, {EChunkStatus::DATA, "{}]"}};
	std::string Output = "old";
	EXPECT_TRUE(Reader.Collect(Output));
	EXPECT_EQ(Output, "[{}]");
	EXPECT_EQ(Reader.m_Reads, 3);
}

TEST(QmNeteaseCdpHttpBody, EmptyResponsePreservesPreviousOutput)
{
	CBodyScript Reader;
	std::string Output = "old";
	EXPECT_FALSE(Reader.Collect(Output));
	EXPECT_EQ(Output, "old");
}

TEST(QmNeteaseCdpHttpBody, ReadErrorAfterValidPrefixDoesNotPublishIt)
{
	CBodyScript Reader;
	Reader.m_vChunks = {{EChunkStatus::DATA, "[{}]"}, {EChunkStatus::FAILED, ""}};
	std::string Output = "old";
	EXPECT_FALSE(Reader.Collect(Output));
	EXPECT_EQ(Output, "old");
}

TEST(QmNeteaseCdpHttpBody, BodyAtExactLimitCanFinish)
{
	CBodyScript Reader;
	Reader.m_vChunks = {{EChunkStatus::DATA, "12"}, {EChunkStatus::DATA, "34"}};
	std::string Output;
	EXPECT_TRUE(Reader.Collect(Output, 4));
	EXPECT_EQ(Output, "1234");
}

TEST(QmNeteaseCdpHttpBody, ExtraByteBeyondLimitPreservesPreviousOutput)
{
	CBodyScript Reader;
	Reader.m_vChunks = {{EChunkStatus::DATA, "1234"}, {EChunkStatus::DATA, "5"}};
	std::string Output = "old";
	EXPECT_FALSE(Reader.Collect(Output, 4));
	EXPECT_EQ(Output, "old");
	EXPECT_EQ(Reader.m_Reads, 2);
}

TEST(QmNeteaseCdpHttpBody, CancellationBeforeReadDoesNotTouchTransport)
{
	CBodyScript Reader;
	Reader.m_Allowed = false;
	std::string Output = "old";
	EXPECT_FALSE(Reader.Collect(Output));
	EXPECT_EQ(Reader.m_Reads, 0);
	EXPECT_EQ(Output, "old");
}

TEST(QmNeteaseCdpHttpBody, CancellationDuringReadRejectsNewBytes)
{
	CBodyScript Reader;
	Reader.m_CancelOnRead = true;
	Reader.m_vChunks = {{EChunkStatus::DATA, "[]"}};
	std::string Output = "old";
	EXPECT_FALSE(Reader.Collect(Output));
	EXPECT_EQ(Output, "old");
}

TEST(QmNeteaseCdpHttpBody, CancellationAtEndDoesNotPublishCandidate)
{
	std::string Output = "old";
	bool Allowed = true;
	int Reads = 0;
	EXPECT_FALSE(ReadCdpHttpBody(Output, 64, [&] { return Allowed; }, [&](void *pData, size_t) -> SIoChunk {
		if(++Reads == 1)
		{
			std::memcpy(pData, "[]", 2);
			return {EChunkStatus::DATA, 2};
		}
		Allowed = false;
		return {EChunkStatus::END, 0}; }));
	EXPECT_EQ(Output, "old");
}

TEST(QmNeteaseCdpHttpBody, InvalidChunkResultsCannotPublishPrefix)
{
	const SBodyChunk aChunks[] = {{EChunkStatus::DATA, ""}, {EChunkStatus::RETRY, ""}, {EChunkStatus::END, "x"}, {EChunkStatus::DATA, std::string(8193, 'x')}};
	for(const SBodyChunk &Chunk : aChunks)
	{
		SCOPED_TRACE(static_cast<int>(Chunk.m_Status));
		CBodyScript Reader;
		Reader.m_vChunks = {{EChunkStatus::DATA, "[]"}, Chunk};
		std::string Output = "old";
		EXPECT_FALSE(Reader.Collect(Output, 16384));
		EXPECT_EQ(Output, "old");
	}
}

TEST(QmNeteaseCdpHttpBody, ReaderExceptionKeepsPreviousOutput)
{
	std::string Output = "old";
	int Reads = 0;
	EXPECT_THROW(ReadCdpHttpBody(Output, 64, [] { return true; }, [&](void *pData, size_t) -> SIoChunk {
		if(++Reads == 1)
		{
			std::memcpy(pData, "[]", 2);
			return {EChunkStatus::DATA, 2};
		}
		throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_EQ(Output, "old");
}
