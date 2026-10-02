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

	std::vector<CTrailPart> MakePixelLine(size_t Count = 65, float Spacing = 6.0f)
	{
		auto vTrail = MakeTrail(Count, 200.0);
		for(size_t i = 0; i < Count; ++i)
		{
			vTrail[i].m_Pos = vec2(float(Count - 1 - i) * Spacing, 40.0f);
			vTrail[i].m_Distance = double(Count - 1 - i) * Spacing;
			vTrail[i].m_Time = 200.0 - double(i) * 0.1;
			vTrail[i].m_Speed = 30.0f;
			vTrail[i].m_Life = 200.0f;
		}
		return vTrail;
	}

	const qm_tee_trail::SQuad *PixelAt(const std::vector<qm_tee_trail::SQuad> &vQuads, vec2 Position, bool Additive = false)
	{
		for(const auto &Quad : vQuads)
			if(Quad.m_Additive == Additive && Position.x >= Quad.m_aPos[0].x && Position.x < Quad.m_aPos[2].x &&
				Position.y >= Quad.m_aPos[0].y && Position.y < Quad.m_aPos[2].y)
				return &Quad;
		return nullptr;
	}

	double PixelArea(const std::vector<qm_tee_trail::SQuad> &vQuads, bool Additive = false)
	{
		double Area = 0.0;
		for(const auto &Quad : vQuads)
			if(Quad.m_Additive == Additive)
				Area += double(Quad.m_aPos[2].x - Quad.m_aPos[0].x) * (Quad.m_aPos[2].y - Quad.m_aPos[0].y);
		return Area;
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

TEST(QmTeeTrailBuild, StyledEffectsBatchNormalLayersBeforeAdditiveAccents)
{
	const std::vector<CTrailPart> vTrail = MakeTrail();
	for(const int Style : {qm_tee_trail::STYLE_MANGA, qm_tee_trail::STYLE_MAGIC, qm_tee_trail::STYLE_PIXEL})
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

TEST(QmTeeTrailBuild, OriginalKeepsNormalBlending)
{
	std::vector<qm_tee_trail::SQuad> vQuads;
	qm_tee_trail::BuildEffect(MakeTrail(), qm_tee_trail::STYLE_ORIGINAL, true, 40.0, 12.0f, 9, vQuads);
	ASSERT_FALSE(vQuads.empty());
	for(const auto &Quad : vQuads)
		EXPECT_FALSE(Quad.m_Additive);
}

TEST(QmTeeTrailBuild, SeparatedSegmentsKeepAllBodiesBeforeTheGlow)
{
	auto vTrail = MakeTrail(16);
	for(size_t i = 0; i < 8; ++i)
	{
		vTrail[i].m_Segment = 1;
		vTrail[i].m_Pos.x += 1000.0f;
	}
	for(const int Style : {qm_tee_trail::STYLE_MANGA, qm_tee_trail::STYLE_MAGIC, qm_tee_trail::STYLE_PIXEL})
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
				EXPECT_FALSE(HasAdditive) << Style;
				HasNormal = true;
			}
		}
		EXPECT_TRUE(HasNormal) << Style;
		EXPECT_TRUE(HasAdditive) << Style;
	}
}

TEST(QmTeeTrailClock, SwitchingConnectionsKeepsBothPlayersHistories)
{
	qm_tee_trail::CTrailClock Clock;
	qm_tee_trail::CTrailState aStates[2];
	for(int Tick = 0; Tick <= 10; ++Tick)
	{
		const double Time = Clock.Update(100.0 + Tick, 1.0, false);
		for(int Player = 0; Player < 2; ++Player)
			aStates[Player].Update(vec2(float(Tick) * 6.0f, float(Player) * 100.0f), Time, 6.0f, 60.0f);
	}

	// 先切到落后的连接，再切回领先的连接；两条历史都不应被误判为回退或长帧。
	const double aGameTimes[] = {90.25, 130.5};
	for(int Switch = 0; Switch < 2; ++Switch)
	{
		const double Time = Clock.Update(aGameTimes[Switch], 0.25, true);
		EXPECT_DOUBLE_EQ(Time, 110.0 + (Switch + 1) * 0.25);
		for(int Player = 0; Player < 2; ++Player)
		{
			const vec2 Position(60.0f + float(Switch + 1) * 1.5f, float(Player) * 100.0f);
			aStates[Player].Update(Position, Time, 6.0f, 60.0f, qm_tee_trail::EUpdateMode::KEEP_HISTORY);
			std::vector<CTrailPart> vTrail;
			aStates[Player].Export(vTrail);
			ASSERT_GT(vTrail.size(), 10u);
			EXPECT_EQ(vTrail.front().m_Pos, Position);
			EXPECT_EQ(vTrail.back().m_Pos, vec2(0, float(Player) * 100.0f));
			EXPECT_DOUBLE_EQ(vTrail.back().m_Time, 100.0);
		}
	}
	EXPECT_DOUBLE_EQ(Clock.Update(131.5, 1.0, false), 111.5);
}

TEST(QmTeeTrailClock, RepeatedSwitchesDoNotRenewStationaryHistory)
{
	qm_tee_trail::CTrailClock Clock;
	qm_tee_trail::CTrailState State;
	for(int Tick = 0; Tick <= 8; ++Tick)
		State.Update(vec2(float(Tick) * 6.0f, 0), Clock.Update(100.0 + Tick, 1.0, false), 6.0f, 16.0f);

	for(int Frame = 1; Frame <= 80; ++Frame)
	{
		const double Time = Clock.Update(Frame % 2 ? 50.0 : 500.0, 0.25, true);
		EXPECT_DOUBLE_EQ(Time, 108.0 + Frame * 0.25);
		State.Update(vec2(48, 0), Time, 0.0f, 16.0f, qm_tee_trail::EUpdateMode::KEEP_HISTORY);
	}
	std::vector<CTrailPart> vTrail;
	State.Export(vTrail);
	EXPECT_LE(vTrail.size(), 1u);
	for(const auto &Part : vTrail)
		EXPECT_GT(Part.m_Time, 108.0);
}

TEST(QmTeeTrailClock, PausedGameDoesNotAdvanceWithRenderFrames)
{
	qm_tee_trail::CTrailClock Clock;
	EXPECT_DOUBLE_EQ(Clock.Update(50.5, 0.25, false), 50.5);
	for(int Frame = 0; Frame < 80; ++Frame)
		EXPECT_DOUBLE_EQ(Clock.Update(50.5, 0.25, false), 50.5);
	EXPECT_DOUBLE_EQ(Clock.Update(51.0, 0.25, false), 51.0);
}

TEST(QmTeeTrailClock, GameRewindStillDropsFutureHistory)
{
	qm_tee_trail::CTrailClock Clock;
	qm_tee_trail::CTrailState State;
	for(int Tick = 0; Tick <= 8; ++Tick)
		State.Update(vec2(float(Tick) * 6.0f, 0), Clock.Update(50.0 + Tick, 1.0, false), 6.0f, 40.0f);

	const double Time = Clock.Update(40.0, 0.25, false);
	State.Update(vec2(48, 0), Time, 6.0f, 40.0f);
	std::vector<CTrailPart> vTrail;
	State.Export(vTrail);
	ASSERT_EQ(vTrail.size(), 1u);
	EXPECT_DOUBLE_EQ(vTrail.front().m_Time, 40.0);

	Clock.Reset();
	EXPECT_DOUBLE_EQ(Clock.Update(5.0, 0.25, false), 5.0);
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

	// 传送、复活等显式重置仍然清掉旧轨迹。
	State.Update(vec2(5000.0f, 5000.0f), 61.0, 12.0f, 60.0f, qm_tee_trail::EUpdateMode::RESET);
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

TEST(QmTeeTrailState, ConnectionSwitchWithContinuousPositionKeepsTheSameSegment)
{
	qm_tee_trail::CTrailState State;
	for(int Tick = 0; Tick <= 8; ++Tick)
		State.Update(vec2(float(Tick) * 6.0f, 0), Tick, 6.0f, 40.0f);
	State.Update(vec2(54, 0), 9.0, 6.0f, 40.0f, qm_tee_trail::EUpdateMode::KEEP_HISTORY);
	std::vector<CTrailPart> vTrail;
	State.Export(vTrail);
	ASSERT_EQ(vTrail.size(), 10u);
	EXPECT_DOUBLE_EQ(vTrail.back().m_Time, 0.0);
	for(size_t i = 1; i < vTrail.size(); ++i)
	{
		EXPECT_EQ(vTrail[i].m_Segment, vTrail.front().m_Segment);
		EXPECT_FLOAT_EQ(distance(vTrail[i - 1].m_Pos, vTrail[i].m_Pos), qm_tee_trail::SAMPLE_SPACING);
	}
}

TEST(QmTeeTrailState, ConnectionSwitchWithPositionJumpKeepsOldGeometryUnchanged)
{
	qm_tee_trail::CTrailState State;
	for(int Tick = 0; Tick <= 8; ++Tick)
		State.Update(vec2(float(Tick) * 6.0f, 0), Tick, 6.0f, 40.0f);
	State.Update(vec2(50, 0), 8.25, 8.0f, 40.0f);
	std::vector<CTrailPart> vBefore;
	State.Export(vBefore);

	State.Update(vec2(1000, 0), 8.5, 8.0f, 40.0f, qm_tee_trail::EUpdateMode::KEEP_HISTORY);
	std::vector<CTrailPart> vAfter;
	State.Export(vAfter);
	ASSERT_EQ(vAfter.size(), vBefore.size() + 1);
	EXPECT_NE(vAfter[0].m_Segment, vAfter[1].m_Segment);
	for(size_t i = 0; i < vBefore.size(); ++i)
	{
		EXPECT_EQ(vAfter[i + 1].m_Pos, vBefore[i].m_Pos);
		EXPECT_DOUBLE_EQ(vAfter[i + 1].m_Time, vBefore[i].m_Time);
		EXPECT_DOUBLE_EQ(vAfter[i + 1].m_Distance, vBefore[i].m_Distance);
	}
	for(int Style = 0; Style < qm_tee_trail::STYLE_COUNT; ++Style)
	{
		std::vector<qm_tee_trail::SQuad> vExpected, vActual;
		qm_tee_trail::BuildEffect(vBefore, Style, true, 8.5, 8.0f, 17, vExpected);
		qm_tee_trail::BuildEffect(vAfter, Style, true, 8.5, 8.0f, 17, vActual);
		ASSERT_FALSE(vExpected.empty()) << Style;
		ASSERT_EQ(vActual.size(), vExpected.size()) << Style;
		for(size_t i = 0; i < vExpected.size(); ++i)
			EXPECT_TRUE(SameQuad(vActual[i], vExpected[i])) << Style << ":" << i;
	}
}

TEST(QmTeeTrailState, MovementAfterConnectionSwitchDoesNotBridgeThePositionJump)
{
	qm_tee_trail::CTrailState State;
	for(int Tick = 0; Tick <= 8; ++Tick)
		State.Update(vec2(float(Tick) * 6.0f, 0), Tick, 6.0f, 40.0f);
	State.Update(vec2(1000, 0), 9.0, 6.0f, 40.0f, qm_tee_trail::EUpdateMode::KEEP_HISTORY);
	for(int Tick = 1; Tick <= 4; ++Tick)
		State.Update(vec2(1000.0f + float(Tick) * 6.0f, 0), 9.0 + Tick, 6.0f, 40.0f);
	std::vector<CTrailPart> vTrail;
	State.Export(vTrail);
	for(int Style = 0; Style < qm_tee_trail::STYLE_COUNT; ++Style)
	{
		std::vector<qm_tee_trail::SQuad> vQuads;
		qm_tee_trail::BuildEffect(vTrail, Style, true, 13.0, 8.0f, 17, vQuads);
		bool OldVisible = false, NewVisible = false;
		for(const auto &Quad : vQuads)
		{
			float MinX = Quad.m_aPos[0].x, MaxX = MinX;
			for(const auto &Pos : Quad.m_aPos)
			{
				MinX = std::min(MinX, Pos.x);
				MaxX = std::max(MaxX, Pos.x);
			}
			EXPECT_TRUE(MaxX < 200.0f || MinX > 800.0f) << Style;
			OldVisible |= MaxX < 200.0f;
			NewVisible |= MinX > 800.0f;
		}
		EXPECT_TRUE(OldVisible) << Style;
		EXPECT_TRUE(NewVisible) << Style;
	}
}

TEST(QmTeeTrailState, OlderSegmentCanOutliveTheNewHead)
{
	qm_tee_trail::CTrailState State;
	for(int Tick = 0; Tick <= 8; ++Tick)
		State.Update(vec2(float(Tick) * 6.0f, 0), Tick, 6.0f, 40.0f);
	State.Update(vec2(1000, 0), 9.0, 0.0f, 1.0f, qm_tee_trail::EUpdateMode::KEEP_HISTORY);
	State.Update(vec2(1000, 0), 11.0, 0.0f, 1.0f);
	std::vector<CTrailPart> vTrail;
	State.Export(vTrail);
	ASSERT_GT(vTrail.size(), 2u);
	for(int Style = 0; Style < qm_tee_trail::STYLE_COUNT; ++Style)
	{
		std::vector<qm_tee_trail::SQuad> vQuads;
		qm_tee_trail::BuildEffect(vTrail, Style, true, 11.0, 8.0f, 17, vQuads);
		ASSERT_FALSE(vQuads.empty()) << Style;
		for(const auto &Quad : vQuads)
			for(const auto &Pos : Quad.m_aPos)
				EXPECT_LT(Pos.x, 200.0f) << Style;
	}
}

TEST(QmTeeTrailState, RepeatedConnectionSegmentsStayWithinCapacity)
{
	qm_tee_trail::CTrailState State;
	for(int Tick = 0; Tick < 600; ++Tick)
	{
		const float X = float((Tick / 4) % 2) * 1000.0f + float(Tick % 4) * 6.0f;
		const auto Mode = Tick % 4 == 0 ? qm_tee_trail::EUpdateMode::KEEP_HISTORY : qm_tee_trail::EUpdateMode::NORMAL;
		State.Update(vec2(X, 0), Tick, 6.0f, 400.0f, Mode);
	}
	std::vector<CTrailPart> vTrail;
	State.Export(vTrail);
	EXPECT_LE(vTrail.size(), qm_tee_trail::MAX_POINTS + 1);
	EXPECT_GT(vTrail.size(), 100u);
	for(int Style = 0; Style < qm_tee_trail::STYLE_COUNT; ++Style)
	{
		std::vector<qm_tee_trail::SQuad> vQuads;
		qm_tee_trail::BuildEffect(vTrail, Style, true, 599.0, 20.0f, 17, vQuads);
		EXPECT_LE(vQuads.size(), qm_tee_trail::MAX_QUADS) << Style;
		EXPECT_TRUE(AllFinite(vQuads)) << Style;
	}
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

TEST(QmTeeTrailPixel, CellsStayAxisAlignedAndFlatColoredOnAnIntegerWorldGrid)
{
	std::vector<qm_tee_trail::SQuad> vQuads;
	qm_tee_trail::BuildEffect(MakeTrail(), qm_tee_trail::STYLE_PIXEL, true, 40.0, 12.0f, 9, vQuads);
	ASSERT_FALSE(vQuads.empty());
	for(const auto &Quad : vQuads)
	{
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

TEST(QmTeeTrailPixel, DenseBodyUsesTheFullTrailWidth)
{
	std::vector<qm_tee_trail::SQuad> vQuads;
	qm_tee_trail::BuildEffect(MakePixelLine(), qm_tee_trail::STYLE_PIXEL, true, 200.0, 20.0f, 9, vQuads, 1.0f, false);
	int CoreHits = 0, WideSections = 0;
	for(int X = 200; X < 340; ++X)
	{
		CoreHits += PixelAt(vQuads, vec2(X + 0.5f, 40.5f)) != nullptr;
		int Occupied = 0;
		for(int Y = 15; Y < 65; ++Y)
			Occupied += PixelAt(vQuads, vec2(X + 0.5f, Y + 0.5f)) != nullptr;
		WideSections += Occupied >= 30;
	}
	EXPECT_EQ(CoreHits, 140);
	EXPECT_GT(WideSections, 100);
}

TEST(QmTeeTrailPixel, GlowUsesDiscreteBrightCoreColors)
{
	std::vector<qm_tee_trail::SQuad> vQuads;
	qm_tee_trail::BuildEffect(MakePixelLine(), qm_tee_trail::STYLE_PIXEL, true, 200.0, 20.0f, 9, vQuads, 1.0f, false);
	std::vector<ColorRGBA> vColors;
	float Darkest = 1.0f, Brightest = 0.0f;
	for(const auto &Quad : vQuads)
	{
		const auto Color = Quad.m_aColor[0].WithAlpha(1.0f);
		if(std::find(vColors.begin(), vColors.end(), Color) == vColors.end())
			vColors.push_back(Color);
		const float Light = (Color.r + Color.g + Color.b) / 3.0f;
		Darkest = std::min(Darkest, Light);
		Brightest = std::max(Brightest, Light);
		if(Quad.m_Additive)
		{
			EXPECT_GT(Light, 0.7f);
			EXPECT_NE(PixelAt(vQuads, (Quad.m_aPos[0] + Quad.m_aPos[2]) * 0.5f), nullptr);
		}
	}
	EXPECT_GE(vColors.size(), 4u);
	EXPECT_LE(vColors.size(), 5u);
	EXPECT_GT(Brightest, 0.9f);
	EXPECT_LT(Darkest, 0.4f);
	EXPECT_GT(PixelArea(vQuads, true), 0.0);
	EXPECT_LT(PixelArea(vQuads, true), PixelArea(vQuads));
}

TEST(QmTeeTrailPixel, TailBreaksIntoSeparatedClustersAlongThePath)
{
	std::vector<qm_tee_trail::SQuad> vQuads;
	qm_tee_trail::BuildEffect(MakePixelLine(), qm_tee_trail::STYLE_PIXEL, true, 200.0, 20.0f, 9, vQuads, 1.0f, false);
	// 碎片是二维结构：中心扫描线可能只穿过主体而错过两侧独立簇。
	constexpr int Columns = 24, Rows = 10;
	std::array<bool, Columns * Rows> aOccupied{};
	int Occupied = 0;
	for(int Y = 0; Y < Rows; ++Y)
		for(int X = 0; X < Columns; ++X)
		{
			const bool Current = PixelAt(vQuads, vec2(X * 5.0f + 0.5f, Y * 5.0f + 15.5f)) != nullptr;
			aOccupied[Y * Columns + X] = Current;
			Occupied += Current;
		}
	int Clusters = 0;
	std::vector<int> vPending;
	for(int Index = 0; Index < Columns * Rows; ++Index)
	{
		if(!aOccupied[Index])
			continue;
		++Clusters;
		aOccupied[Index] = false;
		vPending.push_back(Index);
		while(!vPending.empty())
		{
			const int Current = vPending.back();
			vPending.pop_back();
			const int X = Current % Columns, Y = Current / Columns;
			for(const ivec2 Direction : {ivec2(-1, 0), ivec2(1, 0), ivec2(0, -1), ivec2(0, 1)})
			{
				const int NextX = X + Direction.x, NextY = Y + Direction.y;
				if(NextX < 0 || NextX >= Columns || NextY < 0 || NextY >= Rows)
					continue;
				const int Next = NextY * Columns + NextX;
				if(aOccupied[Next])
				{
					aOccupied[Next] = false;
					vPending.push_back(Next);
				}
			}
		}
	}
	EXPECT_GE(Clusters, 2);
	EXPECT_GT(Occupied, 0);
	EXPECT_LT(Occupied, Columns * Rows);
}

TEST(QmTeeTrailPixel, LessEnergyReducesOccupiedBodyAndGlowingCore)
{
	auto vTrail = MakePixelLine();
	std::vector<qm_tee_trail::SQuad> vFast, vSlow;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_PIXEL, true, 200.0, 15.0f, 9, vFast, 1.0f, false);
	for(auto &Part : vTrail)
		Part.m_Speed = 1.0f;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_PIXEL, true, 200.0, 15.0f, 9, vSlow, 1.0f, false);
	EXPECT_GT(PixelArea(vSlow), 0.0);
	EXPECT_LT(PixelArea(vSlow), PixelArea(vFast));
	EXPECT_LT(PixelArea(vSlow, true), PixelArea(vFast, true));
}

TEST(QmTeeTrailPixel, CustomHueAndBrightnessAlsoControlTheGlow)
{
	auto vTrail = MakePixelLine();
	std::vector<qm_tee_trail::SQuad> vBright, vDark;
	for(auto &Part : vTrail)
		Part.m_Col = ColorRGBA(0.1f, 0.2f, 0.9f, 0.8f);
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_PIXEL, false, 200.0, 15.0f, 9, vBright, 1.0f, false);
	for(auto &Part : vTrail)
		Part.m_Col = ColorRGBA(0.015f, 0.03f, 0.135f, 0.8f);
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_PIXEL, false, 200.0, 15.0f, 9, vDark, 1.0f, false);
	ASSERT_FALSE(vBright.empty());
	ASSERT_FALSE(vDark.empty());
	for(const auto *pQuads : {&vBright, &vDark})
		for(const auto &Quad : *pQuads)
		{
			const auto &Color = Quad.m_aColor[0];
			EXPECT_GE(Color.b, Color.r);
			EXPECT_GE(Color.b, Color.g);
			EXPECT_LE(Color.b, pQuads == &vDark ? 0.1351f : 0.9001f);
		}
}

TEST(QmTeeTrailPixel, SourceOpacityScalesBodyAndGlowTogether)
{
	auto vTrail = MakePixelLine();
	std::vector<qm_tee_trail::SQuad> vOpaque, vFaint;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_PIXEL, true, 200.0, 15.0f, 9, vOpaque, 1.0f, false);
	for(auto &Part : vTrail)
		Part.m_Col.a *= 0.25f;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_PIXEL, true, 200.0, 15.0f, 9, vFaint, 1.0f, false);
	for(const bool Additive : {false, true})
		for(const float X : {220.5f, 260.5f, 300.5f, 340.5f})
		{
			const auto *pOpaque = PixelAt(vOpaque, vec2(X, 40.5f), Additive);
			const auto *pFaint = PixelAt(vFaint, vec2(X, 40.5f), Additive);
			ASSERT_NE(pOpaque, nullptr);
			ASSERT_NE(pFaint, nullptr);
			EXPECT_NEAR(pFaint->m_aColor[0].a, pOpaque->m_aColor[0].a * 0.25f, 0.00001f);
		}
}

TEST(QmTeeTrailPixel, SourceColorsStillChangeAlongTheTrajectory)
{
	auto vTrail = MakePixelLine();
	for(auto &Part : vTrail)
		Part.m_Col = Part.m_Pos.x >= 192.0f ? ColorRGBA(0.9f, 0.2f, 0.1f, 0.8f) : ColorRGBA(0.1f, 0.2f, 0.9f, 0.8f);
	std::vector<qm_tee_trail::SQuad> vQuads;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_PIXEL, false, 200.0, 15.0f, 9, vQuads, 1.0f, false);
	const auto *pNewer = PixelAt(vQuads, vec2(260.5f, 40.5f));
	const auto *pOlder = PixelAt(vQuads, vec2(140.5f, 40.5f));
	ASSERT_NE(pNewer, nullptr);
	ASSERT_NE(pOlder, nullptr);
	EXPECT_GT(pNewer->m_aColor[0].r, pNewer->m_aColor[0].b);
	EXPECT_GT(pOlder->m_aColor[0].b, pOlder->m_aColor[0].r);
}

TEST(QmTeeTrailPixel, AgingFragmentsStayWithinOneCellOfTheirExistingTrail)
{
	std::vector<qm_tee_trail::SQuad> vFresh, vOld;
	qm_tee_trail::BuildEffect(MakePixelLine(), qm_tee_trail::STYLE_PIXEL, true, 200.0, 15.0f, 9, vFresh, 1.0f, false);
	qm_tee_trail::BuildEffect(MakePixelLine(), qm_tee_trail::STYLE_PIXEL, true, 260.0, 15.0f, 9, vOld, 1.0f, false);
	ASSERT_GT(PixelArea(vOld), 0.0);
	EXPECT_LT(PixelArea(vOld), PixelArea(vFresh));
	for(const auto &Quad : vOld)
		if(!Quad.m_Additive)
			for(float X = Quad.m_aPos[0].x + 0.5f; X < Quad.m_aPos[2].x; X += 1.0f)
			{
				const vec2 Position(X, Quad.m_aPos[0].y + 0.5f);
				bool NearExisting = false;
				for(int Dx = -1; Dx <= 1; ++Dx)
					for(int Dy = -1; Dy <= 1; ++Dy)
						NearExisting |= PixelAt(vFresh, Position + vec2(Dx * 4.0f, Dy * 4.0f)) != nullptr;
				EXPECT_TRUE(NearExisting);
			}
}

TEST(QmTeeTrailPixel, SubTickAgingKeepsThePixelStructureStable)
{
	std::vector<qm_tee_trail::SQuad> vFirst, vNext;
	qm_tee_trail::BuildEffect(MakePixelLine(), qm_tee_trail::STYLE_PIXEL, true, 200.0, 15.0f, 9, vFirst, 1.0f, false);
	qm_tee_trail::BuildEffect(MakePixelLine(), qm_tee_trail::STYLE_PIXEL, true, 200.001, 15.0f, 9, vNext, 1.0f, false);
	ASSERT_GT(PixelArea(vFirst), 0.0);
	EXPECT_GE(PixelArea(vNext), PixelArea(vFirst) * 0.99);
	double SharedArea = 0.0;
	for(const auto &Quad : vNext)
		if(!Quad.m_Additive)
		{
			const vec2 Size = Quad.m_aPos[2] - Quad.m_aPos[0];
			const float Step = std::min(Size.x, Size.y);
			ASSERT_GT(Step, 0.0f);
			for(float X = Quad.m_aPos[0].x + Step * 0.5f; X < Quad.m_aPos[2].x; X += Step)
				for(float Y = Quad.m_aPos[0].y + Step * 0.5f; Y < Quad.m_aPos[2].y; Y += Step)
					if(PixelAt(vFirst, vec2(X, Y)))
						SharedArea += Step * Step;
		}
	EXPECT_GE(SharedArea, PixelArea(vNext) * 0.99);
}

TEST(QmTeeTrailPixel, AdvancingTheHeadPreservesExistingPixelClusters)
{
	qm_tee_trail::CTrailState State;
	for(int Tick = 0; Tick <= 40; ++Tick)
		State.Update(vec2(Tick * 8.0f, 40.0f), Tick, 8.0f, 80.0f);
	std::vector<CTrailPart> vBefore, vAfter;
	State.Export(vBefore);
	State.Update(vec2(321.0f, 40.0f), 40.125, 8.0f, 80.0f);
	State.Export(vAfter);
	std::vector<qm_tee_trail::SQuad> vFirst, vNext;
	qm_tee_trail::BuildEffect(vBefore, qm_tee_trail::STYLE_PIXEL, true, 40.0, 15.0f, 9, vFirst, 1.0f, false);
	qm_tee_trail::BuildEffect(vAfter, qm_tee_trail::STYLE_PIXEL, true, 40.125, 15.0f, 9, vNext, 1.0f, false);
	int Compared = 0, Retained = 0;
	for(float X = 80.5f; X < 280.0f; X += 2.0f)
		for(float Y = 20.5f; Y < 60.0f; Y += 2.0f)
			if(PixelAt(vFirst, vec2(X, Y)))
			{
				++Compared;
				Retained += PixelAt(vNext, vec2(X, Y)) != nullptr;
			}
	ASSERT_GT(Compared, 200);
	EXPECT_GE(Retained, Compared * 0.96f);
}

TEST(QmTeeTrailPixel, SelfCrossingsDoNotStackPixelsWithinEitherPass)
{
	auto vTrail = MakePixelLine();
	for(size_t i = 0; i < vTrail.size(); ++i)
	{
		const float Phase = float(i) / float(vTrail.size() - 1) * 6.2831853f;
		vTrail[i].m_Pos = vec2(std::sin(Phase) * 90.0f, std::sin(Phase * 2.0f) * 60.0f);
	}
	std::vector<qm_tee_trail::SQuad> vQuads;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_PIXEL, true, 200.0, 20.0f, 9, vQuads, 1.0f, false);
	ASSERT_FALSE(vQuads.empty());
	for(size_t i = 0; i < vQuads.size(); ++i)
		for(size_t j = i + 1; j < vQuads.size(); ++j)
		{
			const auto &A = vQuads[i], &B = vQuads[j];
			if(A.m_Additive != B.m_Additive)
				continue;
			const bool Overlap = A.m_aPos[0].x < B.m_aPos[2].x && B.m_aPos[0].x < A.m_aPos[2].x &&
					     A.m_aPos[0].y < B.m_aPos[2].y && B.m_aPos[0].y < A.m_aPos[2].y;
			ASSERT_FALSE(Overlap) << i << ", " << j;
		}
}

TEST(QmTeeTrailPixel, FullDiagonalHistoryKeepsTheTailInsideTheQuadBudget)
{
	auto vTrail = MakePixelLine(qm_tee_trail::MAX_POINTS + 1);
	for(auto &Part : vTrail)
		Part.m_Pos = vec2(Part.m_Pos.x * 0.70710678f, Part.m_Pos.x * 0.70710678f);
	std::vector<qm_tee_trail::SQuad> vQuads;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_PIXEL, true, 200.0, 20.0f, 9, vQuads, 1.0f, false);
	ASSERT_FALSE(vQuads.empty());
	EXPECT_LE(vQuads.size(), qm_tee_trail::MAX_QUADS);
	EXPECT_TRUE(AllFinite(vQuads));
	float TailX = vTrail.front().m_Pos.x;
	for(const auto &Quad : vQuads)
		TailX = std::min(TailX, Quad.m_aPos[0].x);
	EXPECT_LT(TailX, vTrail.front().m_Pos.x * 0.15f);
}

TEST(QmTeeTrailPixel, OverlongInputReducesResolutionWithoutTruncatingTheTail)
{
	const auto vTrail = MakePixelLine(qm_tee_trail::MAX_POINTS + 1, 120.0f);
	std::vector<qm_tee_trail::SQuad> vQuads;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_PIXEL, true, 200.0, 20.0f, 9, vQuads, 1.0f, false);
	ASSERT_FALSE(vQuads.empty());
	EXPECT_LE(vQuads.size(), qm_tee_trail::MAX_QUADS);
	EXPECT_TRUE(AllFinite(vQuads));
	float TailX = vTrail.front().m_Pos.x;
	for(const auto &Quad : vQuads)
		TailX = std::min(TailX, Quad.m_aPos[0].x);
	EXPECT_LT(TailX, vTrail.front().m_Pos.x * 0.20f);
}

TEST(QmTeeTrailPixel, ZoomDoesNotReseedOrMovePixelCells)
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

TEST(QmTeeTrailBuild, SamplingRateDoesNotReseedStyledGeometry)
{
	const auto vSlow = SampleMovement(1);
	const auto vFast = SampleMovement(4);
	for(const int Style : {qm_tee_trail::STYLE_MANGA, qm_tee_trail::STYLE_MAGIC, qm_tee_trail::STYLE_PIXEL})
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
