#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCREENSHOT_VIEWER_VIEWPORT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCREENSHOT_VIEWER_VIEWPORT_H

#include <game/client/QmUi/QmImageView.h>

#include <cmath>

// 独立看图窗口的视口：保留原有类名和调用方式，几何与采样策略已并入共享的 CQmImageView。
// 新代码直接用 CQmImageView；这里只维持既有公开 API（含 12 项视口单测）不变。
class CQmScreenshotViewerViewport
{
public:
	// 旧类型名继续可用：现有调用点按 SRect 读取图片矩形。
	using SRect = CQmImageView::SRect;

	void SetImageSize(int Width, int Height) { m_View.SetImageSize(Width, Height); }
	void Resize(int Width, int Height) { m_View.SetViewportSize(Width, Height); }
	void Fit() { m_View.Fit(); }
	void ActualSize() { m_View.ActualSize(); }
	void ToggleFit() { m_View.ToggleFit(); }
	void ZoomAt(float Factor, float X, float Y) { m_View.ZoomAt(Factor, X, Y); }
	void Pan(float X, float Y) { m_View.Pan(X, Y); }
	float Scale() const { return m_View.Scale(); }
	SRect ImageRect() const { return m_View.ImageRect(); }

private:
	CQmImageView m_View;
};

namespace qm_screenshot_viewer
{
	// 圆角裁剪的内缩量：Row 是距矩形上（或下）边缘的纵向偏移，
	// 返回该行在圆角内需要从左右各切掉多少像素。
	// 抽成纯函数是为了让「圆角是不是真的收圆」能被单测覆盖——
	// 看图窗口是独立进程，抓图不可靠，不能只靠肉眼。
	inline float RoundedCornerInset(float Radius, float Row)
	{
		if(Radius <= 0.0f || Row >= Radius)
			return 0.0f;
		const float Dy = Radius - Row;
		const float dx2 = Radius * Radius - Dy * Dy;
		if(dx2 <= 0.0f)
			return Radius;
		return Radius - std::sqrt(dx2);
	}
}

#endif
