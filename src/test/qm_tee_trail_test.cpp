#include <game/client/components/tclient/qm_tee_trail.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{

	// 由新到旧的采样：索引 0 是最新的点，与 CTrailState::Export 的输出顺序一致。
	std::vector<CTrailPart> MakeTrail(size_t Count = 8, double NewestTime = 40.0)
	{
		std::vector<CTrailPart> vTrail(Count);
		for(size_t i = 0; i < Count; ++i)
		{
			const double Time = NewestTime - double(i);
			vTrail[i].m_Pos = vec2(200.0f - float(i) * 18.0f, 40.0f + float(i) * 6.0f);
			vTrail[i].m_Col = ColorRGBA(0.2f, 0.6f, 0.9f, 0.8f);
			vTrail[i].m_Width = 8.0f;
			vTrail[i].m_Time = Time;
			vTrail[i].m_Tick = int(Time);
			vTrail[i].m_Distance = double(Count - 1 - i) * 19.0;
			vTrail[i].m_Speed = 20.0f;
			vTrail[i].m_Life = 40.0f;
		}
		return vTrail;
	}

	bool SameQuad(const qm_tee_trail::SQuad &A, const qm_tee_trail::SQuad &B)
	{
		if(A.m_Additive != B.m_Additive)
			return false;
		for(int i = 0; i < 4; ++i)
		{
			if(A.m_aPos[i] != B.m_aPos[i] || A.m_aColor[i].r != B.m_aColor[i].r || A.m_aColor[i].g != B.m_aColor[i].g ||
				A.m_aColor[i].b != B.m_aColor[i].b || A.m_aColor[i].a != B.m_aColor[i].a)
				return false;
		}
		return true;
	}

	bool AllFinite(const std::vector<qm_tee_trail::SQuad> &vQuads)
	{
		for(const auto &Quad : vQuads)
			for(int i = 0; i < 4; ++i)
				if(!std::isfinite(Quad.m_aPos[i].x) || !std::isfinite(Quad.m_aPos[i].y) ||
					!std::isfinite(Quad.m_aColor[i].r) || !std::isfinite(Quad.m_aColor[i].g) ||
					!std::isfinite(Quad.m_aColor[i].b) || !std::isfinite(Quad.m_aColor[i].a))
					return false;
		return true;
	}

	double WeightedArea(const std::vector<qm_tee_trail::SQuad> &vQuads)
	{
		double Sum = 0.0;
		for(const auto &Quad : vQuads)
		{
			double Area = 0.0, Alpha = 0.0;
			for(int i = 0; i < 4; ++i)
			{
				const vec2 A = Quad.m_aPos[i], B = Quad.m_aPos[(i + 1) % 4];
				Area += double(A.x) * B.y - double(A.y) * B.x;
				Alpha += Quad.m_aColor[i].a;
			}
			Sum += std::abs(Area) * 0.5 * Alpha * 0.25;
		}
		return Sum;
	}

	std::vector<CTrailPart> SampleMovement(int StepsPerTick)
	{
		qm_tee_trail::CTrailState State;
		for(int Step = 0; Step <= 40 * StepsPerTick; ++Step)
		{
			const double Time = double(Step) / StepsPerTick;
			State.Update(vec2(float(Time * 8.0), 20.0f), Time, 8.0f, 80.0f);
		}
		std::vector<CTrailPart> vTrail;
		State.Export(vTrail);
		for(auto &Part : vTrail)
			Part.m_Col = ColorRGBA(0.3f, 0.7f, 0.8f, 0.6f);
		return vTrail;
	}

} // namespace

TEST(QmTeeTrailStyle, UnknownStyleValuesFallBackToOriginal)
{
	EXPECT_EQ(qm_tee_trail::ResolveStyle(qm_tee_trail::STYLE_ORIGINAL), qm_tee_trail::STYLE_ORIGINAL);
	EXPECT_EQ(qm_tee_trail::ResolveStyle(qm_tee_trail::STYLE_PIXEL), qm_tee_trail::STYLE_PIXEL);
	// 配置里可能残留越界或负数：必须退回原版，而不是索引到样式表之外。
	EXPECT_EQ(qm_tee_trail::ResolveStyle(-1), qm_tee_trail::STYLE_ORIGINAL);
	EXPECT_EQ(qm_tee_trail::ResolveStyle(6), qm_tee_trail::STYLE_ORIGINAL);
	EXPECT_EQ(qm_tee_trail::ResolveStyle(99), qm_tee_trail::STYLE_ORIGINAL);
}

TEST(QmTeeTrailStyle, LifetimeClampsLengthAndGrowsWithSpeed)
{
	const int Style = qm_tee_trail::STYLE_MAGIC;
	// 长度低于下限或高于上限时被夹紧。
	EXPECT_FLOAT_EQ(qm_tee_trail::Lifetime(Style, 0, 0.0f), qm_tee_trail::Lifetime(Style, 5, 0.0f));
	EXPECT_FLOAT_EQ(qm_tee_trail::Lifetime(Style, 9999, 0.0f), qm_tee_trail::Lifetime(Style, 200, 0.0f));
	// 更长、更快都延长寿命。
	EXPECT_GT(qm_tee_trail::Lifetime(Style, 120, 0.0f), qm_tee_trail::Lifetime(Style, 20, 0.0f));
	EXPECT_GT(qm_tee_trail::Lifetime(Style, 50, 30.0f), qm_tee_trail::Lifetime(Style, 50, 0.0f));
	// 越界样式按原版处理，而不是读到样式表外面。
	EXPECT_FLOAT_EQ(qm_tee_trail::Lifetime(99, 50, 10.0f), qm_tee_trail::Lifetime(qm_tee_trail::STYLE_ORIGINAL, 50, 10.0f));
}

TEST(QmTeeTrailBuild, EveryStyleBuildsBoundedFiniteGeometry)
{
	const std::vector<CTrailPart> vTrail = MakeTrail();
	for(int Style = 0; Style < qm_tee_trail::STYLE_COUNT; ++Style)
	{
		std::vector<qm_tee_trail::SQuad> vQuads;
		qm_tee_trail::BuildEffect(vTrail, Style, true, 40.0, 17.0f, 7, vQuads);
		EXPECT_FALSE(vQuads.empty()) << "style " << Style;
		EXPECT_LE(vQuads.size(), qm_tee_trail::MAX_QUADS) << "style " << Style;
		EXPECT_TRUE(AllFinite(vQuads)) << "style " << Style;
	}
}

TEST(QmTeeTrailBuild, SameInputAndSeedProduceTheSameGeometry)
{
	const std::vector<CTrailPart> vTrail = MakeTrail();
	std::vector<qm_tee_trail::SQuad> vFirst;
	std::vector<qm_tee_trail::SQuad> vSecond;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_PIXEL, false, 40.0, 9.0f, 42, vFirst);
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_PIXEL, false, 40.0, 9.0f, 42, vSecond);
	ASSERT_EQ(vFirst.size(), vSecond.size());
	for(size_t i = 0; i < vFirst.size(); ++i)
		EXPECT_TRUE(SameQuad(vFirst[i], vSecond[i])) << "quad " << i;
}

TEST(QmTeeTrailBuild, DifferentSeedsProduceDifferentGeometry)
{
	const std::vector<CTrailPart> vTrail = MakeTrail();
	std::vector<qm_tee_trail::SQuad> vFirst;
	std::vector<qm_tee_trail::SQuad> vSecond;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_MAGIC, false, 40.0, 9.0f, 1, vFirst);
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_MAGIC, false, 40.0, 9.0f, 2, vSecond);
	ASSERT_FALSE(vFirst.empty());
	ASSERT_FALSE(vSecond.empty());
	// 种子按玩家区分：相同输入也应给出不同扰动，否则所有玩家拖尾完全重合。
	bool AnyDifferent = vFirst.size() != vSecond.size();
	for(size_t i = 0; i < std::min(vFirst.size(), vSecond.size()) && !AnyDifferent; ++i)
		AnyDifferent = !SameQuad(vFirst[i], vSecond[i]);
	EXPECT_TRUE(AnyDifferent);
}

TEST(QmTeeTrailBuild, RejectsInputItCannotDrawSafely)
{
	std::vector<qm_tee_trail::SQuad> vQuads;

	// 少于两个点。
	qm_tee_trail::BuildEffect(MakeTrail(1), qm_tee_trail::STYLE_MAGIC, true, 40.0, 4.0f, 1, vQuads);
	EXPECT_TRUE(vQuads.empty());

	// 所有点重合，总长度接近零。
	std::vector<CTrailPart> vStationary = MakeTrail();
	for(CTrailPart &Part : vStationary)
		Part.m_Pos = vec2(0.0f, 0.0f);
	qm_tee_trail::BuildEffect(vStationary, qm_tee_trail::STYLE_MAGIC, true, 40.0, 4.0f, 1, vQuads);
	EXPECT_TRUE(vQuads.empty());

	// 相邻点跳变过大视为传送，不能画出一条跨越全图的假轨迹。
	std::vector<CTrailPart> vTeleport = MakeTrail();
	vTeleport[3].m_Pos += vec2(400.0f, 0.0f);
	qm_tee_trail::BuildEffect(vTeleport, qm_tee_trail::STYLE_MAGIC, true, 40.0, 4.0f, 1, vQuads);
	EXPECT_TRUE(vQuads.empty());

	// 时间戳晚于当前时间：说明采样来自未来帧，拒绝而不是画出未定义形状。
	std::vector<CTrailPart> vFuture = MakeTrail();
	vFuture[0].m_Time = 1000.0;
	qm_tee_trail::BuildEffect(vFuture, qm_tee_trail::STYLE_MAGIC, true, 40.0, 4.0f, 1, vQuads);
	EXPECT_TRUE(vQuads.empty());

	// 非有限输入。
	std::vector<CTrailPart> vNan = MakeTrail();
	vNan[2].m_Pos = vec2(std::numeric_limits<float>::quiet_NaN(), 0.0f);
	qm_tee_trail::BuildEffect(vNan, qm_tee_trail::STYLE_MAGIC, true, 40.0, 4.0f, 1, vQuads);
	EXPECT_TRUE(vQuads.empty());

	// 非有限的线宽和像素尺寸同样不能进入几何构建。
	qm_tee_trail::BuildEffect(MakeTrail(), qm_tee_trail::STYLE_MAGIC, true, 40.0, std::numeric_limits<float>::quiet_NaN(), 1, vQuads);
	EXPECT_TRUE(vQuads.empty());
}

TEST(QmTeeTrailBuild, PresetPaletteAndTrailColorsDiffer)
{
	const std::vector<CTrailPart> vTrail = MakeTrail();
	std::vector<qm_tee_trail::SQuad> vPreset;
	std::vector<qm_tee_trail::SQuad> vTrailColors;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_MAGIC, true, 40.0, 6.0f, 9, vPreset);
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_MAGIC, false, 40.0, 6.0f, 9, vTrailColors);
	ASSERT_FALSE(vPreset.empty());
	ASSERT_FALSE(vTrailColors.empty());
	ASSERT_EQ(vPreset.size(), vTrailColors.size());
	bool AnyDifferent = false;
	for(size_t i = 0; i < vPreset.size() && !AnyDifferent; ++i)
		AnyDifferent = !SameQuad(vPreset[i], vTrailColors[i]);
	EXPECT_TRUE(AnyDifferent);
}

TEST(QmTeeTrailBuild, InkAndMagicBatchNormalLayersBeforeAdditiveAccents)
{
	const std::vector<CTrailPart> vTrail = MakeTrail();
	for(const int Style : {qm_tee_trail::STYLE_MANGA, qm_tee_trail::STYLE_MAGIC})
	{
		std::vector<qm_tee_trail::SQuad> vQuads;
		qm_tee_trail::BuildEffect(vTrail, Style, true, 40.0, 12.0f, 9, vQuads);
		bool HasNormal = false, HasAdditive = false;
		for(const auto &Quad : vQuads)
		{
			if(Quad.m_Additive)
				HasAdditive = true;
			else
			{
				EXPECT_FALSE(HasAdditive) << "normal layer after additive for style " << Style;
				HasNormal = true;
			}
		}
		EXPECT_TRUE(HasNormal) << Style;
		EXPECT_TRUE(HasAdditive) << Style;
	}
}

TEST(QmTeeTrailBuild, OriginalAndPixelsKeepNormalBlending)
{
	for(const int Style : {qm_tee_trail::STYLE_ORIGINAL, qm_tee_trail::STYLE_PIXEL})
	{
		std::vector<qm_tee_trail::SQuad> vQuads;
		qm_tee_trail::BuildEffect(MakeTrail(), Style, true, 40.0, 12.0f, 9, vQuads);
		ASSERT_FALSE(vQuads.empty());
		for(const auto &Quad : vQuads)
			EXPECT_FALSE(Quad.m_Additive);
	}
}

TEST(QmTeeTrailState, SamplesAtEvenSpacingAlongThePath)
{
	qm_tee_trail::CTrailState State;
	// 沿直线匀速前进：每次 Update 前进 12 单位，超过采样间距 6。
	for(int Tick = 0; Tick <= 60; ++Tick)
		State.Update(vec2(float(Tick) * 12.0f, 0.0f), double(Tick), 12.0f, 60.0f);

	std::vector<CTrailPart> vOut;
	State.Export(vOut);
	ASSERT_GE(vOut.size(), 3u);
	// 最新点在最前，且相邻导出点的间距应接近采样间距。
	for(size_t i = 0; i + 1 < vOut.size(); ++i)
	{
		const float Gap = distance(vOut[i].m_Pos, vOut[i + 1].m_Pos);
		EXPECT_NEAR(Gap, qm_tee_trail::SAMPLE_SPACING, 0.75f) << "gap " << i;
	}
	// 头端就是最后一次位置。
	EXPECT_NEAR(vOut.front().m_Pos.x, 60.0f * 12.0f, 0.5f);
}

TEST(QmTeeTrailState, IgnoresMovementBelowTheMinimumSpeed)
{
	qm_tee_trail::CTrailState State;
	// 每 tick 只挪 0.01 单位：低于 MIN_SPEED，不应产生任何采样点。
	for(int Tick = 0; Tick <= 40; ++Tick)
		State.Update(vec2(float(Tick) * 0.01f, 0.0f), double(Tick), 0.01f, 60.0f);

	std::vector<CTrailPart> vOut;
	State.Export(vOut);
	// 只剩端帽，没有采样尾巴。
	EXPECT_LE(vOut.size(), 1u);
}

TEST(QmTeeTrailState, BreakDropsHistoryAndRestartsAtTheNewPosition)
{
	qm_tee_trail::CTrailState State;
	for(int Tick = 0; Tick <= 60; ++Tick)
		State.Update(vec2(float(Tick) * 12.0f, 0.0f), double(Tick), 12.0f, 60.0f);

	std::vector<CTrailPart> vBefore;
	State.Export(vBefore);
	ASSERT_GE(vBefore.size(), 3u);

	// 显式断点（传送、复活、渲染时间源切换）必须清掉旧轨迹。
	State.Update(vec2(5000.0f, 5000.0f), 61.0, 12.0f, 60.0f, true);
	std::vector<CTrailPart> vAfter;
	State.Export(vAfter);
	ASSERT_LE(vAfter.size(), 2u);
	for(const auto &Part : vAfter)
		EXPECT_NEAR(Part.m_Pos.x, 5000.0f, 0.5f);
}

TEST(QmTeeTrailState, TeleportBreakIsDetectedWithoutAnExplicitFlag)
{
	qm_tee_trail::CTrailState State;
	for(int Tick = 0; Tick <= 40; ++Tick)
		State.Update(vec2(float(Tick) * 8.0f, 0.0f), double(Tick), 8.0f, 60.0f);

	// 一帧内跳过几百单位：按 12.5 tick 间隔上限与位移预算判定为传送，历史被丢弃。
	State.Update(vec2(4000.0f, 0.0f), 41.0, 8.0f, 60.0f);
	std::vector<CTrailPart> vOut;
	State.Export(vOut);
	ASSERT_LE(vOut.size(), 2u);
	for(const auto &Part : vOut)
		EXPECT_NEAR(Part.m_Pos.x, 4000.0f, 0.5f);
}

TEST(QmTeeTrailState, DropsSamplesOnceTheirLifetimeHasPassed)
{
	qm_tee_trail::CTrailState State;
	for(int Tick = 0; Tick <= 30; ++Tick)
		State.Update(vec2(float(Tick) * 12.0f, 0.0f), double(Tick), 12.0f, 5.0f);

	std::vector<CTrailPart> vFresh;
	State.Export(vFresh);
	ASSERT_GE(vFresh.size(), 2u);

	// 停住不动并推进超过寿命：旧采样必须老化掉，只留当前端帽。
	State.Update(vec2(30.0f * 12.0f, 0.0f), 200.0, 0.0f, 5.0f);
	std::vector<CTrailPart> vAged;
	State.Export(vAged);
	EXPECT_LE(vAged.size(), 1u);
}

TEST(QmTeeTrailState, ExportedHistoryStaysWithinCapacity)
{
	qm_tee_trail::CTrailState State;
	// 跑足够远以填满环形队列；寿命给足以确保不是被老化清掉的。
	for(int Tick = 0; Tick <= 4000; ++Tick)
		State.Update(vec2(float(Tick) * 12.0f, 0.0f), double(Tick), 12.0f, 400.0f);

	std::vector<CTrailPart> vOut;
	State.Export(vOut);
	EXPECT_LE(vOut.size(), qm_tee_trail::MAX_POINTS + 1);
	EXPECT_GT(vOut.size(), 10u);

	// 队列填满后仍然保持等距：最老的采样被丢弃，而不是把间距挤没。
	for(size_t i = 0; i + 1 < vOut.size(); ++i)
		EXPECT_NEAR(distance(vOut[i].m_Pos, vOut[i + 1].m_Pos), qm_tee_trail::SAMPLE_SPACING, 0.75f);
}

TEST(QmTeeTrailState, ResetClearsEverything)
{
	qm_tee_trail::CTrailState State;
	for(int Tick = 0; Tick <= 40; ++Tick)
		State.Update(vec2(float(Tick) * 12.0f, 0.0f), double(Tick), 12.0f, 60.0f);
	State.Reset();

	std::vector<CTrailPart> vOut = {CTrailPart()};
	State.Export(vOut);
	EXPECT_TRUE(vOut.empty());
}

TEST(QmTeeTrailState, NonFiniteInputResetsInsteadOfPoisoningTheQueue)
{
	qm_tee_trail::CTrailState State;
	for(int Tick = 0; Tick <= 40; ++Tick)
		State.Update(vec2(float(Tick) * 12.0f, 0.0f), double(Tick), 12.0f, 60.0f);

	State.Update(vec2(std::numeric_limits<float>::quiet_NaN(), 0.0f), 41.0, 12.0f, 60.0f);
	std::vector<CTrailPart> vOut = {CTrailPart()};
	State.Export(vOut);
	EXPECT_TRUE(vOut.empty());

	// 恢复有效输入后应能正常重新采样，而不是永久卡在损坏状态。
	for(int Tick = 42; Tick <= 90; ++Tick)
		State.Update(vec2(float(Tick) * 12.0f, 0.0f), double(Tick), 12.0f, 60.0f);
	State.Export(vOut);
	EXPECT_FALSE(vOut.empty());
}

TEST(QmTeeTrailStyle, LegacySavedStylesResolveToTheReplacementGeometry)
{
	const int aaMappings[][2] = {{4, qm_tee_trail::STYLE_MANGA}, {5, qm_tee_trail::STYLE_MAGIC}};
	for(const auto &Mapping : aaMappings)
	{
		EXPECT_EQ(qm_tee_trail::ResolveStyle(Mapping[0]), Mapping[1]);
		EXPECT_FLOAT_EQ(qm_tee_trail::Lifetime(Mapping[0], 80, 20.0f), qm_tee_trail::Lifetime(Mapping[1], 80, 20.0f));
		std::vector<qm_tee_trail::SQuad> vLegacy, vCurrent;
		qm_tee_trail::BuildEffect(MakeTrail(), Mapping[0], true, 40.0, 12.0f, 9, vLegacy);
		qm_tee_trail::BuildEffect(MakeTrail(), Mapping[1], true, 40.0, 12.0f, 9, vCurrent);
		ASSERT_FALSE(vLegacy.empty());
		ASSERT_EQ(vLegacy.size(), vCurrent.size());
		for(size_t i = 0; i < vLegacy.size(); ++i)
			EXPECT_TRUE(SameQuad(vLegacy[i], vCurrent[i]));
	}
}

TEST(QmTeeTrailBuild, OriginalIgnoresStylePaletteAndPlayerSeed)
{
	const auto vTrail = MakeTrail();
	for(const bool Taper : {false, true})
	{
		std::vector<qm_tee_trail::SQuad> vReference, vStyled;
		qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_ORIGINAL, false, 40.0, 12.0f, 1, vReference, 0.5f, Taper);
		qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_ORIGINAL, true, 40.0, 12.0f, 917, vStyled, 0.5f, Taper);
		ASSERT_FALSE(vReference.empty());
		ASSERT_EQ(vReference.size(), vStyled.size());
		for(size_t i = 0; i < vReference.size(); ++i)
		{
			EXPECT_TRUE(SameQuad(vReference[i], vStyled[i]));
			for(const auto &Color : vReference[i].m_aColor)
			{
				EXPECT_FLOAT_EQ(Color.r, vTrail[0].m_Col.r);
				EXPECT_FLOAT_EQ(Color.g, vTrail[0].m_Col.g);
				EXPECT_FLOAT_EQ(Color.b, vTrail[0].m_Col.b);
			}
		}
	}
}

TEST(QmTeeTrailBuild, CustomPaletteKeepsEachStylesShapeAndOpacity)
{
	for(int Style = qm_tee_trail::STYLE_MANGA; Style < qm_tee_trail::STYLE_COUNT; ++Style)
	{
		std::vector<qm_tee_trail::SQuad> vPreset, vCustom;
		qm_tee_trail::BuildEffect(MakeTrail(), Style, true, 40.0, 12.0f, 9, vPreset);
		qm_tee_trail::BuildEffect(MakeTrail(), Style, false, 40.0, 12.0f, 9, vCustom);
		ASSERT_FALSE(vPreset.empty());
		ASSERT_EQ(vPreset.size(), vCustom.size()) << Style;
		for(size_t i = 0; i < vPreset.size(); ++i)
			for(int Vertex = 0; Vertex < 4; ++Vertex)
			{
				EXPECT_EQ(vPreset[i].m_aPos[Vertex], vCustom[i].m_aPos[Vertex]) << Style;
				EXPECT_FLOAT_EQ(vPreset[i].m_aColor[Vertex].a, vCustom[i].m_aColor[Vertex].a) << Style;
			}
	}
}

TEST(QmTeeTrailBuild, FullyTransparentTrailsClearEveryLayer)
{
	auto vTrail = MakeTrail();
	for(auto &Part : vTrail)
		Part.m_Col.a = 0.0f;
	for(int Style = 0; Style < qm_tee_trail::STYLE_COUNT; ++Style)
	{
		std::vector<qm_tee_trail::SQuad> vQuads(1);
		qm_tee_trail::BuildEffect(vTrail, Style, true, 40.0, 20.0f, 9, vQuads);
		EXPECT_TRUE(vQuads.empty()) << Style;
	}
}

TEST(QmTeeTrailBuild, ExpiredTrailsDoNotLeaveCracksMarksOrPixels)
{
	for(int Style = 0; Style < qm_tee_trail::STYLE_COUNT; ++Style)
	{
		std::vector<qm_tee_trail::SQuad> vQuads;
		qm_tee_trail::BuildEffect(MakeTrail(), Style, true, 40.0, 12.0f, 9, vQuads);
		ASSERT_FALSE(vQuads.empty()) << Style;
		qm_tee_trail::BuildEffect(MakeTrail(), Style, true, 90.0, 12.0f, 9, vQuads);
		EXPECT_TRUE(vQuads.empty()) << Style;
	}
}

TEST(QmTeeTrailBuild, TaperSwitchNarrowsAgingTrails)
{
	for(int Style = 0; Style < qm_tee_trail::STYLE_COUNT; ++Style)
	{
		std::vector<qm_tee_trail::SQuad> vFull, vTapered;
		qm_tee_trail::BuildEffect(MakeTrail(), Style, true, 64.0, 12.0f, 9, vFull, 0.5f, false);
		qm_tee_trail::BuildEffect(MakeTrail(), Style, true, 64.0, 12.0f, 9, vTapered, 0.5f, true);
		ASSERT_GT(WeightedArea(vFull), 0.0) << Style;
		EXPECT_LT(WeightedArea(vTapered), WeightedArea(vFull)) << Style;
	}
}

TEST(QmTeeTrailBuild, FadeSwitchReducesEmittedOpacity)
{
	for(int Style = 0; Style < qm_tee_trail::STYLE_COUNT; ++Style)
	{
		std::vector<qm_tee_trail::SQuad> vFull, vFaded;
		qm_tee_trail::BuildEffect(MakeTrail(), Style, true, 40.0, 12.0f, 9, vFull, 0.5f, false, false);
		qm_tee_trail::BuildEffect(MakeTrail(), Style, true, 40.0, 12.0f, 9, vFaded, 0.5f, false, true);
		ASSERT_GT(WeightedArea(vFaded), 0.0) << Style;
		EXPECT_LT(WeightedArea(vFaded), WeightedArea(vFull)) << Style;
	}
}

TEST(QmTeeTrailBuild, PixelsStayAxisAlignedOnAnIntegerWorldGrid)
{
	std::vector<qm_tee_trail::SQuad> vQuads;
	qm_tee_trail::BuildEffect(MakeTrail(), qm_tee_trail::STYLE_PIXEL, true, 40.0, 12.0f, 9, vQuads);
	ASSERT_FALSE(vQuads.empty());
	for(const auto &Quad : vQuads)
	{
		EXPECT_FALSE(Quad.m_Additive);
		EXPECT_EQ(Quad.m_aPos[0].y, Quad.m_aPos[1].y);
		EXPECT_EQ(Quad.m_aPos[1].x, Quad.m_aPos[2].x);
		EXPECT_EQ(Quad.m_aPos[2].y, Quad.m_aPos[3].y);
		EXPECT_EQ(Quad.m_aPos[3].x, Quad.m_aPos[0].x);
		for(int Vertex = 0; Vertex < 4; ++Vertex)
		{
			EXPECT_EQ(Quad.m_aPos[Vertex].x, std::round(Quad.m_aPos[Vertex].x));
			EXPECT_EQ(Quad.m_aPos[Vertex].y, std::round(Quad.m_aPos[Vertex].y));
			EXPECT_EQ(Quad.m_aColor[Vertex], Quad.m_aColor[0]);
		}
	}
}

TEST(QmTeeTrailBuild, PixelDepthLayersGrowFromBackToFront)
{
	std::vector<qm_tee_trail::SQuad> vQuads;
	qm_tee_trail::BuildEffect(MakeTrail(), qm_tee_trail::STYLE_PIXEL, true, 40.0, 12.0f, 9, vQuads, 1.0f, false);
	const ColorRGBA aColors[] = {ColorRGBA(0xf0cc10u), ColorRGBA(0x2090e0u), ColorRGBA(0xee3040u)};
	float aLargest[] = {0.0f, 0.0f, 0.0f};
	for(const auto &Quad : vQuads)
		for(int Layer = 0; Layer < 3; ++Layer)
			if(Quad.m_aColor[0].WithAlpha(1.0f) == aColors[Layer])
				aLargest[Layer] = std::max(aLargest[Layer], Quad.m_aPos[1].x - Quad.m_aPos[0].x);
	EXPECT_GT(aLargest[0], 0.0f);
	EXPECT_GT(aLargest[1], aLargest[0]);
	EXPECT_GT(aLargest[2], aLargest[1]);
}

TEST(QmTeeTrailBuild, ZoomDoesNotReseedOrMovePixelCells)
{
	std::vector<qm_tee_trail::SQuad> vNear, vFar;
	qm_tee_trail::BuildEffect(MakeTrail(), qm_tee_trail::STYLE_PIXEL, true, 40.0, 12.0f, 9, vNear, 0.125f);
	qm_tee_trail::BuildEffect(MakeTrail(), qm_tee_trail::STYLE_PIXEL, true, 40.0, 12.0f, 9, vFar, 4.0f);
	ASSERT_FALSE(vNear.empty());
	ASSERT_EQ(vNear.size(), vFar.size());
	for(size_t i = 0; i < vNear.size(); ++i)
		EXPECT_TRUE(SameQuad(vNear[i], vFar[i]));
}

TEST(QmTeeTrailBuild, HistoricalTickOnlyInputSupportsEveryNewStyle)
{
	auto vTrail = MakeTrail();
	for(auto &Part : vTrail)
	{
		Part.m_Time = -1.0;
		Part.m_Distance = 0.0;
	}
	for(int Style = qm_tee_trail::STYLE_MANGA; Style < qm_tee_trail::STYLE_COUNT; ++Style)
	{
		std::vector<qm_tee_trail::SQuad> vQuads;
		qm_tee_trail::BuildEffect(vTrail, Style, true, 40.0, 12.0f, 9, vQuads);
		EXPECT_FALSE(vQuads.empty()) << Style;
		EXPECT_TRUE(AllFinite(vQuads)) << Style;
	}
}

TEST(QmTeeTrailBuild, FullHistoryAndSharpReversalsStayFiniteWithinBudget)
{
	auto vTrail = MakeTrail(qm_tee_trail::MAX_POINTS + 1, 250.0);
	for(size_t i = 0; i < vTrail.size(); ++i)
	{
		const float Along = float(std::min(i, vTrail.size() - 1 - i)) * 6.0f;
		vTrail[i].m_Pos = vec2(Along, float(i % 3) * 0.1f);
		vTrail[i].m_Distance = double(vTrail.size() - 1 - i) * 6.0;
		vTrail[i].m_Life = 300.0f;
	}
	for(int Style = 0; Style < qm_tee_trail::STYLE_COUNT; ++Style)
		for(const float Width : {0.0f, 20.0f})
		{
			std::vector<qm_tee_trail::SQuad> vQuads;
			qm_tee_trail::BuildEffect(vTrail, Style, true, 250.0, Width, 9, vQuads, 0.025f);
			EXPECT_LE(vQuads.size(), qm_tee_trail::MAX_QUADS) << Style;
			EXPECT_FALSE(vQuads.empty()) << Style;
			EXPECT_TRUE(AllFinite(vQuads)) << Style;
		}
}

TEST(QmTeeTrailBuild, SamplingRateDoesNotReseedInkOrMagic)
{
	const auto vSlow = SampleMovement(1);
	const auto vFast = SampleMovement(4);
	for(const int Style : {qm_tee_trail::STYLE_MANGA, qm_tee_trail::STYLE_MAGIC})
	{
		std::vector<qm_tee_trail::SQuad> vA, vB;
		qm_tee_trail::BuildEffect(vSlow, Style, true, 40.0, 12.0f, 9, vA);
		qm_tee_trail::BuildEffect(vFast, Style, true, 40.0, 12.0f, 9, vB);
		ASSERT_FALSE(vA.empty());
		ASSERT_EQ(vA.size(), vB.size()) << Style;
		for(size_t i = 0; i < vA.size(); ++i)
			for(int Vertex = 0; Vertex < 4; ++Vertex)
			{
				EXPECT_NEAR(vA[i].m_aPos[Vertex].x, vB[i].m_aPos[Vertex].x, 0.002f) << Style;
				EXPECT_NEAR(vA[i].m_aPos[Vertex].y, vB[i].m_aPos[Vertex].y, 0.002f) << Style;
				EXPECT_NEAR(vA[i].m_aColor[Vertex].a, vB[i].m_aColor[Vertex].a, 0.0001f) << Style;
			}
	}
}
