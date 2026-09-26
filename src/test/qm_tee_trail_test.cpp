#include <game/client/components/tclient/qm_tee_trail.h>
#include <game/client/components/tclient/trails.h>

#include <gtest/gtest.h>

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
			vTrail[i].m_Distance = double(i) * 19.0;
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
					!std::isfinite(Quad.m_aColor[i].r) || !std::isfinite(Quad.m_aColor[i].a))
					return false;
		return true;
	}

} // namespace

TEST(QmTeeTrailStyle, UnknownStyleValuesFallBackToOriginal)
{
	EXPECT_EQ(qm_tee_trail::ResolveStyle(qm_tee_trail::STYLE_ORIGINAL), qm_tee_trail::STYLE_ORIGINAL);
	EXPECT_EQ(qm_tee_trail::ResolveStyle(qm_tee_trail::STYLE_INFERNO), qm_tee_trail::STYLE_INFERNO);
	// 配置里可能残留越界或负数：必须退回原版，而不是索引到样式表之外。
	EXPECT_EQ(qm_tee_trail::ResolveStyle(-1), qm_tee_trail::STYLE_ORIGINAL);
	EXPECT_EQ(qm_tee_trail::ResolveStyle(qm_tee_trail::STYLE_COUNT), qm_tee_trail::STYLE_ORIGINAL);
	EXPECT_EQ(qm_tee_trail::ResolveStyle(99), qm_tee_trail::STYLE_ORIGINAL);
}

TEST(QmTeeTrailStyle, LifetimeClampsLengthAndGrowsWithSpeed)
{
	const int Style = qm_tee_trail::STYLE_EXO;
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
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_INFERNO, false, 40.0, 9.0f, 42, vFirst);
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_INFERNO, false, 40.0, 9.0f, 42, vSecond);
	ASSERT_EQ(vFirst.size(), vSecond.size());
	for(size_t i = 0; i < vFirst.size(); ++i)
		EXPECT_TRUE(SameQuad(vFirst[i], vSecond[i])) << "quad " << i;
}

TEST(QmTeeTrailBuild, DifferentSeedsProduceDifferentGeometry)
{
	const std::vector<CTrailPart> vTrail = MakeTrail();
	std::vector<qm_tee_trail::SQuad> vFirst;
	std::vector<qm_tee_trail::SQuad> vSecond;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_SPIRIT, false, 40.0, 9.0f, 1, vFirst);
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_SPIRIT, false, 40.0, 9.0f, 2, vSecond);
	ASSERT_EQ(vFirst.size(), vSecond.size());
	// 种子按玩家区分：相同输入也应给出不同扰动，否则所有玩家拖尾完全重合。
	bool AnyDifferent = false;
	for(size_t i = 0; i < vFirst.size() && !AnyDifferent; ++i)
		AnyDifferent = !SameQuad(vFirst[i], vSecond[i]);
	EXPECT_TRUE(AnyDifferent);
}

TEST(QmTeeTrailBuild, RejectsInputItCannotDrawSafely)
{
	std::vector<qm_tee_trail::SQuad> vQuads;

	// 少于两个点。
	qm_tee_trail::BuildEffect(MakeTrail(1), qm_tee_trail::STYLE_EXO, true, 40.0, 4.0f, 1, vQuads);
	EXPECT_TRUE(vQuads.empty());

	// 所有点重合，总长度接近零。
	std::vector<CTrailPart> vStationary = MakeTrail();
	for(CTrailPart &Part : vStationary)
		Part.m_Pos = vec2(0.0f, 0.0f);
	qm_tee_trail::BuildEffect(vStationary, qm_tee_trail::STYLE_EXO, true, 40.0, 4.0f, 1, vQuads);
	EXPECT_TRUE(vQuads.empty());

	// 相邻点跳变过大视为传送，不能画出一条跨越全图的假轨迹。
	std::vector<CTrailPart> vTeleport = MakeTrail();
	vTeleport[3].m_Pos += vec2(400.0f, 0.0f);
	qm_tee_trail::BuildEffect(vTeleport, qm_tee_trail::STYLE_EXO, true, 40.0, 4.0f, 1, vQuads);
	EXPECT_TRUE(vQuads.empty());

	// 时间戳晚于当前时间：说明采样来自未来帧，拒绝而不是画出未定义形状。
	std::vector<CTrailPart> vFuture = MakeTrail();
	vFuture[0].m_Time = 1000.0;
	qm_tee_trail::BuildEffect(vFuture, qm_tee_trail::STYLE_EXO, true, 40.0, 4.0f, 1, vQuads);
	EXPECT_TRUE(vQuads.empty());

	// 非有限输入。
	std::vector<CTrailPart> vNan = MakeTrail();
	vNan[2].m_Pos = vec2(std::numeric_limits<float>::quiet_NaN(), 0.0f);
	qm_tee_trail::BuildEffect(vNan, qm_tee_trail::STYLE_EXO, true, 40.0, 4.0f, 1, vQuads);
	EXPECT_TRUE(vQuads.empty());

	// 非有限的线宽和像素尺寸同样不能进入几何构建。
	qm_tee_trail::BuildEffect(MakeTrail(), qm_tee_trail::STYLE_EXO, true, 40.0, std::numeric_limits<float>::quiet_NaN(), 1, vQuads);
	EXPECT_TRUE(vQuads.empty());
}

TEST(QmTeeTrailBuild, PresetPaletteAndTrailColorsDiffer)
{
	const std::vector<CTrailPart> vTrail = MakeTrail();
	std::vector<qm_tee_trail::SQuad> vPreset;
	std::vector<qm_tee_trail::SQuad> vTrailColors;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_EXO, true, 40.0, 6.0f, 9, vPreset);
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_EXO, false, 40.0, 6.0f, 9, vTrailColors);
	ASSERT_FALSE(vPreset.empty());
	ASSERT_FALSE(vTrailColors.empty());
	ASSERT_EQ(vPreset.size(), vTrailColors.size());
	bool AnyDifferent = false;
	for(size_t i = 0; i < vPreset.size() && !AnyDifferent; ++i)
		AnyDifferent = !SameQuad(vPreset[i], vTrailColors[i]);
	EXPECT_TRUE(AnyDifferent);
}

TEST(QmTeeTrailBuild, StyleLayersSplitAdditiveAndNormalQuads)
{
	const std::vector<CTrailPart> vTrail = MakeTrail();

	// 暗色火焰类样式：主体两层走普通混合，发光层走加法混合，渲染端据此切换。
	std::vector<qm_tee_trail::SQuad> vMixed;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_BLACK_FLASH, true, 40.0, 6.0f, 9, vMixed);
	bool HasAdditive = false, HasNormal = false;
	for(const auto &Quad : vMixed)
		(Quad.m_Additive ? HasAdditive : HasNormal) = true;
	EXPECT_TRUE(HasAdditive);
	EXPECT_TRUE(HasNormal);

	// 整体发光的样式（Exo）连主体都走加法混合。
	std::vector<qm_tee_trail::SQuad> vGlow;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_EXO, true, 40.0, 6.0f, 9, vGlow);
	ASSERT_FALSE(vGlow.empty());
	for(const auto &Quad : vGlow)
		EXPECT_TRUE(Quad.m_Additive);

	// 原版只有普通混合的主体层。
	std::vector<qm_tee_trail::SQuad> vOriginal;
	qm_tee_trail::BuildEffect(vTrail, qm_tee_trail::STYLE_ORIGINAL, true, 40.0, 6.0f, 9, vOriginal);
	ASSERT_FALSE(vOriginal.empty());
	for(const auto &Quad : vOriginal)
		EXPECT_FALSE(Quad.m_Additive);
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
