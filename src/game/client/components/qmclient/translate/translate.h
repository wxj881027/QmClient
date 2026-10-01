// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_H

#include "translate_backend.h"
#include "translate_jobs.h"

#include <game/client/component.h>
#include <game/client/components/chat.h>

class CTranslate : public CComponent
{
	CTranslateJobQueue m_Jobs;

	static void ConTranslate(IConsole::IResult *pResult, void *pUserData);
	static void ConTranslateId(IConsole::IResult *pResult, void *pUserData);

public:
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnRender() override;
	void OnReset() override;
	void OnShutdown() override;

	void Translate(int Id, bool ShowProgress = true);
	void Translate(const char *pName, bool ShowProgress = true);
	void Translate(CChat::CLine &Line, bool ShowProgress = true, bool AutoTriggered = false);
	bool TryTranslateOutgoingChat(int Team, const char *pText);

	void AutoTranslate(CChat::CLine &Line);

	// 自动出站翻译
	bool ShouldAutoTranslateOutgoing(const char *pText) const;
	void StartAutoOutgoingTranslate(int Team, const char *pText);

private:
	// 获取最大并发数
	int GetMaxConcurrency() const;

	// MyMemory 匿名配额提示的节流时间戳（time_get），<0 表示未提示过
	int64_t m_LastMymemoryQuotaNoticeTime = -1;
};

#endif
