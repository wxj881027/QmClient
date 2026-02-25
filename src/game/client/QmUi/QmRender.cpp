/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmRender.h"

void CUiV2RenderBridge::BeginFrame()
{
	m_LastFrameStats = {};
}

void CUiV2RenderBridge::RecordDrawCall()
{
	++m_LastFrameStats.m_DrawCalls;
}

void CUiV2RenderBridge::RecordClipPush()
{
	++m_LastFrameStats.m_ClipPushes;
}

const SUiV2RenderStats &CUiV2RenderBridge::LastFrameStats() const
{
	return m_LastFrameStats;
}
