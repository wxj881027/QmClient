/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "graph.h"

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <game/localization.h>

#include <cmath>
#include <limits>

namespace
{
	bool RoundGraphCoordinate(float Value, int &Result)
	{
		if(!std::isfinite(Value))
			return false;
		// 按原浮点舍入检查转换范围，避免 NaN、无穷及越界值转为 int。
		const double Rounded = Value > 0 ? Value + 0.5f : Value - 0.5f;
		if(Rounded < std::numeric_limits<int>::min() || Rounded > std::numeric_limits<int>::max())
			return false;
		Result = round_to_int(Value);
		return true;
	}
}

int CGraph::BufferSize(int MaxEntries)
{
	constexpr size_t EntrySize = sizeof(CEntry) + 2 * CRingBufferBase::ITEM_SIZE;
	if(MaxEntries <= 0 || static_cast<size_t>(MaxEntries) > std::numeric_limits<int>::max() / EntrySize)
		return 0;
	return static_cast<int>(MaxEntries * EntrySize);
}

CGraph::CGraph(int MaxEntries, int Precision, bool SummaryStats, FAllocate pAllocate) :
	m_EntryBufferSize(BufferSize(MaxEntries)),
	m_pAllocate(pAllocate),
	m_Entries(m_EntryBufferSize, CRingBufferBase::FLAG_RECYCLE, m_pAllocate),
	m_Precision(Precision),
	m_SummaryStats(SummaryStats)
{
}

void CGraph::Init(float Min, float Max)
{
	if(!m_Entries.IsInitialized())
		m_Entries.Init(m_EntryBufferSize, CRingBufferBase::FLAG_RECYCLE, m_pAllocate);
	m_Entries.Clear();
	m_pFirstScaled = nullptr;
	m_RenderedTotalTime = 0;
	m_Average = 0.0f;
	SetMin(Min);
	SetMax(Max);
}

void CGraph::SetMin(float Min)
{
	m_MinRange = m_MinValue = m_MinAxis = Min;
}

void CGraph::SetMax(float Max)
{
	m_MaxRange = m_MaxValue = m_MaxAxis = Max;
}

void CGraph::Scale(int64_t WantedTotalTime)
{
	// Scale X axis for wanted total time
	if(m_Entries.First() != nullptr)
	{
		const int64_t EndTime = m_Entries.Last()->m_Time;
		bool ScaleTotalTime = false;
		m_pFirstScaled = nullptr;

		if(m_Entries.First()->m_Time >= EndTime - WantedTotalTime)
		{
			m_pFirstScaled = m_Entries.First();
		}
		else
		{
			m_pFirstScaled = m_Entries.Last();
			while(m_pFirstScaled)
			{
				CEntry *pPrev = m_Entries.Prev(m_pFirstScaled);
				if(pPrev == nullptr)
					break;
				if(pPrev->m_Time < EndTime - WantedTotalTime)
				{
					// Scale based on actual total time instead of based on wanted total time,
					// to avoid flickering last segment due to rounding errors.
					ScaleTotalTime = true;
					break;
				}
				m_pFirstScaled = pPrev;
			}
		}

		m_RenderedTotalTime = ScaleTotalTime ? (EndTime - m_pFirstScaled->m_Time) : WantedTotalTime;

		// Ensure that color is applied to first line segment
		if(m_pFirstScaled)
		{
			m_pFirstScaled->m_ApplyColor = true;
			CEntry *pNext = m_Entries.Next(m_pFirstScaled);
			if(pNext != nullptr)
			{
				pNext->m_ApplyColor = true;
			}
		}
	}
	else
	{
		m_pFirstScaled = nullptr;
		m_RenderedTotalTime = 0;
	}

	// Scale Y axis
	m_Average = 0.0f;
	size_t NumValues = 0;
	m_MinAxis = m_MinRange;
	m_MaxAxis = m_MaxRange;
	m_MinValue = std::numeric_limits<float>::max();
	m_MaxValue = std::numeric_limits<float>::lowest();
	for(CEntry *pEntry = m_pFirstScaled; pEntry != nullptr; pEntry = m_Entries.Next(pEntry))
	{
		const float Value = pEntry->m_Value;
		m_Average += Value;
		++NumValues;

		if(Value > m_MaxAxis)
		{
			m_MaxAxis = Value;
		}
		else if(Value < m_MinAxis)
		{
			m_MinAxis = Value;
		}

		if(Value > m_MaxValue)
		{
			m_MaxValue = Value;
		}
		else if(Value < m_MinValue)
		{
			m_MinValue = Value;
		}
	}
	if(m_MaxValue < m_MinValue)
	{
		m_MinValue = m_MaxValue = 0.0f;
	}
	if(NumValues)
	{
		m_Average /= NumValues;
	}
}

bool CGraph::Add(float Value, ColorRGBA Color)
{
	return InsertAt(time_get(), Value, Color);
}

bool CGraph::InsertAt(int64_t Time, float Value, ColorRGBA Color)
{
	CEntry *pEntry = m_Entries.Allocate(sizeof(CEntry));
	if(pEntry == nullptr)
		return false;
	pEntry->m_Time = Time;
	pEntry->m_Value = Value;
	pEntry->m_Color = Color;

	// Determine whether the line (pPrev, pEntry) has different
	// vertex colors than the line (pPrevPrev, pPrev).
	CEntry *pPrev = m_Entries.Prev(pEntry);
	if(pPrev == nullptr)
	{
		pEntry->m_ApplyColor = true;
	}
	else
	{
		CEntry *pPrevPrev = m_Entries.Prev(pPrev);
		if(pPrevPrev == nullptr)
		{
			pEntry->m_ApplyColor = true;
		}
		else
		{
			pEntry->m_ApplyColor = Color != pPrev->m_Color || pPrev->m_Color != pPrevPrev->m_Color;
		}
	}
	return true;
}

void CGraph::RenderDataLines(IGraphics *pGraphics, float x, float y, float w, float h)
{
	if(m_pFirstScaled == nullptr || m_RenderedTotalTime <= 0 || !std::isfinite(m_MinAxis) || !std::isfinite(m_MaxAxis))
	{
		return;
	}
	const float AxisRange = m_MaxAxis - m_MinAxis;
	if(AxisRange <= 0.0f || !std::isfinite(AxisRange))
		return;

	IGraphics::CLineItemBatch LineItemBatch;
	pGraphics->LinesBatchBegin(&LineItemBatch);

	const int64_t StartTime = m_pFirstScaled->m_Time;
	const auto Project = [&](const CEntry *pEntry, int &X, int &Y) {
		return RoundGraphCoordinate((pEntry->m_Time - StartTime) * w / m_RenderedTotalTime, X) &&
		       RoundGraphCoordinate((pEntry->m_Value - m_MinAxis) * h / AxisRange, Y);
	};

	CEntry *pEntry0 = m_pFirstScaled;
	int a0 = 0;
	int v0 = 0;
	bool PreviousValid = Project(pEntry0, a0, v0);
	bool ApplyColor = true;
	while(pEntry0 != nullptr)
	{
		CEntry *pEntry1 = m_Entries.Next(pEntry0);
		if(pEntry1 == nullptr)
			break;

		int a1 = 0;
		int v1 = 0;
		const bool CurrentValid = Project(pEntry1, a1, v1);

		if(PreviousValid && CurrentValid)
		{
			if(ApplyColor || pEntry1->m_ApplyColor)
			{
				pGraphics->LinesBatchEnd(&LineItemBatch);
				pGraphics->LinesBatchBegin(&LineItemBatch);
				const IGraphics::CColorVertex aColorVertices[] = {
					IGraphics::CColorVertex(0, pEntry0->m_Color),
					IGraphics::CColorVertex(1, pEntry1->m_Color),
				};
				pGraphics->SetColorVertex(aColorVertices, std::size(aColorVertices));
			}
			const IGraphics::CLineItem Item = IGraphics::CLineItem(
				x + a0,
				y + h - v0,
				x + a1,
				y + h - v1);
			pGraphics->LinesBatchDraw(&LineItemBatch, &Item, 1);
			ApplyColor = false;
		}
		else
		{
			// 不跨越无效样本连接曲线，恢复后的首段重新提交颜色。
			ApplyColor = true;
		}

		pEntry0 = pEntry1;
		a0 = a1;
		v0 = v1;
		PreviousValid = CurrentValid;
	}
	pGraphics->LinesBatchEnd(&LineItemBatch);
}

void CGraph::Render(IGraphics *pGraphics, ITextRender *pTextRender, float x, float y, float w, float h, const char *pDescription)
{
	const float FontSize = 12.0f;
	const float Spacing = 2.0f;
	const float HeaderHeight = FontSize + 2.0f * Spacing;

	pGraphics->TextureClear();

	pGraphics->QuadsBegin();
	pGraphics->SetColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.7f));
	const IGraphics::CQuadItem ItemHeader(x, y, w, HeaderHeight);
	pGraphics->QuadsDrawTL(&ItemHeader, 1);
	const IGraphics::CQuadItem ItemFooter(x, y + h - HeaderHeight, w, HeaderHeight);
	pGraphics->QuadsDrawTL(&ItemFooter, 1);
	pGraphics->SetColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f));
	const IGraphics::CQuadItem ItemGraph(x, y + HeaderHeight, w, h - 2.0f * HeaderHeight);
	pGraphics->QuadsDrawTL(&ItemGraph, 1);
	pGraphics->QuadsEnd();

	pGraphics->LinesBegin();
	pGraphics->SetColor(ColorRGBA(0.5f, 0.5f, 0.5f, 0.75f));
	const IGraphics::CLineItem aLineItems[] = {
		IGraphics::CLineItem(ItemGraph.m_X, ItemGraph.m_Y + (ItemGraph.m_Height * 3) / 4, ItemGraph.m_X + ItemGraph.m_Width, ItemGraph.m_Y + (ItemGraph.m_Height * 3) / 4),
		IGraphics::CLineItem(ItemGraph.m_X, ItemGraph.m_Y + ItemGraph.m_Height / 2, ItemGraph.m_X + ItemGraph.m_Width, ItemGraph.m_Y + ItemGraph.m_Height / 2),
		IGraphics::CLineItem(ItemGraph.m_X, ItemGraph.m_Y + ItemGraph.m_Height / 4, ItemGraph.m_X + ItemGraph.m_Width, ItemGraph.m_Y + ItemGraph.m_Height / 4)};
	pGraphics->LinesDraw(aLineItems, std::size(aLineItems));
	pGraphics->LinesEnd();

	RenderDataLines(pGraphics, ItemGraph.m_X, ItemGraph.m_Y + 1.0f, ItemGraph.m_Width, ItemGraph.m_Height + 2.0f);

	char aBuf[128];

	pTextRender->Text(ItemHeader.m_X + Spacing, ItemHeader.m_Y + Spacing, FontSize, pDescription);

	if(m_SummaryStats)
	{
		str_format(aBuf, sizeof(aBuf), Localize("Avg %.*f   ↓ %.*f   ↑ %.*f"), m_Precision, m_Average, m_Precision, m_MinValue, m_Precision, m_MaxValue);
		pTextRender->Text(ItemFooter.m_X + Spacing, ItemFooter.m_Y + ItemFooter.m_Height - FontSize - Spacing, FontSize, aBuf);
	}

	str_format(aBuf, sizeof(aBuf), "%.*f", m_Precision, m_MaxAxis);
	pTextRender->Text(ItemHeader.m_X + ItemHeader.m_Width - pTextRender->TextWidth(FontSize, aBuf) - Spacing, ItemHeader.m_Y + Spacing, FontSize, aBuf);

	str_format(aBuf, sizeof(aBuf), "%.*f", m_Precision, m_MinAxis);
	pTextRender->Text(ItemFooter.m_X + ItemFooter.m_Width - pTextRender->TextWidth(FontSize, aBuf) - Spacing, ItemFooter.m_Y + ItemFooter.m_Height - FontSize - Spacing, FontSize, aBuf);
}
