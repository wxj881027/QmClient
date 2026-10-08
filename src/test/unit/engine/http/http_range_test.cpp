#include <engine/shared/http_range.h>

#include <gtest/gtest.h>

TEST(HttpContentRange, ValidRangePreservesOffsetsAndLargeTotal)
{
	const auto Range = ParseHttpContentRange(" bytes 1048576-2097151/5368709120\r\n");
	ASSERT_TRUE(Range);
	EXPECT_EQ(Range->m_First, 1048576);
	EXPECT_EQ(Range->m_Last, 2097151);
	EXPECT_EQ(Range->Length(), 1048576);
	EXPECT_EQ(Range->m_Total, 5368709120LL);
}

TEST(HttpContentRange, InvalidOrUnknownRangesCannotAuthorizeAssembly)
{
	for(const char *pValue : {"", "bytes */100", "bytes 0-1/*", "bytes 2-1/3", "bytes 0-3/3", "bytes -1-2/3", "bytes 0-1/2 garbage", "bytes 0-1/9223372036854775808", "bytes 0 -1/2"})
	{
		SCOPED_TRACE(pValue);
		EXPECT_FALSE(ParseHttpContentRange(pValue));
	}
}
