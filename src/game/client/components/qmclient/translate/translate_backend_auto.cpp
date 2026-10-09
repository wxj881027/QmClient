#include "translate_backend.h"
#include "translate_backend_http.h"

#include <base/str.h>

#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>

#include <initializer_list>
#include <string>

namespace
{
	class CTranslateBackendAuto : public ITranslateBackend
	{
		std::shared_ptr<IHttpRequest> m_pRequest;

	public:
		CTranslateBackendAuto(IHttp &Http, const char *pText, const char *pTarget, const char *pSource, FTranslateRequestFactory pCreateRequest)
		{
			// 自动服务只接收文本和语言；维护者凭据只保存在服务端。
			m_pRequest = pCreateRequest("https://qmclient.icu/api/v1/translate");
			m_pRequest->FailOnErrorStatus(false);
			m_pRequest->LogProgress(HTTPLOG::FAILURE);
			m_pRequest->MaxResponseSize(64 * 1024);
			m_pRequest->Timeout(CTimeout{5000, 11000, 500, 5});
			CJsonStringWriter Writer;
			Writer.BeginObject();
			Writer.WriteAttribute("text");
			Writer.WriteStrValue(pText);
			Writer.WriteAttribute("source");
			Writer.WriteStrValue(NormalizeTranslateSource(pSource));
			Writer.WriteAttribute("target");
			Writer.WriteStrValue(pTarget);
			Writer.EndObject();
			const std::string Body = Writer.GetOutputString();
			m_pRequest->PostJson(Body.c_str());
			Http.Run(m_pRequest);
		}
		~CTranslateBackendAuto() override { m_pRequest->Abort(); }
		const char *Name() const override { return "Qm automatic service"; }
		std::optional<bool> Update(CTranslateResponse &Out) override
		{
			Out = {};
			if(!m_pRequest->Done())
				return std::nullopt;
			Out.m_Error = true;
			Out.m_HttpStatus = m_pRequest->StatusCode();
			if(m_pRequest->State() != EHttpState::DONE)
			{
				Out.m_Notice = ETranslateNotice::NETWORK_ERROR;
				return false;
			}
			json_value *pJson = m_pRequest->ResultJson();
			Out.m_Notice = ETranslateNotice::SERVICE_UNAVAILABLE;
			bool Success = false;
			if(pJson != nullptr && pJson->type == json_object)
			{
				const json_value &Ok = (*pJson)["ok"];
				const json_value &Error = (*pJson)["error"];
				const json_value &Text = (*pJson)["text"];
				const json_value &Language = (*pJson)["language"];
				if(m_pRequest->StatusCode() == 200 && Ok.type == json_boolean && Ok.u.boolean && Error.type == json_none)
				{
					Success = CopyTranslateText(&Text, Out);
					if(Success)
					{
						if(Language.type == json_string)
							str_copy(Out.m_Language, Language.u.string.ptr);
						Out.m_Notice = ETranslateNotice::NONE;
					}
					else
						Out.m_Text[0] = '\0';
				}
				else if(Error.type == json_string)
				{
					struct SNotice
					{
						const char *m_pCode;
						ETranslateNotice m_Notice;
					};
					for(const auto &Notice : {SNotice{"content_refused", ETranslateNotice::CONTENT_REFUSED}, SNotice{"authentication", ETranslateNotice::AUTHENTICATION},
						    SNotice{"rate_limit", ETranslateNotice::RATE_LIMIT}, SNotice{"quota_exceeded", ETranslateNotice::QUOTA_EXCEEDED},
						    SNotice{"network_error", ETranslateNotice::NETWORK_ERROR}, SNotice{"service_notice", ETranslateNotice::SERVICE_NOTICE}})
						if(str_comp(Error.u.string.ptr, Notice.m_pCode) == 0)
							Out.m_Notice = Notice.m_Notice;
				}
			}
			Out.m_Error = !Success;
			json_value_free(pJson);
			return Success;
		}
	};
}

std::unique_ptr<ITranslateBackend> CreateTranslateBackendAutomatic(IHttp &Http, const char *pText, const char *pTarget, const char *pSource, FTranslateRequestFactory pCreateRequest)
{
	return std::make_unique<CTranslateBackendAuto>(Http, pText, pTarget, pSource, pCreateRequest);
}
