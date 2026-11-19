#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmTextScreenMapping : public qm_ui_test::CRealUiFixture
	{
	protected:
		STextContainerIndex m_Container;
		ITextRender &Text() { return *m_Ui.TextRender(); }
		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(!HasFatalFailure())
				Graphics().MapScreen(-50, 25, 910, 565);
		}
		void TearDown() override
		{
			Graphics().m_RecordLegacyText = false;
			if(m_Container.Valid())
				Text().DeleteTextContainer(m_Container);
			Graphics().m_TextBuffering = true;
			CRealUiFixture::TearDown();
		}
		bool Prepare(unsigned Flags = TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT, bool Buffered = true)
		{
			Graphics().m_TextBuffering = Buffered;
			Text().SetRenderFlags(Flags);
			CTextCursor Cursor;
			Cursor.SetPosition({0, 0});
			Cursor.m_FontSize = 16;
			Cursor.m_TrackLineRanges = true;
			const bool Created = Text().CreateTextContainer(m_Container, &Cursor, "ABCD");
			Text().SetRenderFlags(0);
			return Created;
		}
		void ExpectOriginalScreen()
		{
			float X0, Y0, X1, Y1;
			Graphics().GetScreen(&X0, &Y0, &X1, &Y1);
			EXPECT_EQ(X0, -50);
			EXPECT_EQ(Y0, 25);
			EXPECT_EQ(X1, 910);
			EXPECT_EQ(Y1, 565);
		}
		void Draw(float X = 31.25f, float Y = -42.25f)
		{
			Text().RenderTextContainer(m_Container, {1, 1, 1, 1}, {0, 0, 0, 0.3f}, X, Y);
		}
		void Sweep()
		{
			STextSweepParams Params;
			Params.m_Progress = 0.5f;
			Params.m_HalfWidth = 200;
			Text().RenderTextContainerSweep(m_Container, Params, 31.25f, -42.25f);
		}
	};
}

TEST_F(QmTextScreenMapping, OffsetSuccessKeepsPixelAlignmentAndRestoresTheIncomingScreen)
{
	ASSERT_TRUE(Prepare(0));
	const auto Before = Text().GetBoundingBoxTextContainer(m_Container);
	const auto Draws = Graphics().m_Draws;
	Draw();
	ExpectOriginalScreen();
	const auto After = Text().GetBoundingBoxTextContainer(m_Container);
	EXPECT_EQ(After.m_X, 31.5f);
	EXPECT_EQ(After.m_Y, -42.5f);
	EXPECT_EQ(After.m_W, Before.m_W);
	EXPECT_EQ(After.m_H, Before.m_H);
	EXPECT_EQ(Graphics().m_Draws, Draws + 1);
}

TEST_F(QmTextScreenMapping, OffsetBufferedDrawFailureRestoresScreenAndKeepsTheContainerForRetry)
{
	ASSERT_TRUE(Prepare());
	Graphics().m_FailTextDraw = true;
	EXPECT_THROW(Draw(), std::bad_alloc);
	ExpectOriginalScreen();
	ASSERT_TRUE(m_Container.Valid());
	EXPECT_NO_THROW(Draw(17, 23));
	ExpectOriginalScreen();
	const auto Box = Text().GetBoundingBoxTextContainer(m_Container);
	EXPECT_EQ(Box.m_X, 17);
	EXPECT_EQ(Box.m_Y, 23);
}

TEST_F(QmTextScreenMapping, OffsetUploadFailureRestoresScreenAndRetainsItsPartialBufferForRetry)
{
	ASSERT_TRUE(Prepare(TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | TEXT_RENDER_FLAG_NO_AUTOMATIC_QUAD_UPLOAD));
	const size_t Buffers = Graphics().m_Buffers.size();
	Graphics().m_FailBufferContainerCreate = true;
	EXPECT_THROW(Draw(), std::bad_alloc);
	ExpectOriginalScreen();
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	ASSERT_TRUE(m_Container.Valid());
	EXPECT_NO_THROW(Draw());
	ExpectOriginalScreen();
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
}

TEST_F(QmTextScreenMapping, OffsetUnbufferedSubmissionFailureAlsoRestoresTheScreen)
{
	ASSERT_TRUE(Prepare(TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT, false));
	Graphics().m_RecordLegacyText = true;
	Graphics().m_FailLegacyDrawAfter = 1;
	EXPECT_THROW(Draw(), std::bad_alloc);
	ExpectOriginalScreen();
	EXPECT_EQ(Graphics().m_vLegacyDraws.size(), 1u);
	// 替身不模拟原生Begin/End状态；这里只验证投影收尾。
}

TEST_F(QmTextScreenMapping, SweepSuccessRestoresScreenWithoutMovingTheBaseTextBounds)
{
	ASSERT_TRUE(Prepare());
	const auto Before = Text().GetBoundingBoxTextContainer(m_Container);
	const auto Draws = Graphics().m_Draws;
	Sweep();
	ExpectOriginalScreen();
	const auto After = Text().GetBoundingBoxTextContainer(m_Container);
	EXPECT_EQ(After.m_X, Before.m_X);
	EXPECT_EQ(After.m_Y, Before.m_Y);
	EXPECT_EQ(After.m_W, Before.m_W);
	EXPECT_EQ(After.m_H, Before.m_H);
	EXPECT_EQ(Graphics().m_Draws, Draws + 1);
}

TEST_F(QmTextScreenMapping, SweepBufferAllocationFailureRestoresScreenAndCanRetry)
{
	ASSERT_TRUE(Prepare());
	Graphics().m_FailBufferObjectCreate = true;
	EXPECT_THROW(Sweep(), std::bad_alloc);
	ExpectOriginalScreen();
	EXPECT_NO_THROW(Sweep());
	ExpectOriginalScreen();
}

TEST_F(QmTextScreenMapping, SweepContainerAllocationFailureRestoresScreenAndRetainsItsOwnedBuffer)
{
	ASSERT_TRUE(Prepare());
	const size_t Buffers = Graphics().m_Buffers.size();
	Graphics().m_FailBufferContainerCreate = true;
	EXPECT_THROW(Sweep(), std::bad_alloc);
	ExpectOriginalScreen();
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_NO_THROW(Sweep());
	ExpectOriginalScreen();
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
}

TEST_F(QmTextScreenMapping, SweepBufferedDrawFailureRestoresScreenAndCanRetry)
{
	ASSERT_TRUE(Prepare());
	Graphics().m_FailTextDraw = true;
	EXPECT_THROW(Sweep(), std::bad_alloc);
	ExpectOriginalScreen();
	EXPECT_NO_THROW(Sweep());
	ExpectOriginalScreen();
}

TEST_F(QmTextScreenMapping, SweepUnbufferedDeviceErrorRestoresScreenAndStillPropagates)
{
	ASSERT_TRUE(Prepare(TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT, false));
	// 此设备替身拒绝未支持的非缓冲绘制接口，用其异常检查原错误不被吞掉。
	EXPECT_THROW(Sweep(), std::logic_error);
	ExpectOriginalScreen();
}
