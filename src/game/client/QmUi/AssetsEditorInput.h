#ifndef GAME_CLIENT_QMUI_ASSETSEDITORINPUT_H
#define GAME_CLIENT_QMUI_ASSETSEDITORINPUT_H

#include <game/client/ui.h>

#include <vector>

namespace qmclient::assets_editor
{
	enum class ECanvasAction
	{
		NONE,
		SELECT,
		CYCLE,
		PICK_COLOR,
		QUICK_REPLACE,
		BEGIN_DRAG,
		DROP
	};

	struct SCanvasHit
	{
		int m_Index = -1;
		bool m_Target = false;
		bool operator==(const SCanvasHit &) const = default;
	};

	inline int QuickReplaceTarget(int SelectedTarget, int DonorSlot)
	{
		return SelectedTarget >= 0 ? SelectedTarget : DonorSlot;
	}

	class CCanvasGesture
	{
		SCanvasHit m_Origin;
		vec2 m_PressPos{};
		bool m_Dragging = false;
		CUiDoubleClickState m_Clicks;
		SCanvasHit m_LastClick;
		std::vector<int> m_vLastDonorCandidates;

	public:
		void Reset() { *this = {}; }
		SCanvasHit Origin() const { return m_Origin; }
		bool Dragging() const { return m_Dragging; }

		ECanvasAction Update(SCanvasHit Hit, const std::vector<int> &vCandidates, vec2 Pos, bool Pressed, bool Down, bool Blocked, float Time, float Threshold)
		{
			if(Blocked)
			{
				Reset();
				return ECanvasAction::NONE;
			}
			if(Pressed)
			{
				m_Origin = Hit;
				m_PressPos = Pos;
				m_Dragging = false;
				if(Hit.m_Index < 0)
					m_Clicks.Reset();
			}
			if(m_Origin.m_Index < 0)
				return ECanvasAction::NONE;
			if(Down)
			{
				if(!m_Dragging && distance(Pos, m_PressPos) > Threshold)
				{
					m_Dragging = true;
					m_Clicks.Reset();
					return ECanvasAction::BEGIN_DRAG;
				}
				return ECanvasAction::NONE;
			}
			const auto Origin = m_Origin;
			m_Origin = {};
			if(m_Dragging)
			{
				m_Dragging = false;
				return Hit.m_Target && Hit.m_Index >= 0 ? ECanvasAction::DROP : ECanvasAction::NONE;
			}
			if(Hit != Origin || distance(Pos, m_PressPos) > Threshold)
			{
				m_Clicks.Reset();
				return ECanvasAction::NONE;
			}
			// 左侧单击会轮换候选，同一组内的第二击仍属于同一次双击。
			if(Hit.m_Target != m_LastClick.m_Target || (Hit.m_Target ? Hit.m_Index != m_LastClick.m_Index : vCandidates != m_vLastDonorCandidates))
				m_Clicks.Reset();
			m_LastClick = Hit;
			if(!Hit.m_Target)
				m_vLastDonorCandidates = vCandidates;
			if(m_Clicks.Click(this, Time, Pos, Threshold))
				return Hit.m_Target ? ECanvasAction::PICK_COLOR : ECanvasAction::QUICK_REPLACE;
			return Hit.m_Target ? ECanvasAction::SELECT : ECanvasAction::CYCLE;
		}
	};
}

#endif
