#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/client/components/camera.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
	struct SZoomArgument
	{
		int m_Calls = 0;
		float m_Value = 0.0f;
	};

	void CaptureZoomArgument(IConsole::IResult *pResult, void *pUser)
	{
		auto &Argument = *static_cast<SZoomArgument *>(pUser);
		++Argument.m_Calls;
		Argument.m_Value = pResult->GetFloat(0);
	}
}

TEST(CameraZoomInputs, ConsoleNonFiniteArgumentDoesNotBecomeValidCameraTarget)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SZoomArgument Argument;
	pConsole->Register("camera_zoom", "f", CFGFLAG_CLIENT, CaptureZoomArgument, &Argument, "");
	for(const char *pLine : {"camera_zoom nan", "camera_zoom inf", "camera_zoom -inf"})
	{
		SCOPED_TRACE(pLine);
		Argument.m_Calls = 0;
		pConsole->ExecuteLine(pLine);
		ASSERT_EQ(Argument.m_Calls, 1);
		ASSERT_FALSE(std::isfinite(Argument.m_Value));
		EXPECT_FALSE(QmCameraEffects::IsValidZoom(CCamera::ZoomStepsToValue(Argument.m_Value - 10.0f), 0.01f, 240.0f));
	}
}

TEST(CameraZoomInputs, ConsoleFiniteDefaultLevelProducesAcceptedZoom)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SZoomArgument Argument;
	pConsole->Register("camera_zoom", "f", CFGFLAG_CLIENT, CaptureZoomArgument, &Argument, "");
	pConsole->ExecuteLine("camera_zoom 10");
	ASSERT_EQ(Argument.m_Calls, 1);
	const float Target = CCamera::ZoomStepsToValue(Argument.m_Value - 10.0f);
	EXPECT_FLOAT_EQ(Target, 1.0f);
	EXPECT_TRUE(QmCameraEffects::IsValidZoom(Target, 0.01f, 240.0f));
}

TEST(CameraZoomInputs, FiniteCommandWithOverflowedExponentialIsRejected)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SZoomArgument Argument;
	pConsole->Register("camera_zoom", "f", CFGFLAG_CLIENT, CaptureZoomArgument, &Argument, "");
	pConsole->ExecuteLine("camera_zoom -100000");
	ASSERT_EQ(Argument.m_Calls, 1);
	ASSERT_TRUE(std::isfinite(Argument.m_Value));
	const float Target = CCamera::ZoomStepsToValue(Argument.m_Value - 10.0f);
	ASSERT_FALSE(std::isfinite(Target));
	EXPECT_FALSE(QmCameraEffects::IsValidZoom(Target, 0.01f, 240.0f));
}

TEST(CameraZoomInputs, ZeroDurationEndpointCanRetargetToFinitePositiveDurationCurve)
{
	const auto Previous = QmCameraEffects::CZoomSmoothingCurve::With(1.0f, 0.0f, 0.5f);
	const float Progress = QmCameraEffects::TransitionProgress(10.0f, 10.0f, 10.0f);
	const float Current = Previous.Evaluate(Progress);
	const float Derivative = Previous.Derivative(Progress);
	EXPECT_FLOAT_EQ(Current, 0.5f);
	EXPECT_FLOAT_EQ(Derivative, 0.0f);
	const auto Next = QmCameraEffects::CZoomSmoothingCurve::With(Current, Derivative, 1.0f);
	const float Value = Next.Evaluate(QmCameraEffects::TransitionProgress(10.125f, 10.0f, 10.25f));
	EXPECT_TRUE(std::isfinite(Value));
	EXPECT_GT(Value, Current);
	EXPECT_LT(Value, 1.0f);
}

TEST(CameraZoomInputs, UnlimitedConsoleTargetCanRetargetAfterDerivativeOverflow)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SZoomArgument Argument;
	pConsole->Register("camera_zoom", "f", CFGFLAG_CLIENT, CaptureZoomArgument, &Argument, "");
	pConsole->ExecuteLine("camera_zoom -605");
	ASSERT_EQ(Argument.m_Calls, 1);
	const float Start = CCamera::ZoomStepsToValue(Argument.m_Value - 10.0f);
	constexpr float Max = std::numeric_limits<float>::max();
	ASSERT_TRUE(QmCameraEffects::IsValidZoom(Start, 0.01f, Max));
	ASSERT_GT(Start, Max * (2.0f / 3.0f));
	const auto Previous = QmCameraEffects::CZoomSmoothingCurve::With(Start, 0.0f, 1.0f);
	const float Current = Previous.Evaluate(0.5f);
	const float Derivative = QmCameraEffects::ZoomDerivativeOnRetarget(Current, Previous.Derivative(0.5f), 1.0f, false);
	EXPECT_EQ(Derivative, -Max);
	const auto Next = QmCameraEffects::CZoomSmoothingCurve::With(Current, Derivative, 1.0f);
	EXPECT_TRUE(std::isfinite(Next.Evaluate(0.25f)));
	EXPECT_TRUE(std::isfinite(Next.Derivative(0.25f)));
	EXPECT_EQ(Next.Evaluate(1.0f), 1.0f);
}

TEST(CameraZoomInputs, RemovedDynamicFovBaseStartsAFiniteZoomCurveAtFloatLimit)
{
	constexpr float Max = std::numeric_limits<float>::max();
	QmCameraEffects::CDynamicFovZoom Fov;
	const float Effective = Fov.Apply(Max, 5.0f);
	const float Base = Fov.Remove(Effective);
	ASSERT_EQ(Base, Max);
	const auto Previous = QmCameraEffects::CZoomSmoothingCurve::With(Base, 0.0f, 1.0f);
	const float Current = Previous.Evaluate(0.5f);
	const float Derivative = QmCameraEffects::ZoomDerivativeOnRetarget(Current, Previous.Derivative(0.5f), Max, true);
	EXPECT_EQ(Derivative, 0.0f);
	const auto Next = QmCameraEffects::CZoomSmoothingCurve::With(Current, Derivative, Max);
	EXPECT_EQ(Next.Evaluate(0.0f), Current);
	EXPECT_TRUE(std::isfinite(Next.Evaluate(0.5f)));
	EXPECT_EQ(Next.Evaluate(1.0f), Max);
}
