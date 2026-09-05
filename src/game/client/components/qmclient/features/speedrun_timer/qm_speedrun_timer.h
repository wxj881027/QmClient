/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_SPEEDRUN_TIMER_QM_SPEEDRUN_TIMER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_SPEEDRUN_TIMER_QM_SPEEDRUN_TIMER_H

#include "qm_speedrun_timer_logic.h"

#include "../../core/qm_ui_model.h"

class ITextRender;

class CQmSpeedrunTimer final
{
	SQmFeatureModel m_Model{"qm.speedrun_timer", "qm.speedrun_timer.title", false, true};
	CQmSpeedrunTimerLogic m_Logic;

public:
	SQmFeatureModel &Model() { return m_Model; }
	const SQmFeatureModel &Model() const { return m_Model; }

	void UpdateModel(bool Enabled, bool Available)
	{
		m_Model.m_Enabled = Enabled;
		m_Model.m_Available = Available;
	}
	SQmSpeedrunTimerAction Update(const SQmSpeedrunTimerInput &Input) { return m_Logic.Update(Input); }
	void Reset() { m_Logic.Reset(); }
	const SQmSpeedrunTimerState &State() const { return m_Logic.State(); }
	void Render(float HudWidth, ITextRender *pTextRender) const;
};

#endif
