#include <base/str.h>
#include <base/system.h>

#include <engine/http.h>

#include <game/client/components/qmclient/map_tags.h>

#include <gtest/gtest.h>

#include <cstdlib>

namespace
{
	class CResponse : public IHttpRequest
	{
	public:
		bool m_Aborted = false;
		explicit CResponse(const char *pUrl) :
			IHttpRequest(pUrl) {}
		void Header(const char *) override {}
		void Abort() override { m_Aborted = true; }
		void Fail() { m_State = EHttpState::ERROR; }
		void Finish(const char *pJson)
		{
			free(m_pBuffer);
			m_ResponseLength = str_length(pJson);
			m_pBuffer = static_cast<unsigned char *>(malloc(m_ResponseLength + 1));
			mem_copy(m_pBuffer, pJson, m_ResponseLength + 1);
			m_BufferSize = m_ResponseLength + 1;
			m_StatusCode = 200;
			m_State = EHttpState::DONE;
		}
	};
	std::unique_ptr<IHttpRequest> Read(const char *pUrl) { return std::make_unique<CResponse>(pUrl); }
	std::unique_ptr<IHttpRequest> Write(const char *pUrl, const char *) { return Read(pUrl); }
	class CHttpRecorder : public IHttp
	{
	public:
		std::vector<std::shared_ptr<CResponse>> m_vRequests;
		void Run(std::shared_ptr<IHttpRequest> pRequest) override { m_vRequests.push_back(std::static_pointer_cast<CResponse>(pRequest)); }
		bool HasIpresolveBug() const override { return false; }
	};
	constexpr const char *SNAPSHOT = "{\"ok\":true,\"community\":\"ddnet\",\"map\":\"Lighthouse\",\"player\":\"Alice\",\"eligible\":true,\"eligibility_error\":\"\",\"tags\":[]}";
}

TEST(QmMapTagsLifecycle, RenamingCancelsReadAndDoesNotPublishItsLateResponse)
{
	CHttpRecorder Http;
	QmMapTags::CClient Client(Read, Write);
	Client.SetContext("ddnet", "Lighthouse", "Alice");
	Client.Update(&Http, 1.0);
	ASSERT_EQ(Http.m_vRequests.size(), 1u);
	Client.SetContext("ddnet", "Lighthouse", "Bob");
	EXPECT_TRUE(Http.m_vRequests[0]->m_Aborted);
	Http.m_vRequests[0]->Finish(SNAPSHOT);
	Client.Poll();
	EXPECT_FALSE(Client.Ready());
}

TEST(QmMapTagsLifecycle, ClearCancelsSubmissionAndRemovesEligibility)
{
	CHttpRecorder Http;
	QmMapTags::CClient Client(Read, Write);
	Client.SetContext("ddnet", "Lighthouse", "Alice");
	Client.Update(&Http, 1.0);
	Http.m_vRequests[0]->Finish(SNAPSHOT);
	Client.Poll();
	ASSERT_TRUE(Client.Ready());
	Client.Submit(&Http, "Hook");
	ASSERT_TRUE(Client.Busy());
	Client.Clear();
	EXPECT_TRUE(Http.m_vRequests.back()->m_Aborted);
	EXPECT_FALSE(Client.Ready());
	EXPECT_FALSE(Client.Snapshot().m_Eligible);
}

TEST(QmMapTagsLifecycle, RepeatedRefreshDoesNotStartConcurrentReads)
{
	CHttpRecorder Http;
	QmMapTags::CClient Client(Read, Write);
	Client.SetContext("ddnet", "Lighthouse", "Alice");
	Client.Update(&Http, 1.0);
	Client.Refresh();
	Client.Update(&Http, 2.0);
	EXPECT_EQ(Http.m_vRequests.size(), 1u);
}

TEST(QmMapTagsLifecycle, FailedReadKeepsRetryAvailableWithoutReadingFailedHttpResult)
{
	CHttpRecorder Http;
	QmMapTags::CClient Client(Read, Write);
	Client.SetContext("ddnet", "Lighthouse", "Alice");
	Client.Update(&Http, 1.0);
	Http.m_vRequests[0]->Fail();
	Client.Poll();
	EXPECT_FALSE(Client.Ready());
	EXPECT_NE(Client.Status()[0], '\0');
	Client.Refresh();
	Client.Update(&Http, 2.0);
	EXPECT_EQ(Http.m_vRequests.size(), 2u);
	Http.m_vRequests[1]->Finish(SNAPSHOT);
	Client.Poll();
	EXPECT_TRUE(Client.Ready());
	EXPECT_STREQ(Client.Status(), "");
}
