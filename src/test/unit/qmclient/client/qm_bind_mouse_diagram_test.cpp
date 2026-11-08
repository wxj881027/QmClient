// 绑定页鼠标图：区域归属、版式缩放和命中优先级。
// 鼠标图本体只做几何，界面层按这里的结果画，所以版式错了会直接在用例里暴露。
#include <game/client/components/qmclient/bind_mouse_diagram.h>

#include <gtest/gtest.h>

#include <set>

using namespace qm_bind_mouse;

namespace
{
	// 默认内容列宽与预留高度：与设置页实际传入的量级一致。
	constexpr float AREA_WIDTH = 600.0f;
	constexpr float LINE_HEIGHT = 20.0f;

	CUIRect DefaultArea()
	{
		return CUIRect{0.0f, 0.0f, AREA_WIDTH, DiagramHeight(LINE_HEIGHT)};
	}

	// 本体相对坐标 -> 屏幕坐标，用例里按「宽高百分之几」描述点位，不写死像素。
	vec2 BodyPoint(const SDiagram &Diagram, const float X, const float Y)
	{
		return vec2(Diagram.m_Body.x + Diagram.m_Body.w * X, Diagram.m_Body.y + Diagram.m_Body.h * Y);
	}

	vec2 ZoneCenter(const SDiagram &Diagram, const int Zone)
	{
		const CUIRect &Rect = Diagram.m_aZones[Zone].m_Rect;
		return vec2(Rect.x + Rect.w * 0.5f, Rect.y + Rect.h * 0.5f);
	}
} // namespace

TEST(QmBindMouseDiagram, LayoutMapsEveryMouseKeyToItsOwnZone)
{
	const SDiagram Diagram = Layout(DefaultArea());
	const std::set<int> ExpectedKeys = {
		KEY_MOUSE_1,
		KEY_MOUSE_2,
		KEY_MOUSE_3,
		KEY_MOUSE_4,
		KEY_MOUSE_5,
		KEY_MOUSE_WHEEL_UP,
		KEY_MOUSE_WHEEL_DOWN,
		KEY_MOUSE_WHEEL_LEFT,
		KEY_MOUSE_WHEEL_RIGHT,
	};
	std::set<int> ActualKeys;
	for(int Zone = 0; Zone < ZONE_COUNT; ++Zone)
	{
		const SZone &Entry = Diagram.m_aZones[Zone];
		ActualKeys.insert(Entry.m_Key);
		// 区域必须有可点的面积，否则图上是画了、玩家却点不到。
		EXPECT_GT(Entry.m_Rect.w, 1.0f) << "zone " << Zone;
		EXPECT_GT(Entry.m_Rect.h, 1.0f) << "zone " << Zone;
		EXPECT_GT(Entry.m_Radius, 0.0f) << "zone " << Zone;
		EXPECT_NE(Entry.m_Corners, 0) << "zone " << Zone;
	}
	EXPECT_EQ(ActualKeys, ExpectedKeys) << "every mouse key needs exactly one drawn region";
}

TEST(QmBindMouseDiagram, BodyKeepsAspectAndStaysInsideArea)
{
	// 高度受限（常见）与宽度受限（窄窗口 / 堆叠布局）两种可用区都要装得下。
	for(const CUIRect &Area : {CUIRect{0.0f, 0.0f, 600.0f, 240.0f}, CUIRect{40.0f, 12.0f, 150.0f, 240.0f}, CUIRect{0.0f, 0.0f, 900.0f, 240.0f}})
	{
		const SDiagram Diagram = Layout(Area);
		EXPECT_GE(Diagram.m_Body.x, Area.x);
		EXPECT_LE(Diagram.m_Body.x + Diagram.m_Body.w, Area.x + Area.w + 0.5f);
		EXPECT_GE(Diagram.m_Body.y, Area.y);
		EXPECT_LE(Diagram.m_Body.y + Diagram.m_Body.h, Area.y + Area.h + 0.5f);
		EXPECT_FLOAT_EQ(Diagram.m_Body.h, Diagram.m_Body.w * 1.50f);
		EXPECT_GT(Diagram.m_BodyRadius, 0.0f);
	}
	// 内容列很宽时不会把鼠标抽成一条细长的巨物：高度才是定尺寸的那一边。
	const SDiagram Wide = Layout(CUIRect{0.0f, 0.0f, 1400.0f, 240.0f});
	EXPECT_NEAR(Wide.m_Body.w, 240.0f / 1.50f, 0.5f);
	// 宽度受限时整体缩小并上下居中，不再强行拉满高度。
	const SDiagram Narrow = Layout(CUIRect{40.0f, 12.0f, 150.0f, 240.0f});
	EXPECT_LT(Narrow.m_Body.w, 150.0f);
	EXPECT_FLOAT_EQ(Narrow.m_Body.h, Narrow.m_Body.w * 1.50f);
	EXPECT_GT(Narrow.m_Body.y, 12.0f);
}

TEST(QmBindMouseDiagram, HitTestFindsEveryZoneUnderItsOwnArea)
{
	const SDiagram Diagram = Layout(DefaultArea());

	// 滚轮 / 倾斜 / 侧键互不重叠，直接打中心。
	for(const int Zone : {ZONE_WHEEL_UP, ZONE_MOUSE3, ZONE_WHEEL_DOWN, ZONE_WHEEL_LEFT, ZONE_WHEEL_RIGHT, ZONE_MOUSE5, ZONE_MOUSE4})
	{
		const vec2 Point = ZoneCenter(Diagram, Zone);
		EXPECT_EQ(HitTest(Diagram, Point.x, Point.y), Zone) << "zone " << Zone;
	}

	// 左右键下半部分（滚轮和倾斜区都在上方），取外侧靠近底边的点。
	const CUIRect &Left = Diagram.m_aZones[ZONE_MOUSE1].m_Rect;
	const vec2 LeftPoint(Left.x + Left.w * 0.20f, Left.y + Left.h * 0.92f);
	EXPECT_EQ(HitTest(Diagram, LeftPoint.x, LeftPoint.y), ZONE_MOUSE1);
	const CUIRect &Right = Diagram.m_aZones[ZONE_MOUSE2].m_Rect;
	const vec2 RightPoint(Right.x + Right.w * 0.80f, Right.y + Right.h * 0.92f);
	EXPECT_EQ(HitTest(Diagram, RightPoint.x, RightPoint.y), ZONE_MOUSE2);
}

TEST(QmBindMouseDiagram, WheelAndTiltWinOverButtonsWhereTheyOverlap)
{
	const SDiagram Diagram = Layout(DefaultArea());

	// 这个点在左键矩形内，同时也在滚轮上滚那一格内：点到的必须是滚轮。
	// 取滚轮上滚格靠左的位置（左键右边缘以内），才能构成真正的重叠。
	const CUIRect &WheelUp = Diagram.m_aZones[ZONE_WHEEL_UP].m_Rect;
	const vec2 OnWheelOverLeftButton(WheelUp.x + WheelUp.w * 0.25f, WheelUp.y + WheelUp.h * 0.5f);
	const CUIRect &LeftButton = Diagram.m_aZones[ZONE_MOUSE1].m_Rect;
	ASSERT_TRUE(OnWheelOverLeftButton.x >= LeftButton.x && OnWheelOverLeftButton.x <= LeftButton.x + LeftButton.w);
	ASSERT_TRUE(OnWheelOverLeftButton.y >= LeftButton.y && OnWheelOverLeftButton.y <= LeftButton.y + LeftButton.h);
	EXPECT_EQ(HitTest(Diagram, OnWheelOverLeftButton.x, OnWheelOverLeftButton.y), ZONE_WHEEL_UP);

	// 倾斜区同样盖在左键上：命中顺序把倾斜排在左右键前面。
	const CUIRect &TiltLeft = Diagram.m_aZones[ZONE_WHEEL_LEFT].m_Rect;
	const vec2 OnTiltOverLeftButton(TiltLeft.x + TiltLeft.w * 0.5f, TiltLeft.y + TiltLeft.h * 0.5f);
	ASSERT_TRUE(OnTiltOverLeftButton.x <= LeftButton.x + LeftButton.w);
	EXPECT_EQ(HitTest(Diagram, OnTiltOverLeftButton.x, OnTiltOverLeftButton.y), ZONE_WHEEL_LEFT);
}

TEST(QmBindMouseDiagram, PalmAndOutsideAreaAreNotClickable)
{
	const SDiagram Diagram = Layout(DefaultArea());
	const vec2 Palm = BodyPoint(Diagram, 0.5f, 0.95f);
	EXPECT_EQ(HitTest(Diagram, Palm.x, Palm.y), -1);
	EXPECT_EQ(HitTest(Diagram, Diagram.m_Area.x - 10.0f, Diagram.m_Area.y - 10.0f), -1);
	EXPECT_EQ(HitTest(Diagram, Diagram.m_Area.x + Diagram.m_Area.w + 10.0f, Diagram.m_Area.y + Diagram.m_Area.h + 10.0f), -1);
}

TEST(QmBindMouseDiagram, SideButtonsStickOutToTheLeftOfTheBody)
{
	const SDiagram Diagram = Layout(DefaultArea());
	for(const int Zone : {ZONE_MOUSE4, ZONE_MOUSE5})
	{
		const CUIRect &Rect = Diagram.m_aZones[Zone].m_Rect;
		EXPECT_LT(Rect.x, Diagram.m_Body.x) << "side buttons are drawn outside the shell";
		EXPECT_GT(Rect.x + Rect.w, Diagram.m_Body.x) << "but still overlap it, so they read as attached";
		EXPECT_GE(Rect.x, Diagram.m_Area.x);
	}
	// 前侧键（更靠上的那颗）是 Mouse 5，后侧键是 Mouse 4：和输入浮层素材的编号一致。
	EXPECT_LT(Diagram.m_aZones[ZONE_MOUSE5].m_Rect.y, Diagram.m_aZones[ZONE_MOUSE4].m_Rect.y);
}

TEST(QmBindMouseDiagram, DiagramHeightFollowsLineHeight)
{
	EXPECT_FLOAT_EQ(DiagramHeight(20.0f), 20.0f * DIAGRAM_HEIGHT_LINES);
	EXPECT_FLOAT_EQ(DiagramHeight(40.0f), 40.0f * DIAGRAM_HEIGHT_LINES);
	// 行高异常时不能给出 0 高度：内容流会把整块折叠掉。
	EXPECT_GT(DiagramHeight(0.0f), 0.0f);
}

TEST(QmBindMouseDiagram, MouseKeyRangeCoversButtonsAndWheelDirections)
{
	for(const int Key : {KEY_MOUSE_1, KEY_MOUSE_2, KEY_MOUSE_3, KEY_MOUSE_4, KEY_MOUSE_5, KEY_MOUSE_WHEEL_UP, KEY_MOUSE_WHEEL_DOWN, KEY_MOUSE_WHEEL_LEFT, KEY_MOUSE_WHEEL_RIGHT})
		EXPECT_TRUE(IsMouseKey(Key)) << Key;
	EXPECT_FALSE(IsMouseKey(KEY_A));
	EXPECT_FALSE(IsMouseKey(KEY_SPACE));
	EXPECT_FALSE(IsMouseKey(KEY_UNKNOWN));
}
