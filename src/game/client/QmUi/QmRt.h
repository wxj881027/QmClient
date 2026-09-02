// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMRT_H
#define GAME_CLIENT_QMUI_QMRT_H

#include "QmAnim.h"
#include "QmLayout.h"
#include "QmRender.h"
#include "QmTree.h"

struct SUiV2PerfStats
{
	float m_BuildTreeMs = 0.0f;
	float m_AnimMs = 0.0f;
	float m_RenderBridgeMs = 0.0f;
	int m_NodeCount = 0;
	int m_ActiveAnimCount = 0;
	int m_QueuedAnimCount = 0;
};

class CUiRuntimeV2
{
public:
	void Init(class CGameClient *pGameClient);
	void Reset();
	void OnRender();
	bool Enabled() const;
	void SetPerfContext(const char *pPage, const char *pOperation);
	void ClearPerfContext();
	CUiV2Tree &Tree() { return m_Tree; }
	const CUiV2Tree &Tree() const { return m_Tree; }
	CUiV2AnimationRuntime &AnimRuntime() { return m_AnimRuntime; }
	const CUiV2AnimationRuntime &AnimRuntime() const { return m_AnimRuntime; }
	float FrameDt() const { return m_FrameDt; }
	const SUiV2PerfStats &LastStats() const;

private:
	class CGameClient *m_pGameClient = nullptr;
	CUiV2Tree m_Tree;
	CUiV2LayoutEngine m_LayoutEngine;
	CUiV2AnimationRuntime m_AnimRuntime;
	CUiV2RenderBridge m_RenderBridge;
	SUiV2PerfStats m_LastStats;
	float m_FrameDt = 1.0f / 60.0f;
	float m_DebugLogAccumulator = 0.0f;
	// perf/ui_runtime 逐帧日志降采样计数器（见 OnRender 注释）
	int m_PerfLogFrameCounter = 0;
	char m_aPerfPage[64] = "";
	char m_aPerfOperation[64] = "";
};

#endif
