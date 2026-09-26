/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information.                */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "test.h"

#include <engine/shared/config.h>

#include <game/client/components/jump_hint_utils.h>

#include <gtest/gtest.h>

// 跳跃提示工具的纯逻辑：默认文案 → 一次性迁移，以及配置里「字面 \n」与真实换行的编解码。
// 这三块都不依赖客户端状态，直接调用生产函数验证输入输出。

TEST(QmJumpHint, DefaultsAreChineseAndDisabled)
{
	EXPECT_EQ(DefaultConfig::QmJumpHint, 0);
	EXPECT_EQ(DefaultConfig::QmJumpHintDefaultsMigrated, 0);
	EXPECT_STREQ(DefaultConfig::QmJumpHintText, "三格边缘跳:\\n左起跳: .34|.31|.16\\n左二段跳: .41|.28|.25|.13\\n右起跳: .63|.66|.81\\n右二段跳: .56|.69|.72|.84");
	// 配置默认值必须与模块里的常量是同一份文案，否则迁移会把用户带到另一个默认。
	EXPECT_STREQ(JUMP_HINT_DEFAULT_TEXT, DefaultConfig::QmJumpHintText);
}

TEST(QmJumpHint, UpgradeReplacesOriginalEnglishAndDisablesOnce)
{
	int Migrated = 0;
	int Enabled = 1;
	char aText[512] = "3 Tiles Edge Jump:\\nLeft Jump: .34|.31|.16\\nLeft Double Jump: .41|.28|.25|.13\\nRight Jump: .63|.66|.81\\nRight Double Jump: .56|.69|.72|.84";
	MigrateJumpHintDefaults(Migrated, Enabled, aText, sizeof(aText));
	EXPECT_EQ(Migrated, 1);
	EXPECT_EQ(Enabled, 0);
	EXPECT_STREQ(aText, JUMP_HINT_DEFAULT_TEXT);

	// 用户重新开启后，后续启动不得再次关闭。
	Enabled = 1;
	MigrateJumpHintDefaults(Migrated, Enabled, aText, sizeof(aText));
	EXPECT_EQ(Enabled, 1);
	EXPECT_STREQ(aText, JUMP_HINT_DEFAULT_TEXT);
}

TEST(QmJumpHint, UpgradePreservesCustomTextIncludingModifiedEnglish)
{
	for(const char *pText : {"我的三跳提示\\n保留这一行", "3 Tiles Edge Jump:\\nLeft Jump: .34", ""})
	{
		int Migrated = 0;
		int Enabled = 1;
		char aText[512];
		str_copy(aText, pText, sizeof(aText));
		MigrateJumpHintDefaults(Migrated, Enabled, aText, sizeof(aText));
		EXPECT_EQ(Migrated, 1);
		EXPECT_EQ(Enabled, 0);
		EXPECT_STREQ(aText, pText);
	}
}

TEST(QmJumpHint, CompletedMigrationPreservesSubsequentUserChoices)
{
	int Migrated = 1;
	int Enabled = 1;
	char aText[512] = "3 Tiles Edge Jump:\\nLeft Jump: .34|.31|.16\\nLeft Double Jump: .41|.28|.25|.13\\nRight Jump: .63|.66|.81\\nRight Double Jump: .56|.69|.72|.84";
	const std::string UserText = aText;
	MigrateJumpHintDefaults(Migrated, Enabled, aText, sizeof(aText));
	EXPECT_EQ(Enabled, 1);
	EXPECT_STREQ(aText, UserText.c_str());
}

TEST(QmJumpHintEscapes, DecodeTurnsLiteralBackslashNIntoRealNewline)
{
	char aBuf[64];
	DecodeEscapedNewlines("a\\nb", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "a\nb");

	// 单独的反斜杠不构成转义，原样保留。
	DecodeEscapedNewlines("a\\b", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "a\\b");
	// 末尾的孤立反斜杠不能越界读取。
	DecodeEscapedNewlines("a\\", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "a\\");
	// 真实换行本来就存在时保持原样，不会被再转义一次。
	DecodeEscapedNewlines("a\nb", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "a\nb");
	// nullptr 输入等价于空串。
	DecodeEscapedNewlines(nullptr, aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "");
}

TEST(QmJumpHintEscapes, EncodeTurnsRealNewlineIntoLiteralBackslashNAndDropsCarriageReturn)
{
	char aBuf[64];
	EncodeEscapedNewlines("a\nb", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "a\\nb");

	// Windows 换行只保留 \n 的字面量，\r 被丢弃。
	EncodeEscapedNewlines("a\r\nb", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "a\\nb");

	EncodeEscapedNewlines("plain", aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "plain");
	EncodeEscapedNewlines(nullptr, aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "");
}

TEST(QmJumpHintEscapes, RoundTripThroughEncodeThenDecodeRestoresTheText)
{
	const char *apInputs[] = {"", "plain", "a\nb", "line1\nline2\nline3", "trailing\n"};
	for(const char *pInput : apInputs)
	{
		char aEncoded[128];
		char aDecoded[128];
		EncodeEscapedNewlines(pInput, aEncoded, sizeof(aEncoded));
		DecodeEscapedNewlines(aEncoded, aDecoded, sizeof(aDecoded));
		EXPECT_STREQ(aDecoded, pInput);
	}
}

TEST(QmJumpHintEscapes, CodecAlwaysNulTerminatesAndNeverOverflowsTheBuffer)
{
	// 缓冲区刚好放得下内容加终止符。
	char aTight[4];
	DecodeEscapedNewlines("abc", aTight, sizeof(aTight));
	EXPECT_STREQ(aTight, "abc");

	// 容量不足时截断并仍然以 NUL 结尾；尾部必须留在缓冲区里。
	char aSmall[3];
	memset(aSmall, 'X', sizeof(aSmall));
	DecodeEscapedNewlines("abcdef", aSmall, sizeof(aSmall));
	EXPECT_EQ(aSmall[sizeof(aSmall) - 1], '\0');
	EXPECT_STREQ(aSmall, "ab");

	char aEncodeSmall[3];
	memset(aEncodeSmall, 'X', sizeof(aEncodeSmall));
	EncodeEscapedNewlines("ab\ncd", aEncodeSmall, sizeof(aEncodeSmall));
	EXPECT_EQ(aEncodeSmall[sizeof(aEncodeSmall) - 1], '\0');

	// OutputSize 为 0 时完全不写。
	char aUntouched[2] = {'X', 'Y'};
	DecodeEscapedNewlines("abc", aUntouched, 0);
	EXPECT_EQ(aUntouched[0], 'X');
	EXPECT_EQ(aUntouched[1], 'Y');
	EncodeEscapedNewlines("abc", aUntouched, 0);
	EXPECT_EQ(aUntouched[0], 'X');
	EXPECT_EQ(aUntouched[1], 'Y');
}
