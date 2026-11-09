#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_MOUSE_DIAGRAM_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_MOUSE_DIAGRAM_H

#include <engine/keys.h>

#include <game/client/ui_rect.h>

#include <array>

// 绑定页的鼠标图：只算几何和命中，不依赖 CUi / IGraphics / CMenus，
// 版式、区域归属和命中优先级都能在 testrunner 里直接断言。
namespace qm_bind_mouse
{
	// 整块图在设置页内容流里占的高度（以设置页行高为单位）。
	// 键盘六排键约 9 行高，鼠标比它矮一点，才不会把整张卡片压成鼠标大、键盘小。
	inline constexpr float DIAGRAM_HEIGHT_LINES = 11.5f;

	// 圆角角位：数值与 IGraphics::CORNER_* 一致（绘制层另有 static_assert 对齐），
	// 这样这个 TU 不必为了几个位标志把图形接口拖进来。
	enum ECorner : int
	{
		CORNER_TL = 1 << 0,
		CORNER_TR = 1 << 1,
		CORNER_BL = 1 << 2,
		CORNER_BR = 1 << 3,
		CORNER_ALL = CORNER_TL | CORNER_TR | CORNER_BL | CORNER_BR,
	};

	// 区域下标固定：绘制顺序和命中优先级都按固定的表走，不依赖数组里的物理顺序。
	enum EZone : int
	{
		ZONE_MOUSE1 = 0,
		ZONE_MOUSE2,
		ZONE_WHEEL_UP,
		ZONE_MOUSE3,
		ZONE_WHEEL_DOWN,
		ZONE_WHEEL_LEFT,
		ZONE_WHEEL_RIGHT,
		ZONE_MOUSE5,
		ZONE_MOUSE4,
		ZONE_COUNT,
	};

	struct SZone
	{
		int m_Key = KEY_UNKNOWN;
		CUIRect m_Rect{};
		float m_Radius = 0.0f;
		int m_Corners = CORNER_ALL;
	};

	struct SDiagram
	{
		CUIRect m_Area{};
		CUIRect m_Body{};
		float m_BodyRadius = 0.0f;
		// 滚轮整根（上滚 + 中键 + 下滚）的胶囊外框：绘制时先铺这一层，再把三段画进去。
		CUIRect m_Wheel{};
		float m_WheelRadius = 0.0f;
		std::array<SZone, ZONE_COUNT> m_aZones{};
	};

	// 绘制顺序：先画被压住的区域，再画上层区域。DoButtonLogic 的 HotItem 由最后画到的
	// 控件接管，所以滚轮压在左右键上时点到的必须是滚轮，而不是底下那颗大键。
	inline constexpr std::array<int, ZONE_COUNT> ZONE_DRAW_ORDER = {
		ZONE_MOUSE1, ZONE_MOUSE2, ZONE_WHEEL_LEFT, ZONE_WHEEL_RIGHT,
		ZONE_WHEEL_UP, ZONE_MOUSE3, ZONE_WHEEL_DOWN, ZONE_MOUSE5, ZONE_MOUSE4};

	// 命中顺序：优先级从高到低，和绘制顺序相反（先问上层）。
	inline constexpr std::array<int, ZONE_COUNT> ZONE_HIT_ORDER = {
		ZONE_WHEEL_UP, ZONE_MOUSE3, ZONE_WHEEL_DOWN, ZONE_WHEEL_LEFT, ZONE_WHEEL_RIGHT,
		ZONE_MOUSE5, ZONE_MOUSE4, ZONE_MOUSE1, ZONE_MOUSE2};

	// 内容流里要预留的高度。
	float DiagramHeight(float LineHeight);
	// 版式：Area 是整块可用区（宽度为内容列宽，高度由 DiagramHeight 给出）。
	SDiagram Layout(const CUIRect &Area);
	// 命中最上层区域；空白处（例如掌托）返回 -1。
	int HitTest(const SDiagram &Diagram, float X, float Y);
	// 鼠标键范围（含滚轮四个方向）：行跳转时用来判断要不要把整张图滚进视口。
	bool IsMouseKey(int Key);
} // namespace qm_bind_mouse

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_MOUSE_DIAGRAM_H
