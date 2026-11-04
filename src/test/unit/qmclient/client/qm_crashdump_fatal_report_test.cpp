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

TEST(QmCrashdumpModules, FindsTheModuleContainingTheAddress)
{
	SCrashModuleRange aModules[2] = {};
	aModules[0].m_Start = 0x1000;
	aModules[0].m_End = 0x2000;
	str_copy(aModules[0].m_aName, "libfoo.so");
	aModules[1].m_Start = 0x8000;
	aModules[1].m_End = 0x9000;
	str_copy(aModules[1].m_aName, "libDDNet.so");

	uintptr_t Offset = 0;
	const char *pName = crashdump_find_module(aModules, 2, 0x8fff, &Offset);
	ASSERT_NE(pName, nullptr);
	EXPECT_STREQ(pName, "libDDNet.so");
	EXPECT_EQ(Offset, 0xfffu);

	// 范围是左闭右开，正好落在 end 上的地址不属于该模块。
	EXPECT_EQ(crashdump_find_module(aModules, 2, 0x9000, &Offset), nullptr);
	EXPECT_EQ(crashdump_find_module(aModules, 2, 0xfff, &Offset), nullptr);
	EXPECT_EQ(crashdump_find_module(aModules, 2, 0, &Offset), nullptr);
	EXPECT_EQ(crashdump_find_module(nullptr, 2, 0x1500, &Offset), nullptr);
}

TEST(QmCrashdumpModules, ModuleOffsetIsRelativeToTheModuleBase)
{
	SCrashModuleRange Module = {};
	Module.m_Start = 0x400000;
	Module.m_End = 0x500000;
	str_copy(Module.m_aName, "libbar.so");

	uintptr_t Offset = 1;
	ASSERT_NE(crashdump_find_module(&Module, 1, 0x400000, &Offset), nullptr);
	EXPECT_EQ(Offset, 0u);

	// 不关心偏移时仍然返回模块名。
	EXPECT_STREQ(crashdump_find_module(&Module, 1, 0x400100, nullptr), "libbar.so");
}

TEST(QmCrashdumpFrameLines, FrameLinesCarryModuleAndOffsetWhenKnown)
{
	SCrashModuleRange Module = {};
	Module.m_Start = 0x100000;
	Module.m_End = 0x200000;
	str_copy(Module.m_aName, "libDDNet.so");

	const uintptr_t aFrames[] = {0x100040, 0x900000};
	char aBuffer[512];
	const size_t Length = crashdump_append_frame_lines(aBuffer, sizeof(aBuffer), 0, aFrames, 2, &Module, 1);

	ASSERT_LT(Length, sizeof(aBuffer));
	const std::string Text(aBuffer);
	EXPECT_NE(Text.find("#00 0x0000000000100040 libDDNet.so+0x40"), std::string::npos);
	// 未命中模块的地址只输出裸地址，不编造模块名。
	EXPECT_NE(Text.find("#01 0x0000000000900000\n"), std::string::npos);
	EXPECT_EQ(Text.find("#01 0x0000000000900000 libDDNet.so"), std::string::npos);
}

TEST(QmCrashdumpStaleReports, KeepsTheNewestEntriesWithinTheLimit)
{
	const time_t aTimes[] = {100, 500, 300, 200, 400};
	size_t aIndices[8] = {};
	const size_t Count = crashdump_select_stale_reports(aTimes, 5, 3, aIndices, 8);

	ASSERT_EQ(Count, 2u);
	// 最旧的两份是 100 和 200。
	EXPECT_EQ(aIndices[0], 0u);
	EXPECT_EQ(aIndices[1], 3u);
}

TEST(QmCrashdumpStaleReports, NothingIsSelectedWhenTheLimitIsNotExceeded)
{
	const time_t aTimes[] = {100, 200};
	size_t aIndices[4] = {};
	EXPECT_EQ(crashdump_select_stale_reports(aTimes, 2, 3, aIndices, 4), 0u);
	EXPECT_EQ(crashdump_select_stale_reports(aTimes, 2, 2, aIndices, 4), 0u);
}

TEST(QmCrashdumpStaleReports, OutputIsCappedByTheProvidedBuffer)
{
	const time_t aTimes[] = {10, 20, 30, 40};
	size_t aIndices[1] = {};
	const size_t Count = crashdump_select_stale_reports(aTimes, 4, 1, aIndices, 1);

	ASSERT_EQ(Count, 1u);
	EXPECT_EQ(aIndices[0], 0u);
}

TEST(QmCrashdumpStaleReports, MissingInputsAreHandled)
{
	time_t aTimes[2] = {};
	size_t aIndices[2] = {};
	EXPECT_EQ(crashdump_select_stale_reports(nullptr, 2, 0, aIndices, 2), 0u);
	EXPECT_EQ(crashdump_select_stale_reports(aTimes, 2, 0, nullptr, 2), 0u);
	EXPECT_EQ(crashdump_select_stale_reports(aTimes, 2, 0, aIndices, 0), 0u);
}

TEST(QmCrashdumpModuleTable, SamePathMergesIntoSingleRange)
{
	SCrashModuleRange aModules[4] = {};
	size_t NumModules = 0;

	ASSERT_TRUE(crashdump_add_module_range(aModules, &NumModules, 4, 0x2000, 0x3000, "/system/lib64/libfoo.so"));
	EXPECT_FALSE(crashdump_add_module_range(aModules, &NumModules, 4, 0x5000, 0x6000, "/system/lib64/libfoo.so"));

	ASSERT_EQ(NumModules, 1u);
	EXPECT_EQ(aModules[0].m_Start, 0x2000u);
	EXPECT_EQ(aModules[0].m_End, 0x6000u);
	EXPECT_STREQ(aModules[0].m_aName, "libfoo.so");
	EXPECT_STREQ(aModules[0].m_aPath, "/system/lib64/libfoo.so");
}

// 真机回归：基址不同但 basename 相同的库必须各自成条，否则合并出的范围会
// 横跨数 GB，把不相干的地址算成 giant offset（曾出现 liblzma.so+0x1207FC3CC）。
TEST(QmCrashdumpModuleTable, SameNameFromDifferentPathsStaysSeparate)
{
	SCrashModuleRange aModules[4] = {};
	size_t NumModules = 0;

	ASSERT_TRUE(crashdump_add_module_range(aModules, &NumModules, 4, 0x1000, 0x2000, "/system/lib64/liblzma.so"));
	ASSERT_TRUE(crashdump_add_module_range(aModules, &NumModules, 4, 0x70000000, 0x70001000, "/apex/com.android.runtime/liblzma.so"));

	ASSERT_EQ(NumModules, 2u);
	EXPECT_STREQ(aModules[0].m_aName, "liblzma.so");
	EXPECT_STREQ(aModules[1].m_aName, "liblzma.so");

	uintptr_t Offset = 0;
	ASSERT_NE(crashdump_find_module(aModules, NumModules, 0x1500, &Offset), nullptr);
	EXPECT_EQ(Offset, 0x500u);
	ASSERT_NE(crashdump_find_module(aModules, NumModules, 0x70000100, &Offset), nullptr);
	EXPECT_EQ(Offset, 0x100u);
}

TEST(QmCrashdumpModuleTable, FullTableRejectsNewPathsButStillMergesKnownOnes)
{
	SCrashModuleRange aModules[1] = {};
	size_t NumModules = 0;

	ASSERT_TRUE(crashdump_add_module_range(aModules, &NumModules, 1, 0x1000, 0x2000, "/a/libx.so"));
	EXPECT_FALSE(crashdump_add_module_range(aModules, &NumModules, 1, 0x3000, 0x4000, "/b/liby.so"));
	ASSERT_EQ(NumModules, 1u);

	EXPECT_FALSE(crashdump_add_module_range(aModules, &NumModules, 1, 0x1800, 0x2800, "/a/libx.so"));
	EXPECT_EQ(aModules[0].m_End, 0x2800u);
}

TEST(QmCrashdumpModuleTable, InvalidInputsAreRejected)
{
	SCrashModuleRange aModules[2] = {};
	size_t NumModules = 0;
	EXPECT_FALSE(crashdump_add_module_range(nullptr, &NumModules, 2, 0x1000, 0x2000, "/a/libx.so"));
	EXPECT_FALSE(crashdump_add_module_range(aModules, nullptr, 2, 0x1000, 0x2000, "/a/libx.so"));
	EXPECT_FALSE(crashdump_add_module_range(aModules, &NumModules, 2, 0x1000, 0x2000, nullptr));
	EXPECT_FALSE(crashdump_add_module_range(aModules, &NumModules, 2, 0x1000, 0x2000, ""));
	EXPECT_EQ(NumModules, 0u);
}
