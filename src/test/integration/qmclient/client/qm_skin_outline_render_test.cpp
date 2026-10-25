#include <game/client/components/qmclient/qm_skin_outline.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <array>

namespace
{
	thread_local bool s_FailAllocation = false;
	void *Allocate(size_t Size) noexcept { return s_FailAllocation ? nullptr : std::malloc(Size); }
	class CAllocationScope
	{
		bool m_Previous = s_FailAllocation;

	public:
		CAllocationScope() { s_FailAllocation = false; }
		~CAllocationScope() { s_FailAllocation = m_Previous; }
	};
	class CGraphics : public CIconBenchmarkGraphics
	{
	public:
		int m_Unloads = 0;
		void UnloadTexture(CTextureHandle *pTexture) override
		{
			++m_Unloads;
			pTexture->Invalidate();
		}
		void QuadsDraw(CQuadItem *, int Num) override { m_Quads += Num; }
	};
}

class CQmSkinOutlineRenderTest : public ::testing::Test
{
protected:
	std::array<uint8_t, 16> m_aPixels;
	CImageInfo m_Source;
	CAllocationScope m_Scope;
	CGraphics m_Graphics;
	void SetUp() override
	{
		m_aPixels.fill(255);
		m_Source.m_Width = m_Source.m_Height = 2;
		m_Source.m_Format = CImageInfo::FORMAT_RGBA;
		m_Source.m_pData = m_aPixels.data();
	}
	void Render(CQmSkinOutline &Outline, int Width) { Outline.Render(&m_Graphics, {0, 0}, {2, 2}, 0, {1, 1, 1, 1}, Width); }
};

TEST_F(CQmSkinOutlineRenderTest, FailedReplacementDoesNotUnloadCachedTextureAndCanRetry)
{
	CQmSkinOutline Outline(m_Source, {0, 0}, {0, 0}, {2, 2}, {2, 2}, Allocate);
	Render(Outline, 1);
	ASSERT_EQ(m_Graphics.m_Uploads, 1u);
	const int Unloads = m_Graphics.m_Unloads;
	const uint64_t Draws = m_Graphics.m_Quads;
	s_FailAllocation = true;
	Render(Outline, 2);
	EXPECT_EQ(m_Graphics.m_Uploads, 1u);
	EXPECT_EQ(m_Graphics.m_Unloads, Unloads);
	EXPECT_EQ(m_Graphics.m_Quads, Draws);
	s_FailAllocation = false;
	Render(Outline, 2);
	EXPECT_EQ(m_Graphics.m_Uploads, 2u);
	EXPECT_EQ(m_Graphics.m_Unloads, Unloads + 1);
	EXPECT_EQ(m_Graphics.m_Quads, Draws + 1);
	Outline.Unload(&m_Graphics);
}

TEST_F(CQmSkinOutlineRenderTest, FailedInitialExpansionDoesNotSubmitEmptyTexture)
{
	CQmSkinOutline Outline(m_Source, {0, 0}, {0, 0}, {2, 2}, {2, 2}, Allocate);
	s_FailAllocation = true;
	Render(Outline, 1);
	EXPECT_EQ(m_Graphics.m_Uploads, 0u);
	EXPECT_EQ(m_Graphics.m_Unloads, 0);
	EXPECT_EQ(m_Graphics.m_Quads, 0u);
	s_FailAllocation = false;
	Render(Outline, 1);
	EXPECT_EQ(m_Graphics.m_Uploads, 1u);
	EXPECT_EQ(m_Graphics.m_Quads, 1u);
	Outline.Unload(&m_Graphics);
}
