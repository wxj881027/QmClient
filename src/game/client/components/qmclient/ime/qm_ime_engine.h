#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_IME_QM_IME_ENGINE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_IME_QM_IME_ENGINE_H

#include "qm_ime_merge.h"
#include "qm_ime_typing.h"

#include <engine/shared/qm_ime_provider.h>

#include <memory>

class IStorage;
class CQmImeRuntimeShutdown;

class CQmImeEngine final : public IImeProvider
{
public:
	explicit CQmImeEngine(IStorage *pStorage);
	~CQmImeEngine() override;
	void SetCommitCallback(FImeCommitCallback Callback) override;
	void Activate() override;
	void Deactivate() override;
	bool IsActive() const override { return m_State.m_Active; }
	bool OnKey(int Key, int Modifiers, bool Press, bool Repeat) override;
	void OnFocusLost() override;
	void OnFrame() override;
	const SQmImeProviderState &State() const override { return m_State; }
	void SetEnabled(bool Enabled) override;
	bool IsEnabled() const override { return m_Enabled && !m_Failed; }
	void SetDomainLexicon(IImeDomainLexicon *pLexicon) { m_pLexicon = pLexicon; }
	void SetCommandContext(bool CommandContext) { m_CommandContext = CommandContext; }
	void RefreshCandidates();

private:
	class CRimeAdapter;

	std::unique_ptr<CRimeAdapter> m_pRime;
	std::unique_ptr<CQmImeRuntimeShutdown> m_pShutdown;
	IImeDomainLexicon *m_pLexicon = nullptr; // 由 manager 持有，先于 engine 注销。
	FImeCommitCallback m_Commit;
	SQmImeProviderState m_State;
	std::vector<SQmImeCandidate> m_vItems;
	std::vector<SQmImeCandidate> m_vRimeAll;
	std::vector<SQmImeCandidate> m_vOrdered;
	std::vector<SQmImeLexiconEntry> m_vDomain;
	std::string m_LastCode;
	std::string m_SingleCommitBuffer;
	int64_t m_LastSingleCommitTime = 0;
	int m_PageIndex = 0;
	int m_PageCount = 1;
	bool m_Enabled = false;
	bool m_Failed = false;
	bool m_ActivationRequested = false;
	bool m_CommandContext = false;
	CQmImeEnglishInput m_English;
	CQmImeModeShortcut m_ModeShortcut;
	CQmImeNumberSuffix m_NumberSuffix;
	CQmImePinyinSlicer m_PinyinSlicer;
	std::string m_PendingUnit;
	bool m_CommandAscii = false;
	int m_LastDomainMax = -1;
	int m_LastDomainWords = -1;
	void Fail(const char *pReason);
	void FinishDeployment();
	void EmitCommit(const char *pText);
	void DrainCommit();
	void SelectCandidate(int Index);
	void ChangePage(int Delta);
	void MoveSelection(int Delta);
	void ApplyPage();
	void ToggleLanguage();
	void RefreshEnglish();
};

#endif
