#include <engine/client/friends.h>
#include <engine/config.h>
#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/friend_code.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>

namespace
{
	class CQmFriendCodeImport : public testing::Test
	{
	protected:
		int m_PreviousIgnoreClan = g_Config.m_ClFriendsIgnoreClan;
		std::unique_ptr<CFriends> m_pLocal = std::make_unique<CFriends>();
		std::unique_ptr<CFriends> m_pRemote = std::make_unique<CFriends>();

		void SetUp() override { g_Config.m_ClFriendsIgnoreClan = 0; }
		void TearDown() override { g_Config.m_ClFriendsIgnoreClan = m_PreviousIgnoreClan; }
	};
}

TEST_F(CQmFriendCodeImport, MergeKeepsLocalDetailsAndAppendsNewFriendsAndGroups)
{
	m_pLocal->AddCategory("Local");
	m_pLocal->AddFriend("Alice", "Clan", "Local");
	m_pLocal->SetFriendNote("Alice", "Clan", "keep me");
	m_pLocal->SetFriendPinned("Alice", "Clan", true);
	m_pRemote->AddCategory("Remote");
	m_pRemote->AddCategory("Empty");
	m_pRemote->AddFriend("Alice", "Clan", "Remote");
	m_pRemote->SetFriendNote("Alice", "Clan", "incoming");
	m_pRemote->AddFriend("Bob", "Clan", "Remote");
	m_pRemote->SetFriendNote("Bob", "Clan", "new note");
	m_pRemote->SetFriendPinned("Bob", "Clan", true);
	const auto Categories = qm_friend_code::Capture(*m_pLocal).m_vCategories;
	ASSERT_TRUE(qm_friend_code::Import(*m_pLocal, qm_friend_code::Capture(*m_pRemote)));
	ASSERT_EQ(m_pLocal->NumFriends(), 2);
	EXPECT_STREQ(m_pLocal->GetFriendCategory("Alice", "Clan"), "Local");
	EXPECT_STREQ(m_pLocal->GetFriendNote("Alice", "Clan"), "keep me");
	EXPECT_TRUE(m_pLocal->IsFriendPinned("Alice", "Clan"));
	EXPECT_STREQ(m_pLocal->GetFriendNote("Bob", "Clan"), "new note");
	EXPECT_TRUE(m_pLocal->IsFriendPinned("Bob", "Clan"));
	for(size_t Index = 0; Index < Categories.size(); ++Index)
		EXPECT_EQ(m_pLocal->GetCategory(Index), Categories[Index]);
	EXPECT_STREQ(m_pLocal->GetCategory(m_pLocal->NumCategories() - 2), "Remote");
	EXPECT_STREQ(m_pLocal->GetCategory(m_pLocal->NumCategories() - 1), "Empty");
}

TEST_F(CQmFriendCodeImport, RepeatedMergeDoesNotDuplicateFriendsOrCaseEquivalentCategories)
{
	m_pLocal->AddCategory("Team");
	m_pRemote->AddCategory("team");
	m_pRemote->AddFriend("Alice", "Clan", "team");
	const auto Incoming = qm_friend_code::Capture(*m_pRemote);
	ASSERT_TRUE(qm_friend_code::Import(*m_pLocal, Incoming));
	ASSERT_TRUE(qm_friend_code::Import(*m_pLocal, Incoming));
	EXPECT_EQ(m_pLocal->NumFriends(), 1);
	EXPECT_EQ(m_pLocal->NumCategories(), 4);
	EXPECT_STREQ(m_pLocal->GetFriendCategory("Alice", "Clan"), "Team");
}

TEST_F(CQmFriendCodeImport, ReplaceRestoresExactIdentityAndOrderRegardlessOfIgnoreClan)
{
	m_pLocal->AddCategory("Removed");
	m_pLocal->AddFriend("Old", "Clan", "Removed");
	m_pRemote->AddCategory("New");
	m_pRemote->MoveCategory(m_pRemote->FindCategory("New"), 0);
	m_pRemote->AddFriend("Alice", "First", "New");
	m_pRemote->AddFriend("Alice", "Second");
	m_pRemote->SetFriendNote("Alice", "Second", "second note");
	m_pRemote->SetFriendPinned("Alice", "Second", true);
	m_pRemote->AddFriend("", "WholeClan");
	std::string Code;
	ASSERT_TRUE(qm_friend_code::Encode(qm_friend_code::Capture(*m_pRemote), Code));
	CFriendList Incoming;
	ASSERT_TRUE(qm_friend_code::Decode(Code, Incoming));
	g_Config.m_ClFriendsIgnoreClan = 1;
	ASSERT_TRUE(qm_friend_code::Import(*m_pLocal, Incoming, qm_friend_code::EImportMode::REPLACE));
	ASSERT_EQ(m_pLocal->NumFriends(), 3);
	EXPECT_EQ(qm_friend_code::Capture(*m_pLocal).m_vCategories, Incoming.m_vCategories);
	EXPECT_STREQ(m_pLocal->GetFriend(0)->m_aClan, "First");
	EXPECT_STREQ(m_pLocal->GetFriend(1)->m_aClan, "Second");
	EXPECT_STREQ(m_pLocal->GetFriend(1)->m_aNote, "second note");
	EXPECT_TRUE(m_pLocal->GetFriend(1)->m_Pinned);
	EXPECT_EQ(m_pLocal->GetFriend(2)->m_aName[0], '\0');
	g_Config.m_ClFriendsIgnoreClan = 0;
	EXPECT_TRUE(m_pLocal->IsFriendPinned("Alice", "Second"));
	EXPECT_FALSE(m_pLocal->IsFriend("Old", "Clan", true));
}

TEST_F(CQmFriendCodeImport, InvalidImportDoesNotChangeDataOrRevision)
{
	m_pLocal->AddFriend("Keep", "Clan");
	const auto Revision = m_pLocal->Revision();
	auto Invalid = qm_friend_code::Capture(*m_pRemote);
	Invalid.m_vCategories.clear();
	EXPECT_FALSE(qm_friend_code::Import(*m_pLocal, Invalid, qm_friend_code::EImportMode::REPLACE));
	EXPECT_EQ(m_pLocal->Revision(), Revision);
	ASSERT_EQ(m_pLocal->NumFriends(), 1);
	EXPECT_STREQ(m_pLocal->GetFriend(0)->m_aName, "Keep");
}

TEST_F(CQmFriendCodeImport, CombinedFriendLimitRejectsEntireMerge)
{
	for(int Index = 0; Index < IFriends::MAX_FRIENDS; ++Index)
	{
		char aName[MAX_NAME_LENGTH];
		str_format(aName, sizeof(aName), "Friend%d", Index);
		m_pLocal->AddFriend(aName, "Clan");
	}
	m_pRemote->AddCategory("New");
	m_pRemote->AddFriend("Extra", "Clan", "New");
	const auto Revision = m_pLocal->Revision();
	EXPECT_FALSE(qm_friend_code::Import(*m_pLocal, qm_friend_code::Capture(*m_pRemote)));
	EXPECT_EQ(m_pLocal->Revision(), Revision);
	EXPECT_EQ(m_pLocal->FindCategory("New"), -1);
	EXPECT_EQ(m_pLocal->NumFriends(), IFriends::MAX_FRIENDS);
}

TEST_F(CQmFriendCodeImport, CombinedCategoryLimitRejectsEntireMerge)
{
	while(m_pLocal->NumCategories() < IFriends::MAX_FRIEND_CATEGORIES)
		ASSERT_TRUE(m_pLocal->AddCategory(("Group" + std::to_string(m_pLocal->NumCategories())).c_str()));
	m_pRemote->AddCategory("Extra");
	m_pRemote->AddFriend("New", "Clan", "Extra");
	const auto Revision = m_pLocal->Revision();
	EXPECT_FALSE(qm_friend_code::Import(*m_pLocal, qm_friend_code::Capture(*m_pRemote)));
	EXPECT_EQ(m_pLocal->Revision(), Revision);
	EXPECT_EQ(m_pLocal->NumFriends(), 0);
}

TEST_F(CQmFriendCodeImport, EmptyReplacementClearsFriendsAndRestoresCategoryOrder)
{
	m_pLocal->AddFriend("Old", "Clan");
	m_pLocal->AddCategory("Old group");
	m_pRemote->AddCategory("Empty");
	m_pRemote->MoveCategory(m_pRemote->FindCategory("Empty"), 0);
	const auto Incoming = qm_friend_code::Capture(*m_pRemote);
	ASSERT_TRUE(qm_friend_code::Import(*m_pLocal, Incoming, qm_friend_code::EImportMode::REPLACE));
	EXPECT_EQ(m_pLocal->NumFriends(), 0);
	EXPECT_EQ(qm_friend_code::Capture(*m_pLocal).m_vCategories, Incoming.m_vCategories);
}

namespace
{
	class CCodeConfigSession
	{
		std::unique_ptr<IKernel> m_pKernel{IKernel::Create()};
		std::unique_ptr<IConsole> m_pConsole{CreateConsole(CFGFLAG_CLIENT)};
		std::unique_ptr<IConfigManager> m_pConfig{CreateConfigManager()};
		std::unique_ptr<CFriends> m_pFriends = std::make_unique<CFriends>();

	public:
		explicit CCodeConfigSession(IStorage *pStorage)
		{
			m_pKernel->RegisterInterface(pStorage, false);
			m_pKernel->RegisterInterface(m_pConsole.get(), false);
			m_pKernel->RegisterInterface(m_pConfig.get(), false);
			m_pKernel->RegisterInterface(m_pFriends.get(), false);
			m_pConsole->Init();
			m_pConfig->Init();
			m_pFriends->Init();
			m_pConsole->StoreCommands(false);
			g_Config.m_ClSaveSettings = 1;
			g_Config.m_ClFriendsIgnoreClan = 0;
		}
		CFriends &Friends() { return *m_pFriends; }
		bool Save() { return m_pConfig->Save(); }
		bool Load()
		{
			return m_pConsole->ExecuteFile(s_aConfigDomains[ConfigDomain::QMCLIENT].m_aConfigPath, IConsole::CLIENT_ID_UNSPECIFIED, false, IStorage::TYPE_SAVE, true);
		}
	};

	class CQmFriendCodePersistence : public testing::Test
	{
	protected:
		CConfig m_PreviousConfig = g_Config;
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
		}
		void TearDown() override { g_Config = m_PreviousConfig; }
	};
}

TEST_F(CQmFriendCodePersistence, ImportedDataSurvivesSavingAndReloadingExistingConfiguration)
{
	CFriendList Incoming;
	{
		CCodeConfigSession Source(m_pStorage.get());
		Source.Friends().AddCategory("伙伴");
		Source.Friends().AddCategory("Empty");
		Source.Friends().MoveCategory(Source.Friends().FindCategory("伙伴"), 0);
		Source.Friends().AddFriend("青竹\"\\", "Clan", "伙伴");
		Source.Friends().SetFriendNote("青竹\"\\", "Clan", "好友\"备注\\");
		Source.Friends().SetFriendPinned("青竹\"\\", "Clan", true);
		Source.Friends().AddFriend("", "WholeClan");
		std::string Code;
		ASSERT_TRUE(qm_friend_code::Encode(qm_friend_code::Capture(Source.Friends()), Code));
		ASSERT_TRUE(qm_friend_code::Decode(Code, Incoming));
	}
	{
		CCodeConfigSession Target(m_pStorage.get());
		Target.Friends().AddFriend("Old", "Clan");
		ASSERT_TRUE(qm_friend_code::Import(Target.Friends(), Incoming, qm_friend_code::EImportMode::REPLACE));
		ASSERT_TRUE(Target.Save());
	}
	{
		CCodeConfigSession Reloaded(m_pStorage.get());
		ASSERT_TRUE(Reloaded.Load());
		ASSERT_EQ(Reloaded.Friends().NumFriends(), 2);
		EXPECT_EQ(qm_friend_code::Capture(Reloaded.Friends()).m_vCategories, Incoming.m_vCategories);
		EXPECT_TRUE(Reloaded.Friends().IsFriendPinned("青竹\"\\", "Clan"));
		EXPECT_STREQ(Reloaded.Friends().GetFriendNote("青竹\"\\", "Clan"), "好友\"备注\\");
		EXPECT_TRUE(Reloaded.Friends().IsFriend("", "WholeClan", false));
		EXPECT_FALSE(Reloaded.Friends().IsFriend("Old", "Clan", true));
	}
}

TEST_F(CQmFriendCodePersistence, SavingWithIgnoreClanKeepsDistinctIdentitiesAndFullyEscapedNotes)
{
	const std::string Note(IFriends::MAX_FRIEND_NOTE_LENGTH - 1, '"');
	{
		CCodeConfigSession Session(m_pStorage.get());
		Session.Friends().AddFriend("Same", "First");
		Session.Friends().AddFriend("Same", "Second");
		Session.Friends().SetFriendNote("Same", "First", Note.c_str());
		Session.Friends().SetFriendNote("Same", "Second", "second");
		Session.Friends().SetFriendPinned("Same", "Second", true);
		const auto Incoming = qm_friend_code::Capture(Session.Friends());
		g_Config.m_ClFriendsIgnoreClan = 1;
		ASSERT_TRUE(qm_friend_code::Import(Session.Friends(), Incoming, qm_friend_code::EImportMode::REPLACE));
		ASSERT_TRUE(Session.Save());
		EXPECT_EQ(g_Config.m_ClFriendsIgnoreClan, 1);
	}
	{
		CCodeConfigSession Reloaded(m_pStorage.get());
		g_Config.m_ClFriendsIgnoreClan = 1;
		ASSERT_TRUE(Reloaded.Load());
		EXPECT_EQ(g_Config.m_ClFriendsIgnoreClan, 1);
		ASSERT_EQ(Reloaded.Friends().NumFriends(), 2);
		EXPECT_STREQ(Reloaded.Friends().GetFriend(0)->m_aNote, Note.c_str());
		EXPECT_FALSE(Reloaded.Friends().GetFriend(0)->m_Pinned);
		EXPECT_STREQ(Reloaded.Friends().GetFriend(1)->m_aNote, "second");
		EXPECT_TRUE(Reloaded.Friends().GetFriend(1)->m_Pinned);
	}
}
