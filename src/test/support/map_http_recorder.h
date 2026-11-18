#ifndef TEST_SUPPORT_MAP_HTTP_RECORDER_H
#define TEST_SUPPORT_MAP_HTTP_RECORDER_H

#include <base/mem.h>
#include <base/str.h>

#include <engine/http.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace MapHttpTest
{
	class CResponse : public IHttpRequest
	{
	public:
		std::string m_Header;
		bool m_Aborted = false;
		explicit CResponse(const char *pUrl) :
			IHttpRequest(pUrl) {}
		void Header(const char *pHeader) override { m_Header = pHeader; }
		void Abort() override { m_Aborted = true; }
		void Fail() { m_State = EHttpState::ERROR; }
		void Finish(const char *pJson, int Status = 200)
		{
			free(m_pBuffer);
			m_ResponseLength = str_length(pJson);
			m_pBuffer = static_cast<unsigned char *>(malloc(m_ResponseLength + 1));
			ASSERT_NE(m_pBuffer, nullptr);
			mem_copy(m_pBuffer, pJson, m_ResponseLength + 1);
			m_BufferSize = m_ResponseLength + 1;
			m_StatusCode = Status;
			m_State = EHttpState::DONE;
		}
	};

	inline std::unique_ptr<IHttpRequest> CreateResponse(const char *pUrl)
	{
		return std::make_unique<CResponse>(pUrl);
	}

	class CHttpRecorder : public IHttp
	{
	public:
		std::vector<std::shared_ptr<CResponse>> m_vRequests;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override
		{
			m_vRequests.push_back(std::static_pointer_cast<CResponse>(pRequest));
		}
		bool HasIpresolveBug() const override { return false; }
	};
}

#endif
