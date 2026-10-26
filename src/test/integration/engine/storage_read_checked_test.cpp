#include <base/system.h>

#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <string>

namespace
{
	using EResult = IStorage::EReadFileResult;
	class CCheckedStorageReadTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		void *m_pData = nullptr;
		unsigned m_Size = 0;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}
		void TearDown() override { free(m_pData); }
		std::string Path(const char *pName)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, pName, aPath, sizeof(aPath));
			return aPath;
		}
		void Write(const char *pName, const std::string &Data)
		{
			IOHANDLE File = m_pStorage->OpenFile(pName, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Data.data(), static_cast<unsigned>(Data.size())), Data.size());
			EXPECT_EQ(io_close(File), 0);
		}
		EResult Read(const char *pName, int Type = IStorage::TYPE_SAVE, IStorage *pStorage = nullptr)
		{
			free(m_pData);
			m_pData = nullptr;
			m_Size = 999;
			return (pStorage ? pStorage : m_pStorage.get())->ReadFileChecked(pName, Type, &m_pData, &m_Size);
		}
		void ExpectFailure(EResult Result, EResult Expected)
		{
			EXPECT_EQ(Result, Expected);
			EXPECT_EQ(m_pData, nullptr);
			EXPECT_EQ(m_Size, 0u);
		}
		std::string Contents() const { return std::string(static_cast<const char *>(m_pData), m_Size); }
	};

	class CLayeredCheckedStorageReadTest : public CCheckedStorageReadTest
	{
	protected:
		std::unique_ptr<IStorage> m_pLayered;
		std::string m_Save;
		std::string m_Data;
		std::string m_Relative;
		void SetUp() override
		{
			CCheckedStorageReadTest::SetUp();
			ASSERT_FALSE(HasFatalFailure());
			for(const char *pDir : {"read_save", "read_app", "read_app/data", "read_app/data/mapres"})
				ASSERT_TRUE(m_pStorage->CreateFolder(pDir, IStorage::TYPE_SAVE));
			m_Save = Path("read_save");
			m_Data = Path("read_app/data");
			const auto Executable = Path("read_app/client.exe");
			const char *apArgs[] = {Executable.c_str()};
			m_pLayered = CreateTempStorage(m_Save.c_str(), 1, apArgs);
			ASSERT_NE(m_pLayered, nullptr);
			m_Relative = std::string("read_") + m_Info.m_aFilename;
			ASSERT_EQ(fs_makedir((m_Save + "/" + m_Relative).c_str()), 0);
			ASSERT_EQ(fs_makedir((m_Data + "/" + m_Relative).c_str()), 0);
		}
		void WriteLayer(const char *pPrefix, const char *pValue)
		{
			Write((std::string(pPrefix) + "/" + m_Relative + "/skin.json").c_str(), pValue);
		}
		EResult ReadLayers() { return Read((m_Relative + "/skin.json").c_str(), IStorage::TYPE_ALL, m_pLayered.get()); }
	};
}

TEST_F(CCheckedStorageReadTest, CheckedOpenMarksMissingAndClearsFlagOnLaterSuccess)
{
	bool NotFound = false;
	EXPECT_EQ(io_open_read_checked(Path("skin.json").c_str(), &NotFound), nullptr);
	EXPECT_TRUE(NotFound);
	Write("skin.json", "ready");
	ASSERT_FALSE(HasFatalFailure());
	IOHANDLE File = io_open_read_checked(Path("skin.json").c_str(), &NotFound);
	ASSERT_NE(File, nullptr);
	EXPECT_FALSE(NotFound);
	char aBuffer[5];
	EXPECT_EQ(io_read(File, aBuffer, sizeof(aBuffer)), sizeof(aBuffer));
	EXPECT_EQ(std::string(aBuffer, sizeof(aBuffer)), "ready");
	EXPECT_EQ(io_close(File), 0);
}

TEST_F(CCheckedStorageReadTest, InvalidUtf8OpenDoesNotMasqueradeAsMissing)
{
	bool NotFound = true;
	EXPECT_EQ(io_open_read_checked("invalid_\xff", &NotFound), nullptr);
	EXPECT_FALSE(NotFound);
}

TEST_F(CCheckedStorageReadTest, MissingFileClearsPreviousOutputValues)
{
	int Old = 0;
	m_pData = &Old;
	m_Size = 123;
	const auto Result = m_pStorage->ReadFileChecked("missing.json", IStorage::TYPE_SAVE, &m_pData, &m_Size);
	ExpectFailure(Result, EResult::NOT_FOUND);
	// 输出必须已清空，不能让测试清理意外释放借用地址。
	if(m_pData == &Old)
		m_pData = nullptr;
}

TEST_F(CCheckedStorageReadTest, EmptyExistingFileSucceedsWithOwnedTerminatedBuffer)
{
	Write("empty.json", "");
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(Read("empty.json"), EResult::SUCCESS);
	ASSERT_NE(m_pData, nullptr);
	EXPECT_EQ(m_Size, 0u);
	EXPECT_EQ(*static_cast<const char *>(m_pData), '\0');
}

TEST_F(CCheckedStorageReadTest, BinaryContentsKeepInteriorZerosAndExtraTerminator)
{
	const std::string Data("a\0b", 3);
	Write("皮肤.json", Data);
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(Read("皮肤.json"), EResult::SUCCESS);
	ASSERT_NE(m_pData, nullptr);
	EXPECT_EQ(Contents(), Data);
	EXPECT_EQ(static_cast<const char *>(m_pData)[m_Size], '\0');
}

TEST_F(CCheckedStorageReadTest, DirectoryReadIsFailureAndReplacementFileCanRecover)
{
	ASSERT_TRUE(m_pStorage->CreateFolder("skin.json", IStorage::TYPE_SAVE));
	ExpectFailure(Read("skin.json"), EResult::IO_FAILURE);
	ASSERT_TRUE(m_pStorage->RemoveFolder("skin.json", IStorage::TYPE_SAVE));
	Write("skin.json", "recovered");
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(Read("skin.json"), EResult::SUCCESS);
	EXPECT_EQ(Contents(), "recovered");
}

TEST_F(CCheckedStorageReadTest, OversizedJoinedPathIsRejectedBeforeTruncation)
{
	const std::string Name(IO_MAX_PATH_LENGTH, 'a');
	ExpectFailure(Read(Name.c_str()), EResult::IO_FAILURE);
}

TEST_F(CCheckedStorageReadTest, InvalidUtf8StoragePathReportsFailure)
{
	ExpectFailure(Read("invalid_\xff"), EResult::IO_FAILURE);
}

TEST_F(CCheckedStorageReadTest, RelativeTraversalIsRejectedAsFailure)
{
	ExpectFailure(Read("../skin.json"), EResult::IO_FAILURE);
}

TEST_F(CCheckedStorageReadTest, AbsoluteAndTranslatedTypesReadTheSameNativeFile)
{
	Write("skin.json", "absolute");
	ASSERT_FALSE(HasFatalFailure());
	auto Absolute = Path("skin.json");
	if(fs_is_relative_path(Absolute.c_str()))
	{
		char aCwd[IO_MAX_PATH_LENGTH];
		ASSERT_NE(fs_getcwd(aCwd, sizeof(aCwd)), nullptr);
		Absolute = std::string(aCwd) + "/" + Absolute;
	}
	for(int Type : {IStorage::TYPE_ABSOLUTE, IStorage::TYPE_SAVE_OR_ABSOLUTE, IStorage::TYPE_ALL_OR_ABSOLUTE})
	{
		ASSERT_EQ(Read(Absolute.c_str(), Type), EResult::SUCCESS);
		EXPECT_EQ(Contents(), "absolute");
	}
	ASSERT_EQ(Read("skin.json", IStorage::TYPE_SAVE_OR_ABSOLUTE), EResult::SUCCESS);
	EXPECT_EQ(Contents(), "absolute");
}

TEST_F(CLayeredCheckedStorageReadTest, EarlierRootContentsTakePriority)
{
	WriteLayer("read_save", "save");
	WriteLayer("read_app/data", "data");
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(ReadLayers(), EResult::SUCCESS);
	EXPECT_EQ(Contents(), "save");
}

TEST_F(CLayeredCheckedStorageReadTest, MissingEarlierRootFallsBackToData)
{
	WriteLayer("read_app/data", "data");
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(ReadLayers(), EResult::SUCCESS);
	EXPECT_EQ(Contents(), "data");
}

TEST_F(CLayeredCheckedStorageReadTest, EarlierReadErrorIsNotHiddenByLaterSuccess)
{
	ASSERT_EQ(fs_makedir((m_Save + "/" + m_Relative + "/skin.json").c_str()), 0);
	WriteLayer("read_app/data", "data");
	ASSERT_FALSE(HasFatalFailure());
	ExpectFailure(ReadLayers(), EResult::IO_FAILURE);
}

TEST_F(CCheckedStorageReadTest, DefaultBackendReportsUnsupportedAndClearsOutputs)
{
	m_Size = 123;
	ExpectFailure(m_pStorage->IStorage::ReadFileChecked("unused", IStorage::TYPE_SAVE, &m_pData, &m_Size), EResult::UNSUPPORTED);
}
