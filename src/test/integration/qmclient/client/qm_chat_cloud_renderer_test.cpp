#include <game/client/QmUi/QmCloudBubble.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/support/tracked_allocation_resource.h>

namespace
{
	class CCloudGraphics : public CIconBenchmarkGraphics
	{
	public:
		int m_Fallbacks = 0;
		int m_Puffs = 0;

		void DrawRect(float X, float Y, float Width, float Height, ColorRGBA Color, int Corners, float Radius) override
		{
			++m_Fallbacks;
		}

		void QuadsDrawTL(const CQuadItem *pQuads, int Count) override
		{
			m_Puffs += Count;
		}
	};
}

TEST(QmChatCloudRenderer, FailedAtlasAllocationKeepsBackgroundAndAllowsRetry)
{
	using namespace qm_chat_bubble;
	CCloudGraphics Graphics;
	CTrackedAllocationResource Memory;
	Memory.m_FailAt = 1;
	CCloudRenderer Renderer;
	const auto Bubble = Layout({100.0f, 100.0f}, {40.0f, 12.0f}, 12.0f, 1.0f, EStyle::CLOUD);
	const auto Colors = Palette(EStyle::CLOUD, false, {}, {}, 1.0f);

	ASSERT_NO_THROW(Renderer.Init(&Graphics, &Memory));
	EXPECT_EQ(Graphics.m_Uploads, 0u);
	Renderer.Render(&Graphics, Bubble, Colors, 1.0f);
	EXPECT_EQ(Graphics.m_Fallbacks, 1);
	EXPECT_EQ(Graphics.m_Puffs, 0);
	EXPECT_EQ(Memory.m_Allocations, 1);

	Renderer.Init(&Graphics, &Memory);
	Renderer.Render(&Graphics, Bubble, Colors, 1.0f);
	EXPECT_EQ(Graphics.m_Uploads, 1u);
	EXPECT_EQ(Graphics.m_Fallbacks, 1);
	EXPECT_EQ(Graphics.m_Puffs, CLOUD_PUFF_COUNT);
	EXPECT_EQ(Memory.m_Live, 0);
	Renderer.Shutdown(&Graphics);
}
