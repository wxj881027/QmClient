#include <engine/client/input_sdl3.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>

namespace
{
	// 只模拟 SDL 拥有的原生缓冲及释放边界，复制与异常收尾走生产接口。
	class CNativeClipboardBuffer
	{
	public:
		char *m_pText;
		int m_Releases = 0;

		explicit CNativeClipboardBuffer(const std::string &Text) :
			m_pText(static_cast<char *>(std::malloc(Text.size() + 1)))
		{
			if(m_pText)
				std::memcpy(m_pText, Text.c_str(), Text.size() + 1);
		}
		~CNativeClipboardBuffer() { std::free(m_pText); }
		CNativeClipboardBuffer(const CNativeClipboardBuffer &) = delete;
		CNativeClipboardBuffer &operator=(const CNativeClipboardBuffer &) = delete;

		void Release(char *pText) noexcept
		{
			++m_Releases;
			std::free(pText);
			m_pText = nullptr;
		}
	};
}

TEST(SdlClipboardText, CopiesBeforeReleasingTheNativeBuffer)
{
	CNativeClipboardBuffer Native("clipboard text");
	ASSERT_NE(Native.m_pText, nullptr);
	const std::string Result = TakeSdlClipboardText(Native.m_pText, [&](char *pText) { Native.Release(pText); }, [&](const char *pText) {
			EXPECT_EQ(Native.m_Releases, 0);
			return std::string(pText); });
	EXPECT_EQ(Result, "clipboard text");
	EXPECT_EQ(Native.m_Releases, 1);
}

TEST(SdlClipboardText, PreservesMultibyteTextAndLineBreaks)
{
	const std::string Text = "中文🙂\r\n第二行\tend";
	CNativeClipboardBuffer Native(Text);
	ASSERT_NE(Native.m_pText, nullptr);
	const std::string Result = TakeSdlClipboardText(Native.m_pText, [&](char *pText) { Native.Release(pText); }, [](const char *pText) { return std::string(pText); });
	EXPECT_EQ(Result, Text);
	EXPECT_EQ(Native.m_Releases, 1);
}

TEST(SdlClipboardText, CopiesLongTextWithoutTruncatingIt)
{
	const std::string Text(8192, 'x');
	CNativeClipboardBuffer Native(Text);
	ASSERT_NE(Native.m_pText, nullptr);
	const std::string Result = TakeSdlClipboardText(Native.m_pText, [&](char *pText) { Native.Release(pText); }, [](const char *pText) { return std::string(pText); });
	EXPECT_EQ(Result, Text);
	EXPECT_EQ(Native.m_Releases, 1);
}

TEST(SdlClipboardText, EmptyNativeTextIsStillReleased)
{
	CNativeClipboardBuffer Native("");
	ASSERT_NE(Native.m_pText, nullptr);
	EXPECT_TRUE(TakeSdlClipboardText(Native.m_pText, [&](char *pText) { Native.Release(pText); }, [](const char *pText) { return std::string(pText); }).empty());
	EXPECT_EQ(Native.m_Releases, 1);
}

TEST(SdlClipboardText, NullNativeResultReturnsEmptyWithoutCopying)
{
	int Copies = 0;
	int Releases = 0;
	EXPECT_TRUE(TakeSdlClipboardText(nullptr, [&](char *) { ++Releases; }, [&](const char *pText) {
			++Copies;
			return std::string(pText); }).empty());
	EXPECT_EQ(Copies, 0);
	EXPECT_EQ(Releases, 0);
}

TEST(SdlClipboardText, CopyExceptionsReturnEmptyAndReleaseTheNativeBuffer)
{
	for(int Kind : {0, 1, 2})
	{
		SCOPED_TRACE(Kind);
		CNativeClipboardBuffer Native("owned text");
		ASSERT_NE(Native.m_pText, nullptr);
		std::string Result;
		EXPECT_NO_THROW(Result = TakeSdlClipboardText(Native.m_pText, [&](char *pText) { Native.Release(pText); }, [Kind](const char *) -> std::string {
				switch(Kind)
				{
				case 0: throw std::bad_alloc();
				case 1: throw std::length_error("clipboard copy");
				default: throw 7;
				} }));
		EXPECT_TRUE(Result.empty());
		EXPECT_EQ(Native.m_Releases, 1);
	}
}

TEST(SdlClipboardText, FailedCopyDoesNotPreventTheNextRead)
{
	CNativeClipboardBuffer Failed("first");
	ASSERT_NE(Failed.m_pText, nullptr);
	EXPECT_TRUE(TakeSdlClipboardText(Failed.m_pText, [&](char *pText) { Failed.Release(pText); }, [](const char *) -> std::string { throw std::bad_alloc(); }).empty());
	EXPECT_EQ(Failed.m_Releases, 1);

	CNativeClipboardBuffer Next("next clipboard");
	ASSERT_NE(Next.m_pText, nullptr);
	const std::string Result = TakeSdlClipboardText(Next.m_pText, [&](char *pText) { Next.Release(pText); }, [](const char *pText) { return std::string(pText); });
	EXPECT_EQ(Result, "next clipboard");
	EXPECT_EQ(Next.m_Releases, 1);
}
