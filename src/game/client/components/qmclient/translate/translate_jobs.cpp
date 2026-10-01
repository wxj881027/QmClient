#include "translate_jobs.h"

#include <base/system.h>

#include <utility>

bool CTranslateJobQueue::CanSubmit(int Limit) const
{
	return Limit > 0 && m_vJobs.size() < static_cast<size_t>(Limit);
}

bool CTranslateJobQueue::Submit(STranslateJob Job, int Limit)
{
	if(!CanSubmit(Limit) || !Job.m_pBackend || !Job.m_pTranslateResponse)
		return false;
	m_vJobs.emplace_back(std::move(Job));
	return true;
}

std::vector<STranslateCompletion> CTranslateJobQueue::Update(const std::function<bool(const STranslateJob &)> &IncomingStillValid)
{
	std::vector<STranslateCompletion> vCompleted;
	for(auto It = m_vJobs.begin(); It != m_vJobs.end();)
	{
		if(!It->m_Outgoing && !IncomingStillValid(*It))
		{
			It = m_vJobs.erase(It);
			continue;
		}
		const std::optional<bool> Done = It->m_pBackend->Update(*It->m_pTranslateResponse);
		if(!Done.has_value())
		{
			++It;
			continue;
		}
		bool Success = *Done;
		if(It->m_Outgoing && Success && It->m_pTranslateResponse->m_Text[0] == '\0')
		{
			Success = false;
			str_copy(It->m_pTranslateResponse->m_Text, "Empty translation result");
		}
		It->m_pTranslateResponse->m_Error = !Success;
		std::string SendText;
		if(It->m_Outgoing)
		{
			if(Success)
				SendText = It->m_pTranslateResponse->m_Text;
			else if(It->m_AutoTriggered)
				SendText = It->m_OriginalText;
		}
		vCompleted.push_back({std::move(*It), Success, std::move(SendText)});
		It = m_vJobs.erase(It);
	}
	return vCompleted;
}

bool IsTranslateResponseCurrent(bool Initialized, unsigned int TranslationId, const std::shared_ptr<CTranslateResponse> &pResponse, const STranslateJob &Job)
{
	return Initialized && TranslationId == Job.m_TranslationId && pResponse == Job.m_pTranslateResponse;
}

bool IsTranslatePlayerCandidate(int ClientId, bool Initialized, bool HasText, bool HasResponse, const int *pLocalIds, size_t NumLocalIds)
{
	if(!Initialized || !HasText || HasResponse || ClientId < 0)
		return false;
	for(size_t i = 0; i < NumLocalIds; ++i)
		if(pLocalIds[i] >= 0 && ClientId == pLocalIds[i])
			return false;
	return true;
}

int SelectTranslateHistoryLine(int CurrentLine, int NumLines, const std::function<int(int)> &CandidateScore)
{
	if(NumLines <= 0 || CurrentLine < 0 || CurrentLine >= NumLines)
		return -1;
	int BestLine = -1;
	int BestScore = -1;
	int Index = CurrentLine;
	for(int i = 0; i < NumLines; ++i)
	{
		const int Score = CandidateScore(Index);
		if(Score > BestScore)
		{
			BestScore = Score;
			BestLine = Index;
		}
		Index = Index == 0 ? NumLines - 1 : Index - 1;
	}
	return BestLine;
}

int TranslateNameMatchScore(const char *pRequested, const char *pDisplayName, const char *pPlayerName)
{
	if(!pRequested)
		return 0;
	if((pDisplayName && str_comp(pRequested, pDisplayName) == 0) || (pPlayerName && str_comp(pRequested, pPlayerName) == 0))
		return 2;
	if((pDisplayName && str_comp_nocase(pRequested, pDisplayName) == 0) || (pPlayerName && str_comp_nocase(pRequested, pPlayerName) == 0))
		return 1;
	return -1;
}
