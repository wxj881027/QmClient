#include <base/str.h>

#include <game/client/components/qmclient/vote_map_library.h>

#include <gtest/gtest.h>

#include <limits>

using namespace QmVoteMaps;

TEST(VoteMapLibrary, CatalogKeepsOfficialMetadataAndZeroStarMaps)
{
	const char *pJson = R"([{"name":"Zero","type":"Race","mapper":"作者","difficulty":0,"points":0,"release":"2026-10-01 12:00"},{"name":"Alpha","type":"Novice","difficulty":2,"points":3}])";
	std::vector<SMap> vMaps;
	ASSERT_TRUE(ParseCatalog(pJson, str_length(pJson), vMaps));
	ASSERT_EQ(vMaps.size(), 2u);
	EXPECT_EQ(vMaps[0].m_Name, "Alpha");
	EXPECT_EQ(vMaps[1].m_Stars, 0);
	EXPECT_EQ(vMaps[1].m_Points, 0);
	EXPECT_EQ(vMaps[1].m_Mapper, "作者");
	EXPECT_EQ(vMaps[1].m_Release, "2026-10-01 12:00");
}

TEST(VoteMapLibrary, MalformedRefreshPreservesLastValidCatalog)
{
	std::vector<SMap> vMaps = {{"Previous"}};
	for(const char *pJson : {"broken", "{}", "[]", R"([{"name":"Bad","type":"Race","difficulty":8}])", R"([{"name":"A\u0000B","type":"Race","difficulty":1}])"})
	{
		SCOPED_TRACE(pJson);
		EXPECT_FALSE(ParseCatalog(pJson, str_length(pJson), vMaps));
		ASSERT_EQ(vMaps.size(), 1u);
		EXPECT_EQ(vMaps[0].m_Name, "Previous");
	}
	EXPECT_FALSE(ParseCatalog(nullptr, 0, vMaps));
	EXPECT_FALSE(ParseCatalog("[]", 9 * 1024 * 1024, vMaps));
}

TEST(VoteMapLibrary, InvalidEntriesDoNotHideValidOrNewCategories)
{
	const char *pJson = R"([null,{"name":"Broken","type":"Race","difficulty":"2"},{"name":"Future","type":"New category","difficulty":1},{"name":"future","type":"Race","difficulty":4}])";
	std::vector<SMap> vMaps;
	ASSERT_TRUE(ParseCatalog(pJson, str_length(pJson), vMaps));
	ASSERT_EQ(vMaps.size(), 1u);
	EXPECT_EQ(vMaps[0].m_Name, "Future");
	EXPECT_EQ(vMaps[0].m_Category, "New category");
}

TEST(VoteMapLibrary, CatalogDoesNotTruncateLongMapNamesToVoteDescriptionSize)
{
	const std::string Name(70, 'a');
	const std::string Json = "[{\"name\":\"" + Name + "\",\"type\":\"Race\",\"difficulty\":0}]";
	std::vector<SMap> vMaps;
	ASSERT_TRUE(ParseCatalog(Json.c_str(), Json.size(), vMaps));
	ASSERT_EQ(vMaps.size(), 1u);
	EXPECT_EQ(vMaps[0].m_Name, Name);
}

TEST(VoteMapLibrary, DetailsRejectOtherMapAndKeepMissingStatisticsUnknown)
{
	const SMap Map{"Alpha", "Author", "Novice"};
	SDetails Details{40, 90};
	const char *pWrong = R"({"name":"Beta","type":"Novice","finishers":2,"average_time":10})";
	EXPECT_FALSE(ParseDetails(pWrong, str_length(pWrong), Map, Details));
	EXPECT_EQ(Details.m_Finishers, 40);
	const char *pWrongCategory = R"({"name":"Alpha","type":"Brutal"})";
	EXPECT_FALSE(ParseDetails(pWrongCategory, str_length(pWrongCategory), Map, Details));
	const char *pMissing = R"({"name":"Alpha","type":"Novice","finishers":-1,"average_time":null})";
	ASSERT_TRUE(ParseDetails(pMissing, str_length(pMissing), Map, Details));
	EXPECT_EQ(Details.m_Finishers, -1);
	EXPECT_EQ(Details.m_AverageSeconds, -1);
}

TEST(VoteMapLibrary, DetailsReadFinishersAndAverageSeconds)
{
	const char *pJson = R"({"name":"Alpha","type":"Novice","finishers":123,"average_time":65.5})";
	SDetails Details;
	ASSERT_TRUE(ParseDetails(pJson, str_length(pJson), {"Alpha", "", "Novice"}, Details));
	EXPECT_EQ(Details.m_Finishers, 123);
	EXPECT_DOUBLE_EQ(Details.m_AverageSeconds, 65.5);
}

TEST(VoteMapLibrary, ResolvesExactMapOptionsAcrossSupportedDescriptionFormats)
{
	CVoteOptionClient aOptions[4]{};
	const char *apDescriptions[] = {"Change settings", "Alpha Two by author | 2/5 ★", "Map: Alpha | details", "Beta by author | ★★✰✰✰"};
	for(int i = 0; i < 4; ++i)
	{
		str_copy(aOptions[i].m_aDescription, apDescriptions[i]);
		aOptions[i].m_pNext = i < 3 ? &aOptions[i + 1] : nullptr;
	}
	EXPECT_EQ(FindOption(aOptions, "alpha"), 2);
	EXPECT_EQ(FindOption(aOptions, "Alpha Two"), 1);
	EXPECT_EQ(FindOption(aOptions, "Beta"), 3);
	EXPECT_EQ(FindOption(aOptions, "Alph"), -1);
	EXPECT_EQ(FindOption(aOptions, ""), -1);
	SMap Parsed;
	ASSERT_TRUE(ParseVoteMap(aOptions[3].m_aDescription, Parsed));
	EXPECT_EQ(Parsed.m_Stars, 2);
	EXPECT_EQ(Parsed.m_Mapper, "author");
}

TEST(VoteMapLibrary, ResolvesAgainstReplacementListInsteadOfRetainingOldIndex)
{
	CVoteOptionClient Option{};
	str_copy(Option.m_aDescription, "Map: Alpha");
	EXPECT_EQ(FindOption(&Option, "Alpha"), 0);
	str_copy(Option.m_aDescription, "Map: Other");
	EXPECT_EQ(FindOption(&Option, "Alpha"), -1);
	EXPECT_EQ(FindOption(nullptr, "Alpha"), -1);
}

TEST(VoteMapLibrary, RecognizesEnglishAndChineseServerVoteResults)
{
	for(const char *pText : {"Vote passed", "Vote passed enforced by authorized player", "投票通过", "授权玩家强制通过投票"})
		EXPECT_EQ(ServerVoteResult(-1, 0, pText), EVoteResult::PASS);
	for(const char *pText : {"Vote failed", "Vote failed because of veto. Find an empty server instead", "投票失败", "授权玩家强制否决投票"})
		EXPECT_EQ(ServerVoteResult(-1, 0, pText), EVoteResult::FAIL);
	for(const char *pText : {"Vote aborted", "Vote canceled", "投票已中止", "'Player' 取消了投票"})
		EXPECT_EQ(ServerVoteResult(-1, 0, pText), EVoteResult::ABORT);
}

TEST(VoteMapLibrary, PlayerChatOrPartialMessagesCannotCompleteVoteChain)
{
	EXPECT_EQ(ServerVoteResult(4, 0, "Vote passed"), EVoteResult::NONE);
	EXPECT_EQ(ServerVoteResult(-1, 1, "Vote passed"), EVoteResult::NONE);
	EXPECT_EQ(ServerVoteResult(-1, 0, "Vote passed?"), EVoteResult::NONE);
	EXPECT_EQ(ServerVoteResult(-1, 0, "Player: Vote passed"), EVoteResult::NONE);
	EXPECT_EQ(ServerVoteResult(-1, 0, nullptr), EVoteResult::NONE);
}

TEST(VoteMapLibrary, MergePreservesCatalogMetadataAndServerOnlyMaps)
{
	CVoteOptionClient aOptions[3]{};
	str_copy(aOptions[0].m_aDescription, "Map: Alpha");
	str_copy(aOptions[1].m_aDescription, "Map: Custom");
	str_copy(aOptions[2].m_aDescription, "Restart server");
	aOptions[0].m_pNext = &aOptions[1];
	aOptions[1].m_pNext = &aOptions[2];
	const auto vMerged = MergeCatalog({{"Alpha", "Author", "Novice", "", 2, 3}}, aOptions);
	ASSERT_EQ(vMerged.size(), 2u);
	EXPECT_EQ(vMerged[0].m_Mapper, "Author");
	EXPECT_EQ(vMerged[1].m_Name, "Custom");
	EXPECT_TRUE(vMerged[1].m_Category.empty());
	EXPECT_EQ(MergeCatalog({}, aOptions).size(), 2u);
}

TEST(VoteMapLibrary, CombinesCategoryStarsFavoritesSearchAndCompletion)
{
	const SMap Map{"森林", "作者", "Novice", "", 2, 3};
	SFilter Filter;
	Filter.m_Category = "Novice";
	Filter.m_StarMask = (1 << 2) | (1 << 3);
	Filter.m_FavoritesOnly = true;
	Filter.m_Search = "作者";
	Filter.m_Completion = ECompletion::UNFINISHED;
	EXPECT_TRUE(Matches(Map, Filter, true, ECompletion::UNFINISHED));
	EXPECT_FALSE(Matches(Map, Filter, false, ECompletion::UNFINISHED));
	EXPECT_FALSE(Matches(Map, Filter, true, ECompletion::FINISHED));
	EXPECT_FALSE(Matches(Map, Filter, true, ECompletion::UNKNOWN));
	Filter.m_Exclude = "森林";
	EXPECT_FALSE(Matches(Map, Filter, true, ECompletion::UNFINISHED));
}

TEST(VoteMapLibrary, UnfilteredViewIncludesUnknownCompletionAndDifficulty)
{
	const SMap Map{"Custom"};
	SFilter Filter;
	EXPECT_TRUE(Matches(Map, Filter, false, ECompletion::UNKNOWN));
	Filter.m_StarMask = 1;
	EXPECT_FALSE(Matches(Map, Filter, false, ECompletion::UNKNOWN));
	EXPECT_TRUE(Matches({"Zero", "", "Race", "", 0}, Filter, false, ECompletion::UNKNOWN));
}

TEST(VoteMapLibrary, SortingUsesMetadataAndKeepsUnknownDifficultyLast)
{
	const std::vector<SMap> vMaps = {{"B", "", "", "2026-10-01", 3}, {"A", "", "", "2025-01-01", 0}, {"Custom"}};
	std::vector<int> vIndices{0, 1, 2};
	Sort(vIndices, vMaps, ESort::DIFFICULTY);
	EXPECT_EQ(vIndices, (std::vector<int>{1, 0, 2}));
	Sort(vIndices, vMaps, ESort::NEWEST);
	EXPECT_EQ(vIndices, (std::vector<int>{0, 1, 2}));
}

TEST(VoteMapLibrary, RandomCandidateStaysInFilteredSetAndHandlesEmptySet)
{
	EXPECT_EQ(Pick({}, 0), -1);
	EXPECT_EQ(Pick({12}, std::numeric_limits<unsigned>::max()), 12);
	for(unsigned Ticket = 0; Ticket < 20; ++Ticket)
	{
		const int Result = Pick({2, 9, 17}, Ticket);
		EXPECT_TRUE(Result == 2 || Result == 9 || Result == 17);
	}
}

TEST(VoteMapLibrary, CategoryKeyNormalizesCatalogDotNotation)
{
	EXPECT_STREQ(CategoryKey("DDmaX.Easy"), "DDmaX Easy");
	EXPECT_STREQ(CategoryKey("DDmaX.Next"), "DDmaX Next");
	EXPECT_STREQ(CategoryKey("ddmax.pro"), "DDmaX Pro");
	EXPECT_STREQ(CategoryKey("DDmaX Nut"), "DDmaX Nut");
	EXPECT_STREQ(CategoryKey("DDmaX"), "DDmaX");
	EXPECT_STREQ(CategoryKey("Novice"), "Novice");
	EXPECT_STREQ(CategoryKey("New category"), "New category");
	EXPECT_STREQ(CategoryKey(""), "");
	EXPECT_STREQ(CategoryKey(nullptr), "");
}

TEST(VoteMapLibrary, DdmaxParentFilterGroupsEverySubcategory)
{
	// 目录没有裸 DDmaX 类型：父项按四个子类归组，而不是永远空列表。
	SFilter Filter;
	Filter.m_Category = "DDmaX";
	const auto MatchesType = [&](const char *pType) {
		return Matches(SMap{"Map", "", pType}, Filter, false, ECompletion::UNKNOWN);
	};
	for(const char *pType : {"DDmaX.Easy", "DDmaX.Next", "DDmaX.Pro", "DDmaX.Nut", "DDmaX Easy"})
		EXPECT_TRUE(MatchesType(pType)) << pType;
	for(const char *pType : {"Novice", "", "New category"})
		EXPECT_FALSE(MatchesType(pType)) << pType;
}

TEST(VoteMapLibrary, CatalogDotNotationMatchesSpacedSidebarFilters)
{
	// ddnet.org 目录把 DDmaX 子类写成点号，侧栏键是空格写法；筛选必须按类出结果。
	const char *pJson = R"([
		{"name":"Easy A","type":"DDmaX.Easy","difficulty":2},
		{"name":"Next A","type":"DDmaX.Next","difficulty":2},
		{"name":"Next B","type":"DDmaX.Next","difficulty":3},
		{"name":"Pro A","type":"DDmaX.Pro","difficulty":4},
		{"name":"Nut A","type":"DDmaX.Nut","difficulty":5},
		{"name":"Novice A","type":"Novice","difficulty":1}
	])";
	std::vector<SMap> vMaps;
	ASSERT_TRUE(ParseCatalog(pJson, str_length(pJson), vMaps));
	ASSERT_EQ(vMaps.size(), 6u);
	// 目录保留接口原文写法，详情接口仍按同一字符串校验。
	EXPECT_EQ(vMaps[0].m_Category, "DDmaX.Easy");
	const auto Count = [&](const char *pCategory) {
		SFilter Filter;
		Filter.m_Category = pCategory;
		int Result = 0;
		for(const auto &Map : vMaps)
			Result += Matches(Map, Filter, false, ECompletion::UNKNOWN) ? 1 : 0;
		return Result;
	};
	EXPECT_EQ(Count("DDmaX Easy"), 1);
	EXPECT_EQ(Count("DDmaX Next"), 2);
	EXPECT_EQ(Count("DDmaX Pro"), 1);
	EXPECT_EQ(Count("DDmaX Nut"), 1);
	EXPECT_EQ(Count("DDmaX"), 5);
	EXPECT_EQ(Count("Novice"), 1);
}

TEST(VoteMapLibrary, TypeVoteOptionMatchesOfficialFlexVoteLabels)
{
	// 官方 ddnet-maps 的切换选项用装饰符号与小型大写字母排版，界面键名是普通写法。
	EXPECT_TRUE(TypeVoteOptionMatches("☐ DDᴍᴀX.Nᴇxᴛ Mᴀᴘs", "DDmaX.Next", "经典 Next"));
	EXPECT_TRUE(TypeVoteOptionMatches("☐ Nᴏᴠɪᴄᴇ Mᴀᴘs", "Novice", "简单"));
	EXPECT_TRUE(TypeVoteOptionMatches("☐ Oʟᴅsᴄʜᴏᴏʟ Mᴀᴘs", "Oldschool", "传统"));
	EXPECT_TRUE(TypeVoteOptionMatches("☐ DDᴍᴀX.Eᴀsʏ Mᴀᴘs", "DDmaX Easy", "经典 Easy"));
	// 当前分类的 info 头不是切换项，选中的正是它时也不能投它。
	EXPECT_FALSE(TypeVoteOptionMatches("☒ DDᴍᴀX.Eᴀsʏ Mᴀᴘs", "DDmaX.Next", "经典 Next"));
	EXPECT_FALSE(TypeVoteOptionMatches("☒ DDᴍᴀX.Eᴀsʏ Mᴀᴘs", "DDmaX Easy", "经典 Easy"));
	EXPECT_FALSE(TypeVoteOptionMatches("Make sure no one is racing before voting!", "Novice", "简单"));
	EXPECT_FALSE(TypeVoteOptionMatches("", "Novice", "简单"));
	EXPECT_FALSE(TypeVoteOptionMatches("☐ Nᴏᴠɪᴄᴇ Mᴀᴘs", "", ""));
	// 纯文本与小写写法继续兼容。
	EXPECT_TRUE(TypeVoteOptionMatches("DDmaX Next Maps", "DDmaX.Next", "经典 Next"));
	EXPECT_TRUE(TypeVoteOptionMatches("ddmax next maps", "DDmaX Next", "经典 Next"));
}
