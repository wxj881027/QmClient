#include <game/client/components/qmclient/pose_capture.h>

#include <gtest/gtest.h>

TEST(QmPoseCapture, ScaleOutsideOneToFourFallsBackToOne)
{
	// 命令来自控制台与界面：越界值不该让渲染路径失效，归一成 1 倍（窗口分辨率）。
	EXPECT_EQ(qm_pose::NormalizeCaptureScale(0), 1);
	EXPECT_EQ(qm_pose::NormalizeCaptureScale(-3), 1);
	EXPECT_EQ(qm_pose::NormalizeCaptureScale(5), 1);
	EXPECT_EQ(qm_pose::NormalizeCaptureScale(100), 1);
	for(int Scale = 1; Scale <= 4; ++Scale)
		EXPECT_EQ(qm_pose::NormalizeCaptureScale(Scale), Scale);
}

TEST(QmPoseCapture, OutputSizeIsTheWindowSizeMultipliedByTheScale)
{
	// 超采样是「整幅画面按目标尺寸重新光栅化」，所以输出像素 = 窗口渲染尺寸 × N。
	for(int Scale = 1; Scale <= 4; ++Scale)
	{
		const qm_pose::SCaptureSize Size = qm_pose::ComputeCaptureSize(1280, 720, Scale);
		EXPECT_EQ(Size.m_Width, 1280 * Scale);
		EXPECT_EQ(Size.m_Height, 720 * Scale);
		EXPECT_EQ(Size.m_Scale, Scale);
		EXPECT_FALSE(Size.m_Clamped);
	}
}

TEST(QmPoseCapture, RequestedScaleIsNormalizedBeforeSizing)
{
	// 非法倍数按 1 倍出图，尺寸也跟着回到窗口大小，并如实报告自己降过级。
	const qm_pose::SCaptureSize Low = qm_pose::ComputeCaptureSize(1280, 720, 0);
	EXPECT_EQ(Low.m_Scale, 1);
	EXPECT_EQ(Low.m_Width, 1280);
	EXPECT_EQ(Low.m_Height, 720);
	EXPECT_TRUE(Low.m_Clamped);

	const qm_pose::SCaptureSize High = qm_pose::ComputeCaptureSize(1280, 720, 9);
	EXPECT_EQ(High.m_Scale, 1);
	EXPECT_EQ(High.m_Width, 1280);
	EXPECT_TRUE(High.m_Clamped);
}

TEST(QmPoseCapture, OversizedTargetIsClampedToFewerSamples)
{
	// 4K 窗口 4 倍会超过后端纹理上限：逐级下调到能渲染的倍数，而不是创建必然失败的目标。
	const qm_pose::SCaptureSize Size = qm_pose::ComputeCaptureSize(4096, 2160, 4, 8192);
	EXPECT_EQ(Size.m_Scale, 2);
	EXPECT_EQ(Size.m_Width, 8192);
	EXPECT_EQ(Size.m_Height, 4320);
	EXPECT_TRUE(Size.m_Clamped);

	// 宽高任一方向超限都要下调：竖屏窗口按高度受限。
	const qm_pose::SCaptureSize Portrait = qm_pose::ComputeCaptureSize(1000, 5000, 4, 16384);
	EXPECT_EQ(Portrait.m_Scale, 3);
	EXPECT_EQ(Portrait.m_Width, 3000);
	EXPECT_EQ(Portrait.m_Height, 15000);
}

TEST(QmPoseCapture, TileViewSplitsTheFullViewIntoEqualCells)
{
	// 2×2 分块：每块只覆盖整幅画面的四分之一，中心落在各自格心，缩放加倍。
	const qm_pose::SView TopLeft = qm_pose::TileView(vec2(0.0f, 0.0f), 1.0f, 1280.0f, 720.0f, 2, 0, 0);
	EXPECT_FLOAT_EQ(TopLeft.m_Center.x, -320.0f);
	EXPECT_FLOAT_EQ(TopLeft.m_Center.y, -180.0f);
	EXPECT_FLOAT_EQ(TopLeft.m_Zoom, 0.5f);

	const qm_pose::SView BottomRight = qm_pose::TileView(vec2(0.0f, 0.0f), 1.0f, 1280.0f, 720.0f, 2, 1, 1);
	EXPECT_FLOAT_EQ(BottomRight.m_Center.x, 320.0f);
	EXPECT_FLOAT_EQ(BottomRight.m_Center.y, 180.0f);

	// 两块相邻：左块右边界正好是右块左边界，拼起来没有缝隙也没有重叠。
	const float TileWorldWidth = 1280.0f * 1.0f / 2.0f;
	EXPECT_FLOAT_EQ(TopLeft.m_Center.x + TileWorldWidth * 0.5f, BottomRight.m_Center.x - TileWorldWidth * 0.5f);
}

TEST(QmPoseCapture, TileViewKeepsTheFullViewAtScaleOne)
{
	// 1 倍没有分块的意义：原样返回整幅取景，调用方走普通超采样/窗口截图。
	const qm_pose::SView View = qm_pose::TileView(vec2(100.0f, -50.0f), 0.75f, 1280.0f, 720.0f, 1, 0, 0);
	EXPECT_FLOAT_EQ(View.m_Center.x, 100.0f);
	EXPECT_FLOAT_EQ(View.m_Center.y, -50.0f);
	EXPECT_FLOAT_EQ(View.m_Zoom, 0.75f);
}

TEST(QmPoseCapture, TileViewCoversTheWholeViewAtHigherScales)
{
	// 4×4 的四个角拼起来必须正好覆盖整幅画面：左上角格的左边界 = -半宽，右下角格的右边界 = +半宽。
	for(int Scale = 2; Scale <= qm_pose::MAX_CAPTURE_SCALE; ++Scale)
	{
		const float WorldWidth = 1280.0f * 1.0f;
		const float WorldHeight = 720.0f * 1.0f;
		const qm_pose::SView First = qm_pose::TileView(vec2(0.0f, 0.0f), 1.0f, 1280.0f, 720.0f, Scale, 0, 0);
		const qm_pose::SView Last = qm_pose::TileView(vec2(0.0f, 0.0f), 1.0f, 1280.0f, 720.0f, Scale, Scale - 1, Scale - 1);
		const float TileWidth = WorldWidth / Scale;
		const float TileHeight = WorldHeight / Scale;
		EXPECT_NEAR(First.m_Center.x - TileWidth * 0.5f, -WorldWidth * 0.5f, 0.01f);
		EXPECT_NEAR(First.m_Center.y - TileHeight * 0.5f, -WorldHeight * 0.5f, 0.01f);
		EXPECT_NEAR(Last.m_Center.x + TileWidth * 0.5f, WorldWidth * 0.5f, 0.01f);
		EXPECT_NEAR(Last.m_Center.y + TileHeight * 0.5f, WorldHeight * 0.5f, 0.01f);
	}
}

TEST(QmPoseCapture, PixelBudgetClampsSupersamplingBeforeItExhaustsMemory)
{
	// 4K 窗口 4 倍 = 1.5 亿像素：颜色、深度、读回各 530 MB，编码还会拖死主线程。
	// 按 8K 预算降到 2 倍，并如实报告降过级。
	const qm_pose::SCaptureSize Size = qm_pose::ComputeCaptureSize(3840, 2160, 4, qm_pose::MAX_CAPTURE_DIMENSION, qm_pose::MAX_CAPTURE_PIXELS);
	EXPECT_EQ(Size.m_Scale, 2);
	EXPECT_EQ(Size.m_Width, 7680);
	EXPECT_EQ(Size.m_Height, 4320);
	EXPECT_TRUE(Size.m_Clamped);

	// 1080p 窗口 4 倍正好落在预算边界上，不该被降级。
	const qm_pose::SCaptureSize Boundary = qm_pose::ComputeCaptureSize(1920, 1080, 4, qm_pose::MAX_CAPTURE_DIMENSION, qm_pose::MAX_CAPTURE_PIXELS);
	EXPECT_EQ(Boundary.m_Scale, 4);
	EXPECT_FALSE(Boundary.m_Clamped);

	// 窗口本身就是 8K 时没有可降的空间：回到 1 倍（窗口截图），而不是硬造 8K 两倍的目标。
	const qm_pose::SCaptureSize Floor = qm_pose::ComputeCaptureSize(7680, 4320, 2, qm_pose::MAX_CAPTURE_DIMENSION, qm_pose::MAX_CAPTURE_PIXELS);
	EXPECT_EQ(Floor.m_Scale, 1);
	EXPECT_TRUE(Floor.m_Clamped);
}

TEST(QmPoseCapture, WindowThatAlreadyExceedsTheLimitReportsNoCapture)
{
	// 窗口本身就超过上限时没有可用的超采样尺寸：调用方退回窗口截图，不能拿 0 尺寸去建目标。
	const qm_pose::SCaptureSize Size = qm_pose::ComputeCaptureSize(20000, 1000, 2, 16384);
	EXPECT_EQ(Size.m_Width, 0);
	EXPECT_EQ(Size.m_Height, 0);
	EXPECT_TRUE(Size.m_Clamped);
}

TEST(QmPoseCapture, InvalidWindowSizeReportsNoCapture)
{
	// 窗口尺寸还没算出来（最小化、启动首帧）或上限非法时不做超采样。
	const qm_pose::SCaptureSize ZeroWidth = qm_pose::ComputeCaptureSize(0, 720, 2);
	EXPECT_EQ(ZeroWidth.m_Width, 0);
	EXPECT_EQ(ZeroWidth.m_Height, 0);

	const qm_pose::SCaptureSize ZeroHeight = qm_pose::ComputeCaptureSize(1280, 0, 2);
	EXPECT_EQ(ZeroHeight.m_Width, 0);
	EXPECT_EQ(ZeroHeight.m_Height, 0);

	const qm_pose::SCaptureSize Negative = qm_pose::ComputeCaptureSize(-1280, -720, 2);
	EXPECT_EQ(Negative.m_Width, 0);

	const qm_pose::SCaptureSize NoLimit = qm_pose::ComputeCaptureSize(1280, 720, 2, 0);
	EXPECT_EQ(NoLimit.m_Width, 0);
	EXPECT_EQ(NoLimit.m_Height, 0);
}

// 分块几何的硬约束：每块必须严丝合缝地拼成整幅——既不能重叠（重叠处会被画两次，
// 半透明内容叠色），也不能留缝（缝里是未初始化的黑色）。整幅宽高除不尽时最容易出错。
TEST(QmPoseCapture, TilesTileTheWholeImageWithoutGapsOrOverlap)
{
	struct SCase
	{
		int m_SumWidth;
		int m_SumHeight;
		int m_Scale;
	};
	// 整除、除不尽、以及 1 倍（只有一块）三种情况都要成立。
	const SCase aCases[] = {
		{3200, 1800, 2},
		{3840, 2160, 3},
		{1000, 777, 3},
		{1600, 900, 1},
		{7, 5, 4},
	};
	for(const SCase &Case : aCases)
	{
		const int N = qm_pose::NormalizeCaptureScale(Case.m_Scale);
		for(int Y = 0; Y < N; ++Y)
		{
			for(int X = 0; X < N; ++X)
			{
				const qm_pose::STile Tile = qm_pose::ComputeTile(Case.m_SumWidth, Case.m_SumHeight, Case.m_Scale, X, Y);
				EXPECT_GT(Tile.m_Width, 0) << "sum=" << Case.m_SumWidth << "x" << Case.m_SumHeight << " tile=" << X << "," << Y;
				EXPECT_GT(Tile.m_Height, 0);
				// 相邻块的边界必须相接：本块右边界 == 右邻块的左边界。
				if(X + 1 < N)
				{
					const qm_pose::STile Right = qm_pose::ComputeTile(Case.m_SumWidth, Case.m_SumHeight, Case.m_Scale, X + 1, Y);
					EXPECT_EQ(Tile.m_OriginX + Tile.m_Width, Right.m_OriginX);
				}
				if(Y + 1 < N)
				{
					const qm_pose::STile Below = qm_pose::ComputeTile(Case.m_SumWidth, Case.m_SumHeight, Case.m_Scale, X, Y + 1);
					EXPECT_EQ(Tile.m_OriginY + Tile.m_Height, Below.m_OriginY);
				}
				// 最后一块必须正好抵到整幅边界，否则拼图右下角会缺一条。
				if(X == N - 1)
					EXPECT_EQ(Tile.m_OriginX + Tile.m_Width, Case.m_SumWidth);
				if(Y == N - 1)
					EXPECT_EQ(Tile.m_OriginY + Tile.m_Height, Case.m_SumHeight);
			}
		}
	}
}

// 锐化的三条硬约束：默认（0）绝不改动像素、透明像素不被染色、
// 边缘对比必须真的被抬高。第一条关系到"关闭时成片逐像素不变"的承诺。
TEST(QmPoseCapture, SharpenLeavesPixelsUntouchedWhenDisabled)
{
	// 一张 3x3 的小图，中间亮、四周暗，正好有可锐化的边缘。
	std::vector<uint8_t> Pixels = {
		10,
		10,
		10,
		255,
		10,
		10,
		10,
		255,
		10,
		10,
		10,
		255,
		10,
		10,
		10,
		255,
		200,
		200,
		200,
		255,
		10,
		10,
		10,
		255,
		10,
		10,
		10,
		255,
		10,
		10,
		10,
		255,
		10,
		10,
		10,
		255,
	};
	const std::vector<uint8_t> Before = Pixels;
	qm_pose::ApplyUnsharpMask(Pixels.data(), 3, 3, 0);
	EXPECT_EQ(Pixels, Before);
}

TEST(QmPoseCapture, SharpenDoesNotTintFullyTransparentPixels)
{
	// 未绘制区域（alpha = 0）必须保持原样，否则缩放后会出现彩边。
	std::vector<uint8_t> Pixels(3 * 3 * 4, 0);
	// 中心是实心白像素，四周全透明。
	Pixels[(1 * 3 + 1) * 4 + 0] = 255;
	Pixels[(1 * 3 + 1) * 4 + 1] = 255;
	Pixels[(1 * 3 + 1) * 4 + 2] = 255;
	Pixels[(1 * 3 + 1) * 4 + 3] = 255;
	qm_pose::ApplyUnsharpMask(Pixels.data(), 3, 3, 100);
	for(int i = 0; i < 9; ++i)
	{
		if(i == 4)
			continue;
		EXPECT_EQ(Pixels[i * 4 + 0], 0) << "pixel " << i;
		EXPECT_EQ(Pixels[i * 4 + 1], 0) << "pixel " << i;
		EXPECT_EQ(Pixels[i * 4 + 2], 0) << "pixel " << i;
		EXPECT_EQ(Pixels[i * 4 + 3], 0) << "pixel " << i;
	}
}

TEST(QmPoseCapture, SharpenSteepensEdgesWithoutTouchingFlatAreas)
{
	std::vector<uint8_t> Pixels = {
		10,
		10,
		10,
		255,
		10,
		10,
		10,
		255,
		10,
		10,
		10,
		255,
		10,
		10,
		10,
		255,
		200,
		200,
		200,
		255,
		10,
		10,
		10,
		255,
		10,
		10,
		10,
		255,
		10,
		10,
		10,
		255,
		10,
		10,
		10,
		255,
	};
	const int BeforeCenter = Pixels[(1 * 3 + 1) * 4];
	qm_pose::ApplyUnsharpMask(Pixels.data(), 3, 3, 100);
	// 亮点更亮：边缘对比被抬高。
	EXPECT_GT(Pixels[(1 * 3 + 1) * 4], BeforeCenter);
	// 四角与亮点不相邻的平坦像素不应被改变（邻域全同值时拉普拉斯为 0）。
	EXPECT_EQ(Pixels[(0 * 3 + 0) * 4], 10);
	EXPECT_EQ(Pixels[(2 * 3 + 2) * 4], 10);
}

// 缓变渐变（天空、地面底色）必须整片跳过：放大它会变成一层层等值线。
// 这是实机在 40% 强度下看到天空分层后补的回归。
TEST(QmPoseCapture, SharpenSkipsSmoothGradients)
{
	constexpr int W = 9;
	constexpr int H = 9;
	std::vector<uint8_t> Pixels((size_t)W * H * 4, 0);
	// 横向每像素 +1 的缓变渐变，alpha 全不透明。
	for(int Y = 0; Y < H; ++Y)
		for(int X = 0; X < W; ++X)
		{
			const size_t Index = ((size_t)Y * W + X) * 4;
			Pixels[Index + 0] = (uint8_t)(100 + X);
			Pixels[Index + 1] = (uint8_t)(120 + X);
			Pixels[Index + 2] = (uint8_t)(150 + X);
			Pixels[Index + 3] = 255;
		}
	const std::vector<uint8_t> Before = Pixels;
	qm_pose::ApplyUnsharpMask(Pixels.data(), W, H, 100);
	EXPECT_EQ(Pixels, Before);
}

TEST(QmPoseCapture, TileOutsideTheGridIsRejected)
{
	const qm_pose::STile OutOfRange = qm_pose::ComputeTile(1600, 900, 2, 2, 0);
	EXPECT_EQ(OutOfRange.m_Width, 0);
	EXPECT_EQ(OutOfRange.m_Height, 0);

	const qm_pose::STile Negative = qm_pose::ComputeTile(1600, 900, 2, -1, 0);
	EXPECT_EQ(Negative.m_Width, 0);

	const qm_pose::STile NoImage = qm_pose::ComputeTile(0, 900, 2, 0, 0);
	EXPECT_EQ(NoImage.m_Width, 0);
}

TEST(QmPoseCapture, DegradedScaleStopsAtOneAndStillReportsIt)
{
	// 上限只够放窗口本身时，实际倍数停在 1：调用方据此放弃离屏路径，按窗口分辨率出图。
	const qm_pose::SCaptureSize Fits = qm_pose::ComputeCaptureSize(1000, 500, 3, 1000);
	EXPECT_EQ(Fits.m_Scale, 1);
	EXPECT_EQ(Fits.m_Width, 1000);
	EXPECT_EQ(Fits.m_Height, 500);
	EXPECT_TRUE(Fits.m_Clamped);
}

TEST(QmPoseCapture, SupersampledRequestIsDeferredInsteadOfSilentlyDowngraded)
{
	// 回归：皮肤闸门若直到渲染后才首次放行，那一帧没开过离屏帧。旧实现直接抓窗口
	// 帧缓冲，4 倍请求于是静默变成 1280x720（实测成片尺寸）。这里要求延到下一帧。
	EXPECT_EQ(qm_pose::DecideCaptureSubmit(4, false, false, true), qm_pose::ECaptureSubmit::DEFER_TO_NEXT_FRAME);
	EXPECT_EQ(qm_pose::DecideCaptureSubmit(2, false, false, true), qm_pose::ECaptureSubmit::DEFER_TO_NEXT_FRAME);

	// 1 倍本来就走窗口截图，不存在降级，直接拍。
	EXPECT_EQ(qm_pose::DecideCaptureSubmit(1, false, false, true), qm_pose::ECaptureSubmit::SUBMIT_WINDOW);
}

TEST(QmPoseCapture, ActiveOffscreenFrameIsAlwaysSubmittedInTheSameFrame)
{
	// 回归：离屏帧不允许跨帧存在。目标还活动时，无论倍数与是否「本帧尝试过」，
	// 都必须先读回收口——带着活动渲染目标 Swap 在 Vulkan 上会卡死图形线程。
	EXPECT_EQ(qm_pose::DecideCaptureSubmit(4, true, true, true), qm_pose::ECaptureSubmit::SUBMIT_OFFSCREEN);
	EXPECT_EQ(qm_pose::DecideCaptureSubmit(2, false, true, true), qm_pose::ECaptureSubmit::SUBMIT_OFFSCREEN);

	// 皮肤未就绪时也不能把离屏帧留在活动状态：调用方据此丢弃收口，而不是继续等。
	EXPECT_EQ(qm_pose::DecideCaptureSubmit(4, true, true, false), qm_pose::ECaptureSubmit::WAIT_FOR_SKINS);
}

TEST(QmPoseCapture, OffscreenFailureFallsBackToTheWindowFramebuffer)
{
	// 后端确实开不了离屏目标（Begin 返回 false）时不能无限延后：本帧尝试过、
	// 但目标没起来，就按窗口分辨率拍，至少保证这次渲染有产物。
	EXPECT_EQ(qm_pose::DecideCaptureSubmit(4, true, false, true), qm_pose::ECaptureSubmit::SUBMIT_WINDOW);
	EXPECT_EQ(qm_pose::DecideCaptureSubmit(3, true, false, true), qm_pose::ECaptureSubmit::SUBMIT_WINDOW);
}
