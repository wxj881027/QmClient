/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_RUNTIME_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_RUNTIME_H

#include <game/client/component.h>

#include "qm_diagnostics.h"
#include "qm_game_state_adapter.h"
#include "qm_i18n.h"
#include "qm_ui_model.h"
#include "../features/player_indicator/qm_player_indicator.h"

#include <memory>

class IGraphics;

/**
 * QmClient 的组合根。
 *
 * 当前只提供一个稳定的官方组件集成点。具体 feature 在拥有明确的状态
 * owner、生命周期和测试之后再加入，不在这里暴露 CGameClient 或 TClient。
 */
class CQmRuntime final : public CComponent
{
	bool m_Initialized = false;
	unsigned m_MapGeneration = 0;
	int m_LastState = -1;
	std::shared_ptr<CQmDiagnostics> m_pDiagnostics = std::make_shared<CQmDiagnostics>();
	CQmUiModel m_UiModel;
	CQmI18n m_I18n;
	SQmFeatureModel m_DiagnosticsModel{"qm.diagnostics", "qm.diagnostics.title", true, true};
	CQmPlayerIndicator m_PlayerIndicator;

public:
	int Sizeof() const override;
	void OnInterfacesInit(CGameClient *pClient) override;

	void OnInit() override;
	void OnShutdown() override;
	void OnReset() override;
	void OnMapLoad() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnWindowResize() override;
	void OnRender() override;
	void UpdateFeatureModels();

	void OnGraphicsInitBegin(IGraphics *pGraphics);
	void OnGraphicsInitFailed(const char *pDetails) { m_pDiagnostics->RecordGraphicsInitFailed(pDetails); }

	void BeginFrame() { m_pDiagnostics->BeginFrame(); }
	void EndFrame() { m_pDiagnostics->EndFrame(); }
	void BeginGameUpdate() { m_pDiagnostics->BeginGameUpdate(); }
	void EndGameUpdate() { m_pDiagnostics->EndGameUpdate(); }
	void BeginGameRender() { m_pDiagnostics->BeginGameRender(); }
	void EndGameRender() { m_pDiagnostics->EndGameRender(); }

	const CQmUiModel &UiModel() const { return m_UiModel; }
	const CQmI18n &I18n() const { return m_I18n; }

	bool IsInitialized() const { return m_Initialized; }
	unsigned MapGeneration() const { return m_MapGeneration; }
	int LastState() const { return m_LastState; }

private:
	void RegisterGraphicsEventListener(IGraphics *pGraphics);
	IGraphics *m_pGraphicsEventSource = nullptr;
};

#endif
