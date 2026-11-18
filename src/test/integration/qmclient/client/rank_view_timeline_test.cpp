#include <engine/storage.h>

#include <game/client/components/qmclient/rank_view_timeline.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <initializer_list>
#include <string>
#include <vector>

using namespace qmclient::rank_ghost;

namespace
{
	void AppendInt(std::string &Data, int32_t Value)
	{
		Data.append(reinterpret_cast<const char *>(&Value), sizeof(Value));
	}
	// 独立构造现有磁盘协议：magic、事件段、开关段、消息段。
	std::string TimelineData(const std::vector<std::vector<int32_t>> &vEvents = {}, const std::vector<std::vector<int32_t>> &vSwitches = {}, const std::vector<std::vector<int32_t>> &vMessages = {}, const char *pMagic = "QMGHEVT3")
	{
		std::string Data(pMagic, 8);
		for(const auto *pSection : {&vEvents, &vSwitches, &vMessages})
		{
			AppendInt(Data, (int32_t)pSection->size());
			for(const auto &vRecord : *pSection)
				for(int32_t Value : vRecord)
					AppendInt(Data, Value);
		}
		return Data;
	}
	bool ReadData(const std::string &Data, SViewTimeline &Timeline)
	{
		CTestInfo Info;
		auto pStorage = Info.CreateTestStorage();
		if(!pStorage)
			return false;
		IOHANDLE File = pStorage->OpenFile("timeline.events", IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		const bool Written = io_write(File, Data.data(), (unsigned)Data.size()) == Data.size();
		io_close(File);
		if(!Written)
			return false;
		File = pStorage->OpenFile("timeline.events", IOFLAG_READ, IStorage::TYPE_SAVE);
		const bool Success = ReadViewTimeline(File, Timeline);
		if(File)
			io_close(File);
		return Success;
	}

	// 走生产写出函数落盘，再用生产读取函数读回：验证写读两侧的字节布局一致。
	bool WriteThenRead(const SViewTimeline &Timeline, SViewTimeline &Out)
	{
		CTestInfo Info;
		auto pStorage = Info.CreateTestStorage();
		if(!pStorage)
			return false;
		IOHANDLE File = pStorage->OpenFile("timeline.events", IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		const bool Written = WriteViewTimeline(File, Timeline);
		io_close(File);
		if(!Written)
			return false;
		File = pStorage->OpenFile("timeline.events", IOFLAG_READ, IStorage::TYPE_SAVE);
		const bool Success = ReadViewTimeline(File, Out);
		if(File)
			io_close(File);
		return Success;
	}
}

TEST(RankViewTimeline, ReadsValidEventSwitchAndMessageSections)
{
	SViewTimeline Timeline;
	ASSERT_TRUE(ReadData(TimelineData({{3, NETEVENTTYPE_DEATH, 100, 200, MAX_CLIENTS - 1}}, {{4, 255, 3, 1, 0, 0, 0, 0, 0, 0, 0}}, {{5, 0, MAX_CLIENTS - 1, 1000, 0, 0, 0}}), Timeline));
	ASSERT_EQ(Timeline.m_vEvents.size(), 1u);
	EXPECT_EQ(Timeline.m_vEvents[0].m_aData[2], MAX_CLIENTS - 1);
	ASSERT_EQ(Timeline.m_vSwitchStates.size(), 1u);
	EXPECT_EQ(Timeline.m_vSwitchStates[0].m_HighestSwitchNumber, 255);
	EXPECT_EQ(Timeline.m_vSwitchStates[0].m_Team, 3);
	ASSERT_EQ(Timeline.m_vMessages.size(), 1u);
	EXPECT_EQ(Timeline.m_vMessages[0].m_Type, EViewMessageType::RACE_FINISH);
}

// 旧 QMGHEVT2 的开关段没有队伍字段。练习组没有源 demo 可重建，
// 读取必须继续成功，并把队伍交给回放阶段回落到查看者当前队伍。
TEST(RankViewTimeline, ReadsLegacyV2SwitchSectionWithoutTeam)
{
	SViewTimeline Timeline;
	ASSERT_TRUE(ReadData(TimelineData({}, {{4, 255, 1, 0, 0, 0, 0, 0, 0, 0}}, {}, "QMGHEVT2"), Timeline));
	ASSERT_EQ(Timeline.m_vSwitchStates.size(), 1u);
	EXPECT_EQ(Timeline.m_vSwitchStates[0].m_Team, -1);
	EXPECT_EQ(Timeline.m_vSwitchStates[0].m_aStatus[0], 1u);
}

TEST(RankViewTimeline, RejectsHugeCountInShortFileAndClearsPreviouslyLoadedState)
{
	SViewTimeline Timeline;
	ASSERT_TRUE(ReadData(TimelineData({{1, NETEVENTTYPE_SPAWN, 0, 0, 0}}), Timeline));
	std::string Data("QMGHEVT3", 8);
	AppendInt(Data, -1);
	EXPECT_FALSE(ReadData(Data, Timeline));
	EXPECT_TRUE(Timeline.m_vEvents.empty());
	EXPECT_TRUE(Timeline.m_vSwitchStates.empty());
	EXPECT_TRUE(Timeline.m_vMessages.empty());
	EXPECT_TRUE(ReadData(TimelineData(), Timeline));
}

TEST(RankViewTimeline, RejectsDeathClientIdOutsideClientArray)
{
	SViewTimeline Timeline;
	EXPECT_FALSE(ReadData(TimelineData({{1, NETEVENTTYPE_DEATH, 0, 0, -1}}), Timeline));
	EXPECT_FALSE(ReadData(TimelineData({{1, NETEVENTTYPE_DEATH, 0, 0, MAX_CLIENTS}}), Timeline));
	EXPECT_TRUE(Timeline.m_vEvents.empty());
}

TEST(RankViewTimeline, RejectsInvalidSwitchNumber)
{
	SViewTimeline Timeline;
	EXPECT_FALSE(ReadData(TimelineData({}, {{1, 256, 0, 0, 0, 0, 0, 0, 0, 0, 0}}), Timeline));
}

TEST(RankViewTimeline, RejectsUnknownMessageType)
{
	SViewTimeline Timeline;
	EXPECT_FALSE(ReadData(TimelineData({}, {}, {{1, 99, 0, 0, 0, 0, 0}}), Timeline));
}

TEST(RankViewTimeline, RejectsRaceFinishClientOutsideClientArray)
{
	SViewTimeline Timeline;
	EXPECT_FALSE(ReadData(TimelineData({}, {}, {{1, 0, MAX_CLIENTS, 0, 0, 0, 0}}), Timeline));
}

TEST(RankViewTimeline, RejectsUnorderedTicks)
{
	SViewTimeline Timeline;
	EXPECT_FALSE(ReadData(TimelineData({{2, NETEVENTTYPE_SPAWN, 0, 0, 0}, {1, NETEVENTTYPE_SPAWN, 0, 0, 0}}), Timeline));
}

TEST(RankViewTimeline, RejectsTruncatedRecord)
{
	SViewTimeline Timeline;
	std::string Data = TimelineData({{1, NETEVENTTYPE_SPAWN, 0, 0, 0}});
	Data.pop_back();
	EXPECT_FALSE(ReadData(Data, Timeline));
}

TEST(RankViewTimeline, RejectsTrailingData)
{
	SViewTimeline Timeline;
	EXPECT_FALSE(ReadData(TimelineData() + "x", Timeline));
}

TEST(RankViewTimeline, WriteAndReadRoundTripsAllSections)
{
	SViewTimeline Source;
	SViewEvent Event = {};
	Event.m_RelTick = 12;
	Event.m_Type = NETEVENTTYPE_DEATH;
	Event.m_aData[0] = 100;
	Event.m_aData[1] = 200;
	Event.m_aData[2] = MAX_CLIENTS - 1;
	Source.m_vEvents.push_back(Event);

	SViewSwitchState Switch = {};
	Switch.m_RelTick = 20;
	Switch.m_HighestSwitchNumber = 2;
	Switch.m_Team = 7;
	Switch.m_aStatus[0] = 0x5;
	Source.m_vSwitchStates.push_back(Switch);

	SViewMessage Message = {};
	Message.m_RelTick = 30;
	Message.m_Type = EViewMessageType::RACE_TIME;
	Message.m_aData[0] = 1234;
	Source.m_vMessages.push_back(Message);

	SViewTimeline Read;
	ASSERT_TRUE(WriteThenRead(Source, Read));
	ASSERT_EQ(Read.m_vEvents.size(), 1u);
	EXPECT_EQ(Read.m_vEvents[0].m_RelTick, 12);
	EXPECT_EQ(Read.m_vEvents[0].m_Type, NETEVENTTYPE_DEATH);
	EXPECT_EQ(Read.m_vEvents[0].m_aData[2], MAX_CLIENTS - 1);
	ASSERT_EQ(Read.m_vSwitchStates.size(), 1u);
	EXPECT_EQ(Read.m_vSwitchStates[0].m_RelTick, 20);
	EXPECT_EQ(Read.m_vSwitchStates[0].m_Team, 7);
	EXPECT_EQ(Read.m_vSwitchStates[0].m_aStatus[0], 0x5u);
	ASSERT_EQ(Read.m_vMessages.size(), 1u);
	EXPECT_EQ(Read.m_vMessages[0].m_Type, EViewMessageType::RACE_TIME);
	EXPECT_EQ(Read.m_vMessages[0].m_aData[0], 1234);
}

TEST(RankViewTimeline, PracticeStyleTimelineWithEmptyMessageSectionRoundTrips)
{
	// 练习 /pg 写出的文件没有消息段；读取器必须接受 0 条并保留事件。
	SViewTimeline Source;
	SViewEvent Event = {};
	Event.m_RelTick = 0;
	Event.m_Type = NETEVENTTYPE_EXPLOSION;
	Source.m_vEvents.push_back(Event);

	SViewTimeline Read;
	ASSERT_TRUE(WriteThenRead(Source, Read));
	ASSERT_EQ(Read.m_vEvents.size(), 1u);
	EXPECT_EQ(Read.m_vEvents[0].m_Type, NETEVENTTYPE_EXPLOSION);
	EXPECT_TRUE(Read.m_vSwitchStates.empty());
	EXPECT_TRUE(Read.m_vMessages.empty());
}
