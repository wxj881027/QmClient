/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#ifndef ENGINE_CLIENT_GRAPH_H
#define ENGINE_CLIENT_GRAPH_H

#include <base/color.h>

#include <engine/shared/ringbuffer.h>

#include <cstddef>

class IGraphics;
class ITextRender;

class CGraph
{
private:
	class CEntry
	{
	public:
		int64_t m_Time;
		float m_Value;
		ColorRGBA m_Color;
		bool m_ApplyColor;
	};
	CEntry *m_pFirstScaled = nullptr;
	int64_t m_RenderedTotalTime = 0;
	float m_Average;
	float m_MinAxis, m_MaxAxis;
	float m_MinValue, m_MaxValue;
	float m_MinRange, m_MaxRange;
	int m_EntryBufferSize;
	CDynamicRingBuffer<CEntry>::FAllocate m_pAllocate;
	CDynamicRingBuffer<CEntry> m_Entries;
	int m_Precision;
	bool m_SummaryStats;

	static int BufferSize(int MaxEntries);
	void RenderDataLines(IGraphics *pGraphics, float x, float y, float w, float h);

public:
	using FAllocate = CDynamicRingBuffer<CEntry>::FAllocate;

	CGraph(int MaxEntries, int Precision, bool SummaryStats, FAllocate pAllocate = malloc);

	void Init(float Min, float Max);
	void SetMin(float Min);
	void SetMax(float Max);

	void Scale(int64_t WantedTotalTime);
	bool Add(float Value, ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f));
	bool InsertAt(int64_t Time, float Value, ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f));
	void Render(IGraphics *pGraphics, ITextRender *pTextRender, float x, float y, float w, float h, const char *pDescription);
};

#endif
