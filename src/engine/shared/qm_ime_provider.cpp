#include "qm_ime_provider.h"

#include <base/system.h>
#include <engine/shared/config.h>

#include <utility>

namespace
{
	void TraceImeProvider(const char *pEvent, bool Enabled, bool Requested, IImeProvider *pProvider, int Key = 0, int Modifiers = 0, bool Press = false, bool Repeat = false, bool Consumed = false)
	{
		if(!g_Config.m_QmImeTrace)
			return;
		const bool Active = pProvider != nullptr && pProvider->IsActive();
		dbg_msg("ime/trace", "layer=session event=%s enabled=%d requested=%d provider_enabled=%d provider_active=%d active=%d key=%d modifiers=%d press=%d repeat=%d consumed=%d composing=%d candidates=%zu preedit_bytes=%zu",
			pEvent, Enabled, Requested, pProvider != nullptr && pProvider->IsEnabled(), Active, Enabled && Requested && Active,
			Key, Modifiers, Press, Repeat, Consumed, pProvider != nullptr && pProvider->State().m_Composing,
			pProvider != nullptr ? pProvider->State().m_vCandidates.size() : 0, pProvider != nullptr ? pProvider->State().m_Preedit.size() : 0);
	}
}

int QmImeKeyToKeysym(int Key, int Modifiers)
{
	const bool Shift = (Modifiers & QM_IME_KEYMASK_SHIFT) != 0;
	const bool Lock = (Modifiers & QM_IME_KEYMASK_LOCK) != 0;
	if(Key >= KEY_A && Key <= KEY_Z)
		return (Shift != Lock ? 'A' : 'a') + Key - KEY_A;
	if(Key >= KEY_1 && Key <= KEY_9)
		return Shift ? "!@#$%^&*("[Key - KEY_1] : '1' + Key - KEY_1;
	if(Key == KEY_0)
		return Shift ? ')' : '0';
	if(Key >= KEY_KP_1 && Key <= KEY_KP_9)
		return '1' + Key - KEY_KP_1;
	switch(Key)
	{
	case KEY_SPACE: return ' ';
	case KEY_RETURN:
	case KEY_KP_ENTER: return 0xff0d;
	case KEY_BACKSPACE: return 0xff08;
	case KEY_ESCAPE: return 0xff1b;
	case KEY_TAB: return 0xff09;
	case KEY_DELETE: return 0xffff;
	case KEY_HOME: return 0xff50;
	case KEY_LEFT: return 0xff51;
	case KEY_UP: return 0xff52;
	case KEY_RIGHT: return 0xff53;
	case KEY_DOWN: return 0xff54;
	case KEY_PAGEUP: return 0xff55;
	case KEY_PAGEDOWN: return 0xff56;
	case KEY_END: return 0xff57;
	case KEY_MINUS: return Shift ? '_' : '-';
	case KEY_EQUALS: return Shift ? '+' : '=';
	case KEY_LEFTBRACKET: return Shift ? '{' : '[';
	case KEY_RIGHTBRACKET: return Shift ? '}' : ']';
	case KEY_BACKSLASH: return Shift ? '|' : '\\';
	case KEY_SEMICOLON: return Shift ? ':' : ';';
	case KEY_APOSTROPHE: return Shift ? '"' : '\'';
	case KEY_GRAVE: return Shift ? '~' : '`';
	case KEY_COMMA: return Shift ? '<' : ',';
	case KEY_PERIOD: return Shift ? '>' : '.';
	case KEY_SLASH: return Shift ? '?' : '/';
	case KEY_KP_0: return '0';
	case KEY_KP_PERIOD: return '.';
	case KEY_KP_DIVIDE: return '/';
	case KEY_KP_MULTIPLY: return '*';
	case KEY_KP_MINUS: return '-';
	case KEY_KP_PLUS: return '+';
	case KEY_KP_EQUALS: return '=';
	default: return 0;
	}
}

bool QmImeKeyShouldPassThrough(int Key, int Modifiers, bool Composing)
{
	if((Modifiers & (QM_IME_KEYMASK_CONTROL | QM_IME_KEYMASK_ALT | QM_IME_KEYMASK_GUI)) != 0 || Key == KEY_TAB)
		return true;
	if(Key == KEY_LSHIFT || Key == KEY_RSHIFT)
		return false;
	if(QmImeKeyToKeysym(Key, Modifiers) == 0)
		return true;
	if(Composing)
		return false;
	return Key == KEY_RETURN || Key == KEY_KP_ENTER || Key == KEY_ESCAPE ||
	       Key == KEY_BACKSPACE || Key == KEY_DELETE || Key == KEY_LEFT || Key == KEY_RIGHT ||
	       Key == KEY_UP || Key == KEY_DOWN || Key == KEY_HOME || Key == KEY_END ||
	       Key == KEY_PAGEUP || Key == KEY_PAGEDOWN;
}

CQmImeProviderSession::CQmImeProviderSession(FImeCommitCallback Commit) :
	m_Commit(std::move(Commit))
{
}

CQmImeProviderSession::~CQmImeProviderSession()
{
	SetProvider(nullptr);
}

void CQmImeProviderSession::SetProvider(IImeProvider *pProvider)
{
	if(m_pProvider == pProvider)
		return;
	if(m_pProvider != nullptr)
	{
		m_pProvider->Deactivate();
		m_pProvider->SetCommitCallback({});
	}
	m_pProvider = pProvider;
	if(m_pProvider != nullptr)
	{
		m_pProvider->SetCommitCallback([this](const char *pText) {
			if(IsActive() && pText != nullptr && pText[0] != '\0' && m_Commit)
				m_Commit(pText);
		});
		m_pProvider->SetEnabled(m_Enabled);
		if(m_TextInputRequested && m_Enabled)
			m_pProvider->Activate();
	}
}

void CQmImeProviderSession::SetEnabled(bool Enabled)
{
	if(m_Enabled == Enabled)
		return;
	const bool Requested = m_TextInputRequested;
	Stop();
	m_Enabled = Enabled;
	if(m_pProvider != nullptr)
		m_pProvider->SetEnabled(Enabled);
	if(Requested)
		Start();
}

void CQmImeProviderSession::Start()
{
	m_TextInputRequested = true;
	if(m_Enabled && m_pProvider != nullptr && m_pProvider->IsEnabled() && !m_pProvider->IsActive())
		m_pProvider->Activate();
	TraceImeProvider("start", m_Enabled, m_TextInputRequested, m_pProvider);
}

void CQmImeProviderSession::Stop()
{
	m_TextInputRequested = false;
	if(m_pProvider != nullptr && m_pProvider->IsActive())
		m_pProvider->Deactivate();
	TraceImeProvider("stop", m_Enabled, m_TextInputRequested, m_pProvider);
	// 保留已消费按键的配对，切换模式或输入框后仍丢弃它们的松键。
}

void CQmImeProviderSession::OnFrame()
{
	const bool WasActive = IsActive();
	if(m_pProvider != nullptr && m_Enabled && m_TextInputRequested)
		m_pProvider->OnFrame();
	if(WasActive != IsActive())
		TraceImeProvider("frame_active_changed", m_Enabled, m_TextInputRequested, m_pProvider);
}

void CQmImeProviderSession::OnFocusLost()
{
	TraceImeProvider("focus_lost", m_Enabled, m_TextInputRequested, m_pProvider);
	if(m_pProvider != nullptr && IsActive())
		m_pProvider->OnFocusLost();
	m_aConsumedKeys.fill(false);
}

bool CQmImeProviderSession::OnKey(int Key, int Modifiers, bool Press, bool Repeat)
{
	if(Key <= KEY_UNKNOWN || Key >= KEY_LAST)
		return false;
	const bool WasConsumed = m_aConsumedKeys[Key];
	const bool Consumed = IsActive() && m_pProvider->OnKey(Key, Modifiers, Press, Repeat);
	if(m_Enabled || m_TextInputRequested || WasConsumed)
		TraceImeProvider("key_after", m_Enabled, m_TextInputRequested, m_pProvider, Key, Modifiers, Press, Repeat, Consumed || (!Press && WasConsumed) || (Repeat && WasConsumed));
	if(!Press)
	{
		m_aConsumedKeys[Key] = false;
		return WasConsumed;
	}
	if(Consumed || (Repeat && WasConsumed))
	{
		m_aConsumedKeys[Key] = true;
		return true;
	}
	return false;
}

bool CQmImeProviderSession::IsActive() const
{
	return m_Enabled && m_TextInputRequested && m_pProvider != nullptr && m_pProvider->IsActive();
}

const SQmImeProviderState &CQmImeProviderSession::State() const
{
	static const SQmImeProviderState s_Empty;
	return IsActive() ? m_pProvider->State() : s_Empty;
}
