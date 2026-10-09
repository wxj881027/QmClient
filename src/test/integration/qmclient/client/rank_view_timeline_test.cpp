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
	std::string TimelineData(const std::vector<std::vector<int32_t>> &vEvents = {}, const std::vector<std::vector<int32_t>> &vSwitches = {}, const std::vector<std::vector<int32_t>> &vMessages = {})
	{
		std::string Data("QMGHEVT2", 8);
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
}

TEST(RankViewTimeline, ReadsValidEventSwitchAndMessageSections)
{
	SViewTimeline Timeline;
	ASSERT_TRUE(ReadData(TimelineData({{3, NETEVENTTYPE_DEATH, 100, 200, MAX_CLIENTS - 1}}, {{4, 255, 1, 0, 0, 0, 0, 0, 0, 0}}, {{5, 0, MAX_CLIENTS - 1, 1000, 0, 0, 0}}), Timeline));
	ASSERT_EQ(Timeline.m_vEvents.size(), 1u);
	EXPECT_EQ(Timeline.m_vEvents[0].m_aData[2], MAX_CLIENTS - 1);
	ASSERT_EQ(Timeline.m_vSwitchStates.size(), 1u);
	EXPECT_EQ(Timeline.m_vSwitchStates[0].m_HighestSwitchNumber, 255);
	ASSERT_EQ(Timeline.m_vMessages.size(), 1u);
	EXPECT_EQ(Timeline.m_vMessages[0].m_Type, EViewMessageType::RACE_FINISH);
}

TEST(RankViewTimeline, RejectsHugeCountInShortFileAndClearsPreviouslyLoadedState)
{
	SViewTimeline Timeline;
	ASSERT_TRUE(ReadData(TimelineData({{1, NETEVENTTYPE_SPAWN, 0, 0, 0}}), Timeline));
	std::string Data("QMGHEVT2", 8);
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
	EXPECT_FALSE(ReadData(TimelineData({}, {{1, 256, 0, 0, 0, 0, 0, 0, 0, 0}}), Timeline));
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
