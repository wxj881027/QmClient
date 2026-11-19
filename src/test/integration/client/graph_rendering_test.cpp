#include <engine/client/graph.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/shared/console.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/gamecore.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/test.h>

#include <array>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace
{
	// 只记录设备收到的线段；采样、缩放、投影和文字入口均执行生产实现。
	class CGraphGraphics : public CIconBenchmarkGraphics
	{
		CLineItemBatch *m_pActiveBatch = nullptr;
		std::array<ColorRGBA, 2> m_aLineColors{};

	public:
		struct SLine
		{
			CLineItem m_Item;
			std::array<ColorRGBA, 2> m_aColors;
		};
		std::vector<SLine> m_vLines;
		size_t m_GuideLines = 0;
		int m_BatchBegins = 0;
		int m_BatchEnds = 0;

		void QuadsDrawTL(const CQuadItem *, int) override {}
		void LinesBegin() override {}
		void LinesEnd() override {}
		void LinesDraw(const CLineItem *, size_t Num) override { m_GuideLines += Num; }
		void LinesBatchBegin(CLineItemBatch *pBatch) override
		{
			EXPECT_EQ(m_pActiveBatch, nullptr);
			m_pActiveBatch = pBatch;
			++m_BatchBegins;
			m_aLineColors.fill(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		}
		void LinesBatchEnd(CLineItemBatch *pBatch) override
		{
			EXPECT_EQ(m_pActiveBatch, pBatch);
			m_pActiveBatch = nullptr;
			++m_BatchEnds;
		}
		void SetColorVertex(const CColorVertex *pArray, size_t Num) override
		{
			for(size_t i = 0; i < Num; ++i)
			{
				ASSERT_GE(pArray[i].m_Index, 0);
				ASSERT_LT(pArray[i].m_Index, 2);
				m_aLineColors[pArray[i].m_Index] = ColorRGBA(pArray[i].m_R, pArray[i].m_G, pArray[i].m_B, pArray[i].m_A);
			}
		}
		void LinesBatchDraw(CLineItemBatch *pBatch, const CLineItem *pArray, size_t Num) override
		{
			EXPECT_EQ(m_pActiveBatch, pBatch);
			for(size_t i = 0; i < Num; ++i)
				m_vLines.push_back({pArray[i], m_aLineColors});
		}
	};

	class GraphRendering : public ::testing::Test
	{
		CConfig m_PreviousConfig = g_Config;
		CTestInfo m_Info;
		std::unique_ptr<IKernel> m_pKernel;
		std::unique_ptr<IStorage> m_pStorage;
		CConsole m_Console{CFGFLAG_CLIENT};
		std::unique_ptr<IEngineTextRender> m_pText;

	protected:
		CGraphGraphics m_Graphics;

		void SetUp() override
		{
			g_Config = CConfig();
			m_Info.m_DeleteTestStorageFilesOnSuccess = false;
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pKernel.reset(IKernel::Create());
			m_Graphics.m_pStorage = m_pStorage.get();
			m_pKernel->RegisterInterface<IConsole>(&m_Console, false);
			m_pKernel->RegisterInterface<IStorage>(m_pStorage.get(), false);
			m_pKernel->RegisterInterface<IGraphics>(&m_Graphics, false);
			m_pText.reset(CreateEngineTextRender());
			m_pKernel->RegisterInterface<IEngineTextRender>(m_pText.get(), false);
			ASSERT_TRUE(m_pText->Init());
		}
		void TearDown() override
		{
			if(m_pText != nullptr)
				m_pText->Shutdown();
			g_Config = m_PreviousConfig;
		}
		void Render(CGraph &Graph)
		{
			Graph.Render(&m_Graphics, m_pText.get(), 10.0f, 20.0f, 120.0f, 132.0f, "Graph");
			EXPECT_EQ(m_Graphics.m_BatchBegins, m_Graphics.m_BatchEnds);
		}
	};
}

TEST_F(GraphRendering, FiniteSamplesKeepTheExistingPixelCoordinates)
{
	CGraph Graph(8, 0, false);
	Graph.Init(0.0f, 100.0f);
	ASSERT_TRUE(Graph.InsertAt(0, 0.0f));
	ASSERT_TRUE(Graph.InsertAt(5, 50.0f));
	ASSERT_TRUE(Graph.InsertAt(10, 100.0f));
	Graph.Scale(10);
	Render(Graph);
	ASSERT_EQ(m_Graphics.m_vLines.size(), 2U);
	const auto &First = m_Graphics.m_vLines[0].m_Item;
	EXPECT_FLOAT_EQ(First.m_X0, 10.0f);
	EXPECT_FLOAT_EQ(First.m_Y0, 139.0f);
	EXPECT_FLOAT_EQ(First.m_X1, 70.0f);
	EXPECT_FLOAT_EQ(First.m_Y1, 88.0f);
	const auto &Last = m_Graphics.m_vLines[1].m_Item;
	EXPECT_FLOAT_EQ(Last.m_X0, 70.0f);
	EXPECT_FLOAT_EQ(Last.m_Y0, 88.0f);
	EXPECT_FLOAT_EQ(Last.m_X1, 130.0f);
	EXPECT_FLOAT_EQ(Last.m_Y1, 37.0f);
}

TEST_F(GraphRendering, ZeroRangeTuningKeepsGuidesWithoutProjectingTheZeroAxis)
{
	CGraph Graph(8, 0, false);
	Graph.Init(0.0f, 0.0f);
	ASSERT_TRUE(Graph.InsertAt(0, 270.0f * VelocityRamp(270.0f, 0.0f, 0.0f, 1.4f) / 32.0f));
	ASSERT_TRUE(Graph.InsertAt(1, 540.0f * VelocityRamp(540.0f, 0.0f, 0.0f, 1.4f) / 32.0f));
	Graph.Scale(1);
	Render(Graph);
	EXPECT_TRUE(m_Graphics.m_vLines.empty());
	EXPECT_EQ(m_Graphics.m_GuideLines, 3);
	EXPECT_EQ(m_Graphics.m_BatchBegins, 0);
}

TEST_F(GraphRendering, NonFiniteAxesSkipTheCurve)
{
	const float Infinity = std::numeric_limits<float>::infinity();
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const std::array<std::pair<float, float>, 3> aRanges{{{NaN, 100.0f}, {0.0f, Infinity}, {-Infinity, 100.0f}}};
	for(const auto &[Min, Max] : aRanges)
	{
		SCOPED_TRACE(::testing::Message() << Min << ',' << Max);
		CGraph Graph(8, 0, false);
		Graph.Init(Min, Max);
		ASSERT_TRUE(Graph.InsertAt(0, 0.0f));
		ASSERT_TRUE(Graph.InsertAt(1, 10.0f));
		Graph.Scale(1);
		Render(Graph);
		EXPECT_TRUE(m_Graphics.m_vLines.empty());
	}
}

TEST_F(GraphRendering, NonFiniteSampleBreaksOnlyAdjacentSegmentsAndRestoresTheirColor)
{
	const ColorRGBA Red(1.0f, 0.0f, 0.0f, 0.75f);
	const ColorRGBA Blue(0.0f, 0.0f, 1.0f, 0.75f);
	CGraph Graph(8, 0, false);
	Graph.Init(0.0f, 100.0f);
	ASSERT_TRUE(Graph.InsertAt(0, 10.0f, Red));
	ASSERT_TRUE(Graph.InsertAt(1, 20.0f, Red));
	ASSERT_TRUE(Graph.InsertAt(2, std::numeric_limits<float>::quiet_NaN(), Blue));
	ASSERT_TRUE(Graph.InsertAt(3, 30.0f, Blue));
	ASSERT_TRUE(Graph.InsertAt(4, 40.0f, Blue));
	Graph.Scale(4);
	Render(Graph);
	ASSERT_EQ(m_Graphics.m_vLines.size(), 2U);
	EXPECT_FLOAT_EQ(m_Graphics.m_vLines[0].m_Item.m_X0, 10.0f);
	EXPECT_FLOAT_EQ(m_Graphics.m_vLines[0].m_Item.m_X1, 40.0f);
	EXPECT_FLOAT_EQ(m_Graphics.m_vLines[1].m_Item.m_X0, 100.0f);
	EXPECT_FLOAT_EQ(m_Graphics.m_vLines[1].m_Item.m_X1, 130.0f);
	EXPECT_EQ(m_Graphics.m_vLines[0].m_aColors, (std::array<ColorRGBA, 2>{Red, Red}));
	EXPECT_EQ(m_Graphics.m_vLines[1].m_aColors, (std::array<ColorRGBA, 2>{Blue, Blue}));
}

TEST_F(GraphRendering, InfiniteSampleCanRecoverAtTheNextInitialization)
{
	CGraph Graph(8, 0, false);
	Graph.Init(0.0f, 100.0f);
	ASSERT_TRUE(Graph.InsertAt(0, 0.0f));
	ASSERT_TRUE(Graph.InsertAt(1, std::numeric_limits<float>::infinity()));
	Graph.Scale(1);
	Render(Graph);
	EXPECT_TRUE(m_Graphics.m_vLines.empty());
	Graph.Init(0.0f, 100.0f);
	ASSERT_TRUE(Graph.InsertAt(2, 0.0f));
	ASSERT_TRUE(Graph.InsertAt(3, 50.0f));
	Graph.Scale(1);
	Render(Graph);
	EXPECT_EQ(m_Graphics.m_vLines.size(), 1U);
}

TEST_F(GraphRendering, NonPositiveTimeRangeSkipsTheCurve)
{
	for(const int WantedTime : {0, -1})
	{
		SCOPED_TRACE(WantedTime);
		CGraph Graph(8, 0, false);
		Graph.Init(0.0f, 100.0f);
		ASSERT_TRUE(Graph.InsertAt(0, 0.0f));
		ASSERT_TRUE(Graph.InsertAt(1, 10.0f));
		Graph.Scale(WantedTime);
		Render(Graph);
		EXPECT_TRUE(m_Graphics.m_vLines.empty());
	}
}

TEST_F(GraphRendering, UnrepresentableProjectionCanRenderAgainWithAValidAxis)
{
	for(const float Max : {1.0e-8f, std::numeric_limits<float>::min()})
	{
		SCOPED_TRACE(Max);
		m_Graphics.m_vLines.clear();
		CGraph Graph(8, 0, false);
		Graph.Init(0.0f, 100.0f);
		ASSERT_TRUE(Graph.InsertAt(0, 0.0f));
		ASSERT_TRUE(Graph.InsertAt(1, 1.0f));
		Graph.Scale(1);
		Graph.SetMax(Max);
		Render(Graph);
		EXPECT_TRUE(m_Graphics.m_vLines.empty());
		Graph.SetMax(100.0f);
		Render(Graph);
		EXPECT_EQ(m_Graphics.m_vLines.size(), 1U);
	}
}

TEST_F(GraphRendering, EmptyGraphKeepsTheFrameWithoutSubmittingCurveLines)
{
	CGraph Graph(8, 0, false);
	Graph.Init(0.0f, 100.0f);
	Graph.Scale(10);
	Render(Graph);
	EXPECT_TRUE(m_Graphics.m_vLines.empty());
	EXPECT_EQ(m_Graphics.m_GuideLines, 3);
}

TEST_F(GraphRendering, SingleSampleDoesNotCreateASegment)
{
	CGraph Graph(8, 0, false);
	Graph.Init(0.0f, 100.0f);
	ASSERT_TRUE(Graph.InsertAt(0, 10.0f));
	Graph.Scale(10);
	Render(Graph);
	EXPECT_TRUE(m_Graphics.m_vLines.empty());
	EXPECT_EQ(m_Graphics.m_GuideLines, 3);
}
