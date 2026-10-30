#include <engine/shared/config.h>
#include <engine/shared/fifo.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>
#elif defined(CONF_FAMILY_UNIX)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
	class FifoLifecycle : public testing::Test
	{
		CTestInfo m_Info;

	protected:
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<IConsole> m_pConsole;
		CFifo m_Fifo;
		char m_aPath[IO_MAX_PATH_LENGTH]{};
		char m_aName[192]{};
		int m_Calls = 0;
		int m_Throws = 0;
#if defined(CONF_FAMILY_WINDOWS)
		HANDLE m_Writer = INVALID_HANDLE_VALUE;
#elif defined(CONF_FAMILY_UNIX)
		int m_Writer = -1;
#endif

		static void Probe(IConsole::IResult *, void *pUser) { ++static_cast<FifoLifecycle *>(pUser)->m_Calls; }
		static void Throw(IConsole::IResult *, void *pUser)
		{
			++static_cast<FifoLifecycle *>(pUser)->m_Throws;
			throw 7;
		}
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "input.fifo", m_aPath, sizeof(m_aPath));
			m_Info.Filename(m_aName, sizeof(m_aName), ".fifo");
			m_pConsole = CreateConsole(CFGFLAG_CLIENT);
			ASSERT_NE(m_pConsole, nullptr);
			m_pConsole->Register("fifo_probe", "", CFGFLAG_CLIENT, Probe, this, "");
			m_pConsole->Register("fifo_throw", "", CFGFLAG_CLIENT, Throw, this, "");
		}
		void TearDown() override
		{
			CloseWriter();
			m_Fifo.Shutdown();
		}
		const char *Name() const
		{
#if defined(CONF_FAMILY_WINDOWS)
			return m_aName;
#else
			return m_aPath;
#endif
		}
		void Start() { m_Fifo.Init(m_pConsole.get(), Name(), CFGFLAG_CLIENT); }
		void CloseWriter()
		{
#if defined(CONF_FAMILY_WINDOWS)
			if(m_Writer != INVALID_HANDLE_VALUE)
				CloseHandle(std::exchange(m_Writer, INVALID_HANDLE_VALUE));
#elif defined(CONF_FAMILY_UNIX)
			if(m_Writer >= 0)
				close(std::exchange(m_Writer, -1));
#endif
		}
		bool OpenWriter()
		{
			CloseWriter();
#if defined(CONF_FAMILY_WINDOWS)
			const std::string Path = std::string("\\\\.\\pipe\\") + m_aName;
			m_Writer = CreateFileW(windows_utf8_to_wide(Path.c_str()).c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			return m_Writer != INVALID_HANDLE_VALUE;
#elif defined(CONF_FAMILY_UNIX)
			m_Writer = open(m_aPath, O_WRONLY | O_NONBLOCK);
			return m_Writer >= 0;
#else
			return false;
#endif
		}
		bool Send(std::string_view Bytes)
		{
#if defined(CONF_FAMILY_WINDOWS)
			DWORD Written = 0;
			return WriteFile(m_Writer, Bytes.data(), static_cast<DWORD>(Bytes.size()), &Written, nullptr) && Written == Bytes.size();
#elif defined(CONF_FAMILY_UNIX)
			return write(m_Writer, Bytes.data(), Bytes.size()) == static_cast<ssize_t>(Bytes.size());
#else
			return false;
#endif
		}
		void WriteRegular(std::string_view Bytes)
		{
			std::unique_ptr<void, decltype(&io_close)> File(io_open(m_aPath, IOFLAG_WRITE), &io_close);
			ASSERT_TRUE(File);
			ASSERT_EQ(io_write(File.get(), Bytes.data(), static_cast<unsigned>(Bytes.size())), Bytes.size());
			EXPECT_EQ(io_close(File.release()), 0);
		}
		std::string ReadRegular()
		{
			std::unique_ptr<void, decltype(&io_close)> File(io_open(m_aPath, IOFLAG_READ), &io_close);
			if(!File)
				return {};
			std::unique_ptr<char, decltype(&free)> Text(io_read_all_str(File.get()), &free);
			return Text ? Text.get() : "";
		}
	};
}

TEST_F(FifoLifecycle, FreshAndDisabledInstancesCanBeClosedRepeatedly)
{
	m_Fifo.Update();
	m_Fifo.Shutdown();
	m_Fifo.Shutdown();
	EXPECT_FALSE(m_Fifo.IsInit());
	m_Fifo.Init(m_pConsole.get(), "", CFGFLAG_CLIENT);
	EXPECT_TRUE(m_Fifo.IsInit());
	m_Fifo.Update();
	m_Fifo.Shutdown();
	EXPECT_FALSE(m_Fifo.IsInit());
}

TEST_F(FifoLifecycle, DisabledInputCanBeEnabledLater)
{
	m_Fifo.Init(m_pConsole.get(), "", CFGFLAG_CLIENT);
	Start();
	ASSERT_TRUE(OpenWriter());
	ASSERT_TRUE(Send("fifo_probe\n"));
	m_Fifo.Update();
	EXPECT_EQ(m_Calls, 1);
}

TEST_F(FifoLifecycle, NormalCommandsKeepTheirExistingLineDispatch)
{
	Start();
	ASSERT_TRUE(OpenWriter());
	ASSERT_TRUE(Send("fifo_probe\nfifo_probe\r\nfifo_probe"));
	m_Fifo.Update();
	EXPECT_EQ(m_Calls, 3);
}

TEST_F(FifoLifecycle, InvalidUtf8LineDoesNotPreventNextValidLine)
{
	Start();
	ASSERT_TRUE(OpenWriter());
	ASSERT_TRUE(Send("\xff\nfifo_probe\n"));
	m_Fifo.Update();
	EXPECT_EQ(m_Calls, 1);
}

TEST_F(FifoLifecycle, CommandExceptionDropsConsumedBatchAndLaterInputRecovers)
{
	Start();
	ASSERT_TRUE(OpenWriter());
	ASSERT_TRUE(Send("fifo_throw\nfifo_probe\n"));
	m_Fifo.Update();
	EXPECT_EQ(m_Throws, 1);
	EXPECT_EQ(m_Calls, 0);
	ASSERT_TRUE(Send("fifo_probe\n"));
	m_Fifo.Update();
	EXPECT_EQ(m_Throws, 1);
	EXPECT_EQ(m_Calls, 1);
}

TEST_F(FifoLifecycle, ReinitializationReleasesPreviousEndpoint)
{
	Start();
	ASSERT_TRUE(OpenWriter());
	ASSERT_TRUE(Send("fifo_probe\n"));
	m_Fifo.Update();
	CloseWriter();
	Start();
	ASSERT_TRUE(OpenWriter());
	ASSERT_TRUE(Send("fifo_probe\n"));
	m_Fifo.Update();
	EXPECT_EQ(m_Calls, 2);
}

TEST_F(FifoLifecycle, OwnerDestructionClosesOwnedEndpoint)
{
	{
		CFifo Owner;
		Owner.Init(m_pConsole.get(), Name(), CFGFLAG_CLIENT);
		ASSERT_TRUE(Owner.IsInit());
	}
#if defined(CONF_FAMILY_WINDOWS)
	EXPECT_FALSE(OpenWriter());
#elif defined(CONF_FAMILY_UNIX)
	struct stat Attribute;
	EXPECT_NE(lstat(m_aPath, &Attribute), 0);
#endif
}

TEST_F(FifoLifecycle, ShutdownMakesLaterUpdateInactive)
{
	Start();
	ASSERT_TRUE(OpenWriter());
	ASSERT_TRUE(Send("fifo_probe\n"));
	m_Fifo.Shutdown();
	EXPECT_FALSE(m_Fifo.IsInit());
	m_Fifo.Update();
	m_Fifo.Shutdown();
	EXPECT_EQ(m_Calls, 0);
}

#if defined(CONF_FAMILY_UNIX)
TEST_F(FifoLifecycle, ExistingRegularFileIsNotOverwrittenOrRemoved)
{
	WriteRegular("keep original data");
	Start();
	EXPECT_EQ(ReadRegular(), "keep original data");
	m_Fifo.Shutdown();
	EXPECT_EQ(ReadRegular(), "keep original data");
}

TEST_F(FifoLifecycle, ExistingSymlinkAndTargetArePreserved)
{
	char aTarget[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "target.txt", aTarget, sizeof(aTarget));
	{
		std::unique_ptr<void, decltype(&io_close)> File(io_open(aTarget, IOFLAG_WRITE), &io_close);
		ASSERT_TRUE(File);
		ASSERT_EQ(io_write(File.get(), "kept", 4), 4u);
	}
	ASSERT_EQ(symlink("target.txt", m_aPath), 0);
	Start();
	m_Fifo.Shutdown();
	struct stat Attribute;
	ASSERT_EQ(lstat(m_aPath, &Attribute), 0);
	EXPECT_TRUE(S_ISLNK(Attribute.st_mode));
	EXPECT_EQ(ReadRegular(), "kept");
}

TEST_F(FifoLifecycle, ExistingFifoCanBeReadWithoutTakingPathOwnership)
{
	ASSERT_EQ(mkfifo(m_aPath, 0600), 0);
	Start();
	ASSERT_TRUE(OpenWriter());
	ASSERT_TRUE(Send("fifo_probe\n"));
	m_Fifo.Update();
	EXPECT_EQ(m_Calls, 1);
	m_Fifo.Shutdown();
	struct stat Attribute;
	ASSERT_EQ(lstat(m_aPath, &Attribute), 0);
	EXPECT_TRUE(S_ISFIFO(Attribute.st_mode));
}

TEST_F(FifoLifecycle, OwnedFifoIsRemovedAndRepeatedShutdownIsSafe)
{
	Start();
	struct stat Attribute;
	ASSERT_EQ(lstat(m_aPath, &Attribute), 0);
	ASSERT_TRUE(S_ISFIFO(Attribute.st_mode));
	m_Fifo.Shutdown();
	EXPECT_NE(lstat(m_aPath, &Attribute), 0);
	m_Fifo.Shutdown();
	EXPECT_FALSE(m_Fifo.IsInit());
}

TEST_F(FifoLifecycle, ReplacedRegularPathIsPreservedAtShutdown)
{
	Start();
	ASSERT_EQ(unlink(m_aPath), 0);
	WriteRegular("replacement");
	m_Fifo.Shutdown();
	EXPECT_EQ(ReadRegular(), "replacement");
}

TEST_F(FifoLifecycle, ReplacementFifoIsNotRemovedAsTheOwnedNode)
{
	Start();
	ASSERT_EQ(unlink(m_aPath), 0);
	ASSERT_EQ(mkfifo(m_aPath, 0600), 0);
	m_Fifo.Shutdown();
	struct stat Attribute;
	ASSERT_EQ(lstat(m_aPath, &Attribute), 0);
	EXPECT_TRUE(S_ISFIFO(Attribute.st_mode));
}
#endif
