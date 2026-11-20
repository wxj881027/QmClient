#include <game/client/components/qmclient/console_text.h>

#include <gtest/gtest.h>

namespace
{
	const ColorRGBA Base(1, 1, 1, 1), Marker(1, 0, 0, 1), Link(0, 0, 1, 1);
	class QmConsoleColorPreparation : public ::testing::Test
	{
	protected:
		QmConsoleText::CColorPreparation m_Preparation;
		std::vector<STextColorSplit> m_vResult;
		bool Prepare(const char *pText)
		{
			return m_Preparation.Prepare(pText, m_vResult, [&](auto &vLayers, const auto &vLinks) {
				vLayers.emplace_back(0, str_length(pText), Base);
				for(const auto &Range : vLinks)
					vLayers.emplace_back(Range.m_StartByte, Range.m_EndByte - Range.m_StartByte, Link);
				return true;
			});
		}
	};
}

TEST_F(QmConsoleColorPreparation, CompletedPlainTextHasOnlyItsBaseLayer)
{
	ASSERT_TRUE(Prepare("plain"));
	ASSERT_EQ(m_vResult.size(), 1u);
	EXPECT_EQ(m_vResult[0].m_CharIndex, 0);
	EXPECT_EQ(m_vResult[0].m_Length, 5);
	EXPECT_EQ(m_vResult[0].m_Color, Base);
	EXPECT_TRUE(m_Preparation.Links().empty());
}

TEST_F(QmConsoleColorPreparation, AllocationFailureDropsBothOldColorsAndLinkTargets)
{
	ASSERT_TRUE(Prepare("https://example.test"));
	ASSERT_EQ(m_Preparation.Links().size(), 1u);
	EXPECT_FALSE(m_Preparation.Prepare("https://new.test", m_vResult, [](auto &vLayers, const auto &) -> bool {
		vLayers.emplace_back(0, 1, Marker);
		throw std::bad_alloc();
	}));
	EXPECT_TRUE(m_vResult.empty());
	EXPECT_TRUE(m_Preparation.Links().empty());
}

TEST_F(QmConsoleColorPreparation, LengthFailureDoesNotPublishPartialDecoration)
{
	ASSERT_TRUE(Prepare("old"));
	EXPECT_FALSE(m_Preparation.Prepare("new", m_vResult, [](auto &, const auto &) -> bool { throw std::length_error("controlled preparation limit"); }));
	EXPECT_TRUE(m_vResult.empty());
	EXPECT_TRUE(m_Preparation.Links().empty());
}

TEST_F(QmConsoleColorPreparation, RejectedBuilderCannotLeaveClickableLinks)
{
	EXPECT_FALSE(m_Preparation.Prepare("https://example.test", m_vResult, [](auto &vLayers, const auto &) {
		vLayers.emplace_back(0, 1, Marker);
		return false;
	}));
	EXPECT_TRUE(m_vResult.empty());
	EXPECT_TRUE(m_Preparation.Links().empty());
}

TEST_F(QmConsoleColorPreparation, FailedDecorationCanBePreparedAgain)
{
	EXPECT_FALSE(m_Preparation.Prepare("bad", m_vResult, [](auto &, const auto &) -> bool { throw std::bad_alloc(); }));
	ASSERT_TRUE(Prepare("https://example.test"));
	ASSERT_EQ(m_Preparation.Links().size(), 1u);
	ASSERT_EQ(m_vResult.size(), 1u);
	EXPECT_EQ(m_vResult[0].m_Color, Link);
}

TEST_F(QmConsoleColorPreparation, ReusingThePreparationDoesNotCarryLinksToTheNextEntry)
{
	ASSERT_TRUE(Prepare("https://example.test"));
	ASSERT_TRUE(Prepare("plain"));
	EXPECT_TRUE(m_Preparation.Links().empty());
	ASSERT_EQ(m_vResult.size(), 1u);
	EXPECT_EQ(m_vResult[0].m_Color, Base);
}

TEST_F(QmConsoleColorPreparation, ExistingMarkerLayersRemainReadableUntilComposition)
{
	m_vResult.emplace_back(1, 2, Marker);
	ASSERT_TRUE(m_Preparation.Prepare("abcd", m_vResult, [&](auto &vLayers, const auto &) {
		vLayers.emplace_back(0, 4, Base);
		vLayers.insert(vLayers.end(), m_vResult.begin(), m_vResult.end());
		return true;
	}));
	ASSERT_EQ(m_vResult.size(), 3u);
	EXPECT_EQ(m_vResult[1].m_CharIndex, 1);
	EXPECT_EQ(m_vResult[1].m_Color, Marker);
}

TEST_F(QmConsoleColorPreparation, SeparateOutputKeepsTheCursorLayersUntouchedUntilSwap)
{
	std::vector<STextColorSplit> vCursor{{1, 2, Marker}};
	ASSERT_TRUE(m_Preparation.Prepare("abcd", m_vResult, [&](auto &vLayers, const auto &) {
		vLayers.emplace_back(0, 4, Base);
		vLayers.insert(vLayers.end(), vCursor.begin(), vCursor.end());
		return true;
	}));
	ASSERT_EQ(vCursor.size(), 1u);
	EXPECT_EQ(vCursor[0].m_CharIndex, 1);
	vCursor.swap(m_vResult);
	ASSERT_EQ(vCursor.size(), 3u);
	EXPECT_EQ(vCursor[1].m_Color, Marker);
}

TEST_F(QmConsoleColorPreparation, EmptyTextDoesNotRetainPreviousColors)
{
	ASSERT_TRUE(Prepare("old"));
	ASSERT_TRUE(Prepare(""));
	EXPECT_TRUE(m_vResult.empty());
	EXPECT_TRUE(m_Preparation.Links().empty());
}
