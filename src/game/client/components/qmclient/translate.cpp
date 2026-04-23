#include "translate.h"
#include "translate_parse.h"

#include <base/hash.h>
#include <base/log.h>
#include <base/system.h>

#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/protocol.h>

#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>

namespace
{
constexpr size_t TC3_HMAC_BLOCK_SIZE = 64;
constexpr const char *TENCENTCLOUD_TMT_ACTION = "TextTranslate";
constexpr const char *TENCENTCLOUD_TMT_VERSION = "2018-03-21";
constexpr const char *TENCENTCLOUD_TMT_SERVICE = "tmt";
constexpr const char *TENCENTCLOUD_TMT_DEFAULT_ENDPOINT = "https://tmt.tencentcloudapi.com/";
constexpr const char *TENCENTCLOUD_SECRET_ID_FALLBACK = "";
constexpr const char *TENCENTCLOUD_SECRET_KEY_FALLBACK = "";

constexpr const char *DEFAULT_TRANSLATE_PROMPT =
    "You are a translation assistant. Your task is straightforward:\n\n"
    "1. Translate the user's message into %s\n"
    "2. Keep game terminology consistent (see glossary below)\n"
    "3. Output ONLY the translated text, nothing else\n\n"
    "Game terminology glossary (DDNet/DDraceNetwork):\n"
    "- hook = 钩子\n"
    "- freeze = 冻结\n"
    "- team = 队伍/组队\n"
    "- race = 竞速/比赛\n"
    "- checkpoint = 检查点/CP\n"
    "- unfreeze = 解冻\n"
    "- deep freeze = 深冻\n"
    "- tele = 传送\n"
    "- swap = 交换\n"
    "- dummy = 分身\n"
    "- hammer = 锤子\n"
    "- shotgun = 霰弹枪\n"
    "- grenade = 榴弹/手雷\n"
    "- laser = 激光枪\n"
    "- ninja = 忍者\n"
    "- kill = 自杀/击杀\n"
    "- spec/spectate = 旁观\n"
    "- tee = 角色/玩家\n\n"
    "Rules:\n"
    "- Do NOT add any explanation, notes, or commentary\n"
    "- Do NOT say 'I don't know' or 'I haven't learned this'\n"
    "- If unsure, provide the best possible translation based on context\n"
    "- Preserve the original meaning and tone\n"
    "- Keep it concise and natural";

enum class ELlmProvider
{
	ZHIPU_AI = 0,
	DEEPSEEK = 1,
	OPENAI = 2,
	CUSTOM = 3,
};

const char *GetDefaultLlmEndpoint(ELlmProvider Provider)
{
	switch(Provider)
	{
		case ELlmProvider::ZHIPU_AI:
			return "https://open.bigmodel.cn/api/paas/v4/chat/completions";
		case ELlmProvider::DEEPSEEK:
			return "https://api.deepseek.com/chat/completions";
		case ELlmProvider::OPENAI:
			return "https://api.openai.com/v1/chat/completions";
		default:
			return "https://open.bigmodel.cn/api/paas/v4/chat/completions";
	}
}

const char *GetLlmEndpoint(ELlmProvider Provider)
{
	switch(Provider)
	{
		case ELlmProvider::ZHIPU_AI:
			return g_Config.m_QmTranslateLlmEndpointZhipu[0] != '\0' ? g_Config.m_QmTranslateLlmEndpointZhipu : GetDefaultLlmEndpoint(Provider);
		case ELlmProvider::DEEPSEEK:
			return g_Config.m_QmTranslateLlmEndpointDeepseek[0] != '\0' ? g_Config.m_QmTranslateLlmEndpointDeepseek : GetDefaultLlmEndpoint(Provider);
		case ELlmProvider::OPENAI:
			return g_Config.m_QmTranslateLlmEndpointOpenai[0] != '\0' ? g_Config.m_QmTranslateLlmEndpointOpenai : GetDefaultLlmEndpoint(Provider);
		case ELlmProvider::CUSTOM:
		default:
			return g_Config.m_QmTranslateLlmEndpointCustom;
	}
}

const char *GetLlmModel(ELlmProvider Provider)
{
	switch(Provider)
	{
		case ELlmProvider::ZHIPU_AI:
			return g_Config.m_QmTranslateLlmModelZhipu;
		case ELlmProvider::DEEPSEEK:
			return g_Config.m_QmTranslateLlmModelDeepseek;
		case ELlmProvider::OPENAI:
			return g_Config.m_QmTranslateLlmModelOpenai;
		case ELlmProvider::CUSTOM:
		default:
			return g_Config.m_QmTranslateLlmModelCustom;
	}
}

const char *GetLlmApiKey(ELlmProvider Provider)
{
	switch(Provider)
	{
		case ELlmProvider::ZHIPU_AI:
			if(g_Config.m_QmTranslateLlmKeyZhipu[0] != '\0')
				return g_Config.m_QmTranslateLlmKeyZhipu;
			if(const char *pEnvKey = std::getenv("QMTRANSLATE_LLM_KEY_ZHIPU"))
				return pEnvKey;
			return "";
		case ELlmProvider::DEEPSEEK:
			if(g_Config.m_QmTranslateLlmKeyDeepseek[0] != '\0')
				return g_Config.m_QmTranslateLlmKeyDeepseek;
			if(const char *pEnvKey = std::getenv("QMTRANSLATE_LLM_KEY_DEEPSEEK"))
				return pEnvKey;
			return "";
		case ELlmProvider::OPENAI:
			if(g_Config.m_QmTranslateLlmKeyOpenai[0] != '\0')
				return g_Config.m_QmTranslateLlmKeyOpenai;
			if(const char *pEnvKey = std::getenv("QMTRANSLATE_LLM_KEY_OPENAI"))
				return pEnvKey;
			return "";
		case ELlmProvider::CUSTOM:
		default:
			if(g_Config.m_QmTranslateLlmKeyCustom[0] != '\0')
				return g_Config.m_QmTranslateLlmKeyCustom;
			if(const char *pEnvKey = std::getenv("QMTRANSLATE_LLM_KEY_CUSTOM"))
				return pEnvKey;
			return "";
	}
}

SHA256_DIGEST HmacSha256(const unsigned char *pKey, size_t KeyLength, const unsigned char *pData, size_t DataLength)
{
	std::array<unsigned char, TC3_HMAC_BLOCK_SIZE> aKeyBlock{};
	if(KeyLength > TC3_HMAC_BLOCK_SIZE)
	{
		const SHA256_DIGEST KeyDigest = sha256(pKey, KeyLength);
		mem_copy(aKeyBlock.data(), KeyDigest.data, sizeof(KeyDigest.data));
	}
	else if(KeyLength > 0)
	{
		mem_copy(aKeyBlock.data(), pKey, KeyLength);
	}

	std::array<unsigned char, TC3_HMAC_BLOCK_SIZE> aInnerPad{};
	std::array<unsigned char, TC3_HMAC_BLOCK_SIZE> aOuterPad{};
	for(size_t i = 0; i < TC3_HMAC_BLOCK_SIZE; ++i)
	{
		aInnerPad[i] = aKeyBlock[i] ^ 0x36;
		aOuterPad[i] = aKeyBlock[i] ^ 0x5c;
	}

	SHA256_CTX InnerCtx;
	sha256_init(&InnerCtx);
	sha256_update(&InnerCtx, aInnerPad.data(), aInnerPad.size());
	if(DataLength > 0)
		sha256_update(&InnerCtx, pData, DataLength);
	const SHA256_DIGEST InnerDigest = sha256_finish(&InnerCtx);

	SHA256_CTX OuterCtx;
	sha256_init(&OuterCtx);
	sha256_update(&OuterCtx, aOuterPad.data(), aOuterPad.size());
	sha256_update(&OuterCtx, InnerDigest.data, sizeof(InnerDigest.data));
	return sha256_finish(&OuterCtx);
}

std::string Sha256Hex(const unsigned char *pData, size_t DataLength)
{
	char aDigest[SHA256_MAXSTRSIZE];
	sha256_str(sha256(pData, DataLength), aDigest, sizeof(aDigest));
	return aDigest;
}

std::string Sha256Hex(const std::string &Value)
{
	return Sha256Hex(reinterpret_cast<const unsigned char *>(Value.data()), Value.size());
}

std::string HmacSha256Hex(const unsigned char *pKey, size_t KeyLength, const std::string &Value)
{
	char aDigest[SHA256_MAXSTRSIZE];
	const SHA256_DIGEST Digest = HmacSha256(pKey, KeyLength,
		reinterpret_cast<const unsigned char *>(Value.data()), Value.size());
	sha256_str(Digest, aDigest, sizeof(aDigest));
	return aDigest;
}

std::string HmacSha256Raw(const unsigned char *pKey, size_t KeyLength, const std::string &Value)
{
	const SHA256_DIGEST Digest = HmacSha256(pKey, KeyLength,
		reinterpret_cast<const unsigned char *>(Value.data()), Value.size());
	return std::string(reinterpret_cast<const char *>(Digest.data), sizeof(Digest.data));
}

bool FormatUtcDate(int64_t Timestamp, char *pOutDate, size_t OutDateSize)
{
	time_t TimeValue = static_cast<time_t>(Timestamp);
	std::tm UtcTime{};
#if defined(CONF_FAMILY_WINDOWS)
	if(gmtime_s(&UtcTime, &TimeValue) != 0)
		return false;
#else
	if(gmtime_r(&TimeValue, &UtcTime) == nullptr)
		return false;
#endif
	return strftime(pOutDate, OutDateSize, "%Y-%m-%d", &UtcTime) > 0;
}

bool ParseHttpsUrl(const char *pUrl, std::string &Host, std::string &Path, char *pError, size_t ErrorSize)
{
	if(!pUrl || pUrl[0] == '\0')
	{
		str_copy(pError, "TencentCloud endpoint is empty", ErrorSize);
		return false;
	}

	std::string Url = pUrl;
	const size_t FirstNonWhitespace = Url.find_first_not_of(" \t\r\n");
	if(FirstNonWhitespace == std::string::npos)
	{
		str_copy(pError, "TencentCloud endpoint is empty", ErrorSize);
		return false;
	}
	const size_t LastNonWhitespace = Url.find_last_not_of(" \t\r\n");
	Url = Url.substr(FirstNonWhitespace, LastNonWhitespace - FirstNonWhitespace + 1);

	const char *pWithoutScheme = str_startswith_nocase(Url.c_str(), "https://");
	if(!pWithoutScheme)
	{
		if(str_startswith_nocase(Url.c_str(), "http://"))
		{
			str_copy(pError, "TencentCloud endpoint must use https", ErrorSize);
			return false;
		}

		if(str_find(Url.c_str(), "://") != nullptr)
		{
			str_copy(pError, "TencentCloud endpoint has unsupported scheme", ErrorSize);
			return false;
		}

		// Allow SDK-style endpoints like `tmt.tencentcloudapi.com`.
		pWithoutScheme = Url.c_str();
	}

	const char *pPath = str_find(pWithoutScheme, "/");
	if(pPath)
	{
		Host.assign(pWithoutScheme, pPath - pWithoutScheme);
		Path.assign(pPath);
	}
	else
	{
		Host.assign(pWithoutScheme);
		Path = "/";
	}

	if(Host.empty())
	{
		str_copy(pError, "TencentCloud endpoint host is empty", ErrorSize);
		return false;
	}
	if(Path.empty())
		Path = "/";
	if(Path.find('?') != std::string::npos)
	{
		str_copy(pError, "TencentCloud endpoint must not contain query parameters", ErrorSize);
		return false;
	}
	return true;
}

bool IsChineseLanguage(const char *pLanguage)
{
	if(!pLanguage || pLanguage[0] == '\0')
		return false;
	return str_comp_nocase(pLanguage, "zh") == 0 ||
		str_comp_nocase(pLanguage, "zh-cn") == 0 ||
		str_comp_nocase(pLanguage, "zh-tw") == 0;
}

const char *GetTencentCloudSecretId()
{
	if(g_Config.m_QmTranslateTcSecretId[0] != '\0')
		return g_Config.m_QmTranslateTcSecretId;
	if(const char *pEnvSecretId = std::getenv("TENCENTCLOUD_SECRET_ID"))
		return pEnvSecretId;
	return TENCENTCLOUD_SECRET_ID_FALLBACK;
}

const char *GetTencentCloudSecretKey()
{
	if(g_Config.m_QmTranslateTcSecretKey[0] != '\0')
		return g_Config.m_QmTranslateTcSecretKey;
	if(const char *pEnvSecretKey = std::getenv("TENCENTCLOUD_SECRET_KEY"))
		return pEnvSecretKey;
	return TENCENTCLOUD_SECRET_KEY_FALLBACK;
}

const char *GetEffectiveTranslateTarget(const char *pTarget)
{
	return (pTarget && pTarget[0] != '\0') ? pTarget : CConfig::ms_pQmTranslateTarget;
}

// 验证语言代码格式
// 有效格式：2-3 个字母（如 zh, en, ja）或 xx-XX 格式（如 zh-CN, zh-TW）
static bool IsValidLanguageCode(const char *pCode)
{
	if(!pCode || pCode[0] == '\0')
		return false;

	size_t Len = str_length(pCode);
	if(Len < 2 || Len > 5) // 最短 "en"，最长 "zh-CN"
		return false;

	// 检查格式：纯字母或 xx-XX 格式
	for(size_t i = 0; i < Len; i++)
	{
		char c = pCode[i];
		if(i == 2 && c == '-')
			continue; // 允许 xx-XX 格式中的连字符
		if(c < 'a' || c > 'z')
		{
			// 允许大写字母（在 - 后面）
			if(i > 2 && c >= 'A' && c <= 'Z')
				continue;
			return false;
		}
	}
	return true;
}

bool IsOutgoingTranslateTargetChar(char Character)
{
	const unsigned char Value = static_cast<unsigned char>(Character);
	return std::isalnum(Value) != 0 || Character == '-' || Character == '_';
}

bool ParseOutgoingTranslateTarget(const char *pLine, std::string &Text, std::string &Target)
{
	if(!pLine)
		return false;

	const char *pTrimmedLine = str_utf8_skip_whitespaces(pLine);
	if(*pTrimmedLine == '\0' || *pTrimmedLine == '/')
		return false;

	std::string Line = pLine;
	const size_t LastNonWhitespace = Line.find_last_not_of(" \t\r\n");
	if(LastNonWhitespace == std::string::npos || Line[LastNonWhitespace] != ']')
		return false;

	const size_t OpenBracket = Line.rfind('[', LastNonWhitespace);
	if(OpenBracket == std::string::npos || OpenBracket + 1 >= LastNonWhitespace)
		return false;

	Target = Line.substr(OpenBracket + 1, LastNonWhitespace - OpenBracket - 1);
	if(Target.size() < 2 || Target.size() >= 16)
		return false;
	if(std::any_of(Target.begin(), Target.end(), [](char Character) { return !IsOutgoingTranslateTargetChar(Character); }))
		return false;

	size_t TextEnd = OpenBracket;
	while(TextEnd > 0 && std::isspace(static_cast<unsigned char>(Line[TextEnd - 1])) != 0)
		--TextEnd;
	if(TextEnd == 0)
		return false;

	Text = Line.substr(0, TextEnd);
	return true;
}

// JSON 字符串转义
// 只转义 JSON 特殊字符（" \）和 ASCII 控制字符（0x00-0x1F）
// UTF-8 多字节字符（>= 0x80）不需要转义，可以直接嵌入 JSON 字符串
// 因为 JSON 规范允许字符串中包含任意 UTF-8 编码的 Unicode 字符
static void EscapeJsonString(const char *pStr, char *pOut, size_t OutSize)
{
	if(OutSize == 0)
		return;

	pOut[0] = '\0';
	if(OutSize < 3)
		return;

	size_t OutPos = 0;
	pOut[OutPos++] = '"';
	for(const char *p = pStr; *p; ++p)
	{
		const unsigned char c = (unsigned char)*p;
		const auto CanAppend = [&](size_t Count) {
			return OutPos + Count + 2 <= OutSize;
		};

		if(c == '"' || c == '\\' || c == '\b' || c == '\n' || c == '\r' || c == '\t')
		{
			if(!CanAppend(2))
				break;
			pOut[OutPos++] = '\\';
			switch(c)
			{
			case '"': pOut[OutPos++] = '"'; break;
			case '\\': pOut[OutPos++] = '\\'; break;
			case '\b': pOut[OutPos++] = 'b'; break;
			case '\n': pOut[OutPos++] = 'n'; break;
			case '\r': pOut[OutPos++] = 'r'; break;
			case '\t': pOut[OutPos++] = 't'; break;
			}
		}
		else if(c < 0x20)
		{
			if(!CanAppend(6))
				break;
			str_format(pOut + OutPos, OutSize - OutPos, "\\u%04x", c);
			OutPos += 6;
		}
		else
		{
			if(!CanAppend(1))
				break;
			pOut[OutPos++] = (char)c;
		}
	}

	pOut[OutPos++] = '"';
	pOut[OutPos] = '\0';
}
} // namespace

static void UrlEncode(const char *pText, char *pOut, size_t Length)
{
	if(Length == 0)
		return;
	size_t OutPos = 0;
	for(const char *p = pText; *p && OutPos < Length - 1; ++p)
	{
		unsigned char c = *(const unsigned char *)p;
		if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
		{
			if(OutPos >= Length - 1)
				break;
			pOut[OutPos++] = c;
		}
		else
		{
			if(OutPos + 3 >= Length)
				break;
			snprintf(pOut + OutPos, 4, "%%%02X", c);
			OutPos += 3;
		}
	}
	pOut[OutPos] = '\0';
}

const char *ITranslateBackend::EncodeTarget(const char *pTarget) const
{
	if(!pTarget || pTarget[0] == '\0')
		return CConfig::ms_pQmTranslateTarget;
	return pTarget;
}

bool ITranslateBackend::CompareTargets(const char *pA, const char *pB) const
{
	if(pA == pB) // if(!pA && !pB)
		return true;
	if(!pA || !pB)
		return false;
	if(str_comp_nocase(EncodeTarget(pA), EncodeTarget(pB)) == 0)
		return true;
	return false;
}

class ITranslateBackendHttp : public ITranslateBackend
{
protected:
	std::shared_ptr<CHttpRequest> m_pHttpRequest = nullptr;
	char m_aInitError[256] = "";
	virtual bool ParseResponse(CTranslateResponse &Out) = 0;
	virtual bool ParseHttpError() const { return false; }
	void SetInitError(const char *pError)
	{
		str_copy(m_aInitError, pError, sizeof(m_aInitError));
	}

	void CreateHttpRequest(IHttp &Http, const char *pUrl)
	{
		auto pGet = std::make_shared<CHttpRequest>(pUrl);
		pGet->LogProgress(HTTPLOG::FAILURE);
		pGet->FailOnErrorStatus(false);
		pGet->Timeout(CTimeout{10000, 0, 500, 10});

		m_pHttpRequest = pGet;
		Http.Run(pGet);
	}

public:
	std::optional<bool> Update(CTranslateResponse &Out) override
	{
		if(m_aInitError[0] != '\0')
		{
			str_copy(Out.m_Text, m_aInitError);
			return false;
		}
		dbg_assert(m_pHttpRequest != nullptr, "m_pHttpRequest is nullptr");
		if(m_pHttpRequest->State() == EHttpState::RUNNING || m_pHttpRequest->State() == EHttpState::QUEUED)
			return std::nullopt;
		if(m_pHttpRequest->State() == EHttpState::ABORTED)
		{
			str_copy(Out.m_Text, "Aborted");
			return false;
		}
		if(m_pHttpRequest->State() != EHttpState::DONE)
		{
			str_copy(Out.m_Text, "Curl error, see console");
			return false;
		}
		if(m_pHttpRequest->StatusCode() != 200 && !ParseHttpError())
		{
			str_format(Out.m_Text, sizeof(Out.m_Text), "Got http code %d", m_pHttpRequest->StatusCode());
			return false;
		}
		return ParseResponse(Out);
	}
	~ITranslateBackendHttp() override
	{
		if(m_pHttpRequest)
			m_pHttpRequest->Abort();
	}
};

class CTranslateBackendLibretranslate : public ITranslateBackendHttp
{
private:
	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		if(!pObj)
		{
			str_copy(Out.m_Text, "Response is not JSON");
			return false;
		}

		if(pObj->type != json_object)
		{
			str_copy(Out.m_Text, "Response is not object");
			return false;
		}

		const json_value *pError = json_object_get(pObj, "error");
		if(pError != &json_value_none)
		{
			if(pError->type != json_string)
				str_copy(Out.m_Text, "Error is not string");
			else
				str_copy(Out.m_Text, pError->u.string.ptr);
			return false;
		}

		const json_value *pTranslatedText = json_object_get(pObj, "translatedText");
		if(pTranslatedText == &json_value_none)
		{
			str_copy(Out.m_Text, "No translatedText");
			return false;
		}
		if(pTranslatedText->type != json_string)
		{
			str_copy(Out.m_Text, "translatedText is not string");
			return false;
		}

		const json_value *pDetectedLanguage = json_object_get(pObj, "detectedLanguage");
		if(pDetectedLanguage == &json_value_none)
		{
			str_copy(Out.m_Text, "No detectedLanguage");
			return false;
		}
		if(pDetectedLanguage->type != json_object)
		{
			str_copy(Out.m_Text, "detectedLanguage is not object");
			return false;
		}

		const json_value *pConfidence = json_object_get(pDetectedLanguage, "confidence");
		if(pConfidence == &json_value_none || ((pConfidence->type == json_double && pConfidence->u.dbl == 0.0f) ||
							      (pConfidence->type == json_integer && pConfidence->u.integer == 0)))
		{
			str_copy(Out.m_Text, "Unknown language");
			return false;
		}

		const json_value *pLanguage = json_object_get(pDetectedLanguage, "language");
		if(pLanguage == &json_value_none)
		{
			str_copy(Out.m_Text, "No language");
			return false;
		}
		if(pLanguage->type != json_string)
		{
			str_copy(Out.m_Text, "language is not string");
			return false;
		}

		str_copy(Out.m_Text, pTranslatedText->u.string.ptr);
		str_copy(Out.m_Language, pLanguage->u.string.ptr);

		return true;
	}

protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pObj = m_pHttpRequest->ResultJson();
		bool Res = ParseResponseJson(pObj, Out);
		if(!Res)
		{
			// Log the raw response for debugging
			unsigned char *pResult = nullptr;
			size_t ResultLength = 0;
			m_pHttpRequest->Result(&pResult, &ResultLength);
			if(pResult && ResultLength > 0)
			{
				// Truncate if too long
				size_t LogLength = std::min(ResultLength, size_t(1024));
				log_debug("translate/libretranslate", "LibreTranslate response failed to parse. Raw response: %.*s", (int)LogLength, pResult);
			}
		}
		json_value_free(pObj);
		return Res;
	}
	bool ParseHttpError() const override { return true; }

public:
	const char *Name() const override
	{
		return "LibreTranslate";
	}
	CTranslateBackendLibretranslate(IHttp &Http, const char *pText, const char *pTarget)
	{
		CJsonStringWriter Json = CJsonStringWriter();
		Json.BeginObject();
		Json.WriteAttribute("q");
		Json.WriteStrValue(pText);
		Json.WriteAttribute("source");
		Json.WriteStrValue("auto");
		Json.WriteAttribute("target");
		Json.WriteStrValue(EncodeTarget(pTarget));
		Json.WriteAttribute("format");
		Json.WriteStrValue("text");
		if(g_Config.m_QmTranslateLibreKey[0] != '\0')
		{
			Json.WriteAttribute("api_key");
			Json.WriteStrValue(g_Config.m_QmTranslateLibreKey);
		}
		Json.EndObject();
		CreateHttpRequest(Http, g_Config.m_QmTranslateLibreEndpoint[0] == '\0' ? "localhost:5000/translate" : g_Config.m_QmTranslateLibreEndpoint);
		const char *pJson = Json.GetOutputString().c_str();
		m_pHttpRequest->PostJson(pJson);
	}
};

class CTranslateBackendTencentCloud : public ITranslateBackendHttp
{
private:
	static constexpr const char *CONTENT_TYPE = "application/json; charset=utf-8";

	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		if(!pObj)
		{
			str_copy(Out.m_Text, "Response is not JSON");
			return false;
		}
		if(pObj->type != json_object)
		{
			str_copy(Out.m_Text, "Response is not object");
			return false;
		}

		const json_value *pResponse = json_object_get(pObj, "Response");
		if(pResponse == &json_value_none || pResponse->type != json_object)
		{
			str_copy(Out.m_Text, "No Response object");
			return false;
		}

		const json_value *pError = json_object_get(pResponse, "Error");
		if(pError != &json_value_none)
		{
			const json_value *pCode = json_object_get(pError, "Code");
			const json_value *pMessage = json_object_get(pError, "Message");
			const char *pCodeStr = pCode != &json_value_none && pCode->type == json_string ? pCode->u.string.ptr : "UnknownError";
			const char *pMessageStr = pMessage != &json_value_none && pMessage->type == json_string ? pMessage->u.string.ptr : "TencentCloud request failed";
			str_format(Out.m_Text, sizeof(Out.m_Text), "%s: %s", pCodeStr, pMessageStr);
			return false;
		}

		const json_value *pTranslatedText = json_object_get(pResponse, "TargetText");
		if(pTranslatedText == &json_value_none)
		{
			str_copy(Out.m_Text, "No TargetText");
			return false;
		}
		if(pTranslatedText->type != json_string)
		{
			str_copy(Out.m_Text, "TargetText is not string");
			return false;
		}

		const json_value *pSource = json_object_get(pResponse, "Source");
		if(pSource != &json_value_none && pSource->type == json_string)
			str_copy(Out.m_Language, pSource->u.string.ptr);
		else
			Out.m_Language[0] = '\0';

		str_copy(Out.m_Text, pTranslatedText->u.string.ptr);
		return true;
	}

protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pObj = m_pHttpRequest->ResultJson();
		const bool Result = ParseResponseJson(pObj, Out);
		json_value_free(pObj);
		return Result;
	}

	bool ParseHttpError() const override
	{
		return true;
	}

public:
	const char *EncodeTarget(const char *pTarget) const override
	{
		if(!pTarget || pTarget[0] == '\0')
			return CConfig::ms_pQmTranslateTarget;
		if(str_comp_nocase(pTarget, "zh-cn") == 0)
			return "zh";
		if(str_comp_nocase(pTarget, "zh-tw") == 0)
			return "zh-TW";
		return pTarget;
	}

	const char *Name() const override
	{
		return "TencentCloud";
	}

	CTranslateBackendTencentCloud(IHttp &Http, const char *pText, const char *pTarget)
	{
		const char *pSecretId = GetTencentCloudSecretId();
		const char *pSecretKey = GetTencentCloudSecretKey();
		if(!pSecretId || !pSecretId[0] || !pSecretKey || !pSecretKey[0])
		{
			SetInitError("Missing TencentCloud credentials: configure SecretId/SecretKey or set TENCENTCLOUD_SECRET_ID/TENCENTCLOUD_SECRET_KEY");
			return;
		}

		const char *pEndpoint = g_Config.m_QmTranslateTcEndpoint[0] != '\0' ? g_Config.m_QmTranslateTcEndpoint : TENCENTCLOUD_TMT_DEFAULT_ENDPOINT;
		std::string Host;
		std::string Path;
		if(!ParseHttpsUrl(pEndpoint, Host, Path, m_aInitError, sizeof(m_aInitError)))
			return;
		const std::string RequestUrl = "https://" + Host + Path;

		CJsonStringWriter Json;
		Json.BeginObject();
		Json.WriteAttribute("ProjectId");
		Json.WriteIntValue(0);
		Json.WriteAttribute("Source");
		Json.WriteStrValue("auto");
		Json.WriteAttribute("SourceText");
		Json.WriteStrValue(pText);
		Json.WriteAttribute("Target");
		Json.WriteStrValue(EncodeTarget(pTarget));
		Json.EndObject();
		const std::string Payload = Json.GetOutputString();

		const int64_t Timestamp = time_timestamp();
		char aDate[32];
		if(!FormatUtcDate(Timestamp, aDate, sizeof(aDate)))
		{
			SetInitError("Failed to format TencentCloud request date");
			return;
		}

		const std::string CanonicalHeaders =
			std::string("content-type:") + CONTENT_TYPE + "\n"
			"host:" + Host + "\n";
		const std::string SignedHeaders = "content-type;host";
		const std::string CanonicalRequest =
			"POST\n" + Path + "\n\n" + CanonicalHeaders + "\n" + SignedHeaders + "\n" + Sha256Hex(Payload);
		const std::string CredentialScope = std::string(aDate) + "/" + TENCENTCLOUD_TMT_SERVICE + "/tc3_request";
		const std::string TimestampString = std::to_string(Timestamp);
		const std::string StringToSign =
			"TC3-HMAC-SHA256\n" + TimestampString + "\n" + CredentialScope + "\n" + Sha256Hex(CanonicalRequest);

		const std::string SecretPrefix = std::string("TC3") + pSecretKey;
		const std::string SecretDate = HmacSha256Raw(
			reinterpret_cast<const unsigned char *>(SecretPrefix.data()), SecretPrefix.size(), aDate);
		const std::string SecretService = HmacSha256Raw(
			reinterpret_cast<const unsigned char *>(SecretDate.data()), SecretDate.size(), TENCENTCLOUD_TMT_SERVICE);
		const std::string SecretSigning = HmacSha256Raw(
			reinterpret_cast<const unsigned char *>(SecretService.data()), SecretService.size(), "tc3_request");
		const std::string Signature = HmacSha256Hex(
			reinterpret_cast<const unsigned char *>(SecretSigning.data()), SecretSigning.size(), StringToSign);

		const std::string Authorization =
			"TC3-HMAC-SHA256 Credential=" + std::string(pSecretId) + "/" + CredentialScope +
			", SignedHeaders=" + SignedHeaders +
			", Signature=" + Signature;

		m_pHttpRequest = std::make_shared<CHttpRequest>(RequestUrl.c_str());
		m_pHttpRequest->LogProgress(HTTPLOG::FAILURE);
		m_pHttpRequest->FailOnErrorStatus(false);
		m_pHttpRequest->Timeout(CTimeout{10000, 0, 500, 10});
		m_pHttpRequest->HeaderString("Content-Type", CONTENT_TYPE);
		m_pHttpRequest->HeaderString("Authorization", Authorization.c_str());
		m_pHttpRequest->HeaderString("Host", Host.c_str());
		m_pHttpRequest->HeaderString("X-TC-Action", TENCENTCLOUD_TMT_ACTION);
		m_pHttpRequest->HeaderString("X-TC-Version", TENCENTCLOUD_TMT_VERSION);
		m_pHttpRequest->HeaderString("X-TC-Region", g_Config.m_QmTranslateTcRegion);
		m_pHttpRequest->HeaderString("X-TC-Timestamp", TimestampString.c_str());
		m_pHttpRequest->Post(reinterpret_cast<const unsigned char *>(Payload.data()), Payload.size());
		Http.Run(m_pHttpRequest);
	}
};

class CTranslateBackendFtapi : public ITranslateBackendHttp
{
private:
	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		if(!pObj)
		{
			str_copy(Out.m_Text, "Response is not JSON");
			return false;
		}

		if(pObj->type != json_object)
		{
			str_copy(Out.m_Text, "Response is not object");
			return false;
		}

		const json_value *pTranslatedText = json_object_get(pObj, "destination-text");
		if(pTranslatedText == &json_value_none)
		{
			str_copy(Out.m_Text, "No destination-text");
			return false;
		}
		if(pTranslatedText->type != json_string)
		{
			str_copy(Out.m_Text, "destination-text is not string");
			return false;
		}

		const json_value *pDetectedLanguage = json_object_get(pObj, "source-language");
		if(pDetectedLanguage == &json_value_none)
		{
			str_copy(Out.m_Text, "No source-language");
			return false;
		}
		if(pDetectedLanguage->type != json_string)
		{
			str_copy(Out.m_Text, "source-language is not string");
			return false;
		}

		str_copy(Out.m_Text, pTranslatedText->u.string.ptr);
		str_copy(Out.m_Language, pDetectedLanguage->u.string.ptr);

		return true;
	}

protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pObj = m_pHttpRequest->ResultJson();
		bool Res = ParseResponseJson(pObj, Out);
		json_value_free(pObj);
		return Res;
	}

public:
	const char *EncodeTarget(const char *pTarget) const override
	{
		if(!pTarget || pTarget[0] == '\0')
			return CConfig::ms_pQmTranslateTarget;
		if(str_comp_nocase(pTarget, "zh") == 0)
			return "zh-cn";
		return pTarget;
	}
	const char *Name() const override
	{
		return "FreeTranslateAPI";
	}
	CTranslateBackendFtapi(IHttp &Http, const char *pText, const char *pTarget)
	{
		char aBuf[4096];
		str_format(aBuf, sizeof(aBuf), "https://ftapi.pythonanywhere.com/translate?dl=%s&text=",
			EncodeTarget(pTarget));

		UrlEncode(pText, aBuf + strlen(aBuf), sizeof(aBuf) - strlen(aBuf));

		CreateHttpRequest(Http, aBuf);
	}
};

class CTranslateBackendLlm : public ITranslateBackendHttp
{
private:
	ELlmProvider m_Provider;

	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		if(!pObj)
		{
			str_copy(Out.m_Text, "Response is not JSON");
			return false;
		}

		if(pObj->type != json_object)
		{
			str_copy(Out.m_Text, "Response is not object");
			return false;
		}

		const json_value *pError = json_object_get(pObj, "error");
		if(pError != &json_value_none)
		{
			const json_value *pMessage = json_object_get(pError, "message");
			const char *pMessageStr = pMessage != &json_value_none && pMessage->type == json_string ? pMessage->u.string.ptr : "LLM API request failed";
			str_copy(Out.m_Text, pMessageStr);
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

			str_copy(Out.m_Text, aErrorMsg);
			return false;
		}
		if(pChoices->type != json_array)
		{
			str_copy(Out.m_Text, "choices is not array");
			return false;
		}
		if(pChoices->u.array.length == 0)
		{
			str_copy(Out.m_Text, "choices is empty");
			return false;
		}

		const json_value *pChoice = pChoices->u.array.values[0];
		if(pChoice->type != json_object)
		{
			str_copy(Out.m_Text, "choice is not object");
			return false;
		}

		const json_value *pMessage = json_object_get(pChoice, "message");
		if(pMessage == &json_value_none)
		{
			str_copy(Out.m_Text, "No message in choice");
			return false;
		}
		if(pMessage->type != json_object)
		{
			str_copy(Out.m_Text, "message is not object");
			return false;
		}

		const json_value *pContent = json_object_get(pMessage, "content");
		if(pContent == &json_value_none)
		{
			str_copy(Out.m_Text, "No content in message");
			return false;
		}
		if(pContent->type != json_string)
		{
			str_copy(Out.m_Text, "content is not string");
			return false;
		}

		str_copy(Out.m_Text, pContent->u.string.ptr);
		Out.m_Language[0] = '\0'; // LLM doesn't return detected language

		return true;
	}

protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		// 检查 HTTP 请求状态
		EHttpState State = m_pHttpRequest->State();
		if(State == EHttpState::ERROR)
		{
			str_copy(Out.m_Text, "HTTP request failed (network error, timeout, or connection refused)");
			return false;
		}
		if(State == EHttpState::ABORTED)
		{
			str_copy(Out.m_Text, "HTTP request was aborted");
			return false;
		}

		int StatusCode = m_pHttpRequest->StatusCode();
		if(StatusCode >= 400)
		{
			str_format(Out.m_Text, sizeof(Out.m_Text), "HTTP error %d", StatusCode);
			// 尝试获取响应体中的错误信息
			json_value *pObj = m_pHttpRequest->ResultJson();
			if(pObj)
			{
				const json_value *pError = json_object_get(pObj, "error");
				if(pError != &json_value_none && pError->type == json_object)
				{
					const json_value *pMessage = json_object_get(pError, "message");
					if(pMessage != &json_value_none && pMessage->type == json_string)
					{
						str_format(Out.m_Text, sizeof(Out.m_Text), "HTTP %d: %.200s", StatusCode, pMessage->u.string.ptr);
					}
				}
				else
				{
					// 智谱AI格式: code/msg
					const json_value *pMsg = json_object_get(pObj, "msg");
					if(pMsg != &json_value_none && pMsg->type == json_string)
					{
						str_format(Out.m_Text, sizeof(Out.m_Text), "HTTP %d: %.200s", StatusCode, pMsg->u.string.ptr);
					}
				}
				json_value_free(pObj);
			}
			return false;
		}

		json_value *pObj = m_pHttpRequest->ResultJson();

		// 如果 JSON 解析失败，尝试获取原始响应内容
		if(!pObj)
		{
			// 获取原始响应内容
			unsigned char *pResult = nullptr;
			size_t ResultLength = 0;
			m_pHttpRequest->Result(&pResult, &ResultLength);

			if(pResult && ResultLength > 0)
			{
				// 截断并格式化原始响应内容用于错误显示
				char aRawResponse[256];
				size_t CopyLen = std::min(ResultLength, sizeof(aRawResponse) - 1);
				mem_copy(aRawResponse, pResult, CopyLen);
				aRawResponse[CopyLen] = '\0';

				// 去除换行符，避免影响错误信息显示
				for(char *p = aRawResponse; *p; ++p)
				{
					if(*p == '\n' || *p == '\r')
						*p = ' ';
				}

				str_format(Out.m_Text, sizeof(Out.m_Text),
					"JSON parse error: %.200s%s",
					aRawResponse,
					ResultLength > 200 ? "... (truncated)" : "");
			}
			else
			{
				str_copy(Out.m_Text, "Empty response from LLM API");
			}
			return false;
		}

		bool Res = ParseResponseJson(pObj, Out);
		json_value_free(pObj);
		return Res;
	}

	bool ParseHttpError() const override
	{
		return true;
	}

public:
	const char *Name() const override
	{
		switch(m_Provider)
		{
			case ELlmProvider::ZHIPU_AI:
				return "ZhipuAI";
			case ELlmProvider::DEEPSEEK:
				return "DeepSeek";
			case ELlmProvider::OPENAI:
				return "OpenAI";
			case ELlmProvider::CUSTOM:
			default:
				return "LLM";
		}
	}

	CTranslateBackendLlm(IHttp &Http, const char *pText, const char *pTarget)
	{
		// 获取当前选择的 Provider（确保值在有效范围内）
		constexpr int PROVIDER_MIN = static_cast<int>(ELlmProvider::ZHIPU_AI);
		constexpr int PROVIDER_MAX = static_cast<int>(ELlmProvider::CUSTOM);
		int ProviderValue = std::clamp(g_Config.m_QmTranslateLlmProvider, PROVIDER_MIN, PROVIDER_MAX);
		m_Provider = static_cast<ELlmProvider>(ProviderValue);

		// 获取对应 Provider 的 API Key
		const char *pApiKey = GetLlmApiKey(m_Provider);
		if(pApiKey[0] == '\0')
		{
			SetInitError("Missing API Key: configure the API key for the selected provider in settings");
			return;
		}

		// 获取对应 Provider 的端点（已配置则使用配置，否则使用默认）
		const char *pEndpoint = GetLlmEndpoint(m_Provider);
		if(pEndpoint[0] == '\0')
		{
			SetInitError("Missing Endpoint: configure the endpoint for the selected provider in settings");
			return;
		}

		// Build system message with target language
		char aSystemMessage[1024];
		if(g_Config.m_QmTranslateSystemPrompt[0] != '\0')
		{
			str_copy(aSystemMessage, g_Config.m_QmTranslateSystemPrompt, sizeof(aSystemMessage));
		}
		else
		{
			str_format(aSystemMessage, sizeof(aSystemMessage), DEFAULT_TRANSLATE_PROMPT, EncodeTarget(pTarget));
		}

		// Build JSON request body manually (CJsonStringWriter doesn't support float values)
		// Need to escape the text and system message for JSON
		// Note: EscapeJsonString adds quotes around the output, so we don't add them in the template
		char aEscapedText[4096];
		char aEscapedSystem[1024];
		EscapeJsonString(pText, aEscapedText, sizeof(aEscapedText));
		EscapeJsonString(aSystemMessage, aEscapedSystem, sizeof(aEscapedSystem));

		// 获取对应 Provider 的模型
		const char *pModel = GetLlmModel(m_Provider);

		// Escape model name for JSON
		char aEscapedModel[128];
		EscapeJsonString(pModel, aEscapedModel, sizeof(aEscapedModel));

		char aPayload[8192];
		// 根据配置决定是否启用思考模式
		// 注意：思考模式会增加响应时间，默认关闭
		if(g_Config.m_QmTranslateLlmEnableThinking)
		{
			// 启用思考模式
			if(m_Provider == ELlmProvider::ZHIPU_AI)
			{
				str_format(aPayload, sizeof(aPayload),
					"{"
					"\"model\":%s,"
					"\"messages\":["
					"{\"role\":\"system\",\"content\":%s},"
					"{\"role\":\"user\",\"content\":%s}"
					"],"
					"\"temperature\":0.3,"
					"\"max_tokens\":1024,"
					"\"thinking\":{\"type\":\"enabled\"}"
					"}",
					aEscapedModel, aEscapedSystem, aEscapedText);
			}
			else if(m_Provider == ELlmProvider::DEEPSEEK)
			{
				str_format(aPayload, sizeof(aPayload),
					"{"
					"\"model\":%s,"
					"\"messages\":["
					"{\"role\":\"system\",\"content\":%s},"
					"{\"role\":\"user\",\"content\":%s}"
					"],"
					"\"thinking\":{\"type\":\"enabled\"}"
					"}",
					aEscapedModel, aEscapedSystem, aEscapedText);
			}
			else if(m_Provider == ELlmProvider::OPENAI)
			{
				// OpenAI: 不支持 thinking 参数，依赖用户选择推理模型
				str_format(aPayload, sizeof(aPayload),
					"{"
					"\"model\":%s,"
					"\"messages\":["
					"{\"role\":\"system\",\"content\":%s},"
					"{\"role\":\"user\",\"content\":%s}"
					"],"
					"\"temperature\":0.3,"
					"\"max_tokens\":1024"
					"}",
					aEscapedModel, aEscapedSystem, aEscapedText);
			}
			else // CUSTOM
			{
				// 自定义: 使用 OpenAI 兼容格式
				str_format(aPayload, sizeof(aPayload),
					"{"
					"\"model\":%s,"
					"\"messages\":["
					"{\"role\":\"system\",\"content\":%s},"
					"{\"role\":\"user\",\"content\":%s}"
					"],"
					"\"thinking\":{\"type\":\"enabled\"}"
					"}",
					aEscapedModel, aEscapedSystem, aEscapedText);
			}
		}
		else
		{
			// 关闭思考模式
			// 智谱AI使用 thinking:disabled（GLM-4.7+ 默认开启）
			// DeepSeek 和其他平台不需要显式关闭
			if(m_Provider == ELlmProvider::ZHIPU_AI)
			{
				str_format(aPayload, sizeof(aPayload),
					"{"
					"\"model\":%s,"
					"\"messages\":["
					"{\"role\":\"system\",\"content\":%s},"
					"{\"role\":\"user\",\"content\":%s}"
					"],"
					"\"temperature\":0.3,"
					"\"max_tokens\":1024,"
					"\"thinking\":{\"type\":\"disabled\"}"
					"}",
					aEscapedModel, aEscapedSystem, aEscapedText);
			}
			else if(m_Provider == ELlmProvider::DEEPSEEK)
			{
				str_format(aPayload, sizeof(aPayload),
					"{"
					"\"model\":%s,"
					"\"messages\":["
					"{\"role\":\"system\",\"content\":%s},"
					"{\"role\":\"user\",\"content\":%s}"
					"],"
					"\"temperature\":0.3,"
					"\"max_tokens\":1024"
					"}",
					aEscapedModel, aEscapedSystem, aEscapedText);
			}
			else
			{
				// 其他 Provider 使用标准格式
				str_format(aPayload, sizeof(aPayload),
					"{"
					"\"model\":%s,"
					"\"messages\":["
					"{\"role\":\"system\",\"content\":%s},"
					"{\"role\":\"user\",\"content\":%s}"
					"],"
					"\"temperature\":0.3,"
					"\"max_tokens\":1024"
					"}",
					aEscapedModel, aEscapedSystem, aEscapedText);
			}
		}

		// Build Authorization header
		char aAuthorization[512];
		str_format(aAuthorization, sizeof(aAuthorization), "Bearer %s", pApiKey);

		m_pHttpRequest = std::make_shared<CHttpRequest>(pEndpoint);
		m_pHttpRequest->LogProgress(HTTPLOG::FAILURE);
		m_pHttpRequest->FailOnErrorStatus(false);
		// LLM API 响应可能较慢（特别是智谱AI），增加超时时间到30秒
		// 降低最低速度要求避免 "Operation too slow" 错误
		m_pHttpRequest->Timeout(CTimeout{30000, 0, 100, 30});
		m_pHttpRequest->HeaderString("Content-Type", "application/json");
		m_pHttpRequest->HeaderString("Authorization", aAuthorization);
		m_pHttpRequest->Post(reinterpret_cast<const unsigned char *>(aPayload), str_length(aPayload));
		Http.Run(m_pHttpRequest);
	}
};

static std::unique_ptr<ITranslateBackend> CreateTranslateBackend(IHttp &Http, const char *pText, const char *pTarget)
{
	if(str_comp_nocase(g_Config.m_QmTranslateBackend, "libretranslate") == 0)
		return std::make_unique<CTranslateBackendLibretranslate>(Http, pText, pTarget);
	if(str_comp_nocase(g_Config.m_QmTranslateBackend, "ftapi") == 0)
		return std::make_unique<CTranslateBackendFtapi>(Http, pText, pTarget);
	if(str_comp_nocase(g_Config.m_QmTranslateBackend, "tencentcloud") == 0)
		return std::make_unique<CTranslateBackendTencentCloud>(Http, pText, pTarget);
	if(str_comp_nocase(g_Config.m_QmTranslateBackend, "llm") == 0)
		return std::make_unique<CTranslateBackendLlm>(Http, pText, pTarget);
	return nullptr;
}

void CTranslate::ConTranslate(IConsole::IResult *pResult, void *pUserData)
{
	const char *pName;
	if(pResult->NumArguments() == 0)
		pName = nullptr;
	else
		pName = pResult->GetString(0);

	CTranslate *pThis = static_cast<CTranslate *>(pUserData);
	pThis->Translate(pName);
}

void CTranslate::ConTranslateId(IConsole::IResult *pResult, void *pUserData)
{
	CTranslate *pThis = static_cast<CTranslate *>(pUserData);
	pThis->Translate(pResult->GetInteger(0));
}

void CTranslate::OnConsoleInit()
{
	Console()->Register("translate", "?r[name]", CFGFLAG_CLIENT, ConTranslate, this, "Translate last message (of a given name)");
	Console()->Register("translate_id", "v[id]", CFGFLAG_CLIENT, ConTranslateId, this, "Translate last message of the person with this id");
}

void CTranslate::OnReset()
{
	m_vJobs.clear();
	m_vOutgoingJobs.clear();
}

void CTranslate::OnShutdown()
{
	m_vJobs.clear();
	m_vOutgoingJobs.clear();
}

void CTranslate::Translate(int Id, bool ShowProgress)
{
	if(Id < 0 || Id >= (int)std::size(GameClient()->m_aClients))
	{
		GameClient()->m_Chat.Echo(Localize("Not a valid ID"));
		return;
	}
	const auto &Player = GameClient()->m_aClients[Id];
	if(!Player.m_Active)
	{
		GameClient()->m_Chat.Echo(Localize("ID not connected"));
		return;
	}
	Translate(Player.m_aName, ShowProgress);
}

void CTranslate::Translate(const char *pName, bool ShowProgress)
{
	CChat::CLine *pLineBest = nullptr;
	if(GameClient()->m_Chat.m_CurrentLine > 0)
	{
		int ScoreBest = -1;
		for(int i = 0; i < CChat::MAX_LINES; i++)
		{
			CChat::CLine *pLine = &GameClient()->m_Chat.m_aLines[((GameClient()->m_Chat.m_CurrentLine - i) + CChat::MAX_LINES) % CChat::MAX_LINES];
			if(pLine->m_pTranslateResponse != nullptr)
				continue;
			if(pLine->m_ClientId == CChat::CLIENT_MSG)
				continue;
			for(int Id : GameClient()->m_aLocalIds)
				if(pLine->m_ClientId == Id)
					continue;
			int Score = 0;
			if(pName)
			{
				if(pLine->m_ClientId == CChat::SERVER_MSG)
					continue;
				if(str_comp(pLine->m_aName, pName) == 0)
					Score = 2;
				else if(str_comp_nocase(pLine->m_aName, pName) == 0)
					Score = 1;
				else
					continue;
			}
			if(Score > ScoreBest)
			{
				ScoreBest = Score;
				pLineBest = pLine;
			}
		}
	}
	if(!pLineBest || pLineBest->m_aText[0] == '\0')
	{
		GameClient()->m_Chat.Echo(Localize("No message to translate"));
		return;
	}

	Translate(*pLineBest, ShowProgress);
}

void CTranslate::Translate(CChat::CLine &Line, bool ShowProgress, bool AutoTriggered)
{
	if(m_vJobs.size() >= static_cast<size_t>(GetMaxConcurrency()))
	{
		return;
	}
	if(Line.m_ClientId == CChat::SERVER_MSG)
	{
		if(ShowProgress)
			GameClient()->m_Chat.Echo(Localize("Server messages are not translated"));
		return;
	}

	CTranslateJob Job;
	Job.m_LineIndex = GameClient()->m_Chat.GetLineIndex(&Line);
	Job.m_TranslationId = Line.m_TranslationId;
	Job.m_pTranslateResponse = std::make_shared<CTranslateResponse>();
	Job.m_AutoTriggered = AutoTriggered;
	Line.m_pTranslateResponse = Job.m_pTranslateResponse;
	const char *pTarget = GetEffectiveTranslateTarget(g_Config.m_QmTranslateTarget);
	if(!IsValidLanguageCode(pTarget))
	{
		// 使用默认语言代码
		pTarget = "en";
	}
	str_copy(Job.m_aTarget, pTarget, sizeof(Job.m_aTarget));
	Job.m_pBackend = CreateTranslateBackend(*Http(), Line.m_aText, Job.m_aTarget);
	if(!Job.m_pBackend)
	{
		GameClient()->m_Chat.Echo(Localize("Invalid translate backend"));
		return;
	}

	if(ShowProgress)
	{
		str_format(Job.m_pTranslateResponse->m_Text, sizeof(Job.m_pTranslateResponse->m_Text), Localize("%s translating to %s"), Job.m_pBackend->Name(), Job.m_aTarget);
		Line.m_Time = time();
	}
	else
	{
		Job.m_pTranslateResponse->m_Text[0] = '\0';
	}

	m_vJobs.emplace_back(std::move(Job));

	if(ShowProgress)
		GameClient()->m_Chat.RebuildChat();
}

bool CTranslate::TryTranslateOutgoingChat(int Team, const char *pText)
{
	std::string Text;
	std::string Target;
	if(!ParseOutgoingTranslateTarget(pText, Text, Target))
		return false;

	if(m_vJobs.size() + m_vOutgoingJobs.size() >= static_cast<size_t>(GetMaxConcurrency()))
	{
		GameClient()->m_Chat.Echo(Localize("Too many translation jobs"));
		return true;
	}

	COutgoingTranslateJob Job;
	Job.m_Team = Team;
	str_copy(Job.m_aTarget, Target.c_str(), sizeof(Job.m_aTarget));
	Job.m_pBackend = CreateTranslateBackend(*Http(), Text.c_str(), Job.m_aTarget);
	if(!Job.m_pBackend)
	{
		GameClient()->m_Chat.Echo(Localize("Invalid translate backend"));
		return true;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), Localize("%s translating to %s before send"), Job.m_pBackend->Name(), Job.m_aTarget);
	GameClient()->m_Chat.Echo(aBuf);
	m_vOutgoingJobs.emplace_back(std::move(Job));
	return true;
}

void CTranslate::OnRender()
{
	const auto Time = time();
	// 检查翻译响应是否仍然属于同一行
	// 使用行索引和翻译ID来安全地检测行重用：
	// - GetLineByIndex 通过索引安全获取行指针
	// - m_TranslationId 检测行内容是否已变更
	// - 如果 ID 不匹配，说明该行已被重用，翻译结果应该丢弃
	auto ForEach = [&](CTranslateJob &Job) {
		CChat::CLine *pLine = GameClient()->m_Chat.GetLineByIndex(Job.m_LineIndex);
		if(pLine == nullptr)
			return true; // 无效索引，丢弃任务

		// 检查翻译ID是否匹配，检测行是否被重用
		if(pLine->m_TranslationId != Job.m_TranslationId)
			return true; // 行已被重用，丢弃翻译结果

		// 检查响应指针是否仍然匹配（额外保护）
		if(pLine->m_pTranslateResponse != Job.m_pTranslateResponse)
			return true; // 响应已被替换，丢弃任务

		const std::optional<bool> Done = Job.m_pBackend->Update(*Job.m_pTranslateResponse);
		if(!Done.has_value())
			return false; // Keep ongoing tasks
		if(*Done)
		{
			if(Job.m_AutoTriggered && IsChineseLanguage(Job.m_pTranslateResponse->m_Language))
				Job.m_pTranslateResponse->m_Text[0] = '\0';
			else if(str_comp_nocase(pLine->m_aText, Job.m_pTranslateResponse->m_Text) == 0) // Check for no translation difference
				Job.m_pTranslateResponse->m_Text[0] = '\0';
		}
		else
		{
			char aBuf[sizeof(Job.m_pTranslateResponse->m_Text)];
			str_format(aBuf, sizeof(aBuf), Localize("%s to %s failed: %s"), Job.m_pBackend->Name(), Job.m_aTarget, Job.m_pTranslateResponse->m_Text);
			Job.m_pTranslateResponse->m_Error = true;
			str_copy(Job.m_pTranslateResponse->m_Text, aBuf);
		}
		pLine->m_Time = Time;
		GameClient()->m_Chat.RebuildChat();
		return true;
	};
	m_vJobs.erase(std::remove_if(m_vJobs.begin(), m_vJobs.end(), ForEach), m_vJobs.end());

	auto ForEachOutgoing = [&](COutgoingTranslateJob &Job) {
		const std::optional<bool> Done = Job.m_pBackend->Update(Job.m_Response);
		if(!Done.has_value())
			return false;
		if(*Done)
		{
			if(Job.m_Response.m_Text[0] == '\0')
			{
				char aBuf[sizeof(Job.m_Response.m_Text)];
				str_format(aBuf, sizeof(aBuf), "%s to %s failed: Empty translation result", Job.m_pBackend->Name(), Job.m_aTarget);
				GameClient()->m_Chat.Echo(aBuf);
			}
			else
			{
				GameClient()->m_Chat.SendChatQueued(Job.m_Team, Job.m_Response.m_Text, false);
			}
		}
		else
		{
			char aBuf[sizeof(Job.m_Response.m_Text)];
			str_format(aBuf, sizeof(aBuf), "%s to %s failed: %s", Job.m_pBackend->Name(), Job.m_aTarget, Job.m_Response.m_Text);
			GameClient()->m_Chat.Echo(aBuf);
		}
		return true;
	};
	m_vOutgoingJobs.erase(std::remove_if(m_vOutgoingJobs.begin(), m_vOutgoingJobs.end(), ForEachOutgoing), m_vOutgoingJobs.end());
}

void CTranslate::AutoTranslate(CChat::CLine &Line)
{
	if(!g_Config.m_QmTranslateAuto)
	{
		dbg_msg("translate", "AutoTranslate skipped: m_QmTranslateAuto=%d", g_Config.m_QmTranslateAuto);
		return;
	}
	if(Line.m_ClientId == CChat::CLIENT_MSG)
	{
		dbg_msg("translate", "AutoTranslate skipped: CLIENT_MSG");
		return;
	}
	if(Line.m_ClientId == CChat::SERVER_MSG)
	{
		dbg_msg("translate", "AutoTranslate skipped: SERVER_MSG");
		return;
	}
	for(const int Id : GameClient()->m_aLocalIds)
	{
		if(Id >= 0 && Id == Line.m_ClientId)
		{
			dbg_msg("translate", "AutoTranslate skipped: local player msg (Id=%d)", Id);
			return;
		}
	}
	if(str_comp(g_Config.m_QmTranslateBackend, "ftapi") == 0)
	{
		// FTAPI 过载保护：默认禁用自动翻译，防止服务过载
		if(!g_Config.m_QmTranslateFtapiAutoEnable)
		{
			static bool s_Warned = false;
			if(!s_Warned)
			{
				GameClient()->m_Chat.Echo(Localize("FTAPI auto-translate is disabled to prevent overload. Enable in settings if needed."));
				s_Warned = true;
			}
			return;
		}
	}
	dbg_msg("translate", "AutoTranslate triggered for: '%.50s...' (ClientId=%d, Backend=%s)",
		Line.m_aText, Line.m_ClientId, g_Config.m_QmTranslateBackend);
	Translate(Line, false, true);
}

bool CTranslate::ContainsChinese(const char *pText)
{
	if(!pText)
		return false;

	const char *p = pText;
	while(*p)
	{
		const int codepoint = str_utf8_decode(&p);
		// CJK Unified Ideographs: 0x4E00-0x9FFF
		// CJK Unified Ideographs Extension A: 0x3400-0x4DBF
		if((codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||
		   (codepoint >= 0x3400 && codepoint <= 0x4DBF))
			return true;
	}
	return false;
}

bool CTranslate::ShouldAutoTranslateOutgoing(const char *pText) const
{
	if(!g_Config.m_QmTranslateAutoOutgoing)
		return false;

	if(!pText || pText[0] == '\0' || pText[0] == '/')
		return false;

	// 模式 0: 仅中文输入时触发
	if(g_Config.m_QmTranslateAutoOutgoingMode == 0)
		return ContainsChinese(pText);

	// 模式 1: 始终翻译
	return true;
}

void CTranslate::StartAutoOutgoingTranslate(int Team, const char *pText)
{
	if(m_vJobs.size() + m_vOutgoingJobs.size() >= static_cast<size_t>(GetMaxConcurrency()))
	{
		GameClient()->m_Chat.Echo(Localize("Translation queue full, sending original text"));
		GameClient()->m_Chat.SendChatQueued(Team, pText, false);
		return;
	}

	COutgoingTranslateJob Job;
	Job.m_Team = Team;
	const char *pTarget = GetEffectiveTranslateTarget(g_Config.m_QmTranslateOutgoingTarget);
	if(!IsValidLanguageCode(pTarget))
	{
		pTarget = "en";
	}
	str_copy(Job.m_aTarget, pTarget, sizeof(Job.m_aTarget));
	Job.m_pBackend = CreateTranslateBackend(*Http(), pText, Job.m_aTarget);

	if(!Job.m_pBackend)
	{
		GameClient()->m_Chat.Echo(Localize("Invalid translate backend, sending original text"));
		GameClient()->m_Chat.SendChatQueued(Team, pText, false);
		return;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), Localize("Translating to %s..."), Job.m_aTarget);
	GameClient()->m_Chat.Echo(aBuf);
	m_vOutgoingJobs.emplace_back(std::move(Job));
}

int CTranslate::GetEffectiveConcurrency() const
{
	// 如果用户手动设置（不等于默认值 1），使用用户值
	if(g_Config.m_QmTranslateLlmConcurrency != 1)
		return g_Config.m_QmTranslateLlmConcurrency;

	// 根据后端类型提供智能默认值
	if(str_comp_nocase(g_Config.m_QmTranslateBackend, "llm") == 0)
	{
		// LLM 后端：根据 Provider 类型提供不同默认值
		constexpr int PROVIDER_MIN = static_cast<int>(ELlmProvider::ZHIPU_AI);
		constexpr int PROVIDER_MAX = static_cast<int>(ELlmProvider::CUSTOM);
		int ProviderValue = std::clamp(g_Config.m_QmTranslateLlmProvider, PROVIDER_MIN, PROVIDER_MAX);
		ELlmProvider Provider = static_cast<ELlmProvider>(ProviderValue);

		switch(Provider)
		{
		case ELlmProvider::ZHIPU_AI:
		case ELlmProvider::DEEPSEEK:
			return 3; // ZhipuAI 和 DeepSeek 默认 3
		case ELlmProvider::OPENAI:
			return 2; // OpenAI 默认 2（成本考虑）
		case ELlmProvider::CUSTOM:
		default:
			return g_Config.m_QmTranslateLlmConcurrencyDefault; // 自定义使用配置默认值
		}
	}
	else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "tencentcloud") == 0)
	{
		return 5; // TencentCloud 默认 5
	}
	else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "libretranslate") == 0)
	{
		return 2; // LibreTranslate 默认 2
	}
	else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "ftapi") == 0)
	{
		return 1; // FTAPI 默认 1（防止过载）
	}

	// 未知后端默认 3
	return 3;
}

int CTranslate::GetMaxConcurrency() const
{
	return GetEffectiveConcurrency();
}
