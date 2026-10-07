#include <base/detect.h>
#if !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <engine/shared/http_curl.h>

#include <gtest/gtest.h>

TEST(QmUpdateHttp, RetryAfterSecondsAcceptsWhitespaceAndRejectsMalformedNumbers)
{
	EXPECT_EQ(CHttpRequestCurl::ParseRetryAfter("900\r\n", 0), 900);
	EXPECT_EQ(CHttpRequestCurl::ParseRetryAfter(" 30 \t", 0), 30);
	EXPECT_EQ(CHttpRequestCurl::ParseRetryAfter("0", 0), 0);
	for(const char *pValue : {"", " ", "-1", "12oops", "999999999999999999999999", "<html>"})
		EXPECT_FALSE(CHttpRequestCurl::ParseRetryAfter(pValue, 0).has_value()) << pValue;
}

TEST(QmUpdateHttp, RetryAfterHttpDateUsesRemainingTimeAndClampsPastDate)
{
	const char *pDate = "Wed, 21 Oct 2015 07:28:00 GMT";
	EXPECT_EQ(CHttpRequestCurl::ParseRetryAfter(pDate, 1445412420), 60);
	EXPECT_EQ(CHttpRequestCurl::ParseRetryAfter(pDate, 1445412600), 0);
}
#endif
