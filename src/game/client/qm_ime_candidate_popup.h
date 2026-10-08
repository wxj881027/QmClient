// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QM_IME_CANDIDATE_POPUP_H
#define GAME_CLIENT_QM_IME_CANDIDATE_POPUP_H

#include "QmUi/QmAnimResolve.h"

#include <base/vmath.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

class CGameClient;

struct SQmImePopupState
{
	bool m_Visible = false;
	bool m_Disabled = false;
	std::string m_Composition;
	std::vector<std::string> m_vCandidates;
	int m_SelectedIndex = -1;
	int m_PageIndex = -1;
	int m_PageCount = 0;
	vec2 m_AnchorScreen = vec2(0.0f, 0.0f);
	float m_LineHeightScreen = 0.0f;
};

inline bool QmImeHasPopupContent(const SQmImePopupState &State)
{
	return State.m_Visible && !State.m_Disabled && !State.m_vCandidates.empty();
}

class CQmImeCandidateTransition
{
public:
	struct SLayer
	{
		SQmImePopupState m_State;
		int m_CandidateStart = 0;
		float m_Alpha = 0.0f;
		bool m_Active = false;
		uint64_t m_NodeKey = 0;
	};

	void Reset()
	{
		m_vLayers.clear();
		m_CurrentLayer = -1;
	}

	void Update(CUiV2AnimationRuntime &AnimRuntime, uint64_t ScopeKey, const SQmImePopupState &State, int CandidateStart, bool Animate)
	{
		if(!QmImeHasPopupContent(State))
		{
			Reset();
			return;
		}

		const bool FirstContent = m_CurrentLayer < 0;
		const bool Changed = FirstContent ||
			m_vLayers[m_CurrentLayer].m_CandidateStart != CandidateStart ||
			m_vLayers[m_CurrentLayer].m_State.m_vCandidates != State.m_vCandidates ||
			m_vLayers[m_CurrentLayer].m_State.m_PageIndex != State.m_PageIndex ||
			m_vLayers[m_CurrentLayer].m_State.m_PageCount != State.m_PageCount;
		if(Changed)
		{
			if(!FirstContent)
			{
				const SLayer &Previous = m_vLayers[m_CurrentLayer];
				const float Alpha = AnimRuntime.GetValue(Previous.m_NodeKey, EUiAnimProperty::ALPHA, Previous.m_Alpha);
				// 退场保留当前透明度，但清除淡入速度，避免打断后转回弹簧并留下长尾。
				AnimRuntime.SetValue(Previous.m_NodeKey, EUiAnimProperty::ALPHA, Alpha);
			}
			int LayerIndex = 0;
			while(LayerIndex < (int)m_vLayers.size() && m_vLayers[LayerIndex].m_Active)
				++LayerIndex;
			if(LayerIndex == (int)m_vLayers.size())
			{
				m_vLayers.emplace_back();
				m_vLayers.back().m_NodeKey = BuildUiAnimNodeKey(ScopeKey, LayerIndex);
			}
			else
			{
				// 复用已退场的动画节点，同时保持旧内容的绘制顺序，避免打断时叠加顺序变化。
				std::rotate(m_vLayers.begin() + LayerIndex, m_vLayers.begin() + LayerIndex + 1, m_vLayers.end());
			}
			SLayer &Layer = m_vLayers.back();
			Layer.m_State = State;
			Layer.m_CandidateStart = CandidateStart;
			Layer.m_Active = true;
			AnimRuntime.SetValue(Layer.m_NodeKey, EUiAnimProperty::ALPHA, FirstContent ? 1.0f : 0.0f);
			m_CurrentLayer = (int)m_vLayers.size() - 1;
		}
		else
		{
			// 选中项和光标移动不重播内容切换动画。
			m_vLayers[m_CurrentLayer].m_State.m_SelectedIndex = State.m_SelectedIndex;
		}

		SUiAnimTransition Transition;
		Transition.m_Driver = EUiAnimDriver::TWEEN;
		Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
		Transition.m_Easing = EEasing::EASE_OUT_QUART;
		float TotalAlpha = 0.0f;
		for(int i = 0; i < (int)m_vLayers.size(); ++i)
		{
			SLayer &Layer = m_vLayers[i];
			if(!Layer.m_Active)
				continue;
			const uint64_t NodeKey = Layer.m_NodeKey;
			const float TargetAlpha = i == m_CurrentLayer ? 1.0f : 0.0f;
			if(!Animate)
			{
				AnimRuntime.SetValue(NodeKey, EUiAnimProperty::ALPHA, TargetAlpha);
				Layer.m_Alpha = TargetAlpha;
			}
			else
			{
				Transition.m_DurationSec = i == m_CurrentLayer ? 0.12f : 0.08f;
				Layer.m_Alpha = std::clamp(AnimRuntime.ResolveTargetValue(NodeKey, EUiAnimProperty::ALPHA, TargetAlpha, Transition), 0.0f, 1.0f);
			}
			if(i != m_CurrentLayer && Layer.m_Alpha <= 0.001f)
			{
				AnimRuntime.SetValue(NodeKey, EUiAnimProperty::ALPHA, 0.0f);
				Layer = {};
				Layer.m_NodeKey = NodeKey;
				continue;
			}
			TotalAlpha += Layer.m_Alpha;
		}
		// 新内容当帧就清晰可见，旧内容仅占最多 15%，仍沿原来的 80ms 轨道退场。
		// 调整绘制权重，不重启旧轨道，连续输入不会延长更早候选的残影。
		if(TotalAlpha > 0.0f)
		{
			for(int i = 0; i < static_cast<int>(m_vLayers.size()); ++i)
			{
				SLayer &Layer = m_vLayers[i];
				Layer.m_Alpha = 0.15f * Layer.m_Alpha / TotalAlpha;
				if(i == m_CurrentLayer)
					Layer.m_Alpha += 0.85f;
			}
		}
	}

	const std::vector<SLayer> &Layers() const { return m_vLayers; }
	int CurrentLayerIndex() const { return m_CurrentLayer; }

private:
	std::vector<SLayer> m_vLayers;
	int m_CurrentLayer = -1;
};

class CQmImeCandidatePopup
{
public:
	void Reset();
	void Render(CGameClient *pGameClient, const SQmImePopupState &State);

private:
	struct SPresentationTargets
	{
		bool m_Initialized = false;
		float m_TargetX = 0.0f;
		float m_TargetY = 0.0f;
		float m_TargetWidth = 0.0f;
		float m_TargetHeight = 0.0f;
		float m_TargetRadius = 0.0f;
		float m_TargetAlpha = 0.0f;
		float m_TargetCandidateAlpha = 0.0f;
		float m_TargetCandidateScale = 1.0f;
		float m_TargetSelectedX = 0.0f;
		float m_TargetSelectedY = 0.0f;
		float m_TargetSelectedWidth = 0.0f;
		float m_TargetSelectedHeight = 0.0f;
	};

	SQmImePopupState m_LastState;
	CQmImeCandidateTransition m_ContentTransition;
	SPresentationTargets m_Presentation;
	int m_CandidateStart = 0;
	bool m_WasVisible = false;
	uint64_t m_PresenceGeneration = 1;
};

#endif
