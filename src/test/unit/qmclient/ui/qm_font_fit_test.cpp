#include <game/client/QmUi/UiTokens.h>
#include <game/client/qm_icon.h>
#include <game/client/ui_font_fit.h>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace
{
	// 字宽 ∝ 字号：把算法与真实字体度量的非线性隔离开。
	constexpr float WIDTH_PER_SIZE = 10.0f;

	float Fit(float StartSize, float Minimum, float MaxWidth, float GridStep, int *pCalls = nullptr)
	{
		int Calls = 0;
		const float Result = ResolveFittedFontSize(StartSize, Minimum, MaxWidth, GridStep, [&](float Size) {
			++Calls;
			return Size * WIDTH_PER_SIZE;
		});
		if(pCalls != nullptr)
			*pCalls = Calls;
		return Result;
	}

	// 旧实现：Size-- 逐档整数递减。
	float LegacyFit(float StartSize, float Minimum, float MaxWidth, int *pCalls)
	{
		int Calls = 0;
		float Size = StartSize;
		while(true)
		{
			++Calls;
			if(Size * WIDTH_PER_SIZE <= MaxWidth + 0.001f || Size <= Minimum)
				break;
			Size -= 1.0f;
		}
		if(pCalls != nullptr)
			*pCalls = Calls;
		return Size;
	}
}

TEST(QmFontFit, UnitGridMatchesLegacyIntegerDescent)
{
	int LegacyCalls = 0;
	int BinaryCalls = 0;
	const float Legacy = LegacyFit(14.0f, 8.0f, 105.0f, &LegacyCalls);
	const float Fitted = Fit(14.0f, 8.0f, 105.0f, 1.0f, &BinaryCalls);
	EXPECT_FLOAT_EQ(Legacy, 10.0f);
	EXPECT_FLOAT_EQ(Fitted, Legacy);
	EXPECT_LE(BinaryCalls, LegacyCalls + 2);
}

TEST(QmFontFit, HalfGridIsActuallyHalfGridAndNeverOverflows)
{
	const float Fitted = Fit(14.0f, 8.0f, 105.0f, 0.5f);
	EXPECT_FLOAT_EQ(Fitted, 10.5f);
	EXPECT_GT(Fitted, 10.0f); // 比整数档更贴近可用宽度
	EXPECT_LE(Fitted * WIDTH_PER_SIZE, 105.0f + 0.001f);
}

TEST(QmFontFit, FineGridCostsFewerMeasurementsThanLinearDescentBySameStep)
{
	int BinaryCalls = 0;
	Fit(14.0f, 8.0f, 105.0f, 0.5f, &BinaryCalls);
	// 逐档 0.5 递减需要试 14, 13.5, ..., 10.5 共 8 次。
	EXPECT_LT(BinaryCalls, 8);
}

TEST(QmFontFit, ResultAlwaysOnGridAndWithinBounds)
{
	for(const float MaxWidth : {82.0f, 97.0f, 105.0f, 118.0f, 139.0f})
	{
		const float Fitted = Fit(14.0f, 8.0f, MaxWidth, 0.5f);
		EXPECT_GE(Fitted, 8.0f);
		EXPECT_LE(Fitted, 14.0f);
		EXPECT_NEAR(std::fmod(Fitted, 0.5f), 0.0f, 0.0001f);
		EXPECT_LE(Fitted * WIDTH_PER_SIZE, MaxWidth + 0.001f) << "MaxWidth=" << MaxWidth;
	}
}

TEST(QmFontFit, BinaryCandidatesStayOnGrid)
{
	// 中间档若用任意浮点字号，会生成一批一次性物理字号，每个都要重新光栅化
	// 该标签的全部字形。这里锁住「候选必须落在网格上」这条防线。
	std::vector<float> vCandidates;
	const float Result = ResolveFittedFontSize(14.0f, 8.0f, 105.0f, 0.5f, [&](float Size) {
		vCandidates.push_back(Size);
		return Size * WIDTH_PER_SIZE;
	});
	EXPECT_FALSE(vCandidates.empty());
	for(const float Candidate : vCandidates)
		EXPECT_NEAR(std::fmod(Candidate, 0.5f), 0.0f, 0.0001f) << "candidate=" << Candidate;
	EXPECT_FLOAT_EQ(Result, 10.5f);
}

TEST(QmFontFit, DegenerateRangeDoesNotLoop)
{
	EXPECT_FLOAT_EQ(Fit(10.0f, 10.0f, 100.0f, 0.5f), 10.0f);
}

TEST(QmFontFit, ActionIconsShareOneInkHeight)
{
	const float InkHeight = ui_token::control::ActionIconInkHeight(12.0f);
	const float StarSize = QmIconInkFontSize(EQmIcon::STAR, InkHeight);
	const float BoxSize = QmIconInkFontSize(EQmIcon::SQUARE_PLUS, InkHeight);
	EXPECT_FLOAT_EQ(StarSize * QmIconInkHeightRatio(EQmIcon::STAR), InkHeight);
	EXPECT_FLOAT_EQ(BoxSize * QmIconInkHeightRatio(EQmIcon::SQUARE_PLUS), InkHeight);
	// 墨迹占比更大的星形必须用更小的 em 字号，两者才会视觉等高。
	EXPECT_LT(StarSize, BoxSize);
}

TEST(QmFontFit, DebugCaptionKeepsItsScreenSizeAcrossCoordinateSystems)
{
	using ui_token::hud::font::DebugSize;
	const float HudSize = DebugSize(480.0f, 300.0f);
	EXPECT_FLOAT_EQ(ui_token::hud::font::CAPTION, HudSize);
	EXPECT_FLOAT_EQ(2.0f * HudSize, DebugSize(960.0f, 600.0f));
	EXPECT_FLOAT_EQ(3.0f * HudSize, DebugSize(1440.0f, 900.0f));
	const float NarrowSize = DebugSize(280.0f, 300.0f);
	EXPECT_LT(NarrowSize, HudSize);
	EXPECT_FLOAT_EQ(2.0f * NarrowSize, DebugSize(560.0f, 600.0f));
}
