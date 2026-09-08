/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_UI_CARD_DRAG_LOGIC_H
#define GAME_CLIENT_UI_CARD_DRAG_LOGIC_H

#include "card_order_model.h"

#include <array>
#include <string>
#include <vector>

// 拖拽相关几何使用独立矩形类型，保持本文件可脱离游戏 UI 单测。
struct SCardDragRect
{
	float m_X = 0.0f;
	float m_Y = 0.0f;
	float m_W = 0.0f;
	float m_H = 0.0f;
};

bool CardDragRectContains(const SCardDragRect &Rect, float X, float Y);
bool CardDragThresholdExceeded(float StartX, float StartY, float X, float Y, float Threshold);

enum class ECardDragPhase
{
	IDLE,
	ARMED,
	DRAGGING,
};

struct SCardDragItem
{
	std::string m_Id;
	int m_Column = 0;
	SCardDragRect m_Rect;
};

// 一帧的静态布局几何快照：拖拽目标解析、让位预览和命中测试都从同一份
// 快照推导，避免让位动画反复改变目标造成抖动。
struct SCardDragFrame
{
	std::vector<SCardDragItem> m_vItems;
	SCardDragRect m_Viewport;
	std::array<SCardDragRect, 3> m_aColumnRects;
	std::vector<SCardDragRect> m_vPageTabRects;
	std::vector<std::string> m_vPageTabIds;
	bool m_TwoColumns = false;
	bool m_SingleVisualColumn = false;
};

struct SCardDragInput
{
	float m_X = 0.0f;
	float m_Y = 0.0f;
	bool m_Pressed = false;
	bool m_Down = false;
	bool m_Released = false;
	bool m_Cancelled = false;
	float m_Dt = 1.0f / 60.0f;
	float m_Scale = 1.0f;
};

struct SCardDragUpdate
{
	bool m_Started = false;         // 超过阈值进入 DRAGGING，调用方获取输入捕获
	bool m_TargetChanged = false;
	bool m_PreviewPageChanged = false;
	bool m_Committed = false;       // 松手提交；目标列/序以 TargetColumn/TargetOrder 为准
	bool m_Cancelled = false;       // Escape 或失效；调用方恢复原布局
	float m_AutoScrollDelta = 0.0f;
	// 提交事务（m_Committed 时有效）：状态机内部已复位，事务数据随结果带出。
	std::string m_CardId;
	std::string m_SourcePageId;
	std::string m_TargetPageId;
	int m_TargetColumn = 0;
	int m_TargetOrder = 0;
};

// 拖拽状态机：idle → armed（按下 header）→ dragging（超过阈值）→ commit/cancel。
// 松手才提交事务；拖拽预览不修改已提交布局。
class CCardDragState
{
public:
	void Arm(std::string PageId, std::string CardId, int Column, SCardDragRect HeaderRect, float GrabX, float GrabY);
	void Reset();
	SCardDragUpdate Update(const SCardDragInput &Input, const SCardDragFrame &Frame, float Threshold);

	ECardDragPhase Phase() const { return m_Phase; }
	const std::string &SourcePageId() const { return m_SourcePageId; }
	const std::string &CardId() const { return m_CardId; }
	int SourceColumn() const { return m_SourceColumn; }
	// 普通拖拽时等于来源页；指针悬停页面 tab 时切换为目标页预览并保留事务。
	const std::string &PreviewPageId() const { return m_PreviewPageId; }
	int TargetColumn() const { return m_TargetColumn; }
	int TargetOrder() const { return m_TargetOrder; }
	float GrabOffsetX() const { return m_GrabOffsetX; }
	float GrabOffsetY() const { return m_GrabOffsetY; }
	float PointerX() const { return m_PointerX; }
	float PointerY() const { return m_PointerY; }

private:
	void ResolveTarget(const SCardDragInput &Input, const SCardDragFrame &Frame, SCardDragUpdate &Update);

	ECardDragPhase m_Phase = ECardDragPhase::IDLE;
	std::string m_SourcePageId;
	std::string m_CardId;
	std::string m_PreviewPageId;
	int m_SourceColumn = 0;
	int m_TargetColumn = 0;
	int m_TargetOrder = 0;
	SCardDragRect m_HeaderRect;
	float m_GrabOffsetX = 0.0f;
	float m_GrabOffsetY = 0.0f;
	float m_StartX = 0.0f;
	float m_StartY = 0.0f;
	float m_PointerX = 0.0f;
	float m_PointerY = 0.0f;
	bool m_HasStart = false;
};

// 目标命中使用稳定布局槽位：返回 MouseY 在目标列中的可见插入序（排除被拖卡片）。
int ResolveCardDropOrder(float MouseY, int TargetColumn, const std::vector<SCardDragItem> &vItems, const std::string &DraggedId);
// 拖拽预览：把被拖卡片从各列移除后插入目标列的插入位。
void ApplyCardDragPlacement(std::array<std::vector<std::string>, 3> &aColumns, const std::string &DraggedId, int TargetColumn, int TargetOrder);
// 单视觉列：drop order 以左列可见序解析，预览把被拖卡插为左列第 TargetOrder
// 个卡片（与提交映射一致）；full 卡保持 full 语义，右列卡片保持原位。
void ApplyCardDragSingleColumnPlacement(std::array<std::vector<std::string>, 3> &aColumns, const std::string &DraggedId, int TargetOrder);
// 边缘自动滚动速度（像素/秒），由调用方按帧时长应用。
float CardDragAutoScrollDelta(float MouseY, const SCardDragRect &Viewport, float UiScale);

// 让位动画轨道：目标变化时从当前动画位置续接，ease-out cubic；
// 动画关闭或非拖拽几何变化时直接对齐目标，最终位置与输入结果不变。
struct SCardReflowTrack
{
	float m_Value = 0.0f;
	float m_From = 0.0f;
	float m_Target = 0.0f;
	float m_Elapsed = 0.0f;
	float m_Duration = 0.0f;
	bool m_Initialized = false;
	bool m_Active = false;
};

void UpdateCardReflowTrack(SCardReflowTrack &Track, float Target, float Dt, float Duration, bool Snap);

#endif
