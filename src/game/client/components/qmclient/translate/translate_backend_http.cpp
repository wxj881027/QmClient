#include "translate_backend_http.h"

#include <base/system.h>

#include <engine/shared/json.h>

ITranslateBackendHttp::ITranslateBackendHttp(FTranslateRequestFactory pCreateRequest) : m_pCreateRequest(pCreateRequest) {}

void ITranslateBackendHttp::SetInitError(const char *pError, ETranslateNotice Notice)
{
	str_copy(m_aInitError, pError);
	m_InitNotice = Notice;
}

void ITranslateBackendHttp::PrepareHttpRequest(const char *pUrl)
{
	if(!pUrl || str_length(pUrl) >= 2048)
	{
		SetInitError("Translation request URL exceeds HTTP capacity", ETranslateNotice::INPUT_TOO_LONG);
		return;
	}
	m_pHttpRequest = m_pCreateRequest(pUrl);
	m_pHttpRequest->LogProgress(HTTPLOG::NONE);
	m_pHttpRequest->FailOnErrorStatus(false);
	// 吊销服务器不可达时允许继续握手，但不绕过已知吊销证书的校验。
	m_pHttpRequest->RevocationBestEffort(true);
	m_pHttpRequest->Timeout(CTimeout{10000, 30000, 500, 10});
	m_pHttpRequest->MaxResponseSize(64 * 1024);
}

std::optional<bool> ITranslateBackendHttp::Update(CTranslateResponse &Out)
{
	Out.m_Notice = ETranslateNotice::NONE;
	Out.m_HttpStatus = 0;
	Out.m_Language[0] = '\0';
	if(m_aInitError[0])
	{
		Out.m_Notice = m_InitNotice;
		str_copy(Out.m_Text, m_aInitError);
		return false;
	}
	dbg_assert(m_pHttpRequest != nullptr, "m_pHttpRequest is nullptr");
	const EHttpState State = m_pHttpRequest->State();
	if(State == EHttpState::RUNNING || State == EHttpState::QUEUED)
		return std::nullopt;
	if(State != EHttpState::DONE)
	{
		Out.m_Notice = ETranslateNotice::NETWORK_ERROR;
		str_copy(Out.m_Text, State == EHttpState::ABORTED ? "Aborted" : "HTTP request failed");
		return false;
	}
	const int Status = m_pHttpRequest->StatusCode();
	Out.m_HttpStatus = Status;
	if(Status == 401 || Status == 403)
		Out.m_Notice = ETranslateNotice::AUTHENTICATION;
	else if(Status == 429)
		Out.m_Notice = ETranslateNotice::RATE_LIMIT;
	else if(Status >= 500 && Status <= 599)
		Out.m_Notice = ETranslateNotice::SERVICE_UNAVAILABLE;
	if(Status != 200 && !ParseHttpError())
	{
		str_format(Out.m_Text, sizeof(Out.m_Text), "HTTP %d", Status);
		if(Out.m_Notice == ETranslateNotice::NONE)
			Out.m_Notice = ETranslateNotice::INVALID_RESPONSE;
		return false;
	}
	const bool Parsed = ParseResponse(Out);
	if(!Parsed || Status != 200 || Out.m_Text[0] == '\0' || !str_utf8_check(Out.m_Text))
	{
		if(Out.m_Notice == ETranslateNotice::NONE)
			Out.m_Notice = ETranslateNotice::INVALID_RESPONSE;
		if(Parsed && Status != 200)
			str_format(Out.m_Text, sizeof(Out.m_Text), "HTTP %d", Status);
		return false;
	}
	return true;
}

ITranslateBackendHttp::~ITranslateBackendHttp()
{
	if(m_pHttpRequest)
		m_pHttpRequest->Abort();
}

std::string EncodeTranslateUrl(std::string_view Text)
{
	static constexpr char aHex[] = "0123456789ABCDEF";
	std::string Result;
	Result.reserve(Text.size());
	for(unsigned char Ch : Text)
	{
		if((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') || (Ch >= '0' && Ch <= '9') || Ch == '-' || Ch == '_' || Ch == '.' || Ch == '~')
			Result += Ch;
		else
		{
			Result += '%';
			Result += aHex[Ch >> 4];
			Result += aHex[Ch & 15];
		}
	}
	return Result;
}

bool CopyTranslateText(const json_value *pText, CTranslateResponse &Out)
{
	if(!pText || pText->type != json_string || pText->u.string.length == 0 || pText->u.string.length >= sizeof(Out.m_Text) || str_length(pText->u.string.ptr) != static_cast<int>(pText->u.string.length) || !str_utf8_check(pText->u.string.ptr))
	{
		Out.m_Notice = ETranslateNotice::INVALID_RESPONSE;
		str_copy(Out.m_Text, "Translation is empty or exceeds buffer capacity, or is invalid UTF-8");
		return false;
	}
	str_copy(Out.m_Text, pText->u.string.ptr);
	return true;
}
