// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_IME_GRADIENT_PROBE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_IME_GRADIENT_PROBE_H

class CGameClient;

// 把 IME 候选栏的渐变圆角表面按生产路径（DrawRoundedGradientSurface）画到交换链上的固定
// 矩形里，供真实截图逐像素检查。只由调试命令触发，日志给出精确像素位置。
// 为什么不用离屏回读：回读是异步池，帧内轮询不稳定；而截图就是最终呈现的像素，
// 能同时覆盖 SDF 片元、混合和呈现三步。
struct SQmImeGradientProbeRect
{
	int m_X = 0;
	int m_Y = 0;
	int m_Width = 0;
	int m_Height = 0;
	int m_BorderWidth = 0;
};

// 请求在下一帧末尾绘制探针；返回实际矩形（未启用时宽高为 0）。
SQmImeGradientProbeRect QmImeGradientProbeDraw(CGameClient *pGameClient);

#endif
