// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/storage.h>

#include <game/client/components/qmclient/local_save_display.h>
#include <game/client/components/qmclient/map_history_ui.h>
#include <game/client/components/qmclient/map_vote_difficulty.h>
#include <game/client/components/tclient/map_history.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <limits>

using namespace QmMapHistory;

TEST(MapHistoryUi, WorkspaceChromeScalesWithAvailableHeightWithinReadableBounds)
{
	const QmMapHistoryUi::SWorkspaceMetrics Compact = QmMapHistoryUi::WorkspaceMetrics(320.0f);
	const QmMapHistoryUi::SWorkspaceMetrics Standard = QmMapHistoryUi::WorkspaceMetrics(520.0f);
	const QmMapHistoryUi::SWorkspaceMetrics Tall = QmMapHistoryUi::WorkspaceMetrics(900.0f);

	EXPECT_LT(Compact.m_TabHeight, Standard.m_TabHeight);
	EXPECT_LT(Standard.m_TabHeight, Tall.m_TabHeight);
	EXPECT_LT(Compact.m_HeaderHeight, Standard.m_HeaderHeight);
	EXPECT_LT(Standard.m_HeaderHeight, Tall.m_HeaderHeight);
	EXPECT_GE(Compact.m_TabHeight, 22.0f);
	EXPECT_LE(Tall.m_TabHeight, 28.0f);
	EXPECT_GE(Compact.m_HeaderHeight, 26.0f);
	EXPECT_LE(Tall.m_HeaderHeight, 32.0f);
}

TEST(MapHistoryUi, CardHeightFillsHistoryPanelWithWholeRows)
{
	EXPECT_EQ(QmMapHistoryUi::VisibleCardRows(116.0f), 2);
	EXPECT_FLOAT_EQ(QmMapHistoryUi::CardRowHeight(116.0f), 58.0f);
	EXPECT_EQ(QmMapHistoryUi::VisibleCardRows(409.4f), 7);
	EXPECT_FLOAT_EQ(QmMapHistoryUi::CardRowHeight(409.4f), 409.4f / 7.0f);
	EXPECT_EQ(QmMapHistoryUi::VisibleCardRows(1001.0f), 14);
	EXPECT_FLOAT_EQ(QmMapHistoryUi::CardRowHeight(1001.0f), 1001.0f / 14.0f);
}

TEST(MapHistoryUi, ResponsiveGridPreservesCompactCardAspect)
{
	EXPECT_EQ(QmMapHistoryUi::GridColumns(224.0f, 60.0f), 1);
	EXPECT_EQ(QmMapHistoryUi::GridColumns(450.0f, 60.0f), 2);
	EXPECT_EQ(QmMapHistoryUi::GridColumns(675.0f, 60.0f), 3);
	EXPECT_EQ(QmMapHistoryUi::GridColumns(900.0f, 60.0f), 4);
	EXPECT_EQ(QmMapHistoryUi::GridColumns(900.0f, 72.0f), 3);
	EXPECT_EQ(QmMapHistoryUi::GridColumns(2000.0f, 60.0f), 8);
	EXPECT_EQ(QmMapHistoryUi::GridColumns(4050.0f, 72.0f), 15);
}

TEST(MapHistoryUi, NarrowControlsStackRelativeToControlHeight)
{
	EXPECT_TRUE(QmMapHistoryUi::StackControls(549.0f, 22.0f));
	EXPECT_FALSE(QmMapHistoryUi::StackControls(550.0f, 22.0f));
}

TEST(MapHistory, RepeatedVisitsAggregateByStableMapId)
{
	CMapHistory History;
	SMapHistoryRecord &First = History.RecordVisit("Kobra", "sha256:a", 100, "2026-07-01");
	First.m_DeathCount = 3;
	History.UpdatePlayTime("sha256:a", 12000);
	History.MarkFinished("sha256:a", 10000, 15000);

	SMapHistoryRecord &Second = History.RecordVisit("Kobra", "sha256:a", 200, "2026-07-02");
	EXPECT_EQ(History.Size(), 1u);
	EXPECT_EQ(&First, &Second);
	EXPECT_EQ(Second.m_LastEnteredAt, 200);
	EXPECT_EQ(Second.m_LastPlayedDate, "2026-07-02");
	EXPECT_EQ(Second.m_DeathCount, 3);
	EXPECT_FALSE(Second.m_Finished);
	EXPECT_EQ(Second.m_FinishTimeMs, 0);
}

TEST(MapHistory, LimitEvictsOldFinishedBeforeUnfinished)
{
	CMapHistory History;
	History.RecordVisit("Finished old", "finished-old", 10, "2026-07-01");
	History.MarkFinished("finished-old", 1000, 1000);
	History.RecordVisit("Unfinished old", "unfinished-old", 20, "2026-07-01");
	History.RecordVisit("Finished new", "finished-new", 30, "2026-07-01");
	History.MarkFinished("finished-new", 1000, 1000);

	History.ApplyLimit(2);
	EXPECT_EQ(History.Find("finished-old"), nullptr);
	EXPECT_NE(History.Find("unfinished-old"), nullptr);
	EXPECT_NE(History.Find("finished-new"), nullptr);

	History.ApplyLimit(1);
	EXPECT_EQ(History.Find("finished-new"), nullptr);
	EXPECT_NE(History.Find("unfinished-old"), nullptr);
}

TEST(MapHistory, DeathsAndFinishUpdateCurrentRecord)
{
	CMapHistory History;
	History.RecordVisit("A", "id-a", 100, "2026-07-01");
	EXPECT_TRUE(History.AddDeath("id-a"));
	EXPECT_TRUE(History.AddDeath("id-a", 2));
	EXPECT_TRUE(History.MarkFinished("id-a", 98765, 100000));

	const SMapHistoryRecord *pRecord = History.Find("id-a");
	ASSERT_NE(pRecord, nullptr);
	EXPECT_EQ(pRecord->m_DeathCount, 3);
	EXPECT_TRUE(pRecord->m_Finished);
	EXPECT_EQ(pRecord->m_FinishTimeMs, 98765);
	EXPECT_EQ(pRecord->m_PlayTimeMs, 100000);
}

TEST(MapHistory, DeathCountSaturatesInsteadOfOverflowing)
{
	CMapHistory History;
	History.RecordVisit("A", "id-a", 100, "2026-07-01");
	EXPECT_TRUE(History.AddDeath("id-a", std::numeric_limits<int>::max()));
	EXPECT_TRUE(History.AddDeath("id-a"));

	const SMapHistoryRecord *pRecord = History.Find("id-a");
	ASSERT_NE(pRecord, nullptr);
	EXPECT_EQ(pRecord->m_DeathCount, std::numeric_limits<int>::max());
}

TEST(MapHistory, JsonRoundTripPreservesRecords)
{
	CMapHistory History;
	History.RecordVisit("A", "id-a", 100, "2026-07-01");
	History.AddDeath("id-a", 4);
	History.UpdatePlayTime("id-a", 7000);
	History.RecordVisit("B", "id-b", 200, "2026-07-02");
	History.MarkFinished("id-b", 60000, 80000);

	const std::string Json = History.ToJson();
	CMapHistory Loaded;
	char aErr[128];
	ASSERT_TRUE(Loaded.FromJson(Json, aErr, sizeof(aErr))) << aErr;
	EXPECT_EQ(Loaded.Size(), 2u);
	const SMapHistoryRecord *pA = Loaded.Find("id-a");
	ASSERT_NE(pA, nullptr);
	EXPECT_FALSE(pA->m_Finished);
	EXPECT_EQ(pA->m_DeathCount, 4);
	EXPECT_EQ(pA->m_PlayTimeMs, 7000);
	const SMapHistoryRecord *pB = Loaded.Find("id-b");
	ASSERT_NE(pB, nullptr);
	EXPECT_TRUE(pB->m_Finished);
	EXPECT_EQ(pB->m_FinishTimeMs, 60000);
}

TEST(MapHistory, FromJsonToleratesMissingFieldsAndRejectsMalformed)
{
	const char *pJson = R"({
		"entries": [
			{"map_name":"Legacy","last_entered":123,"deaths":-5,"finished":false,"finish_time_ms":777},
			{"map_id":"missing-name","last_entered":456},
			{"map_name":"Huge","map_id":"huge","deaths":999999999999},
			{"map_name":"Done","map_id":"done","finished":true,"finish_time_ms":5000}
		]
	})";
	CMapHistory History;
	char aErr[128];
	ASSERT_TRUE(History.FromJson(pJson, aErr, sizeof(aErr))) << aErr;
	EXPECT_NE(History.Find("Legacy"), nullptr);
	EXPECT_EQ(History.Find("missing-name"), nullptr);
	const SMapHistoryRecord *pLegacy = History.Find("Legacy");
	ASSERT_NE(pLegacy, nullptr);
	EXPECT_EQ(pLegacy->m_DeathCount, 0);
	EXPECT_EQ(pLegacy->m_FinishTimeMs, 0);
	const SMapHistoryRecord *pHuge = History.Find("huge");
	ASSERT_NE(pHuge, nullptr);
	EXPECT_EQ(pHuge->m_DeathCount, std::numeric_limits<int>::max());

	CMapHistory Kept;
	Kept.RecordVisit("Keep", "keep", 1, "2026-07-01");
	EXPECT_FALSE(Kept.FromJson("{\"entries\":[", aErr, sizeof(aErr)));
	EXPECT_NE(Kept.Find("keep"), nullptr);
}

TEST(MapHistory, SortedFiltersUnfinishedAndRecent)
{
	CMapHistory History;
	History.RecordVisit("Finished", "finished", 300, "2026-07-03");
	History.MarkFinished("finished", 1000, 1000);
	History.RecordVisit("Unfinished old", "unfinished-old", 100, "2026-07-01");
	History.RecordVisit("Unfinished new", "unfinished-new", 200, "2026-07-02");

	const std::vector<SMapHistoryRecord> Unfinished = History.Sorted(EMapHistoryFilter::UNFINISHED);
	ASSERT_EQ(Unfinished.size(), 2u);
	EXPECT_EQ(Unfinished[0].m_MapId, "unfinished-new");
	EXPECT_EQ(Unfinished[1].m_MapId, "unfinished-old");

	const std::vector<SMapHistoryRecord> Recent = History.Sorted(EMapHistoryFilter::RECENT);
	ASSERT_EQ(Recent.size(), 3u);
	EXPECT_EQ(Recent[0].m_MapId, "finished");
	EXPECT_EQ(Recent[1].m_MapId, "unfinished-new");
	EXPECT_EQ(Recent[2].m_MapId, "unfinished-old");
}

TEST(MapHistory, SortedIndicesKeepFilterAndTieOrderWithoutMovingRecords)
{
	CMapHistory History;
	History.RecordVisit("Zulu", "z", 200, "2026-09-20");
	History.RecordVisit("Old", "old", 100, "2026-09-19");
	History.RecordVisit("Alpha", "a", 200, "2026-09-20");
	History.MarkFinished("z", 1000, 1000);
	std::vector<size_t> vIndices;

	History.SortedIndices(EMapHistoryFilter::RECENT, vIndices);
	EXPECT_EQ(vIndices, (std::vector<size_t>{2, 0, 1}));
	EXPECT_EQ(History.Entries()[0].m_MapId, "z");
	History.SortedIndices(EMapHistoryFilter::UNFINISHED, vIndices);
	EXPECT_EQ(vIndices, (std::vector<size_t>{2, 1}));
	History.SortedIndices(EMapHistoryFilter::FINISHED, vIndices);
	EXPECT_EQ(vIndices, (std::vector<size_t>{0}));
}

TEST(MapHistory, SortedIndicesReuseStorageAndReadCurrentCounters)
{
	CMapHistory History;
	History.RecordVisit(std::string(128, 'a'), "a", 100, "2026-09-20");
	History.RecordVisit(std::string(128, 'b'), "b", 200, "2026-09-20");
	std::vector<size_t> vIndices;
	History.SortedIndices(EMapHistoryFilter::RECENT, vIndices);
	const size_t Capacity = vIndices.capacity();
	const size_t *pStorage = vIndices.data();
	History.AddDeath("b", 3);
	History.UpdatePlayTime("b", 42000);
	History.SortedIndices(EMapHistoryFilter::RECENT, vIndices);
	ASSERT_EQ(vIndices.size(), 2u);
	EXPECT_EQ(vIndices.data(), pStorage);
	EXPECT_EQ(vIndices.capacity(), Capacity);
	EXPECT_EQ(History.Entries()[vIndices[0]].m_DeathCount, 3);
	EXPECT_EQ(History.Entries()[vIndices[0]].m_PlayTimeMs, 42000);
}

TEST(MapHistory, SortedIndicesRebuildAfterRemovalAndClear)
{
	CMapHistory History;
	History.RecordVisit("A", "a", 100, "2026-09-20");
	History.RecordVisit("B", "b", 200, "2026-09-20");
	std::vector<size_t> vIndices;
	History.SortedIndices(EMapHistoryFilter::RECENT, vIndices);
	ASSERT_TRUE(History.Remove("a"));
	History.SortedIndices(EMapHistoryFilter::RECENT, vIndices);
	ASSERT_EQ(vIndices.size(), 1u);
	EXPECT_EQ(History.Entries()[vIndices[0]].m_MapId, "b");
	History.Clear();
	History.SortedIndices(EMapHistoryFilter::RECENT, vIndices);
	EXPECT_TRUE(vIndices.empty());
}

TEST(LocalSaveDisplay, ParsingPreservesQuotedFieldsHeaderAndRawLine)
{
	const char *pText = "Time,Players,Map,Code\n2026-09-20,\"A, B\", Map ,\"say \"\"hi\"\"\"\r\n\n";
	const auto vEntries = qm_local_saves::ParseEntries(pText);
	ASSERT_EQ(vEntries.size(), 1u);
	EXPECT_EQ(vEntries[0].m_Time, "2026-09-20");
	EXPECT_EQ(vEntries[0].m_Players, "A, B");
	EXPECT_EQ(vEntries[0].m_Map, "Map");
	EXPECT_EQ(vEntries[0].m_Code, "say \"hi\"");
	EXPECT_EQ(vEntries[0].m_RawLine, "2026-09-20,\"A, B\", Map ,\"say \"\"hi\"\"\"");
	EXPECT_TRUE(qm_local_saves::ParseEntries("").empty());
	EXPECT_EQ(qm_local_saves::ParseEntries("date,player,map,code").size(), 1u);
}

TEST(LocalSaveDisplay, PublishesOnlyFinishedJobsAndKeepsSnapshotDuringRefresh)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	char aPath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, "saves.csv", aPath, sizeof(aPath));
	IOHANDLE File = io_open(aPath, IOFLAG_WRITE);
	ASSERT_NE(File, nullptr);
	const char *pText = "date,player,map,code\n";
	io_write(File, pText, str_length(pText));
	io_close(File);

	CQmLocalSaveDisplayCache Cache;
	auto pJob = Cache.Refresh(aPath, 100, 20);
	ASSERT_NE(pJob, nullptr);
	EXPECT_FALSE(Cache.Ready());
	EXPECT_EQ(Cache.Refresh(aPath, 101, 20), nullptr);
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	Pool.Shutdown();
	EXPECT_EQ(Cache.Refresh(aPath, 101, 20), nullptr);
	ASSERT_TRUE(Cache.Ready());
	ASSERT_TRUE(Cache.FileExists());
	ASSERT_EQ(Cache.Entries().size(), 1u);
	EXPECT_EQ(Cache.Entries()[0].m_Code, "code");

	ASSERT_EQ(fs_remove(aPath), 0);
	pJob = Cache.Refresh(aPath, 121, 20);
	ASSERT_NE(pJob, nullptr);
	EXPECT_TRUE(Cache.FileExists());
	EXPECT_EQ(Cache.Entries().size(), 1u);
	Pool.Init(1);
	Pool.Add(pJob);
	Pool.Shutdown();
	EXPECT_EQ(Cache.Refresh(aPath, 122, 20), nullptr);
	EXPECT_TRUE(Cache.Ready());
	EXPECT_FALSE(Cache.FileExists());
	EXPECT_TRUE(Cache.Entries().empty());
	Cache.Reset();
	EXPECT_FALSE(Cache.Ready());
}

TEST(LocalSaveDisplay, EmptyFileIsDistinctFromMissingAndJobOutlivesReset)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	char aPath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, "empty.csv", aPath, sizeof(aPath));
	IOHANDLE File = io_open(aPath, IOFLAG_WRITE);
	ASSERT_NE(File, nullptr);
	io_close(File);
	CQmLocalSaveDisplayCache Cache;
	auto pJob = Cache.Refresh(aPath, 100, 20);
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	Pool.Shutdown();
	EXPECT_EQ(Cache.Refresh(aPath, 101, 20), nullptr);
	EXPECT_TRUE(Cache.Ready());
	EXPECT_TRUE(Cache.FileExists());
	EXPECT_TRUE(Cache.Entries().empty());

	pJob = Cache.Refresh(aPath, 121, 20);
	ASSERT_NE(pJob, nullptr);
	Cache.Reset();
	Pool.Init(1);
	Pool.Add(pJob);
	Pool.Shutdown();
	EXPECT_EQ(pJob->State(), IJob::STATE_DONE);
	EXPECT_FALSE(Cache.Ready());
	EXPECT_TRUE(Cache.Entries().empty());
}

TEST(MapVoteDifficulty, KeepsFirstValidMatchAndCaseInsensitiveExactNames)
{
	CVoteOptionClient aOptions[5] = {};
	const char *apDescriptions[] = {
		"Alpha by author 9/5",
		"alpha by author 3/5",
		"ALPHA by other 1/5",
		"Alpha Two by author 5/5",
		"Beta by author 0/5",
	};
	for(int i = 0; i < 5; ++i)
	{
		str_copy(aOptions[i].m_aDescription, apDescriptions[i]);
		aOptions[i].m_pNext = i < 4 ? &aOptions[i + 1] : nullptr;
	}
	CQmMapVoteDifficulty Cache;
	EXPECT_EQ(Cache.Find(1, aOptions, "AlPhA"), 3);
	EXPECT_EQ(Cache.Find(1, aOptions, "Alpha Two"), 5);
	EXPECT_EQ(Cache.Find(1, aOptions, "beta"), 0);
	EXPECT_EQ(Cache.Find(1, aOptions, "Alph"), -1);
	EXPECT_EQ(Cache.Find(1, aOptions, ""), -1);
}

TEST(MapVoteDifficulty, RevisionRebuildsAfterSameCountReplacementAndClear)
{
	CVoteOptionClient Option = {};
	str_copy(Option.m_aDescription, "Map by author 2/5");
	CQmMapVoteDifficulty Cache;
	EXPECT_EQ(Cache.Find(4, &Option, "Map"), 2);
	str_copy(Option.m_aDescription, "Other by author 4/5");
	EXPECT_EQ(Cache.Find(4, &Option, "Map"), 2);
	EXPECT_EQ(Cache.Find(5, &Option, "Map"), -1);
	EXPECT_EQ(Cache.Find(5, &Option, "Other"), 4);
	EXPECT_EQ(Cache.Find(6, nullptr, "Other"), -1);
}

TEST(MapVoteDifficulty, PreservesStarParsingAndRejectsMalformedDescriptions)
{
	CQmMapVoteDifficulty Cache;
	CVoteOptionClient Option = {};
	str_copy(Option.m_aDescription, "Map BY author 00000005/5");
	EXPECT_EQ(Cache.Find(1, &Option, "Map"), 0);
	str_copy(Option.m_aDescription, "Map by author /5");
	EXPECT_EQ(Cache.Find(2, &Option, "Map"), -1);
	str_copy(Option.m_aDescription, "Map 3/5");
	EXPECT_EQ(Cache.Find(3, &Option, "Map"), -1);
	str_copy(Option.m_aDescription, "Map by author 6/5");
	EXPECT_EQ(Cache.Find(4, &Option, "Map"), -1);
	str_copy(Option.m_aDescription, "Map by author 2/5 then 4/5");
	EXPECT_EQ(Cache.Find(5, &Option, "Map"), 2);
}

TEST(MapVoteDifficulty, BrowserFilterSliderMapsSingleDiscreteLevels)
{
	EXPECT_EQ(QmMapVotes::MapBrowserFilterLevel(false, 0), QmMapVotes::MAP_BROWSER_FILTER_LEVEL_NONE);
	EXPECT_EQ(QmMapVotes::MapBrowserFilterLevel(true, 0), QmMapVotes::MAP_BROWSER_FILTER_LEVEL_EMPTY);
	for(int Star = 1; Star <= 5; ++Star)
	{
		const int Level = QmMapVotes::MAP_BROWSER_FILTER_LEVEL_FIRST_STAR + Star - 1;
		EXPECT_EQ(QmMapVotes::MapBrowserFilterLevel(false, 1 << Star), Level);
		EXPECT_EQ(QmMapVotes::MapBrowserFilterStars(Level), Star);
	}
	EXPECT_EQ(QmMapVotes::MapBrowserFilterLevel(true, 1 << 2), -1);
	EXPECT_EQ(QmMapVotes::MapBrowserFilterLevel(false, (1 << 1) | (1 << 3)), -1);
	EXPECT_EQ(QmMapVotes::MapBrowserFilterStars(QmMapVotes::MAP_BROWSER_FILTER_LEVEL_NONE), 0);
	EXPECT_EQ(QmMapVotes::MapBrowserFilterStars(QmMapVotes::MAP_BROWSER_FILTER_LEVEL_EMPTY), 0);

	// 每个档位写入配置后都应读回自身，滑条位置与实际筛选条件保持一致。
	for(int Level = QmMapVotes::MAP_BROWSER_FILTER_LEVEL_NONE; Level <= QmMapVotes::MAP_BROWSER_FILTER_LEVEL_LAST_STAR; ++Level)
	{
		int EmptyOnly = -1;
		int StarMask = -1;
		QmMapVotes::ApplyMapBrowserFilterLevel(Level, EmptyOnly, StarMask);
		EXPECT_EQ(QmMapVotes::MapBrowserFilterLevel(EmptyOnly != 0, StarMask), Level) << "level " << Level;
	}
}

TEST(MapVoteDifficulty, BrowserFilterFavoriteSwitchIsIndependent)
{
	EXPECT_TRUE(QmMapVotes::MatchesFilter(3, 4, false, 1 << 3, true, true));
	EXPECT_FALSE(QmMapVotes::MatchesFilter(3, 4, false, 1 << 3, true, false));
	EXPECT_TRUE(QmMapVotes::MatchesFilter(3, 4, false, 1 << 3, false, false));
	EXPECT_FALSE(QmMapVotes::MatchesFilter(2, 4, false, 1 << 3, true, true));
}

TEST(MapVoteDifficulty, BrowserFilterKeepsEmptyServersUnlessEmptyOnlyIsOn)
{
	// 「不筛选」档必须保留空服务器：引擎侧只在 empty_only 打开时隐藏有人的服务器，
	// 客户端列表层的二次过滤不能把默认档的空服一并滤掉。
	EXPECT_TRUE(QmMapVotes::MatchesFilter(3, 0, false, 0, false, false));
	EXPECT_TRUE(QmMapVotes::MatchesFilter(-1, 0, false, 0, false, false));
	// 打开「只看空服」后：空服保留，有人的服务器隐藏。
	EXPECT_TRUE(QmMapVotes::MatchesFilter(3, 0, true, 0, false, false));
	EXPECT_FALSE(QmMapVotes::MatchesFilter(3, 1, true, 0, false, false));
	// 星级与空服条件叠加时两条都必须满足。
	EXPECT_TRUE(QmMapVotes::MatchesFilter(3, 0, true, 1 << 3, false, false));
	EXPECT_FALSE(QmMapVotes::MatchesFilter(2, 0, true, 1 << 3, false, false));
}
