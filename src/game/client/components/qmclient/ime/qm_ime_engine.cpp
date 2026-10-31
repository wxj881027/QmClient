#include "qm_ime_engine.h"

#include "../bounded_file_reader.h"
#include "qm_ime_merge.h"
#include "qm_ime_runtime.h"

#include <base/system.h>
#include <base/windows.h>

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/version.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

#if defined(CONF_QM_RIME)
#include <windows.h>

#include <rime_api.h>
#endif

namespace
{
	// 全库首次编译在后台进行，期间继续由系统输入法接管；慢磁盘也保留有界回退。
	constexpr int RIME_DEPLOY_TIMEOUT_SECONDS = 60;

	bool QmImeAllCjk(const char *pText, int &CharacterCount)
	{
		CharacterCount = 0;
		if(pText == nullptr || pText[0] == '\0')
			return false;
		const char *pCursor = pText;
		while(*pCursor != '\0')
		{
			const int Codepoint = str_utf8_decode(&pCursor);
			if(Codepoint < 0x3400 || Codepoint > 0x9FFF)
				return false;
			++CharacterCount;
		}
		return CharacterCount > 0;
	}

	void QmImeTrimUtf8Front(std::string &Text, int MaxCharacters)
	{
		int Characters = 0;
		const char *pCursor = Text.c_str();
		while(*pCursor != '\0')
		{
			str_utf8_decode(&pCursor);
			++Characters;
		}
		if(Characters <= MaxCharacters || Text.empty())
			return;
		const char *pFirstEnd = Text.c_str();
		str_utf8_decode(&pFirstEnd);
		Text.erase(0, pFirstEnd - Text.c_str());
		QmImeTrimUtf8Front(Text, MaxCharacters);
	}
}

class CQmImeEngine::CRimeAdapter final : public CQmImeRuntime
{
public:
#if defined(CONF_QM_RIME)
	HMODULE m_Library = nullptr;
	RimeApi *m_pApi = nullptr;
	RimeSessionId m_Session = 0;
	bool m_Initialized = false;
	bool m_Deploying = false;
	int64_t m_DeployStarted = 0;
	std::string m_SharedDir;
	std::string m_UserDir;
	std::string m_BuildDir;
#endif
	IStorage *m_pStorage;
	explicit CRimeAdapter(IStorage *pStorage) :
		m_pStorage(pStorage) {}
	bool Initialized() const override
	{
#if defined(CONF_QM_RIME)
		return m_Initialized;
#else
		return false;
#endif
	}
	void JoinMaintenance() override
	{
#if defined(CONF_QM_RIME)
		m_pApi->join_maintenance_thread();
#endif
	}
	void DestroySession() override
	{
#if defined(CONF_QM_RIME)
		if(m_Session != 0)
		{
			m_pApi->destroy_session(m_Session);
			m_Session = 0;
		}
#endif
	}
	void ClearNotificationHandler() override
	{
#if defined(CONF_QM_RIME)
		m_pApi->set_notification_handler(nullptr, nullptr);
#endif
	}
	void Finalize() override
	{
#if defined(CONF_QM_RIME)
		m_pApi->finalize();
		m_Initialized = false;
#endif
	}
	void UnloadLibrary() override
	{
#if defined(CONF_QM_RIME)
		if(m_Library != nullptr)
		{
			FreeLibrary(m_Library);
			m_Library = nullptr;
		}
		m_pApi = nullptr;
#endif
	}
};

CQmImeEngine::CQmImeEngine(IStorage *pStorage) :
	m_pRime(std::make_unique<CRimeAdapter>(pStorage))
{
}

CQmImeEngine::~CQmImeEngine()
{
	Deactivate();
	if(m_pShutdown != nullptr)
		m_pShutdown->Close(std::move(m_pRime));
	else
		m_pRime->UnloadLibrary();
}

void CQmImeEngine::SetCommitCallback(FImeCommitCallback Callback)
{
	m_Commit = std::move(Callback);
}

void CQmImeEngine::Fail(const char *pReason)
{
	Deactivate();
	m_Failed = true;
	dbg_msg("ime/rime", "Built-in pinyin unavailable: %s; using system IME", pReason);
}

void CQmImeEngine::SetEnabled(bool Enabled)
{
	if(!Enabled)
		Deactivate();
	m_Enabled = Enabled;
}

void CQmImeEngine::Activate()
{
	if(!IsEnabled() || IsActive())
		return;
	m_ActivationRequested = true;
#if defined(CONF_QM_RIME)
	auto &Rime = *m_pRime;
	if(Rime.m_Initialized)
	{
		if(!Rime.m_Deploying)
			FinishDeployment();
		return;
	}
	char aShared[IO_MAX_PATH_LENGTH];
	char aUser[IO_MAX_PATH_LENGTH];
	char aLibrary[IO_MAX_PATH_LENGTH];
	if(Rime.m_pStorage == nullptr || !Rime.m_pStorage->GetDataPath("ime", aShared, sizeof(aShared)) ||
		!Rime.m_pStorage->GetExecutablePath("rime.dll", aLibrary, sizeof(aLibrary)))
	{
		Fail("data or executable path is unavailable");
		return;
	}
	Rime.m_SharedDir = aShared;
	for(const char *pFile : {"default.yaml", "qm_pinyin.schema.yaml", "qm_pinyin.dict.yaml"})
	{
		if(!fs_is_file((Rime.m_SharedDir + "/" + pFile).c_str()))
		{
			Fail("bundled dictionary/schema is missing");
			return;
		}
	}
	if(!Rime.m_pStorage->CreateFolder("ime", IStorage::TYPE_SAVE) ||
		!Rime.m_pStorage->CreateFolder("ime/build", IStorage::TYPE_SAVE))
	{
		Fail("user data directory is not writable");
		return;
	}
	Rime.m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "ime", aUser, sizeof(aUser));
	Rime.m_UserDir = aUser;
	Rime.m_BuildDir = Rime.m_UserDir + "/build";
	// 依赖固定随包携带；限制 DLL 搜索范围，加载失败仍可启动系统输入。
	Rime.m_Library = LoadLibraryExW(windows_utf8_to_wide(aLibrary).c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	if(Rime.m_Library == nullptr)
	{
		Fail("bundled rime.dll or its runtime dependency could not be loaded");
		return;
	}
	using FGetApi = RimeApi *(*)();
	const auto GetApi = reinterpret_cast<FGetApi>(GetProcAddress(Rime.m_Library, "rime_get_api"));
	Rime.m_pApi = GetApi != nullptr ? GetApi() : nullptr;
	RimeApi *pApi = Rime.m_pApi;
	if(pApi == nullptr || !RIME_API_AVAILABLE(pApi, change_page) ||
		!RIME_API_AVAILABLE(pApi, set_input) ||
		!RIME_API_AVAILABLE(pApi, select_candidate) ||
		!RIME_API_AVAILABLE(pApi, candidate_list_begin) ||
		!RIME_API_AVAILABLE(pApi, candidate_list_next) ||
		!RIME_API_AVAILABLE(pApi, candidate_list_end) ||
		!pApi->setup || !pApi->initialize || !pApi->finalize || !pApi->set_notification_handler ||
		!pApi->start_maintenance || !pApi->is_maintenance_mode || !pApi->join_maintenance_thread ||
		!pApi->create_session || !pApi->destroy_session || !pApi->select_schema || !pApi->process_key ||
		!pApi->clear_composition || !pApi->commit_composition || !pApi->get_commit || !pApi->free_commit ||
		!pApi->get_context || !pApi->free_context || !pApi->get_input || !pApi->get_option || !pApi->set_option ||
		!pApi->get_version || str_comp(pApi->get_version(), "1.17.0") != 0)
	{
		Fail("bundled library version/API mismatch (expected 1.17.0)");
		return;
	}
	// 清理线程必须先准备成功，才能启动可能长期运行的 Rime 维护任务。
	m_pShutdown = CQmImeRuntimeShutdown::Prepare();
	if(m_pShutdown == nullptr)
	{
		Rime.UnloadLibrary();
		Fail("Rime cleanup worker is unavailable or a previous runtime is still shutting down");
		return;
	}
	RIME_STRUCT(RimeTraits, Traits);
	Traits.shared_data_dir = Rime.m_SharedDir.c_str();
	Traits.user_data_dir = Rime.m_UserDir.c_str();
	// 编译产物与运行时查找使用同一可写目录，安装目录只读也能部署。
	Traits.prebuilt_data_dir = Rime.m_BuildDir.c_str();
	Traits.staging_dir = Rime.m_BuildDir.c_str();
	Traits.distribution_name = "QmClient";
	Traits.distribution_code_name = "qmclient";
	Traits.distribution_version = QMCLIENT_VERSION;
	Traits.app_name = "rime.qmclient";
	Traits.min_log_level = 1;
	Traits.log_dir = "";
	std::string Text;
	if(QmReadFileBounded(Rime.m_pStorage->OpenFile("ime/qm_english_words.tsv", IOFLAG_READ, IStorage::TYPE_ALL), 4 * 1024 * 1024, Text))
		m_English.Load(Text);
	if(QmReadFileBounded(Rime.m_pStorage->OpenFile("ime/qm_pinyin_chars.txt", IOFLAG_READ, IStorage::TYPE_ALL), 16 * 1024 * 1024, Text))
		m_PinyinSlicer.Load(Text);
	pApi->setup(&Traits);
	pApi->set_notification_handler([](void *pContext, RimeSessionId, const char *pType, const char *pValue) {
		static_cast<CQmImeRuntime *>(pContext)->OnNotification(pType, pValue);
	},
		static_cast<CQmImeRuntime *>(&Rime));
	pApi->initialize(&Traits);
	Rime.m_Initialized = true;
	Rime.m_DeployStarted = time_get();
	Rime.m_Deploying = pApi->start_maintenance(True) != False;
	if(!Rime.m_Deploying)
		FinishDeployment();
#else
	Fail("this build has no Windows MSVC x64 Rime runtime");
#endif
}

void CQmImeEngine::FinishDeployment()
{
#if defined(CONF_QM_RIME)
	auto &Rime = *m_pRime;
	if(Rime.m_Deploying)
	{
		if(Rime.m_pApi->is_maintenance_mode())
			return;
		Rime.m_pApi->join_maintenance_thread();
		Rime.m_Deploying = false;
		dbg_msg("ime/rime", "Deployment completed in %.1f ms", (time_get() - Rime.m_DeployStarted) * 1000.0 / time_freq());
	}
	if(Rime.DeploymentFailed())
	{
		Fail("dictionary deployment failed");
		return;
	}
	if(!m_ActivationRequested || !IsEnabled())
		return;
	if(Rime.m_Session == 0)
		Rime.m_Session = Rime.m_pApi->create_session();
	if(Rime.m_Session == 0 || !Rime.m_pApi->select_schema(Rime.m_Session, "qm_pinyin"))
	{
		Fail("qm_pinyin schema/session could not be opened");
		return;
	}
	Rime.m_pApi->set_option(Rime.m_Session, "ascii_mode", False);
	m_State.m_Active = true;
	RefreshCandidates();
#endif
}

void CQmImeEngine::Deactivate()
{
	OnFocusLost();
	m_ActivationRequested = false;
	m_State = {};
	m_vItems.clear();
	m_vRimeAll.clear();
	m_vOrdered.clear();
	m_vDomain.clear();
	m_LastCode.clear();
	m_PageIndex = 0;
	m_PageCount = 1;
	m_SingleCommitBuffer.clear();
	m_LastSingleCommitTime = 0;
}

void CQmImeEngine::OnFocusLost()
{
	m_ModeShortcut.Reset();
	m_English.Cancel();
	m_NumberSuffix.Reset();
	m_PendingUnit.clear();
	m_CommandAscii = false;
#if defined(CONF_QM_RIME)
	if(m_pRime->m_Session != 0)
		m_pRime->m_pApi->clear_composition(m_pRime->m_Session);
#endif
	const bool Active = m_State.m_Active;
	m_State = {};
	m_State.m_Active = Active;
	m_vItems.clear();
	m_vRimeAll.clear();
	m_vOrdered.clear();
	m_vDomain.clear();
	m_LastCode.clear();
	m_PageIndex = 0;
	m_PageCount = 1;
	m_SingleCommitBuffer.clear();
	m_LastSingleCommitTime = 0;
}

void CQmImeEngine::OnFrame()
{
#if defined(CONF_QM_RIME)
	auto &Rime = *m_pRime;
	if(Rime.m_Deploying && !m_Failed)
	{
		FinishDeployment();
		if(Rime.m_Deploying && time_get() - Rime.m_DeployStarted > RIME_DEPLOY_TIMEOUT_SECONDS * time_freq())
			Fail("dictionary deployment exceeded 60 seconds");
	}
#endif
	if(IsActive() && !g_Config.m_QmImeEnglishCompletion && !m_English.Code().empty())
	{
		EmitCommit(m_English.Finish().c_str());
		RefreshCandidates();
	}
	if(IsActive() && (m_LastDomainMax != g_Config.m_QmImeDomainMax || m_LastDomainWords != g_Config.m_QmImeDomainWords))
		RefreshCandidates();
}

void CQmImeEngine::EmitCommit(const char *pText)
{
	if(pText == nullptr || pText[0] == '\0')
		return;
	if(g_Config.m_QmImeTrace)
		dbg_msg("ime/trace", "layer=engine event=commit bytes=%zu active=%d composing=%d candidates=%zu", std::strlen(pText), IsActive(), m_State.m_Composing, m_State.m_vCandidates.size());
	m_NumberSuffix.OnCommitted(pText);
	if(m_pLexicon != nullptr)
	{
		m_pLexicon->OnCommitted(pText);
		// 连续单字选词（如“才”+“是”）在 2 秒内合并学习为“才是”；
		// 整词提交则不合并，避免把正常句子拼成无意义长词。
		const int64_t Now = time_get();
		if(Now - m_LastSingleCommitTime > 2 * time_freq())
			m_SingleCommitBuffer.clear();
		m_LastSingleCommitTime = Now;
		int Characters = 0;
		if(QmImeAllCjk(pText, Characters) && Characters == 1)
		{
			m_SingleCommitBuffer += pText;
			QmImeTrimUtf8Front(m_SingleCommitBuffer, 8);
			if(m_SingleCommitBuffer.size() > std::strlen(pText))
				m_pLexicon->OnCommitted(m_SingleCommitBuffer.c_str());
		}
		else
		{
			m_SingleCommitBuffer.clear();
		}
	}
	if(m_Commit)
		m_Commit(pText);
	// 本帧后续事件不把刚输入普通内容后的斜杠误判为行首命令。
	if(pText[0] != '/')
		m_CommandContext = false;
}

void CQmImeEngine::DrainCommit()
{
#if defined(CONF_QM_RIME)
	RIME_STRUCT(RimeCommit, Commit);
	if(m_pRime->m_pApi->get_commit(m_pRime->m_Session, &Commit))
	{
		EmitCommit(Commit.text);
		m_pRime->m_pApi->free_commit(&Commit);
	}
#endif
}

void CQmImeEngine::SelectCandidate(int Index)
{
#if defined(CONF_QM_RIME)
	if(Index < 0 || Index >= static_cast<int>(m_vItems.size()))
		return;
	const SQmImeCandidate Item = m_vItems[Index];
	const char *pInput = m_pRime->m_pApi->get_input(m_pRime->m_Session);
	const std::string Code = pInput != nullptr ? pInput : "";
	const size_t Prefix = m_PinyinSlicer.PrefixLength(Item.m_Text.c_str(), Code);
	if(Prefix > 0 && Prefix < Code.size())
	{
		// 先恢复未选拼音，再发布局部提交；回调与下一次输入看到的都是剩余组合。
		m_pRime->m_pApi->clear_composition(m_pRime->m_Session);
		if(m_pRime->m_pApi->set_input(m_pRime->m_Session, Code.substr(Prefix).c_str()))
			EmitCommit(Item.m_Text.c_str());
		else
			m_pRime->m_pApi->set_input(m_pRime->m_Session, Code.c_str());
	}
	else if(Item.m_RimeIndex < 0)
	{
		// 领域词不在 Rime 组合里：直接提交并结束当前组合。
		m_pRime->m_pApi->clear_composition(m_pRime->m_Session);
		EmitCommit(Item.m_Text.c_str());
	}
	else
	{
		// 使用完整候选列表的全局下标，保证自建分页和数字选择一致。
		m_pRime->m_pApi->select_candidate(m_pRime->m_Session, Item.m_RimeIndex);
		DrainCommit();
	}
	m_PageIndex = 0;
	m_State.m_Selected = 0;
	m_LastCode.clear();
	RefreshCandidates();
#endif
}

void CQmImeEngine::MoveSelection(int Delta)
{
	if(m_vItems.empty())
		return;
	const int Current = m_State.m_Selected >= 0 ? m_State.m_Selected : 0;
	m_State.m_Selected = std::clamp(Current + Delta, 0, static_cast<int>(m_vItems.size()) - 1);
}

void CQmImeEngine::ChangePage(int Delta)
{
	if(m_PageCount <= 1)
		return;
	m_PageIndex = std::clamp(m_PageIndex + Delta, 0, m_PageCount - 1);
	m_State.m_Selected = 0;
	ApplyPage();
}

void CQmImeEngine::ApplyPage()
{
#if defined(CONF_QM_RIME)
	const SQmImeCandidatePage Page = QmImeComputeCandidatePage(0, static_cast<int>(m_vOrdered.size()), m_PageIndex, 9);
	m_vItems.clear();
	for(int Index = Page.m_RimeStart; Index < Page.m_RimeStart + Page.m_ShownRime; ++Index)
		m_vItems.push_back(m_vOrdered[Index]);

	m_State.m_vCandidates.clear();
	m_State.m_vComments.clear();
	m_State.m_vCandidates.reserve(m_vItems.size());
	m_State.m_vComments.reserve(m_vItems.size());
	for(const auto &Item : m_vItems)
	{
		m_State.m_vCandidates.push_back(Item.m_Text);
		m_State.m_vComments.push_back(Item.m_Comment);
	}
	m_State.m_PageSize = 9;
	m_State.m_PageStart = Page.m_PageStart;
	m_State.m_TotalCount = static_cast<int>(m_vOrdered.size());
	if(m_vItems.empty())
		m_State.m_Selected = -1;
	else if(m_State.m_Selected < 0 || m_State.m_Selected >= static_cast<int>(m_vItems.size()))
		m_State.m_Selected = 0;
#endif
}

void CQmImeEngine::RefreshEnglish()
{
	m_State = {};
	m_State.m_Active = true;
	m_State.m_Preedit = m_English.Code();
	m_State.m_Cursor = static_cast<int>(m_State.m_Preedit.size());
	m_State.m_Composing = !m_State.m_Preedit.empty();
	m_State.m_vCandidates = m_English.Candidates();
	m_State.m_vComments.resize(m_State.m_vCandidates.size());
	m_State.m_Selected = m_State.m_vCandidates.empty() ? -1 : m_English.Selected();
	m_State.m_PageSize = 9;
	m_State.m_TotalCount = static_cast<int>(m_State.m_vCandidates.size());
	m_vItems.clear();
}

void CQmImeEngine::ToggleLanguage()
{
#if defined(CONF_QM_RIME)
	auto *pApi = m_pRime->m_pApi;
	const auto Session = m_pRime->m_Session;
	const bool WasAscii = pApi->get_option(Session, "ascii_mode") != False;
	if(!m_PendingUnit.empty())
	{
		EmitCommit(m_PendingUnit.c_str());
		m_PendingUnit.clear();
	}
	std::string Text = m_English.Finish();
	if(!WasAscii)
	{
		const char *pInput = pApi->get_input(Session);
		if(pInput != nullptr)
			Text = pInput;
	}
	pApi->clear_composition(Session);
	pApi->set_option(Session, "ascii_mode", !WasAscii);
	m_CommandAscii = false;
	EmitCommit(Text.c_str());
	m_NumberSuffix.Reset();
	m_LastCode.clear();
	RefreshCandidates();
#endif
}

bool CQmImeEngine::OnKey(int Key, int Modifiers, bool Press, bool Repeat)
{
	if(!IsActive())
		return false;
#if defined(CONF_QM_RIME)
	auto *pApi = m_pRime->m_pApi;
	const auto Session = m_pRime->m_Session;
	const bool Ascii = pApi->get_option(Session, "ascii_mode") != False;
	if(g_Config.m_QmImeTrace)
		dbg_msg("ime/trace", "layer=engine event=key_before key=%d modifiers=%d press=%d repeat=%d ascii=%d active=%d composing=%d candidates=%zu preedit_bytes=%zu pending_unit_bytes=%zu command_ascii=%d english_bytes=%zu",
			Key, Modifiers, Press, Repeat, Ascii, IsActive(), m_State.m_Composing, m_State.m_vCandidates.size(), m_State.m_Preedit.size(), m_PendingUnit.size(), m_CommandAscii, m_English.Code().size());
	const auto Shortcut = m_ModeShortcut.OnKey(Key, Modifiers, Press, Repeat, !Ascii && m_State.m_Composing,
		g_Config.m_QmImeShiftSwitch != 0, g_Config.m_QmImeSwitchKey, g_Config.m_QmImeSwitchModifiers);
	if(Shortcut == CQmImeModeShortcut::EResult::TOGGLE)
	{
		ToggleLanguage();
		return true;
	}
	if(Shortcut == CQmImeModeShortcut::EResult::CONSUME)
		return true;
	if(Key == KEY_LSHIFT || Key == KEY_RSHIFT)
		return false;
	if(!Press)
		return false;
	const int Keysym = QmImeKeyToKeysym(Key, Modifiers);
	if(!m_PendingUnit.empty())
	{
		const std::string Unit = std::move(m_PendingUnit);
		m_PendingUnit.clear();
		if(Key == KEY_BACKSPACE || Key == KEY_ESCAPE)
		{
			RefreshCandidates();
			return true;
		}
		if(!(Modifiers & (QM_IME_KEYMASK_CONTROL | QM_IME_KEYMASK_ALT | QM_IME_KEYMASK_GUI)) && CQmImeNumberSuffix::ContinuesPinyin(Unit[0], Keysym))
		{
			// ge / tian / wan 等仍走拼音，避免把“5个”拆成“5g饿”。
			m_NumberSuffix.Reset();
			pApi->process_key(Session, Unit[0], 0);
			DrainCommit();
		}
		else
			EmitCommit(Unit.c_str());
		RefreshCandidates();
	}
	if(Ascii && g_Config.m_QmImeEnglishCompletion && !m_CommandAscii)
	{
		const auto Result = m_English.OnKey(Key, Modifiers);
		EmitCommit(Result.m_Commit.c_str());
		RefreshEnglish();
		return Result.m_Handled;
	}
	const bool WasComposing = m_State.m_Composing;
	if(QmImeKeyShouldPassThrough(Key, Modifiers, WasComposing))
	{
		m_NumberSuffix.Reset();
		return false;
	}
	if(m_State.m_Composing && Key == KEY_ESCAPE)
	{
		pApi->clear_composition(Session);
		m_PageIndex = 0;
		m_LastCode.clear();
		RefreshCandidates();
		return true;
	}
	if(m_State.m_Composing && !(Modifiers & QM_IME_KEYMASK_SHIFT))
	{
		if(Key >= KEY_1 && Key <= KEY_9)
		{
			SelectCandidate(Key - KEY_1);
			return true;
		}
		if(Key == KEY_SPACE && !m_vItems.empty())
		{
			SelectCandidate(m_State.m_Selected >= 0 ? m_State.m_Selected : 0);
			return true;
		}
		if(Key == KEY_RETURN || Key == KEY_KP_ENTER)
		{
			// 回车提交键入的原文；中文候选仍由空格或数字键确认。
			const char *pInput = pApi->get_input(Session);
			const std::string Original = pInput != nullptr ? pInput : "";
			pApi->clear_composition(Session);
			EmitCommit(Original.c_str());
			m_LastCode.clear();
			RefreshCandidates();
			return true;
		}
		if(Key == KEY_LEFT || Key == KEY_UP)
		{
			MoveSelection(-1);
			return true;
		}
		if(Key == KEY_RIGHT || Key == KEY_DOWN)
		{
			MoveSelection(1);
			return true;
		}
		if(Key == KEY_HOME)
		{
			MoveSelection(-static_cast<int>(m_vItems.size()) - 1);
			return true;
		}
		if(Key == KEY_END)
		{
			MoveSelection(static_cast<int>(m_vItems.size()));
			return true;
		}
	}
	const bool PreviousPage = Key == KEY_PAGEUP || (Key == KEY_MINUS && !(Modifiers & QM_IME_KEYMASK_SHIFT));
	const bool NextPage = Key == KEY_PAGEDOWN || (Key == KEY_EQUALS && !(Modifiers & QM_IME_KEYMASK_SHIFT));
	if(m_State.m_Composing && (PreviousPage || NextPage))
	{
		// 自建候选页统一处理，避免 -/= 只改变 Rime 内部页而前端仍显示旧页。
		ChangePage(PreviousPage ? -1 : 1);
		return true;
	}
	if(!Ascii && !m_State.m_Composing && m_NumberSuffix.IsLiteral(Keysym))
	{
		if(Keysym == '.')
			EmitCommit(".");
		else
			m_PendingUnit.assign(1, static_cast<char>(Keysym));
		RefreshCandidates();
		return true;
	}
	if(Key == KEY_SLASH && !(Modifiers & QM_IME_KEYMASK_SHIFT) && !m_State.m_Composing && m_CommandContext)
	{
		m_CommandAscii = true;
		pApi->set_option(Session, "ascii_mode", True);
	}
	const bool Consumed = pApi->process_key(Session, Keysym, Modifiers & (QM_IME_KEYMASK_SHIFT | QM_IME_KEYMASK_LOCK)) != False;
	DrainCommit();
	// Rime 的 ascii_mode 会把未处理字符交还应用，SDL 文本输入此时关闭。
	if(!Consumed && Keysym >= 0x20 && Keysym <= 0x7e)
	{
		if(m_State.m_Composing)
		{
			pApi->commit_composition(Session);
			DrainCommit();
		}
		const char aText[] = {static_cast<char>(Keysym), '\0'};
		EmitCommit(aText);
	}
	RefreshCandidates();
	return Consumed || (Keysym >= 0x20 && Keysym <= 0x7e) || WasComposing;
#else
	return false;
#endif
}

void CQmImeEngine::RefreshCandidates()
{
#if defined(CONF_QM_RIME)
	if(!IsActive())
		return;
	m_LastDomainMax = g_Config.m_QmImeDomainMax;
	m_LastDomainWords = g_Config.m_QmImeDomainWords;
	if(!m_PendingUnit.empty())
	{
		m_State = {};
		m_State.m_Active = true;
		m_State.m_Composing = true;
		m_State.m_Preedit = m_PendingUnit;
		m_State.m_Cursor = static_cast<int>(m_PendingUnit.size());
		m_vItems.clear();
		return;
	}
	auto *pApi = m_pRime->m_pApi;
	const auto Session = m_pRime->m_Session;
	if(pApi->get_option(Session, "ascii_mode") && g_Config.m_QmImeEnglishCompletion && !m_CommandAscii)
	{
		RefreshEnglish();
		return;
	}
	RIME_STRUCT(RimeContext, Context);
	if(!pApi->get_context(Session, &Context))
	{
		Fail("session context became unavailable");
		return;
	}
	m_State = {};
	m_State.m_Active = true;
	m_State.m_Preedit = Context.composition.preedit != nullptr ? Context.composition.preedit : "";
	m_State.m_Cursor = std::clamp(Context.composition.cursor_pos, 0, static_cast<int>(m_State.m_Preedit.size()));

	const char *pInput = pApi->get_input(Session);
	const std::string Code = pInput != nullptr ? pInput : "";
	if(Code != m_LastCode)
	{
		// 新组合从第一页、第一个候选开始；翻页不改变组合串。
		m_LastCode = Code;
		m_PageIndex = 0;
		m_State.m_Selected = 0;
	}

	// 取完整候选列表（不依赖 Rime 的分页），由本地分页决定窗口，避免混入领域词后丢候选。
	m_vRimeAll.clear();
	RimeCandidateListIterator It{};
	if(pApi->candidate_list_begin(Session, &It))
	{
		constexpr size_t MAX_CANDIDATES = 512;
		do
		{
			if(It.candidate.text != nullptr && It.candidate.text[0] != '\0')
				m_vRimeAll.push_back({It.candidate.text, It.candidate.comment != nullptr ? It.candidate.comment : "", It.index});
			if(m_vRimeAll.size() >= MAX_CANDIDATES)
				break;
		} while(pApi->candidate_list_next(&It));
		pApi->candidate_list_end(&It);
	}

	m_vDomain.clear();
	if(!Code.empty() && m_LastDomainWords && m_pLexicon != nullptr)
	{
		std::array<SQmImeLexiconEntry, 5> aDomain;
		const size_t Matched = m_pLexicon->Match(Code.c_str(), aDomain.data(), aDomain.size());
		const size_t Limit = std::min<size_t>(Matched, static_cast<size_t>(std::clamp(m_LastDomainMax, 0, 5)));
		for(size_t Index = 0; Index < Limit; ++Index)
			m_vDomain.push_back(aDomain[Index]);
	}
	m_vOrdered = QmImeBuildCandidateList(m_vRimeAll, m_vDomain);

	// 排序、去重后再分页，数字下标始终指向实际显示的候选。
	const SQmImeCandidatePage Page = QmImeComputeCandidatePage(0, static_cast<int>(m_vOrdered.size()), m_PageIndex, 9);
	m_PageCount = Page.m_PageCount;
	m_PageIndex = std::clamp(m_PageIndex, 0, m_PageCount - 1);
	if(m_State.m_Selected < 0)
		m_State.m_Selected = 0;
	ApplyPage();
	m_State.m_Composing = !m_State.m_Preedit.empty() || !Code.empty() || !m_vItems.empty();
	pApi->free_context(&Context);
#endif
}
