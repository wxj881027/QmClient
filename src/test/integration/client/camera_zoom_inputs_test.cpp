#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/client/components/camera.h>

#include <gtest/gtest.h>

#include <cmath>

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
	const auto Previous = CCubicBezier::With(1.0f, 0.0f, 0.0f, 0.5f);
	const float Progress = QmCameraEffects::TransitionProgress(10.0f, 10.0f, 10.0f);
	const float Current = Previous.Evaluate(Progress);
	const float Derivative = Previous.Derivative(Progress);
	EXPECT_FLOAT_EQ(Current, 0.5f);
	EXPECT_FLOAT_EQ(Derivative, 0.0f);
	const auto Next = CCubicBezier::With(Current, Derivative, 0.0f, 1.0f);
	const float Value = Next.Evaluate(QmCameraEffects::TransitionProgress(10.125f, 10.0f, 10.25f));
	EXPECT_TRUE(std::isfinite(Value));
	EXPECT_GT(Value, Current);
	EXPECT_LT(Value, 1.0f);
}
