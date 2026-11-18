#include <base/io.h>
#include <base/str.h>

#include <engine/console.h>
#include <engine/engine.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <test/test.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>

namespace
{
constexpr char OLD_CAPTURE[] = "earlier capture";

class CNetworkLogStorage final : public IStorage
{
	IStorage &m_Storage;

public:
	explicit CNetworkLogStorage(IStorage &Storage) :
		m_Storage(Storage)
	{
	}

	bool m_OccupyNextSent = false;
	bool m_RejectWrites = false;
	bool m_RejectReceived = false;
	char m_aOccupied[IO_MAX_PATH_LENGTH] = {};
	char m_aSent[IO_MAX_PATH_LENGTH] = {};
	char m_aReceived[IO_MAX_PATH_LENGTH] = {};

	IOHANDLE OpenFile(const char *pName, int Flags, int Type, char *pBuffer, int Size) override
	{
		if(!(Flags & IOFLAG_WRITE))
			return m_Storage.OpenFile(pName, Flags, Type, pBuffer, Size);
		const bool Sent = str_startswith(pName, "dumps/network_sent_") != nullptr;
		if(m_RejectWrites || (!Sent && m_RejectReceived))
			return nullptr;
		if(Sent && m_OccupyNextSent)
		{
			// 在独占创建前由另一个写入方占用同名路径。
			m_OccupyNextSent = false;
			str_copy(m_aOccupied, pName);
			IOHANDLE Existing = m_Storage.OpenFile(pName, IOFLAG_WRITE, Type);
			if(!Existing)
			{
				ADD_FAILURE() << "Could not prepare the existing capture";
				return nullptr;
			}
			EXPECT_EQ(io_write(Existing, OLD_CAPTURE, sizeof(OLD_CAPTURE) - 1), sizeof(OLD_CAPTURE) - 1);
			EXPECT_EQ(io_close(Existing), 0);
		}
		IOHANDLE File = m_Storage.OpenFile(pName, Flags, Type, pBuffer, Size);
		if(File && Sent)
			str_copy(m_aSent, pName);
		else if(File)
			str_copy(m_aReceived, pName);
		return File;
	}

	int NumPaths() const override { return m_Storage.NumPaths(); }
	bool GetDataPath(const char *pName, char *pBuffer, unsigned Size) const override { return m_Storage.GetDataPath(pName, pBuffer, Size); }
	bool GetExecutablePath(const char *pName, char *pBuffer, unsigned Size) const override { return m_Storage.GetExecutablePath(pName, pBuffer, Size); }
	void ListDirectory(int Type, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override { m_Storage.ListDirectory(Type, pPath, Callback, pUser); }
	void ListDirectoryInfo(int Type, const char *pPath, FS_LISTDIR_CALLBACK_FILEINFO Callback, void *pUser) override { m_Storage.ListDirectoryInfo(Type, pPath, Callback, pUser); }
	bool FileExists(const char *pName, int Type) override { return m_Storage.FileExists(pName, Type); }
	bool FolderExists(const char *pName, int Type) override { return m_Storage.FolderExists(pName, Type); }
	bool ReadFile(const char *pName, int Type, void **ppResult, unsigned *pSize) override { return m_Storage.ReadFile(pName, Type, ppResult, pSize); }
	char *ReadFileStr(const char *pName, int Type) override { return m_Storage.ReadFileStr(pName, Type); }
	bool RetrieveTimes(const char *pName, int Type, time_t *pCreated, time_t *pModified) override { return m_Storage.RetrieveTimes(pName, Type, pCreated, pModified); }
	bool CalculateHashes(const char *pName, int Type, SHA256_DIGEST *pSha, unsigned *pCrc) override { return m_Storage.CalculateHashes(pName, Type, pSha, pCrc); }
	bool FindFile(const char *pName, const char *pPath, int Type, char *pBuffer, int Size) override { return m_Storage.FindFile(pName, pPath, Type, pBuffer, Size); }
	size_t FindFiles(const char *pName, const char *pPath, int Type, std::set<std::string> *pEntries) override { return m_Storage.FindFiles(pName, pPath, Type, pEntries); }
	bool RemoveFile(const char *pName, int Type) override { return m_Storage.RemoveFile(pName, Type); }
	bool RemoveFolder(const char *pName, int Type) override { return m_Storage.RemoveFolder(pName, Type); }
	bool RenameFile(const char *pFrom, const char *pTo, int Type, bool AllowRemoval) override { return m_Storage.RenameFile(pFrom, pTo, Type, AllowRemoval); }
	bool CreateFolder(const char *pName, int Type) override { return m_Storage.CreateFolder(pName, Type); }
	void GetCompletePath(int Type, const char *pName, char *pBuffer, unsigned Size) override { m_Storage.GetCompletePath(Type, pName, pBuffer, Size); }
	bool RemoveBinaryFile(const char *pName) override { return m_Storage.RemoveBinaryFile(pName); }
	bool RenameBinaryFile(const char *pFrom, const char *pTo) override { return m_Storage.RenameBinaryFile(pFrom, pTo); }
	const char *GetBinaryPath(const char *pName) override { return m_Storage.GetBinaryPath(pName); }
	const char *GetBinaryPathAbsolute(const char *pName) override { return m_Storage.GetBinaryPathAbsolute(pName); }
};

class EngineNetworkLog : public ::testing::Test
{
protected:
	CTestInfo m_Info;
	std::unique_ptr<IStorage> m_pStorage;
	std::unique_ptr<CNetworkLogStorage> m_pLogStorage;
	std::unique_ptr<IConsole> m_pConsole;
	std::unique_ptr<IKernel> m_pKernel;
	std::unique_ptr<IEngine> m_pEngine;

	void SetUp() override
	{
		m_pStorage = m_Info.CreateTestStorage();
		ASSERT_NE(m_pStorage, nullptr);
		ASSERT_TRUE(m_pStorage->CreateFolder("dumps", IStorage::TYPE_SAVE));
		m_pLogStorage = std::make_unique<CNetworkLogStorage>(*m_pStorage);
		m_pConsole = CreateConsole(CFGFLAG_CLIENT);
		m_pKernel.reset(IKernel::Create());
		m_pEngine.reset(CreateTestEngine("network-log"));
		ASSERT_NE(m_pEngine, nullptr);
		m_pKernel->RegisterInterface(static_cast<IStorage *>(m_pLogStorage.get()), false);
		m_pKernel->RegisterInterface(m_pConsole.get(), false);
		m_pKernel->RegisterInterface(m_pEngine.get(), false);
		m_pEngine->Init();
	}

	void Toggle()
	{
		m_pConsole->ExecuteLine("dbg_lognetwork");
	}
};
} // namespace

TEST_F(EngineNetworkLog, OccupiedPathKeepsExistingCaptureAndStartsAnotherPair)
{
	m_pLogStorage->m_OccupyNextSent = true;
	Toggle();
	Toggle();

	ASSERT_NE(m_pLogStorage->m_aOccupied[0], '\0');
	const std::unique_ptr<char, decltype(&std::free)> Existing(
		m_pStorage->ReadFileStr(m_pLogStorage->m_aOccupied, IStorage::TYPE_SAVE), std::free);
	ASSERT_NE(Existing, nullptr);
	EXPECT_STREQ(Existing.get(), OLD_CAPTURE);
	EXPECT_STRNE(m_pLogStorage->m_aSent, m_pLogStorage->m_aOccupied);
	ASSERT_NE(m_pLogStorage->m_aSent[0], '\0');
	ASSERT_NE(m_pLogStorage->m_aReceived[0], '\0');
	EXPECT_STREQ(str_startswith(m_pLogStorage->m_aSent, "dumps/network_sent_"),
		str_startswith(m_pLogStorage->m_aReceived, "dumps/network_recv_"));
}

TEST_F(EngineNetworkLog, FailedStartCanRetryOnNextToggle)
{
	m_pLogStorage->m_RejectWrites = true;
	Toggle();
	EXPECT_EQ(m_pLogStorage->m_aSent[0], '\0');
	EXPECT_EQ(m_pLogStorage->m_aReceived[0], '\0');

	m_pLogStorage->m_RejectWrites = false;
	Toggle();
	EXPECT_NE(m_pLogStorage->m_aSent[0], '\0');
	EXPECT_NE(m_pLogStorage->m_aReceived[0], '\0');
}

TEST_F(EngineNetworkLog, PartialStartCanStillBeStopped)
{
	m_pLogStorage->m_RejectReceived = true;
	Toggle();
	ASSERT_NE(m_pLogStorage->m_aSent[0], '\0');
	ASSERT_EQ(m_pLogStorage->m_aReceived[0], '\0');

	m_pLogStorage->m_RejectReceived = false;
	Toggle();
	EXPECT_EQ(m_pLogStorage->m_aReceived[0], '\0');
	Toggle();
	EXPECT_NE(m_pLogStorage->m_aReceived[0], '\0');
}
