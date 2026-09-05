#include "qm_runtime.h"

#include <base/log.h>
#include <base/str.h>

#include <engine/shared/config.h>

#include <game/client/components/qmclient/features/speedrun_timer/qm_speedrun_timer_adapter.h>
#include <game/client/components/qmclient/features/auto_team_lock/qm_auto_team_lock_adapter.h>
#include <engine/client.h>

#include <game/localization.h>
#include <game/client/gameclient.h>

int CQmRuntime::Sizeof() const
{
	return sizeof(*this);
}

void CQmRuntime::OnInterfacesInit(CGameClient *pClient)
{
	CComponent::OnInterfacesInit(pClient);
}

void CQmRuntime::OnConsoleInit()
{
	m_ConfigMigration.OnConsoleInit(Console());
}

void CQmRuntime::OnInit()
{
	m_Initialized = true;
	m_I18n.SetLookup(Localize);
	m_DiagnosticsModel.m_Enabled = g_Config.m_QmDiagnostics != 0;
	const bool DiagnosticsFeatureRegistered = m_UiModel.RegisterFeature(m_DiagnosticsModel);
	const bool PlayerIndicatorFeatureRegistered = m_UiModel.RegisterFeature(m_PlayerIndicator.Model());
	const bool AutoTeamLockFeatureRegistered = m_UiModel.RegisterFeature(m_AutoTeamLock.Model());
	const bool SpeedrunTimerFeatureRegistered = m_UiModel.RegisterFeature(m_SpeedrunTimer.Model());
	if(!DiagnosticsFeatureRegistered || !PlayerIndicatorFeatureRegistered || !AutoTeamLockFeatureRegistered || !SpeedrunTimerFeatureRegistered)
		log_error("qm/runtime", "failed to register a feature model");
	const bool DiagnosticsCardRegistered = m_UiModel.RegisterCard({EQmUiPage::HOME, "qm.diagnostics", "activity", m_DiagnosticsModel});
	const bool PlayerIndicatorCardRegistered = m_UiModel.RegisterCard({EQmUiPage::HOME, "qm.player_indicator", "compass", m_PlayerIndicator.Model()});
	const bool AutoTeamLockCardRegistered = m_UiModel.RegisterCard({EQmUiPage::HOME, "qm.auto_team_lock", "lock", m_AutoTeamLock.Model()});
	const bool SpeedrunTimerCardRegistered = m_UiModel.RegisterCard({EQmUiPage::HOME, "qm.speedrun_timer", "timer", m_SpeedrunTimer.Model()});
	if(!DiagnosticsCardRegistered || !PlayerIndicatorCardRegistered || !AutoTeamLockCardRegistered || !SpeedrunTimerCardRegistered)
		log_error("qm/runtime", "failed to register a UI card");
	m_UiModel.Freeze();
	if(g_Config.m_QmDiagnostics != 0)
		m_pDiagnostics->Init(Storage(), Graphics());
	if(g_Config.m_QmDiagnostics != 0)
	{
		m_PlayerIndicatorTiming = m_pDiagnostics->RegisterFeatureTiming("qm.player_indicator");
		if(m_PlayerIndicatorTiming == CQmDiagnostics::INVALID_FEATURE_TIMING)
			log_error("qm/runtime", "failed to register player indicator diagnostics timing");
	}
	if(g_Config.m_QmDiagnostics != 0)
	{
		m_AutoTeamLockTiming = m_pDiagnostics->RegisterFeatureTiming("qm.auto_team_lock");
		if(m_AutoTeamLockTiming == CQmDiagnostics::INVALID_FEATURE_TIMING)
			log_error("qm/runtime", "failed to register auto team lock diagnostics timing");
	}
	if(g_Config.m_QmDiagnostics != 0)
	{
		m_SpeedrunTimerTiming = m_pDiagnostics->RegisterFeatureTiming("qm.speedrun_timer");
		if(m_SpeedrunTimerTiming == CQmDiagnostics::INVALID_FEATURE_TIMING)
			log_error("qm/runtime", "failed to register speedrun timer diagnostics timing");
	}
	if(g_Config.m_QmDiagnostics != 0)
	{
		RegisterGraphicsEventListener(Graphics());
		m_pDiagnostics->RecordGraphicsInfo();
	}
	UpdateFeatureModels();
	log_trace("qm/runtime", "composition root initialized");
}

void CQmRuntime::OnGraphicsInitBegin(IGraphics *pGraphics)
{
	// Graphics backend events can happen during Init(), so the listener must be
	// attached before CClient starts initializing the backend.
	if(g_Config.m_QmDiagnostics == 0)
		return;
	m_pDiagnostics->Init(Storage(), pGraphics);
	RegisterGraphicsEventListener(pGraphics);
	m_pDiagnostics->RecordGraphicsInitBegin();
}

bool CQmRuntime::OnConfigUnknownCommand(const char *pCommand, IConfigManager *pConfigManager)
{
	return m_ConfigMigration.OnUnknownCommand(pCommand, pConfigManager);
}

void CQmRuntime::OnConfigLoaded(IConfigManager *pConfigManager)
{
	m_ConfigMigration.OnConfigLoaded(Console(), pConfigManager);
	m_DiagnosticsModel.m_Enabled = g_Config.m_QmDiagnostics != 0;
	if(m_Initialized)
		UpdateFeatureModels();
}

void CQmRuntime::RegisterGraphicsEventListener(IGraphics *pGraphics)
{
	if(pGraphics == nullptr)
		return;
	const uint32_t SessionGeneration = m_pDiagnostics->SessionGeneration();
	if(m_pGraphicsEventSource == pGraphics && m_pGraphicsEventListenerState)
	{
		m_pGraphicsEventListenerState->m_SessionGeneration.store(SessionGeneration, std::memory_order_release);
		m_pGraphicsEventListenerState->m_Active.store(true, std::memory_order_release);
		return;
	}
	if(m_pGraphicsEventListenerState)
		m_pGraphicsEventListenerState->m_Active.store(false, std::memory_order_release);

	const std::weak_ptr<CQmDiagnostics> WeakDiagnostics = m_pDiagnostics;
	const std::shared_ptr<SGraphicsEventListenerState> pListenerState = std::make_shared<SGraphicsEventListenerState>();
	pListenerState->m_SessionGeneration.store(SessionGeneration, std::memory_order_release);
	pListenerState->m_Active.store(true, std::memory_order_release);
	pGraphics->AddGraphicsEventListener([WeakDiagnostics, pListenerState](const char *pName, const char *pDetails) {
		if(!pListenerState->m_Active.load(std::memory_order_acquire))
			return;
		const std::shared_ptr<CQmDiagnostics> pDiagnostics = WeakDiagnostics.lock();
		if(pDiagnostics)
			pDiagnostics->RecordEventNonBlocking(pListenerState->m_SessionGeneration.load(std::memory_order_acquire), pName, pDetails);
	});
	m_pGraphicsEventSource = pGraphics;
	m_pGraphicsEventListenerState = pListenerState;
}

void CQmRuntime::OnShutdown()
{
	if(m_pGraphicsEventListenerState)
		m_pGraphicsEventListenerState->m_Active.store(false, std::memory_order_release);
	m_pGraphicsEventListenerState.reset();
	m_pGraphicsEventSource = nullptr;
	m_pDiagnostics->Shutdown();
	log_trace("qm/runtime", "composition root shutdown after %u map generations", m_MapGeneration);
	m_Initialized = false;
}

void CQmRuntime::OnReset()
{
	m_AutoTeamLock.Reset();
	m_SpeedrunTimer.Reset();
	log_trace("qm/runtime", "component state reset at map generation %u", m_MapGeneration);
	if(m_Initialized)
		UpdateFeatureModels();
}

void CQmRuntime::OnMapLoad()
{
	m_AutoTeamLock.Reset();
	m_SpeedrunTimer.Reset();
	++m_MapGeneration;
	log_trace("qm/runtime", "map lifecycle entered, generation=%u", m_MapGeneration);
}

void CQmRuntime::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE)
	{
		m_AutoTeamLock.Reset();
		m_SpeedrunTimer.Reset();
	}
	m_LastState = NewState;
	char aDetails[64];
	str_format(aDetails, sizeof(aDetails), "%d->%d", OldState, NewState);
	m_pDiagnostics->RecordEvent("client_state_change", aDetails);
	log_trace("qm/runtime", "client state changed: %d -> %d", OldState, NewState);
	if(m_Initialized)
		UpdateFeatureModels();
}

void CQmRuntime::OnWindowResize()
{
	m_pDiagnostics->RecordEvent("window_resize");
	m_pDiagnostics->ResetFramePacing();
	m_pDiagnostics->RecordGraphicsInfo();
}

void CQmRuntime::OnRender()
{
	RenderSlot(EQmRenderSlot::ENTITY_OVERLAY);
}

void CQmRuntime::OnUpdate()
{
	m_pDiagnostics->BeginFeatureTiming(m_AutoTeamLockTiming, CQmDiagnostics::EFeatureTimingPhase::UPDATE);
	const SQmAutoTeamLockAction AutoTeamLockAction = m_AutoTeamLock.Update(BuildQmAutoTeamLockInput(*GameClient(), *Client()));
	m_pDiagnostics->EndFeatureTiming(m_AutoTeamLockTiming, CQmDiagnostics::EFeatureTimingPhase::UPDATE);
	if(AutoTeamLockAction.m_SendLockCommand)
		GameClient()->m_Chat.SendChat(0, "/lock 1");

	m_pDiagnostics->BeginFeatureTiming(m_SpeedrunTimerTiming, CQmDiagnostics::EFeatureTimingPhase::UPDATE);
	const SQmSpeedrunTimerAction Action = m_SpeedrunTimer.Update(BuildQmSpeedrunTimerInput(*GameClient(), *Client()));
	m_pDiagnostics->EndFeatureTiming(m_SpeedrunTimerTiming, CQmDiagnostics::EFeatureTimingPhase::UPDATE);
	if(Action.m_RequestKill)
		GameClient()->SendKill();
	if(Action.m_Disable)
		g_Config.m_QmSpeedrunTimer = 0;
}

void CQmRuntime::RenderSlot(const EQmRenderSlot Slot)
{
	if(Slot == EQmRenderSlot::ENTITY_OVERLAY)
	{
		m_pDiagnostics->BeginFeatureTiming(m_PlayerIndicatorTiming, CQmDiagnostics::EFeatureTimingPhase::RENDER);
		if(!m_PlayerIndicator.Model().m_Enabled)
		{
			m_pDiagnostics->EndFeatureTiming(m_PlayerIndicatorTiming, CQmDiagnostics::EFeatureTimingPhase::RENDER);
			return;
		}
		const SQmPlayerIndicatorFrame Frame = BuildQmPlayerIndicatorFrame(*GameClient(), *Client(), *Graphics());
		if(!Frame.m_Settings.m_Enabled)
		{
			m_pDiagnostics->EndFeatureTiming(m_PlayerIndicatorTiming, CQmDiagnostics::EFeatureTimingPhase::RENDER);
			return;
		}
		m_PlayerIndicator.Render(Frame, Graphics(), RenderTools());
		m_pDiagnostics->EndFeatureTiming(m_PlayerIndicatorTiming, CQmDiagnostics::EFeatureTimingPhase::RENDER);
		return;
	}
	if(Slot == EQmRenderSlot::HUD_OVERLAY)
	{
		m_pDiagnostics->BeginFeatureTiming(m_SpeedrunTimerTiming, CQmDiagnostics::EFeatureTimingPhase::RENDER);
		m_SpeedrunTimer.Render(300.0f * Graphics()->ScreenAspect(), TextRender());
		m_pDiagnostics->EndFeatureTiming(m_SpeedrunTimerTiming, CQmDiagnostics::EFeatureTimingPhase::RENDER);
		return;
	}
}

void CQmRuntime::UpdateFeatureModels()
{
	m_pDiagnostics->BeginFeatureTiming(m_PlayerIndicatorTiming, CQmDiagnostics::EFeatureTimingPhase::UPDATE);
	const bool Available = QmPlayerIndicatorAvailable(*GameClient(), *Client());
	m_PlayerIndicator.UpdateModel(g_Config.m_QmPlayerIndicator != 0 && Available, Available);
	m_pDiagnostics->EndFeatureTiming(m_PlayerIndicatorTiming, CQmDiagnostics::EFeatureTimingPhase::UPDATE);
	m_AutoTeamLock.UpdateModel(g_Config.m_QmAutoTeamLock != 0, Client()->State() == IClient::STATE_ONLINE);
	m_SpeedrunTimer.UpdateModel(g_Config.m_QmSpeedrunTimer != 0, Client()->State() == IClient::STATE_ONLINE);
}
