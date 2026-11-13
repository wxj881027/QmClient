#include <engine/http.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <string>

namespace
{
	class CRequest : public IHttpRequest
	{
	public:
		bool m_Fail = false;
		int m_Allocations = 0;
		size_t m_LastSize = 0;
		CRequest() :
			IHttpRequest("https://body.test/request") { LogProgress(HTTPLOG::NONE); }
		void Header(const char *) override {}
		bool Ready() { return BeforeInit(); }
		const unsigned char *BodyData() const { return m_pBody; }
		size_t BodyLength() const { return m_BodyLength; }
		const char *Method() const { return GetRequestType(m_Type); }
		std::string Body() const { return m_pBody ? std::string(reinterpret_cast<const char *>(m_pBody), m_BodyLength) : std::string(); }

	private:
		void *AllocateRequestBody(size_t Size) noexcept override
		{
			++m_Allocations;
			m_LastSize = Size;
			return m_Fail ? nullptr : std::malloc(Size);
		}
	};

	void Post(CRequest &Request, int Format, const char *pText, size_t Size)
	{
		if(Format == 0)
			Request.Post(reinterpret_cast<const unsigned char *>(pText), Size);
		else if(Format == 1)
			Request.PostForm(reinterpret_cast<const unsigned char *>(pText), Size);
		else
			Request.PostJson(pText);
	}
}

TEST(HttpRequestBody, EachPostVariantRejectsAllocationFailureBeforeInit)
{
	for(int Format : {0, 1, 2})
	{
		SCOPED_TRACE(Format);
		CRequest Request;
		Request.m_Fail = true;
		Post(Request, Format, "payload", 7);
		EXPECT_EQ(Request.m_Allocations, 1);
		EXPECT_EQ(Request.m_LastSize, 7u);
		EXPECT_EQ(Request.BodyData(), nullptr);
		EXPECT_EQ(Request.BodyLength(), 7u);
		EXPECT_FALSE(Request.Ready());
		EXPECT_EQ(Request.State(), EHttpState::QUEUED);
	}
}

TEST(HttpRequestBody, FailedReplacementCannotReusePreviousBody)
{
	CRequest Request;
	Request.PostJson("old body");
	ASSERT_TRUE(Request.Ready());
	Request.m_Fail = true;
	Request.PostJson("new body");
	EXPECT_EQ(Request.BodyData(), nullptr);
	EXPECT_TRUE(Request.Body().empty());
	EXPECT_FALSE(Request.Ready());
}

TEST(HttpRequestBody, SuccessfulReplacementRecoversBeforeSubmission)
{
	CRequest Request;
	Request.m_Fail = true;
	Request.PostJson("failed");
	EXPECT_FALSE(Request.Ready());
	Request.m_Fail = false;
	Request.PostForm(reinterpret_cast<const unsigned char *>("replacement"), 11);
	EXPECT_TRUE(Request.Ready());
	EXPECT_EQ(Request.Body(), "replacement");
	EXPECT_FALSE(Request.IsAbortRequested());
}

TEST(HttpRequestBody, EmptyPostsOwnNonNullBodyWithZeroLength)
{
	for(int Format : {0, 1, 2})
	{
		SCOPED_TRACE(Format);
		CRequest Request;
		Post(Request, Format, "", 0);
		EXPECT_EQ(Request.m_LastSize, 1u);
		EXPECT_NE(Request.BodyData(), nullptr);
		EXPECT_EQ(Request.BodyLength(), 0u);
		EXPECT_TRUE(Request.Body().empty());
		EXPECT_TRUE(Request.Ready());
		EXPECT_STREQ(Request.Method(), "POST");
	}
}

TEST(HttpRequestBody, EmptyPostAllocationFailureIsRejected)
{
	for(int Format : {0, 1, 2})
	{
		SCOPED_TRACE(Format);
		CRequest Request;
		Request.m_Fail = true;
		Post(Request, Format, "", 0);
		EXPECT_EQ(Request.m_LastSize, 1u);
		EXPECT_FALSE(Request.Ready());
	}
}

TEST(HttpRequestBody, GetAndHeadDoNotNeedAllocatedBody)
{
	CRequest Request;
	EXPECT_TRUE(Request.Ready());
	Request.Head();
	EXPECT_TRUE(Request.Ready());
	EXPECT_EQ(Request.m_Allocations, 0);
}

TEST(HttpRequestBody, ExplicitHeadOverridesFailedPostWithoutBody)
{
	CRequest Request;
	Request.m_Fail = true;
	Request.PostJson("failed");
	EXPECT_FALSE(Request.Ready());
	Request.Head();
	EXPECT_TRUE(Request.Ready());
	EXPECT_STREQ(Request.Method(), "HEAD");
	EXPECT_EQ(Request.BodyData(), nullptr);
}

TEST(HttpRequestBody, BinaryPostOwnsOriginalBytesIncludingNul)
{
	CRequest Request;
	std::array<unsigned char, 4> aInput = {'a', 0, 'b', 0xff};
	Request.Post(aInput.data(), aInput.size());
	aInput.fill(0xee);
	EXPECT_EQ(Request.Body(), std::string("a\0b\xff", 4));
	EXPECT_TRUE(Request.Ready());
}
