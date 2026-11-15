#include <engine/client/rounded_rect_geometry.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/ui.h>

#include <gtest/gtest.h>

#include <cmath>

TEST(QmRoundedSurface, BorderMeshStaysInsideTinyAndNarrowSurfaces)
{
	for(const vec2 Size : {vec2(1, 1), vec2(2, 40), vec2(160, 24)})
	{
		SCOPED_TRACE(Size.x);
		SCOPED_TRACE(Size.y);
		SRoundedSurfaceParams Params;
		Params.m_Radius = 100;
		Params.m_BorderWidth = 100;
		const auto Plan = ResolveRoundedSurfacePlan({20, 30, Size.x, Size.y}, Params, false);
		const auto Mesh = ResolveRoundedSurfaceBorderMesh(Plan, IGraphics::CORNER_ALL);
		for(const auto &Quad : Mesh)
		{
			for(float X : {Quad.m_X0, Quad.m_X1, Quad.m_X2, Quad.m_X3})
			{
				EXPECT_GE(X, Plan.m_Rect.x);
				EXPECT_LE(X, Plan.m_Rect.x + Plan.m_Rect.w);
			}
			for(float Y : {Quad.m_Y0, Quad.m_Y1, Quad.m_Y2, Quad.m_Y3})
			{
				EXPECT_GE(Y, Plan.m_Rect.y);
				EXPECT_LE(Y, Plan.m_Rect.y + Plan.m_Rect.h);
			}
		}
	}
}

TEST(QmRoundedSurface, QuantizedGeometryClampsRadiusAndInvalidSizes)
{
	const SRoundedRectGeometry Geometry = ResolveRoundedRectGeometry(0.24f, 0.74f, 10.32f, 4.19f, 3.9f, 0.5f);
	EXPECT_NEAR(Geometry.m_X, 0.0f, 1e-6f);
	EXPECT_NEAR(Geometry.m_Y, 0.5f, 1e-6f);
	EXPECT_NEAR(Geometry.m_W, 10.5f, 1e-6f);
	EXPECT_NEAR(Geometry.m_H, 4.5f, 1e-6f);
	EXPECT_NEAR(Geometry.m_Rounding, 2.25f, 1e-6f);
	const SRoundedRectGeometry SmallGeometry = ResolveRoundedRectGeometry(0.24f, 0.24f, 0.51f, 0.51f, 0.4f, 0.5f);
	EXPECT_NEAR(SmallGeometry.m_W, 1.0f, 1e-6f);
	EXPECT_NEAR(SmallGeometry.m_H, 1.0f, 1e-6f);
	EXPECT_NEAR(SmallGeometry.m_Rounding, 0.5f, 1e-6f);
	const SRoundedRectGeometry InvalidGeometry = ResolveRoundedRectGeometry(0.5f, 0.5f, 0.0f, 4.0f, 3.0f, 0.5f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_X, 0.5f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_Y, 0.5f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_W, 0.0f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_H, 4.0f);
	EXPECT_FLOAT_EQ(InvalidGeometry.m_Rounding, 0.0f);
}

TEST(QmRoundedSurface, SurfacePlanQuantizesPhysicalPixels)
{
	const CUIRect Rect{0.2f, 0.2f, 20.0f, 10.0f};
	SRoundedSurfaceParams SdfParams;
	SdfParams.m_Radius = 8.0f;
	SdfParams.m_BorderWidth = 0.6f;
	SdfParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan Sdf = ResolveRoundedSurfacePlan(Rect, SdfParams, true);
	EXPECT_TRUE(Sdf.m_UseSdf);
	EXPECT_FLOAT_EQ(Sdf.m_Rect.x, 0.0f);
	EXPECT_FLOAT_EQ(Sdf.m_Rect.y, 0.0f);
	EXPECT_FLOAT_EQ(Sdf.m_Rect.w, 20.0f);
	EXPECT_FLOAT_EQ(Sdf.m_Rect.h, 10.0f);
	EXPECT_FLOAT_EQ(Sdf.m_Radius, 5.0f);
	EXPECT_FLOAT_EQ(Sdf.m_BorderWidth, 0.5f);
	EXPECT_FLOAT_EQ(Sdf.m_PixelSize, 0.5f);
	EXPECT_FLOAT_EQ(Sdf.m_CornerRadii.x, 5.0f);
	EXPECT_FLOAT_EQ(Sdf.m_CornerRadii.y, 5.0f);
	EXPECT_FLOAT_EQ(Sdf.m_CornerRadii.z, 5.0f);
	EXPECT_FLOAT_EQ(Sdf.m_CornerRadii.w, 5.0f);
	SRoundedSurfaceParams NonIntegerPixelParams;
	NonIntegerPixelParams.m_Radius = 3.9f;
	NonIntegerPixelParams.m_BorderWidth = 0.6f;
	NonIntegerPixelParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan NonIntegerPixelPlan = ResolveRoundedSurfacePlan(CUIRect{0.24f, 0.74f, 10.32f, 4.19f}, NonIntegerPixelParams, true);
	EXPECT_NEAR(NonIntegerPixelPlan.m_Rect.x, 0.0f, 1e-6f);
	EXPECT_NEAR(NonIntegerPixelPlan.m_Rect.y, 0.5f, 1e-6f);
	EXPECT_NEAR(NonIntegerPixelPlan.m_Rect.w, 10.5f, 1e-6f);
	EXPECT_NEAR(NonIntegerPixelPlan.m_Rect.h, 4.5f, 1e-6f);
	SRoundedSurfaceParams OnePhysicalPixelParams;
	OnePhysicalPixelParams.m_Radius = 0.4f;
	OnePhysicalPixelParams.m_BorderWidth = 0.4f;
	OnePhysicalPixelParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan OnePhysicalPixelPlan = ResolveRoundedSurfacePlan(CUIRect{0.24f, 0.24f, 0.51f, 0.51f}, OnePhysicalPixelParams, true);
	EXPECT_NEAR(OnePhysicalPixelPlan.m_Rect.w, 1.0f, 1e-6f);
	EXPECT_NEAR(OnePhysicalPixelPlan.m_Rect.h, 1.0f, 1e-6f);
	EXPECT_NEAR(OnePhysicalPixelPlan.m_Radius, 0.5f, 1e-6f);
	EXPECT_NEAR(OnePhysicalPixelPlan.m_BorderWidth, 0.5f, 1e-6f);
}

TEST(QmRoundedSurface, CornerShapeKeepsCapsuleAndSmallControlsOnTheArc)
{
	// 胶囊：半径已经贴到短边一半，没有生长空间，曲线强度必须回到 0，
	// 否则半圆端头会被压成「平直段 + 急弯」的圆角矩形，看起来更方。
	const SRoundedCornerShape Capsule = ResolveRoundedCornerShape(10.0f, 40.0f, 20.0f, 1.0f, ROUNDED_CORNER_CURVE_QUINTIC, 0.0f);
	EXPECT_FLOAT_EQ(Capsule.m_Radius, 10.0f);
	EXPECT_FLOAT_EQ(Capsule.m_Amount, 0.0f);
	// 列表行、按钮这类小控件：短边 25% 的生长上限已经低于原半径，同样退回圆弧。
	const SRoundedCornerShape Row = ResolveRoundedCornerShape(8.0f, 200.0f, 20.0f, 1.0f, ROUNDED_CORNER_CURVE_SUPERELLIPSE, 0.0f);
	EXPECT_FLOAT_EQ(Row.m_Radius, 8.0f);
	EXPECT_FLOAT_EQ(Row.m_Amount, 0.0f);
	// 生长上限只夹生长：本来就比上限大的半径不被削小。
	const SRoundedCornerShape Huge = ResolveRoundedCornerShape(9.0f, 100.0f, 20.0f, 1.0f, ROUNDED_CORNER_CURVE_CONIC, 0.0f);
	EXPECT_FLOAT_EQ(Huge.m_Radius, 9.0f);
	EXPECT_FLOAT_EQ(Huge.m_Amount, 0.0f);
}

TEST(QmRoundedSurface, CornerShapeExtendsLargeSurfacesAndClassicNeverGrows)
{
	// 大卡片：短边 300 时 25% 上限（37.5）比满强度目标（32.2）宽，按强度正常生长。
	const SRoundedCornerShape Card = ResolveRoundedCornerShape(14.0f, 400.0f, 300.0f, 1.0f, ROUNDED_CORNER_CURVE_QUINTIC, 0.0f);
	EXPECT_NEAR(Card.m_Radius, 14.0f * 2.3f, 1e-4f);
	EXPECT_NEAR(Card.m_Amount, 1.0f, 1e-4f);
	// 强度与实际生长始终一致：默认 40% 强度就是 1.52 倍半径。
	const SRoundedCornerShape DefaultStrength = ResolveRoundedCornerShape(14.0f, 400.0f, 300.0f, 0.4f, ROUNDED_CORNER_CURVE_QUINTIC, 0.0f);
	EXPECT_NEAR(DefaultStrength.m_Radius, 14.0f * 1.52f, 1e-4f);
	EXPECT_NEAR(DefaultStrength.m_Amount, 0.4f, 1e-4f);
	// 撞上生长上限时，曲线强度跟着降下来，不会一边被夹住一边又按满强度压弯。
	// 短边 100 的卡片：上限是短边 25% = 25，低于满强度目标 32.2。
	const SRoundedCornerShape Capped = ResolveRoundedCornerShape(14.0f, 400.0f, 100.0f, 1.0f, ROUNDED_CORNER_CURVE_QUINTIC, 0.0f);
	EXPECT_NEAR(Capped.m_Radius, 25.0f, 1e-4f);
	EXPECT_NEAR(Capped.m_Amount, (25.0f / 14.0f - 1.0f) / 1.3f, 1e-4f);
	// 原圆弧档是旧外观：无论强度多少都不生长、也不换曲线。
	const SRoundedCornerShape Classic = ResolveRoundedCornerShape(14.0f, 400.0f, 300.0f, 1.0f, ROUNDED_CORNER_CURVE_CLASSIC, 0.0f);
	EXPECT_FLOAT_EQ(Classic.m_Radius, 14.0f);
	EXPECT_FLOAT_EQ(Classic.m_Amount, 0.0f);
}

TEST(QmRoundedSurface, CornerCurveSamplesAgreeOnSeamAndOnlyBezierEndsAtZeroCurvature)
{
	// 所有档位共用同一个角盒：两端分别落在竖边和横边的切点上。
	for(const int Curve : {ROUNDED_CORNER_CURVE_CLASSIC, ROUNDED_CORNER_CURVE_SUPERELLIPSE, ROUNDED_CORNER_CURVE_QUINTIC, ROUNDED_CORNER_CURVE_CONIC})
	{
		SCOPED_TRACE(Curve);
		float StartX = 0.0f, StartY = 0.0f, EndX = 0.0f, EndY = 0.0f;
		RoundedCornerCurveDirection(Curve, 1.0f, 0, 16, StartX, StartY);
		RoundedCornerCurveDirection(Curve, 1.0f, 16, 16, EndX, EndY);
		EXPECT_NEAR(StartX, 1.0f, 1e-6f);
		EXPECT_NEAR(StartY, 0.0f, 1e-6f);
		EXPECT_NEAR(EndX, 0.0f, 1e-6f);
		EXPECT_NEAR(EndY, 1.0f, 1e-6f);
		// 强度 0 时四档都精确等于原来的圆弧采样表。
		float X = 0.0f, Y = 0.0f;
		RoundedCornerCurveDirection(Curve, 0.0f, 5, 16, X, Y);
		const float Angle = 5.0f / 16.0f * (float)pi * 0.5f;
		EXPECT_NEAR(X, std::cos(Angle), 1e-6f);
		EXPECT_NEAR(Y, std::sin(Angle), 1e-6f);
	}

	// 接缝处曲率：三段圆周曲率估计随采样变细应当收敛到真实值。
	// 超椭圆与五次贝塞尔收敛到 0（真连续曲率），圆锥贝塞尔停在 1/(2W²E) ≈ 0.35，
	// 这就是它只能算「更柔和」而不能算连续曲率的原因。
	const auto SeamCurvature = [](const int Curve, const float Amount, const int Count) {
		float Ax = 0.0f, Ay = 0.0f, Bx = 0.0f, By = 0.0f, Cx = 0.0f, Cy = 0.0f;
		RoundedCornerCurveDirection(Curve, Amount, 0, Count, Ax, Ay);
		RoundedCornerCurveDirection(Curve, Amount, 1, Count, Bx, By);
		RoundedCornerCurveDirection(Curve, Amount, 2, Count, Cx, Cy);
		const float Cross = std::fabs((Bx - Ax) * (Cy - By) - (By - Ay) * (Cx - Bx));
		const float La = std::sqrt((Bx - Ax) * (Bx - Ax) + (By - Ay) * (By - Ay));
		const float Lb = std::sqrt((Cx - Bx) * (Cx - Bx) + (Cy - By) * (Cy - By));
		const float Lc = std::sqrt((Cx - Ax) * (Cx - Ax) + (Cy - Ay) * (Cy - Ay));
		return 2.0f * Cross / (La * Lb * Lc);
	};
	EXPECT_NEAR(SeamCurvature(ROUNDED_CORNER_CURVE_CLASSIC, 0.0f, 128), 1.0f, 0.01f);
	EXPECT_LT(SeamCurvature(ROUNDED_CORNER_CURVE_QUINTIC, 1.0f, 128), 0.20f);
	EXPECT_LT(SeamCurvature(ROUNDED_CORNER_CURVE_SUPERELLIPSE, 1.0f, 128), 0.10f);
	EXPECT_GT(SeamCurvature(ROUNDED_CORNER_CURVE_CONIC, 1.0f, 128), 0.30f);
	// 变细不会让圆锥的接缝曲率归零：它没有随强度消失的机制。
	EXPECT_GT(SeamCurvature(ROUNDED_CORNER_CURVE_CONIC, 1.0f, 64), 0.30f);
}

TEST(QmRoundedSurface, CornerShapeSeamSoftensCapsuleWithoutGrowingRadius)
{
	// 胶囊的端帽永远保持精确正圆：半径不生长，曲线强度恒为 0。
	// 「丝滑胶囊」只改直边上的肩部（着色器里独立实现），不参与圆角曲线，所以这里
	// 无论接缝参数给多少，胶囊都回到纯半圆端头。
	for(const float SeamAmount : {0.0f, 0.3f, 1.0f})
	{
		SCOPED_TRACE(SeamAmount);
		const SRoundedCornerShape Capsule = ResolveRoundedCornerShape(10.0f, 40.0f, 20.0f, 1.0f, ROUNDED_CORNER_CURVE_QUINTIC, SeamAmount);
		EXPECT_FLOAT_EQ(Capsule.m_Radius, 10.0f);
		EXPECT_FLOAT_EQ(Capsule.m_Amount, 0.0f);
	}
	// 不是胶囊的控件不受接缝设置影响，强度仍只由生长决定。
	const SRoundedCornerShape Card = ResolveRoundedCornerShape(14.0f, 400.0f, 300.0f, 0.4f, ROUNDED_CORNER_CURVE_QUINTIC, 1.0f);
	EXPECT_NEAR(Card.m_Radius, 14.0f * 1.52f, 1e-4f);
	EXPECT_NEAR(Card.m_Amount, 0.4f, 1e-4f);
	// 原圆弧档不吃接缝软化：它就是旧外观。
	const SRoundedCornerShape Classic = ResolveRoundedCornerShape(10.0f, 40.0f, 20.0f, 1.0f, ROUNDED_CORNER_CURVE_CLASSIC, 1.0f);
	EXPECT_FLOAT_EQ(Classic.m_Amount, 0.0f);
	// 正圆（20x20、半径 10）没有直边，谈不上接缝，不能被压成方圆。
	const SRoundedCornerShape Circle = ResolveRoundedCornerShape(10.0f, 20.0f, 20.0f, 1.0f, ROUNDED_CORNER_CURVE_QUINTIC, 1.0f);
	EXPECT_FLOAT_EQ(Circle.m_Radius, 10.0f);
	EXPECT_FLOAT_EQ(Circle.m_Amount, 0.0f);
}

TEST(QmRoundedSurface, PartialCornersBordersAndFallbackRemainBounded)
{
	const CUIRect Rect{0.2f, 0.2f, 20.0f, 10.0f};
	const auto ExpectCornerRadii = [](const SRoundedSurfacePlan &Plan, const float Tl, const float Tr, const float Br, const float Bl) {
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.x, Tl);
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.y, Tr);
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.z, Br);
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.w, Bl);
	};

	SRoundedSurfaceParams PartialParams;
	PartialParams.m_Radius = 4.0f;
	PartialParams.m_BorderWidth = 1.0f;
	PartialParams.m_PixelSize = 0.5f;
	PartialParams.m_Corners = IGraphics::CORNER_R;
	const SRoundedSurfacePlan Partial = ResolveRoundedSurfacePlan(Rect, PartialParams, true);
	EXPECT_TRUE(Partial.m_UseSdf);
	ExpectCornerRadii(Partial, 0.0f, 4.0f, 4.0f, 0.0f);
	SRoundedSurfaceParams LeftParams;
	LeftParams.m_Radius = 4.0f;
	LeftParams.m_BorderWidth = 1.0f;
	LeftParams.m_PixelSize = 0.5f;
	LeftParams.m_Corners = IGraphics::CORNER_L;
	const SRoundedSurfacePlan Left = ResolveRoundedSurfacePlan(Rect, LeftParams, true);
	EXPECT_TRUE(Left.m_UseSdf);
	ExpectCornerRadii(Left, 4.0f, 0.0f, 0.0f, 4.0f);
	const auto ExpectMask = [&](const int Corners, const float Tl, const float Tr, const float Br, const float Bl) {
		SRoundedSurfaceParams Params;
		Params.m_Radius = 4.0f;
		Params.m_BorderWidth = 1.0f;
		Params.m_PixelSize = 0.5f;
		Params.m_Corners = Corners;
		const SRoundedSurfacePlan Plan = ResolveRoundedSurfacePlan(Rect, Params, true);
		EXPECT_TRUE(Plan.m_UseSdf);
		ExpectCornerRadii(Plan, Tl, Tr, Br, Bl);
	};
	ExpectMask(IGraphics::CORNER_T, 4.0f, 4.0f, 0.0f, 0.0f);
	ExpectMask(IGraphics::CORNER_B, 0.0f, 0.0f, 4.0f, 4.0f);
	ExpectMask(IGraphics::CORNER_TL, 4.0f, 0.0f, 0.0f, 0.0f);
	ExpectMask(IGraphics::CORNER_TR, 0.0f, 4.0f, 0.0f, 0.0f);
	ExpectMask(IGraphics::CORNER_BR, 0.0f, 0.0f, 4.0f, 0.0f);
	ExpectMask(IGraphics::CORNER_BL, 0.0f, 0.0f, 0.0f, 4.0f);
	// 约定：半径是形状的唯一来源。给了非零半径又传 CORNER_NONE 的调用点在语义上是
	// 「要圆但忘了给角掩码」，一律按四角全圆处理；真要方块应当把半径给 0。
	ExpectMask(IGraphics::CORNER_NONE, 4.0f, 4.0f, 4.0f, 4.0f);
	{
		SRoundedSurfaceParams SquareParams;
		SquareParams.m_Radius = 0.0f;
		SquareParams.m_Corners = IGraphics::CORNER_NONE;
		const SRoundedSurfacePlan Square = ResolveRoundedSurfacePlan(Rect, SquareParams, true);
		ExpectCornerRadii(Square, 0.0f, 0.0f, 0.0f, 0.0f);
	}
	SRoundedSurfaceParams WideBorderParams;
	WideBorderParams.m_Radius = 2.0f;
	WideBorderParams.m_BorderWidth = 4.0f;
	WideBorderParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan WideBorder = ResolveRoundedSurfacePlan(Rect, WideBorderParams, true);
	EXPECT_FLOAT_EQ(WideBorder.m_Radius, 2.0f);
	EXPECT_FLOAT_EQ(WideBorder.m_BorderWidth, 4.0f);
	SRoundedSurfaceParams SwallowedInteriorParams;
	SwallowedInteriorParams.m_Radius = 8.0f;
	SwallowedInteriorParams.m_BorderWidth = 9.0f;
	SwallowedInteriorParams.m_PixelSize = 0.5f;
	const SRoundedSurfacePlan SwallowedInterior = ResolveRoundedSurfacePlan(CUIRect{0.0f, 0.0f, 6.0f, 4.0f}, SwallowedInteriorParams, true);
	EXPECT_FLOAT_EQ(SwallowedInterior.m_Radius, 2.0f);
	EXPECT_FLOAT_EQ(SwallowedInterior.m_BorderWidth, 2.0f);
	SRoundedSurfaceParams UnsupportedParams;
	UnsupportedParams.m_Radius = 4.0f;
	UnsupportedParams.m_BorderWidth = 1.0f;
	UnsupportedParams.m_PixelSize = 0.0f;
	const SRoundedSurfacePlan Unsupported = ResolveRoundedSurfacePlan(Rect, UnsupportedParams, false);
	EXPECT_FALSE(Unsupported.m_UseSdf);
	EXPECT_FLOAT_EQ(Unsupported.m_PixelSize, 0.0001f);
}

TEST(QmRoundedSurface, AppleCurveMatchesMeasuredContinuousCorner)
{
	// 满强度就是 UIBezierPath(roundedRect:) 实测的三段三次贝塞尔：单侧足迹 1.528665 r、
	// 最贴角点 0.41225 r（对 CoreAnimation 的零像素误差反解）。这两个值同时钉住缩放和形状：
	// 只对上足迹、形状不对的近似（比如「圆弧 + 自造手柄」）会在这里被最贴角点检出来。
	const int Count = 400;
	float StartX = 0.0f, StartY = 0.0f, EndX = 0.0f, EndY = 0.0f;
	RoundedCornerCurveDirection(ROUNDED_CORNER_CURVE_APPLE, 1.0f, 0, Count, StartX, StartY);
	RoundedCornerCurveDirection(ROUNDED_CORNER_CURVE_APPLE, 1.0f, Count, Count, EndX, EndY);
	EXPECT_NEAR(StartX, 1.0f, 1e-5f);
	EXPECT_NEAR(StartY, -APPLE_CORNER_FOOTPRINT, 1e-5f);
	EXPECT_NEAR(EndX, -APPLE_CORNER_FOOTPRINT, 1e-5f);
	EXPECT_NEAR(EndY, 1.0f, 1e-5f);
	// 调用方坐标里角点在 (1, 1)：到曲线的最小距离就是最贴角点。曲线还必须沿两个轴单调，
	// 否则会自己折回去——分轴各算一个混合系数最容易犯这个错。
	float MinDistance = 1e9f;
	float PreviousX = 2.0f;
	float PreviousY = -1.0f;
	for(int i = 0; i <= Count; i++)
	{
		float X = 0.0f, Y = 0.0f;
		RoundedCornerCurveDirection(ROUNDED_CORNER_CURVE_APPLE, 1.0f, i, Count, X, Y);
		MinDistance = std::min(MinDistance, std::sqrt((X - 1.0f) * (X - 1.0f) + (Y - 1.0f) * (Y - 1.0f)));
		EXPECT_LE(X, PreviousX + 1e-5f);
		EXPECT_GE(Y, PreviousY - 1e-5f);
		PreviousX = X;
		PreviousY = Y;
	}
	EXPECT_NEAR(MinDistance, 0.41225f, 2e-4f);
	// 强度减半时足迹线性收窄，仍是同一条曲线族里的形状。
	float HalfX = 0.0f, HalfY = 0.0f;
	RoundedCornerCurveDirection(ROUNDED_CORNER_CURVE_APPLE, 0.5f, 0, Count, HalfX, HalfY);
	EXPECT_NEAR(HalfY, -APPLE_CORNER_FOOTPRINT * 0.5f, 1e-5f);
	// 0 = 原圆弧：切点回到 (1, 0)。
	float ZeroX = 0.0f, ZeroY = 0.0f;
	RoundedCornerCurveDirection(ROUNDED_CORNER_CURVE_APPLE, 0.0f, 0, Count, ZeroX, ZeroY);
	EXPECT_NEAR(ZeroX, 1.0f, 1e-6f);
	EXPECT_NEAR(ZeroY, 0.0f, 1e-6f);
}

TEST(QmRoundedSurface, AppleCornerShrinksOnTheShortSideInsteadOfStretching)
{
	// 足迹 1 + 0.528665·s 必须塞进短边半高：塞不下时只削强度（两个轴共用同一个标量），
	// 半径不缩、曲线不拉伸。否则上下两个角的肩部会互相穿过中线，端边挤出外凸的唇边。
	const SRoundedCornerShape Squeezed = ResolveRoundedCornerShape(8.0f, 400.0f, 20.0f, 1.0f, ROUNDED_CORNER_CURVE_APPLE, 0.0f);
	EXPECT_FLOAT_EQ(Squeezed.m_Radius, 8.0f);
	EXPECT_NEAR(Squeezed.m_Amount, (10.0f / 8.0f - 1.0f) / APPLE_CORNER_FOOTPRINT, 1e-5f);
	EXPECT_NEAR(8.0f * (1.0f + APPLE_CORNER_FOOTPRINT * Squeezed.m_Amount), 10.0f, 1e-4f);
	// 半径已经贴到短边一半：没有生长空间，强度归零（端头由端头构造单独负责）。
	const SRoundedCornerShape Capsule = ResolveRoundedCornerShape(10.0f, 40.0f, 20.0f, 1.0f, ROUNDED_CORNER_CURVE_APPLE, 1.6f);
	EXPECT_FLOAT_EQ(Capsule.m_Radius, 10.0f);
	EXPECT_FLOAT_EQ(Capsule.m_Amount, 0.0f);
}

TEST(QmRoundedSurface, AppleCurveUsesSmoothingDirectlyWithoutGrowingRadius)
{
	// Figma / CoreGraphics 的构造里曲线自己负责扩边，半径不再乘 1.3：
	// 否则曲线扩边和半径生长会叠加，圆角被放大两次。
	const SRoundedCornerShape Apple = ResolveRoundedCornerShape(14.0f, 400.0f, 300.0f, 0.6f, ROUNDED_CORNER_CURVE_APPLE, 0.0f);
	EXPECT_FLOAT_EQ(Apple.m_Radius, 14.0f);
	EXPECT_NEAR(Apple.m_Amount, 0.6f, 1e-6f);
	// 对照：同样参数的五次贝塞尔确实会生长。
	const SRoundedCornerShape Quintic = ResolveRoundedCornerShape(14.0f, 400.0f, 300.0f, 0.6f, ROUNDED_CORNER_CURVE_QUINTIC, 0.0f);
	EXPECT_GT(Quintic.m_Radius, 14.0f);
	// 平滑度为 0 时退化成原圆弧的外观：半径不变、强度为 0。
	const SRoundedCornerShape Flat = ResolveRoundedCornerShape(14.0f, 400.0f, 300.0f, 0.0f, ROUNDED_CORNER_CURVE_APPLE, 0.0f);
	EXPECT_FLOAT_EQ(Flat.m_Radius, 14.0f);
	EXPECT_FLOAT_EQ(Flat.m_Amount, 0.0f);
}
