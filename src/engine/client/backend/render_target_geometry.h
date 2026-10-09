#ifndef ENGINE_CLIENT_BACKEND_RENDER_TARGET_GEOMETRY_H
#define ENGINE_CLIENT_BACKEND_RENDER_TARGET_GEOMETRY_H

#include <algorithm>
#include <cstdint>

namespace render_target_geometry
{
struct SClipRect
{
	int m_X = 0;
	int m_Y = 0;
	int m_W = 0;
	int m_H = 0;
};

// 命令裁剪坐标以屏幕左下角为原点，结果统一为目标左上角坐标。
// 按边界向外取整，避免分别缩放宽高造成一像素缝隙。
inline SClipRect MapScreenClip(int X, int Y, int W, int H, int ScreenW, int ScreenH, int TargetW, int TargetH)
{
	if(ScreenW <= 0 || ScreenH <= 0 || TargetW <= 0 || TargetH <= 0 || W <= 0 || H <= 0)
		return {};
	const int64_t Left = std::clamp<int64_t>(X, 0, ScreenW);
	const int64_t Right = std::clamp<int64_t>((int64_t)X + W, 0, ScreenW);
	const int64_t Top = std::clamp<int64_t>((int64_t)ScreenH - Y - H, 0, ScreenH);
	const int64_t Bottom = std::clamp<int64_t>((int64_t)ScreenH - Y, 0, ScreenH);
	if(Right <= Left || Bottom <= Top)
		return {};
	const int TargetLeft = (int)(Left * TargetW / ScreenW);
	const int TargetTop = (int)(Top * TargetH / ScreenH);
	const int TargetRight = (int)((Right * TargetW + ScreenW - 1) / ScreenW);
	const int TargetBottom = (int)((Bottom * TargetH + ScreenH - 1) / ScreenH);
	return {TargetLeft, TargetTop, TargetRight - TargetLeft, TargetBottom - TargetTop};
}
}

#endif
