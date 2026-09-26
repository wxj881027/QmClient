// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QM_ICON_MORPH_H
#define GAME_CLIENT_QM_ICON_MORPH_H

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/ui_rect.h>

#include <cstddef>

class IGraphics;

constexpr int QM_ICON_MORPH_SAMPLE_COUNT = 64;

struct SQmIconMorphPathData
{
	const float *m_pSource = nullptr;
	const float *m_pTarget = nullptr;
	float m_SourceCenterX = 128.0f;
	float m_SourceCenterY = 128.0f;
	float m_TargetCenterX = 128.0f;
	float m_TargetCenterY = 128.0f;
	float m_Theta = 0.0f;
	float m_LogScale = 0.0f;
};

struct SQmIconMorphSurfaceData
{
	SQmIconMorphPathData m_Outer;
	SQmIconMorphPathData m_Inner;
};

struct SQmIconMorphPlan
{
	const SQmIconMorphSurfaceData *m_pSurfaces = nullptr;
	int m_NumSurfaces = 0;
};

// 解析一个归一化路径点。极坐标变换保持整体旋转和缩放连续，剩余几何线性变形。
// 同一表面的内外轮廓共享 m_Theta / m_LogScale（生成端保证）：各持一套参数时内外圈
// 差速旋转会把连接带拧成碎片，在实机上渲染为散点。
vec2 ResolveQmIconMorphPoint(const SQmIconMorphPathData &Path, int PointIndex, float Progress);

const SQmIconMorphPlan *QmEyeMorphPlanForWeight(int Weight);

// 绘制采样后的眼睛路径几何。样例不可用时返回 false，由调用方保留 atlas/MSDF/字形回退。
bool RenderQmEyeMorph(IGraphics *pGraphics, int Weight, const CUIRect &Rect, const ColorRGBA &Color, float Progress);

#endif
