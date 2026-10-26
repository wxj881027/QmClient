#include <engine/shared/packer.h>

#include <generated/protocol7.h>

#include <game/client/components/qmclient/vote_option_packet.h>

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	// 计数器只约束读取次数，解包和错误状态均交给真实CUnpacker。
	// 回归到错误后continue时立即报错，避免让测试空转INT_MAX次。
	class CVoteReadBudget
	{
		CUnpacker &m_Unpacker;
		int m_MaxReads;

	public:
		int m_Reads = 0;
		CVoteReadBudget(CUnpacker &Unpacker, int MaxReads) :
			m_Unpacker(Unpacker), m_MaxReads(MaxReads) {}
		const char *GetString(int Flags)
		{
			if(m_Reads >= m_MaxReads)
				throw std::runtime_error("vote packet exceeded its read budget");
			++m_Reads;
			return m_Unpacker.GetString(Flags);
		}
		bool Error() const { return m_Unpacker.Error(); }
	};
}

TEST(VoteOptionPacket, SixupSecuredHugeCountWithNoDescriptionsStopsAtFirstReadError)
{
	CPacker Packet;
	Packet.AddInt(std::numeric_limits<int>::max());
	ASSERT_FALSE(Packet.Error());
	CUnpacker Unpacker;
	Unpacker.Reset(Packet.Data(), Packet.Size());
	protocol7::CNetObjHandler Handler;
	ASSERT_NE(Handler.SecureUnpackMsg(protocol7::NETMSGTYPE_SV_VOTEOPTIONLISTADD, &Unpacker), nullptr);
	const int NumOptions = Unpacker.GetInt();
	ASSERT_EQ(NumOptions, std::numeric_limits<int>::max());
	ASSERT_FALSE(Unpacker.Error());
	CVoteReadBudget Reader(Unpacker, 1);
	int Consumed = 0;
	int Parsed = -1;
	ASSERT_NO_THROW(Parsed = ReadQmVoteOptions(Reader, NumOptions, [&](int, const char *) { ++Consumed; }));
	EXPECT_EQ(Parsed, 0);
	EXPECT_EQ(Consumed, 0);
	EXPECT_EQ(Reader.m_Reads, 1);
	EXPECT_TRUE(Unpacker.Error());
}

TEST(VoteOptionPacket, HugeDeclaredCountPreservesValidPrefixAndStopsWhenPacketEnds)
{
	CPacker Packet;
	Packet.AddInt(std::numeric_limits<int>::max());
	Packet.AddString("first");
	Packet.AddString("second");
	ASSERT_FALSE(Packet.Error());
	CUnpacker Unpacker;
	Unpacker.Reset(Packet.Data(), Packet.Size());
	const int NumOptions = Unpacker.GetInt();
	CVoteReadBudget Reader(Unpacker, 3);
	std::vector<std::string> vOptions;
	int Parsed = -1;
	ASSERT_NO_THROW(Parsed = ReadQmVoteOptions(Reader, NumOptions, [&](int Index, const char *pText) {
		EXPECT_EQ(Index, static_cast<int>(vOptions.size()));
		vOptions.emplace_back(pText);
	}));
	EXPECT_EQ(Parsed, 2);
	EXPECT_EQ(vOptions, (std::vector<std::string>{"first", "second"}));
	EXPECT_EQ(Reader.m_Reads, 3);
	EXPECT_TRUE(Unpacker.Error());
}

TEST(VoteOptionPacket, UnterminatedDescriptionDoesNotPublishPartialTextOrReadAgain)
{
	CPacker Packet;
	Packet.AddString("first");
	Packet.AddRaw("broken", 6);
	ASSERT_FALSE(Packet.Error());
	CUnpacker Unpacker;
	Unpacker.Reset(Packet.Data(), Packet.Size());
	CVoteReadBudget Reader(Unpacker, 2);
	std::vector<std::string> vOptions;
	int Parsed = -1;
	ASSERT_NO_THROW(Parsed = ReadQmVoteOptions(Reader, 1000000, [&](int, const char *pText) { vOptions.emplace_back(pText); }));
	EXPECT_EQ(Parsed, 1);
	EXPECT_EQ(vOptions, (std::vector<std::string>{"first"}));
	EXPECT_EQ(Reader.m_Reads, 2);
	EXPECT_TRUE(Unpacker.Error());
}

TEST(VoteOptionPacket, InvalidUtf8StopsAfterTheLastValidDescription)
{
	CPacker Packet;
	Packet.AddString("first");
	const unsigned char aInvalid[] = {0xff, 0};
	Packet.AddRaw(aInvalid, sizeof(aInvalid));
	ASSERT_FALSE(Packet.Error());
	CUnpacker Unpacker;
	Unpacker.Reset(Packet.Data(), Packet.Size());
	CVoteReadBudget Reader(Unpacker, 2);
	std::vector<std::string> vOptions;
	int Parsed = -1;
	ASSERT_NO_THROW(Parsed = ReadQmVoteOptions(Reader, std::numeric_limits<int>::max(), [&](int, const char *pText) { vOptions.emplace_back(pText); }));
	EXPECT_EQ(Parsed, 1);
	EXPECT_EQ(vOptions, (std::vector<std::string>{"first"}));
	EXPECT_TRUE(Unpacker.Error());
}

TEST(VoteOptionPacket, LongValidListPreservesEveryOptionInOrder)
{
	CPacker Packet;
	std::vector<std::string> vExpected;
	for(int i = 0; i < 40; ++i)
	{
		vExpected.push_back("option " + std::to_string(i));
		Packet.AddString(vExpected.back().c_str());
	}
	ASSERT_FALSE(Packet.Error());
	CUnpacker Unpacker;
	Unpacker.Reset(Packet.Data(), Packet.Size());
	std::vector<std::string> vOptions;
	EXPECT_EQ(ReadQmVoteOptions(Unpacker, 40, [&](int Index, const char *pText) {
		EXPECT_EQ(Index, static_cast<int>(vOptions.size()));
		vOptions.emplace_back(pText);
	}),
		40);
	EXPECT_EQ(vOptions, vExpected);
	EXPECT_FALSE(Unpacker.Error());
}

TEST(VoteOptionPacket, DeclaredShortListLeavesTrailingDataUnread)
{
	CPacker Packet;
	Packet.AddString("one");
	Packet.AddString("two");
	Packet.AddString("trailing");
	CUnpacker Unpacker;
	Unpacker.Reset(Packet.Data(), Packet.Size());
	std::vector<std::string> vOptions;
	EXPECT_EQ(ReadQmVoteOptions(Unpacker, 2, [&](int, const char *pText) { vOptions.emplace_back(pText); }), 2);
	EXPECT_EQ(vOptions, (std::vector<std::string>{"one", "two"}));
	EXPECT_STREQ(Unpacker.GetString(), "trailing");
	EXPECT_FALSE(Unpacker.Error());
}

TEST(VoteOptionPacket, NonpositiveCountsDoNotReadOrPublishDescriptions)
{
	for(const int Count : {0, -1, std::numeric_limits<int>::min()})
	{
		SCOPED_TRACE(Count);
		CPacker Packet;
		Packet.AddInt(Count);
		Packet.AddString("untouched");
		CUnpacker Unpacker;
		Unpacker.Reset(Packet.Data(), Packet.Size());
		const int Declared = Unpacker.GetInt();
		CVoteReadBudget Reader(Unpacker, 0);
		int Consumed = 0;
		int Parsed = -1;
		ASSERT_NO_THROW(Parsed = ReadQmVoteOptions(Reader, Declared, [&](int, const char *) { ++Consumed; }));
		EXPECT_EQ(Parsed, 0);
		EXPECT_EQ(Consumed, 0);
		EXPECT_EQ(Reader.m_Reads, 0);
		EXPECT_STREQ(Unpacker.GetString(), "untouched");
	}
}

TEST(VoteOptionPacket, EmptyDescriptionOccupiesItsOriginalSlot)
{
	CPacker Packet;
	Packet.AddString("");
	Packet.AddString("next");
	CUnpacker Unpacker;
	Unpacker.Reset(Packet.Data(), Packet.Size());
	std::vector<std::string> vOptions;
	EXPECT_EQ(ReadQmVoteOptions(Unpacker, 2, [&](int, const char *pText) { vOptions.emplace_back(pText); }), 2);
	EXPECT_EQ(vOptions, (std::vector<std::string>{"", "next"}));
	EXPECT_FALSE(Unpacker.Error());
}
