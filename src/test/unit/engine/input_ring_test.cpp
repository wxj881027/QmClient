#include <engine/client/input_ring.h>

#include <gtest/gtest.h>

namespace
{
	struct SInputRecord
	{
		int m_Tick = -1;
		int m_aData[2] = {0, 0};
	};
}

// 输入环按发送序号写入，槽位与 tick 没有对应关系。取某个 tick 的输入必须扫描整环。
TEST(QmInputRing, FindsTheLatestRecordNotAfterTheRequestedTick)
{
	SInputRecord aRing[8];
	aRing[3] = {106, {106, 0}};
	aRing[5] = {112, {112, 0}};
	aRing[1] = {100, {100, 0}};

	const SInputRecord *pRecord = QmLatestInputRecord(aRing, 108);
	ASSERT_NE(pRecord, nullptr);
	EXPECT_EQ(pRecord->m_Tick, 106);
	EXPECT_EQ(pRecord->m_aData[0], 106);

	pRecord = QmLatestInputRecord(aRing, 200);
	ASSERT_NE(pRecord, nullptr);
	EXPECT_EQ(pRecord->m_Tick, 112);
}

// 回归：记录槽位不等于 tick % 容量时仍然能取到。按 tick 取模定位几乎总是落空，
// 快速练习因此拿不到跳跃/钩子输入，只能靠偶发命中的旧记录。
TEST(QmInputRing, FindsRecordsWhoseSlotDoesNotMatchTheirTick)
{
	SInputRecord aRing[8];
	aRing[2] = {207, {207, 0}};
	ASSERT_NE(207 % 8, 2);

	const SInputRecord *pRecord = QmLatestInputRecord(aRing, 207);
	ASSERT_NE(pRecord, nullptr);
	EXPECT_EQ(pRecord->m_Tick, 207);
}

TEST(QmInputRing, ReturnsNothingBeforeTheOldestRecordOrOnAnEmptyRing)
{
	SInputRecord aRing[8];
	aRing[4] = {300, {300, 0}};

	EXPECT_EQ(QmLatestInputRecord(aRing, 299), nullptr);

	SInputRecord aEmptyRing[8];
	EXPECT_EQ(QmLatestInputRecord(aEmptyRing, 1000), nullptr);
}

// 多 tick 追赶时同一记录会被连续消费：这是保持按键按住的正常语义，不是重复触发。
TEST(QmInputRing, KeepsReturningTheSameRecordForTicksBeyondIt)
{
	SInputRecord aRing[8];
	aRing[0] = {500, {500, 0}};

	for(int Tick = 500; Tick <= 505; Tick++)
	{
		const SInputRecord *pRecord = QmLatestInputRecord(aRing, Tick);
		ASSERT_NE(pRecord, nullptr) << "tick " << Tick;
		EXPECT_EQ(pRecord->m_Tick, 500) << "tick " << Tick;
	}
}
