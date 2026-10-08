#include "translate_backend.h"

#include "translate_detect.h"
#include "translate_parse.h"

#include <base/hash.h>
#include <base/log.h>
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <string>
#include <utility>

const char *NormalizeTranslateSource(const char *pSource)
{
	if(!pSource || pSource[0] == '\0')
		return "auto";
	if(str_comp_nocase(pSource, "auto") == 0)
		return "auto";
	return pSource;
}

namespace
{
	ETranslateNotice HttpErrorNotice(int StatusCode)
	{
		if(StatusCode == 401 || StatusCode == 403)
			return ETranslateNotice::AUTHENTICATION;
		if(StatusCode == 429)
			return ETranslateNotice::RATE_LIMIT;
		if(StatusCode >= 500 && StatusCode <= 599)
			return ETranslateNotice::SERVICE_UNAVAILABLE;
		return ETranslateNotice::NONE;
	}

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
		"- grenade = 榴弹炮\n"
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

	using qm_translate::IsChineseLanguage;
	using qm_translate::IsChineseVariantLanguage;

	bool HasExplicitTranslateSource(const char *pSource)
	{
		return str_comp_nocase(NormalizeTranslateSource(pSource), "auto") != 0;
	}

	using SLocalLanguageStats = qm_translate::SLanguageStats;
	using qm_translate::AnalyzeLanguage;
	SLocalLanguageStats AnalyzeLocalLanguageStats(const char *pText) { return AnalyzeLanguage(pText); }

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

	std::string QuoteJsonString(const char *pText)
	{
		CJsonStringWriter Writer;
		Writer.WriteStrValue(pText);
		return Writer.GetOutputString();
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
		return DefaultConfig::QmTranslateTarget;
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

// NOLINTNEXTLINE(misc-use-internal-linkage)
class ITranslateBackendHttp : public ITranslateBackend
{
protected:
	FTranslateRequestFactory m_pCreateRequest;
	explicit ITranslateBackendHttp(FTranslateRequestFactory pCreateRequest) : m_pCreateRequest(pCreateRequest) {}
	std::shared_ptr<IHttpRequest> m_pHttpRequest = nullptr;
	char m_aInitError[256] = "";
	virtual bool ParseResponse(CTranslateResponse &Out) = 0;
	virtual bool ParseHttpError() const { return false; }
	void SetInitError(const char *pError)
	{
		str_copy(m_aInitError, pError, sizeof(m_aInitError));
	}

	void PrepareHttpRequest(const char *pUrl)
	{
		std::shared_ptr<IHttpRequest> pGet = m_pCreateRequest(pUrl);
		pGet->LogProgress(HTTPLOG::FAILURE);
		pGet->FailOnErrorStatus(false);
		pGet->Timeout(CTimeout{10000, 30000, 500, 10});

		m_pHttpRequest = pGet;
	}

public:
	std::optional<bool> Update(CTranslateResponse &Out) override
	{
		Out.m_Notice = ETranslateNotice::NONE;
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
			Out.m_Notice = ETranslateNotice::NETWORK_ERROR;
			str_copy(Out.m_Text, "Curl error, see console");
			return false;
		}
		Out.m_Notice = HttpErrorNotice(m_pHttpRequest->StatusCode());
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

// NOLINTNEXTLINE(misc-use-internal-linkage)
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
	CTranslateBackendLibretranslate(IHttp &Http, const char *pText, const char *pTarget, const char *pSource, FTranslateRequestFactory pCreateRequest) : ITranslateBackendHttp(pCreateRequest)
	{
		CJsonStringWriter Json = CJsonStringWriter();
		Json.BeginObject();
		Json.WriteAttribute("q");
		Json.WriteStrValue(pText);
		Json.WriteAttribute("source");
		Json.WriteStrValue(NormalizeTranslateSource(pSource));
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
		PrepareHttpRequest(g_Config.m_QmTranslateLibreEndpoint[0] == '\0' ? "http://localhost:5000/translate" : g_Config.m_QmTranslateLibreEndpoint);
		const std::string Payload = Json.GetOutputString();
		m_pHttpRequest->PostJson(Payload.c_str());
		Http.Run(m_pHttpRequest);
	}
};

// NOLINTNEXTLINE(misc-use-internal-linkage)
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
			return DefaultConfig::QmTranslateTarget;
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

	CTranslateBackendTencentCloud(IHttp &Http, const char *pText, const char *pTarget, const char *pSource, FTranslateRequestFactory pCreateRequest) : ITranslateBackendHttp(pCreateRequest)
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
		Json.WriteStrValue(NormalizeTranslateSource(pSource));
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
								      "host:" +
			Host + "\n";
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

		m_pHttpRequest = m_pCreateRequest(RequestUrl.c_str());
		m_pHttpRequest->LogProgress(HTTPLOG::FAILURE);
		m_pHttpRequest->FailOnErrorStatus(false);
		m_pHttpRequest->Timeout(CTimeout{10000, 30000, 500, 10});
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

// NOLINTNEXTLINE(misc-use-internal-linkage)
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
			return DefaultConfig::QmTranslateTarget;
		if(str_comp_nocase(pTarget, "zh") == 0)
			return "zh-cn";
		return pTarget;
	}
	const char *Name() const override
	{
		return "FreeTranslateAPI";
	}
	CTranslateBackendFtapi(IHttp &Http, const char *pText, const char *pTarget, FTranslateRequestFactory pCreateRequest) : ITranslateBackendHttp(pCreateRequest)
	{
		char aBuf[4096];
		str_format(aBuf, sizeof(aBuf), "https://ftapi.pythonanywhere.com/translate?dl=%s&text=",
			EncodeTarget(pTarget));

		UrlEncode(pText, aBuf + strlen(aBuf), sizeof(aBuf) - strlen(aBuf));

		PrepareHttpRequest(aBuf);
		Http.Run(m_pHttpRequest);
	}
};

// NOLINTNEXTLINE(misc-use-internal-linkage)
class CTranslateBackendMymemory : public ITranslateBackendHttp
{
private:
	// MyMemory 单次查询上限 500 字节
	static constexpr size_t MAX_QUERY_BYTES = 500;

	// 保留源文本，避免把用户主动翻译的已知样板识别为服务提示。
	std::string m_QueryText;

	// 只识别已知 KDE 翻译指导样板，普通链接或“如何翻译”等措辞不足以认定拒绝。
	bool ResultLooksLikeServiceNotice(const char *pResultText) const
	{
		if(!pResultText || pResultText[0] == '\0')
			return false;
		if(str_find_nocase(m_QueryText.c_str(), "You are about to translate") || str_find_nocase(m_QueryText.c_str(), "kturtle/translator.php"))
			return false;
		return str_startswith_nocase(pResultText, "You are about to translate the ") &&
		       str_find_nocase(pResultText, "on how to translate it") &&
		       str_find_nocase(pResultText, "kturtle/translator.php");
	}

	// MyMemory 使用 RFC3066 语言码，简体中文需写作 zh-CN
	static const char *EncodeLangCode(const char *pCode)
	{
		if(!pCode || pCode[0] == '\0')
			return "en";
		if(str_comp_nocase(pCode, "zh") == 0 || str_comp_nocase(pCode, "zh-cn") == 0)
			return "zh-CN";
		if(str_comp_nocase(pCode, "zh-tw") == 0)
			return "zh-TW";
		return pCode;
	}

	// MyMemory 不支持源语言自动检测：优先用用户显式配置的来源语言，
	// 否则按文字系统猜测（拉丁字母默认按英文处理）
	const char *ResolveSource(const char *pSource, const char *pText) const
	{
		if(HasExplicitTranslateSource(pSource))
			return EncodeLangCode(NormalizeTranslateSource(pSource));

		const SLocalLanguageStats Stats = AnalyzeLocalLanguageStats(pText);
		if(Stats.m_Kana > 0)
			return "ja";
		if(Stats.m_Hangul > 0)
			return "ko";
		if(Stats.m_Cyrillic > 0)
			return "ru";
		if(Stats.m_Han > 0)
			return "zh-CN";
		return "en";
	}

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

		int Status = 200;
		const json_value *pStatus = json_object_get(pObj, "responseStatus");
		if(pStatus != &json_value_none)
		{
			if(pStatus->type == json_integer)
				Status = (int)pStatus->u.integer;
			else if(pStatus->type == json_double)
				Status = (int)pStatus->u.dbl;
		}

		const json_value *pData = json_object_get(pObj, "responseData");
		if(pData == &json_value_none || pData->type != json_object)
		{
			str_copy(Out.m_Text, "No responseData");
			return false;
		}

		const json_value *pTranslatedText = json_object_get(pData, "translatedText");
		if(pTranslatedText == &json_value_none || pTranslatedText->type != json_string)
		{
			str_copy(Out.m_Text, "No translatedText");
			return false;
		}

		// MyMemory 把配额超限、语言对无效等错误以 WARNING 文本形式塞进 translatedText
		if(str_startswith_nocase(pTranslatedText->u.string.ptr, "MYMEMORY WARNING"))
		{
			if(str_find_nocase(pTranslatedText->u.string.ptr, "USED ALL AVAILABLE"))
			{
				Out.m_Notice = ETranslateNotice::QUOTA_EXCEEDED;
				str_copy(Out.m_Text, "MyMemory: daily anonymous quota reached (resets tomorrow) - pick another service in settings");
			}
			else
				str_format(Out.m_Text, sizeof(Out.m_Text), "MyMemory: %.120s", pTranslatedText->u.string.ptr);
			return false;
		}

		// 翻译记忆命中的服务提示样板按失败处理，由调用方展示本地化固定文案
		if(ResultLooksLikeServiceNotice(pTranslatedText->u.string.ptr))
		{
			Out.m_Notice = ETranslateNotice::SERVICE_NOTICE;
			str_copy(Out.m_Text, "MyMemory returned a service notice instead of a translation");
			return false;
		}

		if(Status != 200)
		{
			Out.m_Notice = HttpErrorNotice(Status);
			str_format(Out.m_Text, sizeof(Out.m_Text), "MyMemory error %d: %.120s", Status, pTranslatedText->u.string.ptr);
			return false;
		}

		str_copy(Out.m_Text, pTranslatedText->u.string.ptr);
		Out.m_Language[0] = '\0';
		return true;
	}

protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pObj = m_pHttpRequest->ResultJson();
		bool Res = ParseResponseJson(pObj, Out);
		if(!Res)
		{
			unsigned char *pResult = nullptr;
			size_t ResultLength = 0;
			m_pHttpRequest->Result(&pResult, &ResultLength);
			if(pResult && ResultLength > 0)
			{
				size_t LogLength = std::min(ResultLength, size_t(1024));
				log_debug("translate/mymemory", "MyMemory response failed to parse. Raw response: %.*s", (int)LogLength, pResult);
			}
		}
		json_value_free(pObj);
		return Res;
	}

public:
	const char *EncodeTarget(const char *pTarget) const override
	{
		if(!pTarget || pTarget[0] == '\0')
			return EncodeLangCode(DefaultConfig::QmTranslateTarget);
		return EncodeLangCode(pTarget);
	}

	const char *Name() const override
	{
		return "MyMemory";
	}

	CTranslateBackendMymemory(IHttp &Http, const char *pText, const char *pTarget, const char *pSource, FTranslateRequestFactory pCreateRequest) : ITranslateBackendHttp(pCreateRequest)
	{
		m_QueryText = pText ? pText : "";

		// 按 UTF-8 边界截断到 MyMemory 500 字节上限
		char aQuery[MAX_QUERY_BYTES + 1];
		size_t Copy = 0;
		const char *p = pText;
		while(p && *p && Copy < MAX_QUERY_BYTES)
		{
			const char *pBefore = p;
			str_utf8_decode(&p);
			const size_t ByteLen = (size_t)(p - pBefore);
			if(Copy + ByteLen > MAX_QUERY_BYTES)
				break;
			mem_copy(aQuery + Copy, pBefore, ByteLen);
			Copy += ByteLen;
		}
		aQuery[Copy] = '\0';

		char aBuf[8192];
		str_copy(aBuf, "https://api.mymemory.translated.net/get?q=");
		UrlEncode(aQuery, aBuf + strlen(aBuf), sizeof(aBuf) - strlen(aBuf));
		str_append(aBuf, "&langpair=", sizeof(aBuf));
		str_append(aBuf, ResolveSource(pSource, pText), sizeof(aBuf));
		str_append(aBuf, "%7C", sizeof(aBuf));
		str_append(aBuf, EncodeTarget(pTarget), sizeof(aBuf));

		PrepareHttpRequest(aBuf);
		Http.Run(m_pHttpRequest);
	}
};

// NOLINTNEXTLINE(misc-use-internal-linkage)
class CTranslateBackendDeepl : public ITranslateBackendHttp
{
private:
	// DeepL 语言码使用大写；官方语言表无 ZH-TW，繁体目标为 ZH-HANT（仅目标），源语言用基础码 ZH
	static std::string UpperLanguageCode(const char *pCode)
	{
		std::string Upper = pCode ? pCode : "";
		for(char &c : Upper)
			c = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
		return Upper;
	}

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

		const json_value *pTranslations = json_object_get(pObj, "translations");
		if(pTranslations == &json_value_none || pTranslations->type != json_array)
		{
			str_copy(Out.m_Text, "No translations");
			return false;
		}
		if(pTranslations->u.array.length <= 0)
		{
			str_copy(Out.m_Text, "translations is empty");
			return false;
		}

		const json_value *pTranslation = pTranslations->u.array.values[0];
		if(!pTranslation || pTranslation->type != json_object)
		{
			str_copy(Out.m_Text, "translation is not object");
			return false;
		}

		const json_value *pText = json_object_get(pTranslation, "text");
		if(pText == &json_value_none || pText->type != json_string)
		{
			str_copy(Out.m_Text, "No translation text");
			return false;
		}

		if(pText->u.string.length == 0 || pText->u.string.length >= sizeof(Out.m_Text))
		{
			str_copy(Out.m_Text, "DeepL translation is empty or exceeds buffer capacity");
			return false;
		}

		const json_value *pDetected = json_object_get(pTranslation, "detected_source_language");
		if(pDetected != &json_value_none && pDetected->type == json_string)
			str_copy(Out.m_Language, pDetected->u.string.ptr);
		else
			Out.m_Language[0] = '\0';

		str_copy(Out.m_Text, pText->u.string.ptr);
		return true;
	}

	// 非 200 时也走 ParseResponse：给出 403/429/456 的针对性错误说明
	bool ParseHttpError() const override
	{
		return true;
	}

protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		const int StatusCode = m_pHttpRequest->StatusCode();
		if(StatusCode >= 400)
		{
			const char *pMeaning = nullptr;
			if(StatusCode == 403)
				pMeaning = "DeepL: invalid API key or wrong endpoint tier (free keys end with :fx)";
			else if(StatusCode == 429)
				pMeaning = "DeepL: too many requests, try again later";
			else if(StatusCode == 456)
			{
				Out.m_Notice = ETranslateNotice::QUOTA_EXCEEDED;
				pMeaning = "DeepL: monthly character quota exceeded";
			}
			if(pMeaning)
			{
				str_copy(Out.m_Text, pMeaning);
				return false;
			}
			str_format(Out.m_Text, sizeof(Out.m_Text), "DeepL HTTP %d", StatusCode);
			// 其余错误读取响应体的 message 字段补充细节
			json_value *pObj = m_pHttpRequest->ResultJson();
			if(pObj && pObj->type == json_object)
			{
				const json_value *pMessage = json_object_get(pObj, "message");
				if(pMessage != &json_value_none && pMessage->type == json_string)
					str_format(Out.m_Text, sizeof(Out.m_Text), "DeepL HTTP %d: %.200s", StatusCode, pMessage->u.string.ptr);
			}
			json_value_free(pObj);
			return false;
		}

		json_value *pObj = m_pHttpRequest->ResultJson();
		const bool Result = ParseResponseJson(pObj, Out);
		json_value_free(pObj);
		return Result;
	}

public:
	const char *EncodeTarget(const char *pTarget) const override
	{
		// 接口按值语义消费返回串，但可能连续调用两次（CompareTargets），
		// 用双槽位轮换缓冲避免悬空与互相覆盖。
		static char aBuf[2][16];
		static int Slot = 0;
		char *pBuf = aBuf[Slot];
		Slot = (Slot + 1) % 2;
		const char *pCode = (pTarget && pTarget[0] != '\0') ? pTarget : DefaultConfig::QmTranslateTarget;
		if(str_comp_nocase(pCode, "zh") == 0)
		{
			str_copy(pBuf, "ZH", 16);
			return pBuf;
		}
		if(str_comp_nocase(pCode, "zh-tw") == 0)
		{
			str_copy(pBuf, "ZH-HANT", 16);
			return pBuf;
		}
		str_copy(pBuf, UpperLanguageCode(pCode).c_str(), 16);
		return pBuf;
	}

	const char *Name() const override
	{
		return "DeepL";
	}

	CTranslateBackendDeepl(IHttp &Http, const char *pText, const char *pTarget, const char *pSource, FTranslateRequestFactory pCreateRequest) : ITranslateBackendHttp(pCreateRequest)
	{
		// 免费 key 以 ":fx" 结尾，使用 api-free.deepl.com；Pro key 使用 api.deepl.com（官方 CLI 同款判别）
		const char *pApiKey = g_Config.m_QmTranslateDeeplKey;
		if(pApiKey[0] == '\0')
		{
			SetInitError("Missing API Key: configure the DeepL API key in settings (free keys end with :fx)");
			return;
		}

		const char *pUrl = str_endswith_nocase(pApiKey, ":fx") != nullptr ? "https://api-free.deepl.com/v2/translate" : "https://api.deepl.com/v2/translate";

		CJsonStringWriter Json;
		Json.BeginObject();
		Json.WriteAttribute("text");
		Json.BeginArray();
		Json.WriteStrValue(pText);
		Json.EndArray();
		Json.WriteAttribute("target_lang");
		Json.WriteStrValue(EncodeTarget(pTarget));
		if(HasExplicitTranslateSource(pSource))
		{
			Json.WriteAttribute("source_lang");
			// 源语言只接受基础码：繁体源同样按 ZH 提交
			const char *pSourceCode = str_comp_nocase(NormalizeTranslateSource(pSource), "zh-tw") == 0 ? "zh" : NormalizeTranslateSource(pSource);
			const std::string SourceLang = UpperLanguageCode(pSourceCode);
			Json.WriteStrValue(SourceLang.c_str());
		}
		Json.EndObject();
		const std::string Payload = Json.GetOutputString();

		m_pHttpRequest = m_pCreateRequest(pUrl);
		m_pHttpRequest->LogProgress(HTTPLOG::FAILURE);
		m_pHttpRequest->FailOnErrorStatus(false);
		m_pHttpRequest->Timeout(CTimeout{10000, 30000, 500, 10});
		m_pHttpRequest->HeaderString("Content-Type", "application/json");
		char aAuthorization[512];
		str_format(aAuthorization, sizeof(aAuthorization), "DeepL-Auth-Key %s", pApiKey);
		m_pHttpRequest->HeaderString("Authorization", aAuthorization);
		m_pHttpRequest->Post(reinterpret_cast<const unsigned char *>(Payload.data()), Payload.size());
		Http.Run(m_pHttpRequest);
	}
};

// NOLINTNEXTLINE(misc-use-internal-linkage)
class CTranslateBackendLlm : public ITranslateBackendHttp
{
private:
	ELlmProvider m_Provider;

	// 端点归一与双格式（Chat Completions / Responses）支持
	IHttp *m_pHttp = nullptr;
	char m_aBaseUrl[256] = "";
	ELlmApiStyle m_ConfigStyle = ELlmApiStyle::AUTO; // 端点后缀显式指定的格式
	ELlmApiStyle m_ActiveStyle = ELlmApiStyle::CHAT; // 当前实际使用的格式
	std::string m_ChatPayload;
	std::string m_ResponsesPayload;
	char m_aAuthorization[512] = "";

	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		SLlmParseResult Parsed;
		const bool Success = m_ActiveStyle == ELlmApiStyle::RESPONSES ? ParseLlmResponsesJson(pObj, Parsed) : ParseLlmResponseJson(pObj, Parsed);
		Out.m_Language[0] = '\0';
		if(!Success)
		{
			if(Parsed.m_Refused)
				Out.m_Notice = ETranslateNotice::CONTENT_REFUSED;
			str_copy(Out.m_Text, Parsed.m_aError);
			return false;
		}
		if(str_length(Parsed.m_aText) >= static_cast<int>(sizeof(Out.m_Text)))
		{
			str_copy(Out.m_Text, "Translation result exceeds buffer capacity");
			return false;
		}
		str_copy(Out.m_Text, Parsed.m_aText);
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
				SLlmParseResult Parsed;
				ParseLlmResponseJson(pObj, Parsed);
				if(Parsed.m_Refused)
				{
					Out.m_Notice = ETranslateNotice::CONTENT_REFUSED;
					str_copy(Out.m_Text, Parsed.m_aError);
					json_value_free(pObj);
					return false;
				}
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

				// HTML 响应通常意味着端点填写的是站点首页或 base URL，
				// 而非完整的 chat/completions 请求地址，给出针对性提示
				size_t Offset = 0;
				while(Offset < CopyLen && (aRawResponse[Offset] == ' ' || aRawResponse[Offset] == '\t'))
					Offset++;
				if(aRawResponse[Offset] == '<')
				{
					str_format(Out.m_Text, sizeof(Out.m_Text),
						"Endpoint returned HTML instead of JSON: fill in the full chat completions URL (e.g. https://api.example.com/v1/chat/completions), response starts with: %.100s",
						aRawResponse + Offset);
				}
				else
				{
					str_format(Out.m_Text, sizeof(Out.m_Text),
						"JSON parse error: %.200s%s",
						aRawResponse,
						ResultLength > 200 ? "... (truncated)" : "");
				}
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

protected:
	// 会话内记忆 base URL 的可用接口格式，避免每次请求双发探测
	static std::vector<std::pair<std::string, int>> s_vLlmApiStyleMemory;

	static void LearnApiStyle(const char *pBaseUrl, ELlmApiStyle Style)
	{
		for(auto &Entry : s_vLlmApiStyleMemory)
		{
			if(Entry.first == pBaseUrl)
			{
				Entry.second = static_cast<int>(Style);
				return;
			}
		}
		if(s_vLlmApiStyleMemory.size() < 16)
			s_vLlmApiStyleMemory.emplace_back(pBaseUrl, static_cast<int>(Style));
	}

	static std::optional<ELlmApiStyle> RecallApiStyle(const char *pBaseUrl)
	{
		for(const auto &Entry : s_vLlmApiStyleMemory)
		{
			if(Entry.first == pBaseUrl)
				return static_cast<ELlmApiStyle>(Entry.second);
		}
		return std::nullopt;
	}

	void StartRequest(ELlmApiStyle Style)
	{
		m_ActiveStyle = Style;
		const char *pPath = Style == ELlmApiStyle::RESPONSES ? "/responses" : "/chat/completions";
		char aUrl[512];
		str_format(aUrl, sizeof(aUrl), "%s%s", m_aBaseUrl, pPath);
		const char *pPayload = Style == ELlmApiStyle::RESPONSES ? m_ResponsesPayload.c_str() : m_ChatPayload.c_str();

		m_pHttpRequest = m_pCreateRequest(aUrl);
		m_pHttpRequest->LogProgress(HTTPLOG::FAILURE);
		m_pHttpRequest->FailOnErrorStatus(false);
		// 连接最多 10 秒，完整请求最多 60 秒；低速限制独立于总时限。
		m_pHttpRequest->Timeout(CTimeout{10000, 60000, 100, 30});
		m_pHttpRequest->HeaderString("Content-Type", "application/json");
		m_pHttpRequest->HeaderString("Authorization", m_aAuthorization);
		m_pHttpRequest->Post(reinterpret_cast<const unsigned char *>(pPayload), str_length(pPayload));
		m_pHttp->Run(m_pHttpRequest);
	}

	// Chat 格式请求失败且可能是端点不支持时，判断是否值得回退尝试 Responses
	bool ShouldRetryWithResponses() const
	{
		if(m_aInitError[0] != '\0' || !m_pHttpRequest || m_pHttpRequest->State() != EHttpState::DONE || m_ConfigStyle != ELlmApiStyle::AUTO || m_ActiveStyle != ELlmApiStyle::CHAT)
			return false;
		const int StatusCode = m_pHttpRequest->StatusCode();
		if(StatusCode == 404 || StatusCode == 405)
			return true;
		if(StatusCode != 200)
			return false;
		// 200 但返回 HTML 页面（站点首页/错误页），同样尝试 Responses
		unsigned char *pResult = nullptr;
		size_t ResultLength = 0;
		m_pHttpRequest->Result(&pResult, &ResultLength);
		if(pResult && ResultLength > 0)
		{
			size_t Offset = 0;
			while(Offset < ResultLength && (pResult[Offset] == ' ' || pResult[Offset] == '\t' || pResult[Offset] == '\r' || pResult[Offset] == '\n'))
				++Offset;
			return Offset < ResultLength && pResult[Offset] == '<';
		}
		return false;
	}

public:
	std::optional<bool> Update(CTranslateResponse &Out) override
	{
		const std::optional<bool> Result = ITranslateBackendHttp::Update(Out);
		if(Result.has_value() && *Result)
		{
			// 成功：记忆该 base URL 的可用格式
			if(m_ConfigStyle == ELlmApiStyle::AUTO)
				LearnApiStyle(m_aBaseUrl, m_ActiveStyle);
			return Result;
		}
		if(Result.has_value() && ShouldRetryWithResponses())
		{
			StartRequest(ELlmApiStyle::RESPONSES);
			return std::nullopt; // 继续等待 Responses 请求
		}
		return Result;
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

	CTranslateBackendLlm(IHttp &Http, const char *pText, const char *pTarget, const char *pSource, FTranslateRequestFactory pCreateRequest) : ITranslateBackendHttp(pCreateRequest)
	{
		// 获取当前选择的 Provider（确保值在有效范围内）
		constexpr int ProviderMin = static_cast<int>(ELlmProvider::ZHIPU_AI);
		constexpr int ProviderMax = static_cast<int>(ELlmProvider::CUSTOM);
		int ProviderValue = std::clamp(g_Config.m_QmTranslateLlmProvider, ProviderMin, ProviderMax);
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

		// 归一端点：兼容 base URL、/v1、完整路径、笔误后缀；显式后缀决定接口格式
		SLlmEndpointInfo EndpointInfo;
		if(!NormalizeLlmEndpoint(pEndpoint, EndpointInfo))
		{
			SetInitError("Invalid Endpoint: must be an http(s) URL, e.g. https://api.example.com/v1");
			return;
		}
		str_copy(m_aBaseUrl, EndpointInfo.m_aBaseUrl, sizeof(m_aBaseUrl));
		m_ConfigStyle = EndpointInfo.m_Style;
		m_pHttp = &Http;

		std::string SystemMessage = g_Config.m_QmTranslateSystemPrompt;
		if(SystemMessage.empty())
		{
			SystemMessage = DEFAULT_TRANSLATE_PROMPT;
			const size_t TargetPosition = SystemMessage.find("%s");
			SystemMessage.replace(TargetPosition, 2, EncodeTarget(pTarget));
		}
		if(HasExplicitTranslateSource(pSource))
		{
			char aSourceHint[128];
			str_format(aSourceHint, sizeof(aSourceHint), " The input language is %s.", NormalizeTranslateSource(pSource));
			SystemMessage += aSourceHint;
		}

		// 字符串交给生产 JSON writer 转义，避免固定缓冲区截断提示词和 UTF-8。
		const std::string Model = QuoteJsonString(GetLlmModel(m_Provider));
		const std::string System = QuoteJsonString(SystemMessage.c_str());
		const std::string Text = QuoteJsonString(pText);
		m_ChatPayload = "{\"model\":" + Model + ",\"messages\":[{\"role\":\"system\",\"content\":" + System +
				"},{\"role\":\"user\",\"content\":" + Text + "}]";
		const bool Thinking = g_Config.m_QmTranslateLlmEnableThinking != 0;
		if(!Thinking || m_Provider == ELlmProvider::ZHIPU_AI || m_Provider == ELlmProvider::OPENAI)
			m_ChatPayload += ",\"temperature\":0.3,\"max_tokens\":1024";
		if(m_Provider == ELlmProvider::ZHIPU_AI || (Thinking && m_Provider != ELlmProvider::OPENAI))
			m_ChatPayload += Thinking ? ",\"thinking\":{\"type\":\"enabled\"}" : ",\"thinking\":{\"type\":\"disabled\"}";
		m_ChatPayload += "}";
		m_ResponsesPayload = "{\"model\":" + Model + ",\"instructions\":" + System + ",\"input\":" + Text + ",\"max_output_tokens\":1024}";
		str_format(m_aAuthorization, sizeof(m_aAuthorization), "Bearer %s", pApiKey);

		// 确定初始接口格式：显式后缀 > 会话记忆 > 默认 Chat
		std::optional<ELlmApiStyle> Remembered;
		if(m_ConfigStyle == ELlmApiStyle::AUTO)
			Remembered = RecallApiStyle(m_aBaseUrl);
		ELlmApiStyle InitialStyle = m_ConfigStyle != ELlmApiStyle::AUTO ? m_ConfigStyle : Remembered.value_or(ELlmApiStyle::CHAT);
		StartRequest(InitialStyle);
	}
};

std::vector<std::pair<std::string, int>> CTranslateBackendLlm::s_vLlmApiStyleMemory;

std::unique_ptr<ITranslateBackend> CreateTranslateBackend(IHttp &Http, const char *pText, const char *pTarget, const char *pSource, FTranslateRequestFactory pCreateRequest)
{
	if(str_comp_nocase(g_Config.m_QmTranslateBackend, "libretranslate") == 0)
		return std::make_unique<CTranslateBackendLibretranslate>(Http, pText, pTarget, pSource, pCreateRequest);
	if(str_comp_nocase(g_Config.m_QmTranslateBackend, "ftapi") == 0)
		return std::make_unique<CTranslateBackendFtapi>(Http, pText, pTarget, pCreateRequest);
	if(str_comp_nocase(g_Config.m_QmTranslateBackend, "mymemory") == 0)
		return std::make_unique<CTranslateBackendMymemory>(Http, pText, pTarget, pSource, pCreateRequest);
	if(str_comp_nocase(g_Config.m_QmTranslateBackend, "deepl") == 0)
		return std::make_unique<CTranslateBackendDeepl>(Http, pText, pTarget, pSource, pCreateRequest);
	if(str_comp_nocase(g_Config.m_QmTranslateBackend, "tencentcloud") == 0)
		return std::make_unique<CTranslateBackendTencentCloud>(Http, pText, pTarget, pSource, pCreateRequest);
	if(str_comp_nocase(g_Config.m_QmTranslateBackend, "llm") == 0)
		return std::make_unique<CTranslateBackendLlm>(Http, pText, pTarget, pSource, pCreateRequest);
	return nullptr;
}

const char *GetSelectedTranslateLlmKey()
{
	return GetLlmApiKey(static_cast<ELlmProvider>(std::clamp(g_Config.m_QmTranslateLlmProvider, 0, 3)));
}

int GetTranslateConcurrency()
{
	// 如果用户手动设置（非 0），使用用户值；0 表示自动模式
	if(g_Config.m_QmTranslateLlmConcurrency != 0)
		return std::clamp(g_Config.m_QmTranslateLlmConcurrency, 1, 20);

	// 根据后端类型提供智能默认值
	if(str_comp_nocase(g_Config.m_QmTranslateBackend, "llm") == 0)
	{
		// LLM 后端：根据 Provider 类型提供不同默认值
		constexpr int ProviderMin = static_cast<int>(ELlmProvider::ZHIPU_AI);
		constexpr int ProviderMax = static_cast<int>(ELlmProvider::CUSTOM);
		int ProviderValue = std::clamp(g_Config.m_QmTranslateLlmProvider, ProviderMin, ProviderMax);
		ELlmProvider Provider = static_cast<ELlmProvider>(ProviderValue);

		switch(Provider)
		{
		case ELlmProvider::ZHIPU_AI:
			return 1; // 智谱免费 flash 档速率限制仅 1 条并发
		case ELlmProvider::DEEPSEEK:
			return 3; // DeepSeek 默认 3
		case ELlmProvider::OPENAI:
			return 2; // OpenAI 默认 2（成本考虑）
		case ELlmProvider::CUSTOM:
		default:
			return std::clamp(g_Config.m_QmTranslateLlmConcurrencyDefault, 1, 20); // 自定义使用配置默认值
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
	else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "mymemory") == 0)
	{
		return 1; // MyMemory 匿名配额有限，默认 1
	}
	else if(str_comp_nocase(g_Config.m_QmTranslateBackend, "deepl") == 0)
	{
		return 2; // DeepL 免费档按月配额计费，无严格并发限制，保守取 2
	}

	// 未知后端默认 3
	return 3;
}
