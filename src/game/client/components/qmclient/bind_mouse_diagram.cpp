#include <game/client/components/qmclient/bind_mouse_diagram.h>

#include <algorithm>

// 顶视鼠标的分区几何：本体 -> 左右键 -> 滚轮三段 -> 滚轮倾斜两段 -> 左侧两个侧键。
// 所有数值都相对本体宽高，换窗口宽度只整体缩放，区域之间不会出现「这一块按比例、
// 那一块按像素」的错位。滚动方向压在左右键上面，靠绘制顺序 + 命中顺序共同保证。
namespace qm_bind_mouse
{
	namespace
	{
		// 本体高宽比：1.14，比真实鼠标扁一档，在设置页里更像"鼠标"而不是细长条。
		constexpr float BODY_ASPECT = 1.50f;
		// 本体圆角（相对本体宽）。左右键是本体里嵌着的两块圆角板，四周留一圈壳沿。
		constexpr float BODY_RADIUS = 0.26f;
		// 侧键探出本体左侧的比例（相对本体宽），居中时要把这一截算进总宽度。
		constexpr float SIDE_PROTRUSION = 0.10f;

		constexpr float BUTTON_INSET = 0.03f;
		constexpr float BUTTON_TOP = 0.02f;
		constexpr float BUTTON_HEIGHT = 0.42f;
		constexpr float BUTTON_RADIUS = 0.11f;
		constexpr float SEAM_WIDTH = 0.030f;

		// 滚轮整簇压在左右键接缝上，纵向只到 0.26H：左右键下沿要给「Mouse 1/2」文字留位置。
		constexpr float WHEEL_WIDTH = 0.20f;
		constexpr float WHEEL_TOP = 0.040f;
		constexpr float WHEEL_HEIGHT = 0.22f;
		constexpr float WHEEL_GAP = 0.010f;
		// 滚轮三段：上滚 / 中键 / 下滚。中键是按钮，给它多一点高度。
		constexpr float WHEEL_UP_PART = 0.30f;
		constexpr float WHEEL_MIDDLE_PART = 0.40f;

		// 倾斜滚轮：只画一对细箭头压在滚轮两侧，不再各占一枚药丸按钮。
		constexpr float TILT_WIDTH = 0.090f;
		constexpr float TILT_HEIGHT = 0.09f;

		constexpr float SIDE_WIDTH = 0.19f;
		constexpr float SIDE_HEIGHT = 0.12f;
		// 侧键有多宽伸到本体外面，剩下的压进本体：贴着左边框，不像浮在半空的圆片。
		constexpr float SIDE_OUTSET = 0.62f;
		constexpr float SIDE_TOP_FORWARD = 0.48f;
		constexpr float SIDE_TOP_BACK = 0.64f;

		bool InsideRect(const CUIRect &Rect, const float X, const float Y)
		{
			return X >= Rect.x && X < Rect.x + Rect.w && Y >= Rect.y && Y < Rect.y + Rect.h;
		}
	} // namespace

	float DiagramHeight(const float LineHeight)
	{
		return std::max(1.0f, LineHeight) * DIAGRAM_HEIGHT_LINES;
	}

	SDiagram Layout(const CUIRect &Area)
	{
		SDiagram Out;
		Out.m_Area = Area;

		const float AreaW = std::max(1.0f, Area.w);
		const float AreaH = std::max(1.0f, Area.h);
		// 本体始终按长宽比缩放：高度够就吃满预留高度，宽度不够就整体缩小并上下居中。
		const float BodyW = std::max(1.0f, std::min(AreaH / BODY_ASPECT, AreaW / (1.0f + SIDE_PROTRUSION)));
		const float BodyH = BodyW * BODY_ASPECT;
		const float SideOut = BodyW * SIDE_PROTRUSION;
		const float TotalW = BodyW + SideOut;
		// 侧键只往左探，居中时把属于它的那一截算进去，视觉重心才落回中间。
		const float X = Area.x + std::max(0.0f, AreaW - TotalW) * 0.5f + SideOut;
		const float Y = Area.y + std::max(0.0f, AreaH - BodyH) * 0.5f;
		Out.m_Body = CUIRect{X, Y, BodyW, BodyH};
		const float H = BodyH;

		const float W = BodyW;
		const float Center = X + W * 0.5f;
		const float BodyRadius = W * BODY_RADIUS;
		Out.m_BodyRadius = BodyRadius;

		// ---- 左右键：本体里嵌着的两块圆角板，四周留一圈壳沿 ----
		const float ButtonInset = W * BUTTON_INSET;
		const float ButtonTop = Y + H * BUTTON_TOP;
		const float ButtonHeight = H * BUTTON_HEIGHT;
		const float Seam = std::max(2.0f, W * SEAM_WIDTH);
		const float ButtonRadius = W * BUTTON_RADIUS;
		const float LeftButtonRight = Center - Seam * 0.5f;
		const float RightButtonLeft = Center + Seam * 0.5f;

		SZone &LeftButton = Out.m_aZones[ZONE_MOUSE1];
		LeftButton.m_Key = KEY_MOUSE_1;
		LeftButton.m_Rect = CUIRect{X + ButtonInset, ButtonTop, LeftButtonRight - (X + ButtonInset), ButtonHeight};
		LeftButton.m_Radius = ButtonRadius;
		LeftButton.m_Corners = CORNER_ALL;

		SZone &RightButton = Out.m_aZones[ZONE_MOUSE2];
		RightButton.m_Key = KEY_MOUSE_2;
		RightButton.m_Rect = CUIRect{RightButtonLeft, ButtonTop, (X + W - ButtonInset) - RightButtonLeft, ButtonHeight};
		RightButton.m_Radius = ButtonRadius;
		RightButton.m_Corners = CORNER_ALL;

		// ---- 滚轮：三条竖着叠起来，压在两键的接缝上 ----
		const float WheelWidth = W * WHEEL_WIDTH;
		const float WheelTop = Y + H * WHEEL_TOP;
		const float WheelHeight = H * WHEEL_HEIGHT;
		const float WheelGap = std::max(1.0f, W * WHEEL_GAP);
		const float WheelX = Center - WheelWidth * 0.5f;
		const float WheelInnerHeight = std::max(1.0f, WheelHeight - 2.0f * WheelGap);
		const float WheelUpHeight = WheelInnerHeight * WHEEL_UP_PART;
		const float WheelMiddleHeight = WheelInnerHeight * WHEEL_MIDDLE_PART;
		const float WheelDownHeight = std::max(1.0f, WheelInnerHeight - WheelUpHeight - WheelMiddleHeight);
		const float WheelRadius = WheelWidth * 0.40f;
		Out.m_Wheel = CUIRect{WheelX, WheelTop, WheelWidth, WheelHeight};
		Out.m_WheelRadius = WheelWidth * 0.5f;

		SZone &WheelUp = Out.m_aZones[ZONE_WHEEL_UP];
		WheelUp.m_Key = KEY_MOUSE_WHEEL_UP;
		WheelUp.m_Rect = CUIRect{WheelX, WheelTop, WheelWidth, WheelUpHeight};
		WheelUp.m_Radius = WheelRadius;

		SZone &WheelMiddle = Out.m_aZones[ZONE_MOUSE3];
		WheelMiddle.m_Key = KEY_MOUSE_3;
		WheelMiddle.m_Rect = CUIRect{WheelX, WheelTop + WheelUpHeight + WheelGap, WheelWidth, WheelMiddleHeight};
		WheelMiddle.m_Radius = WheelRadius;

		SZone &WheelDown = Out.m_aZones[ZONE_WHEEL_DOWN];
		WheelDown.m_Key = KEY_MOUSE_WHEEL_DOWN;
		WheelDown.m_Rect = CUIRect{WheelX, WheelTop + WheelUpHeight + WheelMiddleHeight + 2.0f * WheelGap, WheelWidth, WheelDownHeight};
		WheelDown.m_Radius = WheelRadius;

		// ---- 滚轮左右倾斜：贴在滚轮两侧，和滚轮等高居中 ----
		const float TiltWidth = W * TILT_WIDTH;
		const float TiltHeight = H * TILT_HEIGHT;
		const float TiltGap = WheelGap;
		const float TiltY = WheelTop + WheelHeight * 0.5f - TiltHeight * 0.5f;
		const float TiltRadius = std::min(TiltWidth, TiltHeight) * 0.42f;

		SZone &TiltLeft = Out.m_aZones[ZONE_WHEEL_LEFT];
		TiltLeft.m_Key = KEY_MOUSE_WHEEL_LEFT;
		TiltLeft.m_Rect = CUIRect{WheelX - TiltGap - TiltWidth, TiltY, TiltWidth, TiltHeight};
		TiltLeft.m_Radius = TiltRadius;

		SZone &TiltRight = Out.m_aZones[ZONE_WHEEL_RIGHT];
		TiltRight.m_Key = KEY_MOUSE_WHEEL_RIGHT;
		TiltRight.m_Rect = CUIRect{WheelX + WheelWidth + TiltGap, TiltY, TiltWidth, TiltHeight};
		TiltRight.m_Radius = TiltRadius;

		// ---- 侧键：左侧两颗，前面的（更靠上）是 Mouse 5，后面的是 Mouse 4 ----
		const float SideWidth = W * SIDE_WIDTH;
		const float SideHeight = H * SIDE_HEIGHT;
		const float SideX = X - SideWidth * SIDE_OUTSET;
		const float SideRadius = std::min(SideWidth, SideHeight) * 0.42f;

		SZone &SideForward = Out.m_aZones[ZONE_MOUSE5];
		SideForward.m_Key = KEY_MOUSE_5;
		SideForward.m_Rect = CUIRect{SideX, Y + H * SIDE_TOP_FORWARD, SideWidth, SideHeight};
		SideForward.m_Radius = SideRadius;

		SZone &SideBack = Out.m_aZones[ZONE_MOUSE4];
		SideBack.m_Key = KEY_MOUSE_4;
		SideBack.m_Rect = CUIRect{SideX, Y + H * SIDE_TOP_BACK, SideWidth, SideHeight};
		SideBack.m_Radius = SideRadius;

		return Out;
	}

	int HitTest(const SDiagram &Diagram, const float X, const float Y)
	{
		for(const int Zone : ZONE_HIT_ORDER)
		{
			if(InsideRect(Diagram.m_aZones[Zone].m_Rect, X, Y))
				return Zone;
		}
		return -1;
	}

	bool IsMouseKey(const int Key)
	{
		return Key >= KEY_MOUSE_1 && Key <= KEY_MOUSE_WHEEL_RIGHT;
	}
} // namespace qm_bind_mouse
