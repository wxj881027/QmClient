// 密码显隐图标的形变参数直接验证生产接口：帧索引、目标尺寸与 UV 内缩。
#include <game/client/QmUi/QmPasswordIcon.h>

#include <gtest/gtest.h>

TEST(QmPasswordIcon, StyleFallsBackToFadeForUnknownValues)
{
	EXPECT_EQ(qm_password_icon::ResolveStyle(0), qm_password_icon::STYLE_FADE);
	EXPECT_EQ(qm_password_icon::ResolveStyle(1), qm_password_icon::STYLE_MORPH);
	// 越界值按淡化处理：形变依赖成对烘焙资产，宁可退回旧动画也不要画错。
	EXPECT_EQ(qm_password_icon::ResolveStyle(-3), qm_password_icon::STYLE_FADE);
	EXPECT_EQ(qm_password_icon::ResolveStyle(7), qm_password_icon::STYLE_FADE);
}

TEST(QmPasswordIcon, ResolveFramePixelsRoundsAndClamps)
{
	// 纹理与绘制尺寸必须严格 1:1：取整后 quad 落在整数坐标，采样才不会落到半像素。
	EXPECT_EQ(qm_password_icon::ResolveFramePixels(14.4f), 14);
	EXPECT_EQ(qm_password_icon::ResolveFramePixels(13.6f), 14);
	EXPECT_EQ(qm_password_icon::ResolveFramePixels(14.5f), 15);
	EXPECT_EQ(qm_password_icon::ResolveFramePixels(20.4f), 20);
	// 极小/极大字号夹到可重采样的范围，避免生成 0 宽纹理或超大纹理。
	EXPECT_EQ(qm_password_icon::ResolveFramePixels(0.0f), qm_password_icon::FRAME_MIN_PIXELS);
	EXPECT_EQ(qm_password_icon::ResolveFramePixels(4.0f), qm_password_icon::FRAME_MIN_PIXELS);
	EXPECT_EQ(qm_password_icon::ResolveFramePixels(200.0f), qm_password_icon::FRAME_MAX_PIXELS);
}

TEST(QmPasswordIcon, FrameIndexSaturatesAndCoversBothEnds)
{
	EXPECT_EQ(qm_password_icon::FrameIndex(-1.0f), 0);
	EXPECT_EQ(qm_password_icon::FrameIndex(0.0f), 0);
	EXPECT_EQ(qm_password_icon::FrameIndex(1.0f), qm_password_icon::FRAME_COUNT - 1);
	EXPECT_EQ(qm_password_icon::FrameIndex(2.0f), qm_password_icon::FRAME_COUNT - 1);

	int Previous = -1;
	for(int Step = 0; Step <= 100; ++Step)
	{
		const int Index = qm_password_icon::FrameIndex(Step / 100.0f);
		ASSERT_GE(Index, 0);
		ASSERT_LT(Index, qm_password_icon::FRAME_COUNT);
		ASSERT_GE(Index, Previous) << "帧索引必须随进度单调不减，step=" << Step;
		Previous = Index;
	}
}

TEST(QmPasswordIcon, FrameUvKeepsHalfTexelInsetAndStaysOrdered)
{
	// 纹理宽度 = FRAME_COUNT * Pixels，重采样后仍然按该尺寸算半纹素内缩。
	for(const int Pixels : {14, 24, 40, 96})
	{
		for(int Index = 0; Index < qm_password_icon::FRAME_COUNT; ++Index)
		{
			const float U0 = qm_password_icon::FrameU0(Index, Pixels);
			const float U1 = qm_password_icon::FrameU1(Index, Pixels);
			ASSERT_GT(U0, 0.0f) << Pixels << "px frame " << Index;
			ASSERT_LT(U0, U1) << Pixels << "px frame " << Index;
			ASSERT_LT(U1, 1.0f) << Pixels << "px frame " << Index;
			ASSERT_NEAR(U0, (static_cast<float>(Index) + 0.5f / static_cast<float>(Pixels)) / qm_password_icon::FRAME_COUNT, 1e-6f) << Pixels << "px";
			ASSERT_NEAR(U1, (static_cast<float>(Index + 1) - 0.5f / static_cast<float>(Pixels)) / qm_password_icon::FRAME_COUNT, 1e-6f) << Pixels << "px";
			if(Index + 1 < qm_password_icon::FRAME_COUNT)
				ASSERT_LT(U1, qm_password_icon::FrameU0(Index + 1, Pixels));
		}
	}
}

TEST(QmPasswordIcon, FrameUvSplitsFillAndOutlineHalves)
{
	// 纹理上半是填充、下半是描边，两半各按自己那一行算半纹素内缩且不得重叠；
	// 重叠会让描边采样到填充（或反过来），图标边缘出现亮线。
	for(const int Pixels : {14, 24, 40, 96})
	{
		const float FillV0 = qm_password_icon::FrameV0(false, Pixels);
		const float FillV1 = qm_password_icon::FrameV1(false, Pixels);
		const float OutlineV0 = qm_password_icon::FrameV0(true, Pixels);
		const float OutlineV1 = qm_password_icon::FrameV1(true, Pixels);
		ASSERT_GT(FillV0, 0.0f) << Pixels << "px";
		ASSERT_LT(FillV0, FillV1) << Pixels << "px";
		ASSERT_LT(FillV1, OutlineV0) << Pixels << "px";
		ASSERT_LT(OutlineV0, OutlineV1) << Pixels << "px";
		ASSERT_LT(OutlineV1, 1.0f) << Pixels << "px";
		// 两半等宽：同一帧在填充与描边两次绘制里覆盖完全相同的像素行高。
		ASSERT_NEAR(OutlineV1 - OutlineV0, FillV1 - FillV0, 1e-6f) << Pixels << "px";
	}
}

TEST(QmPasswordIcon, FrameSideScalesWithCanvasUnits)
{
	// 画布 1152 单位对字体 1024 upm：帧的绘制边长是字号的 1.125 倍，
	// 才能让帧内墨迹与同字号字形墨迹等高。
	EXPECT_FLOAT_EQ(qm_password_icon::FrameSide(1024.0f), qm_password_icon::CANVAS_UNITS);
	EXPECT_FLOAT_EQ(qm_password_icon::FrameSide(0.0f), 0.0f);
	EXPECT_GT(qm_password_icon::FrameSide(20.0f), 20.0f);
}

TEST(QmPasswordIcon, PhysicalScaleInvertsUiPixelSize)
{
	// CUi::PixelSize() 是 UI 单位占用的物理像素比例；纹理边长要的是它的倒数。
	// 600 高的虚拟分辨率映射到 1266 高的窗口时，1 UI 单位 = 2.11 物理像素。
	EXPECT_FLOAT_EQ(qm_password_icon::ResolvePhysicalScale(600.0f / 1266.0f), 1266.0f / 600.0f);
	EXPECT_FLOAT_EQ(qm_password_icon::ResolvePhysicalScale(1.0f), 1.0f);
	// 异常输入不能把纹理尺寸带偏：退回 1 倍，宁可糊一点也不要把图放大十倍。
	EXPECT_FLOAT_EQ(qm_password_icon::ResolvePhysicalScale(0.0f), 1.0f);
	EXPECT_FLOAT_EQ(qm_password_icon::ResolvePhysicalScale(-2.0f), 1.0f);
	EXPECT_FLOAT_EQ(qm_password_icon::ResolvePhysicalScale(1.0f / 1000.0f), 1.0f);
}

TEST(QmPasswordIcon, FrameTexturePixelsFollowPhysicsNotUiUnits)
{
	// 14.4 UI 单位的图标在 2.11 倍物理缩放下约占 30 个物理像素：纹理必须按 30 取，
	// 否则 GPU 会把 14 像素的纹理放大到 30 像素，笔画被双线性摊平（就是「发糊」）。
	const float Scale = qm_password_icon::ResolvePhysicalScale(600.0f / 1266.0f);
	EXPECT_EQ(qm_password_icon::ResolveFrameTexturePixels(14.4f, Scale), 30);
	EXPECT_EQ(qm_password_icon::ResolveFrameTexturePixels(14.4f, 1.0f), 14);
	// 量化到偶数：hover 的 1.0→1.1 弹簧只让纹理换一两次，而不是每帧换。
	EXPECT_EQ(qm_password_icon::ResolveFrameTexturePixels(14.0f, 1.0f), 14);
	EXPECT_EQ(qm_password_icon::ResolveFrameTexturePixels(15.0f, 1.0f), 16);
	EXPECT_EQ(qm_password_icon::ResolveFrameTexturePixels(16.0f, 1.0f), 16);
	// 上限取源资产边长：宁可让 GPU 轻微放大，也不要把源图放大后再采样。
	EXPECT_LE(qm_password_icon::ResolveFrameTexturePixels(200.0f, 4.0f), qm_password_icon::FRAME_SOURCE_PIXELS);
}
