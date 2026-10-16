#ifndef GAME_MAP_RENDER_INTERFACES_H
#define GAME_MAP_RENDER_INTERFACES_H

#include <engine/graphics.h>

enum EMapImageEntityLayerType
{
	MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH = 0,
	MAP_IMAGE_ENTITY_LAYER_TYPE_SWITCH,

	MAP_IMAGE_ENTITY_LAYER_TYPE_COUNT,
};

enum ERenderType
{
	RENDERTYPE_BACKGROUND = 0,
	RENDERTYPE_BACKGROUND_FORCE,
	RENDERTYPE_FOREGROUND,
	RENDERTYPE_FULL_DESIGN,
};

// 完整缓存也可用于背景/前景绘制；层的归属由游戏层位置决定，不能仅切换绘制参数。
inline bool MapRenderLayerInPass(int RenderType, int Group, int Layer, int GameGroup, int GameLayer)
{
	// 组节点必须更新投影与裁剪，包括游戏层位于组中间的情况。
	if(Layer < 0 || RenderType == RENDERTYPE_FULL_DESIGN)
		return true;
	const bool BeforeGame = GameGroup < 0 || Group < GameGroup || (Group == GameGroup && Layer < GameLayer);
	return RenderType == RENDERTYPE_FOREGROUND ? !BeforeGame : BeforeGame;
}

class IEnvelopeEval
{
public:
	virtual ~IEnvelopeEval() = default;
	virtual void EnvelopeEval(int TimeOffsetMillis, int EnvelopeIndex, ColorRGBA &Result, size_t Channels) const = 0;
};

class IMapImages
{
public:
	virtual ~IMapImages() = default;
	virtual IGraphics::CTextureHandle Get(int Index) const = 0;
	virtual int Num() const = 0;
	// DDRace
	virtual IGraphics::CTextureHandle GetEntities(EMapImageEntityLayerType EntityLayerType) = 0;
	virtual IGraphics::CTextureHandle GetSpeedupArrow() = 0;
	virtual IGraphics::CTextureHandle GetTuneColors() = 0;

	virtual IGraphics::CTextureHandle GetOverlayBottom() = 0;
	virtual IGraphics::CTextureHandle GetOverlayTop() = 0;
	virtual IGraphics::CTextureHandle GetOverlayCenter() = 0;
};

#endif
