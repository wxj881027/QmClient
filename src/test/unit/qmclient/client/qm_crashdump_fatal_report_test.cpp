#include <base/crashdump.h>
#include <base/str.h>

#include <gtest/gtest.h>

#include <csignal>
#include <map>
#include <string>

// 移动端崩溃报告：文本格式与栈回溯都是纯函数，不依赖真实信号，
// 因此可以在任意平台验证崩溃处理器写入磁盘的内容。

namespace
{
struct SFakeStack
{
	std::map<uintptr_t, uintptr_t> m_Values;
};

bool FakeReadMemory(uintptr_t Address, uintptr_t *pValue, void *pUser)
{
	SFakeStack *pStack = static_cast<SFakeStack *>(pUser);
	const auto It = pStack->m_Values.find(Address);
	if(It == pStack->m_Values.end())
		return false;
	*pValue = It->second;
	return true;
}

// 每层 frame pointer 记录上一层的 frame pointer 与返回地址，与 AAPCS64/x86-64 一致。
void PushFrame(SFakeStack &Stack, uintptr_t Frame, uintptr_t NextFrame, uintptr_t ReturnAddress)
{
	Stack.m_Values[Frame] = NextFrame;
	Stack.m_Values[Frame + sizeof(uintptr_t)] = ReturnAddress;
}

SCrashFatalReport MakeReport()
{
	SCrashFatalReport Report = {};
	Report.m_pProgramName = "QmClient";
	Report.m_pVersion = "3.13.0";
	Report.m_pPlatform = "android arm64";
	Report.m_pStartTime = "2026-11-03 19-12-00";
	Report.m_pGraphicsBackend = "Vulkan";
	Report.m_Pid = 4242;
	Report.m_Signal = SIGSEGV;
	Report.m_SignalCode = 1;
	Report.m_FaultAddress = 0x1234;
	Report.m_Pc = 0xabcdef;
	Report.m_Sp = 0x7000;
	Report.m_Fp = 0x8000;
	Report.m_Lr = 0x9999;
	return Report;
}
} // namespace

TEST(QmCrashdumpSignalName, KnownSignalsUseTheirCanonicalName)
{
	EXPECT_STREQ(crashdump_signal_name(SIGSEGV), "SIGSEGV");
	EXPECT_STREQ(crashdump_signal_name(SIGABRT), "SIGABRT");
	EXPECT_STREQ(crashdump_signal_name(SIGFPE), "SIGFPE");
	EXPECT_STREQ(crashdump_signal_name(SIGILL), "SIGILL");
}

TEST(QmCrashdumpSignalName, UnknownSignalDoesNotClaimAKnownName)
{
	EXPECT_STREQ(crashdump_signal_name(-1), "SIG?");
	EXPECT_STREQ(crashdump_signal_name(1234), "SIG?");
}

TEST(QmCrashdumpFatalReport, ReportCarriesSignalRegistersAndFaultAddress)
{
	const SCrashFatalReport Report = MakeReport();

	char aBuffer[2048];
	const size_t Length = crashdump_format_fatal_report(aBuffer, sizeof(aBuffer), Report);
	ASSERT_GT(Length, 0u);
	ASSERT_LT(Length, sizeof(aBuffer));
	EXPECT_EQ(aBuffer[Length], '\0');

	const std::string Text(aBuffer);
	EXPECT_NE(Text.find("QmClient fatal crash report"), std::string::npos);
	EXPECT_NE(Text.find("Version: 3.13.0"), std::string::npos);
	EXPECT_NE(Text.find("Platform: android arm64"), std::string::npos);
	EXPECT_NE(Text.find("Graphics backend: Vulkan"), std::string::npos);
	EXPECT_NE(Text.find("Started at: 2026-11-03 19-12-00"), std::string::npos);
	EXPECT_NE(Text.find("Process id: 4242"), std::string::npos);
	EXPECT_NE(Text.find("Signal: SIGSEGV"), std::string::npos);
	EXPECT_NE(Text.find("Fault address: 0x0000000000001234"), std::string::npos);
	EXPECT_NE(Text.find("pc 0x0000000000ABCDEF"), std::string::npos);
	EXPECT_NE(Text.find("lr 0x0000000000009999"), std::string::npos);
	EXPECT_NE(Text.find("sp 0x0000000000007000"), std::string::npos);
	EXPECT_NE(Text.find("fp 0x0000000000008000"), std::string::npos);
}

TEST(QmCrashdumpFatalReport, OptionalLinesAreOmittedWhenUnavailable)
{
	SCrashFatalReport Report = MakeReport();
	Report.m_pVersion = nullptr;
	Report.m_pGraphicsBackend = nullptr;

	char aBuffer[2048];
	const size_t Length = crashdump_format_fatal_report(aBuffer, sizeof(aBuffer), Report);
	ASSERT_GT(Length, 0u);

	const std::string Text(aBuffer);
	EXPECT_EQ(Text.find("Version:"), std::string::npos);
	EXPECT_EQ(Text.find("Graphics backend:"), std::string::npos);
	EXPECT_NE(Text.find("Platform: android arm64"), std::string::npos);
}

TEST(QmCrashdumpFatalReport, ShortBufferIsTruncatedAndStillTerminated)
{
	const SCrashFatalReport Report = MakeReport();

	char aBuffer[32];
	const size_t Length = crashdump_format_fatal_report(aBuffer, sizeof(aBuffer), Report);
	EXPECT_EQ(Length, sizeof(aBuffer) - 1);
	EXPECT_EQ(aBuffer[Length], '\0');
}

TEST(QmCrashdumpFatalReport, EmptyBufferIsRejectedWithoutWriting)
{
	const SCrashFatalReport Report = MakeReport();

	char aBuffer[8] = "keep";
	EXPECT_EQ(crashdump_format_fatal_report(aBuffer, 0, Report), 0u);
	EXPECT_EQ(aBuffer[0], 'k');
	EXPECT_EQ(crashdump_format_fatal_report(nullptr, 64, Report), 0u);
}

TEST(QmCrashdumpFrameChain, WalksEveryFrameUntilTheChainEnds)
{
	SFakeStack Stack;
	PushFrame(Stack, 0x8000, 0x8100, 0x1000);
	PushFrame(Stack, 0x8100, 0x8200, 0x2000);
	PushFrame(Stack, 0x8200, 0x8300, 0x3000);
	PushFrame(Stack, 0x8300, 0, 0);

	uintptr_t aFrames[16] = {};
	const size_t Count = crashdump_walk_frame_chain(FakeReadMemory, &Stack, 0x500, 0x8000, 0x7000, aFrames, 16);

	ASSERT_EQ(Count, 4u);
	EXPECT_EQ(aFrames[0], 0x500u);
	EXPECT_EQ(aFrames[1], 0x1000u);
	EXPECT_EQ(aFrames[2], 0x2000u);
	EXPECT_EQ(aFrames[3], 0x3000u);
}

TEST(QmCrashdumpFrameChain, StopsWhenFramePointerStopsGrowing)
{
	// 自环：上一层 frame pointer 等于当前值，说明链已损坏，不能无限循环。
	SFakeStack Stack;
	PushFrame(Stack, 0x8000, 0x8000, 0x1000);

	uintptr_t aFrames[16] = {};
	const size_t Count = crashdump_walk_frame_chain(FakeReadMemory, &Stack, 0x500, 0x8000, 0x7000, aFrames, 16);

	ASSERT_EQ(Count, 2u);
	EXPECT_EQ(aFrames[1], 0x1000u);
}

TEST(QmCrashdumpFrameChain, StopsAtEmptyReturnAddress)
{
	SFakeStack Stack;
	PushFrame(Stack, 0x8000, 0x8100, 0);
	PushFrame(Stack, 0x8100, 0x8200, 0x2000);

	uintptr_t aFrames[16] = {};
	const size_t Count = crashdump_walk_frame_chain(FakeReadMemory, &Stack, 0x500, 0x8000, 0x7000, aFrames, 16);

	ASSERT_EQ(Count, 1u);
	EXPECT_EQ(aFrames[0], 0x500u);
}

TEST(QmCrashdumpFrameChain, StopsAtUnreadableFrame)
{
	SFakeStack Stack;
	PushFrame(Stack, 0x8000, 0x8100, 0x1000);

	uintptr_t aFrames[16] = {};
	const size_t Count = crashdump_walk_frame_chain(FakeReadMemory, &Stack, 0x500, 0x8000, 0x7000, aFrames, 16);

	ASSERT_EQ(Count, 2u);
	EXPECT_EQ(aFrames[1], 0x1000u);
}

TEST(QmCrashdumpFrameChain, FramePointerBelowStackPointerIsNotTrusted)
{
	SFakeStack Stack;
	PushFrame(Stack, 0x8000, 0x8100, 0x1000);

	uintptr_t aFrames[16] = {};
	const size_t Count = crashdump_walk_frame_chain(FakeReadMemory, &Stack, 0x500, 0x8000, 0x9000, aFrames, 16);

	ASSERT_EQ(Count, 1u);
	EXPECT_EQ(aFrames[0], 0x500u);
}

TEST(QmCrashdumpFrameChain, MisalignedFramePointerIsRejected)
{
	SFakeStack Stack;
	PushFrame(Stack, 0x8004, 0x8100, 0x1000);

	uintptr_t aFrames[16] = {};
	const size_t Count = crashdump_walk_frame_chain(FakeReadMemory, &Stack, 0x500, 0x8004, 0x7000, aFrames, 16);

	ASSERT_EQ(Count, 1u);
}

TEST(QmCrashdumpFrameChain, RespectsTheFrameLimit)
{
	SFakeStack Stack;
	PushFrame(Stack, 0x8000, 0x8100, 0x1000);
	PushFrame(Stack, 0x8100, 0x8200, 0x2000);
	PushFrame(Stack, 0x8200, 0x8300, 0x3000);

	uintptr_t aFrames[2] = {};
	const size_t Count = crashdump_walk_frame_chain(FakeReadMemory, &Stack, 0x500, 0x8000, 0x7000, aFrames, 2);

	ASSERT_EQ(Count, 2u);
	EXPECT_EQ(aFrames[0], 0x500u);
	EXPECT_EQ(aFrames[1], 0x1000u);
}

TEST(QmCrashdumpFrameChain, MissingInputsYieldNoFrames)
{
	uintptr_t aFrames[4] = {};
	EXPECT_EQ(crashdump_walk_frame_chain(nullptr, nullptr, 0x500, 0x8000, 0x7000, aFrames, 4), 0u);
	EXPECT_EQ(crashdump_walk_frame_chain(FakeReadMemory, nullptr, 0x500, 0x8000, 0x7000, nullptr, 4), 0u);
	EXPECT_EQ(crashdump_walk_frame_chain(FakeReadMemory, nullptr, 0x500, 0x8000, 0x7000, aFrames, 0), 0u);
}

TEST(QmCrashdumpFrameLines, AppendsFramesAfterExistingContent)
{
	char aBuffer[512];
	str_copy(aBuffer, "header\n");
	const size_t HeaderLength = str_length(aBuffer);
	ASSERT_GT(HeaderLength, 0u);

	const uintptr_t aFrames[] = {0x1000, 0x2000};
	const size_t Length = crashdump_append_frame_lines(aBuffer, sizeof(aBuffer), HeaderLength, aFrames, 2);

	ASSERT_LT(Length, sizeof(aBuffer));
	EXPECT_EQ(aBuffer[Length], '\0');
	const std::string Text(aBuffer);
	EXPECT_EQ(Text.rfind("header\n", 0), 0u);
	EXPECT_NE(Text.find("Backtrace (2 frames"), std::string::npos);
	EXPECT_NE(Text.find("#00 0x0000000000001000"), std::string::npos);
	EXPECT_NE(Text.find("#01 0x0000000000002000"), std::string::npos);
}

TEST(QmCrashdumpFrameLines, NoFramesLeavesBufferUnchanged)
{
	char aBuffer[512];
	str_copy(aBuffer, "header\n");
	const size_t HeaderLength = str_length(aBuffer);
	ASSERT_GT(HeaderLength, 0u);

	const uintptr_t aFrames[] = {0x1000};
	const size_t Length = crashdump_append_frame_lines(aBuffer, sizeof(aBuffer), HeaderLength, aFrames, 0);

	EXPECT_EQ(Length, HeaderLength);
	EXPECT_STREQ(aBuffer, "header\n");
}

TEST(QmCrashdumpFrameLines, FullBufferIsNotOverrun)
{
	char aBuffer[16] = "0123456789abcde";
	const uintptr_t aFrames[] = {0x1000, 0x2000};
	const size_t Length = crashdump_append_frame_lines(aBuffer, sizeof(aBuffer), 10, aFrames, 2);

	EXPECT_LE(Length, sizeof(aBuffer));
	EXPECT_EQ(aBuffer[Length], '\0');
}
