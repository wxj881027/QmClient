#ifndef ENGINE_SHARED_JSON_H
#define ENGINE_SHARED_JSON_H

#include <engine/external/json-parser/json.h>

// 默认校验整棵树；调用方可显式排除一个可选值子树，父对象中的成员名仍会校验。
bool JsonValidateUtf8(const json_value *pRoot, const json_value *pIgnoredValue = nullptr);

json_value *JsonParse(const json_char *pJson, size_t Length);
json_value *JsonParseEx(json_settings *pSettings, const json_char *pJson, size_t Length, char *pError);

const struct _json_value *json_object_get(const json_value *pObject, const char *pIndex);
const struct _json_value *json_array_get(const json_value *pArray, int Index);
int json_array_length(const json_value *pArray);
const char *json_string_get(const json_value *pString);
int json_int_get(const json_value *pInteger);
int json_boolean_get(const json_value *pBoolean);

char *EscapeJson(char *pBuffer, int BufferSize, const char *pString);
const char *JsonBool(bool Bool);

#endif // ENGINE_SHARED_JSON_H
