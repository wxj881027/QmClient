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
