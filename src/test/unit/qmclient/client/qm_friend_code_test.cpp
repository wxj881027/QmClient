#include <base/system.h>

#include <game/client/components/qmclient/friend_code.h>

#include <gtest/gtest.h>
#include <zlib.h>

#include <algorithm>
#include <cstring>

namespace
{
	CFriendList Sample()
	{
		CFriendList List;
		List.m_vCategories = {IFriends::OFFLINE_CATEGORY, "一起玩", IFriends::DEFAULT_CATEGORY, IFriends::CLAN_MEMBERS_CATEGORY, "Empty"};
		CFriendInfo Friend{};
		str_copy(Friend.m_aName, "青竹\"\\");
		str_copy(Friend.m_aClan, "战队");
		str_copy(Friend.m_aCategory, "一起玩");
		str_copy(Friend.m_aNote, "老朋友 \"备注\" \\ 😀");
		Friend.m_Pinned = true;
		List.m_vFriends.push_back(Friend);
		CFriendInfo Clan{};
		str_copy(Clan.m_aClan, "全战队");
		str_copy(Clan.m_aCategory, IFriends::CLAN_MEMBERS_CATEGORY);
		List.m_vFriends.push_back(Clan);
		return List;
	}

	std::string WrapRaw(std::string_view Raw)
	{
		uLongf Size = compressBound(static_cast<uLong>(Raw.size()));
		std::vector<unsigned char> vCompressed(Size);
		if(compress2(vCompressed.data(), &Size, reinterpret_cast<const Bytef *>(Raw.data()), static_cast<uLong>(Raw.size()), Z_BEST_COMPRESSION) != Z_OK)
			return {};
		std::vector<char> vEncoded((Size + 2) / 3 * 4 + 1);
		str_base64(vEncoded.data(), static_cast<int>(vEncoded.size()), vCompressed.data(), static_cast<int>(Size));
		return std::string("QMF1:") + vEncoded.data();
	}
}

TEST(QmFriendCode, RoundTripPreservesUnicodeNotesPinsClanEntriesAndCategoryOrder)
{
	const auto Original = Sample();
	std::string Code;
	ASSERT_TRUE(qm_friend_code::Encode(Original, Code));
	EXPECT_TRUE(Code.starts_with("QMF1:"));
	EXPECT_EQ(Code.find_first_of("\r\n\t "), std::string::npos);
	CFriendList Restored;
	ASSERT_TRUE(qm_friend_code::Decode(Code, Restored));
	EXPECT_EQ(Restored.m_vCategories, Original.m_vCategories);
	ASSERT_EQ(Restored.m_vFriends.size(), Original.m_vFriends.size());
	for(size_t Index = 0; Index < Original.m_vFriends.size(); ++Index)
	{
		EXPECT_STREQ(Restored.m_vFriends[Index].m_aName, Original.m_vFriends[Index].m_aName);
		EXPECT_STREQ(Restored.m_vFriends[Index].m_aClan, Original.m_vFriends[Index].m_aClan);
		EXPECT_STREQ(Restored.m_vFriends[Index].m_aCategory, Original.m_vFriends[Index].m_aCategory);
		EXPECT_STREQ(Restored.m_vFriends[Index].m_aNote, Original.m_vFriends[Index].m_aNote);
		EXPECT_EQ(Restored.m_vFriends[Index].m_Pinned, Original.m_vFriends[Index].m_Pinned);
	}
}

TEST(QmFriendCode, EmptyListRetainsEmptyGroups)
{
	auto List = Sample();
	List.m_vFriends.clear();
	std::string Code;
	ASSERT_TRUE(qm_friend_code::Encode(List, Code));
	CFriendList Restored;
	ASSERT_TRUE(qm_friend_code::Decode(Code, Restored));
	EXPECT_TRUE(Restored.m_vFriends.empty());
	EXPECT_EQ(Restored.m_vCategories, List.m_vCategories);
}

TEST(QmFriendCode, ClipboardWhitespaceDoesNotChangeData)
{
	std::string Code;
	ASSERT_TRUE(qm_friend_code::Encode(Sample(), Code));
	Code.insert(Code.size() / 2, "\r\n\t ");
	CFriendList Restored;
	ASSERT_TRUE(qm_friend_code::Decode(" \n" + Code + "\r\n", Restored));
	EXPECT_EQ(Restored.m_vCategories, Sample().m_vCategories);
}

TEST(QmFriendCode, DamagedOrUnsupportedCodeDoesNotPublishPartialData)
{
	std::string Code;
	ASSERT_TRUE(qm_friend_code::Encode(Sample(), Code));
	std::string Corrupt = Code;
	Corrupt[Corrupt.size() / 2] = Corrupt[Corrupt.size() / 2] == 'A' ? 'B' : 'A';
	for(const auto &Bad : {std::string(), std::string("QMF2:") + Code.substr(5), Code.substr(0, Code.size() - 4), Corrupt, Code + "AAAA", std::string("QMF1:!!!!"), Code + std::string(1, '\0')})
	{
		SCOPED_TRACE(Bad.size());
		CFriendList Previous = Sample();
		ASSERT_FALSE(qm_friend_code::Decode(Bad, Previous));
		EXPECT_EQ(Previous.m_vCategories, Sample().m_vCategories);
		ASSERT_EQ(Previous.m_vFriends.size(), 2u);
		EXPECT_TRUE(Previous.m_vFriends[0].m_Pinned);
	}
}

TEST(QmFriendCode, EncodedAndInflatedSizeLimitsRejectUntrustedInput)
{
	CFriendList List;
	EXPECT_FALSE(qm_friend_code::Decode(std::string(qm_friend_code::MAX_CODE_BYTES + 1, 'A'), List));
	const std::string Bomb = WrapRaw(std::string(qm_friend_code::MAX_RAW_BYTES + 1, 'A'));
	ASSERT_FALSE(Bomb.empty());
	EXPECT_FALSE(qm_friend_code::Decode(Bomb, List));
}

TEST(QmFriendCode, InvalidBinaryCountsAndUnfinishedFieldsAreRejected)
{
	CFriendList List;
	for(const auto &Raw : {std::string("\xff\xff", 2), std::string("\x03\x00\x40", 3), std::string("\x03\x00\x06"
												       "A",
												   4)})
		EXPECT_FALSE(qm_friend_code::Decode(WrapRaw(Raw), List));
}

TEST(QmFriendCode, MaximumFriendAndCategoryCountsRoundTrip)
{
	auto List = Sample();
	List.m_vFriends.clear();
	while(List.m_vCategories.size() < IFriends::MAX_FRIEND_CATEGORIES)
		List.m_vCategories.push_back("Group" + std::to_string(List.m_vCategories.size()));
	for(int Index = 0; Index < IFriends::MAX_FRIENDS; ++Index)
	{
		CFriendInfo Friend{};
		str_format(Friend.m_aName, sizeof(Friend.m_aName), "Friend%d", Index);
		str_copy(Friend.m_aCategory, List.m_vCategories.back().c_str());
		str_copy(Friend.m_aNote, std::string(IFriends::MAX_FRIEND_NOTE_LENGTH - 1, 'n').c_str());
		List.m_vFriends.push_back(Friend);
	}
	std::string Code;
	ASSERT_TRUE(qm_friend_code::Encode(List, Code));
	CFriendList Restored;
	ASSERT_TRUE(qm_friend_code::Decode(Code, Restored));
	EXPECT_EQ(Restored.m_vFriends.size(), IFriends::MAX_FRIENDS);
	EXPECT_EQ(Restored.m_vCategories, List.m_vCategories);
	EXPECT_STREQ(Restored.m_vFriends.back().m_aNote, List.m_vFriends.back().m_aNote);
}

TEST(QmFriendCode, InvalidListCannotBeExported)
{
	const auto Reject = [](const CFriendList &List) {
		std::string Code = "unchanged";
		EXPECT_FALSE(qm_friend_code::Encode(List, Code));
		EXPECT_EQ(Code, "unchanged");
	};
	auto List = Sample();
	List.m_vFriends.push_back(List.m_vFriends.front());
	Reject(List);
	List = Sample();
	List.m_vCategories.push_back("friends");
	Reject(List);
	List = Sample();
	List.m_vCategories.erase(List.m_vCategories.begin());
	Reject(List);
	List = Sample();
	str_copy(List.m_vFriends.front().m_aCategory, "Missing");
	Reject(List);
	List = Sample();
	List.m_vFriends.back().m_Pinned = true;
	Reject(List);
	List = Sample();
	std::memset(List.m_vFriends.front().m_aName, 'a', sizeof(List.m_vFriends.front().m_aName));
	Reject(List);
	List = Sample();
	List.m_vFriends.front().m_aNote[0] = static_cast<char>(0xff);
	Reject(List);
}

TEST(QmFriendCode, EmbeddedLineBreaksCannotBecomeConfigurationCommands)
{
	for(const char *pText : {"friend\nquit", "friend\rquit"})
	{
		auto List = Sample();
		str_copy(List.m_vFriends.front().m_aNote, pText);
		std::string Code;
		EXPECT_FALSE(qm_friend_code::Encode(List, Code));
		List = Sample();
		List.m_vCategories.push_back(pText);
		EXPECT_FALSE(qm_friend_code::Encode(List, Code));
	}
}
