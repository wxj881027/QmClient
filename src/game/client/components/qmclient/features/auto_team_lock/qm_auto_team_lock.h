/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_AUTO_TEAM_LOCK_QM_AUTO_TEAM_LOCK_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_AUTO_TEAM_LOCK_QM_AUTO_TEAM_LOCK_H

#include "qm_auto_team_lock_logic.h"
#include <game/client/components/qmclient/core/qm_feature_model.h>

class CQmAutoTeamLock final
{
	SQmFeatureModel m_Model{"qm.auto_team_lock", "qm.auto_team_lock.title", false, true};
	CQmAutoTeamLockLogic m_Logic;

public:
	SQmFeatureModel &Model() { return m_Model; }
	const SQmFeatureModel &Model() const { return m_Model; }

	void UpdateModel(bool Enabled, bool Available)
	{
		if(!Available && m_Model.m_Available)
			m_Logic.Reset();
		else if(m_Model.m_Enabled && !Enabled)
			m_Logic.Disable();
		m_Model.m_Enabled = Enabled && Available;
		m_Model.m_Available = Available;
	}
	SQmAutoTeamLockAction Update(const SQmAutoTeamLockInput &Input) { return m_Logic.Update(Input); }
	void Reset() { m_Logic.Reset(); }
};

#endif
