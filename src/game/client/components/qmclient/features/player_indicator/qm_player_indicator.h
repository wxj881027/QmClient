/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_PLAYER_INDICATOR_QM_PLAYER_INDICATOR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_PLAYER_INDICATOR_QM_PLAYER_INDICATOR_H

#include <base/vmath.h>

#include <engine/graphics.h>

#include <generated/protocol.h>

#include <game/client/render.h>
#include <game/client/components/qmclient/core/qm_feature_model.h>

#include <array>

class CRenderTools;

struct SQmPlayerIndicatorSettings
{
	bool m_Enabled = false;
	bool m_TeamOnly = true;
	bool m_FrozenOnly = false;
	bool m_HideVisible = false;
	bool m_VariableDistance = false;
	int m_Offset = 42;
	int m_OffsetMax = 100;
	int m_MaxDistance = 1000;
	int m_Radius = 4;
	int m_Opacity = 50;
	int m_AliveColor = 0;
	int m_FrozenColor = 0;
	int m_UnfreezingColor = 0;
	bool m_UseTees = false;
};

struct SQmPlayerIndicatorPlayer
{
	vec2 m_Position = vec2(0.0f, 0.0f);
	int m_Team = 0;
	bool m_Active = false;
	bool m_IsLocal = false;
	bool m_Spectator = false;
	bool m_Frozen = false;
	bool m_Unfreezing = false;
	const CTeeRenderInfo *m_pRenderInfo = nullptr;
	int m_Emote = 0;
};

struct SQmPlayerIndicatorFrame
{
	SQmPlayerIndicatorSettings m_Settings;
	vec2 m_LocalPosition = vec2(0.0f, 0.0f);
	int m_LocalTeam = 0;
	int m_LocalRaceTeam = 0;
	CScreenRect m_Screen = CScreenRect(0.0f, 0.0f, 0.0f, 0.0f);
	std::array<SQmPlayerIndicatorPlayer, MAX_CLIENTS> m_aPlayers;
};

class CQmPlayerIndicator final
{
	SQmFeatureModel m_Model{"qm.player_indicator", "qm.player_indicator.title", false, true};

public:
	SQmFeatureModel &Model() { return m_Model; }
	const SQmFeatureModel &Model() const { return m_Model; }
	void UpdateModel(bool Enabled, bool Available)
	{
		m_Model.m_Enabled = Enabled;
		m_Model.m_Available = Available;
	}
	void Render(const SQmPlayerIndicatorFrame &Frame, IGraphics *pGraphics, CRenderTools *pRenderTools) const;
};

#endif
