#include <game/client/components/qmclient/recent_tee_skins.h>
#include <game/client/components/skins.h>

#include <gtest/gtest.h>

#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using ECollection = EQmTeeSkinCollection;
	struct SCopyFailure
	{
		int m_Copies = 0;
		int m_FailAt = -1;
	};

	struct SEntry
	{
		std::string m_Name;
		bool m_Favorite;
		SCopyFailure *m_pFailure;
		SEntry(std::string Name, bool Favorite = false, SCopyFailure *pFailure = nullptr) :
			m_Name(std::move(Name)), m_Favorite(Favorite), m_pFailure(pFailure) {}
		SEntry(const SEntry &Other) :
			m_Name(Other.m_Name), m_Favorite(Other.m_Favorite), m_pFailure(Other.m_pFailure)
		{
			if(m_pFailure && m_pFailure->m_Copies++ == m_pFailure->m_FailAt)
				throw std::bad_alloc();
		}
		SEntry(SEntry &&) noexcept = default;
		SEntry &operator=(SEntry &&) noexcept = default;
		SEntry &operator=(const SEntry &) = default;
		bool IsFavorite() const { return m_Favorite; }
	};

	std::vector<std::string> Names(const std::vector<SEntry> &Entries)
	{
		std::vector<std::string> Result;
		for(const auto &Entry : Entries)
			Result.push_back(Entry.m_Name);
		return Result;
	}

	class CTeeCollection : public ::testing::Test
	{
	protected:
		SCopyFailure m_CopyFailure;
		CQmRecentTeeSkins m_History;
		SQmRecentTeeSkin m_Main{"default"};
		SQmRecentTeeSkin m_Dummy{"bluekitty"};
		std::vector<SEntry> m_Source;
		CQmTeeSkinCollectionCache<SEntry> m_Cache;
		uint64_t m_SourceRevision = 1;
		int m_Conversions = 0;
		std::string m_FailName;

		void SeedHistory()
		{
			ASSERT_TRUE(m_History.Record({"blue"}));
			ASSERT_TRUE(m_History.Record({"red"}));
		}
		SQmTeeSkinCollectionRequest Request(ECollection Collection, std::string_view Filter = {})
		{
			return {m_SourceRevision, m_History.Revision(), Collection, Filter, m_Main, m_Dummy};
		}
		std::vector<SEntry> &Resolve(ECollection Collection, std::string_view Filter = {})
		{
			const auto Input = Request(Collection, Filter);
			return m_Cache.Resolve(Input, m_History.Entries(), m_Source, [&](const SQmRecentTeeSkin &Entry) -> std::optional<SEntry> {
				++m_Conversions;
				if(Entry.m_Name == m_FailName)
					throw std::bad_alloc();
				if(Entry.m_Name == "missing")
					return std::nullopt;
				return SEntry(Entry.m_Name);
			});
		}
	};
}

TEST_F(CTeeCollection, AllReturnsOriginalSourceWithoutMaterializingRecentEntries)
{
	m_Source.emplace_back("source");
	EXPECT_EQ(&Resolve(ECollection::ALL), &m_Source);
	EXPECT_EQ(m_Cache.Revision(), 1u);
	EXPECT_EQ(&Resolve(ECollection::ALL), &m_Source);
	EXPECT_EQ(m_Cache.Revision(), 1u);
	EXPECT_EQ(m_Conversions, 0);
}

TEST_F(CTeeCollection, FavoritesUseSourceOrderAndDoNotApplyItsFilterAgain)
{
	m_Source.emplace_back("red", true);
	m_Source.emplace_back("blue", false);
	m_Source.emplace_back("gold", true);
	EXPECT_EQ(Names(Resolve(ECollection::FAVORITES, "already-filtered-by-source")), (std::vector<std::string>{"red", "gold"}));
	EXPECT_EQ(m_Conversions, 0);
}

TEST_F(CTeeCollection, RecentStartsInHistoryOrderAndSkipsMissingSkins)
{
	SeedHistory();
	ASSERT_TRUE(m_History.Record({"missing"}));
	EXPECT_EQ(Names(Resolve(ECollection::RECENT)), (std::vector<std::string>{"red", "blue"}));
	EXPECT_EQ(m_Conversions, 3);
}

TEST_F(CTeeCollection, RecentFiltersCaseInsensitivelyBeforeMaterializing)
{
	SeedHistory();
	EXPECT_EQ(Names(Resolve(ECollection::RECENT, "BLUE")), (std::vector<std::string>{"blue"}));
	EXPECT_EQ(m_Conversions, 1);
}

TEST_F(CTeeCollection, EmptyHistoryCanPublishAndLaterAcceptItsFirstAppearance)
{
	EXPECT_TRUE(Resolve(ECollection::RECENT).empty());
	EXPECT_EQ(m_Conversions, 0);
	EXPECT_EQ(m_Cache.Revision(), 1u);
	ASSERT_TRUE(m_History.Record({"red"}));
	EXPECT_EQ(Names(Resolve(ECollection::RECENT)), (std::vector<std::string>{"red"}));
	EXPECT_EQ(m_Cache.Revision(), 2u);
}

TEST_F(CTeeCollection, MatchingKeyKeepsRevisionAndDoesNotRepeatConversion)
{
	SeedHistory();
	Resolve(ECollection::RECENT);
	const uint64_t Revision = m_Cache.Revision();
	EXPECT_EQ(Names(Resolve(ECollection::RECENT)), (std::vector<std::string>{"red", "blue"}));
	EXPECT_EQ(m_Conversions, 2);
	EXPECT_EQ(m_Cache.Revision(), Revision);
}

TEST_F(CTeeCollection, FailedFirstBuildCanRetryTheExactSameKey)
{
	SeedHistory();
	m_FailName = "blue";
	EXPECT_THROW(Resolve(ECollection::RECENT), std::bad_alloc);
	EXPECT_EQ(m_Cache.Revision(), 0u);
	m_FailName.clear();
	EXPECT_EQ(Names(Resolve(ECollection::RECENT)), (std::vector<std::string>{"red", "blue"}));
	EXPECT_EQ(m_Cache.Revision(), 1u);
	EXPECT_EQ(m_Conversions, 4);
}

TEST_F(CTeeCollection, LaterConversionFailureKeepsOldRowsAndRetriesNewKey)
{
	SeedHistory();
	auto &OldRows = Resolve(ECollection::RECENT);
	const uint64_t Revision = m_Cache.Revision();
	ASSERT_TRUE(m_History.Record({"gold"}));
	m_FailName = "red";
	EXPECT_THROW(Resolve(ECollection::RECENT), std::bad_alloc);
	EXPECT_EQ(Names(OldRows), (std::vector<std::string>{"red", "blue"}));
	EXPECT_EQ(m_Cache.Revision(), Revision);
	m_FailName.clear();
	EXPECT_EQ(Names(Resolve(ECollection::RECENT)), (std::vector<std::string>{"gold", "red", "blue"}));
	EXPECT_EQ(m_Cache.Revision(), Revision + 1);
}

TEST_F(CTeeCollection, FavoriteCopyFailureKeepsPriorRowsAndSourceRevision)
{
	m_Source.emplace_back("red", true, &m_CopyFailure);
	m_Source.emplace_back("gold", true, &m_CopyFailure);
	auto &OldRows = Resolve(ECollection::FAVORITES);
	const uint64_t Revision = m_Cache.Revision();
	m_Source.clear();
	m_Source.emplace_back("blue", true, &m_CopyFailure);
	m_Source.emplace_back("pink", true, &m_CopyFailure);
	++m_SourceRevision;
	m_CopyFailure.m_Copies = 0;
	m_CopyFailure.m_FailAt = 1;
	EXPECT_THROW(Resolve(ECollection::FAVORITES), std::bad_alloc);
	EXPECT_EQ(Names(OldRows), (std::vector<std::string>{"red", "gold"}));
	EXPECT_EQ(m_Cache.Revision(), Revision);
	m_CopyFailure.m_FailAt = -1;
	EXPECT_EQ(Names(Resolve(ECollection::FAVORITES)), (std::vector<std::string>{"blue", "pink"}));
	EXPECT_EQ(m_Cache.Revision(), Revision + 1);
}

TEST_F(CTeeCollection, ReapplyingRecentAppearanceKeepsVisibleOrder)
{
	SeedHistory();
	Resolve(ECollection::RECENT);
	ASSERT_TRUE(m_History.Record({"blue"}));
	ASSERT_EQ(m_History.Entries().front().m_Name, "blue");
	EXPECT_EQ(Names(Resolve(ECollection::RECENT)), (std::vector<std::string>{"red", "blue"}));
}

TEST_F(CTeeCollection, LeavingRecentAndReturningStartsFromCurrentHistoryOrder)
{
	SeedHistory();
	Resolve(ECollection::RECENT);
	ASSERT_TRUE(m_History.Record({"blue"}));
	Resolve(ECollection::ALL);
	EXPECT_EQ(Names(Resolve(ECollection::RECENT)), (std::vector<std::string>{"blue", "red"}));
}

TEST_F(CTeeCollection, InvalidatingSameKeyForcesOneCompletedRebuild)
{
	SeedHistory();
	Resolve(ECollection::RECENT);
	const uint64_t Revision = m_Cache.Revision();
	m_Cache.Invalidate();
	EXPECT_EQ(m_Cache.Revision(), Revision);
	Resolve(ECollection::RECENT);
	EXPECT_EQ(m_Cache.Revision(), Revision + 1);
	EXPECT_EQ(m_Conversions, 4);
	Resolve(ECollection::RECENT);
	EXPECT_EQ(m_Conversions, 4);
}

TEST_F(CTeeCollection, OrdinaryInvalidationPreservesRecentClickOrder)
{
	SeedHistory();
	Resolve(ECollection::RECENT);
	ASSERT_TRUE(m_History.Record({"blue"}));
	m_Cache.Invalidate();
	EXPECT_EQ(Names(Resolve(ECollection::RECENT)), (std::vector<std::string>{"red", "blue"}));
}

TEST_F(CTeeCollection, PageExitInvalidationRestartsFromCurrentHistory)
{
	SeedHistory();
	Resolve(ECollection::RECENT);
	ASSERT_TRUE(m_History.Record({"blue"}));
	m_Cache.Invalidate(true);
	EXPECT_EQ(Names(Resolve(ECollection::RECENT)), (std::vector<std::string>{"blue", "red"}));
}

TEST_F(CTeeCollection, FailedResetRebuildRetainsResetIntentForRetry)
{
	SeedHistory();
	auto &OldRows = Resolve(ECollection::RECENT);
	ASSERT_TRUE(m_History.Record({"blue"}));
	m_Cache.Invalidate(true);
	m_FailName = "red";
	EXPECT_THROW(Resolve(ECollection::RECENT), std::bad_alloc);
	EXPECT_EQ(Names(OldRows), (std::vector<std::string>{"red", "blue"}));
	m_FailName.clear();
	m_Cache.Invalidate();
	EXPECT_EQ(Names(Resolve(ECollection::RECENT)), (std::vector<std::string>{"blue", "red"}));
	EXPECT_EQ(m_Cache.Revision(), 2u);
}

TEST_F(CTeeCollection, ChangingMainAppearanceRebuildsSelectionMetadata)
{
	SeedHistory();
	Resolve(ECollection::RECENT);
	m_Main.m_Name = "new-main";
	Resolve(ECollection::RECENT);
	EXPECT_EQ(m_Conversions, 4);
	EXPECT_EQ(m_Cache.Revision(), 2u);
}

TEST_F(CTeeCollection, ChangingDummyAppearanceRebuildsSelectionMetadata)
{
	SeedHistory();
	Resolve(ECollection::RECENT);
	m_Dummy.m_UseCustomColor = true;
	m_Dummy.m_ColorBody = 123;
	Resolve(ECollection::RECENT);
	EXPECT_EQ(m_Conversions, 4);
	EXPECT_EQ(m_Cache.Revision(), 2u);
}

TEST_F(CTeeCollection, InactiveCustomColorsDoNotInvalidateAppearanceKey)
{
	SeedHistory();
	Resolve(ECollection::RECENT);
	m_Main.m_ColorBody = 123;
	m_Main.m_ColorFeet = 456;
	Resolve(ECollection::RECENT);
	EXPECT_EQ(m_Conversions, 2);
	EXPECT_EQ(m_Cache.Revision(), 1u);
}

TEST_F(CTeeCollection, FilterKeyOwnsItsBytesAcrossCallerEdits)
{
	SeedHistory();
	std::string Filter = "red";
	EXPECT_EQ(Names(Resolve(ECollection::RECENT, Filter)), (std::vector<std::string>{"red"}));
	Filter = "blue";
	EXPECT_EQ(Names(Resolve(ECollection::RECENT, Filter)), (std::vector<std::string>{"blue"}));
	EXPECT_EQ(m_Conversions, 2);
	EXPECT_EQ(m_Cache.Revision(), 2u);
}

TEST_F(CTeeCollection, DifferentColorAppearancesArePassedToMaterializerSeparately)
{
	ASSERT_TRUE(m_History.Record({"default", true, 1, 0}));
	ASSERT_TRUE(m_History.Record({"default", true, 2, 0}));
	std::vector<unsigned> Colors;
	const auto Input = Request(ECollection::RECENT);
	const auto &Rows = m_Cache.Resolve(Input, m_History.Entries(), m_Source, [&](const SQmRecentTeeSkin &Entry) -> std::optional<SEntry> {
		Colors.push_back(Entry.m_ColorBody);
		return SEntry(Entry.m_Name);
	});
	EXPECT_EQ(Rows.size(), 2u);
	EXPECT_EQ(Colors, (std::vector<unsigned>{2, 1}));
}

TEST(TeeCollectionSkinEntry, SameSizedRebuildPreservesActualControlIdentities)
{
	using CEntry = CSkins::CSkinListEntry;
	std::vector<CEntry> Source;
	Source.emplace_back(nullptr, true, true, false, std::nullopt, std::nullopt);
	Source.emplace_back(nullptr, true, false, true, std::nullopt, std::nullopt);
	CQmTeeSkinCollectionCache<CEntry> Cache;
	const SQmRecentTeeSkin Main{"default"}, Dummy{"bluekitty"};
	SQmTeeSkinCollectionRequest Input{1, 0, ECollection::FAVORITES, {}, Main, Dummy};
	const auto ResolveMissing = [](const SQmRecentTeeSkin &) -> std::optional<CEntry> { return std::nullopt; };
	auto &Rows = Cache.Resolve(Input, {}, Source, ResolveMissing);
	ASSERT_EQ(Rows.size(), 2u);
	const void *pListId = Rows[0].ListItemId();
	const void *pFavoriteId = Rows[0].FavoriteButtonId();
	const void *pRightId = Rows[0].RightDoubleClickId();
	Input.m_SourceRevision = 2;
	auto &Updated = Cache.Resolve(Input, {}, Source, ResolveMissing);
	ASSERT_EQ(Updated.size(), 2u);
	EXPECT_EQ(Updated[0].ListItemId(), pListId);
	EXPECT_EQ(Updated[0].FavoriteButtonId(), pFavoriteId);
	EXPECT_EQ(Updated[0].RightDoubleClickId(), pRightId);
	EXPECT_TRUE(Updated[0].IsSelectedMain());
	EXPECT_TRUE(Updated[1].IsSelectedDummy());
}
