#include "translate_backend_http.h"

#include <base/hash.h>
#include <base/secure.h>
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <string>

namespace
{
	struct SLanguageCode
	{
		const char *m_pClient;
		const char *m_pService;
	};
	constexpr SLanguageCode s_aBaiduCodes[] = {{"zh", "zh"}, {"zh-CN", "zh"}, {"zh-Hans", "zh"}, {"zh-TW", "cht"}, {"zh-Hant", "cht"}, {"ja", "jp"}, {"ko", "kor"}, {"fr", "fra"}, {"es", "spa"}, {"en", "en"}, {"ru", "ru"}, {"de", "de"}, {"pt", "pt"}};

	const char *EncodeBaiduLanguage(const char *pCode)
	{
		for(const auto &Code : s_aBaiduCodes)
			if(str_comp_nocase(pCode, Code.m_pClient) == 0)
				return Code.m_pService;
		return pCode;
	}

	const char *DecodeBaiduLanguage(const char *pCode)
	{
		for(const auto &Code : s_aBaiduCodes)
			if(str_comp_nocase(pCode, Code.m_pService) == 0)
				return Code.m_pClient;
		return pCode;
	}

	ETranslateNotice BaiduErrorNotice(int Code)
	{
		switch(Code)
		{
		case 52001: return ETranslateNotice::NETWORK_ERROR;
		case 52002: return ETranslateNotice::SERVICE_UNAVAILABLE;
		case 52003:
		case 54001:
		case 58000:
		case 90107: return ETranslateNotice::AUTHENTICATION;
		case 54003:
		case 54005: return ETranslateNotice::RATE_LIMIT;
		case 54004: return ETranslateNotice::QUOTA_EXCEEDED;
		case 54000:
		case 58001:
		case 58002: return ETranslateNotice::INVALID_CONFIGURATION;
		default: return ETranslateNotice::INVALID_RESPONSE;
		}
	}

	class CTranslateBackendBaidu : public ITranslateBackendHttp
	{
		bool ParseResponse(CTranslateResponse &Out) override
		{
			json_value *pJson = m_pHttpRequest->ResultJson();
			const bool Success = ParseResponseJson(pJson, Out);
			json_value_free(pJson);
			return Success;
		}
		bool ParseHttpError() const override { return true; }

		bool ParseResponseJson(const json_value *pJson, CTranslateResponse &Out)
		{
			if(!pJson || pJson->type != json_object)
				return false;
			const json_value *pError = json_object_get(pJson, "error_code");
			if(pError != &json_value_none)
			{
				const int Code = pError->type == json_string ? str_toint(pError->u.string.ptr) : pError->type == json_integer ? static_cast<int>(pError->u.integer) :
																		-1;
				if(Code != 52000)
				{
					Out.m_Notice = BaiduErrorNotice(Code);
					str_format(Out.m_Text, sizeof(Out.m_Text), "Baidu translation error %d", Code);
					return false;
				}
			}
			const json_value *pResults = json_object_get(pJson, "trans_result");
			if(pResults->type != json_array || pResults->u.array.length == 0)
				return false;
			// 先校验全部段落，禁止把已成功的前半段发布为完整译文。
			std::string Text;
			for(unsigned int i = 0; i < pResults->u.array.length; ++i)
			{
				const json_value *pItem = pResults->u.array.values[i];
				CTranslateResponse Segment;
				if(pItem->type != json_object || !CopyTranslateText(json_object_get(pItem, "dst"), Segment))
					return false;
				if(i > 0)
					Text += '\n';
				Text += Segment.m_Text;
				if(Text.size() >= sizeof(Out.m_Text))
					return false;
			}
			const json_value *pSource = json_object_get(pJson, "from");
			if(pSource->type == json_string)
				str_copy(Out.m_Language, DecodeBaiduLanguage(pSource->u.string.ptr));
			str_copy(Out.m_Text, Text.c_str());
			return true;
		}

	public:
		const char *Name() const override { return "Baidu"; }
		const char *EncodeTarget(const char *pTarget) const override
		{
			return EncodeBaiduLanguage(ITranslateBackend::EncodeTarget(pTarget));
		}
		CTranslateBackendBaidu(IHttp &Http, const char *pText, const char *pTarget, const char *pSource, FTranslateRequestFactory pCreateRequest) : ITranslateBackendHttp(pCreateRequest)
		{
			if(g_Config.m_QmTranslateBaiduAppId[0] == '\0' || g_Config.m_QmTranslateBaiduKey[0] == '\0')
			{
				SetInitError("Configure the Baidu APP ID and API key");
				return;
			}
			if(!pText || !pText[0] || !str_utf8_check(pText))
			{
				SetInitError("Baidu input must be nonempty UTF-8", ETranslateNotice::INVALID_RESPONSE);
				return;
			}
			if(str_length(pText) > 6000)
			{
				SetInitError("Baidu input exceeds 6000 bytes", ETranslateNotice::INPUT_TOO_LONG);
				return;
			}
			// salt 每次重新生成；密钥始终只用于计算签名。
			unsigned int SaltValue;
			secure_random_fill(&SaltValue, sizeof(SaltValue));
			const std::string Salt = std::to_string(SaltValue);
			const std::string Body = BuildBaiduTranslateForm(pText, EncodeBaiduLanguage(NormalizeTranslateSource(pSource)), EncodeTarget(pTarget), g_Config.m_QmTranslateBaiduAppId, g_Config.m_QmTranslateBaiduKey, Salt.c_str());
			PrepareHttpRequest("https://fanyi-api.baidu.com/api/trans/vip/translate");
			m_pHttpRequest->HeaderString("Content-Type", "application/x-www-form-urlencoded; charset=UTF-8");
			m_pHttpRequest->Post(reinterpret_cast<const unsigned char *>(Body.data()), Body.size());
			Http.Run(m_pHttpRequest);
		}
	};
}

std::string BuildBaiduTranslateForm(const char *pText, const char *pSource, const char *pTarget, const char *pAppId, const char *pKey, const char *pSalt)
{
	// 百度签名必须使用未经 URL 编码的原文；密钥只参与签名，不进入请求体。
	const std::string SignatureInput = std::string(pAppId) + pText + pSalt + pKey;
	char aSignature[MD5_MAXSTRSIZE];
	md5_str(md5(SignatureInput.data(), SignatureInput.size()), aSignature, sizeof(aSignature));
	return "q=" + EncodeTranslateUrl(pText) + "&from=" + EncodeTranslateUrl(pSource) + "&to=" + EncodeTranslateUrl(pTarget) + "&appid=" + EncodeTranslateUrl(pAppId) + "&salt=" + EncodeTranslateUrl(pSalt) + "&sign=" + aSignature;
}

std::unique_ptr<ITranslateBackend> CreateBaiduTranslateBackend(IHttp &Http, const char *pText, const char *pTarget, const char *pSource, FTranslateRequestFactory pCreateRequest)
{
	return std::make_unique<CTranslateBackendBaidu>(Http, pText, pTarget, pSource, pCreateRequest);
}
