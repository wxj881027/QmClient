#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_WATERMARK_COMPOSITOR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_WATERMARK_COMPOSITOR_H

#include <engine/image.h>

struct SQmWatermarkLayer
{
	// 位置表示旋转后包围盒在可移动空间中的比例，九宫格与拖动共用此坐标。
	int m_X = 1000;
	int m_Y = 1000;
	int m_Size = 35;
	int m_Opacity = 75;
	int m_Angle = 0;
	bool m_Tile = false;
};

struct SQmWatermarkBounds
{
	float m_X, m_Y, m_Width, m_Height;
};

SQmWatermarkBounds QmWatermarkBounds(int Width, int Height, int LayerWidth, int LayerHeight, const SQmWatermarkLayer &Layer);
// 输入均为 RGBA；支持透明底图，按源覆盖规则合成，不修改图层像素。
bool QmCompositeWatermark(CImageInfo &Image, const CImageInfo &Stamp, const SQmWatermarkLayer &Layer);

#endif
