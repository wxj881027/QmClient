#include "translate.h"

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
	return TENCENTCLOUD_SECRET_ID_FALLBACK;
}

const char *GetTencentCloudSecretKey()
{
	return TENCENTCLOUD_SECRET_KEY_FALLBACK;
}

const char *GetEffectiveTranslateTarget(const char *pTarget)
{
	return (pTarget && pTarget[0] != '\0') ? pTarget : CConfig::ms_pTcTranslateTarget;
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
		return CConfig::ms_pTcTranslateTarget;
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
			str_copy(Out.m_Text, "No pDetectedLanguage");
			return false;
		}
		if(pDetectedLanguage->type != json_object)
		{
			str_copy(Out.m_Text, "pDetectedLanguage is not object");
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
		if(g_Config.m_TcTranslateKey[0] != '\0')
		{
			Json.WriteAttribute("api_key");
			Json.WriteStrValue(g_Config.m_TcTranslateKey);
		}
		Json.EndObject();
		CreateHttpRequest(Http, g_Config.m_TcTranslateEndpoint[0] == '\0' ? "localhost:5000/translate" : g_Config.m_TcTranslateEndpoint);
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
			return CConfig::ms_pTcTranslateTarget;
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
			SetInitError("Missing TencentCloud credentials: set env vars or tc_translate_secret_id/tc_translate_secret_key");
			return;
		}

		const char *pEndpoint = g_Config.m_TcTranslateEndpoint[0] != '\0' ? g_Config.m_TcTranslateEndpoint : TENCENTCLOUD_TMT_DEFAULT_ENDPOINT;
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
		m_pHttpRequest->HeaderString("X-TC-Region", g_Config.m_TcTranslateRegion);
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
			return CConfig::ms_pTcTranslateTarget;
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
		str_format(aBuf, sizeof(aBuf), "%s/translate?dl=%s&text=",
			g_Config.m_TcTranslateEndpoint[0] != '\0' ? g_Config.m_TcTranslateEndpoint : "https://ftapi.pythonanywhere.com",
			EncodeTarget(pTarget));

		UrlEncode(pText, aBuf + strlen(aBuf), sizeof(aBuf) - strlen(aBuf));

		CreateHttpRequest(Http, aBuf);
	}
};

static std::unique_ptr<ITranslateBackend> CreateTranslateBackend(IHttp &Http, const char *pText, const char *pTarget)
{
	if(str_comp_nocase(g_Config.m_TcTranslateBackend, "libretranslate") == 0)
		return std::make_unique<CTranslateBackendLibretranslate>(Http, pText, pTarget);
	if(str_comp_nocase(g_Config.m_TcTranslateBackend, "ftapi") == 0)
		return std::make_unique<CTranslateBackendFtapi>(Http, pText, pTarget);
	if(str_comp_nocase(g_Config.m_TcTranslateBackend, "tencentcloud") == 0)
		return std::make_unique<CTranslateBackendTencentCloud>(Http, pText, pTarget);
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

void CTranslate::Translate(int Id, bool ShowProgress)
{
	if(Id < 0 || Id >= (int)std::size(GameClient()->m_aClients))
	{
		GameClient()->m_Chat.Echo("Not a valid ID");
		return;
	}
	const auto &Player = GameClient()->m_aClients[Id];
	if(!Player.m_Active)
	{
		GameClient()->m_Chat.Echo("ID not connected");
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
		GameClient()->m_Chat.Echo("No message to translate");
		return;
	}

	Translate(*pLineBest, ShowProgress);
}

void CTranslate::Translate(CChat::CLine &Line, bool ShowProgress, bool AutoTriggered)
{
	if(m_vJobs.size() > 15)
	{
		return;
	}
	if(Line.m_ClientId == CChat::SERVER_MSG)
	{
		if(ShowProgress)
			GameClient()->m_Chat.Echo("Server messages are not translated");
		return;
	}

	CTranslateJob Job;
	Job.m_pLine = &Line;
	Job.m_pTranslateResponse = std::make_shared<CTranslateResponse>();
	Job.m_AutoTriggered = AutoTriggered;
	Job.m_pLine->m_pTranslateResponse = Job.m_pTranslateResponse;
	str_copy(Job.m_aTarget, GetEffectiveTranslateTarget(g_Config.m_TcTranslateTarget), sizeof(Job.m_aTarget));
	Job.m_pBackend = CreateTranslateBackend(*Http(), Job.m_pLine->m_aText, Job.m_aTarget);
	if(!Job.m_pBackend)
	{
		GameClient()->m_Chat.Echo("Invalid translate backend");
		return;
	}

	if(ShowProgress)
	{
		str_format(Job.m_pTranslateResponse->m_Text, sizeof(Job.m_pTranslateResponse->m_Text), TCLocalize("%s translating to %s", "translate"), Job.m_pBackend->Name(), Job.m_aTarget);
		Job.m_pLine->m_Time = time();
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

	if(m_vJobs.size() + m_vOutgoingJobs.size() > 15)
	{
		GameClient()->m_Chat.Echo("Too many translation jobs");
		return true;
	}

	COutgoingTranslateJob Job;
	Job.m_Team = Team;
	str_copy(Job.m_aTarget, Target.c_str(), sizeof(Job.m_aTarget));
	Job.m_pBackend = CreateTranslateBackend(*Http(), Text.c_str(), Job.m_aTarget);
	if(!Job.m_pBackend)
	{
		GameClient()->m_Chat.Echo("Invalid translate backend");
		return true;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s translating to %s before send", Job.m_pBackend->Name(), Job.m_aTarget);
	GameClient()->m_Chat.Echo(aBuf);
	m_vOutgoingJobs.emplace_back(std::move(Job));
	return true;
}

void CTranslate::OnRender()
{
	const auto Time = time();
	auto ForEach = [&](CTranslateJob &Job) {
		if(Job.m_pLine->m_pTranslateResponse != Job.m_pTranslateResponse)
			return true; // Not the same line anymore
		const std::optional<bool> Done = Job.m_pBackend->Update(*Job.m_pTranslateResponse);
		if(!Done.has_value())
			return false; // Keep ongoing tasks
		if(*Done)
		{
			if(Job.m_AutoTriggered && IsChineseLanguage(Job.m_pTranslateResponse->m_Language))
				Job.m_pTranslateResponse->m_Text[0] = '\0';
			else if(str_comp_nocase(Job.m_pLine->m_aText, Job.m_pTranslateResponse->m_Text) == 0) // Check for no translation difference
				Job.m_pTranslateResponse->m_Text[0] = '\0';
		}
		else
		{
			char aBuf[sizeof(Job.m_pTranslateResponse->m_Text)];
			str_format(aBuf, sizeof(aBuf), TCLocalize("%s to %s failed: %s", "translate"), Job.m_pBackend->Name(), Job.m_aTarget, Job.m_pTranslateResponse->m_Text);
			Job.m_pTranslateResponse->m_Error = true;
			str_copy(Job.m_pTranslateResponse->m_Text, aBuf);
		}
		Job.m_pLine->m_Time = Time;
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
	if(!g_Config.m_TcTranslateAuto)
		return;
	if(Line.m_ClientId == CChat::CLIENT_MSG)
		return;
	if(Line.m_ClientId == CChat::SERVER_MSG)
		return;
	for(const int Id : GameClient()->m_aLocalIds)
	{
		if(Id >= 0 && Id == Line.m_ClientId)
			return;
	}
	if(str_comp(g_Config.m_TcTranslateBackend, "ftapi") == 0)
	{
		// FTAPI quickly gets overloaded, please do not disable this
		// It may shut down if we spam it too hard
		return;
	}
	Translate(Line, false, true);
}
