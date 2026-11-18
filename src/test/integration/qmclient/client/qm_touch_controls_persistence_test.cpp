#include <game/client/components/touch_controls.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <new>
#include <string>

namespace
{
	constexpr int SAVE = IStorage::TYPE_SAVE;
	using qm_touch_controls::CONFIGURATION_FILENAME;

	class CSaveStorage
	{
		IStorage &m_Storage;

	public:
		std::string m_Temporary;
		bool m_UseReadOnlyHandle = false;
		bool m_RejectRename = false;
		int m_Renames = 0;

		explicit CSaveStorage(IStorage &Storage) :
			m_Storage(Storage) {}

		IOHANDLE OpenFile(const char *pPath, int Flags, int Type)
		{
			m_Temporary = pPath;
			IOHANDLE File = m_Storage.OpenFile(pPath, Flags, Type);
			if(File && m_UseReadOnlyHandle)
			{
				EXPECT_EQ(io_close(File), 0);
				return m_Storage.OpenFile(pPath, IOFLAG_READ, Type);
			}
			return File;
		}

		bool RenameFile(const char *pFrom, const char *pTo, int Type, bool AllowDestinationRemoval)
		{
			++m_Renames;
			EXPECT_FALSE(AllowDestinationRemoval);
			return !m_RejectRename && m_Storage.RenameFile(pFrom, pTo, Type, AllowDestinationRemoval);
		}

		bool RemoveFile(const char *pPath, int Type) { return m_Storage.RemoveFile(pPath, Type); }
	};

	class CQmTouchControlsPersistence : public ::testing::Test
	{
	protected:
		static constexpr char OLD_CONTENT[] = "{\"touch-buttons\":[]}\n";
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			IOHANDLE File = m_pStorage->OpenFile(CONFIGURATION_FILENAME, IOFLAG_WRITE, SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, OLD_CONTENT, sizeof(OLD_CONTENT) - 1), sizeof(OLD_CONTENT) - 1);
			EXPECT_EQ(io_close(File), 0);
		}

		std::string SavedContents()
		{
			void *pRaw = nullptr;
			unsigned Size = 0;
			EXPECT_TRUE(m_pStorage->ReadFile(CONFIGURATION_FILENAME, SAVE, &pRaw, &Size));
			const std::unique_ptr<void, decltype(&std::free)> pData(pRaw, std::free);
			return pData ? std::string(static_cast<const char *>(pData.get()), Size) : std::string();
		}

		static void WriteConfiguration(CJsonWriter &Writer)
		{
			Writer.BeginObject();
			Writer.WriteAttribute("direct-touch-ingame");
			Writer.WriteStrValue("hook");
			Writer.WriteAttribute("direct-touch-spectate");
			Writer.WriteStrValue("aim");
			Writer.WriteAttribute("touch-buttons");
			Writer.BeginArray();
			Writer.EndArray();
			Writer.EndObject();
		}

		void ExpectNoTemporary(const CSaveStorage &Storage)
		{
			ASSERT_FALSE(Storage.m_Temporary.empty());
			EXPECT_FALSE(m_pStorage->FileExists(Storage.m_Temporary.c_str(), SAVE));
		}
	};
}

TEST_F(CQmTouchControlsPersistence, CompleteWriteReplacesTheLayout)
{
	CSaveStorage Storage(*m_pStorage);
	ASSERT_TRUE(qm_touch_controls::SaveConfiguration(Storage, WriteConfiguration));

	CJsonStringWriter Expected;
	WriteConfiguration(Expected);
	EXPECT_EQ(SavedContents(), Expected.GetOutputString());
	EXPECT_EQ(Storage.m_Renames, 1);
	ExpectNoTemporary(Storage);
}

TEST_F(CQmTouchControlsPersistence, WriteErrorPreservesTheLayout)
{
	CSaveStorage Storage(*m_pStorage);
	Storage.m_UseReadOnlyHandle = true;
	EXPECT_FALSE(qm_touch_controls::SaveConfiguration(Storage, WriteConfiguration));

	EXPECT_EQ(SavedContents(), OLD_CONTENT);
	EXPECT_EQ(Storage.m_Renames, 0);
	ExpectNoTemporary(Storage);
}

TEST_F(CQmTouchControlsPersistence, SerializationExceptionPreservesTheLayout)
{
	CSaveStorage Storage(*m_pStorage);
	EXPECT_FALSE(qm_touch_controls::SaveConfiguration(Storage, [](CJsonWriter &Writer) {
		Writer.BeginObject();
		throw std::bad_alloc();
	}));

	EXPECT_EQ(SavedContents(), OLD_CONTENT);
	EXPECT_EQ(Storage.m_Renames, 0);
	ExpectNoTemporary(Storage);
}

TEST_F(CQmTouchControlsPersistence, RenameFailurePreservesTheLayoutAndPermitsRetry)
{
	CSaveStorage Storage(*m_pStorage);
	Storage.m_RejectRename = true;
	EXPECT_FALSE(qm_touch_controls::SaveConfiguration(Storage, WriteConfiguration));
	EXPECT_EQ(SavedContents(), OLD_CONTENT);
	EXPECT_EQ(Storage.m_Renames, 1);
	ExpectNoTemporary(Storage);

	Storage.m_RejectRename = false;
	EXPECT_TRUE(qm_touch_controls::SaveConfiguration(Storage, WriteConfiguration));
	CJsonStringWriter Expected;
	WriteConfiguration(Expected);
	EXPECT_EQ(SavedContents(), Expected.GetOutputString());
	ExpectNoTemporary(Storage);
}
