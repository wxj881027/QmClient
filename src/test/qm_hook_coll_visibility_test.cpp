// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <game/client/components/qmclient/qm_hook_coll_visibility.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>

#include <iterator>

namespace
{
	constexpr float HOOK_LENGTH = 380.0f;
	constexpr float HOOK_FIRE_SPEED = 10.0f;
	constexpr vec2 SCREEN_MIN(0.0f, 0.0f);
	constexpr vec2 SCREEN_MAX(1000.0f, 800.0f);

	// 与 CQmHookCollVisibility::MayReachView 内部一致：量化误差 1/128 + 100 固定余量 + 线宽
	float Reach(float HookLength, float HookFireSpeed, float LinePadding)
	{
		constexpr float MAX_DIRECTION_LENGTH = 1.0f + 1.0f / 128.0f;
		return (HookLength + HookFireSpeed) * MAX_DIRECTION_LENGTH + 100.0f + LinePadding;
	}

	CTeleTile MakeTele(int Type, int Number)
	{
		CTeleTile Tile;
		Tile.m_Type = (unsigned char)Type;
		Tile.m_Number = (unsigned char)Number;
		return Tile;
	}
}

// 未加载地图时保持保守：任何位置都认为可能可见，绝不误裁
TEST(QmHookCollVisibility, ConservativeBeforeMapLoad)
{
	CQmHookCollVisibility Visibility;

	EXPECT_TRUE(Visibility.MayReachView(vec2(-100000.0f, -100000.0f), HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, 0.0f, false));
	EXPECT_TRUE(Visibility.MayReachView(vec2(-100000.0f, -100000.0f), HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, 0.0f, true));
}

// 地图里有可用的 hook teleport 时不做距离裁剪（钩子可能被传送到远处）
TEST(QmHookCollVisibility, TeleHookInMapDisablesDistanceCulling)
{
	CQmHookCollVisibility Visibility;
	const CTeleTile aTiles[] = {MakeTele(TILE_TELEINHOOK, 1)};
	Visibility.OnMapLoad(aTiles, std::size(aTiles));

	EXPECT_TRUE(Visibility.MayReachView(vec2(-100000.0f, 0.0f), HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, 0.0f, false));
	// 未启用 legacy 语义时，legacy 旗标仍为 false -> 允许距离裁剪
	EXPECT_FALSE(Visibility.MayReachView(vec2(-100000.0f, 0.0f), HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, 0.0f, true));
}

// Number == 0 的 tele 瓦片是禁用状态，不应让裁剪失效
TEST(QmHookCollVisibility, DisabledTeleTilesAreIgnored)
{
	CQmHookCollVisibility Visibility;
	const CTeleTile aTiles[] = {MakeTele(TILE_TELEINHOOK, 0), MakeTele(TILE_TELEIN, 0)};
	Visibility.OnMapLoad(aTiles, std::size(aTiles));

	EXPECT_FALSE(Visibility.MayReachView(vec2(-100000.0f, 0.0f), HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, 0.0f, false));
	EXPECT_FALSE(Visibility.MayReachView(vec2(-100000.0f, 0.0f), HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, 0.0f, true));
}

// 旧式 TILE_TELEIN 只有开启 legacy 语义时才算数
TEST(QmHookCollVisibility, LegacyTeleportFlagSelectsDetection)
{
	CQmHookCollVisibility Visibility;
	const CTeleTile aTiles[] = {MakeTele(TILE_TELEIN, 3)};
	Visibility.OnMapLoad(aTiles, std::size(aTiles));

	EXPECT_TRUE(Visibility.MayReachView(vec2(-100000.0f, 0.0f), HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, 0.0f, true));
	EXPECT_FALSE(Visibility.MayReachView(vec2(-100000.0f, 0.0f), HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, 0.0f, false));
}

// 无可传送钩子时：只有超出 Reach 的位置才被裁掉，边界本身仍然保留
TEST(QmHookCollVisibility, CullsOnlyBeyondReach)
{
	CQmHookCollVisibility Visibility;
	const CTeleTile aTiles[] = {MakeTele(TILE_TELEINHOOK, 0)};
	Visibility.OnMapLoad(aTiles, std::size(aTiles));

	const float HookReach = Reach(HOOK_LENGTH, HOOK_FIRE_SPEED, 0.0f);

	// 恰好在 Reach 内（略近一点）
	EXPECT_TRUE(Visibility.MayReachView(vec2(SCREEN_MIN.x - HookReach + 1.0f, 400.0f), HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, 0.0f, false));
	// 超出 Reach 一点
	EXPECT_FALSE(Visibility.MayReachView(vec2(SCREEN_MIN.x - HookReach - 1.0f, 400.0f), HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, 0.0f, false));
	// 右侧与上下同理
	EXPECT_TRUE(Visibility.MayReachView(vec2(SCREEN_MAX.x + HookReach - 1.0f, 400.0f), HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, 0.0f, false));
	EXPECT_FALSE(Visibility.MayReachView(vec2(SCREEN_MAX.x + HookReach + 1.0f, 400.0f), HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, 0.0f, false));
	EXPECT_FALSE(Visibility.MayReachView(vec2(500.0f, SCREEN_MIN.y - HookReach - 1.0f), HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, 0.0f, false));
	EXPECT_FALSE(Visibility.MayReachView(vec2(500.0f, SCREEN_MAX.y + HookReach + 1.0f), HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, 0.0f, false));
	// 屏幕内的位置永远可见
	EXPECT_TRUE(Visibility.MayReachView(vec2(500.0f, 400.0f), HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, 0.0f, false));
}

// 线宽等附加余量会扩大可保留范围
TEST(QmHookCollVisibility, LinePaddingExtendsReach)
{
	CQmHookCollVisibility Visibility;
	const CTeleTile aTiles[] = {MakeTele(TILE_TELEINHOOK, 0)};
	Visibility.OnMapLoad(aTiles, std::size(aTiles));

	const float HookReach = Reach(HOOK_LENGTH, HOOK_FIRE_SPEED, 0.0f);
	const float Padding = 64.0f;
	const vec2 Position(SCREEN_MIN.x - HookReach - Padding / 2.0f, 400.0f);

	EXPECT_FALSE(Visibility.MayReachView(Position, HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, 0.0f, false));
	EXPECT_TRUE(Visibility.MayReachView(Position, HOOK_LENGTH, HOOK_FIRE_SPEED, SCREEN_MIN, SCREEN_MAX, Padding, false));
}

// 钩长/初速越大，保留范围越大（单调性）
TEST(QmHookCollVisibility, LongerHookKeepsMorePositions)
{
	CQmHookCollVisibility Visibility;
	const CTeleTile aTiles[] = {MakeTele(TILE_TELEINHOOK, 0)};
	Visibility.OnMapLoad(aTiles, std::size(aTiles));

	const vec2 Position(SCREEN_MIN.x - 600.0f, 400.0f);
	EXPECT_FALSE(Visibility.MayReachView(Position, 100.0f, 10.0f, SCREEN_MIN, SCREEN_MAX, 0.0f, false));
	EXPECT_TRUE(Visibility.MayReachView(Position, 1000.0f, 10.0f, SCREEN_MIN, SCREEN_MAX, 0.0f, false));
}
