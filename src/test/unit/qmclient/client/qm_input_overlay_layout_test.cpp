#include <game/client/components/qmclient/input_overlay.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <vector>

namespace
{
	class COverlayGraphics : public CIconBenchmarkGraphics
	{
	public:
		std::array<int, 4> m_aUnloads{};
		CTextureHandle CreateLayoutTexture(int Id) { return CreateTextureHandle(Id); }

		void UnloadTexture(CTextureHandle *pHandle) override
		{
			++m_aUnloads.at(pHandle->Id());
			pHandle->Invalidate();
		}
	};
}

TEST(QmInputOverlayLayoutTexture, FailedReplacementReleasesOnlyTheCandidateTexture)
{
	COverlayGraphics Graphics;
	std::vector<QmInputOverlay::CObsLayoutTexture> vActive;
	vActive.emplace_back().Take(Graphics, Graphics.CreateLayoutTexture(1));
	const auto LoadReplacement = [&] {
		std::vector<QmInputOverlay::CObsLayoutTexture> vCandidate;
		vCandidate.emplace_back().Take(Graphics, Graphics.CreateLayoutTexture(2));
		throw std::bad_alloc();
	};

	EXPECT_THROW(LoadReplacement(), std::bad_alloc);
	EXPECT_EQ(Graphics.m_aUnloads[2], 1);
	EXPECT_EQ(Graphics.m_aUnloads[1], 0);
	EXPECT_EQ(vActive.front().Handle().Id(), 1);

	vActive.clear();
	EXPECT_EQ(Graphics.m_aUnloads[1], 1);
	EXPECT_EQ(Graphics.m_aUnloads[2], 1);
}

TEST(QmInputOverlayLayoutTexture, MovingLayoutKeepsTextureUntilTheNewOwnerIsDestroyed)
{
	COverlayGraphics Graphics;
	std::vector<QmInputOverlay::CObsLayoutTexture> vLayouts;
	vLayouts.emplace_back().Take(Graphics, Graphics.CreateLayoutTexture(1));

	vLayouts.reserve(vLayouts.capacity() + 1);
	EXPECT_EQ(Graphics.m_aUnloads[1], 0);
	EXPECT_EQ(vLayouts.front().Handle().Id(), 1);

	vLayouts.clear();
	EXPECT_EQ(Graphics.m_aUnloads[1], 1);
}

TEST(QmInputOverlayAtlasSampling, RoundedSampleCanReachTheImageEdge)
{
	const auto Rect = QmInputOverlay::ResolveObsAtlasSampleRect(2.6f, 4.4f, 8.0f, 8.0f, 11, 12);
	ASSERT_TRUE(Rect.has_value());
	EXPECT_EQ(Rect->m_X, 3);
	EXPECT_EQ(Rect->m_Y, 4);
	EXPECT_EQ(Rect->m_W, 8);
	EXPECT_EQ(Rect->m_H, 8);
}

TEST(QmInputOverlayAtlasSampling, RejectsBoundsThatCannotFitInTheAtlas)
{
	constexpr int MaxSize = std::numeric_limits<int>::max();
	const float LastWholeColumn = std::nextafter(static_cast<float>(MaxSize), 0.0f);
	EXPECT_FALSE(QmInputOverlay::ResolveObsAtlasSampleRect(LastWholeColumn, 0.0f, 256.0f, 8.0f, MaxSize, 64).has_value());
	EXPECT_FALSE(QmInputOverlay::ResolveObsAtlasSampleRect(0.0f, 60.0f, 8.0f, 8.0f, 64, 64).has_value());
}
