#include "test.h"

#include <game/client/components/qmclient/spectator_tele_search.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
	CTeleTile MakeTele(int Number, int Type)
	{
		CTeleTile Tile{};
		Tile.m_Number = Number;
		Tile.m_Type = Type;
		return Tile;
	}
} // namespace

TEST(QmSpectatorTeleSearch, DigitFromKeyAcceptsTopRowAndKeypad)
{
	// 键值按 SDL 扫描码排布，KEY_0 在 KEY_9 之后，故逐位列出而不是做算术。
	const int aTopRow[] = {KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};
	const int aKeypad[] = {KEY_KP_0, KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4, KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8, KEY_KP_9};
	for(int Digit = 0; Digit <= 9; ++Digit)
	{
		EXPECT_EQ(qm_spectator_tele::DigitFromKey(aTopRow[Digit]), Digit);
		EXPECT_EQ(qm_spectator_tele::DigitFromKey(aKeypad[Digit]), Digit);
	}

	// 非数字键不能产生编号输入。
	EXPECT_EQ(qm_spectator_tele::DigitFromKey(KEY_A), -1);
	EXPECT_EQ(qm_spectator_tele::DigitFromKey(KEY_ESCAPE), -1);
}

TEST(QmSpectatorTeleSearch, ParseNumberRejectsNonDigitsAndOutOfRangeNumbers)
{
	EXPECT_EQ(qm_spectator_tele::ParseNumber("7"), 7);
	EXPECT_EQ(qm_spectator_tele::ParseNumber("255"), 255);

	// 空输入、含非数字、超出 tele 编号上限都视为未指定。
	EXPECT_EQ(qm_spectator_tele::ParseNumber(""), 0);
	EXPECT_EQ(qm_spectator_tele::ParseNumber("0"), 0);
	EXPECT_EQ(qm_spectator_tele::ParseNumber("256"), 0);
	EXPECT_EQ(qm_spectator_tele::ParseNumber("12a"), 0);
}

TEST(QmSpectatorTeleSearch, StepNumberWrapsInsideOneTo255)
{
	EXPECT_EQ(qm_spectator_tele::StepNumber(5, 1), 6);
	EXPECT_EQ(qm_spectator_tele::StepNumber(5, -1), 4);
	EXPECT_EQ(qm_spectator_tele::StepNumber(255, 1), 1);
	EXPECT_EQ(qm_spectator_tele::StepNumber(1, -1), 255);

	// 尚未输入有效编号时，按方向回到该方向的端点，而不是停在 0。
	EXPECT_EQ(qm_spectator_tele::StepNumber(0, 1), 1);
	EXPECT_EQ(qm_spectator_tele::StepNumber(0, -1), 255);
}

TEST(QmSpectatorTeleSearch, FindNextSkipsNearbySameNumbersAndWrapsAround)
{
	// 宽度 12 保证两处同编号格相距 10，足以验证编辑器的距离规则。
	std::vector<CTeleTile> vTiles(12, MakeTele(0, TILE_AIR));
	vTiles[0] = MakeTele(3, TILE_TELEIN);
	vTiles[5] = MakeTele(3, TILE_TELEIN);
	vTiles[10] = MakeTele(3, TILE_TELEIN);

	// 首次查找定位到第一处。
	EXPECT_EQ(qm_spectator_tele::FindNext(vTiles.data(), 12, 1, 3, -1), 0);
	// 从第一处继续：距 1 格的 5 号被跳过，距 10 格的 10 号才是下一个目标。
	EXPECT_EQ(qm_spectator_tele::FindNext(vTiles.data(), 12, 1, 3, 0), 10);
	// 到末尾后回绕到地图起点处的第一格。
	EXPECT_EQ(qm_spectator_tele::FindNext(vTiles.data(), 12, 1, 3, 10), 0);
}

TEST(QmSpectatorTeleSearch, FindNextRejectsInvalidLayerAndNumber)
{
	std::vector<CTeleTile> vTiles(12, MakeTele(0, TILE_AIR));
	vTiles[3] = MakeTele(9, TILE_TELEIN);

	EXPECT_EQ(qm_spectator_tele::FindNext(nullptr, 12, 1, 9, -1), -1);
	EXPECT_EQ(qm_spectator_tele::FindNext(vTiles.data(), 0, 1, 9, -1), -1);
	EXPECT_EQ(qm_spectator_tele::FindNext(vTiles.data(), 12, 1, 0, -1), -1);

	// 地图上没有该编号时不应误报位置。
	EXPECT_EQ(qm_spectator_tele::FindNext(vTiles.data(), 12, 1, 7, -1), -1);
}
