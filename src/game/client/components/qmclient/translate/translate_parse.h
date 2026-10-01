// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_PARSE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_PARSE_H

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

// LLM 接口格式：AUTO 表示由客户端自动识别（先 Chat Completions 后 Responses）
enum class ELlmApiStyle
{
	AUTO = 0,
	CHAT = 1,
	RESPONSES = 2,
};

// 端点归一结果
struct SLlmEndpointInfo
{
	char m_aBaseUrl[256];
	ELlmApiStyle m_Style; // URL 后缀显式指定的格式；无后缀时为 AUTO
};

// 归一用户填写的 LLM 端点：
// - 去除首尾空白与尾部斜杠；
// - 识别并剥离完整路径后缀（/chat/completions、/responses，容忍少写 s 的笔误与大小写差异）；
// - 剥离后得到 base URL；显式后缀决定格式，否则 AUTO。
// 返回 false 表示端点非法（空、非 http(s) 前缀）。
bool NormalizeLlmEndpoint(const char *pEndpoint, SLlmEndpointInfo &Out);

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_PARSE_H
