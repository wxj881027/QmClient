#ifndef GAME_EDITOR_MAPITEMS_H
#define GAME_EDITOR_MAPITEMS_H

#include <game/mapitems.h>

#include <algorithm>

enum class EQuadProp
{
	PROP_NONE = -1,
	PROP_ORDER,
	PROP_POS_X,
	PROP_POS_Y,
	PROP_POS_ENV,
	PROP_POS_ENV_OFFSET,
	PROP_COLOR,
	PROP_COLOR_ENV,
	PROP_COLOR_ENV_OFFSET,
	NUM_PROPS,
};

enum class EQuadPointProp
{
	PROP_NONE = -1,
	PROP_POS_X,
	PROP_POS_Y,
	PROP_COLOR,
	PROP_TEX_U,
	PROP_TEX_V,
	NUM_PROPS,
};

enum class ESoundProp
{
	PROP_NONE = -1,
	PROP_POS_X,
	PROP_POS_Y,
	PROP_LOOP,
	PROP_PAN,
	PROP_TIME_DELAY,
	PROP_FALLOFF,
	PROP_POS_ENV,
	PROP_POS_ENV_OFFSET,
	PROP_SOUND_ENV,
	PROP_SOUND_ENV_OFFSET,
	NUM_PROPS,
};

enum class ERectangleShapeProp
{
	PROP_NONE = -1,
	PROP_RECTANGLE_WIDTH,
	PROP_RECTANGLE_HEIGHT,
	NUM_PROPS,
};

enum class ECircleShapeProp
{
	PROP_NONE = -1,
	PROP_CIRCLE_RADIUS,
	NUM_PROPS,
};

enum class ELayerProp
{
	PROP_NONE = -1,
	PROP_GROUP,
	PROP_ORDER,
	PROP_HQ,
	NUM_PROPS,
};

enum class ETilesProp
{
	PROP_NONE = -1,
	PROP_WIDTH,
	PROP_HEIGHT,
	PROP_SHIFT,
	PROP_SHIFT_BY,
	PROP_IMAGE,
	PROP_COLOR,
	PROP_COLOR_ENV,
	PROP_COLOR_ENV_OFFSET,
	PROP_AUTOMAPPER,
	PROP_AUTOMAPPER_REFERENCE,
	PROP_LIVE_GAMETILES,
	PROP_SEED,
	NUM_PROPS
};

enum class ETilesCommonProp
{
	PROP_NONE = -1,
	PROP_WIDTH,
	PROP_HEIGHT,
	PROP_SHIFT,
	PROP_SHIFT_BY,
	PROP_COLOR,
	NUM_PROPS,
};

enum class EGroupProp
{
	PROP_NONE = -1,
	PROP_ORDER,
	PROP_POS_X,
	PROP_POS_Y,
	PROP_PARA_X,
	PROP_PARA_Y,
	PROP_USE_CLIPPING,
	PROP_CLIP_X,
	PROP_CLIP_Y,
	PROP_CLIP_W,
	PROP_CLIP_H,
	NUM_PROPS,
};

enum class ELayerQuadsProp
{
	PROP_NONE = -1,
	PROP_IMAGE,
	NUM_PROPS,
};

enum class ELayerSoundsProp
{
	PROP_NONE = -1,
	PROP_SOUND,
	NUM_PROPS,
};

/**
 * 图层种类。
 * 复制区域时画笔只包含被选中的图层，而游戏组里的传送/开关/速度/调参层和游戏层属于同一块地图数据，
 * 因此需要靠种类把它们自动补进画笔；粘贴时也要按种类而不是按索引把画笔层对应到目标层。
 */
enum class ELayerKind
{
	INVALID,
	TILES,
	GAME,
	FRONT,
	TELE,
	SPEEDUP,
	SWITCH,
	TUNE,
	QUADS,
	SOUNDS,
};

/**
 * 由图块层的实体标记推导图层种类。
 * 正常地图里这些标记互斥；若同时置位则按 Game/Front/Tele/Speedup/Switch/Tune 的固定顺序取第一个。
 */
constexpr ELayerKind LayerKindFromFlags(bool HasGame, bool HasFront, bool HasTele, bool HasSpeedup, bool HasSwitch, bool HasTune)
{
	if(HasGame)
		return ELayerKind::GAME;
	if(HasFront)
		return ELayerKind::FRONT;
	if(HasTele)
		return ELayerKind::TELE;
	if(HasSpeedup)
		return ELayerKind::SPEEDUP;
	if(HasSwitch)
		return ELayerKind::SWITCH;
	if(HasTune)
		return ELayerKind::TUNE;
	return ELayerKind::TILES;
}

/**
 * 未选中的图层在抓取画笔时是否需要自动补进来。
 * 只有传送/开关/速度/调参层带独立数据，漏掉它们会让复制出来的区块缺内容；
 * 游戏层与普通图块层是玩家主动选择的对象，前景层的数据由游戏层的 through-cut 机制跟随，都不自动补抓。
 */
constexpr bool ShouldAutoGrabEntityLayer(ELayerKind Kind, bool AlreadySelected)
{
	if(AlreadySelected)
		return false;
	return Kind == ELayerKind::TELE || Kind == ELayerKind::SPEEDUP || Kind == ELayerKind::SWITCH || Kind == ELayerKind::TUNE;
}

/**
 * 遍历矩形覆盖的所有格子（自动按图层边界裁剪），任一格子让回调返回 true 就立即返回 true。
 * 空图层、空矩形、负尺寸或完全在图层外的矩形都返回 false。
 */
template<typename TCallback>
bool AnyInRect(int Width, int Height, int RectX, int RectY, int RectW, int RectH, TCallback &&Callback)
{
	if(Width <= 0 || Height <= 0 || RectW <= 0 || RectH <= 0)
		return false;

	const int StartX = std::clamp(RectX, 0, Width);
	const int StartY = std::clamp(RectY, 0, Height);
	const int EndX = std::clamp(RectX + RectW, 0, Width);
	const int EndY = std::clamp(RectY + RectH, 0, Height);

	for(int y = StartY; y < EndY; y++)
		for(int x = StartX; x < EndX; x++)
			if(Callback(x, y))
				return true;
	return false;
}

#endif
