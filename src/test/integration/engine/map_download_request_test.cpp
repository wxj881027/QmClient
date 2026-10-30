#include <engine/client/map_download_request.h>

#include <gtest/gtest.h>

#include <limits>
#include <vector>

namespace
{
	class CMapDownloadRequestTest : public testing::Test
	{
	protected:
		struct SRequest
		{
			int m_Id;
			bool m_System;
			bool m_NoTranslate;
			std::vector<unsigned char> m_vData;
		};
		std::vector<SRequest> m_vRequests;

		auto Sender()
		{
			return [this](CMsgPacker &Msg) {
				m_vRequests.push_back({Msg.m_MsgId, Msg.m_System, Msg.m_NoTranslate,
					std::vector<unsigned char>(Msg.Data(), Msg.Data() + Msg.Size())});
			};
		}

		void ExpectSixupRequest(size_t Index)
		{
			ASSERT_LT(Index, m_vRequests.size());
			const auto &Request = m_vRequests[Index];
			EXPECT_EQ(Request.m_Id, protocol7::NETMSG_REQUEST_MAP_DATA);
			EXPECT_TRUE(Request.m_System);
			EXPECT_TRUE(Request.m_NoTranslate);
			EXPECT_TRUE(Request.m_vData.empty());
		}

		void ExpectLegacyRequest(size_t Index, int Chunk)
		{
			ASSERT_LT(Index, m_vRequests.size());
			const auto &Request = m_vRequests[Index];
			EXPECT_EQ(Request.m_Id, NETMSG_REQUEST_MAP_DATA);
			EXPECT_TRUE(Request.m_System);
			EXPECT_FALSE(Request.m_NoTranslate);
			ASSERT_FALSE(Request.m_vData.empty());
			CUnpacker Unpacker;
			Unpacker.Reset(Request.m_vData.data(), static_cast<int>(Request.m_vData.size()));
			EXPECT_EQ(Unpacker.GetInt(), Chunk);
			EXPECT_FALSE(Unpacker.Error());
		}
	};
}

TEST_F(CMapDownloadRequestTest, InitialLegacyRequestContainsChunkNumber)
{
	map_download::SendRequest(false, 0, Sender());
	ASSERT_EQ(m_vRequests.size(), 1U);
	ExpectLegacyRequest(0, 0);
}

TEST_F(CMapDownloadRequestTest, InitialSixupRequestBypassesLegacyTranslationWithoutChunkPayload)
{
	map_download::SendRequest(true, 0, Sender());
	ASSERT_EQ(m_vRequests.size(), 1U);
	ExpectSixupRequest(0);
}

TEST_F(CMapDownloadRequestTest, SixupRequestsOnlyAtCompletedWindowBoundaries)
{
	map_download::SendRequest(true, 0, Sender());
	for(int Chunk = 1; Chunk <= 8; ++Chunk)
	{
		SCOPED_TRACE(Chunk);
		const bool Requested = map_download::ContinueRequest(true, Chunk, 4, Sender());
		EXPECT_EQ(Requested, Chunk == 4 || Chunk == 8);
	}
	ASSERT_EQ(m_vRequests.size(), 3U);
	for(size_t Index = 0; Index < m_vRequests.size(); ++Index)
		ExpectSixupRequest(Index);
}

TEST_F(CMapDownloadRequestTest, SingleChunkSixupWindowRequestsEveryFollowingChunk)
{
	for(int Chunk = 1; Chunk <= 3; ++Chunk)
	{
		SCOPED_TRACE(Chunk);
		EXPECT_TRUE(map_download::ContinueRequest(true, Chunk, 1, Sender()));
	}
	ASSERT_EQ(m_vRequests.size(), 3U);
	for(size_t Index = 0; Index < m_vRequests.size(); ++Index)
		ExpectSixupRequest(Index);
}

TEST_F(CMapDownloadRequestTest, InvalidSixupWindowDoesNotRequestOrDivideByZero)
{
	for(int Window : {0, -1, std::numeric_limits<int>::min()})
	{
		SCOPED_TRACE(Window);
		EXPECT_FALSE(map_download::ContinueRequest(true, 1, Window, Sender()));
	}
	EXPECT_TRUE(m_vRequests.empty());
}

TEST_F(CMapDownloadRequestTest, NegativeChunkDoesNotSendARequest)
{
	for(bool Sixup : {false, true})
	{
		SCOPED_TRACE(Sixup);
		EXPECT_FALSE(map_download::ContinueRequest(Sixup, -4, 4, Sender()));
		EXPECT_FALSE(map_download::ContinueRequest(Sixup, std::numeric_limits<int>::min(), 1, Sender()));
	}
	EXPECT_TRUE(m_vRequests.empty());
}

TEST_F(CMapDownloadRequestTest, LegacyRequestsEveryChunkIndependentOfWindowSize)
{
	int Chunk = 1;
	for(int Window : {0, 4, -1})
	{
		SCOPED_TRACE(Window);
		ASSERT_TRUE(map_download::ContinueRequest(false, Chunk, Window, Sender()));
		ExpectLegacyRequest(m_vRequests.size() - 1, Chunk);
		++Chunk;
	}
	EXPECT_EQ(m_vRequests.size(), 3U);
}

TEST_F(CMapDownloadRequestTest, LargeChunkBoundaryDoesNotOverflow)
{
	constexpr int Max = std::numeric_limits<int>::max();
	EXPECT_TRUE(map_download::ContinueRequest(true, Max, Max, Sender()));
	EXPECT_FALSE(map_download::ContinueRequest(true, Max, 2, Sender()));
	ASSERT_EQ(m_vRequests.size(), 1U);
	ExpectSixupRequest(0);
}
