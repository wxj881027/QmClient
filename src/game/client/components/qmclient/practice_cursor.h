#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_CURSOR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_CURSOR_H

#include <base/vmath.h>

inline vec2 QmPracticeCursorWorldPosition(const vec2 &CursorTarget, const vec2 &CameraCenter, float CameraZoom, bool Freeview)
{
	// 自由旁观准心固定在屏幕中心；平滑镜头尚未到达鼠标目标时也使用实际镜头中心。
	if(Freeview)
		return CameraCenter;

	// HUD 准心使用缩放 1 绘制，地图使用相机缩放；以同一镜头中心还原准心下的世界坐标。
	return CameraCenter + (CursorTarget - CameraCenter) * CameraZoom;
}

#endif
