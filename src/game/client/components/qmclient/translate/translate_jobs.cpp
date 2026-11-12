#include "translate_jobs.h"

#include <base/system.h>

#include <algorithm>
#include <cctype>
#include <utility>

namespace
{
	bool IsOutgoingTranslateTargetChar(char Character)
	{
		const unsigned char Value = static_cast<unsigned char>(Character);
		return std::isalnum(Value) != 0 || Character == '-' || Character == '_';
	}

	bool ParseOutgoingTranslateTarget(const char *pLine, std::string &Text, std::string &Target)
	{
		if(!pLine)
			return false;

		const char *pTrimmedLine = str_utf8_skip_whitespaces(pLine);
		if(*pTrimmedLine == '\0' || *pTrimmedLine == '/')
			return false;

		std::string Line = pLine;
		const size_t LastNonWhitespace = Line.find_last_not_of(" \t\r\n");
		if(LastNonWhitespace == std::string::npos || Line[LastNonWhitespace] != ']')
			return false;

		const size_t OpenBracket = Line.rfind('[', LastNonWhitespace);
		if(OpenBracket == std::string::npos || OpenBracket + 1 >= LastNonWhitespace)
			return false;

		Target = Line.substr(OpenBracket + 1, LastNonWhitespace - OpenBracket - 1);
		if(Target.size() < 2 || Target.size() >= 16)
			return false;
		if(std::any_of(Target.begin(), Target.end(), [](char Character) { return !IsOutgoingTranslateTargetChar(Character); }))
			return false;

		size_t TextEnd = OpenBracket;
		while(TextEnd > 0 && std::isspace(static_cast<unsigned char>(Line[TextEnd - 1])) != 0)
			--TextEnd;
		if(TextEnd == 0)
			return false;

		Text = Line.substr(0, TextEnd);
		return true;
	}
}

bool PrepareOutgoingTranslateRequest(const char *pLine, const char *pFallbackTarget, std::string &Text, std::string &Target)
{
	if(ParseOutgoingTranslateTarget(pLine, Text, Target))
		return true;
	if(!pLine || !pFallbackTarget || pFallbackTarget[0] == '\0')
		return false;
	const char *pTrimmed = str_utf8_skip_whitespaces(pLine);
	if(*pTrimmed == '\0' || *pTrimmed == '/')
		return false;
	Text = pLine;
	Target = pFallbackTarget;
	return true;
}

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

void CTranslateJobQueue::CancelOutgoing(int Connection)
{
	m_vJobs.erase(std::remove_if(m_vJobs.begin(), m_vJobs.end(), [Connection](const STranslateJob &Job) { return Job.m_Outgoing && Job.m_Connection == Connection; }), m_vJobs.end());
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
		}
		vCompleted.push_back({std::move(*It), Success, std::move(SendText)});
		It = m_vJobs.erase(It);
	}
	return vCompleted;
}

bool DispatchOutgoingTranslation(const STranslateCompletion &Completed, const std::function<bool(int, const char *, const char *, int)> &SendTranslated)
{
	if(!Completed.m_Job.m_Outgoing || !Completed.m_Success || Completed.m_SendText.empty())
		return false;
	const auto &Job = Completed.m_Job;
	// 使用聊天协议常用的 256 字节正文边界，发送与本地附注匹配共用同一段完整 UTF-8 文本。
	char aText[256];
	str_utf8_copy_num(aText, Completed.m_SendText.c_str(), sizeof(aText), 256);
	str_utf8_trim_right(aText);
	if(aText[0] == '\0')
		return false;
	return SendTranslated(Job.m_Team, aText, Job.m_DisplayOriginal ? Job.m_OriginalText.c_str() : nullptr, Job.m_Connection);
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
