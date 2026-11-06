#include <engine/client/sixup_connless.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>

namespace
{
	class CSixupConnlessTest : public testing::Test
	{
	protected:
		std::array<unsigned char, NET_MAX_CONNLESS_PAYLOAD + 16> m_aData;
		int m_Size = 0;
		CPacker m_Input;
		CUnpacker m_Output;

		void Build(std::array<int, 4> Counts = {1, 16, 1, 16}, int ActualClients = 1,
			int Country = 156, int Score = -123, int Flags = 0, const char *pName = "player", const char *pClan = "clan")
		{
			m_aData.fill(0xa5);
			m_Input.Reset();
			m_Input.AddInt(0x112);
			m_Input.AddString("  v0.7");
			m_Input.AddString("  new \t server  ");
			m_Input.AddString("host");
			m_Input.AddString("map");
			m_Input.AddString("DDNet");
			m_Input.AddInt(SERVER_FLAG_PASSWORD);
			m_Input.AddInt(2);
			for(int Count : Counts)
				m_Input.AddInt(Count);
			for(int i = 0; i < ActualClients; ++i)
			{
				m_Input.AddString(pName);
				m_Input.AddString(pClan);
				m_Input.AddInt(Country);
				m_Input.AddInt(Score);
				m_Input.AddInt(Flags);
			}
			ASSERT_FALSE(m_Input.Error());
			m_Size = SERVERBROWSE_SIZE + m_Input.Size();
			ASSERT_LE(m_Size, NET_MAX_CONNLESS_PAYLOAD);
			mem_copy(m_aData.data(), SERVERBROWSE_INFO, SERVERBROWSE_SIZE);
			mem_copy(m_aData.data() + SERVERBROWSE_SIZE, m_Input.Data(), m_Input.Size());
		}

		bool Convert(int Capacity = NET_MAX_CONNLESS_PAYLOAD)
		{
			return sixup_translation::PreprocessConnless(m_aData.data(), m_Size, Capacity);
		}

		void ReadHeader(bool Extended, std::array<int, 4> Counts)
		{
			EXPECT_EQ(mem_comp(m_aData.data(), Extended ? SERVERBROWSE_INFO_EXTENDED : SERVERBROWSE_INFO, SERVERBROWSE_SIZE), 0);
			m_Output.Reset(m_aData.data() + SERVERBROWSE_SIZE, m_Size - SERVERBROWSE_SIZE);
			EXPECT_STREQ(m_Output.GetString(), Extended ? "274" : "18");
			EXPECT_STREQ(m_Output.GetString(), "v0.7");
			EXPECT_STREQ(m_Output.GetString(), "new server");
			EXPECT_STREQ(m_Output.GetString(), "map");
			if(Extended)
			{
				EXPECT_STREQ(m_Output.GetString(), "0");
				EXPECT_STREQ(m_Output.GetString(), "0");
			}
			EXPECT_STREQ(m_Output.GetString(), "DDNet");
			EXPECT_EQ(str_toint(m_Output.GetString()), SERVER_FLAG_PASSWORD);
			for(int Count : Counts)
				EXPECT_EQ(str_toint(m_Output.GetString()), Count);
			if(Extended)
				EXPECT_STREQ(m_Output.GetString(), "");
			EXPECT_FALSE(m_Output.Error());
		}
	};
}

TEST_F(CSixupConnlessTest, VanillaResponsePreservesFieldsAndMasksToken)
{
	Build();
	ASSERT_TRUE(Convert());
	ReadHeader(false, {1, 16, 1, 16});
	EXPECT_STREQ(m_Output.GetString(), "player");
	EXPECT_STREQ(m_Output.GetString(), "clan");
	EXPECT_STREQ(m_Output.GetString(), "156");
	EXPECT_STREQ(m_Output.GetString(), "-123");
	EXPECT_STREQ(m_Output.GetString(), "1");
	EXPECT_FALSE(m_Output.Error());
	EXPECT_EQ(m_Output.GetRaw(1), nullptr);
	for(size_t i = NET_MAX_CONNLESS_PAYLOAD; i < m_aData.size(); ++i)
		EXPECT_EQ(m_aData[i], 0xa5);
}

TEST_F(CSixupConnlessTest, ExtendedResponseKeepsTokenAndAddsReservedFields)
{
	Build({0, 32, 1, 64}, 1, 999999, -123, 3);
	ASSERT_TRUE(Convert());
	ReadHeader(true, {0, 32, 1, 64});
	EXPECT_STREQ(m_Output.GetString(), "player");
	EXPECT_STREQ(m_Output.GetString(), "clan");
	EXPECT_EQ(str_toint(m_Output.GetString()), CountryCode::DEFAULT);
	EXPECT_STREQ(m_Output.GetString(), "-123");
	EXPECT_STREQ(m_Output.GetString(), "0");
	EXPECT_STREQ(m_Output.GetString(), "");
	EXPECT_FALSE(m_Output.Error());
}

TEST_F(CSixupConnlessTest, ShortSignaturePassesThroughWithoutReadingStaleTail)
{
	Build();
	const auto Original = m_aData;
	for(int Size = 0; Size < SERVERBROWSE_SIZE; ++Size)
	{
		m_Size = Size;
		ASSERT_TRUE(Convert());
		EXPECT_EQ(m_Size, Size);
		EXPECT_EQ(m_aData, Original);
	}
}

TEST_F(CSixupConnlessTest, OtherConnectionlessMessagePassesThrough)
{
	Build();
	m_aData[0] ^= 1;
	const auto Original = m_aData;
	const int Size = m_Size;
	EXPECT_TRUE(Convert());
	EXPECT_EQ(m_Size, Size);
	EXPECT_EQ(m_aData, Original);
}

TEST_F(CSixupConnlessTest, EveryTruncatedResponseIsRejectedWithoutPublishing)
{
	Build();
	const auto Original = m_aData;
	const int CompleteSize = m_Size;
	for(int Size = SERVERBROWSE_SIZE; Size < CompleteSize; ++Size)
	{
		SCOPED_TRACE(Size);
		m_Size = Size;
		ASSERT_FALSE(Convert());
		EXPECT_EQ(m_Size, Size);
		EXPECT_EQ(m_aData, Original);
	}
}

TEST_F(CSixupConnlessTest, InvalidCountRelationshipsAreRejected)
{
	const std::array<std::array<int, 4>, 6> Invalid = {{{-1, 16, 1, 16}, {1, -1, 1, 16}, {0, 16, -1, 16}, {1, 16, 1, -1}, {2, 16, 1, 16}, {1, 17, 1, 16}}};
	for(const auto &Counts : Invalid)
	{
		Build(Counts);
		const auto Original = m_aData;
		const int Size = m_Size;
		EXPECT_FALSE(Convert());
		EXPECT_EQ(m_aData, Original);
		EXPECT_EQ(m_Size, Size);
	}
}

TEST_F(CSixupConnlessTest, InvalidUtf8PlayerNameIsRejectedWithoutPublishing)
{
	Build({1, 16, 1, 16}, 1, 156, -123, 0, "\xc3\x28");
	const auto Original = m_aData;
	const int Size = m_Size;
	EXPECT_FALSE(Convert());
	EXPECT_EQ(m_aData, Original);
	EXPECT_EQ(m_Size, Size);
}

TEST_F(CSixupConnlessTest, PopulationAboveAdvertisedMaximumRetainsExistingPolicy)
{
	Build({1, 1, 2, 1}, 2);
	ASSERT_TRUE(Convert());
	ReadHeader(false, {1, 1, 2, 1});
}

TEST_F(CSixupConnlessTest, EmptyServerNeedsNoPlayerRecord)
{
	Build({0, 16, 0, 16}, 0);
	ASSERT_TRUE(Convert());
	ReadHeader(false, {0, 16, 0, 16});
	EXPECT_EQ(m_Output.GetRaw(1), nullptr);
}

TEST_F(CSixupConnlessTest, ExistingPlayerConversionCapPreservesAdvertisedCount)
{
	Build({0, 128, 129, 128}, 128, 0, 0, 0, "", "");
	ASSERT_TRUE(Convert());
	ReadHeader(true, {0, 128, 129, 128});
	for(int i = 0; i < 128; ++i)
	{
		EXPECT_STREQ(m_Output.GetString(), "");
		EXPECT_STREQ(m_Output.GetString(), "");
		EXPECT_STREQ(m_Output.GetString(), "0");
		EXPECT_STREQ(m_Output.GetString(), "0");
		EXPECT_STREQ(m_Output.GetString(), "1");
		EXPECT_STREQ(m_Output.GetString(), "");
	}
	EXPECT_FALSE(m_Output.Error());
	EXPECT_EQ(m_Output.GetRaw(1), nullptr);
}

TEST_F(CSixupConnlessTest, ExpandedResponseBeyondConnectionlessBudgetIsRejected)
{
	Build({0, 128, 100, 128}, 100, 0, std::numeric_limits<int>::min(), 0, "", "");
	const auto Original = m_aData;
	const int Size = m_Size;
	EXPECT_FALSE(Convert());
	EXPECT_EQ(m_aData, Original);
	EXPECT_EQ(m_Size, Size);
}

TEST_F(CSixupConnlessTest, InsufficientOutputCapacityPreservesInput)
{
	Build();
	const auto Original = m_aData;
	const int Size = m_Size;
	EXPECT_FALSE(Convert(Size));
	EXPECT_EQ(m_aData, Original);
	EXPECT_EQ(m_Size, Size);
}

TEST_F(CSixupConnlessTest, PackerOverflowIsRejectedWithoutPublishing)
{
	Build({0, 128, 128, 128}, 128, 0, std::numeric_limits<int>::min(), 0, "", "");
	const auto Original = m_aData;
	const int Size = m_Size;
	EXPECT_FALSE(Convert());
	EXPECT_EQ(m_aData, Original);
	EXPECT_EQ(m_Size, Size);
}

TEST_F(CSixupConnlessTest, ExactOutputCapacitySucceeds)
{
	Build();
	ASSERT_TRUE(Convert());
	const int OutputSize = m_Size;
	Build();
	ASSERT_TRUE(Convert(OutputSize));
	EXPECT_EQ(m_Size, OutputSize);
	ReadHeader(false, {1, 16, 1, 16});
}

TEST_F(CSixupConnlessTest, RetryAfterMalformedResponseCanConvertValidResponse)
{
	Build();
	--m_Size;
	EXPECT_FALSE(Convert());
	Build();
	ASSERT_TRUE(Convert());
	ReadHeader(false, {1, 16, 1, 16});
}
