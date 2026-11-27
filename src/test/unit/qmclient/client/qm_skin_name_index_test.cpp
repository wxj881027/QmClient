#include <game/client/components/qmclient/skin_name_index.h>

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>

TEST(QmSkinNameIndex, FindsCanonicalNameIgnoringCase)
{
	CSkinNameIndex Index;
	ASSERT_TRUE(Index.Add("M_buoumao"));

	EXPECT_STREQ(Index.Find("m_buoumao"), "M_buoumao");
	EXPECT_STREQ(Index.Find("M_BUOUMAO"), "M_buoumao");
	EXPECT_EQ(Index.Find("missing"), nullptr);
}

TEST(QmSkinNameIndex, HigherPriorityVariantReplacesExistingName)
{
	CSkinNameIndex Index;
	ASSERT_TRUE(Index.Add("m_buoumao", 0));
	ASSERT_TRUE(Index.Add("M_buoumao", 1));

	EXPECT_STREQ(Index.Find("m_buoumao"), "M_buoumao");
	EXPECT_STREQ(Index.Find("M_buoumao"), "M_buoumao");
}

TEST(QmSkinNameIndex, EqualPriorityKeepsFirstCanonicalName)
{
	CSkinNameIndex Index;
	ASSERT_TRUE(Index.Add("M_buoumao", 1));
	EXPECT_FALSE(Index.Add("m_buoumao", 1));

	EXPECT_STREQ(Index.Find("m_buoumao"), "M_buoumao");
}

TEST(QmSkinNameIndex, MixedCaseReplacesLowercaseAtSamePriority)
{
	CSkinNameIndex Index;
	ASSERT_TRUE(Index.Add("alpha_skin", 1));
	ASSERT_TRUE(Index.Add("Alpha_Skin", 1));

	EXPECT_STREQ(Index.Find("ALPHA_SKIN"), "Alpha_Skin");
}

TEST(QmSkinNameIndex, UnicodeNamesUseTheSameCaseFoldedIdentity)
{
	CSkinNameIndex Index;
	ASSERT_TRUE(Index.Add("ÄSkin"));

	EXPECT_STREQ(Index.Find("äskin"), "ÄSkin");
}

TEST(QmSkinNameIndex, RemoveUsesCaseFoldedIdentity)
{
	CSkinNameIndex Index;
	ASSERT_TRUE(Index.Add("M_buoumao"));

	Index.Remove("m_BUOUMAO");

	EXPECT_EQ(Index.Find("M_buoumao"), nullptr);
}

TEST(QmSkinNameIndex, CaseInsensitiveMapFindsExistingContainerForEitherCase)
{
	std::unordered_map<std::string, int, SSkinNameHash, SSkinNameEqual> Entries;
	Entries.emplace("m_buoumao", 1);

	const auto Entry = Entries.find("M_buoumao");
	ASSERT_NE(Entry, Entries.end());
	EXPECT_EQ(Entry->second, 1);
	EXPECT_EQ(Entries.size(), 1u);
}
