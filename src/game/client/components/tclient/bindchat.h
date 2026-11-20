#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_BINDCHAT_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_BINDCHAT_H

#include <base/str.h>

#include <engine/console.h>

#include <game/client/component.h>
#include <game/client/lineinput.h>

#include <algorithm>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <vector>

class IConfigManager;

enum
{
	BINDCHAT_MAX_NAME = 64,
	BINDCHAT_MAX_CMD = 1024,
	BINDCHAT_MAX_BINDS = 256,
};

class CBindChat : public CComponent
{
public:
	class CBind
	{
	public:
		char m_aName[BINDCHAT_MAX_NAME];
		char m_aCommand[BINDCHAT_MAX_CMD];
		CBind() = default;
		CBind(const char *pName, const char *pCommand);
		bool CompContent(const CBind &Other) const { return str_comp(m_aCommand, Other.m_aCommand) == 0; }
	};
	class CBindDefault
	{
	public:
		const char *m_pTitle;
		CBind m_Bind;
		CLineInput m_LineInput;
		char m_aEditName[BINDCHAT_MAX_NAME] = "";
		bool m_NameCommitPending = false;

		// 默认集合初始化后不再移动；活动输入始终借用本条目的缓冲，不借用动态绑定。
		void PrepareNameInput(const char *pCurrentName)
		{
			m_LineInput.SetBuffer(m_aEditName, sizeof(m_aEditName));
			if(!m_LineInput.IsActive() && !m_LineInput.HasPendingChanges() && !m_NameCommitPending && str_comp(m_LineInput.GetString(), pCurrentName) != 0)
				m_LineInput.Set(pCurrentName);
		}
		bool CommitName(std::vector<CBind> &vBinds);
	};
	static std::vector<std::pair<const char *, std::vector<CBindDefault>>> BIND_DEFAULTS;

private:
	static void ConAddBindchat(IConsole::IResult *pResult, void *pUserData);
	static void ConBindchats(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveBindchat(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveBindchatAll(IConsole::IResult *pResult, void *pUserData);
	static void ConBindchatDefaults(IConsole::IResult *pResult, void *pUserData);

	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);

	void ExecuteBind(const CBind &Bind, const char *pArgs);

public:
	std::vector<CBind> m_vBinds; // TODO use map

	CBindChat();
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;

	bool AddBind(const CBind &Bind);

	bool RemoveBind(const char *pName);
	void RemoveAllBinds();

	CBind *GetBind(const char *pCommand);

	bool CheckBindChat(const char *pText);
	bool ChatDoBinds(const char *pText);
};

// 先复制候选以支持调用者借用旧条目；固定数组复制/移动不抛出，替换保留原移到末尾的顺序。
inline bool QmPublishChatBind(std::vector<CBindChat::CBind> &vBinds, const CBindChat::CBind &Bind)
{
	static_assert(std::is_nothrow_copy_constructible_v<CBindChat::CBind> && std::is_nothrow_move_assignable_v<CBindChat::CBind>);
	const CBindChat::CBind Candidate = Bind;
	const auto It = std::find_if(vBinds.begin(), vBinds.end(), [&](const auto &Current) { return str_comp(Current.m_aName, Candidate.m_aName) == 0; });
	if(It != vBinds.end())
	{
		// erase不会缩小capacity，后续追加固定条目无需申请资源。
		vBinds.erase(It);
		vBinds.push_back(Candidate);
		return true;
	}
	try
	{
		vBinds.push_back(Candidate);
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}
	return true;
}

inline bool QmPublishDefaultChatBindName(std::vector<CBindChat::CBind> &vBinds, const CBindChat::CBind &Default, const char *pName)
{
	CBindChat::CBind Candidate = Default;
	str_copy(Candidate.m_aName, pName);
	const auto It = std::find_if(vBinds.begin(), vBinds.end(), [&](const auto &Current) { return str_comp_nocase(Current.m_aCommand, Candidate.m_aCommand) == 0; });
	if(It != vBinds.end())
	{
		if(Candidate.m_aName[0] == '\0')
			vBinds.erase(It);
		else
			str_copy(It->m_aName, Candidate.m_aName);
		return true;
	}
	return Candidate.m_aName[0] == '\0' || QmPublishChatBind(vBinds, Candidate);
}

inline bool CBindChat::CBindDefault::CommitName(std::vector<CBind> &vBinds)
{
	m_NameCommitPending = !QmPublishDefaultChatBindName(vBinds, m_Bind, m_LineInput.GetString());
	return !m_NameCommitPending;
}

#endif
