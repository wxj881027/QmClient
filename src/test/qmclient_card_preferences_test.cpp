#include "test.h"

#include <base/io.h>
#include <base/windows.h>
#include <engine/storage.h>
#include <game/client/ui/card_preferences_storage.h>
#include <game/client/ui/card_ui_model.h>

#include <gtest/gtest.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#endif

class CCardPreferencesTest : public ::testing::Test
{
protected:
	static CCardRegistry CreateRegistry()
	{
		CCardRegistry Registry;
		EXPECT_TRUE(Registry.RegisterCard({"ddnet.a", "Official", {}, "icon", "", {}, ECardOwner::UPSTREAM, 0, true}));
		EXPECT_TRUE(Registry.RegisterCard({"qm.b", "Qm", {}, "icon", "", {}, ECardOwner::QM, 10, true}));
		EXPECT_TRUE(Registry.RegisterPage({"home", "Home", 0, {"ddnet.a", "qm.b"}}));
		EXPECT_TRUE(Registry.RegisterPage({"other", "Other", 1, {}}));
		Registry.Freeze();
		return Registry;
	}

	CCardRegistry m_Registry = CreateRegistry();
	CCardUiModel m_Model{m_Registry};
};

TEST_F(CCardPreferencesTest, DeterministicRoundTripAndReplacement)
{
	ASSERT_TRUE(m_Model.SetPreferences("home", "qm.b", {false, true}));
	ASSERT_TRUE(m_Model.SetPreferences("home", "ddnet.a", {false, false}));
	ASSERT_TRUE(m_Model.MoveCardWithinPage("home", "qm.b", ECardColumn::RIGHT, 0));
	const std::string Json = SerializeCardPreferences(m_Model);
	// 导出按 (page, card) 稳定排序。
	EXPECT_LT(Json.find("\"ddnet.a\""), Json.find("\"qm.b\""));
	CCardUiModel Loaded(m_Registry);
	std::string Error;
	ASSERT_TRUE(ParseCardPreferences(Json, Loaded, Error)) << Error;
	EXPECT_EQ(SerializeCardPreferences(Loaded), Json);
	EXPECT_FALSE(Loaded.IsDirty());
	EXPECT_EQ(Loaded.OrderModel().Find("home", "qm.b")->m_Column, ECardColumn::RIGHT);
	// 空状态替换回默认声明。
	SCardUiState Empty;
	ASSERT_TRUE(Loaded.ImportState(Empty, Error));
	EXPECT_TRUE(Loaded.Preferences("home", "qm.b").m_Visible);
	EXPECT_EQ(Loaded.OrderModel().Find("home", "qm.b")->m_Column, ECardColumn::FULL);
	EXPECT_FALSE(m_Model.ResetPreferences("home", "missing"));
}

TEST_F(CCardPreferencesTest, RejectsMalformedSchemaAtomically)
{
	ASSERT_TRUE(m_Model.SetPreferences("home", "qm.b", {false, true}));
	ASSERT_TRUE(m_Model.SetPreferences("home", "ddnet.a", {false, false}));
	const std::string Before = SerializeCardPreferences(m_Model);
	const char *apInvalid[] = {
		"",
		"null",
		R"({"version":4,"placements":[],"view":{"mode":0,"light":false,"animations":true}})",
		R"({"version":0,"placements":[],"view":{"mode":0,"light":false,"animations":true}})",
		R"({"version":3,"version":3,"placements":[],"view":{"mode":0,"light":false,"animations":true}})",
		R"({"version":3,"placements":{}})",
		R"({"version":3,"placements":[],"view":{}})",
		R"({"version":3,"placements":[{"card":"qm.b","page":"home","column":1,"order":0}],"view":{"mode":0,"light":false,"animations":true}})",
		R"({"version":3,"placements":[{"card":"qm.b","page":"home","column":9,"order":0,"present":true,"visible":true,"collapsed":false}],"view":{"mode":0,"light":false,"animations":true}})",
		R"({"version":3,"placements":[{"card":"qm.b","page":"home","column":1,"order":-1,"present":true,"visible":true,"collapsed":false}],"view":{"mode":0,"light":false,"animations":true}})",
		R"({"version":3,"placements":[{"card":"qm.b","page":"home","column":1,"order":0.5,"present":true,"visible":true,"collapsed":false}],"view":{"mode":0,"light":false,"animations":true}})",
		R"({"version":3,"placements":[{"card":"qm.b","card":"qm.b","page":"home","column":1,"order":0,"present":true,"visible":true,"collapsed":false}],"view":{"mode":0,"light":false,"animations":true}})",
		R"({"version":3,"placements":[{"card":"qm.b\u0000x","page":"home","column":1,"order":0,"present":true,"visible":true,"collapsed":false}],"view":{"mode":0,"light":false,"animations":true}})",
		R"({"version":3,"placements":[{"card":"qm.b","page":"home","column":1,"order":0,"present":"yes","visible":true,"collapsed":false}],"view":{"mode":0,"light":false,"animations":true}})",
		R"({"version":3,"placements":[{"card":"qm.b","page":"home","column":1,"order":0,"present":true,"visible":true,"collapsed":false},{"card":"qm.b","page":"home","column":2,"order":1,"present":true,"visible":true,"collapsed":false}],"view":{"mode":0,"light":false,"animations":true}})",
		R"({"version":3,"placements":[],"view":{"mode":9,"light":false,"animations":true}})",
		R"({"version":3,"placements":[],"view":{"mode":0,"light":"yes","animations":true}})",
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
	ASSERT_TRUE(ParseCardPreferences(R"({"version":3,"placements":[{"card":"qm.future","page":"home","column":0,"order":4,"present":true,"visible":false,"collapsed":true}],"view":{"mode":0,"light":false,"animations":true}})", m_Model, Error));
	EXPECT_TRUE(m_Model.ExportState().m_vPlacements.empty());
	EXPECT_FALSE(m_Model.IsDirty());
	ASSERT_TRUE(m_Model.SetPreferences("home", "qm.b", {false, true}));
	ASSERT_TRUE(m_Model.ResetPreferences("home", "qm.b"));
	EXPECT_TRUE(m_Model.Preferences("home", "qm.b").m_Visible);
	EXPECT_FALSE(m_Model.Preferences("home", "qm.b").m_Collapsed);
	m_Model.ClearDirty();
	EXPECT_FALSE(m_Model.ResetPreferences("home", "missing"));
	EXPECT_FALSE(m_Model.ResetPreferences("other", "qm.b"));
	EXPECT_FALSE(m_Model.IsDirty());
	// 与默认一致的偏好不产生覆盖记录。
	EXPECT_TRUE(m_Model.SetPreferences("home", "qm.b", {true, false}));
	EXPECT_FALSE(m_Model.IsDirty());
	EXPECT_TRUE(m_Model.ExportState().m_vPlacements.empty());
}

TEST_F(CCardPreferencesTest, V1MigrationMapsGlobalPreferencesToPrimaryPage)
{
	std::string Error;
	ASSERT_TRUE(ParseCardPreferences(R"({"version":1,"cards":[{"id":"qm.b","visible":false,"collapsed":true,"order":3},{"id":"ddnet.a","visible":true,"collapsed":false,"order":0}]})", m_Model, Error)) << Error;
	const SCardUiState State = m_Model.ExportState();
	// ddnet.a 迁移后与默认声明一致，不再导出；qm.b 的偏好落到主页面声明。
	ASSERT_EQ(State.m_vPlacements.size(), 1);
	EXPECT_EQ(State.m_vPlacements.front().m_CardId, "qm.b");
	EXPECT_EQ(State.m_vPlacements.front().m_PageId, "home");
	EXPECT_FALSE(State.m_vPlacements.front().m_Visible);
	EXPECT_TRUE(State.m_vPlacements.front().m_Collapsed);
	EXPECT_FALSE(m_Model.IsDirty());
}

TEST_F(CCardPreferencesTest, V2MigrationPreservesPlacementsAndMapsPreferences)
{
	std::string Error;
	ASSERT_TRUE(ParseCardPreferences(R"({"version":2,"cards":[{"id":"qm.b","visible":true,"collapsed":true}],"placements":[{"id":"qm.b","page":"other","column":2,"order":0}],"view":{"mode":1,"light":true,"animations":false}})", m_Model, Error)) << Error;
	const SCardUiState State = m_Model.ExportState();
	// v2 放置迁移为 present 记录；全局偏好落到主页面声明的默认放置上。
	ASSERT_EQ(State.m_vPlacements.size(), 2);
	EXPECT_EQ(State.m_View.m_Mode, 1);
	EXPECT_TRUE(State.m_View.m_LightTheme);
	EXPECT_FALSE(State.m_View.m_Animations);
	EXPECT_TRUE(m_Model.Preferences("home", "qm.b").m_Collapsed);
	EXPECT_TRUE(m_Model.Preferences("home", "qm.b").m_Visible);
	ASSERT_NE(m_Model.OrderModel().Find("other", "qm.b"), nullptr);
	EXPECT_EQ(m_Model.OrderModel().Find("other", "qm.b")->m_Column, ECardColumn::RIGHT);
	ASSERT_EQ(m_Model.CardsForPage("other").size(), 1);
	EXPECT_EQ(m_Model.CardsForPage("other").front()->m_Id, "qm.b");
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
	ASSERT_TRUE(m_Model.SetPreferences("home", "qm.b", {false, true}));
	ASSERT_TRUE(m_Model.MoveCard("qm.b", "home", "other", ECardColumn::LEFT, 0));
	ASSERT_TRUE(SaveCardPreferences(*pStorage, m_Model, Error)) << Error;
	EXPECT_FALSE(m_Model.IsDirty());
	CCardUiModel Loaded(m_Registry);
	ASSERT_TRUE(LoadCardPreferences(*pStorage, Loaded, Error)) << Error;
	EXPECT_FALSE(Loaded.Preferences("home", "qm.b").m_Visible);
	// 源页面的移出标记被持久化：重启后不回补默认声明。
	ASSERT_NE(Loaded.OrderModel().Find("home", "qm.b"), nullptr);
	EXPECT_FALSE(Loaded.OrderModel().Find("home", "qm.b")->m_Present);
	ASSERT_EQ(Loaded.OrderModel().Find("other", "qm.b")->m_Column, ECardColumn::LEFT);
	Loaded.ResetAllPreferences();
	ASSERT_TRUE(SaveCardPreferences(*pStorage, Loaded, Error)) << Error;
	ASSERT_TRUE(LoadCardPreferences(*pStorage, m_Model, Error)) << Error;
	EXPECT_TRUE(m_Model.ExportState().m_vPlacements.empty());
	EXPECT_TRUE(m_Model.OrderModel().Find("home", "qm.b")->m_Present);
}

TEST_F(CCardPreferencesTest, ResetPageRestoresDeclarationsAndClearsPagePreferences)
{
	ASSERT_TRUE(m_Model.MoveCard("qm.b", "home", "other", ECardColumn::RIGHT, 0));
	ASSERT_TRUE(m_Model.SetPreferences("home", "ddnet.a", {false, true}));
	ASSERT_TRUE(m_Model.ResetPagePreferences("home"));
	// 本页偏好被清除，页面默认声明被恢复（曾被移出的卡片回到本页）。
	EXPECT_TRUE(m_Model.Preferences("home", "ddnet.a").m_Visible);
	EXPECT_FALSE(m_Model.Preferences("home", "ddnet.a").m_Collapsed);
	EXPECT_TRUE(m_Model.OrderModel().Find("home", "qm.b")->m_Present);
	// 其他页面的放置不受影响。
	EXPECT_TRUE(m_Model.OrderModel().Find("other", "qm.b")->m_Present);
	EXPECT_TRUE(m_Model.IsDirty());
	EXPECT_FALSE(m_Model.ResetPagePreferences("missing"));
}

TEST_F(CCardPreferencesTest, ImportStateRejectsInvalidPlacementsWithoutMutation)
{
	ASSERT_TRUE(m_Model.SetPreferences("home", "qm.b", {false, true}));
	const std::string Before = SerializeCardPreferences(m_Model);
	std::string Error;
	SCardUiState Bad;
	Bad.m_vPlacements.push_back({"qm.missing", "home", ECardColumn::FULL, 0, true, true, false});
	EXPECT_FALSE(m_Model.ImportState(Bad, Error));
	Bad.m_vPlacements.clear();
	Bad.m_vPlacements.push_back({"qm.b", "missing-page", ECardColumn::FULL, 0, true, true, false});
	EXPECT_FALSE(m_Model.ImportState(Bad, Error));
	Bad.m_vPlacements.clear();
	Bad.m_vPlacements.push_back({"qm.b", "home", static_cast<ECardColumn>(9), 0, true, true, false});
	EXPECT_FALSE(m_Model.ImportState(Bad, Error));
	Bad.m_vPlacements.clear();
	Bad.m_vPlacements.push_back({"qm.b", "home", ECardColumn::FULL, 0, true, true, false});
	Bad.m_vPlacements.push_back({"qm.b", "home", ECardColumn::FULL, 1, true, true, false});
	EXPECT_FALSE(m_Model.ImportState(Bad, Error));
	EXPECT_EQ(SerializeCardPreferences(m_Model), Before);
	// 相对移动与跨页移动的无效参数被拒绝。
	const unsigned Revision = m_Model.Revision();
	EXPECT_FALSE(m_Model.MoveCardWithinPage("home", "qm.missing", ECardColumn::LEFT, 0));
	EXPECT_FALSE(m_Model.MoveCard("qm.b", "home", "missing-page", ECardColumn::LEFT, 0));
	EXPECT_FALSE(m_Model.MoveCardRelative("home", "qm.b", "qm.missing", true));
	EXPECT_EQ(m_Model.Revision(), Revision);
}

TEST_F(CCardPreferencesTest, VisibilityChangesCannotCreateASecondOrderSource)
{
	ASSERT_TRUE(m_Model.SetPreferences("home", "qm.b", {true, false}));
	EXPECT_EQ(m_Model.CardsForPage("home").back()->m_Id, "qm.b");
	SCardUiPreferences Preferences = m_Model.Preferences("home", "qm.b");
	Preferences.m_Visible = false;
	ASSERT_TRUE(m_Model.SetPreferences("home", "qm.b", Preferences));
	// 隐藏只改偏好，不动 order 模型。
	EXPECT_EQ(m_Model.OrderModel().Find("home", "qm.b")->m_Order, 1);
	ASSERT_EQ(m_Model.CardsForPage("home").size(), 1);
	EXPECT_EQ(m_Model.CardsForPage("home").front()->m_Id, "ddnet.a");
	EXPECT_TRUE(m_Model.ExportState().m_vPlacements.size() == 1);
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
	ASSERT_TRUE(m_Model.SetPreferences("home", "qm.b", {false, true}));
	std::string Error;
	EXPECT_FALSE(LoadCardPreferences(*pStorage, m_Model, Error));
	EXPECT_FALSE(m_Model.Preferences("home", "qm.b").m_Visible);
	EXPECT_TRUE(m_Model.IsDirty());
	ASSERT_TRUE(pStorage->RemoveFile(CARD_PREFERENCES_PATH, IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->CreateFolder(CARD_PREFERENCES_PATH, IStorage::TYPE_SAVE));
	EXPECT_FALSE(SaveCardPreferences(*pStorage, m_Model, Error));
	EXPECT_FALSE(Error.empty());
	EXPECT_TRUE(m_Model.IsDirty());
	EXPECT_TRUE(pStorage->FolderExists(CARD_PREFERENCES_PATH, IStorage::TYPE_SAVE));
}

#if defined(CONF_FAMILY_WINDOWS)
TEST_F(CCardPreferencesTest, LockedDestinationSavePreservesPreviousFile)
{
	CTestInfo Info;
	Info.m_DeleteTestStorageFilesOnSuccess = true;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	std::string Error;
	ASSERT_TRUE(m_Model.SetPreferences("home", "qm.b", {false, true}));
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
