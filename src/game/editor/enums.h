#ifndef GAME_EDITOR_ENUMS_H
#define GAME_EDITOR_ENUMS_H

constexpr const char *GAME_TILE_OP_NAMES[] = {
	"空气",
	"可钩",
	"死亡",
	"不可钩",
	"穿钩",
	"冻结",
	"解冻",
	"深度冻结",
	"深度解冻",
	"蓝色检查点传送",
	"红色检查点传送",
	"实时冻结",
	"实时解冻",
};
enum class EGameTileOp
{
	AIR,
	HOOKABLE,
	DEATH,
	UNHOOKABLE,
	HOOKTHROUGH,
	FREEZE,
	UNFREEZE,
	DEEP_FREEZE,
	DEEP_UNFREEZE,
	BLUE_CHECK_TELE,
	RED_CHECK_TELE,
	LIVE_FREEZE,
	LIVE_UNFREEZE,
};

constexpr const char *AUTOMAP_REFERENCE_NAMES[] = {
	"游戏层",
	"可钩",
	"死亡",
	"不可钩",
	"冻结",
	"解冻",
	"深度冻结",
	"深度解冻",
	"实时冻结",
	"实时解冻",
};

#endif
