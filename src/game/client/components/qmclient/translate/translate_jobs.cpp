#include "translate_jobs.h"

#include <base/system.h>

#include <algorithm>
#include <cctype>
#include <new>
#include <type_traits>
#include <utility>

std::optional<SOutgoingTranslateText> ParseOutgoingTranslateTarget(const char *pLine)
{
	if(!pLine)
		return std::nullopt;
	const char *pTrimmedLine = str_utf8_skip_whitespaces(pLine);
	if(*pTrimmedLine == '\0' || *pTrimmedLine == '/')
		return std::nullopt;
	const std::string_view Line(pLine);
	const size_t LastNonWhitespace = Line.find_last_not_of(" \t\r\n");
	if(LastNonWhitespace == std::string_view::npos || Line[LastNonWhitespace] != ']')
		return std::nullopt;
	const size_t OpenBracket = Line.rfind('[', LastNonWhitespace);
	if(OpenBracket == std::string_view::npos || OpenBracket + 1 >= LastNonWhitespace)
		return std::nullopt;
	const std::string_view Target = Line.substr(OpenBracket + 1, LastNonWhitespace - OpenBracket - 1);
	if(Target.size() < 2 || Target.size() >= 16)
		return std::nullopt;
	if(std::any_of(Target.begin(), Target.end(), [](char Character) {
		   return std::isalnum(static_cast<unsigned char>(Character)) == 0 && Character != '-' && Character != '_';
	   }))
		return std::nullopt;
	size_t TextEnd = OpenBracket;
	while(TextEnd > 0 && std::isspace(static_cast<unsigned char>(Line[TextEnd - 1])) != 0)
		--TextEnd;
	if(TextEnd == 0)
		return std::nullopt;
	return SOutgoingTranslateText{Line.substr(0, TextEnd), Target};
}

bool CTranslateJobQueue::CanSubmit(int Limit) const
{
	return Limit > 0 && m_vJobs.size() < static_cast<size_t>(Limit);
}

bool CTranslateJobQueue::Submit(STranslateJob Job, int Limit)
{
	if(!CanSubmit(Limit) || !Job.m_pBackend || !Job.m_pTranslateResponse)
		return false;
	static_assert(std::is_nothrow_move_constructible_v<SPending>);
	try
	{
		m_vJobs.push_back({std::move(Job), std::nullopt});
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	return true;
}

void CTranslateJobQueue::CancelOutgoing(int Connection)
{
	m_vJobs.erase(std::remove_if(m_vJobs.begin(), m_vJobs.end(), [Connection](const SPending &Pending) { return Pending.m_Job.m_Outgoing && Pending.m_Job.m_Connection == Connection; }), m_vJobs.end());
}

std::pmr::vector<STranslateCompletion> CTranslateJobQueue::Update(const std::function<bool(const STranslateJob &)> &IncomingStillValid, std::pmr::memory_resource *pResultMemory)
{
	static_assert(std::is_nothrow_move_constructible_v<STranslateCompletion>);
	static_assert(std::is_nothrow_move_assignable_v<SPending>);
	std::pmr::vector<STranslateCompletion> vCompleted(pResultMemory);
	for(auto It = m_vJobs.begin(); It != m_vJobs.end();)
	{
		try
		{
			auto &Job = It->m_Job;
			if(!Job.m_Outgoing && !IncomingStillValid(Job))
			{
				It = m_vJobs.erase(It);
				continue;
			}
			if(!It->m_Completion.has_value())
			{
				const std::optional<bool> Done = Job.m_pBackend->Update(*Job.m_pTranslateResponse);
				if(!Done.has_value())
				{
					++It;
					continue;
				}
				bool Success = *Done;
				if(Job.m_Outgoing && Success && Job.m_pTranslateResponse->m_Text[0] == '\0')
				{
					Success = false;
					str_copy(Job.m_pTranslateResponse->m_Text, "Empty translation result");
				}
				Job.m_pTranslateResponse->m_Error = !Success;
				It->m_Completion = Success;
			}
			const bool Success = *It->m_Completion;
			// 只有出现完成结果才准备容量，等待HTTP的帧不为结果列表分配。
			if(vCompleted.capacity() == 0)
				vCompleted.reserve(m_vJobs.size());
			std::pmr::string SendText(pResultMemory);
			if(Job.m_Outgoing && Success)
				SendText = Job.m_pTranslateResponse->m_Text;
			// 容量和正文均已准备；后续移动与队列擦除不抛异常。
			vCompleted.push_back({std::move(Job), Success, std::move(SendText)});
			It = m_vJobs.erase(It);
		}
		catch(const std::bad_alloc &)
		{
			// 保留当前任务及已有完成状态供下帧重试，先返回此前结果。
			break;
		}
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
