#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS) || defined(CONF_PLATFORM_LINUX)
#include <base/fs.h>
#include <base/system.h>

#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <string>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

namespace
{
	enum class ECompletion
	{
		NORMAL,
		EARLY_STOP,
		THROW,
	};
	struct SVisitor
	{
		ECompletion m_Completion;
		int m_Calls = 0;
	};
	int Visit(const char *, int, int, void *pUser)
	{
		auto &Visitor = *static_cast<SVisitor *>(pUser);
		++Visitor.m_Calls;
		if(Visitor.m_Completion == ECompletion::THROW)
			throw 7;
		return Visitor.m_Completion == ECompletion::EARLY_STOP;
	}
	int VisitInfo(const CFsFileInfo *, int IsDir, int Type, void *pUser)
	{
		return Visit(nullptr, IsDir, Type, pUser);
	}

#if defined(CONF_FAMILY_WINDOWS)
	DWORD WINAPI ProbeTimeout(void *)
	{
		Sleep(5000);
		ExitProcess(124);
		return 0;
	}
#endif
	int CountNativeResources()
	{
#if defined(CONF_FAMILY_WINDOWS)
		DWORD Count;
		return GetProcessHandleCount(GetCurrentProcess(), &Count) ? static_cast<int>(Count) : -1;
#else
		const std::unique_ptr<DIR, decltype(&closedir)> Directory(opendir("/proc/self/fd"), closedir);
		if(!Directory)
			return -1;
		int Count = 0;
		while(const dirent *pEntry = readdir(Directory.get()))
			if(pEntry->d_name[0] != '.')
				++Count;
		return Count;
#endif
	}

	int RunProbe(bool WithInfo, ECompletion Completion)
	{
#if defined(CONF_FAMILY_WINDOWS)
		const HANDLE Watchdog = CreateThread(nullptr, 0, ProbeTimeout, nullptr, 0, nullptr);
		if(!Watchdog)
			return 74;
		CloseHandle(Watchdog);
#else
		alarm(5);
#endif
		CTestInfo Info;
		auto pStorage = Info.CreateTestStorage();
		if(!pStorage)
			return 75;
		IOHANDLE File = pStorage->OpenFile("entry.txt", IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File || io_close(File) != 0)
			return 76;
		auto Enumerate = [&](ECompletion Mode) {
			SVisitor Visitor{Mode};
			bool Caught = false;
			try
			{
				if(WithInfo)
					fs_listdir_fileinfo(Info.StoragePath(), VisitInfo, IStorage::TYPE_SAVE, &Visitor);
				else
					fs_listdir(Info.StoragePath(), Visit, IStorage::TYPE_SAVE, &Visitor);
			}
			catch(int Value)
			{
				Caught = Value == 7;
			}
			return Visitor.m_Calls > 0 && (Mode == ECompletion::NORMAL || Visitor.m_Calls == 1) && Caught == (Mode == ECompletion::THROW);
		};
		// 先准备原生转码和异常运行库，再在隔离子进程内比较资源数。
		if(!Enumerate(ECompletion::NORMAL) || !Enumerate(Completion))
			return 77;
		const int Before = CountNativeResources();
		for(int Attempt = 0; Attempt < 8; ++Attempt)
			if(!Enumerate(Completion))
				return 78;
		return Before >= 0 && CountNativeResources() == Before ? 73 : 79;
	}

	class CDirectoryEnumerationDeathTest : public ::testing::Test
	{
		const std::string m_PreviousStyle = ::testing::FLAGS_gtest_death_test_style;
		void SetUp() override
		{
#if defined(CONF_PLATFORM_LINUX)
			if(access("/proc/self/fd", R_OK) != 0)
				GTEST_SKIP() << "Native descriptor inventory is unavailable";
#endif
			::testing::FLAGS_gtest_death_test_style = "threadsafe";
		}
		void TearDown() override { ::testing::FLAGS_gtest_death_test_style = m_PreviousStyle; }
	};
}

TEST_F(CDirectoryEnumerationDeathTest, SimpleCallbackExceptionClosesTheNativeSearch)
{
	EXPECT_EXIT(std::_Exit(RunProbe(false, ECompletion::THROW)), ::testing::ExitedWithCode(73), "");
}

TEST_F(CDirectoryEnumerationDeathTest, FileInfoCallbackExceptionClosesTheNativeSearch)
{
	EXPECT_EXIT(std::_Exit(RunProbe(true, ECompletion::THROW)), ::testing::ExitedWithCode(73), "");
}

TEST_F(CDirectoryEnumerationDeathTest, EarlyStopClosesBothNativeSearchKinds)
{
	for(bool WithInfo : {false, true})
	{
		SCOPED_TRACE(WithInfo);
		EXPECT_EXIT(std::_Exit(RunProbe(WithInfo, ECompletion::EARLY_STOP)), ::testing::ExitedWithCode(73), "");
	}
}

TEST_F(CDirectoryEnumerationDeathTest, NormalCompletionClosesBothNativeSearchKinds)
{
	for(bool WithInfo : {false, true})
	{
		SCOPED_TRACE(WithInfo);
		EXPECT_EXIT(std::_Exit(RunProbe(WithInfo, ECompletion::NORMAL)), ::testing::ExitedWithCode(73), "");
	}
}
#endif
