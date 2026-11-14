#include <game/client/components/qmclient/ghost_freeze_bar.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
	std::vector<unsigned char> FrozenMask(std::initializer_list<int> Values)
	{
		std::vector<unsigned char> vMask;
		for(const int Value : Values)
			vMask.push_back(Value != 0 ? 1 : 0);
		return vMask;
	}
}

// 连续冻结帧是同一个冻结段，播放头落在段内时进度单调递减到 0。
TEST(QmGhostFreezeBar, ConsecutiveFrozenFramesFormOneSpanWithCountdownProgress)
{
	std::vector<SGhostFreezeSpan> vSpans;
	QmCollectGhostFreezeSpans({10, 12, 14, 16, 20}, FrozenMask({0, 1, 1, 1, 0}), vSpans);

	ASSERT_EQ(vSpans.size(), 1u);
	EXPECT_EQ(vSpans[0].m_StartTick, 12);
	EXPECT_EQ(vSpans[0].m_EndTick, 16);

	EXPECT_FLOAT_EQ(QmGhostFreezeProgress(vSpans, 12), 1.0f);
	EXPECT_FLOAT_EQ(QmGhostFreezeProgress(vSpans, 14), 0.5f);
	EXPECT_FLOAT_EQ(QmGhostFreezeProgress(vSpans, 16), 0.0f);
}

// 冻结→解冻→再冻结是同一次回放里的两个独立区间，区间外不画条。
TEST(QmGhostFreezeBar, SeparateFreezeRunsProduceIndependentSpans)
{
	std::vector<SGhostFreezeSpan> vSpans;
	QmCollectGhostFreezeSpans({1, 2, 3, 4, 5, 6, 7}, FrozenMask({1, 1, 0, 0, 1, 1, 1}), vSpans);

	ASSERT_EQ(vSpans.size(), 2u);
	EXPECT_EQ(vSpans[0].m_StartTick, 1);
	EXPECT_EQ(vSpans[0].m_EndTick, 2);
	EXPECT_EQ(vSpans[1].m_StartTick, 5);
	EXPECT_EQ(vSpans[1].m_EndTick, 7);

	EXPECT_FLOAT_EQ(QmGhostFreezeProgress(vSpans, 3), -1.0f);
	EXPECT_FLOAT_EQ(QmGhostFreezeProgress(vSpans, 6), 0.5f);
}

// 单帧冻结没有可信时长：保留区间但不画条，避免画出一根满格假进度。
TEST(QmGhostFreezeBar, SingleFrameFreezeIsKeptButHasNoDrawableProgress)
{
	std::vector<SGhostFreezeSpan> vSpans;
	QmCollectGhostFreezeSpans({4, 5, 6}, FrozenMask({0, 1, 0}), vSpans);

	ASSERT_EQ(vSpans.size(), 1u);
	EXPECT_EQ(vSpans[0].m_StartTick, 5);
	EXPECT_EQ(vSpans[0].m_EndTick, 5);
	EXPECT_FLOAT_EQ(QmGhostFreezeProgress(vSpans, 5), -1.0f);
}

// 轨迹段之间 tick 不连续（录制断段）：区间按帧位置跨过空档，进度仍按 tick 计算。
TEST(QmGhostFreezeBar, SparseFrameTicksKeepTheRunAsOneSpan)
{
	std::vector<SGhostFreezeSpan> vSpans;
	QmCollectGhostFreezeSpans({100, 130, 160}, FrozenMask({1, 1, 1}), vSpans);

	ASSERT_EQ(vSpans.size(), 1u);
	EXPECT_EQ(vSpans[0].m_StartTick, 100);
	EXPECT_EQ(vSpans[0].m_EndTick, 160);
	EXPECT_FLOAT_EQ(QmGhostFreezeProgress(vSpans, 130), 0.5f);
}

TEST(QmGhostFreezeBar, EmptyPathProducesNoSpans)
{
	std::vector<SGhostFreezeSpan> vSpans;
	QmCollectGhostFreezeSpans({}, {}, vSpans);
	EXPECT_TRUE(vSpans.empty());
}

// 显式标记把“明确未冻结”和旧文件的“无标记”区分开：新文件里的真实忍者帧
// 不再被当成冻结，旧文件仍保留历史判定。
TEST(QmGhostFreezeBar, ExplicitMarkerSeparatesGenuineNinjaFromFreeze)
{
	const int NotFrozen = QmEncodeGhostFreezeMarker(0);
	const int Frozen = QmEncodeGhostFreezeMarker(37);

	EXPECT_EQ(QmDecodeGhostFreezeMarker(NotFrozen), 0);
	EXPECT_EQ(QmDecodeGhostFreezeMarker(Frozen), 37);
	EXPECT_EQ(QmDecodeGhostFreezeMarker(0), -1);
	EXPECT_EQ(QmDecodeGhostFreezeMarker(12345), -1);

	EXPECT_FALSE(QmGhostFrameFrozen(NotFrozen, true));
	EXPECT_TRUE(QmGhostFrameFrozen(Frozen, false));
	EXPECT_TRUE(QmGhostFrameFrozen(0, true));
	EXPECT_FALSE(QmGhostFrameFrozen(0, false));
}

// 剩余 tick 截到 16 位，防止超长冻结污染标记位。
TEST(QmGhostFreezeBar, MarkerClampsRemainingTicksTo16Bits)
{
	EXPECT_EQ(QmDecodeGhostFreezeMarker(QmEncodeGhostFreezeMarker(70000)), 0xFFFF);
	EXPECT_EQ(QmDecodeGhostFreezeMarker(QmEncodeGhostFreezeMarker(-5)), 0);
}

// 冻结结束 tick 的三种状态：深度冻结无结束时间、正常倒计时、已过期不算冻结。
TEST(QmGhostFreezeBar, FreezeEndConvertsToRemainingTicks)
{
	EXPECT_EQ(QmGhostFreezeTicksFromEnd(0, 100), 0);
	EXPECT_EQ(QmGhostFreezeTicksFromEnd(-1, 100), 1);
	EXPECT_EQ(QmGhostFreezeTicksFromEnd(137, 100), 37);
	EXPECT_EQ(QmGhostFreezeTicksFromEnd(100, 100), 0);
	EXPECT_EQ(QmGhostFreezeTicksFromEnd(90, 100), 0);
}
