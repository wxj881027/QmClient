#include "qm_runtime.h"

#include <base/log.h>
#include <base/str.h>

#include <engine/shared/config.h>

#include <game/client/components/qmclient/features/speedrun_timer/qm_speedrun_timer_adapter.h>
#include <game/client/components/qmclient/features/auto_team_lock/qm_auto_team_lock_adapter.h>
#include <game/client/components/qmclient/features/player_indicator/qm_player_indicator_adapter.h>
#include <engine/client.h>

#include <game/localization.h>
#include <game/client/gameclient.h>
#include <game/client/ui/card_preferences_storage.h>
#include <game/client/ui/asset_page_resources.h>
#include <game/client/ui/resource_page_loader.h>
#include <game/client/components/qmclient/presentation/qm_card_settings_adapter.h>
#include <game/client/components/qmclient/presentation/qm_card_settings_view.h>

CQmRuntime::CQmRuntime() = default;
CQmRuntime::~CQmRuntime() = default;

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
	const bool HomePageRegistered = m_CardRegistry.RegisterPage({"home", "qm.ui.home", 0});
	const bool SearchPageRegistered = m_CardRegistry.RegisterPage({"search", "qm.ui.search", 1000});
	const bool DiagnosticsFeatureRegistered = m_CardRegistry.RegisterFeature(m_DiagnosticsModel);
	const bool PlayerIndicatorFeatureRegistered = m_CardRegistry.RegisterFeature(m_PlayerIndicator.Model());
	const bool AutoTeamLockFeatureRegistered = m_CardRegistry.RegisterFeature(m_AutoTeamLock.Model());
	const bool SpeedrunTimerFeatureRegistered = m_CardRegistry.RegisterFeature(m_SpeedrunTimer.Model());
	if(!HomePageRegistered || !SearchPageRegistered || !DiagnosticsFeatureRegistered || !PlayerIndicatorFeatureRegistered || !AutoTeamLockFeatureRegistered || !SpeedrunTimerFeatureRegistered)
		log_error("qm/runtime", "failed to register a feature model");
	const bool DiagnosticsCardRegistered = m_CardRegistry.RegisterCard({"qm.diagnostics", "home", "qm.diagnostics.title", "qm.diagnostics.description", "activity", m_DiagnosticsModel.m_Id, {"diagnostics", "performance"}, ECardOwner::QM, 10, true, "toggle"});
	const bool PlayerIndicatorCardRegistered = m_CardRegistry.RegisterCard({"qm.player_indicator", "home", "qm.player_indicator.title", "qm.player_indicator.description", "compass", m_PlayerIndicator.Model().m_Id, {"player", "teammate", "direction"}, ECardOwner::QM, 20, true});
	const bool AutoTeamLockCardRegistered = m_CardRegistry.RegisterCard({"qm.auto_team_lock", "home", "qm.auto_team_lock.title", "qm.auto_team_lock.description", "lock", m_AutoTeamLock.Model().m_Id, {"team", "lock"}, ECardOwner::QM, 30, true});
	const bool SpeedrunTimerCardRegistered = m_CardRegistry.RegisterCard({"qm.speedrun_timer", "home", "qm.speedrun_timer.title", "qm.speedrun_timer.description", "timer", m_SpeedrunTimer.Model().m_Id, {"speedrun", "timer", "race"}, ECardOwner::QM, 40, true});
	if(!DiagnosticsCardRegistered || !PlayerIndicatorCardRegistered || !AutoTeamLockCardRegistered || !SpeedrunTimerCardRegistered)
		log_error("qm/runtime", "failed to register a UI card");
	if(!RegisterQmSettingsAdapterCards(m_CardRegistry))
		log_error("qm/runtime", "failed to register official settings cards");
	m_CardRegistry.Freeze();
	// 注册表先冻结，UI 状态仅在组件已初始化的生命周期内存在。
	m_pCardUiModel = std::make_unique<CCardUiModel>(m_CardRegistry);
	std::string PreferencesError;
	if(!LoadCardPreferences(*Storage(), *m_pCardUiModel, PreferencesError))
		log_error("qm/ui", "%s", PreferencesError.c_str());
	m_pCardSettingsView = std::make_unique<CQmCardSettingsView>(*m_pCardUiModel, g_Config, Engine(), Storage(), Graphics());
	m_pAssetPageResources = std::make_unique<CAssetPageResources>(Engine(), Storage(), Graphics());
	const auto ResourceReporter = [this](const char *pPageId, const SResourcePageMetrics &Metrics, int Scale) {
		if(g_Config.m_QmDiagnostics == 0)
			return;
		char aDetails[512];
		str_format(aDetails, sizeof(aDetails),
			"page=%s;scale=%d;attempts=%llu;cache_hits=%llu;cancelled=%llu;load_ns=%llu;decoded_bytes=%llu;peak_decoded_bytes=%llu;uploads=%llu;failures=%llu",
			pPageId, Scale,
			static_cast<unsigned long long>(Metrics.m_LoadAttempts),
			static_cast<unsigned long long>(Metrics.m_CacheHits),
			static_cast<unsigned long long>(Metrics.m_CancelledLoads),
			static_cast<unsigned long long>(Metrics.m_LastLoadNanoseconds),
			static_cast<unsigned long long>(Metrics.m_LastDecodedBytes),
			static_cast<unsigned long long>(Metrics.m_PeakDecodedBytes),
			static_cast<unsigned long long>(Metrics.m_UploadedResources),
			static_cast<unsigned long long>(Metrics.m_FailedLoads));
		m_pDiagnostics->RecordEvent("ui.resource_page", aDetails);
	};
	m_pCardSettingsView->SetResourceReporter(ResourceReporter);
	m_pAssetPageResources->SetReporter(ResourceReporter);
	const bool RenderRegistered =
		m_RenderDispatch.Register(EQmRenderSlot::ENTITY_OVERLAY, 0, "qm.player_indicator", 0) == EQmDispatchRegistration::REGISTERED &&
		m_RenderDispatch.Register(EQmRenderSlot::HUD_OVERLAY, 0, "qm.speedrun_timer", 1) == EQmDispatchRegistration::REGISTERED;
	const bool UpdateRegistered =
		m_UpdateDispatch.Register(EQmUpdateSlot::UPDATE, 0, "qm.auto_team_lock", 0) == EQmDispatchRegistration::REGISTERED &&
		m_UpdateDispatch.Register(EQmUpdateSlot::UPDATE, 0, "qm.speedrun_timer", 1) == EQmDispatchRegistration::REGISTERED;
	if(!RenderRegistered || !UpdateRegistered)
		log_error("qm/runtime", "failed to register feature dispatch");
	m_RenderDispatch.Freeze();
	m_UpdateDispatch.Freeze();
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
	if(m_pCardSettingsView)
		m_pCardSettingsView->Shutdown(*Ui());
	if(m_pAssetPageResources)
		m_pAssetPageResources->Shutdown();
	std::string PreferencesError;
	if(m_pCardUiModel && !SaveCardPreferences(*Storage(), *m_pCardUiModel, PreferencesError))
		log_error("qm/ui", "%s", PreferencesError.c_str());
	m_pCardSettingsView.reset();
	m_pAssetPageResources.reset();
	m_pCardUiModel.reset();
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
	if(m_pCardSettingsView)
		m_pCardSettingsView->InvalidateResources();
	if(m_pAssetPageResources)
		m_pAssetPageResources->Invalidate();
	m_AutoTeamLock.Reset();
	m_SpeedrunTimer.Reset();
	log_trace("qm/runtime", "component state reset at map generation %u", m_MapGeneration);
	if(m_Initialized)
		UpdateFeatureModels();
}

void CQmRuntime::OnMapLoad()
{
	if(m_pCardSettingsView)
		m_pCardSettingsView->InvalidateResources();
	if(m_pAssetPageResources)
		m_pAssetPageResources->Invalidate();
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
	if(m_pCardSettingsView)
		m_pCardSettingsView->OnResize();
	if(m_pAssetPageResources)
		m_pAssetPageResources->Invalidate();
	m_pDiagnostics->RecordEvent("window_resize");
	m_pDiagnostics->ResetFramePacing();
	m_pDiagnostics->RecordGraphicsInfo();
}

void CQmRuntime::OnRender()
{
	RenderSlot(EQmRenderSlot::ENTITY_OVERLAY);
}

void CQmRuntime::EndFrame()
{
	if(m_pCardSettingsView)
		m_pCardSettingsView->EndFrame(*Ui());
	if(m_pAssetPageResources)
		m_pAssetPageResources->EndFrame();
	m_pDiagnostics->EndFrame();
}

void CQmRuntime::OnUpdate()
{
	if(!m_Initialized)
		return;
	m_UpdateDispatch.Dispatch(EQmUpdateSlot::UPDATE,
		[this](const auto &Entry) {
			return Entry.m_UserIndex == 0 ?
				g_Config.m_QmAutoTeamLock != 0 && m_AutoTeamLock.Model().m_Enabled :
				g_Config.m_QmSpeedrunTimer != 0 && m_SpeedrunTimer.Model().m_Enabled;
		},
		[this](const auto &Entry) {
			if(Entry.m_UserIndex == 0)
			{
				m_pDiagnostics->BeginFeatureTiming(m_AutoTeamLockTiming, CQmDiagnostics::EFeatureTimingPhase::UPDATE);
				const SQmAutoTeamLockAction Action = m_AutoTeamLock.Update(BuildQmAutoTeamLockInput(*GameClient(), *Client()));
				m_pDiagnostics->EndFeatureTiming(m_AutoTeamLockTiming, CQmDiagnostics::EFeatureTimingPhase::UPDATE);
				ApplyQmAutoTeamLockAction(*GameClient(), Action);
			}
			else
			{
				m_pDiagnostics->BeginFeatureTiming(m_SpeedrunTimerTiming, CQmDiagnostics::EFeatureTimingPhase::UPDATE);
				const SQmSpeedrunTimerAction Action = m_SpeedrunTimer.Update(BuildQmSpeedrunTimerInput(*GameClient(), *Client()));
				m_pDiagnostics->EndFeatureTiming(m_SpeedrunTimerTiming, CQmDiagnostics::EFeatureTimingPhase::UPDATE);
				ApplyQmSpeedrunTimerAction(*GameClient(), Action);
				if(Action.m_Disable)
					m_SpeedrunTimer.UpdateModel(false, m_SpeedrunTimer.Model().m_Available);
			}
		});
}

void CQmRuntime::RenderSlot(const EQmRenderSlot Slot)
{
	if(!m_Initialized)
		return;
	m_RenderDispatch.Dispatch(Slot,
		[this](const auto &Entry) {
			return Entry.m_UserIndex == 0 ?
				g_Config.m_QmPlayerIndicator != 0 && m_PlayerIndicator.Model().m_Enabled :
				g_Config.m_QmSpeedrunTimer != 0 && m_SpeedrunTimer.Model().m_Enabled;
		},
		[this](const auto &Entry) {
			if(Entry.m_UserIndex == 0)
			{
				m_pDiagnostics->BeginFeatureTiming(m_PlayerIndicatorTiming, CQmDiagnostics::EFeatureTimingPhase::RENDER);
				const SQmPlayerIndicatorFrame Frame = BuildQmPlayerIndicatorFrame(*GameClient(), *Client(), *Graphics());
				if(Frame.m_Settings.m_Enabled)
					m_PlayerIndicator.Render(Frame, Graphics(), RenderTools());
				m_pDiagnostics->EndFeatureTiming(m_PlayerIndicatorTiming, CQmDiagnostics::EFeatureTimingPhase::RENDER);
			}
			else
			{
				m_pDiagnostics->BeginFeatureTiming(m_SpeedrunTimerTiming, CQmDiagnostics::EFeatureTimingPhase::RENDER);
				m_SpeedrunTimer.Render(300.0f * Graphics()->ScreenAspect(), TextRender());
				m_pDiagnostics->EndFeatureTiming(m_SpeedrunTimerTiming, CQmDiagnostics::EFeatureTimingPhase::RENDER);
			}
		});
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
