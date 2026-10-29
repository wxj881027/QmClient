#include <android/jni_server.h>

#include <gtest/gtest.h>
#include <test/support/android_jni_input.h>

#include <new>

TEST(AndroidJniServerString, EmptySpanProducesEmptyString)
{
	EXPECT_EQ(android_jni::Utf8(static_cast<const char16_t *>(nullptr), 0), "");
}

TEST(AndroidJniServerString, MissingCharactersAreRejected)
{
	EXPECT_FALSE(android_jni::Utf8(static_cast<const char16_t *>(nullptr), 1));
}

TEST(AndroidJniServerString, AsciiAndBmpUseStandardUtf8)
{
	const std::u16string Text = u"sv_name 中文";
	EXPECT_EQ(android_jni::Utf8(Text.data(), Text.size()), "sv_name 中文");
}

TEST(AndroidJniServerString, SupplementaryPairUsesFourByteUtf8)
{
	const std::u16string Text = u"\U0001f600\U0010ffff";
	EXPECT_EQ(android_jni::Utf8(Text.data(), Text.size()), "\xf0\x9f\x98\x80\xf4\x8f\xbf\xbf");
}

TEST(AndroidJniServerString, UnpairedSurrogatesAreReplacedWithoutDroppingNextCharacter)
{
	const char16_t aText[] = {0xd800, 'a', 0xdc00, 0xdbff};
	EXPECT_EQ(android_jni::Utf8(aText, 4), "\xef\xbf\xbd"
					       "a"
					       "\xef\xbf\xbd\xef\xbf\xbd");
}

TEST(AndroidJniServerString, EmbeddedNulRejectsWholeString)
{
	const char16_t aText[] = {'a', 0, 'b'};
	EXPECT_FALSE(android_jni::Utf8(aText, 3));
}

TEST(AndroidJniServerString, EmptyJavaStringDoesNotBorrowCharacters)
{
	CAndroidJniInput Input;
	Input.m_Directory->m_Text.clear();
	EXPECT_EQ(android_jni::ReadString(Input.m_Env, Input.m_Directory), "");
	EXPECT_EQ(Input.m_Env.m_Borrows, 0);
	EXPECT_EQ(Input.m_Env.m_Releases, 0);
}

TEST(AndroidJniServerString, NullJavaStringStopsBeforeReading)
{
	CAndroidJniEnv Env;
	EXPECT_FALSE(android_jni::ReadString(Env, CAndroidJniEnv::TObject{}));
	EXPECT_EQ(Env.m_ReadStrings, 0);
	EXPECT_EQ(Env.m_BadCalls, 0);
}

TEST(AndroidJniServerString, SuccessfulReadReleasesBorrowBeforeReturning)
{
	CAndroidJniInput Input;
	Input.m_Directory->m_Text = u"/目录/\U0001f600";
	EXPECT_EQ(android_jni::ReadString(Input.m_Env, Input.m_Directory), "/目录/\xf0\x9f\x98\x80");
	EXPECT_EQ(Input.m_Env.m_Borrows, 0);
	EXPECT_EQ(Input.m_Env.m_Releases, 1);
	EXPECT_EQ(Input.m_Env.m_BadCalls, 0);
}

TEST(AndroidJniServerString, RejectedNulAlsoReleasesBorrow)
{
	CAndroidJniInput Input;
	Input.m_Directory->m_Text.assign({u'a', 0, u'b'});
	EXPECT_FALSE(android_jni::ReadString(Input.m_Env, Input.m_Directory));
	EXPECT_EQ(Input.m_Env.m_Borrows, 0);
	EXPECT_EQ(Input.m_Env.m_Releases, 1);
}

TEST(AndroidJniServerString, BorrowGuardReleasesDuringCppUnwind)
{
	CAndroidJniInput Input;
	EXPECT_THROW(
		{
			const android_jni::CChars Chars(Input.m_Env, Input.m_Directory, Input.m_Env.GetStringChars(Input.m_Directory, nullptr));
			EXPECT_EQ(Input.m_Env.m_Borrows, 1);
			throw std::bad_alloc();
		},
		std::bad_alloc);
	EXPECT_EQ(Input.m_Env.m_Borrows, 0);
	EXPECT_EQ(Input.m_Env.m_Releases, 1);
	EXPECT_EQ(Input.m_Env.m_BadCalls, 0);
}
