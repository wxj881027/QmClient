#include "QmServerInfoLayout.h"

#include "UiButtons.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/localization.h>

QmServerInfoGeometry::SLayout CQmServerInfoLayout::Render(CUi *pUi, CUIRect View, bool ShowTags)
{
	using namespace QmServerInfoGeometry;
	m_PreviewLine = {};
	CUIRect Toolbar, Reset;
	View.HSplitTop(22.0f, &Toolbar, &View);
	Toolbar.VSplitRight(std::min(110.0f, Toolbar.w), nullptr, &Reset);
	View.HSplitTop(5.0f, nullptr, &View);
	if(ui_widget::SecondaryButton(ui_widget::ControlContext(pUi), &m_ResetButton, Localize("Reset layout"), Reset))
	{
		g_Config.m_QmServerInfoRows = 500;
		g_Config.m_QmServerInfoTopColumns = 500;
		g_Config.m_QmServerInfoBottomColumns = 380;
	}

	auto BuildLayout = [&] { return Layout(View, g_Config.m_QmServerInfoRows, g_Config.m_QmServerInfoTopColumns, g_Config.m_QmServerInfoBottomColumns, ShowTags); };
	SLayout Result = BuildLayout();
	int *apRatios[] = {&g_Config.m_QmServerInfoRows, &g_Config.m_QmServerInfoTopColumns, &g_Config.m_QmServerInfoBottomColumns};
	for(int Index = 0; Index < (ShowTags ? 3 : 2); ++Index)
	{
		const CUIRect Handle = Result.m_aDividers[Index];
		const bool Horizontal = Index == 0;
		const float Pointer = Horizontal ? pUi->MouseY() : pUi->MouseX();
		const float Origin = Horizontal ? View.y : View.x;
		const float DividerPosition = Horizontal ? Handle.y : Handle.x;
		const float Available = std::max(0.0f, (Horizontal ? View.h - Handle.h : View.w - Handle.w));
		if(!pUi->RenderOnly() && !pUi->IsPopupOpen())
		{
			const EEditState State = pUi->DoPickerLogic(&m_aDividerIds[Index], &Handle, nullptr, nullptr);
			if(State == EEditState::START)
			{
				m_GrabOffset = Pointer - DividerPosition;
				m_PendingRatio = *apRatios[Index];
			}
			else if(State == EEditState::EDITING || State == EEditState::END)
			{
				const float MinimumFirst = Horizontal ? 110.0f : (Index == 1 ? 280.0f : 180.0f);
				const float MinimumSecond = Horizontal ? (ShowTags ? 160.0f : 100.0f) : (Index == 1 ? 280.0f : 220.0f);
				m_PendingRatio = DragRatio(Pointer, Origin, m_GrabOffset, Available, MinimumFirst, MinimumSecond);
				if(State == EEditState::END)
				{
					// 松手才提交布局，避免拖动每一帧都为长公告和信息字段建立新文字容器。
					*apRatios[Index] = m_PendingRatio;
					Result = BuildLayout();
				}
			}
		}

		const bool Highlight = !pUi->RenderOnly() && !pUi->IsPopupOpen() && (pUi->IsActiveItem(&m_aDividerIds[Index]) || pUi->MouseHovered(&Result.m_aDividers[Index]));
		CUIRect Grip = Result.m_aDividers[Index];
		if(!pUi->RenderOnly() && !pUi->IsPopupOpen() && pUi->IsActiveItem(&m_aDividerIds[Index]))
		{
			int aPreviewRatios[] = {*apRatios[0], *apRatios[1], *apRatios[2]};
			aPreviewRatios[Index] = m_PendingRatio;
			const auto Preview = Layout(View, aPreviewRatios[0], aPreviewRatios[1], aPreviewRatios[2], ShowTags);
			CUIRect Line = Preview.m_aDividers[Index];
			if(Horizontal)
				Line.HMargin(4.0f, &Line);
			else
				Line.VMargin(4.0f, &Line);
			m_PreviewLine = Line;
		}
		if(Horizontal)
		{
			Grip.x += std::max(0.0f, (Grip.w - 44.0f) * 0.5f);
			Grip.w = std::min(Grip.w, 44.0f);
			Grip.HMargin(3.0f, &Grip);
		}
		else
		{
			Grip.y += std::max(0.0f, (Grip.h - 44.0f) * 0.5f);
			Grip.h = std::min(Grip.h, 44.0f);
			Grip.VMargin(3.0f, &Grip);
		}
		if(Grip.w > 0.0f && Grip.h > 0.0f)
			Grip.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Highlight ? 0.8f : 0.25f), IGraphics::CORNER_ALL, 2.0f);
	}
	return Result;
}

void CQmServerInfoLayout::RenderOverlay() const
{
	if(m_PreviewLine.w > 0.0f && m_PreviewLine.h > 0.0f)
		m_PreviewLine.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.8f), IGraphics::CORNER_ALL, 1.0f);
}

void CQmServerInfoLayout::BeginCard(int Index, CUIRect &View, float ContentHeight, bool Collecting)
{
	vec2 Offset(0.0f, 0.0f);
	if(!Collecting)
	{
		CScrollRegionParams Params = QmScrollRegionParamsForSize(EQmScrollSize::MEDIUM);
		m_aScrollRegions[Index].Begin(&View, &Offset, &Params);
	}
	View.y += Offset.y;
	View.h = std::max(View.h, ContentHeight);
	if(!Collecting)
		m_aScrollRegions[Index].AddRect(View);
}

void CQmServerInfoLayout::EndCard(int Index, bool Collecting)
{
	if(!Collecting)
		m_aScrollRegions[Index].End();
}
