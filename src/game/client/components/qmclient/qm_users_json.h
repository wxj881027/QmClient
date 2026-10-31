#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_USERS_JSON_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_USERS_JSON_H

#include <base/str.h>

#include <engine/shared/json.h>

#include <cstring>

namespace qm_users_json
{
	// 名单字段按完整成员名匹配，未知字段的 NUL 前缀不参与协议解释。
	inline const json_value *Field(const json_value *pObject, const char *pName)
	{
		if(!pObject || pObject->type != json_object)
			return &json_value_none;
		const size_t Length = std::strlen(pName);
		for(unsigned Index = 0; Index < pObject->u.object.length; ++Index)
		{
			const auto &Member = pObject->u.object.values[Index];
			if(Member.name_length == Length && std::memcmp(Member.name, pName, Length) == 0)
				return Member.value;
		}
		return &json_value_none;
	}

	// 借用解析器提供的已终止字符串；完整长度内不能含 NUL，随后检查 UTF-8。
	inline bool Text(const json_value *pValue, bool AllowEmpty = true)
	{
		return pValue && pValue->type == json_string && pValue->u.string.ptr &&
		       (AllowEmpty || pValue->u.string.length != 0) &&
		       !std::memchr(pValue->u.string.ptr, '\0', pValue->u.string.length) &&
		       str_utf8_check(pValue->u.string.ptr);
	}
}

#endif
