#include "test.h"

#include <base/io.h>
#include <base/windows.h>
#include <engine/storage.h>
#include <game/client/ui/card_preferences_storage.h>
#include <game/client/ui/card_ui_model.h>

#include <gtest/gtest.h>

#include <limits>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#endif

class CCardPreferencesTest : public ::testing::Test
{
protected:
	static CCardRegistry CreateRegistry()
	{
		CCardRegistry Registry;
		EXPECT_TRUE(Registry.RegisterPage({"home", "Home", 0}));
		EXPECT_TRUE(Registry.RegisterPage({"other", "Other", 1}));
		EXPECT_TRUE(Registry.RegisterCard({"ddnet.a", "home", "Official", {}, "icon"}));
		EXPECT_TRUE(Registry.RegisterCard({"qm.b", "home", "Qm", {}, "icon"}));
		Registry.Freeze();
		return Registry;
	}

	CCardRegistry m_Registry = CreateRegistry();
	CCardUiModel m_Model{m_Registry};
};

TEST_F(CCardPreferencesTest, DeterministicRoundTripAndReplacement)
{
	ASSERT_TRUE(m_Model.SetPreferences("qm.b", {false, true, 0}));
	ASSERT_TRUE(m_Model.SetPreferences("ddnet.a", {true, false, std::numeric_limits<int>::max()}));
	const std::string Json = SerializeCardPreferences(m_Model);
	EXPECT_LT(Json.find("ddnet.a"), Json.find("qm.b"));
	CCardUiModel Loaded(m_Registry);
	std::string Error;
	ASSERT_TRUE(ParseCardPreferences(Json, Loaded, Error)) << Error;
	EXPECT_EQ(SerializeCardPreferences(Loaded), Json);
	EXPECT_FALSE(Loaded.IsDirty());
	EXPECT_EQ(Loaded.Preferences("qm.b").m_Order, 0);
	ASSERT_TRUE(ParseCardPreferences(R"({"version":1,"cards":[]})", Loaded, Error));
	EXPECT_TRUE(Loaded.ExportPreferences().empty());
	EXPECT_TRUE(Loaded.Preferences("qm.b").m_Visible);
}

TEST_F(CCardPreferencesTest, RejectsMalformedSchemaAtomically)
{
	ASSERT_TRUE(m_Model.SetPreferences("qm.b", {false, true, 7}));
	const std::string Before = SerializeCardPreferences(m_Model);
	const char *apInvalid[] = {
		"",
		"null",
		R"({"version":2,"cards":[]})",
		R"({"version":1,"version":1,"cards":[]})",
		R"({"version":1,"cards":{}})",
		R"({"version":1,"cards":[{"id":"qm.b","visible":1,"collapsed":false,"order":0}]})",
		R"({"version":1,"cards":[{"id":"qm.b","visible":true,"collapsed":false,"order":2147483648}]})",
		R"({"version":1,"cards":[{"id":"qm.b","visible":true,"collapsed":false,"order":-2147483649}]})",
		R"({"version":1,"cards":[{"id":"qm.b","visible":true,"collapsed":false,"order":0.5}]})",
		R"({"version":1,"cards":[{"id":"qm.b","visible":true,"visible":false,"order":0}]})",
		R"({"version":1,"cards":[{"id":"qm.b\u0000other","visible":true,"collapsed":false,"order":0}]})",
		R"({"version":1,"cards":[{"id":"qm.b","visible":true,"collapsed":false,"order":0},{"id":"qm.b","visible":false,"collapsed":false,"order":1}]})",
	};
	for(const char *pJson : apInvalid)
	{
		std::string Error;
		EXPECT_FALSE(ParseCardPreferences(pJson, m_Model, Error)) << pJson;
		EXPECT_FALSE(Error.empty()) << pJson;
		EXPECT_EQ(SerializeCardPreferences(m_Model), Before) << pJson;
		EXPECT_TRUE(m_Model.IsDirty());
	}
	std::string Error;
	EXPECT_FALSE(ParseCardPreferences(std::string(1024 * 1024 + 1, ' '), m_Model, Error));
	EXPECT_EQ(SerializeCardPreferences(m_Model), Before);
}

TEST_F(CCardPreferencesTest, SkipsUnregisteredCardsAndRestoresDefaults)
{
	std::string Error;
	ASSERT_TRUE(ParseCardPreferences(R"({"version":1,"cards":[{"id":"qm.future","visible":false,"collapsed":true,"order":4}]})", m_Model, Error));
	EXPECT_TRUE(m_Model.ExportPreferences().empty());
	EXPECT_FALSE(m_Model.IsDirty());
	ASSERT_TRUE(m_Model.SetPreferences("qm.b", {false, true, 9}));
	ASSERT_TRUE(m_Model.ResetPreferences("qm.b"));
	EXPECT_TRUE(m_Model.Preferences("qm.b").m_Visible);
	EXPECT_FALSE(m_Model.Preferences("qm.b").m_Collapsed);
	m_Model.ClearDirty();
	EXPECT_FALSE(m_Model.ResetPreferences("qm.missing"));
	EXPECT_FALSE(m_Model.IsDirty());
	ASSERT_TRUE(m_Model.SetPreferences("qm.b", {true, false, m_Model.Preferences("qm.b").m_Order}));
	EXPECT_FALSE(m_Model.IsDirty());
}

TEST_F(CCardPreferencesTest, StorageRoundTripAndNoWriteForUntouchedDefaults)
{
	CTestInfo Info;
	Info.m_DeleteTestStorageFilesOnSuccess = true;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	std::string Error;
	ASSERT_TRUE(LoadCardPreferences(*pStorage, m_Model, Error));
	ASSERT_TRUE(SaveCardPreferences(*pStorage, m_Model, Error));
	EXPECT_FALSE(pStorage->FileExists(CARD_PREFERENCES_PATH, IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_Model.SetPreferences("qm.b", {false, true, 42}));
	ASSERT_TRUE(SaveCardPreferences(*pStorage, m_Model, Error)) << Error;
	EXPECT_FALSE(m_Model.IsDirty());
	CCardUiModel Loaded(m_Registry);
	ASSERT_TRUE(LoadCardPreferences(*pStorage, Loaded, Error)) << Error;
	EXPECT_FALSE(Loaded.Preferences("qm.b").m_Visible);
	EXPECT_EQ(Loaded.Preferences("qm.b").m_Order, 1);
	Loaded.ResetAllPreferences();
	ASSERT_TRUE(SaveCardPreferences(*pStorage, Loaded, Error)) << Error;
	ASSERT_TRUE(LoadCardPreferences(*pStorage, m_Model, Error)) << Error;
	EXPECT_TRUE(m_Model.ExportPreferences().empty());
}

TEST_F(CCardPreferencesTest, CorruptFilePreservesModelAndSaveFailureKeepsDirty)
{
	CTestInfo Info;
	Info.m_DeleteTestStorageFilesOnSuccess = true;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
	IOHANDLE File = pStorage->OpenFile(CARD_PREFERENCES_PATH, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_write(File, "bad", 3), 3u);
	EXPECT_EQ(io_close(File), 0);
	ASSERT_TRUE(m_Model.SetPreferences("qm.b", {false, true, 5}));
	std::string Error;
	EXPECT_FALSE(LoadCardPreferences(*pStorage, m_Model, Error));
	EXPECT_EQ(m_Model.Preferences("qm.b").m_Order, 1);
	EXPECT_TRUE(m_Model.IsDirty());
	ASSERT_TRUE(pStorage->RemoveFile(CARD_PREFERENCES_PATH, IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->CreateFolder(CARD_PREFERENCES_PATH, IStorage::TYPE_SAVE));
	EXPECT_FALSE(SaveCardPreferences(*pStorage, m_Model, Error));
	EXPECT_FALSE(Error.empty());
	EXPECT_TRUE(m_Model.IsDirty());
	EXPECT_TRUE(pStorage->FolderExists(CARD_PREFERENCES_PATH, IStorage::TYPE_SAVE));
}

TEST_F(CCardPreferencesTest, VersionTwoPreservesPageColumnOrderAndView)
{
	ASSERT_TRUE(m_Model.MoveCard("qm.b", "other", ECardColumn::RIGHT, 0));
	ASSERT_TRUE(m_Model.SetPreferences("qm.b", {true, true, 0}));
	ASSERT_TRUE(m_Model.SetViewPreferences({1, true, false}));
	const std::string Json = SerializeCardPreferences(m_Model);
	CCardUiModel Loaded(m_Registry);
	std::string Error;
	ASSERT_TRUE(ParseCardPreferences(Json, Loaded, Error)) << Error;
	EXPECT_EQ(SerializeCardPreferences(Loaded), Json);
	ASSERT_NE(Loaded.OrderModel().Find("qm.b"), nullptr);
	EXPECT_EQ(Loaded.OrderModel().Find("qm.b")->m_PageId, "other");
	EXPECT_EQ(Loaded.OrderModel().Find("qm.b")->m_Column, ECardColumn::RIGHT);
	EXPECT_EQ(Loaded.Preferences("qm.b").m_Order, Loaded.OrderModel().Find("qm.b")->m_Order);
	EXPECT_TRUE(Loaded.Preferences("qm.b").m_Collapsed);
	ASSERT_EQ(Loaded.CardsForPage("other").size(), 1);
	EXPECT_EQ(Loaded.CardsForPage("other").front()->m_Id, "qm.b");
	EXPECT_EQ(Loaded.ViewPreferences().m_Mode, 1);
	EXPECT_TRUE(Loaded.ViewPreferences().m_LightTheme);
	EXPECT_FALSE(Loaded.ViewPreferences().m_Animations);
	EXPECT_FALSE(Loaded.IsDirty());
}

TEST_F(CCardPreferencesTest, InvalidPlacementAndViewDoNotMutateTheModel)
{
	ASSERT_TRUE(m_Model.SetPreferences("qm.b", {false, true, 1}));
	const std::string Before = SerializeCardPreferences(m_Model);
	EXPECT_FALSE(m_Model.MoveCard("qm.b", "missing", ECardColumn::FULL, 0));
	EXPECT_FALSE(m_Model.SetPreferences("qm.b", {true, false, -1}));
	EXPECT_FALSE(m_Model.SetViewPreferences({3, false, true}));
	EXPECT_FALSE(m_Model.ReplaceState({}, {{"qm.b", "missing", ECardColumn::FULL, 0}}));
	EXPECT_FALSE(m_Model.ReplaceState({}, {{"qm.b", "home", static_cast<ECardColumn>(3), 0}}));
	EXPECT_EQ(SerializeCardPreferences(m_Model), Before);
	std::string Error;
	EXPECT_FALSE(ParseCardPreferences(R"({"version":2,"cards":[],"placements":[{"id":"qm.b","page":"home","column":9,"order":0}],"view":{"mode":0,"light":false,"animations":true}})", m_Model, Error));
	EXPECT_EQ(SerializeCardPreferences(m_Model), Before);
}

TEST_F(CCardPreferencesTest, VisibilityChangesCannotCreateASecondOrderSource)
{
	ASSERT_TRUE(m_Model.SetPreferences("qm.b", {true, false, 0}));
	ASSERT_EQ(m_Model.CardsForPage("home").front()->m_Id, "qm.b");
	auto Preferences = m_Model.Preferences("qm.b");
	Preferences.m_Visible = false;
	ASSERT_TRUE(m_Model.SetPreferences("qm.b", Preferences));
	EXPECT_EQ(m_Model.OrderModel().Find("qm.b")->m_Order, 0);
	Preferences.m_Visible = true;
	ASSERT_TRUE(m_Model.SetPreferences("qm.b", Preferences));
	EXPECT_EQ(m_Model.CardsForPage("home").front()->m_Id, "qm.b");
	m_Model.ResetAllPreferences();
	EXPECT_EQ(m_Model.CardsForPage("home").front()->m_Id, "ddnet.a");
	EXPECT_EQ(m_Model.Preferences("qm.b").m_Order, 1);
}

#if defined(CONF_FAMILY_WINDOWS)
TEST_F(CCardPreferencesTest, LockedDestinationSavePreservesPreviousFile)
{
	CTestInfo Info;
	Info.m_DeleteTestStorageFilesOnSuccess = true;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	std::string Error;
	ASSERT_TRUE(m_Model.SetPreferences("qm.b", {false, true, 0}));
	ASSERT_TRUE(SaveCardPreferences(*pStorage, m_Model, Error)) << Error;
	const std::string Before = SerializeCardPreferences(m_Model);
	char aPath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, CARD_PREFERENCES_PATH, aPath, sizeof(aPath));
	const HANDLE Lock = CreateFileW(windows_utf8_to_wide(aPath).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	ASSERT_NE(Lock, INVALID_HANDLE_VALUE);
	m_Model.ResetAllPreferences();
	EXPECT_FALSE(SaveCardPreferences(*pStorage, m_Model, Error));
	EXPECT_TRUE(m_Model.IsDirty());
	EXPECT_TRUE(CloseHandle(Lock));
	EXPECT_TRUE(pStorage->FileExists(CARD_PREFERENCES_PATH, IStorage::TYPE_SAVE));
	CCardUiModel Loaded(m_Registry);
	ASSERT_TRUE(LoadCardPreferences(*pStorage, Loaded, Error)) << Error;
	EXPECT_EQ(SerializeCardPreferences(Loaded), Before);
}
#endif
