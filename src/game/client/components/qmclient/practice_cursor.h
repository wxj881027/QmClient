#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_CURSOR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_CURSOR_H

#include <base/math.h>
#include <base/vmath.h>

// 准心所在的坐标空间。快速练习的传送落点必须与 HUD 准心共用同一个基准点，
// 否则基准差会随缩放放大成 (基准差 × (1 - Zoom)) 的落点偏移。
enum class EQmPracticeCursorSpace
{
	PLAYER, // 玩家本体：HUD 准心用渲染镜头中心绘制，落点也必须以它为基准
	FOLLOW, // 跟踪旁观：m_aTargetPos 以被跟随玩家的世界坐标为基准
	FREEVIEW, // 自由旁观：准心钉在屏幕中心
};

inline EQmPracticeCursorSpace QmPracticeCursorSpaceFor(bool Spectating, bool UsePosition)
{
	if(!Spectating)
		return EQmPracticeCursorSpace::PLAYER;
	return UsePosition ? EQmPracticeCursorSpace::FOLLOW : EQmPracticeCursorSpace::FREEVIEW;
}

inline vec2 QmPracticeCursorWorldPosition(const vec2 &CursorTarget, const vec2 &CameraCenter, const vec2 &SpecPosition,
	float CameraZoom, EQmPracticeCursorSpace Space)
{
	// 自由旁观准心固定在屏幕中心；平滑镜头尚未到达鼠标目标时也使用实际镜头中心。
	if(Space == EQmPracticeCursorSpace::FREEVIEW)
		return CameraCenter;

	// HUD 准心使用缩放 1 绘制，地图使用相机缩放；以同一基准点还原准心下的世界坐标。
	const vec2 Base = Space == EQmPracticeCursorSpace::FOLLOW ? SpecPosition : CameraCenter;
	return Base + (CursorTarget - Base) * maximum(CameraZoom, 0.01f);
}

// 兼容旧调用：只区分自由旁观与其余模式。
inline vec2 QmPracticeCursorWorldPosition(const vec2 &CursorTarget, const vec2 &CameraCenter, float CameraZoom, bool Freeview)
{
	return QmPracticeCursorWorldPosition(CursorTarget, CameraCenter, CameraCenter, CameraZoom,
		Freeview ? EQmPracticeCursorSpace::FREEVIEW : EQmPracticeCursorSpace::PLAYER);
}

// 练习世界只保留本体与分身。fork 时若钩子指向已被裁掉的玩家，
// HOOK_GRABBED 会按旧世界的 HookPos 继续施加拖拽力，表现为人被钩子甩飞。
inline bool QmPracticeHookShouldRelease(int HookedPlayer, int ParticipantA, int ParticipantB)
{
	if(HookedPlayer < 0)
		return false;
	return HookedPlayer != ParticipantA && HookedPlayer != ParticipantB;
}

#endif
