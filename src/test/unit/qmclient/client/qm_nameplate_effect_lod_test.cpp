#include <game/client/components/nameplate_text_effects.h>

#include <gtest/gtest.h>

TEST(QmNameplateEffectLod, FullDrawsCountsGlowAndBorderPasses)
{
	// 无特效：不产生任何额外绘制。
	EXPECT_EQ(QmNameplateEffectFullDraws(0, 1, 4), 0);
	// 只有辉光：每圈 4 个方向。
	EXPECT_EQ(QmNameplateEffectFullDraws(QM_TEXT_EFFECT_GLOW, 1, 4), 4 * 4);
	// 描边范围 1 不额外画圈（本体自带描边），只有范围 >1 才计入。
	EXPECT_EQ(QmNameplateEffectFullDraws(QM_TEXT_EFFECT_BORDER, 1, 0), 0);
	EXPECT_EQ(QmNameplateEffectFullDraws(QM_TEXT_EFFECT_BORDER, 3, 0), 8 * 3);
	// 辉光与描边叠加，并各自受圈数上限约束。
	EXPECT_EQ(
		QmNameplateEffectFullDraws(QM_TEXT_EFFECT_GLOW | QM_TEXT_EFFECT_BORDER, 4, 12),
		QM_NAMEPLATE_EFFECT_GLOW_DRAWS_PER_PASS * QM_NAMEPLATE_EFFECT_GLOW_MAX_PASSES +
			QM_NAMEPLATE_EFFECT_BORDER_DRAWS_PER_PASS * QM_NAMEPLATE_EFFECT_BORDER_MAX_PASSES);
}

TEST(QmNameplateEffectLod, SmoothCountIgnoresJitterAndFollowsRealChanges)
{
	constexpr int DeadZone = QM_NAMEPLATE_EFFECT_LOD_DEAD_ZONE_NAMEPLATES;
	constexpr int StepUp = QM_NAMEPLATE_EFFECT_LOD_STEP_UP_NAMEPLATES;
	constexpr int StepDown = QM_NAMEPLATE_EFFECT_LOD_STEP_DOWN_NAMEPLATES;

	// 死区内不动：屏幕上个别玩家进出不应改变档位。
	EXPECT_EQ(QmNameplateEffectLodSmoothCount(20, 22, DeadZone, StepUp, StepDown), 20);
	EXPECT_EQ(QmNameplateEffectLodSmoothCount(20, 18, DeadZone, StepUp, StepDown), 20);
	// 上升按步长快速跟随。
	EXPECT_EQ(QmNameplateEffectLodSmoothCount(10, 40, DeadZone, StepUp, StepDown), 10 + StepUp);
	// 下降按步长缓慢跟随，避免特效闪烁。
	EXPECT_EQ(QmNameplateEffectLodSmoothCount(40, 10, DeadZone, StepUp, StepDown), 40 - StepDown);
	// 目标值在步长范围内时直接到达。
	EXPECT_EQ(QmNameplateEffectLodSmoothCount(10, 13, DeadZone, StepUp, StepDown), 13);
}

TEST(QmNameplateEffectLod, IdealDrawsKeepsFullQualityUntilThreshold)
{
	constexpr int FullDraws = 48;
	constexpr int Threshold = 20;

	// 未超过阈值：一个绘制都不少。
	EXPECT_EQ(QmNameplateEffectLodIdealDraws(Threshold, FullDraws, Threshold), FullDraws);
	EXPECT_EQ(QmNameplateEffectLodIdealDraws(1, FullDraws, Threshold), FullDraws);
	// 超过阈值：按人数比例摊薄，且向上取整。
	EXPECT_EQ(QmNameplateEffectLodIdealDraws(2 * Threshold, FullDraws, Threshold), FullDraws / 2);
	EXPECT_EQ(QmNameplateEffectLodIdealDraws(3 * Threshold, FullDraws, Threshold), FullDraws / 3);
	// 人数再多也至少保留一次绘制。
	EXPECT_EQ(QmNameplateEffectLodIdealDraws(1000000, FullDraws, Threshold), 1);
	// 无效输入退回满档，不做除零或负预算。
	EXPECT_EQ(QmNameplateEffectLodIdealDraws(100, 0, Threshold), 0);
	EXPECT_EQ(QmNameplateEffectLodIdealDraws(100, FullDraws, 0), FullDraws);
}

TEST(QmNameplateEffectLod, ResolvePassesTrimsOuterLayersAndKeepsBody)
{
	// 负预算表示满档，所有圈数保留。
	const SQmNameplateEffectPasses Full = QmNameplateEffectResolvePasses(QM_TEXT_EFFECT_DRAWS_UNLIMITED, 4, 6);
	EXPECT_EQ(Full.m_BorderPasses, 4);
	EXPECT_EQ(Full.m_GlowPasses, 6);

	// 预算只够描边：辉光被全部砍掉。
	const SQmNameplateEffectPasses BorderOnly = QmNameplateEffectResolvePasses(16, 4, 6);
	EXPECT_EQ(BorderOnly.m_BorderPasses, 2);
	EXPECT_EQ(BorderOnly.m_GlowPasses, 0);

	// 预算连一圈描边都不够：先削外层辉光，再从描边余量里取辉光。
	const SQmNameplateEffectPasses GlowFirst = QmNameplateEffectResolvePasses(8, 4, 6);
	EXPECT_EQ(GlowFirst.m_BorderPasses, 1);
	EXPECT_EQ(GlowFirst.m_GlowPasses, 0);

	// 零预算时只剩本体，不产生额外绘制。
	const SQmNameplateEffectPasses BodyOnly = QmNameplateEffectResolvePasses(0, 4, 6);
	EXPECT_EQ(BodyOnly.m_BorderPasses, 0);
	EXPECT_EQ(BodyOnly.m_GlowPasses, 0);
}
