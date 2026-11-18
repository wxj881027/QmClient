#include <base/system.h>

#include <engine/shared/json.h>

#include <cstring>
#include <memory>
#include <vector>

static bool JsonStringHasValidUtf8(const char *pString, size_t Length)
{
	const char *pEnd = pString + Length;
	// 解析器保留终止符，内嵌的 NUL 也可以作为一个字符解码。
	while(pString < pEnd)
	{
		if(str_utf8_decode(&pString) < 0)
			return false;
	}
	return pString == pEnd;
}

bool JsonValidateUtf8(const json_value *pRoot, const json_value *pIgnoredValue)
{
	if(pRoot == nullptr)
		return false;
	std::vector<const json_value *> vpValues;
	vpValues.push_back(pRoot);
	while(!vpValues.empty())
	{
		const json_value *pValue = vpValues.back();
		vpValues.pop_back();
		if(pValue == pIgnoredValue)
			continue;
		switch(pValue->type)
		{
		case json_string:
			if(!JsonStringHasValidUtf8(pValue->u.string.ptr, pValue->u.string.length))
			{
				return false;
			}
			break;
		case json_array:
			for(unsigned i = 0; i < pValue->u.array.length; i++)
			{
				vpValues.push_back(pValue->u.array.values[i]);
			}
			break;
		case json_object:
			for(unsigned i = 0; i < pValue->u.object.length; i++)
			{
				const auto &Entry = pValue->u.object.values[i];
				if(!JsonStringHasValidUtf8(Entry.name, Entry.name_length))
				{
					return false;
				}
				vpValues.push_back(Entry.value);
			}
			break;
		default:
			break;
		}
	}
	return true;
}

json_value *JsonParse(const json_char *pJson, size_t Length)
{
	json_settings Settings{};
	return JsonParseEx(&Settings, pJson, Length, nullptr);
}

json_value *JsonParseEx(json_settings *pSettings, const json_char *pJson, size_t Length, char *pError)
{
	// 底层解析器会将输入中的 NUL 当成结束标记。
	if(!pJson || std::memchr(pJson, '\0', Length))
	{
		if(pError)
			str_copy(pError, !pJson ? "missing JSON input" : "unexpected NUL in JSON input", json_error_max);
		return nullptr;
	}

	json_settings Settings = *pSettings;
	const auto FreeValue = [&Settings](json_value *pValue) {
		if(Settings.mem_free)
			json_value_free_ex(&Settings, pValue);
		else
			json_value_free(pValue);
	};
	std::unique_ptr<json_value, decltype(FreeValue)> pValue(json_parse_ex(&Settings, pJson, Length, pError), FreeValue);
	if(pValue == nullptr)
	{
		return nullptr;
	}
	if(!JsonValidateUtf8(pValue.get()))
	{
		if(pError)
		{
			str_copy(pError, "invalid utf-8 in string literal", json_error_max);
		}
		return nullptr;
	}
	return pValue.release();
}

const struct _json_value *json_object_get(const json_value *pObject, const char *pIndex)
{
	if(pObject->type != json_object)
		return &json_value_none;

	const size_t IndexLength = std::strlen(pIndex);
	for(unsigned i = 0; i < pObject->u.object.length; ++i)
	{
		const auto &Entry = pObject->u.object.values[i];
		if(Entry.name_length == IndexLength && std::memcmp(Entry.name, pIndex, IndexLength) == 0)
			return Entry.value;
	}

	return &json_value_none;
}

const struct _json_value *json_array_get(const json_value *pArray, int Index)
{
	return &(*pArray)[Index];
}

int json_array_length(const json_value *pArray)
{
	return pArray->u.array.length;
}

const char *json_string_get(const json_value *pString)
{
	return pString->u.string.ptr;
}

int json_int_get(const json_value *pInteger)
{
	return pInteger->type == json_integer ? static_cast<int>(pInteger->u.integer) : 0;
}

int json_boolean_get(const json_value *pBoolean)
{
	return pBoolean->u.boolean != 0;
}

static char EscapeJsonChar(char c)
{
	switch(c)
	{
	case '\"': return '\"';
	case '\\': return '\\';
	case '\b': return 'b';
	case '\n': return 'n';
	case '\r': return 'r';
	case '\t': return 't';
	// Don't escape '\f', who uses that. :)
	default: return 0;
	}
}

char *EscapeJson(char *pBuffer, int BufferSize, const char *pString)
{
	dbg_assert(str_utf8_check(pString), "invalid UTF-8 in string");
	dbg_assert(BufferSize > 0, "can't null-terminate the string");
	// Subtract the space for null termination early.
	BufferSize--;

	char *pResult = pBuffer;
	while(BufferSize && *pString)
	{
		char c = *pString;
		pString++;
		char Escaped = EscapeJsonChar(c);
		if(Escaped)
		{
			if(BufferSize < 2)
			{
				break;
			}
			*pBuffer++ = '\\';
			*pBuffer++ = Escaped;
			BufferSize -= 2;
		}
		// Assuming ASCII/UTF-8, "if control character".
		else if((unsigned char)c < 0x20)
		{
			// \uXXXX
			if(BufferSize < 6)
			{
				break;
			}
			// BufferSize 已扣除终止符空间，格式化时加回以保留完整的六字节转义。
			str_format(pBuffer, BufferSize + 1, "\\u%04x", c);
			pBuffer += 6;
			BufferSize -= 6;
		}
		else
		{
			*pBuffer++ = c;
			BufferSize--;
		}
	}
	*pBuffer = 0;
	if(!str_utf8_isstart(*pString))
		str_utf8_fix_truncation(pResult);
	return pResult;
}

const char *JsonBool(bool Bool)
{
	if(Bool)
	{
		return "true";
	}
	else
	{
		return "false";
	}
}
