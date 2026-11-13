#include <game/client/components/camera.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using QmCameraEffects::CZoomSmoothingCurve;

TEST(QmCameraZoomCurve, DefaultCurveIsConstant)
{
	const CZoomSmoothingCurve Curve;
	EXPECT_EQ(Curve.Evaluate(0.5f), 1.0f);
	EXPECT_EQ(Curve.Derivative(0.5f), 0.0f);
}

TEST(QmCameraZoomCurve, OrdinarySamplesKeepOriginalFloatResults)
{
	const auto Original = CCubicBezier::With(1.6f, -0.7f, 0.0f, 0.5f);
	const auto Curve = CZoomSmoothingCurve::With(1.6f, -0.7f, 0.5f);
	for(float Progress : {-0.5f, 0.0f, 0.1f, 0.5f, 0.9f, 1.0f, 1.5f})
	{
		SCOPED_TRACE(Progress);
		EXPECT_EQ(Curve.Evaluate(Progress), Original.Evaluate(Progress));
	}
}

TEST(QmCameraZoomCurve, OrdinaryDerivativesKeepOriginalFloatResults)
{
	const auto Original = CCubicBezier::With(0.8f, 1.4f, 0.0f, 3.0f);
	const auto Curve = CZoomSmoothingCurve::With(0.8f, 1.4f, 3.0f);
	for(float Progress : {0.0f, 0.1f, 0.5f, 0.9f, 1.0f})
	{
		SCOPED_TRACE(Progress);
		EXPECT_EQ(Curve.Derivative(Progress), Original.Derivative(Progress));
	}
}

TEST(QmCameraZoomCurve, ConstantFloatMaximumRemainsFinite)
{
	constexpr float Max = std::numeric_limits<float>::max();
	const auto Curve = CZoomSmoothingCurve::With(Max, 0.0f, Max);
	for(float Progress : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
	{
		SCOPED_TRACE(Progress);
		EXPECT_TRUE(std::isfinite(Curve.Evaluate(Progress)));
		EXPECT_FLOAT_EQ(Curve.Evaluate(Progress), Max);
		EXPECT_EQ(Curve.Derivative(Progress), 0.0f);
	}
}

TEST(QmCameraZoomCurve, OverflowingControlPointKeepsFiniteEndpoints)
{
	constexpr float Max = std::numeric_limits<float>::max();
	const auto Curve = CZoomSmoothingCurve::With(Max, Max, 1.0f);
	EXPECT_EQ(Curve.Evaluate(0.0f), Max);
	EXPECT_EQ(Curve.Evaluate(1.0f), 1.0f);
	EXPECT_EQ(Curve.Derivative(0.0f), Max);
	EXPECT_EQ(Curve.Derivative(1.0f), 0.0f);
}

TEST(QmCameraZoomCurve, WideControlPointStillProducesRepresentableInteriorSample)
{
	constexpr float Max = std::numeric_limits<float>::max();
	const auto Curve = CZoomSmoothingCurve::With(Max, Max, 1.0f);
	EXPECT_FLOAT_EQ(Curve.Evaluate(0.5f), Max * 0.625f);
}

TEST(QmCameraZoomCurve, UnrepresentableSampleUsesFloatMaximum)
{
	constexpr float Max = std::numeric_limits<float>::max();
	const auto Curve = CZoomSmoothingCurve::With(Max, Max, Max);
	EXPECT_EQ(Curve.Evaluate(0.5f), Max);
	EXPECT_EQ(Curve.Evaluate(1.0f), Max);
}

TEST(QmCameraZoomCurve, LargeZoomDownDerivativeUsesNegativeFloatMaximum)
{
	constexpr float Max = std::numeric_limits<float>::max();
	const auto Curve = CZoomSmoothingCurve::With(Max, 0.0f, 0.01f);
	EXPECT_EQ(Curve.Derivative(0.5f), -Max);
	EXPECT_FLOAT_EQ(Curve.Evaluate(0.5f), Max * 0.5f);
}

TEST(QmCameraZoomCurve, LargeZoomUpDerivativeUsesPositiveFloatMaximum)
{
	constexpr float Max = std::numeric_limits<float>::max();
	const auto Curve = CZoomSmoothingCurve::With(0.01f, 0.0f, Max);
	EXPECT_EQ(Curve.Derivative(0.5f), Max);
	EXPECT_EQ(Curve.Evaluate(1.0f), Max);
}

TEST(QmCameraZoomCurve, RetargetingWithRepresentableDerivativeKeepsFiniteCurve)
{
	constexpr float Max = std::numeric_limits<float>::max();
	const auto Previous = CZoomSmoothingCurve::With(1.0f, 0.0f, Max);
	const auto Next = CZoomSmoothingCurve::With(Previous.Evaluate(0.75f), Previous.Derivative(0.5f), Max);
	for(float Progress : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
	{
		SCOPED_TRACE(Progress);
		EXPECT_TRUE(std::isfinite(Next.Evaluate(Progress)));
		EXPECT_TRUE(std::isfinite(Next.Derivative(Progress)));
	}
	EXPECT_EQ(Next.Evaluate(1.0f), Max);
}

TEST(QmCameraZoomCurve, ReplacingAnExtremeCurveDiscardsOldControlState)
{
	constexpr float Max = std::numeric_limits<float>::max();
	auto Curve = CZoomSmoothingCurve::With(Max, Max, Max);
	Curve = CZoomSmoothingCurve::With(1.0f, 0.0f, 2.0f);
	EXPECT_EQ(Curve.Evaluate(0.0f), 1.0f);
	EXPECT_EQ(Curve.Evaluate(0.5f), 1.5f);
	EXPECT_EQ(Curve.Evaluate(1.0f), 2.0f);
	EXPECT_EQ(Curve.Derivative(1.0f), 0.0f);
}
