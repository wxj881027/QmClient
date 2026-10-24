#include <base/str.h>

#include <game/client/components/qmclient/qm_sponsor_authors.h>

#include <gtest/gtest.h>

TEST(QmSponsorAuthors, ProvidesTheFourDisplayedAuthors)
{
	const auto &Authors = QmSponsorAuthors::Authors();
	ASSERT_EQ(Authors.size(), 4u);
	EXPECT_STREQ(Authors[0].m_pName, "璇梦");
	EXPECT_STREQ(Authors[1].m_pName, "DYL");
	EXPECT_STREQ(Authors[2].m_pName, "夏日");
	EXPECT_STREQ(Authors[0].m_pSkin, "qwqdog_mie");
	EXPECT_STREQ(Authors[1].m_pSkin, "default_v2");
	EXPECT_STREQ(Authors[2].m_pSkin, "Miemiemiea");
	EXPECT_STREQ(Authors[3].m_pName, "丘卡");
	EXPECT_STREQ(Authors[3].m_pSkin, "Vaporeon");
	EXPECT_FALSE(Authors[3].m_CustomColors);
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

TEST(QmSponsorAuthors, OnlyXiariHasGlitchAndBlur)
{
	const CUIRect Name{100, 100, 30, 20};
	for(const auto &Author : QmSponsorAuthors::Authors())
	{
		const auto Result = QmSponsorAuthors::NamePresentation(Author, Name, Name, vec2(110, 110), true, false);
		if(str_comp(Author.m_pName, "夏日") == 0)
		{
			EXPECT_TRUE(Author.m_Glitch);
			EXPECT_TRUE(Result.m_Redacted);
		}
		else
		{
			EXPECT_FALSE(Author.m_Glitch);
			EXPECT_STREQ(Result.m_pText, Author.m_pName);
			EXPECT_FLOAT_EQ(Result.m_Blur, 0.0f);
		}
	}
}

TEST(QmSponsorAuthors, XiariBlurIncreasesAsPointerApproachesTheName)
{
	const auto &Author = QmSponsorAuthors::Authors()[2];
	const CUIRect Name{100, 100, 30, 20};
	const auto Far = QmSponsorAuthors::NamePresentation(Author, Name, Name, vec2(10, 110), true, false);
	const auto Near = QmSponsorAuthors::NamePresentation(Author, Name, Name, vec2(80, 110), true, false);
	const auto Edge = QmSponsorAuthors::NamePresentation(Author, Name, Name, vec2(99, 110), true, false);
	EXPECT_GT(Far.m_Blur, 0.0f);
	EXPECT_LT(Far.m_Blur, Near.m_Blur);
	EXPECT_LT(Near.m_Blur, Edge.m_Blur);
	EXPECT_STREQ(Edge.m_pText, "夏日");
}

TEST(QmSponsorAuthors, HoverRedactsAndExpandedTextRetainsHoverUntilPointerLeaves)
{
	const auto &Author = QmSponsorAuthors::Authors()[2];
	const CUIRect Name{100, 100, 30, 20};
	const CUIRect Redacted{60, 100, 110, 20};
	const auto Enter = QmSponsorAuthors::NamePresentation(Author, Name, Redacted, vec2(115, 110), true, false);
	ASSERT_TRUE(Enter.m_Redacted);
	EXPECT_STREQ(Enter.m_pText, "[数据删除]");
	EXPECT_FLOAT_EQ(Enter.m_Blur, 0.0f);
	const auto Expanded = QmSponsorAuthors::NamePresentation(Author, Name, Redacted, vec2(70, 110), true, Enter.m_Redacted);
	EXPECT_TRUE(Expanded.m_Redacted);
	const auto Leave = QmSponsorAuthors::NamePresentation(Author, Name, Redacted, vec2(40, 110), true, Expanded.m_Redacted);
	EXPECT_FALSE(Leave.m_Redacted);
	EXPECT_STREQ(Leave.m_pText, "夏日");
	const auto OutsideOriginal = QmSponsorAuthors::NamePresentation(Author, Name, Redacted, vec2(70, 110), true, Leave.m_Redacted);
	EXPECT_FALSE(OutsideOriginal.m_Redacted);
}

TEST(QmSponsorAuthors, InactivePointerRestoresBaselineBlurWithoutRedaction)
{
	const auto &Author = QmSponsorAuthors::Authors()[2];
	const CUIRect Name{100, 100, 30, 20};
	const auto Result = QmSponsorAuthors::NamePresentation(Author, Name, Name, vec2(115, 110), false, true);
	EXPECT_FALSE(Result.m_Redacted);
	EXPECT_STREQ(Result.m_pText, "夏日");
	EXPECT_FLOAT_EQ(Result.m_Blur, 0.12f);
}

TEST(QmSponsorAuthors, GlitchHasShortBurstsAndReturnsToTheOriginalTee)
{
	const auto &Author = QmSponsorAuthors::Authors()[2];
	const auto Burst = QmSponsorAuthors::TeePresentation(Author, 0.13, 2);
	EXPECT_GT(length(Burst.m_Offset), 0.0f);
	const auto Rest = QmSponsorAuthors::TeePresentation(Author, 1.0, 2);
	EXPECT_FLOAT_EQ(length(Rest.m_Offset), 0.0f);
	EXPECT_FLOAT_EQ(Rest.m_Scale, 1.0f);
	const auto Repeated = QmSponsorAuthors::TeePresentation(Author, 3.33, 2);
	EXPECT_EQ(Burst.m_Offset, Repeated.m_Offset);
	EXPECT_FLOAT_EQ(Burst.m_Scale, Repeated.m_Scale);
}

TEST(QmSponsorAuthors, ReducedMotionAndOtherAuthorsRemainStationary)
{
	for(const int Level : {0, 1})
	{
		const auto Result = QmSponsorAuthors::TeePresentation(QmSponsorAuthors::Authors()[2], 0.13, Level);
		EXPECT_FLOAT_EQ(length(Result.m_Offset), 0.0f);
		EXPECT_FLOAT_EQ(Result.m_Scale, 1.0f);
	}
	const auto Qiuka = QmSponsorAuthors::TeePresentation(QmSponsorAuthors::Authors()[3], 0.13, 2);
	EXPECT_FLOAT_EQ(length(Qiuka.m_Offset), 0.0f);
	EXPECT_FLOAT_EQ(Qiuka.m_Scale, 1.0f);
}
