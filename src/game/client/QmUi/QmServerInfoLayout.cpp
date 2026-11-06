#include "QmServerInfoLayout.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

QmServerInfoGeometry::SLayout CQmServerInfoLayout::Render(CUi *pUi, CUIRect View, bool ShowTags)
{
	using namespace QmServerInfoGeometry;
	m_PreviewLine = {};

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

		if(!pUi->RenderOnly() && !pUi->IsPopupOpen() && pUi->IsActiveItem(&m_aDividerIds[Index]))
		{
			int aPreviewRatios[] = {*apRatios[0], *apRatios[1], *apRatios[2]};
			aPreviewRatios[Index] = m_PendingRatio;
			const auto Preview = Layout(View, aPreviewRatios[0], aPreviewRatios[1], aPreviewRatios[2], ShowTags);
			CUIRect Line = Preview.m_aDividers[Index];
			if(Horizontal)
				Line.HMargin(std::max(0.0f, (Line.h - 2.0f) * 0.5f), &Line);
			else
				Line.VMargin(std::max(0.0f, (Line.w - 2.0f) * 0.5f), &Line);
			m_PreviewLine = Line;
		}
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
