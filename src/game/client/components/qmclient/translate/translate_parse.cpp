// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "translate_parse.h"

#include <base/system.h>

#include <engine/shared/json.h>

bool ParseLlmResponseJson(const json_value *pObj, SLlmParseResult &Out)
{
	if(!pObj)
	{
		str_copy(Out.m_aError, "Response is not valid JSON", sizeof(Out.m_aError));
		Out.m_Success = false;
		return false;
	}

	if(pObj->type != json_object)
	{
		str_copy(Out.m_aError, "Response is not a JSON object", sizeof(Out.m_aError));
		Out.m_Success = false;
		return false;
	}

	const json_value *pError = json_object_get(pObj, "error");
	if(pError != &json_value_none)
	{
		const json_value *pMessage = json_object_get(pError, "message");
		const char *pMessageStr = pMessage != &json_value_none && pMessage->type == json_string ? pMessage->u.string.ptr : "LLM API request failed";
		str_copy(Out.m_aError, pMessageStr, sizeof(Out.m_aError));
		Out.m_Success = false;
		return false;
	}

	const json_value *pChoices = json_object_get(pObj, "choices");
	if(pChoices == &json_value_none)
	{
		char aErrorMsg[512];
		str_copy(aErrorMsg, "No choices in response", sizeof(aErrorMsg));

		const json_value *pCode = json_object_get(pObj, "code");
		const json_value *pMsg = json_object_get(pObj, "msg");
		if(pCode != &json_value_none && pCode->type == json_string)
		{
			str_format(aErrorMsg + str_length(aErrorMsg), sizeof(aErrorMsg) - str_length(aErrorMsg),
				" (code: %s", pCode->u.string.ptr);
			if(pMsg != &json_value_none && pMsg->type == json_string)
				str_format(aErrorMsg + str_length(aErrorMsg), sizeof(aErrorMsg) - str_length(aErrorMsg),
					", %s)", pMsg->u.string.ptr);
			else
				str_append(aErrorMsg, ")", sizeof(aErrorMsg));
		}

		str_copy(Out.m_aError, aErrorMsg, sizeof(Out.m_aError));
		Out.m_Success = false;
		return false;
	}
	if(pChoices->type != json_array)
	{
		str_copy(Out.m_aError, "choices is not array", sizeof(Out.m_aError));
		Out.m_Success = false;
		return false;
	}
	if(pChoices->u.array.length == 0)
	{
		str_copy(Out.m_aError, "choices is empty", sizeof(Out.m_aError));
		Out.m_Success = false;
		return false;
	}

	const json_value *pChoice = pChoices->u.array.values[0];
	if(pChoice->type != json_object)
	{
		str_copy(Out.m_aError, "choice is not object", sizeof(Out.m_aError));
		Out.m_Success = false;
		return false;
	}

	const json_value *pMessage = json_object_get(pChoice, "message");
	if(pMessage == &json_value_none)
	{
		str_copy(Out.m_aError, "No message in choice", sizeof(Out.m_aError));
		Out.m_Success = false;
		return false;
	}
	if(pMessage->type != json_object)
	{
		str_copy(Out.m_aError, "message is not object", sizeof(Out.m_aError));
		Out.m_Success = false;
		return false;
	}

	const json_value *pContent = json_object_get(pMessage, "content");
	if(pContent == &json_value_none)
	{
		str_copy(Out.m_aError, "No content in message", sizeof(Out.m_aError));
		Out.m_Success = false;
		return false;
	}
	if(pContent->type != json_string)
	{
		str_copy(Out.m_aError, "content is not string", sizeof(Out.m_aError));
		Out.m_Success = false;
		return false;
	}

	str_copy(Out.m_aText, pContent->u.string.ptr, sizeof(Out.m_aText));
	Out.m_Success = true;
	return true;
}

namespace
{
	bool StrEndsWithNocase(const char *pStr, const char *pSuffix)
	{
		const int StrLen = str_length(pStr);
		const int SuffixLen = str_length(pSuffix);
		return StrLen >= SuffixLen && str_comp_nocase(pStr + StrLen - SuffixLen, pSuffix) == 0;
	}

	void StripTrailingSlashes(char *pUrl)
	{
		int End = str_length(pUrl);
		while(End > 0 && pUrl[End - 1] == '/')
			--End;
		pUrl[End] = '\0';
	}

	void StripSuffixNocase(char *pUrl, const char *pSuffix)
	{
		pUrl[str_length(pUrl) - str_length(pSuffix)] = '\0';
		StripTrailingSlashes(pUrl);
	}
} // namespace

bool NormalizeLlmEndpoint(const char *pEndpoint, SLlmEndpointInfo &Out)
{
	Out.m_aBaseUrl[0] = '\0';
	Out.m_Style = ELlmApiStyle::AUTO;
	if(!pEndpoint)
		return false;

	// 去除首尾空白
	char aTrimmed[256];
	{
		const char *pStart = pEndpoint;
		while(*pStart == ' ' || *pStart == '\t' || *pStart == '\r' || *pStart == '\n')
			++pStart;
		int End = str_length(pStart);
		while(End > 0 && (pStart[End - 1] == ' ' || pStart[End - 1] == '\t' || pStart[End - 1] == '\r' || pStart[End - 1] == '\n'))
			--End;
		if(End >= (int)sizeof(aTrimmed))
			return false;
		str_copy(aTrimmed, pStart, End + 1);
	}

	if(aTrimmed[0] == '\0')
		return false;
	// 仅接受 http(s) 地址
	if(!str_startswith_nocase(aTrimmed, "http://") && !str_startswith_nocase(aTrimmed, "https://"))
		return false;

	StripTrailingSlashes(aTrimmed);

	// 识别显式路径后缀（容忍 chat/completion 少写 s 的笔误与大小写差异）
	if(StrEndsWithNocase(aTrimmed, "/chat/completions"))
	{
		Out.m_Style = ELlmApiStyle::CHAT;
		StripSuffixNocase(aTrimmed, "/chat/completions");
	}
	else if(StrEndsWithNocase(aTrimmed, "/chat/completion"))
	{
		Out.m_Style = ELlmApiStyle::CHAT;
		StripSuffixNocase(aTrimmed, "/chat/completion");
	}
	else if(StrEndsWithNocase(aTrimmed, "/responses"))
	{
		Out.m_Style = ELlmApiStyle::RESPONSES;
		StripSuffixNocase(aTrimmed, "/responses");
	}

	if(aTrimmed[0] == '\0')
		return false;
	str_copy(Out.m_aBaseUrl, aTrimmed, sizeof(Out.m_aBaseUrl));
	return true;
}
