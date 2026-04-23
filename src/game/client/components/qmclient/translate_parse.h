#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_PARSE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_PARSE_H

// LLM 响应解析结果
struct SLlmParseResult
{
	bool m_Success;
	char m_aText[4096];
	char m_aError[512];
};

struct _json_value;

// 解析 LLM API 的 JSON 响应
// pObj: 解析后的 JSON 对象（来自 json_parse）
// Out: 解析结果
// 返回: true 表示解析成功，false 表示解析失败
bool ParseLlmResponseJson(const struct _json_value *pObj, SLlmParseResult &Out);

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_PARSE_H
