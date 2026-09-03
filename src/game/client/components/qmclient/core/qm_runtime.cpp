#include "qm_runtime.h"

#include <base/log.h>
#include <base/str.h>

#include <engine/shared/config.h>

#include <game/localization.h>

int CQmRuntime::Sizeof() const
{
	return sizeof(*this);
}

void CQmRuntime::OnInterfacesInit(CGameClient *pClient)
{
	CComponent::OnInterfacesInit(pClient);
}

void CQmRuntime::OnInit()
{
	m_Initialized = true;
	m_I18n.SetLookup(Localize);
	const bool DiagnosticsFeatureRegistered = m_UiModel.RegisterFeature(m_DiagnosticsModel);
	const bool PlayerIndicatorFeatureRegistered = m_UiModel.RegisterFeature(m_PlayerIndicator.Model());
	if(!DiagnosticsFeatureRegistered || !PlayerIndicatorFeatureRegistered)
		log_error("qm/runtime", "failed to register a feature model");
	const bool DiagnosticsCardRegistered = m_UiModel.RegisterCard({EQmUiPage::HOME, "qm.diagnostics", "activity", &m_DiagnosticsModel});
	const bool PlayerIndicatorCardRegistered = m_UiModel.RegisterCard({EQmUiPage::HOME, "qm.player_indicator", "compass", &m_PlayerIndicator.Model()});
	if(!DiagnosticsCardRegistered || !PlayerIndicatorCardRegistered)
		log_error("qm/runtime", "failed to register a UI card");
	m_UiModel.Freeze();
	m_pDiagnostics->Init(Storage(), Graphics());
	RegisterGraphicsEventListener(Graphics());
	m_pDiagnostics->RecordGraphicsInfo();
	UpdateFeatureModels();
	log_trace("qm/runtime", "composition root initialized");
}

void CQmRuntime::OnGraphicsInitBegin(IGraphics *pGraphics)
{
	// Graphics backend events can happen during Init(), so the listener must be
	// attached before CClient starts initializing the backend.
	m_pDiagnostics->Init(Storage(), pGraphics);
	RegisterGraphicsEventListener(pGraphics);
	m_pDiagnostics->RecordGraphicsInitBegin();
}

void CQmRuntime::RegisterGraphicsEventListener(IGraphics *pGraphics)
{
	if(pGraphics == nullptr || m_pGraphicsEventSource == pGraphics)
		return;

	const std::weak_ptr<CQmDiagnostics> WeakDiagnostics = m_pDiagnostics;
	pGraphics->AddGraphicsEventListener([WeakDiagnostics](const char *pName, const char *pDetails) {
		const std::shared_ptr<CQmDiagnostics> pDiagnostics = WeakDiagnostics.lock();
		if(pDiagnostics)
			pDiagnostics->RecordEventNonBlocking(pName, pDetails);
	});
	m_pGraphicsEventSource = pGraphics;
}

void CQmRuntime::OnShutdown()
{
	m_pDiagnostics->Shutdown();
	log_trace("qm/runtime", "composition root shutdown after %u map generations", m_MapGeneration);
	m_Initialized = false;
}

void CQmRuntime::OnReset()
{
	log_trace("qm/runtime", "component state reset at map generation %u", m_MapGeneration);
}

void CQmRuntime::OnMapLoad()
{
	++m_MapGeneration;
	log_trace("qm/runtime", "map lifecycle entered, generation=%u", m_MapGeneration);
}

void CQmRuntime::OnStateChange(int NewState, int OldState)
{
	m_LastState = NewState;
	char aDetails[64];
	str_format(aDetails, sizeof(aDetails), "%d->%d", OldState, NewState);
	m_pDiagnostics->RecordEvent("client_state_change", aDetails);
	log_trace("qm/runtime", "client state changed: %d -> %d", OldState, NewState);
}

void CQmRuntime::OnWindowResize()
{
	m_pDiagnostics->RecordEvent("window_resize");
	m_pDiagnostics->ResetFramePacing();
	m_pDiagnostics->RecordGraphicsInfo();
}

void CQmRuntime::OnRender()
{
	const bool Available = QmPlayerIndicatorAvailable(*GameClient(), *Client());
	const bool Enabled = g_Config.m_QmPlayerIndicator != 0 && Available;
	m_PlayerIndicator.UpdateModel(Enabled, Available);
	if(!Enabled)
		return;
	const SQmPlayerIndicatorFrame Frame = BuildQmPlayerIndicatorFrame(*GameClient(), *Client(), *Graphics());
	m_PlayerIndicator.Render(Frame, Graphics(), RenderTools());
}

void CQmRuntime::UpdateFeatureModels()
{
	const bool Available = QmPlayerIndicatorAvailable(*GameClient(), *Client());
	m_PlayerIndicator.UpdateModel(g_Config.m_QmPlayerIndicator != 0 && Available, Available);
}
