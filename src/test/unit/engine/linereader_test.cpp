// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/fs.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/shared/linereader.h>

#include <gtest/gtest.h>
#include <test/test.h>

CLineReader::EOpenResult TestFileLineReaderRaw(const char *pWritten, unsigned WrittenLength, std::initializer_list<const char *> pExpectedLines, bool ExpectSuccess, bool WriteBom, bool RejectInvalidLines = false)
{
	CTestInfo Info;
	IOHANDLE File = io_open(Info.m_aFilename, IOFLAG_WRITE);
	EXPECT_TRUE(File);
	if(!File)
		return CLineReader::EOpenResult::CANNOT_READ;
	if(WriteBom)
	{
		constexpr const unsigned char UTF8_BOM[] = {0xEF, 0xBB, 0xBF};
		EXPECT_EQ(io_write(File, UTF8_BOM, sizeof(UTF8_BOM)), sizeof(UTF8_BOM));
	}
	EXPECT_EQ(io_write(File, pWritten, WrittenLength), WrittenLength);
	EXPECT_FALSE(io_close(File));

	CLineReader LineReader;
	const bool ActualSuccess = LineReader.OpenFile(io_open(Info.m_aFilename, IOFLAG_READ), RejectInvalidLines);
	EXPECT_EQ(ActualSuccess, ExpectSuccess);
	if(ActualSuccess)
	{
		bool AllExpectedLinesRead = true;
		for(const char *pExpectedLine : pExpectedLines)
		{
			const char *pActualLine = LineReader.Get();
			if(pActualLine == nullptr)
			{
				ADD_FAILURE() << "Line reader returned less lines than expected. Expected next line: '" << pExpectedLine << "'";
				AllExpectedLinesRead = false;
				break;
			}
			EXPECT_STREQ(pActualLine, pExpectedLine) << "Line reader returned unexpected line";
		}
		if(AllExpectedLinesRead)
		{
			const char *pActualLastLine = LineReader.Get();
			EXPECT_FALSE(pActualLastLine) << "Line reader returned more lines than expected. Unexpected last line: '" << pActualLastLine << "'";
		}
	}
	const CLineReader::EOpenResult Result = LineReader.LastOpenResult();

	EXPECT_FALSE(fs_remove(Info.m_aFilename));
	return Result;
}

CLineReader::EOpenResult TestFileLineReaderRaw(const char *pWritten, unsigned WrittenLength, std::initializer_list<const char *> pReads, bool ExpectSuccess)
{
	const CLineReader::EOpenResult Result = TestFileLineReaderRaw(pWritten, WrittenLength, pReads, ExpectSuccess, false);
	TestFileLineReaderRaw(pWritten, WrittenLength, pReads, ExpectSuccess, true);
	return Result;
}

void TestFileLineReader(const char *pWritten, std::initializer_list<const char *> pReads)
{
	TestFileLineReaderRaw(pWritten, str_length(pWritten), pReads, true);
}

TEST(LineReader, LineFeedLineEndings)
{
	TestFileLineReader("foo\nbar\nbaz", {"foo", "bar", "baz"});
	TestFileLineReader("foo\nbar\nbaz\n", {"foo", "bar", "baz"});
}

TEST(LineReader, CarriageReturnLineFeedLineEndings)
{
	TestFileLineReader("foo\r\nbar\r\nbaz", {"foo", "bar", "baz"});
	TestFileLineReader("foo\r\nbar\r\nbaz\r\n", {"foo", "bar", "baz"});
}

TEST(LineReader, CarriageReturnLineEndings)
{
	// Line ending `\r` not supported
	TestFileLineReader("foo\rbar\rbaz", {});
	TestFileLineReader("foo\rbar\rbaz\r", {});
}

TEST(LineReader, MixedLineEndings)
{
	TestFileLineReader("1\n2\r\n3\n4\n5\r\n6", {"1", "2", "3", "4", "5", "6"});
	TestFileLineReader("1\n2\r\n3\n4\n5\r\n6\n", {"1", "2", "3", "4", "5", "6"});
	TestFileLineReader("1\n2\r\n3\n4\n5\r\n6\r\n", {"1", "2", "3", "4", "5", "6"});
	TestFileLineReader("1\n2\r\n3\n4\n5\r\n6\r", {"1", "2", "3", "4", "5"}); // Line with trailing `\r` is skipped
}

TEST(LineReader, EmptyLines)
{
	TestFileLineReader("\n\r\n\n\n\r\n", {"", "", "", "", ""});
	TestFileLineReader("\n\r\n\n\n\r\n\n", {"", "", "", "", "", ""});
	TestFileLineReader("\n\r\n\n\n\r\n\r\n", {"", "", "", "", "", ""});
	TestFileLineReader("\n\r\n\n\n\r\n\r", {"", "", "", "", ""}); // Line with trailing `\r` is skipped
}

TEST(LineReader, InvalidUtf8)
{
	// Lines containing invalid UTF-8 are skipped
	TestFileLineReader("foo\xff\nbar\xff\nbaz\xff", {});
	TestFileLineReader("foo\xff\nbar\nbaz", {"bar", "baz"});
	TestFileLineReader("foo\nbar\xff\nbaz", {"foo", "baz"});
	TestFileLineReader("foo\nbar\nbaz\xff", {"foo", "bar"});
	TestFileLineReader("foo\nbar1\xff\nbar2\xff\nfoobar\nbar3\xff\nbaz", {"foo", "foobar", "baz"});
}

TEST(LineReader, ControlCharacters)
{
	// Lines containing control characters except `\t` are skipped
	TestFileLineReader(
		"\x01\n\x02\n\x03\n\x04\n\x05\n\x06\n\x07\n\x08\n\x09\n\x0B\n\x0C\n\x0E\n\x0F\n\x10\n" // `\0x0A` and `\0x0D` are `\n` and `\r`
		"\x11\n\x12\n\x13\n\x14\n\x15\n\x16\n\x17\n\x18\n\x19\n\x1A\n\x1B\n\x1C\n\x1D\n\x1E\n\x1F",
		{"\t"});
}

TEST(LineReader, NullBytes)
{
	// 含 NUL 又不是 UTF-16 布局的文件整份拒绝，并报“编码不支持”而不是“打不开”。
	EXPECT_EQ(TestFileLineReaderRaw("foo\0\nbar\nbaz", 12, {}, false, false), CLineReader::EOpenResult::UNSUPPORTED_ENCODING);
	EXPECT_EQ(TestFileLineReaderRaw("foo\nbar\0\nbaz", 12, {}, false, false), CLineReader::EOpenResult::UNSUPPORTED_ENCODING);
	EXPECT_EQ(TestFileLineReaderRaw("foo\nbar\nbaz\0", 12, {}, false, false), CLineReader::EOpenResult::UNSUPPORTED_ENCODING);
}

TEST(LineReader, MissingFileIsReportedAsUnreadable)
{
	CLineReader LineReader;
	EXPECT_FALSE(LineReader.OpenFile(nullptr));
	EXPECT_EQ(LineReader.LastOpenResult(), CLineReader::EOpenResult::CANNOT_READ);
}

TEST(LineReader, Utf16LittleEndianWithBomIsDecoded)
{
	// 记事本“Unicode”默认产物：FF FE + 小端码元。
	const char aScript[] = "\xFF\xFE"
			       "f\0o\0o\0\n\0b\0a\0r\0";
	TestFileLineReaderRaw(aScript, sizeof(aScript) - 1, {"foo", "bar"}, true, false);
}

TEST(LineReader, Utf16BigEndianWithBomIsDecoded)
{
	const char aScript[] = "\xFE\xFF"
			       "\0f\0o\0o\0\n\0b\0a\0r";
	TestFileLineReaderRaw(aScript, sizeof(aScript) - 1, {"foo", "bar"}, true, false);
}

TEST(LineReader, Utf16WithoutBomIsDetectedFromNullBytes)
{
	// 不写 BOM 的小端 UTF-16 靠 NUL 分布识别。
	const char aScript[] = "f\0o\0o\0\n\0b\0a\0r\0";
	TestFileLineReaderRaw(aScript, sizeof(aScript) - 1, {"foo", "bar"}, true, false);
}

TEST(LineReader, Utf16SurrogatePairIsDecoded)
{
	// U+1D11E（𝄞）在 UTF-16 中是一对代理项，必须合并成一个码点。
	const char aScript[] = "\xFF\xFE"
			       "\x34\xD8\x1E\xDD";
	TestFileLineReaderRaw(aScript, sizeof(aScript) - 1, {"\xF0\x9D\x84\x9E"}, true, false);
}

TEST(LineReader, Utf16UnpairedSurrogateBecomesReplacementCharacter)
{
	// 坏代理项换成 U+FFFD，行仍是合法 UTF-8，不整份拒绝。
	const char aScript[] = "\xFF\xFE"
			       "\x00\xD8"
			       "o\0k\0";
	TestFileLineReaderRaw(aScript, sizeof(aScript) - 1, {"\xEF\xBF\xBDok"}, true, false);
}

TEST(LineReader, Utf32BomIsRejectedAsUnsupportedEncoding)
{
	const char aLittleEndian[] = "\xFF\xFE\0\0"
				     "f\0\0\0";
	EXPECT_EQ(TestFileLineReaderRaw(aLittleEndian, sizeof(aLittleEndian) - 1, {}, false, false), CLineReader::EOpenResult::UNSUPPORTED_ENCODING);
	const char aBigEndian[] = "\0\0\xFE\xFF"
				  "\0\0\0f";
	EXPECT_EQ(TestFileLineReaderRaw(aBigEndian, sizeof(aBigEndian) - 1, {}, false, false), CLineReader::EOpenResult::UNSUPPORTED_ENCODING);
}

TEST(LineReader, StrictLoadAcceptsUtf16Script)
{
	const char aScript[] = "\xFF\xFE"
			       "f\0o\0o\0\n\0b\0a\0r\0";
	TestFileLineReaderRaw(aScript, sizeof(aScript) - 1, {"foo", "bar"}, true, false, true);
}

TEST(LineReader, StrictLoadRejectsInvalidUtf8BeforeReadingValidPrefix)
{
	TestFileLineReaderRaw("foo\nbar\xff", 8, {}, false, false, true);
}

TEST(LineReader, StrictLoadRejectsControlCharacter)
{
	TestFileLineReaderRaw("foo\nbar\x01", 8, {}, false, false, true);
}

TEST(LineReader, StrictLoadRejectsUnpairedCarriageReturn)
{
	TestFileLineReaderRaw("foo\rbar", 7, {}, false, false, true);
}

TEST(LineReader, StrictLoadAcceptsBomAndMixedValidLineEndings)
{
	TestFileLineReaderRaw("foo\r\n\tbar\nbaz", 13, {"foo", "\tbar", "baz"}, true, true, true);
}
