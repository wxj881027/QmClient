#include <game/client/components/qmclient/map_tags.h>

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace
{
	std::string Snapshot(const char *pTags, bool Eligible = true)
	{
		return std::string("{\"ok\":true,\"community\":\"ddnet\",\"map\":\"Lighthouse\",\"player\":\"Alice\",\"eligibility_error\":\"\",\"eligible\":") + (Eligible ? "true" : "false") + ",\"tags\":[" + pTags + "]}";
	}
}

TEST(QmMapTags, CountsUnicodeCharactersInsteadOfUtf8Bytes)
{
	EXPECT_TRUE(QmMapTags::ValidTag("一二三四五六七八九十"));
	EXPECT_FALSE(QmMapTags::ValidTag("一二三四五六七八九十一"));
	EXPECT_TRUE(QmMapTags::ValidTag("Gores"));
	EXPECT_FALSE(QmMapTags::ValidTag(""));
	EXPECT_FALSE(QmMapTags::ValidTag("bad\ntext"));
	EXPECT_FALSE(QmMapTags::ValidTag("\xff"));
}

TEST(QmMapTags, ReadsApprovedCapsuleCountAndOwnSupport)
{
	const auto Json = Snapshot("{\"id\":\"id-1\",\"text\":\"这是好图\",\"color\":3,\"count\":99,\"voted\":true}");
	QmMapTags::SSnapshot Parsed;
	ASSERT_TRUE(QmMapTags::ParseSnapshot(Json.data(), Json.size(), Parsed));
	ASSERT_EQ(Parsed.m_vTags.size(), 1u);
	EXPECT_EQ(Parsed.m_vTags[0].m_Text, "这是好图");
	EXPECT_EQ(Parsed.m_vTags[0].m_Count, 99);
	EXPECT_TRUE(Parsed.m_vTags[0].m_Voted);
	EXPECT_TRUE(Parsed.m_Eligible);
	EXPECT_EQ(Parsed.m_Map, "Lighthouse");
}

TEST(QmMapTags, RejectsDuplicateColorsWithoutReplacingPublishedSnapshot)
{
	const auto Json = Snapshot("{\"id\":\"a\",\"text\":\"Hook\",\"color\":1,\"count\":1,\"voted\":false},{\"id\":\"b\",\"text\":\"fly\",\"color\":1,\"count\":2,\"voted\":false}");
	QmMapTags::SSnapshot Parsed;
	Parsed.m_Map = "previous";
	EXPECT_FALSE(QmMapTags::ParseSnapshot(Json.data(), Json.size(), Parsed));
	EXPECT_EQ(Parsed.m_Map, "previous");
}

TEST(QmMapTags, RejectsNegativeCountsAndOversizedLabels)
{
	for(const char *pTag : {"{\"id\":\"a\",\"text\":\"Hook\",\"color\":0,\"count\":-1,\"voted\":false}", "{\"id\":\"a\",\"text\":\"abcdefghijk\",\"color\":0,\"count\":1,\"voted\":false}"})
	{
		const auto Json = Snapshot(pTag);
		QmMapTags::SSnapshot Parsed;
		EXPECT_FALSE(QmMapTags::ParseSnapshot(Json.data(), Json.size(), Parsed));
	}
}

TEST(QmMapTags, SpectatorsWithoutCompletionCanReadEmptyTags)
{
	const auto Json = Snapshot("", false);
	QmMapTags::SSnapshot Parsed;
	ASSERT_TRUE(QmMapTags::ParseSnapshot(Json.data(), Json.size(), Parsed));
	EXPECT_FALSE(Parsed.m_Eligible);
	EXPECT_TRUE(Parsed.m_vTags.empty());
}

TEST(QmMapTags, EveryStoredColorHasDistinctStableHue)
{
	std::set<float> Hues;
	for(int Color = 0; Color < QmMapTags::MAX_TAGS; ++Color)
	{
		const float Hue = QmMapTags::ColorHue(Color);
		EXPECT_GE(Hue, 0.0f);
		EXPECT_LT(Hue, 1.0f);
		EXPECT_TRUE(Hues.insert(Hue).second);
		EXPECT_EQ(Hue, QmMapTags::ColorHue(Color));
	}
}
