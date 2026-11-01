#include <game/client/components/qmclient/bg_draw_quad_containers.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <array>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace
{
	struct SGraphics
	{
		int m_NextIndex = 0;
		int m_Creates = 0, m_Uploads = 0, m_Appends = 0, m_Deletes = 0;
		int m_LastAppendIndex = -1, m_LastAppendCount = 0;
		int m_DeleteFailureIndex = -1;
		bool m_AutomaticUpload = true;
		bool m_CreateFailure = false, m_UploadFailure = false, m_AppendFailure = false, m_RejectAppend = false;
		std::array<int, 4> m_aDeleted{};
		ColorRGBA m_Color{1, 1, 1, 1};

		int CreateQuadContainer(bool AutomaticUpload)
		{
			++m_Creates;
			m_AutomaticUpload = AutomaticUpload;
			if(m_CreateFailure)
				throw std::bad_alloc();
			return m_NextIndex++;
		}
		void QuadContainerUpload(int)
		{
			++m_Uploads;
			if(m_UploadFailure)
				throw std::length_error("quad upload");
		}
		void SetColor(ColorRGBA Color) { m_Color = Color; }
		int QuadContainerAddQuads(int Index, IGraphics::CFreeformItem *, int Count)
		{
			++m_Appends;
			m_LastAppendIndex = Index;
			m_LastAppendCount = Count;
			if(m_AppendFailure)
				throw std::bad_alloc();
			return m_RejectAppend ? -1 : 0;
		}
		void DeleteQuadContainer(int &Index)
		{
			if(Index == m_DeleteFailureIndex)
				throw 23;
			m_aDeleted[m_Deletes++] = Index;
			Index = -1;
		}
	};

	const ColorRGBA gs_Color{0.2f, 0.4f, 0.6f, 0.8f};
}

TEST(QmBgDrawQuadContainers, SuccessfulBatchesReuseTheTailAndPreserveTheExistingGroupLimit)
{
	SGraphics Graphics;
	std::vector<int> vIndexes;
	int QuadCount = 0;
	IGraphics::CFreeformItem aQuads[512]{};
	QmBgDrawAppendQuads(Graphics, vIndexes, QuadCount, aQuads, 511, gs_Color);
	QmBgDrawAppendQuads(Graphics, vIndexes, QuadCount, aQuads, 1, gs_Color);
	EXPECT_EQ(vIndexes, (std::vector<int>{0}));
	EXPECT_EQ(QuadCount, 512);
	EXPECT_EQ(Graphics.m_Creates, 1);
	EXPECT_EQ(Graphics.m_Uploads, 0);
	QmBgDrawAppendQuads(Graphics, vIndexes, QuadCount, aQuads, 1, gs_Color);
	EXPECT_EQ(vIndexes, (std::vector<int>{0, 1}));
	EXPECT_EQ(QuadCount, 1);
	EXPECT_EQ(Graphics.m_Creates, 2);
	EXPECT_EQ(Graphics.m_Uploads, 1);
	EXPECT_EQ(Graphics.m_Appends, 3);
	EXPECT_EQ(Graphics.m_LastAppendIndex, 1);
	EXPECT_EQ(Graphics.m_LastAppendCount, 1);
	EXPECT_FALSE(Graphics.m_AutomaticUpload);
	EXPECT_EQ(Graphics.m_Color, gs_Color);
}

TEST(QmBgDrawQuadContainers, OwnerAllocationFailureDoesNotCreateAResource)
{
	SGraphics Graphics;
	std::vector<int> vIndexes;
	int QuadCount = 0;
	IGraphics::CFreeformItem Quad{};
	bool Threw = false, Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		try
		{
			QmBgDrawAppendQuads(Graphics, vIndexes, QuadCount, &Quad, 1, gs_Color);
		}
		catch(const std::bad_alloc &)
		{
			Threw = true;
		}
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Threw);
	EXPECT_TRUE(Triggered);
	EXPECT_EQ(Attempts, 1u);
	EXPECT_TRUE(vIndexes.empty());
	EXPECT_EQ(QuadCount, 0);
	EXPECT_EQ(Graphics.m_Creates, 0);
	EXPECT_EQ(Graphics.m_Appends, 0);
}

TEST(QmBgDrawQuadContainers, CreateFailureRemovesOnlyItsPlaceholderAndAllowsRetry)
{
	SGraphics Graphics;
	Graphics.m_NextIndex = 1;
	Graphics.m_CreateFailure = true;
	std::vector<int> vIndexes{0};
	int QuadCount = 512;
	IGraphics::CFreeformItem Quad{};
	EXPECT_THROW(QmBgDrawAppendQuads(Graphics, vIndexes, QuadCount, &Quad, 1, gs_Color), std::bad_alloc);
	EXPECT_EQ(vIndexes, (std::vector<int>{0}));
	EXPECT_EQ(QuadCount, 512);
	EXPECT_EQ(Graphics.m_Appends, 0);
	Graphics.m_CreateFailure = false;
	QmBgDrawAppendQuads(Graphics, vIndexes, QuadCount, &Quad, 1, gs_Color);
	EXPECT_EQ(vIndexes, (std::vector<int>{0, 1}));
	EXPECT_EQ(QuadCount, 1);
}

TEST(QmBgDrawQuadContainers, RolloverUploadFailurePreservesTheTailAndItsCount)
{
	SGraphics Graphics;
	Graphics.m_UploadFailure = true;
	std::vector<int> vIndexes{0};
	int QuadCount = 512;
	IGraphics::CFreeformItem Quad{};
	EXPECT_THROW(QmBgDrawAppendQuads(Graphics, vIndexes, QuadCount, &Quad, 1, gs_Color), std::length_error);
	EXPECT_EQ(vIndexes, (std::vector<int>{0}));
	EXPECT_EQ(QuadCount, 512);
	EXPECT_EQ(Graphics.m_Creates, 0);
	EXPECT_EQ(Graphics.m_Appends, 0);
}

TEST(QmBgDrawQuadContainers, AppendFailureKeepsTheNewOwnerAndRetryReusesItWithoutDoubleCounting)
{
	SGraphics Graphics;
	Graphics.m_AppendFailure = true;
	std::vector<int> vIndexes;
	int QuadCount = 0;
	IGraphics::CFreeformItem aQuads[2]{};
	EXPECT_THROW(QmBgDrawAppendQuads(Graphics, vIndexes, QuadCount, aQuads, 2, gs_Color), std::bad_alloc);
	EXPECT_EQ(vIndexes, (std::vector<int>{0}));
	EXPECT_EQ(QuadCount, 0);
	Graphics.m_AppendFailure = false;
	QmBgDrawAppendQuads(Graphics, vIndexes, QuadCount, aQuads, 2, gs_Color);
	EXPECT_EQ(vIndexes, (std::vector<int>{0}));
	EXPECT_EQ(QuadCount, 2);
	EXPECT_EQ(Graphics.m_Creates, 1);
}

TEST(QmBgDrawQuadContainers, RejectedAppendDoesNotAdvanceTheSuccessfulCount)
{
	SGraphics Graphics;
	Graphics.m_RejectAppend = true;
	std::vector<int> vIndexes{0};
	int QuadCount = 500;
	IGraphics::CFreeformItem Quad{};
	QmBgDrawAppendQuads(Graphics, vIndexes, QuadCount, &Quad, 1, gs_Color);
	EXPECT_EQ(QuadCount, 500);
	EXPECT_EQ(vIndexes, (std::vector<int>{0}));
	EXPECT_EQ(Graphics.m_Creates, 0);
}

TEST(QmBgDrawQuadContainers, PartialClearRetainsUnreleasedOwnersAndRetrySkipsTheReleasedPrefix)
{
	SGraphics Graphics;
	Graphics.m_DeleteFailureIndex = 1;
	std::vector<int> vIndexes{0, 1, 2};
	int QuadCount = 8;
	int Error = 0;
	try
	{
		QmBgDrawClearQuadContainers(Graphics, vIndexes, QuadCount);
	}
	catch(int Value)
	{
		Error = Value;
	}
	EXPECT_EQ(Error, 23);
	EXPECT_EQ(vIndexes, (std::vector<int>{-1, 1, 2}));
	EXPECT_EQ(QuadCount, 8);
	EXPECT_EQ(Graphics.m_Deletes, 1);
	Graphics.m_DeleteFailureIndex = -1;
	QmBgDrawClearQuadContainers(Graphics, vIndexes, QuadCount);
	EXPECT_TRUE(vIndexes.empty());
	EXPECT_EQ(QuadCount, 0);
	ASSERT_EQ(Graphics.m_Deletes, 3);
	EXPECT_EQ(Graphics.m_aDeleted[0], 0);
	EXPECT_EQ(Graphics.m_aDeleted[1], 1);
	EXPECT_EQ(Graphics.m_aDeleted[2], 2);
}

TEST(QmBgDrawQuadContainers, ClearAndRepeatedClearDoNotAllocate)
{
	SGraphics Graphics;
	std::vector<int> vIndexes{0, 1};
	int QuadCount = 512;
	bool Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		QmBgDrawClearQuadContainers(Graphics, vIndexes, QuadCount);
		QmBgDrawClearQuadContainers(Graphics, vIndexes, QuadCount);
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	EXPECT_EQ(Graphics.m_Deletes, 2);
	EXPECT_TRUE(vIndexes.empty());
	EXPECT_EQ(QuadCount, 0);
}

TEST(QmBgDrawQuadContainers, InvalidBatchesAreRejectedBeforeCreatingOrChangingOwners)
{
	SGraphics Graphics;
	std::vector<int> vIndexes;
	int QuadCount = 0;
	IGraphics::CFreeformItem Quad{};
	for(int Count : {0, -1, 513, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
	{
		SCOPED_TRACE(Count);
		QmBgDrawAppendQuads(Graphics, vIndexes, QuadCount, &Quad, Count, gs_Color);
		EXPECT_TRUE(vIndexes.empty());
		EXPECT_EQ(QuadCount, 0);
		EXPECT_EQ(Graphics.m_Creates, 0);
		EXPECT_EQ(Graphics.m_Appends, 0);
	}
	QmBgDrawAppendQuads(Graphics, vIndexes, QuadCount, nullptr, 1, gs_Color);
	EXPECT_TRUE(vIndexes.empty());
	EXPECT_EQ(QuadCount, 0);
	EXPECT_EQ(Graphics.m_Creates, 0);
	EXPECT_EQ(Graphics.m_Appends, 0);
}
