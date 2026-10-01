#include <game/client/components/qmclient/qm_sponsor_authors.h>

#include <gtest/gtest.h>

TEST(QmSponsorAuthors, ProvidesTheThreeDisplayedAuthors)
{
	const auto &Authors = QmSponsorAuthors::Authors();
	ASSERT_EQ(Authors.size(), 3u);
	EXPECT_STREQ(Authors[0].m_pName, "璇梦");
	EXPECT_STREQ(Authors[1].m_pName, "DYL");
	EXPECT_STREQ(Authors[2].m_pName, "夏日");
	EXPECT_STREQ(Authors[0].m_pSkin, "qwqdog_mie");
	EXPECT_STREQ(Authors[1].m_pSkin, "default_v2");
	EXPECT_STREQ(Authors[2].m_pSkin, "Miemiemiea");
}

TEST(QmSponsorAuthors, DylUsesWhiteCustomColors)
{
	// DYL 使用可着色皮肤 default_v2，必须以自定义颜色渲染，身体与脚均为白色；
	// 其余作者不启用自定义颜色。
	const auto &Authors = QmSponsorAuthors::Authors();
	ASSERT_TRUE(Authors[1].m_CustomColors);
	EXPECT_EQ(Authors[1].m_BodyColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	EXPECT_EQ(Authors[1].m_FeetColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	EXPECT_FALSE(Authors[0].m_CustomColors);
	EXPECT_FALSE(Authors[2].m_CustomColors);
}

TEST(QmSponsorAuthors, RowsHeightUsesOneHorizontalAuthorRow)
{
	EXPECT_FLOAT_EQ(QmSponsorAuthors::RowsHeight(50.0f, 4.0f, 20.0f), 74.0f);
	EXPECT_FLOAT_EQ(QmSponsorAuthors::RowsHeight(-50.0f, -4.0f, -20.0f), 0.0f);
}
