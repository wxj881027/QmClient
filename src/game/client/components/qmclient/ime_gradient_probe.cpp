// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "ime_gradient_probe.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/QmUi/QmColorGradient.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/gameclient.h>

namespace
{
	// 探针面板固定尺寸：足够宽放下完整渐变过渡，又不会盖住太多菜单。
	constexpr int PROBE_WIDTH = 512;
	constexpr int PROBE_HEIGHT = 128;
	constexpr int PROBE_BORDER = 2;
	constexpr int PROBE_MARGIN = 8;
}

SQmImeGradientProbeRect QmImeGradientProbeDraw(CGameClient *pGameClient)
{
	SQmImeGradientProbeRect Result;
	if(pGameClient == nullptr)
		return Result;
	IGraphics *pGraphics = pGameClient->Graphics();
	if(pGraphics == nullptr || !pGraphics->HasRoundedRectSdf())
		return Result;

	float OldScreenX0, OldScreenY0, OldScreenX1, OldScreenY1;
	pGraphics->GetScreen(&OldScreenX0, &OldScreenY0, &OldScreenX1, &OldScreenY1);
	// 屏幕左上角画一块固定大小的面板；菜单背景不会覆盖它，截图后按日志坐标取样。
	pGraphics->MapScreen(0.0f, 0.0f, (float)pGraphics->ScreenWidth(), (float)pGraphics->ScreenHeight());
	// 先铺一块纯色底板：菜单背景本身是渐变的，羽化像素只有落在固定底色上
	// 才能被逐像素判定。底板向外多留 8 像素，让 SDF 的 2 像素 padding 也有底色。
	const CUIRect Backdrop{(float)PROBE_MARGIN - 8.0f, (float)PROBE_MARGIN - 8.0f, (float)PROBE_WIDTH + 16.0f, (float)PROBE_HEIGHT + 16.0f};
	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	pGraphics->SetColor(ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f));
	const IGraphics::CQuadItem BackdropQuad(Backdrop.x, Backdrop.y, Backdrop.w, Backdrop.h);
	pGraphics->QuadsDrawTL(&BackdropQuad, 1);
	pGraphics->QuadsEnd();
	const CUIRect Rect{(float)PROBE_MARGIN, (float)PROBE_MARGIN, (float)PROBE_WIDTH, (float)PROBE_HEIGHT};
	// 两色线性渐变：左红右蓝，方向沿 +X，范围覆盖整个面板。
	SQmColorGradient Gradient;
	Gradient.m_NumColors = 2;
	Gradient.m_aColors[0] = ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f);
	Gradient.m_aColors[1] = ColorRGBA(0.0f, 0.0f, 1.0f, 1.0f);
	Gradient.m_Type = EQmGradientType::LINEAR;
	Gradient.m_Center = vec2(0.5f, 0.5f);
	Gradient.m_Direction = vec2(1.0f, 0.0f);
	Gradient.m_Range = 1.0f;
	Gradient.m_Reverse = false;
	SRoundedSurfaceParams Params;
	Params.m_Radius = PROBE_HEIGHT * 0.5f;
	Params.m_BorderWidth = (float)PROBE_BORDER;
	Params.m_PixelSize = 1.0f;
	Params.m_Corners = IGraphics::CORNER_ALL;
	const bool Drawn = DrawRoundedGradientSurface(pGraphics, Rect, Gradient, 1.0f, ColorRGBA(0.0f, 1.0f, 0.0f, 1.0f), Params);
	pGraphics->MapScreen(OldScreenX0, OldScreenY0, OldScreenX1, OldScreenY1);
	if(!Drawn)
	{
		log_error("ime/probe", "gradient surface draw failed");
		return Result;
	}

	Result.m_X = PROBE_MARGIN;
	Result.m_Y = PROBE_MARGIN;
	Result.m_Width = PROBE_WIDTH;
	Result.m_Height = PROBE_HEIGHT;
	Result.m_BorderWidth = PROBE_BORDER;
	// 每帧都画，但只记录一次，避免把日志刷满。
	static bool s_Logged = false;
	if(!s_Logged)
	{
		s_Logged = true;
		log_info("ime/probe", "drawn x=%d y=%d w=%d h=%d border=%d sdf=1", Result.m_X, Result.m_Y, Result.m_Width, Result.m_Height, Result.m_BorderWidth);
	}
	return Result;
}
