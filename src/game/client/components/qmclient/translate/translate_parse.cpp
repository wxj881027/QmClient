// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "translate_parse.h"

#include <base/system.h>

#include <engine/shared/json.h>

namespace
{
	// JSON名称可含内嵌NUL，不能把未知字段的C字符串前缀当成协议字段。
	const json_value *JsonObjectGet(const json_value *pObject, const char *pName)
	{
		if(!pObject || pObject->type != json_object)
			return &json_value_none;
		const auto Length = static_cast<unsigned int>(str_length(pName));
		for(unsigned int i = 0; i < pObject->u.object.length; ++i)
		{
			const auto &Entry = pObject->u.object.values[i];
			if(Entry.name_length == Length && str_comp(Entry.name, pName) == 0)
				return Entry.value;
		}
		return &json_value_none;
	}

	bool JsonStringIs(const json_value *pValue, const char *pExpected)
	{
		return pValue->type == json_string && pValue->u.string.length == static_cast<unsigned int>(str_length(pExpected)) && str_comp(pValue->u.string.ptr, pExpected) == 0;
	}

	bool RejectInvalidMetadata(const json_value *pValue, SLlmParseResult &Out)
	{
		if(pValue->type != json_string || (pValue->u.string.length == static_cast<unsigned int>(str_length(pValue->u.string.ptr)) && str_utf8_check(pValue->u.string.ptr)))
			return false;
		str_copy(Out.m_aError, "Translation response has invalid text");
		return true;
	}

	bool IsContentRefusalError(const json_value *pError)
	{
		const json_value *pCode = JsonObjectGet(pError, "code");
		return JsonStringIs(pCode, "content_filter") || JsonStringIs(pCode, "content_policy_violation");
	}

	bool RefuseContent(SLlmParseResult &Out)
	{
		Out.m_Success = false;
		Out.m_Refused = true;
		Out.m_aText[0] = '\0';
		str_copy(Out.m_aError, "Translation service refused this content");
		return false;
	}
}

bool ParseLlmResponseJson(const json_value *pObj, SLlmParseResult &Out)
{
	Out = {};
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

	const json_value *pError = JsonObjectGet(pObj, "error");
	if(pError != &json_value_none && pError->type != json_null)
	{
		if(IsContentRefusalError(pError))
			return RefuseContent(Out);
		const json_value *pMessage = JsonObjectGet(pError, "message");
		const char *pMessageStr = pMessage != &json_value_none && pMessage->type == json_string ? pMessage->u.string.ptr : "LLM API request failed";
		str_copy(Out.m_aError, pMessageStr, sizeof(Out.m_aError));
		Out.m_Success = false;
		return false;
	}

	const json_value *pChoices = JsonObjectGet(pObj, "choices");
	if(pChoices == &json_value_none)
	{
		char aErrorMsg[512];
		str_copy(aErrorMsg, "No choices in response", sizeof(aErrorMsg));

		const json_value *pCode = JsonObjectGet(pObj, "code");
		const json_value *pMsg = JsonObjectGet(pObj, "msg");
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

	const json_value *pMessage = JsonObjectGet(pChoice, "message");
	const json_value *pFinishReason = JsonObjectGet(pChoice, "finish_reason");
	if(RejectInvalidMetadata(pFinishReason, Out))
		return false;
	if(JsonStringIs(pFinishReason, "content_filter"))
		return RefuseContent(Out);
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
	const json_value *pRefusal = JsonObjectGet(pMessage, "refusal");
	if(pRefusal->type == json_string && pRefusal->u.string.length > 0)
		return RefuseContent(Out);

	if(JsonStringIs(pFinishReason, "length") || JsonStringIs(pFinishReason, "tool_calls"))
	{
		str_copy(Out.m_aError, "Translation response is incomplete");
		return false;
	}
	const json_value *pContent = JsonObjectGet(pMessage, "content");
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

	if(pContent->u.string.length == 0 || str_length(pContent->u.string.ptr) != static_cast<int>(pContent->u.string.length) || !str_utf8_check(pContent->u.string.ptr))
	{
		str_copy(Out.m_aError, "Translation response has invalid text");
		return false;
	}
	if(pContent->u.string.length >= sizeof(Out.m_aText))
	{
		str_copy(Out.m_aError, "Translation result exceeds buffer capacity");
		return false;
	}
	str_copy(Out.m_aText, pContent->u.string.ptr, sizeof(Out.m_aText));
	Out.m_Success = true;
	return true;
}

bool ParseLlmResponsesJson(const json_value *pObj, SLlmParseResult &Out)
{
	Out = {};
	if(!pObj || pObj->type != json_object)
	{
		str_copy(Out.m_aError, "Response is not a JSON object");
		return false;
	}
	const json_value *pError = JsonObjectGet(pObj, "error");
	if(pError != &json_value_none && pError->type != json_null)
	{
		if(IsContentRefusalError(pError))
			return RefuseContent(Out);
		const json_value *pMessage = JsonObjectGet(pError, "message");
		str_copy(Out.m_aError, pMessage->type == json_string ? pMessage->u.string.ptr : "LLM API request failed");
		return false;
	}
	const json_value *pIncompleteReason = JsonObjectGet(JsonObjectGet(pObj, "incomplete_details"), "reason");
	if(RejectInvalidMetadata(pIncompleteReason, Out))
		return false;
	if(JsonStringIs(pIncompleteReason, "content_filter"))
		return RefuseContent(Out);
	const json_value *pStatus = JsonObjectGet(pObj, "status");
	if(pStatus->type == json_string && !JsonStringIs(pStatus, "completed"))
	{
		str_copy(Out.m_aError, "Translation response is incomplete");
		return false;
	}
	// 顶层文本也可能伴随拒绝片段，先检查所有消息，避免发布部分译文。
	const json_value *pOutput = JsonObjectGet(pObj, "output");
	if(pOutput->type == json_array)
	{
		for(size_t i = 0; i < pOutput->u.array.length; ++i)
		{
			const json_value *pItem = pOutput->u.array.values[i];
			const json_value *pType = JsonObjectGet(pItem, "type");
			if(RejectInvalidMetadata(pType, Out))
				return false;
			if(!JsonStringIs(pType, "message"))
				continue;
			const json_value *pContent = JsonObjectGet(pItem, "content");
			if(pContent->type != json_array)
				continue;
			for(size_t j = 0; j < pContent->u.array.length; ++j)
			{
				const json_value *pPartType = JsonObjectGet(pContent->u.array.values[j], "type");
				if(RejectInvalidMetadata(pPartType, Out))
					return false;
				if(JsonStringIs(pPartType, "refusal"))
					return RefuseContent(Out);
			}
		}
	}
	auto AppendText = [&](const json_value *pText) {
		if(pText->type != json_string || str_length(pText->u.string.ptr) != static_cast<int>(pText->u.string.length) || !str_utf8_check(pText->u.string.ptr))
		{
			Out.m_aText[0] = '\0';
			str_copy(Out.m_aError, "Translation response has invalid text");
			return false;
		}
		const size_t Current = str_length(Out.m_aText);
		if(pText->u.string.length >= sizeof(Out.m_aText) - Current)
		{
			Out.m_aText[0] = '\0';
			str_copy(Out.m_aError, "Translation result exceeds buffer capacity");
			return false;
		}
		str_append(Out.m_aText, pText->u.string.ptr);
		return true;
	};
	const json_value *pTopText = JsonObjectGet(pObj, "output_text");
	if(pTopText->type == json_string && pTopText->u.string.length > 0)
	{
		if(!AppendText(pTopText))
			return false;
		Out.m_Success = true;
		return true;
	}
	if(pOutput->type == json_array)
	{
		for(size_t i = 0; i < pOutput->u.array.length; ++i)
		{
			const json_value *pItem = pOutput->u.array.values[i];
			if(!JsonStringIs(JsonObjectGet(pItem, "type"), "message"))
				continue;
			const json_value *pContent = JsonObjectGet(pItem, "content");
			if(pContent->type != json_array)
				continue;
			for(size_t j = 0; j < pContent->u.array.length; ++j)
			{
				const json_value *pPart = pContent->u.array.values[j];
				if(JsonStringIs(JsonObjectGet(pPart, "type"), "output_text") && !AppendText(JsonObjectGet(pPart, "text")))
					return false;
			}
		}
	}
	if(Out.m_aText[0] == '\0')
	{
		str_copy(Out.m_aError, "No output_text in response");
		return false;
	}
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
