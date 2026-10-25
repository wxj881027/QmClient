#include <engine/shared/http_response_headers.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	class HttpResponseHeaders : public ::testing::Test
	{
	protected:
		std::string m_Text = "Date: Tue, 01 Jan 2030 00:00:00 GMT\r\nLast-Modified: Mon, 31 Dec 2029 23:00:00 GMT\r\n";
		std::vector<std::pair<std::string, std::string>> m_vHeaders;
		int m_Allocations = 0;
		int m_Reads = 0;
		bool m_FailAllocation = false;
		bool ReadReported(size_t ReportedLength)
		{
			return ReadHttpResponseHeaders(ReportedLength, [this](char *pBuffer, size_t Capacity) {
					++m_Reads;
					if(m_Text.size() + 1 > Capacity)
						return size_t{0};
					std::memcpy(pBuffer, m_Text.c_str(), m_Text.size() + 1);
					return m_Text.size() + 1; }, [this](const char *pName, const char *pValue) { m_vHeaders.emplace_back(pName, pValue); }, [this](size_t Size) -> void * {
					++m_Allocations;
					return m_FailAllocation ? nullptr : std::malloc(Size); });
		}
		bool Read() { return ReadReported(m_Text.size() + 1); }
	};
}

TEST_F(HttpResponseHeaders, ReportedLengthIncludingTerminatorAcceptsActualSdkByteCount)
{
	ASSERT_TRUE(Read());
	ASSERT_EQ(m_vHeaders.size(), 2u);
	EXPECT_EQ(m_vHeaders[0], std::make_pair(std::string("Date"), std::string(" Tue, 01 Jan 2030 00:00:00 GMT")));
	EXPECT_EQ(m_vHeaders[1].first, "Last-Modified");
	EXPECT_EQ(m_vHeaders[1].second, " Mon, 31 Dec 2029 23:00:00 GMT");
	EXPECT_EQ(m_Reads, 1);
}

TEST_F(HttpResponseHeaders, ReportedLengthExcludingTerminatorStillAllowsTerminatedRead)
{
	ASSERT_TRUE(ReadReported(m_Text.size()));
	ASSERT_EQ(m_vHeaders.size(), 2u);
	EXPECT_EQ(m_vHeaders.front().first, "Date");
}

TEST_F(HttpResponseHeaders, EmptySdkHeaderBlockContainingOnlyTerminatorSucceeds)
{
	m_Text.clear();
	EXPECT_TRUE(Read());
	EXPECT_TRUE(m_vHeaders.empty());
	EXPECT_EQ(m_Reads, 1);
}

TEST_F(HttpResponseHeaders, UnavailableHeadersNeedNoAllocationOrRead)
{
	EXPECT_TRUE(ReadReported(0));
	EXPECT_EQ(m_Allocations, 0);
	EXPECT_EQ(m_Reads, 0);
	EXPECT_TRUE(m_vHeaders.empty());
}

TEST_F(HttpResponseHeaders, AllocationFailureDoesNotReadOrVisitNullStorage)
{
	m_FailAllocation = true;
	EXPECT_FALSE(Read());
	EXPECT_EQ(m_Allocations, 1);
	EXPECT_EQ(m_Reads, 0);
	EXPECT_TRUE(m_vHeaders.empty());
}

TEST_F(HttpResponseHeaders, TerminatorCapacityOverflowIsRejectedBeforeAllocation)
{
	EXPECT_FALSE(ReadReported(std::numeric_limits<size_t>::max()));
	EXPECT_EQ(m_Allocations, 0);
	EXPECT_EQ(m_Reads, 0);
	EXPECT_TRUE(m_vHeaders.empty());
}

TEST(HttpResponseHeaderReadFailure, ZeroBytesFromReaderRejectsWithoutParsingUninitializedMemory)
{
	int Visits = 0;
	EXPECT_FALSE(ReadHttpResponseHeaders(8, [](char *, size_t) { return size_t{0}; }, [&](const char *, const char *) { ++Visits; }));
	EXPECT_EQ(Visits, 0);
}

TEST(HttpResponseHeaderReadFailure, ShortTerminatedReadCannotPublishPartialHeaders)
{
	int Visits = 0;
	EXPECT_FALSE(ReadHttpResponseHeaders(16, [](char *pBuffer, size_t) {
			std::memcpy(pBuffer, "X:a\0", 4);
			return size_t{4}; }, [&](const char *, const char *) { ++Visits; }));
	EXPECT_EQ(Visits, 0);
}

TEST(HttpResponseHeaderReadFailure, ReportedReadBeyondCapacityIsRejectedBeforeAccess)
{
	int Visits = 0;
	EXPECT_FALSE(ReadHttpResponseHeaders(8, [](char *, size_t Capacity) { return Capacity + 1; }, [&](const char *, const char *) { ++Visits; }));
	EXPECT_EQ(Visits, 0);
}

TEST(HttpResponseHeaderReadFailure, MissingTerminatorRejectsTheRead)
{
	int Visits = 0;
	EXPECT_FALSE(ReadHttpResponseHeaders(8, [](char *pBuffer, size_t Capacity) {
			std::memset(pBuffer, 'x', Capacity);
			return Capacity; }, [&](const char *, const char *) { ++Visits; }));
	EXPECT_EQ(Visits, 0);
}

TEST_F(HttpResponseHeaders, EmbeddedNullDoesNotHideTrailingHeaders)
{
	constexpr char aHeaders[] = "Date: first\0Last-Modified: hidden\r\n";
	m_Text.assign(aHeaders, sizeof(aHeaders) - 1);
	EXPECT_FALSE(Read());
	EXPECT_TRUE(m_vHeaders.empty());
}

TEST_F(HttpResponseHeaders, MissingNameValueSeparatorRejectsMalformedHeader)
{
	m_Text = "malformed header\r\n";
	EXPECT_FALSE(Read());
	EXPECT_TRUE(m_vHeaders.empty());
}

TEST_F(HttpResponseHeaders, EmptyHeaderNameIsRejected)
{
	m_Text = ":value\r\n";
	EXPECT_FALSE(Read());
	EXPECT_TRUE(m_vHeaders.empty());
}

TEST_F(HttpResponseHeaders, LineEndingsAndFinalLineWithoutNewlinePreserveHeader)
{
	for(const char *pText : {"\nName:value\n\n", "\r\nName:value\r\n\r\n", "Name:value"})
	{
		SCOPED_TRACE(pText);
		m_Text = pText;
		m_vHeaders.clear();
		ASSERT_TRUE(Read());
		ASSERT_EQ(m_vHeaders.size(), 1u);
		EXPECT_EQ(m_vHeaders.front(), std::make_pair(std::string("Name"), std::string("value")));
	}
}

TEST_F(HttpResponseHeaders, FirstColonSeparatesNameWhileEmptyAndColonContainingValuesRemainIntact)
{
	m_Text = "Location:https://example.test:8443/path\r\nEmpty:\r\n";
	ASSERT_TRUE(Read());
	ASSERT_EQ(m_vHeaders.size(), 2u);
	EXPECT_EQ(m_vHeaders[0].second, "https://example.test:8443/path");
	EXPECT_EQ(m_vHeaders[1], std::make_pair(std::string("Empty"), std::string()));
}

TEST_F(HttpResponseHeaders, RepeatedHeadersKeepOriginalOrderAndValueWhitespace)
{
	m_Text = "Date: first \r\nDate:second\r\n";
	ASSERT_TRUE(Read());
	ASSERT_EQ(m_vHeaders.size(), 2u);
	EXPECT_EQ(m_vHeaders[0].second, " first ");
	EXPECT_EQ(m_vHeaders[1].second, "second");
}

TEST(HttpResponseHeaderReadFailure, ReaderExceptionReturnsLocalFailure)
{
	int Visits = 0;
	EXPECT_FALSE(ReadHttpResponseHeaders(8, [](char *, size_t) -> size_t { throw std::bad_alloc(); }, [&](const char *, const char *) { ++Visits; }));
	EXPECT_EQ(Visits, 0);
}

TEST(HttpResponseHeaderReadFailure, VisitorExceptionReturnsLocalFailure)
{
	int Visits = 0;
	EXPECT_FALSE(ReadHttpResponseHeaders(4, [](char *pBuffer, size_t) {
			std::memcpy(pBuffer, "X:a\0", 4);
			return size_t{4}; }, [&](const char *, const char *) {
			++Visits;
			throw std::runtime_error("header consumer failed"); }));
	EXPECT_EQ(Visits, 1);
}

TEST_F(HttpResponseHeaders, NewReadAfterAllocationFailureCanParseNormally)
{
	m_FailAllocation = true;
	ASSERT_FALSE(Read());
	m_FailAllocation = false;
	ASSERT_TRUE(Read());
	EXPECT_EQ(m_vHeaders.size(), 2u);
}

TEST_F(HttpResponseHeaders, ManyHeadersUseOneOwnedAllocationInsteadOfPerFieldAllocations)
{
	m_Text.clear();
	for(int Index = 0; Index < 32; ++Index)
		m_Text += "Name:value\r\n";
	ASSERT_TRUE(Read());
	EXPECT_EQ(m_vHeaders.size(), 32u);
	EXPECT_EQ(m_Allocations, 1);
	EXPECT_EQ(m_Reads, 1);
}
