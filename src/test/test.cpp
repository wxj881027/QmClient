// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <base/logger.h>
#include <base/os.h>
#include <base/system.h>

#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/support/qm_crashdump_process_probe.h>
#include <test/support/qm_exit_process_probe.h>
#include <test/support/qm_open_link_probe.h>
#include <test/support/qm_test_terminate_diagnostics.h>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#include <execinfo.h>
#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 600 // macOS 的 <ucontext.h> 只在 _XOPEN_SOURCE 下可用
#endif
#include <ucontext.h>
#endif
#include <cstdint>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#if !defined(DDNET_TEST_SOURCE_DIR)
#define DDNET_TEST_SOURCE_DIR "."
#endif

std::string TestSourcePath(const char *pRelativePath)
{
	if(pRelativePath == nullptr || pRelativePath[0] == '\0')
		return DDNET_TEST_SOURCE_DIR;

	std::string Path = DDNET_TEST_SOURCE_DIR;
	if(!Path.empty() && Path.back() != '/' && Path.back() != '\\')
		Path += '/';
	Path += pRelativePath;
	return Path;
}

std::string ReadTestSourceFile(const char *pRelativePath)
{
	const std::string Path = TestSourcePath(pRelativePath);
	std::ifstream File(Path, std::ios::binary);
	EXPECT_TRUE(File.good()) << Path;
	std::ostringstream Buffer;
	Buffer << File.rdbuf();
	std::string Contents = Buffer.str();
	// Source-contract tests use '\n' literals; normalize checked-in CRLF files
	// so assertions are independent of the checkout's line-ending policy.
	Contents.erase(std::remove(Contents.begin(), Contents.end(), '\r'), Contents.end());
	return Contents;
}

static void WriteTestFileOrDir(const char *pBasePath, const char *pRelativePath, bool Directory)
{
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s", pBasePath, pRelativePath);
	if(Directory)
	{
		ASSERT_FALSE(fs_makedir(aPath));
		return;
	}

	IOHANDLE File = io_open(aPath, IOFLAG_WRITE);
	ASSERT_TRUE(File != nullptr);
	static const char *pContents = "tmp";
	io_write(File, pContents, str_length(pContents));
	io_close(File);
}

static void CreateScopedTestStorageFixture(const std::function<void(CTestInfo &)> &Body)
{
	CTestInfo Info;
	Body(Info);
}

// 默认只在用例失败时输出该用例产生的日志，避免单元测试被海量 info 日志淹没。
class CTestLogLogger : public ILogger
{
	std::mutex m_Mutex;
	std::vector<std::string> m_vLines;

public:
	void Log(const CLogMessage *pMessage) override
	{
		// 故障注入期间日志捕获不是被测业务；避免测试后端的容器分配改变预算矩阵。
		if(CPredictionAllocationFailure::Active())
			return;
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_vLines.emplace_back(pMessage->m_aLine, pMessage->m_LineLength);
	}

	void Clear()
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_vLines.clear();
	}

	void Flush()
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		for(const std::string &Line : m_vLines)
		{
			std::fprintf(stderr, "%s\n", Line.c_str());
		}
		m_vLines.clear();
	}
};

static CTestLogLogger *gs_pTestLogLogger = nullptr;

class CTestLogListener : public ::testing::EmptyTestEventListener
{
	void OnTestStart(const ::testing::TestInfo &Info) override
	{
		(void)Info;
		gs_pTestLogLogger->Clear();
	}

	void OnTestEnd(const ::testing::TestInfo &Info) override
	{
		(void)Info;
		if(::testing::Test::HasFailure())
			gs_pTestLogLogger->Flush();
		else
			gs_pTestLogLogger->Clear();
	}
};

int main(int argc, const char **argv)
{
	QmInstallMacTestTerminateDiagnostics();
	CCmdlineFix CmdlineFix(&argc, &argv);
	if(const auto Result = QmCrashdumpProcessProbe(argc, argv))
		return *Result;
	gs_pTestLogLogger = new CTestLogLogger();
	log_set_global_logger(gs_pTestLogLogger);
	if(const auto Result = QmExitProcessProbe(argc, argv))
		return *Result;
	if(const auto Result = QmOpenLinkProbe(argc, argv))
		return *Result;
	::testing::InitGoogleTest(&argc, const_cast<char **>(argv));
	::testing::UnitTest::GetInstance()->listeners().Append(new CTestLogListener());
	GTEST_FLAG_SET(death_test_style, "threadsafe");
	// terminate 兜底：异常漏出测试体时把当前用例名打到 stderr（无缓冲）。
	std::set_terminate([] { // NOLINT
		const char *pSuite = "unknown";
		const char *pTest = "unknown";
		if(const auto *pInfo = ::testing::UnitTest::GetInstance()->current_test_info())
		{
			pSuite = pInfo->test_suite_name();
			pTest = pInfo->name();
		}
		std::fprintf(stderr, "FATAL: uncaught exception while running %s.%s\n", pSuite, pTest);
		if(auto pCurrent = std::current_exception())
		{
			try
			{
				std::rethrow_exception(pCurrent);
			}
			catch(const std::exception &e)
			{
				std::fprintf(stderr, "FATAL: what(): %s\n", e.what());
			}
			catch(...)
			{
				std::fprintf(stderr, "FATAL: non-std exception\n");
			}
		}
#if defined(__APPLE__) || defined(__linux__)
		// CI 的 stdout 是块缓冲，帧地址 + dladdr 名字是唯一能落到具体语句的线索。
		void *aFrames[40];
		const int NumFrames = backtrace(aFrames, 40);
		for(int i = 0; i < NumFrames; ++i)
		{
			Dl_info Info;
			if(dladdr(aFrames[i], &Info) != 0 && Info.dli_sname != nullptr)
				std::fprintf(stderr, "  #%d %s (%s)\n", i, Info.dli_sname, Info.dli_fname != nullptr ? Info.dli_fname : "?");
			else
				std::fprintf(stderr, "  #%d %p\n", i, aFrames[i]);
		}
#endif
		std::fflush(stderr);
		std::abort();
	});
#if defined(__APPLE__) || defined(__linux__)
	// 信号型崩溃（如 dbg_break 的 SIGTRAP）在 CI 上只留下退出码；打印出错 PC 与栈帧后
	// 恢复默认处理并重新抛出同一信号，death test 仍然能看到原来的信号。
	// 出错 PC 比栈帧更关键：栈帧里的叶子函数常被尾调用掉，只有 PC 能直接指到出错函数。
	struct sigaction Action = {};
	Action.sa_sigaction = [](int Sig, siginfo_t *pInfo, void *pContext) {
		const char *pSuite = "unknown";
		const char *pTest = "unknown";
		if(const auto *pT = ::testing::UnitTest::GetInstance()->current_test_info())
		{
			pSuite = pT->test_suite_name();
			pTest = pT->name();
		}
		std::fprintf(stderr, "FATAL: signal %d (code %d) while running %s.%s\n", Sig, pInfo != nullptr ? pInfo->si_code : -1, pSuite, pTest);
		if(auto *pUContext = static_cast<ucontext_t *>(pContext))
		{
			const uintptr_t Pc =
#if defined(__APPLE__)
				(uintptr_t)pUContext->uc_mcontext->__ss.__pc;
#elif defined(REG_RIP)
				(uintptr_t)pUContext->uc_mcontext.gregs[REG_RIP];
#elif defined(REG_PC)
				(uintptr_t)pUContext->uc_mcontext.gregs[REG_PC];
#else
				(uintptr_t)0;
#endif
			Dl_info Info;
			if(dladdr((void *)Pc, &Info) != 0 && Info.dli_sname != nullptr)
				std::fprintf(stderr, "FATAL: at %s+0x%lx\n", Info.dli_sname, (unsigned long)(Pc - (uintptr_t)Info.dli_saddr));
			else
				std::fprintf(stderr, "FATAL: at %p\n", (void *)Pc);
		}
		void *aFrames[40];
		const int NumFrames = backtrace(aFrames, 40);
		for(int i = 0; i < NumFrames; ++i)
		{
			Dl_info Info;
			if(dladdr(aFrames[i], &Info) != 0 && Info.dli_sname != nullptr)
				std::fprintf(stderr, "  #%d %s (%s)\n", i, Info.dli_sname, Info.dli_fname != nullptr ? Info.dli_fname : "?");
			else
				std::fprintf(stderr, "  #%d %p\n", i, aFrames[i]);
		}
		std::fflush(stderr);
		std::signal(Sig, SIG_DFL);
		std::raise(Sig);
	};
	Action.sa_flags = SA_SIGINFO;
	sigemptyset(&Action.sa_mask);
	sigaction(SIGTRAP, &Action, nullptr);
#endif
	net_init();
	return RUN_ALL_TESTS();
}

TEST(TestInfo, Sort)
{
	std::vector<CTestInfoPath> vEntries;
	vEntries.resize(3);

	const char aBasePath[] = "test_dir";
	const char aSubPath[] = "test_dir/subdir";
	const char aFilePath[] = "test_dir/subdir/file.txt";

	vEntries[0].m_IsDirectory = true;
	str_copy(vEntries[0].m_aData, aBasePath);

	vEntries[1].m_IsDirectory = true;
	str_copy(vEntries[1].m_aData, aSubPath);

	vEntries[2].m_IsDirectory = false;
	str_copy(vEntries[2].m_aData, aFilePath);

	// Sorts directories after files.
	std::sort(vEntries.begin(), vEntries.end());

	EXPECT_FALSE(vEntries[0].m_IsDirectory);
	EXPECT_EQ(str_comp(vEntries[0].m_aData, aFilePath), 0);
	EXPECT_TRUE(vEntries[1].m_IsDirectory);
	EXPECT_EQ(str_comp(vEntries[1].m_aData, aSubPath), 0);
	EXPECT_TRUE(vEntries[2].m_IsDirectory);
	EXPECT_EQ(str_comp(vEntries[2].m_aData, aBasePath), 0);
}

TEST(TestInfo, DeletesTestStorageOnSuccessByDefault)
{
	CTestInfo Info;
	EXPECT_TRUE(Info.m_DeleteTestStorageFilesOnSuccess);
}

TEST(TestInfo, SuccessfulScopedStorageIsDeletedRecursively)
{
	char aPath[IO_MAX_PATH_LENGTH];
	CreateScopedTestStorageFixture([&](CTestInfo &Info) {
		str_copy(aPath, Info.m_aFilename, sizeof(aPath));
		ASSERT_FALSE(fs_makedir(Info.m_aFilename));
		WriteTestFileOrDir(Info.m_aFilename, "root.txt", false);
		WriteTestFileOrDir(Info.m_aFilename, "nested", true);
		WriteTestFileOrDir(Info.m_aFilename, "nested/child.txt", false);
		EXPECT_TRUE(fs_is_dir(Info.m_aFilename));
	});

	EXPECT_FALSE(fs_is_dir(aPath));
	EXPECT_FALSE(fs_is_file(aPath));
}

TEST(TestInfo, CreateTestStorageCleansCreatedSaveFilesOnSuccess)
{
	char aPath[IO_MAX_PATH_LENGTH];
	CreateScopedTestStorageFixture([&](CTestInfo &Info) {
		str_copy(aPath, Info.StoragePath(), sizeof(aPath));
		std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
		ASSERT_NE(pStorage, nullptr);

		char aSavePath[IO_MAX_PATH_LENGTH];
		pStorage->GetCompletePath(IStorage::TYPE_SAVE, "cleanup.txt", aSavePath, sizeof(aSavePath));
		IOHANDLE File = io_open(aSavePath, IOFLAG_WRITE);
		ASSERT_TRUE(File != nullptr);
		static const char *pContents = "cleanup";
		io_write(File, pContents, str_length(pContents));
		io_close(File);

		EXPECT_TRUE(fs_is_file(aSavePath));
	});

	EXPECT_FALSE(fs_is_dir(aPath));
}

TEST(TestInfo, DisabledCleanupPreservesFilesForInspection)
{
	char aPath[IO_MAX_PATH_LENGTH];
	CreateScopedTestStorageFixture([&](CTestInfo &Info) {
		Info.m_DeleteTestStorageFilesOnSuccess = false;
		str_copy(aPath, Info.StoragePath(), sizeof(aPath));
		std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
		ASSERT_NE(pStorage, nullptr);
		WriteTestFileOrDir(Info.StoragePath(), "keep.txt", false);
	});

	EXPECT_TRUE(fs_is_dir(aPath));
	char aFile[IO_MAX_PATH_LENGTH];
	str_format(aFile, sizeof(aFile), "%s/keep.txt", aPath);
	EXPECT_TRUE(fs_is_file(aFile));
	TestDeleteTestStorageFiles(aPath);
}
