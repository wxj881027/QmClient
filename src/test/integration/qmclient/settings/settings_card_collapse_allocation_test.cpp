#include <base/str.h>

#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsCardCollapseState.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <array>
#include <limits>

namespace
{
	constexpr char OLD_ID[] = "deck:existing-card-with-a-long-stable-id";
	constexpr char NEW_ID[] = "deck:new-card-with-a-long-stable-id";
	constexpr char NEW_CONFIG[] = "deck:new-card-with-a-long-stable-id;!qm:voice";

	enum class EAction
	{
		SYNC,
		IMPORT,
		INSERT,
		EXPAND,
	};

	class SettingsCardCollapsePublication : public testing::Test
	{
		std::array<char, sizeof(g_Config.m_QmSettingsCardCollapsed)> m_aPrevious{};
		std::array<char, sizeof(g_Config.m_QmSidebarCardCollapsed)> m_aLegacy{};
		int m_Migrated = 0;

	protected:
		void SetUp() override
		{
			str_copy(m_aPrevious.data(), g_Config.m_QmSettingsCardCollapsed, m_aPrevious.size());
			str_copy(m_aLegacy.data(), g_Config.m_QmSidebarCardCollapsed, m_aLegacy.size());
			m_Migrated = g_Config.m_QmSettingsCardCollapseMigrated;
		}
		void TearDown() override
		{
			str_copy(g_Config.m_QmSettingsCardCollapsed, m_aPrevious.data());
			str_copy(g_Config.m_QmSidebarCardCollapsed, m_aLegacy.data());
			g_Config.m_QmSettingsCardCollapseMigrated = 1;
			qm_card_collapse::SyncFromConfig();
			g_Config.m_QmSettingsCardCollapseMigrated = m_Migrated;
		}
		const qm_card_collapse::CState &Prepare(EAction Action)
		{
			str_copy(g_Config.m_QmSettingsCardCollapsed, OLD_ID);
			g_Config.m_QmSidebarCardCollapsed[0] = '\0';
			g_Config.m_QmSettingsCardCollapseMigrated = 1;
			const auto &State = qm_card_collapse::CurrentState();
			if(Action == EAction::SYNC)
				str_copy(g_Config.m_QmSettingsCardCollapsed, NEW_CONFIG);
			else if(Action == EAction::IMPORT)
			{
				g_Config.m_QmSettingsCardCollapsed[0] = '\0';
				str_copy(g_Config.m_QmSidebarCardCollapsed, "chat_bubble:old;translate;voice");
				g_Config.m_QmSettingsCardCollapseMigrated = 0;
			}
			return State;
		}
		bool Invoke(EAction Action)
		{
			if(Action == EAction::SYNC)
				return qm_card_collapse::CurrentState().IsCollapsed(NEW_ID, false);
			if(Action == EAction::IMPORT)
				return qm_card_collapse::CurrentState().IsCollapsed("qm:translate", false);
			return qm_card_collapse::SetCollapsed(Action == EAction::INSERT ? NEW_ID : OLD_ID, Action == EAction::INSERT);
		}
		void ExpectPublished(EAction Action)
		{
			const auto &State = qm_card_collapse::CurrentState();
			EXPECT_EQ(State.IsCollapsed(OLD_ID, false), Action == EAction::INSERT);
			if(Action == EAction::SYNC)
			{
				EXPECT_TRUE(State.IsCollapsed(NEW_ID, false));
				EXPECT_FALSE(State.IsCollapsed("qm:voice", true));
				EXPECT_STREQ(g_Config.m_QmSettingsCardCollapsed, NEW_CONFIG);
			}
			else if(Action == EAction::IMPORT)
			{
				EXPECT_STREQ(g_Config.m_QmSettingsCardCollapsed, "qm:chat_bubble;qm:translate;qm:voice");
				EXPECT_EQ(g_Config.m_QmSettingsCardCollapseMigrated, 1);
			}
			else
				EXPECT_EQ(State.IsCollapsed(NEW_ID, false), Action == EAction::INSERT);
		}
		void ExpectEveryFailureAndRetry(EAction Action)
		{
			Prepare(Action);
			size_t Allocations = 0;
			bool Ready = false;
			{
				CPredictionAllocationFailure Count(std::numeric_limits<size_t>::max());
				Ready = Invoke(Action);
				Allocations = Count.Attempts();
			}
			ASSERT_TRUE(Ready);
			ASSERT_GT(Allocations, 0u);
			ExpectPublished(Action);
			for(size_t BeforeFailure = 0; BeforeFailure < Allocations; ++BeforeFailure)
			{
				SCOPED_TRACE(BeforeFailure);
				const auto &State = Prepare(Action);
				std::array<char, sizeof(g_Config.m_QmSettingsCardCollapsed)> aConfig{};
				str_copy(aConfig.data(), g_Config.m_QmSettingsCardCollapsed, aConfig.size());
				const int Migrated = g_Config.m_QmSettingsCardCollapseMigrated;
				bool Triggered = false;
				size_t Attempts = 0;
				{
					CPredictionAllocationFailure Failure(BeforeFailure);
					Ready = Invoke(Action);
					Triggered = Failure.Triggered();
					Attempts = Failure.Attempts();
				}
				EXPECT_TRUE(Triggered);
				EXPECT_FALSE(Ready);
				EXPECT_EQ(Attempts, BeforeFailure + 1);
				EXPECT_TRUE(State.IsCollapsed(OLD_ID, false));
				EXPECT_FALSE(State.IsCollapsed(NEW_ID, false));
				EXPECT_FALSE(State.IsCollapsed("qm:translate", false));
				EXPECT_STREQ(g_Config.m_QmSettingsCardCollapsed, aConfig.data());
				EXPECT_EQ(g_Config.m_QmSettingsCardCollapseMigrated, Migrated);
				if(Action == EAction::IMPORT)
					EXPECT_STREQ(g_Config.m_QmSidebarCardCollapsed, "chat_bubble:old;translate;voice");
				ASSERT_TRUE(Invoke(Action));
				ExpectPublished(Action);
			}
		}
	};
}

TEST_F(SettingsCardCollapsePublication, ConfigParsingFailureRetainsOldStateAndRetries)
{
	ExpectEveryFailureAndRetry(EAction::SYNC);
}

TEST_F(SettingsCardCollapsePublication, ExistingLegacyImportFailureRetainsConfigAndRetries)
{
	ExpectEveryFailureAndRetry(EAction::IMPORT);
}

TEST_F(SettingsCardCollapsePublication, NewCardFailureRetainsOldStateAndRetries)
{
	ExpectEveryFailureAndRetry(EAction::INSERT);
}

TEST_F(SettingsCardCollapsePublication, ExpansionFailureRetainsOldStateAndRetries)
{
	ExpectEveryFailureAndRetry(EAction::EXPAND);
}

TEST_F(SettingsCardCollapsePublication, FailedConfigSyncRejectsClickWithoutOverwritingExternalEdit)
{
	const auto &State = Prepare(EAction::SYNC);
	bool Changed = true, Triggered = false;
	{
		CPredictionAllocationFailure Failure;
		Changed = qm_card_collapse::SetCollapsed("qm:translate", true);
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Changed);
	EXPECT_STREQ(g_Config.m_QmSettingsCardCollapsed, NEW_CONFIG);
	EXPECT_TRUE(State.IsCollapsed(OLD_ID, false));
	EXPECT_FALSE(State.IsCollapsed("qm:translate", false));
	ASSERT_TRUE(qm_card_collapse::SetCollapsed("qm:translate", true));
	EXPECT_TRUE(State.IsCollapsed(NEW_ID, false));
	EXPECT_TRUE(State.IsCollapsed("qm:translate", false));
	EXPECT_FALSE(State.IsCollapsed(OLD_ID, false));
	EXPECT_FALSE(State.IsCollapsed("qm:voice", true));
}

TEST_F(SettingsCardCollapsePublication, CachedConfigAndLongIdentityCanBeReadWithoutAllocation)
{
	Prepare(EAction::INSERT);
	bool Collapsed = false, Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		Collapsed = qm_card_collapse::IsCollapsed(OLD_ID);
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Collapsed);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
}
