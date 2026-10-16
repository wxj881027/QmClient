// 灵动岛状态区连接质量图标（数据流量语义）的常驻占位几何。
// 方案 A 根因回归：图标此前嵌在信息栈分支内，普通对局没有信息栈
// （无本地时钟、无冰冻摘要）时整段不渲染，图标三轮修复仍不可见。
#include <game/client/components/hud_media_island_logic.h>
#include <game/client/qm_icon.h>

#include <gtest/gtest.h>

// 无信息栈（普通对局、无冰冻）时状态区占位仍必须包含图标宽度：
// 若生产布局退回「ShowInfoStack 才算图标」，此测试失败。
TEST(QmHudConnectionSignalSlot, ReserveWidthFollowsStatusSectionNotInfoStack)
{
	const SHudConnectionSignalSlot Slot = QmHudConnectionSignalSlot();
	// 占位与信息栈文本字号解绑：按胶囊高度取固定比例，单双行文本切换不改图标大小。
	const SHudConnectionSignalSlot Again = QmHudConnectionSignalSlot();
	EXPECT_FLOAT_EQ(Again.m_Size, Slot.m_Size);
	EXPECT_GT(Slot.m_Size, 0.0f);
	EXPECT_GT(Slot.m_Width, Slot.m_Size);
	// 图标占位为灵动岛胶囊高度的 0.7 倍（16 设计高 × 0.7 缩放）：11.2/16 = 0.7。
	// 用户反馈原 8.4（半高）偏小，放大后最高档墨迹（1.5×0.5625 方框字号）
	// 仍完整落在方框内。
	EXPECT_FLOAT_EQ(Slot.m_Size, QmHudMediaIslandScaled(11.2f));
	EXPECT_FLOAT_EQ(Slot.m_Size, QmHudMediaIslandBaseHeight * 0.7f * QmHudMediaIslandDesignScale);
	// 放大后的最高档墨迹不能超出占位方框，否则字形被误解为越界。
	EXPECT_LE(QmSignalIconMaxVisualSize(Slot.m_Size), Slot.m_Size);
	// 录制路径状态区宽度 = 左 padding + 图标占位 + 折叠/展开宽度，
	// 图标在无信息栈时也占据真实宽度（布局侧同口径）。
	const float Collapsed = QmHudMediaIslandScaled(4.0f) + Slot.m_Width + (QmHudMediaIslandScaled(4.0f) + QmHudMediaIslandScaled(5.0f) + QmHudMediaIslandScaled(5.0f));
	EXPECT_FLOAT_EQ(Collapsed, QmHudMediaIslandScaled(4.0f) + Slot.m_Width + QmHudMediaIslandScaled(14.0f));
}

// 信号图标在胶囊内垂直居中：方框下移量 = (胶囊高 - 方框高)/2，
// 方框中心与状态区文本中线（胶囊中心）对齐；用户反馈原贴顶绘制偏高。
TEST(QmHudConnectionSignalSlot, IconBoxIsVerticallyCenteredInIsland)
{
	const SHudConnectionSignalSlot Slot = QmHudConnectionSignalSlot();
	const float IslandHeight = QmHudMediaIslandScaled(QmHudMediaIslandBaseHeight);
	// 占位方框小于胶囊，留出上下对称的居中边距。
	EXPECT_LT(Slot.m_Size, IslandHeight);
	const float CenteredY = QmHudConnectionSignalIconY(0.0f, Slot.m_Size, 0);
	const float ExpectedOffset = (IslandHeight - Slot.m_Size) * 0.5f;
	EXPECT_FLOAT_EQ(CenteredY, ExpectedOffset);
	// 方框中心落在胶囊中线上。
	EXPECT_FLOAT_EQ(CenteredY + Slot.m_Size * 0.5f, IslandHeight * 0.5f);
	// 档位不影响居中位置（四档共用同一垂直中心）。
	for(int Tier = 0; Tier <= 3; ++Tier)
		EXPECT_FLOAT_EQ(QmHudConnectionSignalIconY(0.0f, Slot.m_Size, Tier), ExpectedOffset);
	// 非零 BoxY 平移一致，且方框不出胶囊底。
	const float BoxY = 37.5f;
	const float ShiftedY = QmHudConnectionSignalIconY(BoxY, Slot.m_Size, 2);
	EXPECT_FLOAT_EQ(ShiftedY, BoxY + ExpectedOffset);
	EXPECT_LE(ShiftedY + Slot.m_Size, BoxY + IslandHeight);
}

// 信息栈路径：图标在左缘，文本从图标占位之后开始，两者不重叠。
TEST(QmHudConnectionSignalSlot, InfoStackTextStartsAfterIconSlot)
{
	const SHudConnectionSignalSlot Slot = QmHudConnectionSignalSlot();
	const float PaddingLeft = QmHudMediaIslandScaled(4.0f);
	const float IconLeft = PaddingLeft + Slot.m_Inset;
	const float TextLeft = PaddingLeft + Slot.m_Width;
	EXPECT_GE(TextLeft, IconLeft + Slot.m_Size);
	EXPECT_FLOAT_EQ(Slot.m_Inset, 0.0f);
}

// 录制文本路径：文本让位到图标占位之后，收起（仅图标）时状态区仍有内容宽度。
TEST(QmHudConnectionSignalSlot, RecordingTextShiftsRightOfIconSlot)
{
	const SHudConnectionSignalSlot Slot = QmHudConnectionSignalSlot();
	const float PaddingLeft = QmHudMediaIslandScaled(4.0f);
	const float IconLeft = PaddingLeft + Slot.m_Inset;
	const float TextLeft = PaddingLeft + Slot.m_Width + QmHudMediaIslandScaled(5.0f) + QmHudMediaIslandScaled(4.0f);
	EXPECT_GT(TextLeft, IconLeft + Slot.m_Size);
}
