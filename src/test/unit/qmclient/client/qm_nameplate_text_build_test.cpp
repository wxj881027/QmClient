#include <game/client/components/qmclient/nameplate_text_build.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <array>
#include <new>

namespace
{
	struct SText
	{
		unsigned m_Flags = 17;
		int m_DeleteCalls = 0, m_FailDeleteCall = 0;
		bool m_FailFlags = false;
		std::array<int, 32> m_Deleted{};
		int m_DeletedCount = 0;
		unsigned GetRenderFlags() const { return m_Flags; }
		void SetRenderFlags(unsigned Flags)
		{
			m_Flags = Flags;
			if(std::exchange(m_FailFlags, false))
				throw 9;
		}
		void DeleteTextContainer(STextContainerIndex &Index)
		{
			if(Index.m_Index < 0)
				return;
			if(++m_DeleteCalls == m_FailDeleteCall)
				throw std::bad_alloc();
			m_Deleted[m_DeletedCount++] = Index.m_Index;
			Index.m_UseCount->Invalidate();
			Index.Reset();
		}
	};
	struct SGraphics
	{
		std::array<float, 4> m_Screen{1, 2, 3, 4};
		bool m_FailMap = false;
		int m_Maps = 0, m_Snapshots = 0;
		void GetScreen(float *pX0, float *pY0, float *pX1, float *pY1)
		{
			++m_Snapshots;
			*pX0 = m_Screen[0];
			*pY0 = m_Screen[1];
			*pX1 = m_Screen[2];
			*pY1 = m_Screen[3];
		}
		void MapScreen(float X0, float Y0, float X1, float Y1)
		{
			++m_Maps;
			if(std::exchange(m_FailMap, false))
				throw 8;
			m_Screen = {X0, Y0, X1, Y1};
		}
	};
}

class QmNameplateTextBuild : public ::testing::Test
{
protected:
	SText m_Text;
	SGraphics m_Graphics;
	STextContainerIndex m_Index, m_Candidate, m_Next;
	CQmNameplateTextCache m_Cache;
	CQmNameplateTextBuild m_Build;
	bool m_Reuse = true;
	void SetUp() override
	{
		m_Index.m_Index = 1;
		m_Candidate.m_Index = 2;
		m_Next.m_Index = 3;
		m_Cache.OnUpdate(7);
	}
	void TearDown() override
	{
		m_Text.m_FailDeleteCall = 0;
		m_Build.Reset(m_Text, m_Index);
	}
	void ExpectContextRestored()
	{
		EXPECT_EQ(m_Text.m_Flags, 17u);
		EXPECT_EQ(m_Graphics.m_Screen, (std::array<float, 4>{1, 2, 3, 4}));
	}
};

TEST_F(QmNameplateTextBuild, SuccessfulContextRestoresScreenAndFlagsOnlyOnce)
{
	{
		CQmNameplateTextContext Scope(m_Text, m_Graphics, true);
		Scope.Apply(99, [&] { m_Graphics.MapScreen(10, 20, 30, 40); });
		Scope.Finish();
		Scope.Finish();
	}
	ExpectContextRestored();
	EXPECT_EQ(m_Graphics.m_Maps, 2);
}

TEST_F(QmNameplateTextBuild, ContextUnwindRestoresTheCallerState)
{
	EXPECT_THROW({
		CQmNameplateTextContext Scope(m_Text, m_Graphics, true);
		Scope.Apply(99, [&] { m_Graphics.MapScreen(10, 20, 30, 40); });
		throw 7; }, int);
	ExpectContextRestored();
}

TEST_F(QmNameplateTextBuild, PreviewContextDoesNotApplyOrRestoreAnUnchangedScreen)
{
	bool Mapped = false;
	{
		CQmNameplateTextContext Scope(m_Text, m_Graphics, false);
		Scope.Apply(99, [&] { Mapped = true; });
		Scope.Finish();
	}
	EXPECT_FALSE(Mapped);
	EXPECT_EQ(m_Graphics.m_Maps, 0);
	EXPECT_EQ(m_Graphics.m_Snapshots, 0);
	ExpectContextRestored();
}

TEST_F(QmNameplateTextBuild, MappingSetupExceptionRestoresAnAlreadyChangedScreen)
{
	EXPECT_THROW({
		CQmNameplateTextContext Scope(m_Text, m_Graphics, true);
		Scope.Apply(99, [&] {
			m_Graphics.MapScreen(10, 20, 30, 40);
			throw 7;
		}); }, int);
	ExpectContextRestored();
}

TEST_F(QmNameplateTextBuild, ExplicitScreenRestoreFailureStillRestoresFlags)
{
	CQmNameplateTextContext Scope(m_Text, m_Graphics, true);
	Scope.Apply(99, [&] { m_Graphics.MapScreen(10, 20, 30, 40); });
	m_Graphics.m_FailMap = true;
	EXPECT_THROW(Scope.Finish(), int);
	EXPECT_EQ(m_Text.m_Flags, 17u);
}

TEST_F(QmNameplateTextBuild, UnwindRestoreFailurePreservesTheOriginalException)
{
	int Error = 0;
	try
	{
		CQmNameplateTextContext Scope(m_Text, m_Graphics, true);
		Scope.Apply(99, [&] { m_Graphics.MapScreen(10, 20, 30, 40); });
		m_Graphics.m_FailMap = true;
		throw 7;
	}
	catch(int Value)
	{
		Error = Value;
	}
	EXPECT_EQ(Error, 7);
	EXPECT_EQ(m_Text.m_Flags, 17u);
}

TEST_F(QmNameplateTextBuild, FlagSetupFailureCanRestoreFlagsWithoutMapping)
{
	m_Text.m_FailFlags = true;
	EXPECT_THROW({
		CQmNameplateTextContext Scope(m_Text, m_Graphics, true);
		Scope.Apply(99, [&] { m_Graphics.MapScreen(10, 20, 30, 40); }); }, int);
	EXPECT_EQ(m_Graphics.m_Maps, 0);
	ExpectContextRestored();
}

TEST_F(QmNameplateTextBuild, DensitySuccessDeletesOldContainerAndRestoresReuseMode)
{
	bool ForcedCreation = false;
	ASSERT_TRUE(m_Build.Run(m_Text, m_Index, m_Reuse, true, m_Cache, [&] {
		ForcedCreation = !m_Reuse && !m_Index.Valid();
		m_Index = m_Candidate;
	}));
	EXPECT_TRUE(ForcedCreation);
	EXPECT_TRUE(m_Reuse);
	EXPECT_EQ(m_Index.m_Index, 2);
	ASSERT_EQ(m_Text.m_DeletedCount, 1);
	EXPECT_EQ(m_Text.m_Deleted[0], 1);
}

TEST_F(QmNameplateTextBuild, EmptyDensityReplacementRetainsThePreviousContainer)
{
	EXPECT_FALSE(m_Build.Run(m_Text, m_Index, m_Reuse, true, m_Cache, [&] { m_Index.Reset(); }));
	EXPECT_EQ(m_Index.m_Index, 1);
	EXPECT_TRUE(m_Index.Valid());
	EXPECT_TRUE(m_Reuse);
	EXPECT_EQ(m_Text.m_DeleteCalls, 0);
}

TEST_F(QmNameplateTextBuild, EmptyInitialResultCompletesWithoutInventingAContainer)
{
	m_Index.Reset();
	m_Reuse = false;
	EXPECT_TRUE(m_Build.Run(m_Text, m_Index, m_Reuse, false, m_Cache, [&] { m_Index.Reset(); }));
	EXPECT_FALSE(m_Index.Valid());
	EXPECT_EQ(m_Text.m_DeleteCalls, 0);
}

TEST_F(QmNameplateTextBuild, FailureBeforeCandidateCreationRestoresOldAndRequiresRetry)
{
	EXPECT_THROW(m_Build.Run(m_Text, m_Index, m_Reuse, true, m_Cache, [] { throw 7; }), int);
	EXPECT_EQ(m_Index.m_Index, 1);
	EXPECT_TRUE(m_Index.Valid());
	EXPECT_TRUE(m_Reuse);
	EXPECT_TRUE(m_Cache.NeedsUpdate(true, false));
	m_Build.Cleanup(m_Text);
	EXPECT_EQ(m_Text.m_DeleteCalls, 0);
}

TEST_F(QmNameplateTextBuild, FailureAfterCandidateCreationRetainsItUntilCleanup)
{
	EXPECT_THROW(m_Build.Run(m_Text, m_Index, m_Reuse, true, m_Cache, [&] {
		m_Index = m_Candidate;
		throw 7;
	}),
		int);
	EXPECT_EQ(m_Index.m_Index, 1);
	EXPECT_TRUE(m_Candidate.Valid());
	EXPECT_TRUE(m_Cache.NeedsUpdate(true, false));
	EXPECT_EQ(m_Text.m_DeleteCalls, 0);
	m_Build.Cleanup(m_Text);
	EXPECT_FALSE(m_Candidate.Valid());
	ASSERT_EQ(m_Text.m_DeletedCount, 1);
	EXPECT_EQ(m_Text.m_Deleted[0], 2);
	EXPECT_TRUE(m_Index.Valid());
}

TEST_F(QmNameplateTextBuild, OrdinaryReplacementFailureHidesPartialContainerAndRetries)
{
	m_Reuse = false;
	EXPECT_THROW(m_Build.Run(m_Text, m_Index, m_Reuse, false, m_Cache, [&] {
		m_Index = m_Candidate;
		throw std::bad_alloc();
	}),
		std::bad_alloc);
	EXPECT_FALSE(m_Index.Valid());
	EXPECT_TRUE(m_Candidate.Valid());
	EXPECT_TRUE(m_Cache.NeedsUpdate(true, false));
	bool CleanedBeforeBuild = false;
	ASSERT_TRUE(m_Build.Run(m_Text, m_Index, m_Reuse, false, m_Cache, [&] {
		CleanedBeforeBuild = !m_Candidate.Valid();
		m_Index = m_Next;
	}));
	EXPECT_TRUE(CleanedBeforeBuild);
	EXPECT_EQ(m_Index.m_Index, 3);
	ASSERT_EQ(m_Text.m_DeletedCount, 2);
	EXPECT_EQ(m_Text.m_Deleted[0], 1);
	EXPECT_EQ(m_Text.m_Deleted[1], 2);
}

TEST_F(QmNameplateTextBuild, ReuseFailureKeepsThePartialOriginalForCleanup)
{
	EXPECT_THROW(m_Build.Run(m_Text, m_Index, m_Reuse, false, m_Cache, [] { throw 7; }), int);
	EXPECT_FALSE(m_Index.Valid());
	EXPECT_TRUE(m_Reuse);
	EXPECT_EQ(m_Text.m_DeleteCalls, 0);
	m_Build.Cleanup(m_Text);
	ASSERT_EQ(m_Text.m_DeletedCount, 1);
	EXPECT_EQ(m_Text.m_Deleted[0], 1);
	EXPECT_TRUE(m_Cache.NeedsUpdate(true, false));
}

TEST_F(QmNameplateTextBuild, OldDeletionFailureRetainsTheCompleteCandidateAndOldOwner)
{
	m_Text.m_FailDeleteCall = 1;
	EXPECT_THROW(m_Build.Run(m_Text, m_Index, m_Reuse, true, m_Cache, [&] { m_Index = m_Candidate; }), std::bad_alloc);
	EXPECT_EQ(m_Index.m_Index, 2);
	EXPECT_TRUE(m_Index.Valid());
	EXPECT_TRUE(m_Reuse);
	EXPECT_TRUE(m_Cache.NeedsUpdate(true, false));
	m_Build.Cleanup(m_Text);
	ASSERT_EQ(m_Text.m_DeletedCount, 1);
	EXPECT_EQ(m_Text.m_Deleted[0], 1);
	EXPECT_TRUE(m_Index.Valid());
}

TEST_F(QmNameplateTextBuild, PendingCleanupFailureBlocksAnotherBuildWithoutLosingOwners)
{
	EXPECT_THROW(m_Build.Run(m_Text, m_Index, m_Reuse, true, m_Cache, [&] {
		m_Index = m_Candidate;
		throw 7;
	}),
		int);
	m_Text.m_FailDeleteCall = 1;
	bool Built = false;
	EXPECT_THROW(m_Build.Run(m_Text, m_Index, m_Reuse, true, m_Cache, [&] { Built = true; }), std::bad_alloc);
	EXPECT_FALSE(Built);
	EXPECT_EQ(m_Index.m_Index, 1);
	EXPECT_TRUE(m_Index.Valid());
	EXPECT_TRUE(m_Candidate.Valid());
	m_Build.Cleanup(m_Text);
	EXPECT_FALSE(m_Candidate.Valid());
	EXPECT_TRUE(m_Index.Valid());
}

TEST_F(QmNameplateTextBuild, ResetReleasesBothPendingAndActiveContainers)
{
	EXPECT_THROW(m_Build.Run(m_Text, m_Index, m_Reuse, true, m_Cache, [&] {
		m_Index = m_Candidate;
		throw 7;
	}),
		int);
	m_Build.Reset(m_Text, m_Index);
	EXPECT_FALSE(m_Index.Valid());
	EXPECT_FALSE(m_Candidate.Valid());
	ASSERT_EQ(m_Text.m_DeletedCount, 2);
	EXPECT_EQ(m_Text.m_Deleted[0], 2);
	EXPECT_EQ(m_Text.m_Deleted[1], 1);
}

TEST_F(QmNameplateTextBuild, AtlasInvalidationDoesNotHidePendingSlotCleanup)
{
	auto Revision = std::make_shared<uint64_t>(1);
	m_Candidate.m_UseCount->BindGlyphAtlas(Revision);
	EXPECT_THROW(m_Build.Run(m_Text, m_Index, m_Reuse, true, m_Cache, [&] {
		m_Index = m_Candidate;
		throw 7;
	}),
		int);
	++*Revision;
	ASSERT_FALSE(m_Candidate.Valid());
	m_Build.Cleanup(m_Text);
	ASSERT_EQ(m_Text.m_DeletedCount, 1);
	EXPECT_EQ(m_Text.m_Deleted[0], 2);
	EXPECT_TRUE(m_Index.Valid());
}

TEST_F(QmNameplateTextBuild, ResetDeletionFailureLeavesTheActiveOwnerForRetry)
{
	m_Text.m_FailDeleteCall = 1;
	EXPECT_THROW(m_Build.Reset(m_Text, m_Index), std::bad_alloc);
	EXPECT_TRUE(m_Index.Valid());
	EXPECT_EQ(m_Index.m_Index, 1);
	m_Build.Reset(m_Text, m_Index);
	EXPECT_FALSE(m_Index.Valid());
	ASSERT_EQ(m_Text.m_DeletedCount, 1);
	EXPECT_EQ(m_Text.m_Deleted[0], 1);
}

TEST_F(QmNameplateTextBuild, ContextAndOwnershipMetadataDoNotAllocateDuringAdmission)
{
	bool Completed = false, Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		{
			CQmNameplateTextContext Scope(m_Text, m_Graphics, true);
			Scope.Apply(99, [&] { m_Graphics.MapScreen(10, 20, 30, 40); });
			Completed = m_Build.Run(m_Text, m_Index, m_Reuse, true, m_Cache, [&] {
				m_Index = m_Candidate;
				Scope.Finish();
			});
		}
		Triggered = Fault.Triggered();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_TRUE(Completed);
	ExpectContextRestored();
}
