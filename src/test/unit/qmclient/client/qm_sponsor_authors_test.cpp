#include <base/str.h>

#include <game/client/components/qmclient/qm_sponsor_authors.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

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

TEST(QmSponsorAuthors, XiariKeepsVisibleGlitchComponentsThroughoutTheCycle)
{
	const auto &Author = QmSponsorAuthors::Authors()[2];
	for(const double Time : {0.0, 0.13, 0.5, 1.0, 2.8, 3.19, 3.21, 4.0, 5.7, 7.99})
	{
		SCOPED_TRACE(Time);
		const auto Result = QmSponsorAuthors::TeePresentation(Author, Time);
		EXPECT_TRUE(Result.m_Glitch);
		EXPECT_GT(Result.m_ChromaOffset, 0.0f);
		EXPECT_GT(Result.m_ChromaAlpha, 0.0f);
		EXPECT_GT(Result.m_ScanlineHeight, 0.0f);
		EXPECT_GT(Result.m_ScanlineAlpha, 0.0f);
		for(float Offset : Result.m_aSliceOffsets)
			EXPECT_GT(std::abs(Offset), 0.0f);
	}
}

TEST(QmSponsorAuthors, SliceTearingChangesOverTimeAndStaysWithinTheAvatarMargin)
{
	const auto &Author = QmSponsorAuthors::Authors()[2];
	const auto First = QmSponsorAuthors::TeePresentation(Author, 0.0);
	const auto Later = QmSponsorAuthors::TeePresentation(Author, 0.2);
	EXPECT_NE(First.m_aSliceOffsets, Later.m_aSliceOffsets);
	for(const auto &Result : {First, Later})
	{
		bool HasLeft = false, HasRight = false;
		for(float Offset : Result.m_aSliceOffsets)
		{
			HasLeft |= Offset < 0.0f;
			HasRight |= Offset > 0.0f;
			EXPECT_LT(std::abs(Offset), 0.15f);
		}
		EXPECT_TRUE(HasLeft);
		EXPECT_TRUE(HasRight);
	}
}

TEST(QmSponsorAuthors, ScanlineTravelsAndWrapsWhileChromaRemainsVisible)
{
	const auto &Author = QmSponsorAuthors::Authors()[2];
	const auto First = QmSponsorAuthors::TeePresentation(Author, 0.25);
	const auto Later = QmSponsorAuthors::TeePresentation(Author, 1.25);
	const auto Wrapped = QmSponsorAuthors::TeePresentation(Author, 2.25);
	EXPECT_LT(First.m_ScanlineY, Later.m_ScanlineY);
	EXPECT_FLOAT_EQ(First.m_ScanlineY, Wrapped.m_ScanlineY);
	EXPECT_NE(First.m_ChromaOffset, Later.m_ChromaOffset);
	for(const auto &Result : {First, Later, Wrapped})
	{
		EXPECT_GE(Result.m_ScanlineY, 0.0f);
		EXPECT_LT(Result.m_ScanlineY, 1.0f);
		EXPECT_GT(Result.m_ChromaAlpha, 0.0f);
		EXPECT_LE(Result.m_ChromaAlpha, 1.0f);
	}
}

TEST(QmSponsorAuthors, GlitchIsDeterministicAcrossClockWrapAndNegativeTimes)
{
	const auto &Author = QmSponsorAuthors::Authors()[2];
	const auto Reference = QmSponsorAuthors::TeePresentation(Author, 7.75);
	for(const double Time : {-0.25, 15.75, 8000007.75})
	{
		SCOPED_TRACE(Time);
		const auto Result = QmSponsorAuthors::TeePresentation(Author, Time);
		EXPECT_EQ(Result.m_aSliceOffsets, Reference.m_aSliceOffsets);
		EXPECT_FLOAT_EQ(Result.m_ChromaOffset, Reference.m_ChromaOffset);
		EXPECT_FLOAT_EQ(Result.m_ChromaAlpha, Reference.m_ChromaAlpha);
		EXPECT_FLOAT_EQ(Result.m_ScanlineY, Reference.m_ScanlineY);
	}
}

TEST(QmSponsorAuthors, InvalidClockKeepsAVisibleFiniteInitialGlitch)
{
	const auto &Author = QmSponsorAuthors::Authors()[2];
	const auto Initial = QmSponsorAuthors::TeePresentation(Author, 0.0);
	for(const double Time : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
	{
		const auto Result = QmSponsorAuthors::TeePresentation(Author, Time);
		EXPECT_TRUE(Result.m_Glitch);
		EXPECT_EQ(Result.m_aSliceOffsets, Initial.m_aSliceOffsets);
		EXPECT_FLOAT_EQ(Result.m_ChromaOffset, Initial.m_ChromaOffset);
		EXPECT_FLOAT_EQ(Result.m_ScanlineY, Initial.m_ScanlineY);
	}
}

TEST(QmSponsorAuthors, OtherAuthorsKeepTheirOriginalSkinWithoutGlitchLayers)
{
	for(const auto &Author : QmSponsorAuthors::Authors())
	{
		if(Author.m_Glitch)
			continue;
		SCOPED_TRACE(Author.m_pName);
		const auto Result = QmSponsorAuthors::TeePresentation(Author, 1.25);
		EXPECT_FALSE(Result.m_Glitch);
		EXPECT_FLOAT_EQ(Result.m_ChromaOffset, 0.0f);
		EXPECT_FLOAT_EQ(Result.m_ChromaAlpha, 0.0f);
		EXPECT_FLOAT_EQ(Result.m_ScanlineAlpha, 0.0f);
		for(float Offset : Result.m_aSliceOffsets)
			EXPECT_FLOAT_EQ(Offset, 0.0f);
	}
}
