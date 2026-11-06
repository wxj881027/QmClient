#include <base/detect.h>

#include <engine/http.h>

#include <gtest/gtest.h>

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <curl/curl.h>

#include <array>
#include <memory>
#include <string>

TEST(HttpNativeUrlEscape, UnreservedAsciiIsUnchanged)
{
	char aBuffer[128];
	const char *pText = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~";
	EscapeUrl(aBuffer, pText);
	EXPECT_STREQ(aBuffer, pText);
}

TEST(HttpNativeUrlEscape, ReservedBytesUseUppercasePercentEncoding)
{
	char aBuffer[128];
	EscapeUrl(aBuffer, " :/?#&=+%'!()*[]@");
	EXPECT_STREQ(aBuffer, "%20%3A%2F%3F%23%26%3D%2B%25%27%21%28%29%2A%5B%5D%40");
}

TEST(HttpNativeUrlEscape, Utf8AndRawNonAsciiAreEncodedBytewise)
{
	char aBuffer[128];
	EscapeUrl(aBuffer, "玩家");
	EXPECT_STREQ(aBuffer, "%E7%8E%A9%E5%AE%B6");
	const char aBytes[] = {static_cast<char>(0xff), static_cast<char>(0x80), '\0'};
	EscapeUrl(aBuffer, aBytes);
	EXPECT_STREQ(aBuffer, "%FF%80");
}

TEST(HttpNativeUrlEscape, EmptyInputTerminatesTheOutput)
{
	char aBuffer[8] = "stale";
	EscapeUrl(aBuffer, "");
	EXPECT_STREQ(aBuffer, "");
}

TEST(HttpNativeUrlEscape, ZeroCapacityDoesNotAccessEitherBuffer)
{
	EscapeUrl(nullptr, 0, nullptr);
}

TEST(HttpNativeUrlEscape, SingleByteDestinationOnlyStoresTerminator)
{
	char aBuffer[1] = {'x'};
	EscapeUrl(aBuffer, "/long/name");
	EXPECT_EQ(aBuffer[0], '\0');
}

TEST(HttpNativeUrlEscape, TruncationKeepsTheSameEncodedPrefixIncludingPartialEscape)
{
	const std::string Expected = "a%2Fb%20c";
	for(size_t Capacity = 1; Capacity <= Expected.size() + 2; ++Capacity)
	{
		SCOPED_TRACE(Capacity);
		std::array<char, 16> aBuffer;
		aBuffer.fill('#');
		EscapeUrl(aBuffer.data() + 1, Capacity, "a/b c");
		EXPECT_EQ(std::string(aBuffer.data() + 1), Expected.substr(0, Capacity - 1));
		EXPECT_EQ(aBuffer.front(), '#');
		EXPECT_EQ(aBuffer[Capacity + 1], '#');
	}
}

TEST(HttpNativeUrlEscape, InPlaceExpansionDoesNotOverwriteUnreadInput)
{
	char aBuffer[32] = "a b/c";
	EscapeUrl(aBuffer, sizeof(aBuffer), aBuffer);
	EXPECT_STREQ(aBuffer, "a%20b%2Fc");
}

TEST(HttpNativeUrlEscape, OverlappingBuffersPreserveTheOriginalInputPrefix)
{
	char aBefore[32] = "xa b/c";
	EscapeUrl(aBefore, sizeof(aBefore), aBefore + 1);
	EXPECT_STREQ(aBefore, "a%20b%2Fc");
	char aAfter[32] = "a b/c";
	EscapeUrl(aAfter + 1, sizeof(aAfter) - 1, aAfter);
	EXPECT_STREQ(aAfter + 1, "a%20b%2Fc");
}

TEST(HttpNativeUrlEscape, EveryNonZeroByteMatchesTheLibcurlReference)
{
	std::string Input;
	for(int Byte = 1; Byte <= 255; ++Byte)
		Input.push_back(static_cast<char>(Byte));
	const std::unique_ptr<char, decltype(&curl_free)> pReference(curl_easy_escape(nullptr, Input.c_str(), 0), curl_free);
	ASSERT_NE(pReference, nullptr);
	std::array<char, 1024> aBuffer;
	EscapeUrl(aBuffer.data(), aBuffer.size(), Input.c_str());
	EXPECT_STREQ(aBuffer.data(), pReference.get());
}
#endif
