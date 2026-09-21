// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/shared/qm_ime_policy.h>

#include <game/client/qm_ime_manager.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

TEST(QmImePlatform, SystemCandidateUiPolicyMatchesPlatform)
{
#if defined(CONF_FAMILY_WINDOWS)
	EXPECT_FALSE(QmImeShouldUseSystemCandidateUi());
	EXPECT_TRUE(QmImeShouldRenderCustomCandidateUi());
#else
	EXPECT_TRUE(QmImeShouldUseSystemCandidateUi());
	EXPECT_FALSE(QmImeShouldRenderCustomCandidateUi());
#endif
}

TEST(QmImePlatform, CandidateRenderActionKeepsLifecycleValidationOnAllPlatforms)
{
	EXPECT_EQ(QmImeComputeCandidateRenderAction(false, 0), EQmImeCandidateRenderAction::VALIDATE_ONLY);
	EXPECT_EQ(QmImeComputeCandidateRenderAction(false, 1), EQmImeCandidateRenderAction::VALIDATE_ONLY);
	EXPECT_EQ(QmImeComputeCandidateRenderAction(true, 0), EQmImeCandidateRenderAction::LEGACY);
	EXPECT_EQ(QmImeComputeCandidateRenderAction(true, 1), EQmImeCandidateRenderAction::POPUP);
}

TEST(QmImePlatform, CandidateListNotifyFlagsSelectChangedList)
{
	EXPECT_TRUE(QmImeNotifyFlagsIncludeCandidateList(0, 0));
	EXPECT_FALSE(QmImeNotifyFlagsIncludeCandidateList(0, 1));
	EXPECT_TRUE(QmImeNotifyFlagsIncludeCandidateList(1u << 2, 2));
	EXPECT_FALSE(QmImeNotifyFlagsIncludeCandidateList(1u << 2, 0));
	EXPECT_TRUE(QmImeNotifyFlagsIncludeCandidateList((1u << 1) | (1u << 3), 3));
	EXPECT_FALSE(QmImeNotifyFlagsIncludeCandidateList(1u << 3, 32));
}

TEST(QmImePlatform, CandidatePageSizeFallsBackToCount)
{
	EXPECT_EQ(QmImeCandidatePageSizeOrCount(0, 9), 9u);
	EXPECT_EQ(QmImeCandidatePageSizeOrCount(5, 9), 5u);
}
TEST(QmImePlatform, CandidateOffsetCapacityClampsMalformedCount)
{
	EXPECT_EQ(QmImeCandidateOffsetCapacity(20, 12), 2u);
	EXPECT_EQ(QmImeCandidateOffsetCapacity(11, 12), 0u);
}

TEST(QmImePlatform, BoundedUtf16LengthRejectsMalformedCandidateRanges)
{
	const unsigned char aBuffer[] = {
		0x41,
		0x00,
		0x42,
		0x00,
		0x00,
		0x00,
		0x43,
		0x00,
	};

	ASSERT_TRUE(QmImeBoundedUtf16Length(aBuffer, sizeof(aBuffer), 0).has_value());
	EXPECT_EQ(*QmImeBoundedUtf16Length(aBuffer, sizeof(aBuffer), 0), 2u);
	EXPECT_FALSE(QmImeBoundedUtf16Length(aBuffer, sizeof(aBuffer), 1).has_value());
	EXPECT_FALSE(QmImeBoundedUtf16Length(aBuffer, sizeof(aBuffer), 6).has_value());
	EXPECT_FALSE(QmImeBoundedUtf16Length(aBuffer, sizeof(aBuffer), sizeof(aBuffer)).has_value());
	EXPECT_FALSE(QmImeBoundedUtf16Length(nullptr, sizeof(aBuffer), 0).has_value());
}

TEST(QmImePlatform, EmptyTextEditingKeepsCandidatesForSogouPageBoundary)
{
	EXPECT_FALSE(QmImeEmptyTextEditingShouldClearCandidates());
}

TEST(QmImePlatform, PopupVisibilityDrivenByCandidateCount)
{
	EXPECT_FALSE(QmImePopupShouldBeVisible(0));
	EXPECT_TRUE(QmImePopupShouldBeVisible(1));
	EXPECT_TRUE(QmImePopupShouldBeVisible(5));
}

TEST(QmImePlatform, FailedCandidateReloadKeepsPreviousOnChangeNotify)
{
	EXPECT_EQ(QmImeResolveCandidateReloadAction(true, false), EQmImeCandidateReloadAction::REPLACE);
	EXPECT_EQ(QmImeResolveCandidateReloadAction(true, true), EQmImeCandidateReloadAction::REPLACE);
	EXPECT_EQ(QmImeResolveCandidateReloadAction(false, true), EQmImeCandidateReloadAction::CLEAR);
	EXPECT_EQ(QmImeResolveCandidateReloadAction(false, false), EQmImeCandidateReloadAction::KEEP_PREVIOUS);
}

TEST(QmImePlatform, StaleCandidateReloadAfterCommitIsSuppressed)
{
	EXPECT_EQ(QmImeResolveCandidateReloadAction(true, false, true), EQmImeCandidateReloadAction::KEEP_PREVIOUS);
	EXPECT_EQ(QmImeResolveCandidateReloadAction(true, true, true), EQmImeCandidateReloadAction::REPLACE);
	EXPECT_FALSE(QmImeShouldSuppressStaleCandidateReload(true, true));
	EXPECT_TRUE(QmImeShouldSuppressStaleCandidateReload(true, false));
	EXPECT_FALSE(QmImeShouldSuppressStaleCandidateReload(false, false));
}

TEST(QmImePlatform, LayoutMeasureIgnoresSelectedPadding)
{
	EXPECT_FALSE(QmImeLayoutMeasureUsesSelectedPadding());
	EXPECT_FLOAT_EQ(QmImeSelectedLayoutExtraWidth(6.2f, 5.6f), 1.2f);
	EXPECT_FLOAT_EQ(QmImeSelectedLayoutExtraWidth(5.6f, 5.6f), 0.0f);
	EXPECT_FLOAT_EQ(QmImeSelectedLayoutExtraWidth(4.0f, 5.6f), 0.0f);
}

TEST(QmImePlatform, CandidateWindowStartIsStickyAndKeepsSelectionVisible)
{
	// 全部放得下时不滚动
	EXPECT_EQ(QmImeResolveCandidateWindowStart(5, 5, 4, 0), 0);
	EXPECT_EQ(QmImeResolveCandidateWindowStart(5, 5, 0, 2), 0);
	// 窗口 4、选中第 5 个（index=4）：start 应到 1，而不是每帧乱跳
	EXPECT_EQ(QmImeResolveCandidateWindowStart(5, 4, 4, 0), 1);
	EXPECT_EQ(QmImeResolveCandidateWindowStart(5, 4, 4, 1), 1);
	// 选中仍在窗口内时保持上一帧 start
	EXPECT_EQ(QmImeResolveCandidateWindowStart(12, 7, 5, 2), 2);
	// 选中移出右侧窗口才滚动
	EXPECT_EQ(QmImeResolveCandidateWindowStart(12, 7, 9, 2), 3);
	// 选中移出左侧窗口
	EXPECT_EQ(QmImeResolveCandidateWindowStart(12, 7, 0, 2), 0);
	// 越界输入
	EXPECT_EQ(QmImeResolveCandidateWindowStart(0, 5, 2, 1), 0);
	EXPECT_EQ(QmImeResolveCandidateWindowStart(5, 0, 2, 1), 0);
	EXPECT_EQ(QmImeResolveCandidateWindowStart(5, 3, -1, 99), 2);
}

TEST(QmImePlatform, FlashGuardsAreWiredInInputAndManager)
{
	const std::string InputSource = ReadTestSourceFile("src/engine/client/input.cpp");
	const std::string ManagerSource = ReadTestSourceFile("src/game/client/qm_ime_manager.cpp");
	const std::string PopupSource = ReadTestSourceFile("src/game/client/qm_ime_candidate_popup.cpp");

	EXPECT_NE(InputSource.find("QmImeEmptyTextEditingShouldClearCandidates"), std::string::npos);
	EXPECT_NE(InputSource.find("QmImeResolveCandidateReloadAction"), std::string::npos);
	EXPECT_NE(InputSource.find("EQmImeCandidateReloadAction::KEEP_PREVIOUS"), std::string::npos);
	EXPECT_NE(InputSource.find("m_ImeSuppressStaleCandidateReload"), std::string::npos);
	EXPECT_NE(InputSource.find("QmImeShouldSuppressStaleCandidateReload"), std::string::npos);
	EXPECT_NE(ManagerSource.find("State.m_Visible = QmImePopupShouldBeVisible(CandidateCount);"), std::string::npos);
	EXPECT_EQ(ManagerSource.find("State.m_Visible = HasComposition && CandidateCount > 0;"), std::string::npos);
	EXPECT_NE(PopupSource.find("QmImeResolveCandidateWindowStart"), std::string::npos);
	EXPECT_NE(PopupSource.find("QmImeLayoutMeasureUsesSelectedPadding"), std::string::npos);
	EXPECT_NE(PopupSource.find("m_CandidateStart = CandidateStart"), std::string::npos);
	EXPECT_EQ(PopupSource.find("(void)CandidateViewport"), std::string::npos);
}
