#include "public_word_filter.h"

#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/http.h>
#include <engine/shared/localization.h>
#include <engine/shared/protocol.h>
#include <engine/storage.h>

#include <algorithm>
#include <utility>

namespace
{
	constexpr const char *PUBLIC_WORDS_URL = "https://qmclient.icu/api/v1/word-filter";
	constexpr const char *CACHE_PATH = "qmclient/public_word_filter.txt";

	void SaveCache(IStorage *pStorage, std::string_view Content)
	{
		if(!pStorage)
			return;
		pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE);
		char aTempPath[IO_MAX_PATH_LENGTH];
		str_format(aTempPath, sizeof(aTempPath), "%s.%d.tmp", CACHE_PATH, pid());
		IOHANDLE File = pStorage->OpenFile(aTempPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
		{
			log_warn("public-word-filter", "Cannot open public word cache for writing");
			return;
		}
		const bool Written = io_write(File, Content.data(), static_cast<unsigned>(Content.size())) == Content.size();
		const bool Closed = io_close(File) == 0;
		if(!Written || !Closed || !pStorage->RenameFile(aTempPath, CACHE_PATH, IStorage::TYPE_SAVE, false))
			log_warn("public-word-filter", "Cannot save public word cache; current words remain active");
	}
}

bool QmPublicWordFilter::IsIncomingPlayerChat(int ClientId, bool LocalClient, bool OutgoingWhisper, bool DemoPlayback)
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS && !LocalClient && !OutgoingWhisper && !DemoPlayback;
}

void QmPublicWordFilter::CState::SetEnabled(bool Enabled)
{
	if(m_Enabled == Enabled)
		return;
	m_Enabled = Enabled;
	m_Refreshing = false;
	m_NextRefresh = 0;
	++m_Generation;
}

bool QmPublicWordFilter::CState::Restore(std::string_view Content)
{
	if(!m_List.UpdateFromText(Content))
	{
		m_Error = m_List.Error();
		return false;
	}
	m_HasSnapshot = true;
	m_Error.clear();
	return true;
}

uint64_t QmPublicWordFilter::CState::StartRefresh(int64_t Now)
{
	if(!m_Enabled || m_Refreshing || Now < m_NextRefresh)
		return 0;
	m_Refreshing = true;
	return ++m_Generation;
}

bool QmPublicWordFilter::CState::CompleteRefresh(uint64_t Generation, std::optional<std::string_view> Content, int64_t Now)
{
	if(!m_Enabled || !m_Refreshing || Generation != m_Generation)
		return false;
	m_Refreshing = false;
	const bool ValidResponse = Content.has_value() && Content->substr(0, RESPONSE_HEADER.size()) == RESPONSE_HEADER;
	const bool Success = ValidResponse && Restore(*Content);
	if(!Content.has_value())
		m_Error = Localizable("Cannot update public words. Previous words remain active.");
	else if(!ValidResponse)
		m_Error = Localizable("Invalid public word list response. Previous words remain active.");
	m_NextRefresh = Now + (Success ? REFRESH_SECONDS : RETRY_SECONDS);
	return Success;
}

bool QmPublicWordFilter::CState::Filter(std::string &Text, const char *pReplacement, bool MultiReplace, std::vector<std::string> *pMatched) const
{
	if(!m_Enabled || m_List.Words().empty())
		return false;
	std::vector<std::pair<size_t, size_t>> vMatches;
	for(const std::string &Word : m_List.Words())
	{
		const char *pCursor = Text.c_str();
		const char *pEnd = nullptr;
		bool Matched = false;
		while(const char *pStart = str_utf8_find_nocase(pCursor, Word.c_str(), &pEnd))
		{
			vMatches.emplace_back(pStart - Text.c_str(), pEnd - Text.c_str());
			pCursor = pEnd;
			Matched = true;
		}
		if(Matched && pMatched && std::find(pMatched->begin(), pMatched->end(), Word) == pMatched->end())
			pMatched->push_back(Word);
	}
	if(vMatches.empty())
		return false;
	std::sort(vMatches.begin(), vMatches.end());
	// 先在原文收集区间，合并重叠词，避免替换字符再次参与匹配。
	std::string Result;
	const char *pMask = pReplacement && pReplacement[0] ? pReplacement : "*";
	size_t Cursor = 0;
	for(size_t Index = 0; Index < vMatches.size(); ++Index)
	{
		const size_t Start = vMatches[Index].first;
		size_t End = vMatches[Index].second;
		while(Index + 1 < vMatches.size() && vMatches[Index + 1].first < End)
			End = std::max(End, vMatches[++Index].second);
		Result.append(Text, Cursor, Start - Cursor);
		size_t Count = 1;
		if(MultiReplace)
		{
			Count = 0;
			for(size_t Position = Start; Position < End; ++Position)
				if((static_cast<unsigned char>(Text[Position]) & 0xc0) != 0x80)
					++Count;
		}
		for(size_t Position = 0; Position < Count; ++Position)
			Result += pMask;
		Cursor = End;
	}
	Result.append(Text, Cursor, std::string::npos);
	Text.swap(Result);
	return true;
}

QmPublicWordFilter::CClient::~CClient()
{
	Shutdown();
}

void QmPublicWordFilter::CClient::Initialize(IStorage *pStorage, bool Enabled)
{
	m_State.SetEnabled(Enabled);
	if(!pStorage)
		return;
	IOHANDLE File = pStorage->OpenFile(CACHE_PATH, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return;
	const int64_t Length = io_length(File);
	if(Length < 0 || static_cast<uint64_t>(Length) > QmWordFilterList::MAX_FILE_BYTES)
	{
		io_close(File);
		return;
	}
	std::string Content(static_cast<size_t>(Length), '\0');
	const unsigned Read = io_read(File, Content.data(), static_cast<unsigned>(Content.size()));
	char Extra;
	const bool Complete = Read == Content.size() && io_read(File, &Extra, 1) == 0;
	io_close(File);
	if(Complete)
		m_State.Restore(Content);
}

void QmPublicWordFilter::CClient::Update(IHttp *pHttp, IStorage *pStorage, bool Enabled, int64_t Now)
{
	m_State.SetEnabled(Enabled);
	if(!Enabled)
	{
		Shutdown();
		return;
	}
	if(m_pRequest && m_pRequest->Done())
	{
		std::optional<std::string_view> Content;
		if(m_pRequest->State() == EHttpState::DONE && m_pRequest->StatusCode() == 200)
		{
			unsigned char *pData;
			size_t Size;
			m_pRequest->Result(&pData, &Size);
			Content = std::string_view(pData ? reinterpret_cast<const char *>(pData) : "", Size);
		}
		if(m_State.CompleteRefresh(m_RequestGeneration, Content, Now))
			SaveCache(pStorage, *Content);
		m_pRequest.reset();
	}
	if(!pHttp || m_pRequest)
		return;
	m_RequestGeneration = m_State.StartRefresh(Now);
	if(m_RequestGeneration == 0)
		return;
	m_pRequest = HttpGet(PUBLIC_WORDS_URL);
	m_pRequest->Timeout(CTimeout{4000, 10000, 256, 5});
	m_pRequest->MaxResponseSize(QmWordFilterList::MAX_FILE_BYTES);
	m_pRequest->LogProgress(HTTPLOG::NONE);
	pHttp->Run(m_pRequest);
}

void QmPublicWordFilter::CClient::Shutdown()
{
	if(m_pRequest)
	{
		m_pRequest->Abort();
		m_pRequest.reset();
	}
	m_State.SetEnabled(false);
}
