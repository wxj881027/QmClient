/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QM_UI_QM_RT_H
#define GAME_CLIENT_QM_UI_QM_RT_H

#include "QmAnim.h"
#include "QmLayout.h"
#include "QmRender.h"
#include "QmTree.h"

struct SUiV2PerfStats
{
	float m_BuildTreeMs = 0.0f;
	float m_LayoutMs = 0.0f;
	float m_AnimMs = 0.0f;
	float m_RenderBridgeMs = 0.0f;
	int m_NodeCount = 0;
};

class CUiRuntimeV2
{
public:
	void Init(class CGameClient *pGameClient);
	void Reset();
	void OnRender();
	bool Enabled() const;
	CUiV2AnimationRuntime &AnimRuntime() { return m_AnimRuntime; }
	const CUiV2AnimationRuntime &AnimRuntime() const { return m_AnimRuntime; }

	const SUiV2PerfStats &LastStats() const;

private:
	class CGameClient *m_pGameClient = nullptr;
	CUiV2Tree m_Tree;
	CUiV2LayoutEngine m_LayoutEngine;
	CUiV2AnimationRuntime m_AnimRuntime;
	CUiV2RenderBridge m_RenderBridge;
	SUiV2PerfStats m_LastStats;
	float m_DebugLogAccumulator = 0.0f;
};

#endif
