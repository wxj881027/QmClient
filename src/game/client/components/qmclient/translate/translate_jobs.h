#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_JOBS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_JOBS_H

#include "translate_backend.h"

#include <functional>
#include <string>
#include <vector>

struct STranslateJob
{
	std::unique_ptr<ITranslateBackend> m_pBackend;
	std::shared_ptr<CTranslateResponse> m_pTranslateResponse = std::make_shared<CTranslateResponse>();
	int m_LineIndex = -1;
	unsigned int m_TranslationId = 0;
	bool m_Outgoing = false;
	bool m_AutoTriggered = false;
	int m_Team = 0;
	char m_aTarget[16] = "";
	std::string m_OriginalText;
};

struct STranslateCompletion
{
	STranslateJob m_Job;
	bool m_Success;
	// 只有成功译文或自动出站失败恢复原文时才产生发送内容。
	std::string m_SendText;
};

// 收发任务共用容量，完成或取消时统一释放后端。
class CTranslateJobQueue
{
	std::vector<STranslateJob> m_vJobs;

public:
	bool CanSubmit(int Limit) const;
	bool Submit(STranslateJob Job, int Limit);
	size_t Size() const { return m_vJobs.size(); }
	void Clear() { m_vJobs.clear(); }
	std::vector<STranslateCompletion> Update(const std::function<bool(const STranslateJob &)> &IncomingStillValid);
};

bool IsTranslateResponseCurrent(bool Initialized, unsigned int TranslationId, const std::shared_ptr<CTranslateResponse> &pResponse, const STranslateJob &Job);

bool IsTranslatePlayerCandidate(int ClientId, bool Initialized, bool HasText, bool HasResponse, const int *pLocalIds, size_t NumLocalIds);
// 无效候选返回负分；相同分数保留最新消息，精确名字匹配优先于大小写兼容匹配。
int SelectTranslateHistoryLine(int CurrentLine, int NumLines, const std::function<int(int)> &CandidateScore);
int TranslateNameMatchScore(const char *pRequested, const char *pDisplayName, const char *pPlayerName);

#endif
