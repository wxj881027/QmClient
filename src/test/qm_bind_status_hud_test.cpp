// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/qmclient/qm_bind_status_hud.h>

#include <gtest/gtest.h>

// 关闭彩虹色 HUD 后内置四项的语义配色：开/正常=绿(OK)，关/DF=红(DANGER)，Reset Self/HDF/Custom=黄(WARNING)
TEST(QmBindStatusHud, BuiltInTonesClassifyKeyStickingByValue)
{
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::KEY_STICKING, 0), EQmBindStatusTone::OK);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::KEY_STICKING, 1), EQmBindStatusTone::DANGER);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::KEY_STICKING, 2), EQmBindStatusTone::WARNING);
	// 越界值只显示 "Key Sticking: ?"，不参与配色
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::KEY_STICKING, 3), EQmBindStatusTone::NONE);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::KEY_STICKING, -1), EQmBindStatusTone::NONE);
}

TEST(QmBindStatusHud, BuiltInTonesClassifyHammerByValue)
{
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::HAMMER, 0), EQmBindStatusTone::OK);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::HAMMER, 1), EQmBindStatusTone::DANGER);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::HAMMER, 2), EQmBindStatusTone::WARNING);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::HAMMER, 3), EQmBindStatusTone::WARNING);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::HAMMER, 4), EQmBindStatusTone::NONE);
}

TEST(QmBindStatusHud, BuiltInTonesClassifyDummySwitches)
{
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::DUMMY_CONTROL, 0), EQmBindStatusTone::DANGER);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::DUMMY_CONTROL, 1), EQmBindStatusTone::OK);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::DUMMY_COPY, 0), EQmBindStatusTone::DANGER);
	EXPECT_EQ(QmResolveBuiltinBindStatusTone(EQmBindStatusLine::DUMMY_COPY, 1), EQmBindStatusTone::OK);
}
