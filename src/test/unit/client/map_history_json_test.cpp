#include <game/client/components/tclient/map_history.h>

#include <gtest/gtest.h>

#include <limits>
#include <string>

using namespace QmMapHistory;

namespace
{
	std::string NumericRecord(const char *pNumber)
	{
		return std::string(R"({"entries":[{"map_name":"A","map_id":"a","finished":true,"last_entered":)") + pNumber +
		       R"(,"deaths":)" + pNumber + R"(,"finish_time_ms":)" + pNumber + R"(,"play_time_ms":)" + pNumber + "}]}";
	}
}

TEST(MapHistoryJson, FiniteFractionsKeepTruncationTowardZero)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(R"({"entries":[{"map_name":"A","map_id":"a","last_entered":123.75,"deaths":17.9,"finished":true,"finish_time_ms":456.5,"play_time_ms":789.9}]})"));
	const auto *pRecord = History.Find("a");
	ASSERT_NE(pRecord, nullptr);
	EXPECT_EQ(pRecord->m_LastEnteredAt, 123);
	EXPECT_EQ(pRecord->m_DeathCount, 17);
	EXPECT_EQ(pRecord->m_FinishTimeMs, 456);
	EXPECT_EQ(pRecord->m_PlayTimeMs, 789);
}

TEST(MapHistoryJson, MaximumIntegerTokenKeepsItsExactValueAndDeathCountClamp)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(NumericRecord("9223372036854775807")));
	const auto *pRecord = History.Find("a");
	ASSERT_NE(pRecord, nullptr);
	EXPECT_EQ(pRecord->m_LastEnteredAt, std::numeric_limits<int64_t>::max());
	EXPECT_EQ(pRecord->m_FinishTimeMs, std::numeric_limits<int64_t>::max());
	EXPECT_EQ(pRecord->m_PlayTimeMs, std::numeric_limits<int64_t>::max());
	EXPECT_EQ(pRecord->m_DeathCount, std::numeric_limits<int>::max());
}

TEST(MapHistoryJson, LargestDoubleBelowTheUpperBoundaryRemainsRepresentable)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(NumericRecord("9223372036854774784.0")));
	const auto *pRecord = History.Find("a");
	ASSERT_NE(pRecord, nullptr);
	EXPECT_EQ(pRecord->m_LastEnteredAt, INT64_C(9223372036854774784));
	EXPECT_EQ(pRecord->m_FinishTimeMs, INT64_C(9223372036854774784));
	EXPECT_EQ(pRecord->m_PlayTimeMs, INT64_C(9223372036854774784));
	EXPECT_EQ(pRecord->m_DeathCount, std::numeric_limits<int>::max());
}

TEST(MapHistoryJson, IntegerMaximumRoundedToDoubleUsesTheExistingNumericDefault)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(NumericRecord("9223372036854775807.0")));
	const auto *pRecord = History.Find("a");
	ASSERT_NE(pRecord, nullptr);
	EXPECT_EQ(pRecord->m_LastEnteredAt, 0);
	EXPECT_EQ(pRecord->m_DeathCount, 0);
	EXPECT_EQ(pRecord->m_FinishTimeMs, 0);
	EXPECT_EQ(pRecord->m_PlayTimeMs, 0);
}

TEST(MapHistoryJson, FiniteDoublesOutsideTheIntegerDomainKeepTheRecordWithDefaultNumbers)
{
	for(const char *pNumber : {"1e100", "-1e100", "9223372036854775808.0", "-9223372036854777856.0"})
	{
		SCOPED_TRACE(pNumber);
		CMapHistory History;
		ASSERT_TRUE(History.FromJson(NumericRecord(pNumber)));
		const auto *pRecord = History.Find("a");
		ASSERT_NE(pRecord, nullptr);
		EXPECT_EQ(pRecord->m_LastEnteredAt, 0);
		EXPECT_EQ(pRecord->m_DeathCount, 0);
		EXPECT_EQ(pRecord->m_FinishTimeMs, 0);
		EXPECT_EQ(pRecord->m_PlayTimeMs, 0);
		EXPECT_TRUE(pRecord->m_Finished);
	}
}

TEST(MapHistoryJson, NonfiniteExponentResultsKeepTheRecordWithDefaultNumbers)
{
	for(const char *pNumber : {"1e400", "-1e400", "0.0e400"})
	{
		SCOPED_TRACE(pNumber);
		CMapHistory History;
		ASSERT_TRUE(History.FromJson(NumericRecord(pNumber)));
		const auto *pRecord = History.Find("a");
		ASSERT_NE(pRecord, nullptr);
		EXPECT_EQ(pRecord->m_LastEnteredAt, 0);
		EXPECT_EQ(pRecord->m_DeathCount, 0);
		EXPECT_EQ(pRecord->m_FinishTimeMs, 0);
		EXPECT_EQ(pRecord->m_PlayTimeMs, 0);
	}
}

TEST(MapHistoryJson, RepresentableNegativeNumbersKeepTheOriginalNonnegativeFieldPolicy)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(NumericRecord("-123.75")));
	const auto *pRecord = History.Find("a");
	ASSERT_NE(pRecord, nullptr);
	EXPECT_EQ(pRecord->m_LastEnteredAt, 0);
	EXPECT_EQ(pRecord->m_DeathCount, 0);
	EXPECT_EQ(pRecord->m_FinishTimeMs, 0);
	EXPECT_EQ(pRecord->m_PlayTimeMs, 0);
}

TEST(MapHistoryJson, SuccessfulEmptyLoadStillReplacesThePreviousRecords)
{
	for(const char *pJson : {"{}", R"({"entries":[]})"})
	{
		SCOPED_TRACE(pJson);
		CMapHistory History;
		History.RecordVisit("Keep", "keep", 1, "2026-11-01");
		ASSERT_TRUE(History.FromJson(pJson));
		EXPECT_EQ(History.Size(), 0u);
		EXPECT_EQ(History.Find("keep"), nullptr);
	}
}

TEST(MapHistoryJson, InvalidEntriesTypeKeepsExistingRecordsAndTheirBorrowedAddresses)
{
	CMapHistory History;
	History.RecordVisit("Keep", "keep", 1, "2026-11-01");
	const auto *pBefore = History.Find("keep");
	const std::string Before = History.ToJson();
	char aError[128]{};
	EXPECT_FALSE(History.FromJson(R"({"entries":{}})", aError, sizeof(aError)));
	EXPECT_NE(aError[0], '\0');
	EXPECT_EQ(History.Find("keep"), pBefore);
	EXPECT_EQ(History.ToJson(), Before);
}

TEST(MapHistoryJson, DuplicateRecordsKeepNewestMetadataAndMaximumDeaths)
{
	CMapHistory History;
	ASSERT_TRUE(History.FromJson(R"({"entries":[
		{"map_name":"First","map_id":"a","last_entered":100,"deaths":10,"finished":true,"finish_time_ms":99},
		{"map_name":"Latest","map_id":"a","last_entered":200,"deaths":2,"finished":false,"play_time_ms":33},
		{"map_name":"Older","map_id":"a","last_entered":50,"deaths":30,"finished":true}
	]})"));
	ASSERT_EQ(History.Size(), 1u);
	const auto *pRecord = History.Find("a");
	ASSERT_NE(pRecord, nullptr);
	EXPECT_EQ(pRecord->m_MapName, "Latest");
	EXPECT_EQ(pRecord->m_LastEnteredAt, 200);
	EXPECT_EQ(pRecord->m_DeathCount, 30);
	EXPECT_FALSE(pRecord->m_Finished);
	EXPECT_EQ(pRecord->m_FinishTimeMs, 0);
	EXPECT_EQ(pRecord->m_PlayTimeMs, 33);
}

TEST(MapHistoryJson, SuccessfulCandidateReplacesTheOldCollectionInsteadOfAppending)
{
	CMapHistory History;
	History.RecordVisit("Keep", "keep", 1, "2026-11-01");
	ASSERT_TRUE(History.FromJson(R"({"entries":[{"map_name":"New","map_id":"new"}]})"));
	EXPECT_EQ(History.Size(), 1u);
	EXPECT_EQ(History.Find("keep"), nullptr);
	ASSERT_NE(History.Find("new"), nullptr);
	EXPECT_EQ(History.Find("new")->m_MapName, "New");
}
