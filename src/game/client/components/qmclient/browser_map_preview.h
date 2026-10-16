#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_MAP_PREVIEW_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_MAP_PREVIEW_H

#include <base/vmath.h>

#include <game/client/ui.h>

#include <algorithm>
#include <cmath>

// 相机直接消费引擎的视野尺寸；缩放后重新渲染，避免放大整图缩略纹理。
class CQmBrowserMapPreviewView
{
	vec2 m_WorldSize{1.0f, 1.0f};
	vec2 m_BaseView{1.0f, 1.0f};
	vec2 m_Center{0.5f, 0.5f};
	vec2 m_DragMouse{};
	float m_FitZoom = 1.0f;
	float m_ZoomFactor = 1.0f;
	bool m_Dragging = false;

	void ClampCenter()
	{
		// 允许边缘留白，整图适配时也能拖动；中心不离开地图，避免拖到完全找不到图。
		m_Center.x = std::clamp(m_Center.x, 0.0f, m_WorldSize.x);
		m_Center.y = std::clamp(m_Center.y, 0.0f, m_WorldSize.y);
	}

public:
	void FocusStart(vec2 Position)
	{
		m_Center = Position;
		m_ZoomFactor = std::max(1.0f, m_FitZoom);
		ClampCenter();
		EndDrag();
	}

	void Configure(vec2 WorldSize, vec2 BaseView)
	{
		WorldSize = vec2(std::max(1.0f, WorldSize.x), std::max(1.0f, WorldSize.y));
		BaseView = vec2(std::max(1.0f, BaseView.x), std::max(1.0f, BaseView.y));
		if(WorldSize != m_WorldSize)
		{
			m_WorldSize = WorldSize;
			m_Center = WorldSize * 0.5f;
			m_ZoomFactor = 1.0f;
			EndDrag();
		}
		m_BaseView = BaseView;
		// 引擎 Zoom 越大，视野越大；必须取两个维度的最大比例才能容纳全图。
		m_FitZoom = std::max(WorldSize.x / BaseView.x, WorldSize.y / BaseView.y);
		ClampCenter();
	}

	vec2 Center() const { return m_Center; }
	float Zoom() const { return m_FitZoom / m_ZoomFactor; }
	float ZoomFactor() const { return m_ZoomFactor; }
	vec2 ViewSize() const { return m_BaseView * Zoom(); }
	bool Dragging() const { return m_Dragging; }

	void ZoomAt(float Steps, vec2 Anchor)
	{
		const vec2 Before = ViewSize();
		// 至少支持 64 倍放大；大地图继续放大到可辨认单个瓦片的视野。
		m_ZoomFactor = std::clamp(m_ZoomFactor * std::pow(1.25f, std::clamp(Steps, -32.0f, 32.0f)), 1.0f, std::max(64.0f, m_FitZoom / 0.25f));
		m_Center += (Before - ViewSize()) * (Anchor - vec2(0.5f, 0.5f));
		if(m_ZoomFactor == 1.0f)
			m_Center = m_WorldSize * 0.5f;
		ClampCenter();
	}

	void BeginDrag(vec2 Mouse)
	{
		m_DragMouse = Mouse;
		m_Dragging = true;
	}
	void EndDrag() { m_Dragging = false; }
	void DragTo(vec2 Mouse, vec2 ViewportSize)
	{
		if(!m_Dragging || ViewportSize.x <= 0.0f || ViewportSize.y <= 0.0f)
			return;
		m_Center -= (Mouse - m_DragMouse) * ViewSize() / ViewportSize;
		m_DragMouse = Mouse;
		// 逐次消费鼠标位移，抵达边界后反向拖动立即生效。
		ClampCenter();
	}
};

inline void QmBrowserMapPreviewInput(CUi &Ui, CQmBrowserMapPreviewView &View, const CUIRect &Canvas, bool Active)
{
	if(!Active || Ui.RenderOnly())
	{
		View.EndDrag();
		if(!Ui.RenderOnly() && Ui.IsActiveItem(&View))
			Ui.SetActiveItem(nullptr);
		return;
	}
	Ui.RegisterWheelOwner(&View, EUiWheelOwnerPriority::COMPOSITE_CONTROL, Canvas, Ui.MouseHovered(&Canvas));
	float Wheel = 0.0f;
	if(Canvas.w > 0.0f && Canvas.h > 0.0f && Ui.TryConsumeWheel(&View, &Wheel))
		View.ZoomAt(Wheel / 120.0f, (Ui.MousePos() - Canvas.TopLeft()) / Canvas.Size());
	if(Ui.CheckActiveItem(&View))
	{
		if(!Ui.MouseButton(0))
		{
			View.EndDrag();
			Ui.SetActiveItem(nullptr);
		}
		else
			View.DragTo(Ui.MousePos(), Canvas.Size());
	}
	else if(Ui.MouseHovered(&Canvas) && Ui.MouseButtonClicked(0))
	{
		View.BeginDrag(Ui.MousePos());
		Ui.SetActiveItem(&View);
	}
	else
		View.EndDrag();
}

#endif
