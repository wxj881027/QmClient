#include <base/system.h>

#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/support/regular_file_open_timeout.h>
#include <test/test.h>

#include <memory>
#include <string>

#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>

#include <windows.h>
#elif defined(CONF_FAMILY_UNIX)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
	using CFile = std::unique_ptr<void, decltype(&io_close)>;

	class RegularFileOpen : public testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}
		std::string Path(const char *pName)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, pName, aPath, sizeof(aPath));
			return aPath;
		}
		void Write(const char *pName, const std::string &Bytes)
		{
			CFile File(m_pStorage->OpenFile(pName, IOFLAG_WRITE, IStorage::TYPE_SAVE), io_close);
			ASSERT_NE(File, nullptr);
			ASSERT_EQ(io_write(File.get(), Bytes.data(), static_cast<unsigned>(Bytes.size())), Bytes.size());
			ASSERT_EQ(io_close(File.release()), 0);
		}
	};
	class RegularFileOpenDeathTest : public RegularFileOpen
	{
	};
}

TEST_F(RegularFileOpen, NativeFileKeepsBinaryReadAndStreamOwnership)
{
	const std::string Bytes("a\0b\r\n", 5);
	ASSERT_NO_FATAL_FAILURE(Write("普通文件.bin", Bytes));
	bool Missing = true;
	CFile File(io_open_read_regular_checked(Path("普通文件.bin").c_str(), &Missing), io_close);
	ASSERT_NE(File, nullptr);
	EXPECT_FALSE(Missing);
	char aBytes[5];
	ASSERT_EQ(io_read(File.get(), aBytes, sizeof(aBytes)), sizeof(aBytes));
	EXPECT_EQ(std::string(aBytes, sizeof(aBytes)), Bytes);
	EXPECT_EQ(io_close(File.release()), 0);
}

TEST_F(RegularFileOpen, OnlyMissingPathSetsFlagAndLaterSuccessClearsIt)
{
	bool Missing = false;
	CFile MissingFile(io_open_read_regular_checked(Path("missing.bin").c_str(), &Missing), io_close);
	EXPECT_EQ(MissingFile, nullptr);
	EXPECT_TRUE(Missing);
	ASSERT_NO_FATAL_FAILURE(Write("missing.bin", "ready"));
	CFile File(io_open_read_regular_checked(Path("missing.bin").c_str(), &Missing), io_close);
	ASSERT_NE(File, nullptr);
	EXPECT_FALSE(Missing);
}

TEST_F(RegularFileOpen, DirectoryIsRejectedWithoutClaimingMissing)
{
	ASSERT_TRUE(m_pStorage->CreateFolder("directory", IStorage::TYPE_SAVE));
	bool Missing = true;
	CFile File(io_open_read_regular_checked(Path("directory").c_str(), &Missing), io_close);
	EXPECT_EQ(File, nullptr);
	EXPECT_FALSE(Missing);
	EXPECT_TRUE(m_pStorage->FolderExists("directory", IStorage::TYPE_SAVE));
}

TEST_F(RegularFileOpen, InvalidNameCannotMasqueradeAsMissing)
{
	for(const char *pName : {static_cast<const char *>(nullptr), "", "invalid_\xff"})
	{
		bool Missing = true;
		CFile File(io_open_read_regular_checked(pName, &Missing), io_close);
		EXPECT_EQ(File, nullptr);
		EXPECT_FALSE(Missing);
	}
}

TEST_F(RegularFileOpen, MissingOutputCanBeOmitted)
{
	ASSERT_NO_FATAL_FAILURE(Write("ready.bin", "ready"));
	CFile File(io_open_read_regular_checked(Path("ready.bin").c_str(), nullptr), io_close);
	ASSERT_NE(File, nullptr);
}

#if defined(CONF_FAMILY_UNIX)
TEST_F(RegularFileOpen, SymlinkToRegularTargetKeepsLinkAndContents)
{
	ASSERT_NO_FATAL_FAILURE(Write("target.bin", "ready"));
	const auto Link = Path("link.bin");
	ASSERT_EQ(symlink("target.bin", Link.c_str()), 0);
	bool Missing = true;
	CFile File(io_open_read_regular_checked(Link.c_str(), &Missing), io_close);
	ASSERT_NE(File, nullptr);
	EXPECT_FALSE(Missing);
	char aData[5];
	ASSERT_EQ(io_read(File.get(), aData, sizeof(aData)), sizeof(aData));
	EXPECT_EQ(std::string(aData, sizeof(aData)), "ready");
	struct stat Information;
	ASSERT_EQ(lstat(Link.c_str(), &Information), 0);
	EXPECT_TRUE(S_ISLNK(Information.st_mode));
}

TEST_F(RegularFileOpen, DanglingSymlinkReportsMissingWithoutRemovingLink)
{
	const auto Link = Path("link.bin");
	ASSERT_EQ(symlink("missing.bin", Link.c_str()), 0);
	bool Missing = false;
	CFile File(io_open_read_regular_checked(Link.c_str(), &Missing), io_close);
	EXPECT_EQ(File, nullptr);
	EXPECT_TRUE(Missing);
	struct stat Information;
	ASSERT_EQ(lstat(Link.c_str(), &Information), 0);
	EXPECT_TRUE(S_ISLNK(Information.st_mode));
}

TEST_F(RegularFileOpen, SymlinkToDirectoryDoesNotCountAsMissingFile)
{
	ASSERT_TRUE(m_pStorage->CreateFolder("directory", IStorage::TYPE_SAVE));
	const auto Link = Path("link.bin");
	ASSERT_EQ(symlink("directory", Link.c_str()), 0);
	bool Missing = true;
	CFile File(io_open_read_regular_checked(Link.c_str(), &Missing), io_close);
	EXPECT_EQ(File, nullptr);
	EXPECT_FALSE(Missing);
}

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
TEST_F(RegularFileOpen, AcceptedDescriptorIsBlockingAndCloseOnExec)
{
	ASSERT_NO_FATAL_FAILURE(Write("ready.bin", "ready"));
	CFile File(io_open_read_regular_checked(Path("ready.bin").c_str(), nullptr), io_close);
	ASSERT_NE(File, nullptr);
	const int Descriptor = fileno(static_cast<FILE *>(File.get()));
	const int StatusFlags = fcntl(Descriptor, F_GETFL);
	ASSERT_NE(StatusFlags, -1);
	EXPECT_EQ(StatusFlags & O_NONBLOCK, 0);
	const int DescriptorFlags = fcntl(Descriptor, F_GETFD);
	ASSERT_NE(DescriptorFlags, -1);
	EXPECT_NE(DescriptorFlags & FD_CLOEXEC, 0);
}
#endif

TEST_F(RegularFileOpen, CharacterDeviceIsRejectedWithoutClaimingMissing)
{
	bool Missing = true;
	CFile File(io_open_read_regular_checked("/dev/null", &Missing), io_close);
	EXPECT_EQ(File, nullptr);
	EXPECT_FALSE(Missing);
}

#if GTEST_HAS_DEATH_TEST && !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_EMSCRIPTEN)
TEST_F(RegularFileOpenDeathTest, FifoWithoutWriterIsRejectedWithBoundedWait)
{
	const auto Fifo = Path("input.fifo");
	ASSERT_EQ(mkfifo(Fifo.c_str(), 0600), 0);
	ASSERT_EXIT(
		{
			ArmRegularFileOpenChildTimeout();
			bool Missing = true;
			CFile File(io_open_read_regular_checked(Fifo.c_str(), &Missing), io_close);
			_exit(!File && !Missing ? 0 : 1);
		},
		testing::ExitedWithCode(0), "");
	struct stat Information;
	ASSERT_EQ(lstat(Fifo.c_str(), &Information), 0);
	EXPECT_TRUE(S_ISFIFO(Information.st_mode));
}

TEST_F(RegularFileOpenDeathTest, SymlinkToFifoIsRejectedWithBoundedWait)
{
	const auto Fifo = Path("input.fifo");
	const auto Link = Path("link.bin");
	ASSERT_EQ(mkfifo(Fifo.c_str(), 0600), 0);
	ASSERT_EQ(symlink("input.fifo", Link.c_str()), 0);
	ASSERT_EXIT(
		{
			ArmRegularFileOpenChildTimeout();
			bool Missing = true;
			CFile File(io_open_read_regular_checked(Link.c_str(), &Missing), io_close);
			_exit(!File && !Missing ? 0 : 1);
		},
		testing::ExitedWithCode(0), "");
}

TEST_F(RegularFileOpenDeathTest, RepeatedTypeRejectionReleasesEachDescriptor)
{
	const auto Fifo = Path("input.fifo");
	ASSERT_EQ(mkfifo(Fifo.c_str(), 0600), 0);
	ASSERT_EXIT(
		{
			ArmRegularFileOpenChildTimeout();
			const int Expected = open("/dev/null", O_RDONLY);
			if(Expected == -1)
				_exit(2);
			close(Expected);
			for(int Index = 0; Index < 512; ++Index)
			{
				bool Missing = true;
				CFile File(io_open_read_regular_checked(Fifo.c_str(), &Missing), io_close);
				if(File || Missing)
					_exit(3);
			}
			const int Probe = open("/dev/null", O_RDONLY);
			const bool Released = Probe == Expected;
			if(Probe != -1)
				close(Probe);
			_exit(Released ? 0 : 4);
		},
		testing::ExitedWithCode(0), "");
}
#endif
#elif defined(CONF_FAMILY_WINDOWS)
TEST_F(RegularFileOpen, WindowsCharacterHandleIsRejectedWithoutClaimingMissing)
{
	bool Missing = true;
	CFile File(io_open_read_regular_checked("NUL", &Missing), io_close);
	EXPECT_EQ(File, nullptr);
	EXPECT_FALSE(Missing);
}

TEST_F(RegularFileOpen, WindowsNamedPipeHandleIsRejectedBeforeStreamRead)
{
	char aPipe[256];
	m_Info.Filename(aPipe, sizeof(aPipe), ".regular-open");
	const std::string Name = std::string("\\\\.\\pipe\\") + aPipe;
	const auto Wide = windows_utf8_to_wide(Name.c_str());
	const HANDLE Native = CreateNamedPipeW(Wide.c_str(), PIPE_ACCESS_OUTBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT, 1, 1024, 1024, 0, nullptr);
	ASSERT_NE(Native, INVALID_HANDLE_VALUE);
	std::unique_ptr<void, decltype(&CloseHandle)> Server(Native, CloseHandle);
	if(!ConnectNamedPipe(Server.get(), nullptr))
		ASSERT_EQ(GetLastError(), ERROR_PIPE_LISTENING);
	bool Missing = true;
	CFile File(io_open_read_regular_checked(Name.c_str(), &Missing), io_close);
	EXPECT_EQ(File, nullptr);
	EXPECT_FALSE(Missing);
}
#endif
