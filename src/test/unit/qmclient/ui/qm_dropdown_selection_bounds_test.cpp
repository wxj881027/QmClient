#include <game/client/QmUi/QmDropdown.h>

#include <gtest/gtest.h>

#include <limits>

TEST(QmDropdownSelectionBounds, UpAtIndexOneWithMaximumCountSelectsZero)
{
	SQmDropdownInput Input;
	Input.m_KeyUp = true;
	int ActiveIndex = 1;
	const auto Result = QmUpdateDropdownPopupSelection(Input, std::numeric_limits<int>::max(), true, ActiveIndex);
	EXPECT_EQ(ActiveIndex, 0);
	EXPECT_FALSE(Result.m_Closed);
	EXPECT_FALSE(Result.m_Selected);
}

TEST(QmDropdownSelectionBounds, UpNearTheEndOfMaximumCountStaysInRange)
{
	SQmDropdownInput Input;
	Input.m_KeyUp = true;
	int ActiveIndex = std::numeric_limits<int>::max() - 2;
	QmUpdateDropdownPopupSelection(Input, std::numeric_limits<int>::max(), true, ActiveIndex);
	EXPECT_EQ(ActiveIndex, std::numeric_limits<int>::max() - 3);
}

TEST(QmDropdownSelectionBounds, UpAtZeroWrapsToTheLastItemOfMaximumCount)
{
	SQmDropdownInput Input;
	Input.m_KeyUp = true;
	int ActiveIndex = 0;
	QmUpdateDropdownPopupSelection(Input, std::numeric_limits<int>::max(), true, ActiveIndex);
	EXPECT_EQ(ActiveIndex, std::numeric_limits<int>::max() - 1);
}

TEST(QmDropdownSelectionBounds, DownAtTheLastItemOfMaximumCountWrapsToZero)
{
	SQmDropdownInput Input;
	Input.m_KeyDown = true;
	int ActiveIndex = std::numeric_limits<int>::max() - 1;
	QmUpdateDropdownPopupSelection(Input, std::numeric_limits<int>::max(), true, ActiveIndex);
	EXPECT_EQ(ActiveIndex, 0);
}

TEST(QmDropdownSelectionBounds, BothDirectionKeysKeepTheExistingUpThenDownOrder)
{
	SQmDropdownInput Input;
	Input.m_KeyUp = Input.m_KeyDown = true;
	int ActiveIndex = std::numeric_limits<int>::max() - 2;
	QmUpdateDropdownPopupSelection(Input, std::numeric_limits<int>::max(), true, ActiveIndex);
	EXPECT_EQ(ActiveIndex, std::numeric_limits<int>::max() - 2);
}

TEST(QmDropdownSelectionBounds, SingleItemNavigationRemainsAtZero)
{
	SQmDropdownInput Input;
	Input.m_KeyUp = Input.m_KeyDown = true;
	int ActiveIndex = 0;
	QmUpdateDropdownPopupSelection(Input, 1, true, ActiveIndex);
	EXPECT_EQ(ActiveIndex, 0);
}
