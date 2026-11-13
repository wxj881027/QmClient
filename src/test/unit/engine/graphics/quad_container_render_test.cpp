#include <engine/client/quad_container_render.h>

#include <gtest/gtest.h>

#include <new>
#include <stdexcept>
#include <vector>

namespace
{
	struct SQuad
	{
		int m_aVertices[4];
	};
	const std::vector<SQuad> gs_vQuads = {{{0, 1, 2, 3}}, {{10, 11, 12, 13}}, {{20, 21, 22, 23}}, {{30, 31, 32, 33}}, {{40, 41, 42, 43}}};
}

TEST(QuadContainerRange, NegativeAppendCountIsRejected)
{
	EXPECT_FALSE(GraphicsCanAppendQuads(0, -1));
	EXPECT_FALSE(GraphicsCanAppendQuads(0, std::numeric_limits<int>::min()));
}

TEST(QuadContainerRange, AppendCannotOverflowTheCommandIndexRepresentation)
{
	EXPECT_TRUE(GraphicsCanAppendQuads(GRAPHICS_MAX_CONTAINER_QUADS - 1, 1));
	EXPECT_FALSE(GraphicsCanAppendQuads(GRAPHICS_MAX_CONTAINER_QUADS, 1));
	EXPECT_FALSE(GraphicsCanAppendQuads(std::numeric_limits<size_t>::max(), 1));
	EXPECT_FALSE(GraphicsCanAppendQuads(1, std::numeric_limits<int>::max()));
}

TEST(QuadContainerRange, ValidContainerCanExceedOneTemporaryVertexBatch)
{
	EXPECT_TRUE(GraphicsCanAppendQuads(40000, 10));
	EXPECT_TRUE(GraphicsCanAppendQuads(GRAPHICS_MAX_CONTAINER_QUADS, 0));
}

TEST(QuadContainerRange, AllRemainingQuadsStartAtTheRequestedOffset)
{
	int Count = -1;
	ASSERT_TRUE(GraphicsResolveQuadRange(9, 4, Count));
	EXPECT_EQ(Count, 5);
}

TEST(QuadContainerRange, NegativeOffsetsCannotIndexBeforeTheContainer)
{
	int Count = 1;
	EXPECT_FALSE(GraphicsResolveQuadRange(9, -1, Count));
	EXPECT_FALSE(GraphicsResolveQuadRange(9, std::numeric_limits<int>::min(), Count));
}

TEST(QuadContainerRange, CountsBelowTheAllRemainingSentinelAreRejected)
{
	int Count = -2;
	EXPECT_FALSE(GraphicsResolveQuadRange(9, 0, Count));
	Count = std::numeric_limits<int>::min();
	EXPECT_FALSE(GraphicsResolveQuadRange(9, 0, Count));
}

TEST(QuadContainerRange, RangeChecksDoNotAddUntrustedSignedIntegers)
{
	int Count = std::numeric_limits<int>::max();
	EXPECT_FALSE(GraphicsResolveQuadRange(9, std::numeric_limits<int>::max(), Count));
	EXPECT_FALSE(GraphicsResolveQuadRange(9, 1, Count));
	Count = 8;
	EXPECT_FALSE(GraphicsResolveQuadRange(9, 2, Count));
}

TEST(QuadContainerRange, EmptyRangesAndOffsetsAtTheEndDoNotDraw)
{
	int Count = -1;
	EXPECT_FALSE(GraphicsResolveQuadRange(9, 9, Count));
	EXPECT_EQ(Count, 0);
	Count = 1;
	EXPECT_FALSE(GraphicsResolveQuadRange(9, 9, Count));
	Count = 0;
	EXPECT_FALSE(GraphicsResolveQuadRange(9, 0, Count));
	Count = -1;
	EXPECT_FALSE(GraphicsResolveQuadRange(0, 0, Count));
}

TEST(QuadContainerRange, CountsLargerThanTheCommandRepresentationAreRejected)
{
	int Count = 1;
	EXPECT_FALSE(GraphicsResolveQuadRange(GRAPHICS_MAX_CONTAINER_QUADS + 1, 0, Count));
}

TEST(QuadContainerRange, BufferedDrawsRequestIndicesPastTheInitialCapacity)
{
	EXPECT_EQ(GraphicsQuadIndicesToNotify(0, 8192), 0u);
	EXPECT_EQ(GraphicsQuadIndicesToNotify(8192, 8192), 0u);
	EXPECT_EQ(GraphicsQuadIndicesToNotify(8193, 8192), 49158u);
	EXPECT_EQ(GraphicsQuadIndicesToNotify(GRAPHICS_MAX_CONTAINER_QUADS + 1, 8192), 0u);
}

TEST(QuadContainerStream, QuadBatchesPreserveEveryVertexAndTheirOrder)
{
	int aVertices[8]{};
	std::vector<int> vOutput, vSizes;
	ASSERT_TRUE(StreamGraphicsQuads(gs_vQuads, 0, -1, false, aVertices, [](int *, int) {}, [&](int Count) {
		vSizes.push_back(Count);
		vOutput.insert(vOutput.end(), aVertices, aVertices + Count); }));
	EXPECT_EQ(vSizes, (std::vector<int>{8, 8, 4}));
	EXPECT_EQ(vOutput, (std::vector<int>{0, 1, 2, 3, 10, 11, 12, 13, 20, 21, 22, 23, 30, 31, 32, 33, 40, 41, 42, 43}));
}

TEST(QuadContainerStream, TriangleBatchesNeverUseAnIncompleteQuadSlot)
{
	struct SArena
	{
		int m_Before = -7;
		int m_aVertices[13]{};
		int m_After = -9;
	} Arena;
	Arena.m_aVertices[12] = -11;
	std::vector<int> vOutput, vSizes;
	ASSERT_TRUE(StreamGraphicsQuads(gs_vQuads, 0, -1, true, Arena.m_aVertices, [](int *, int) {}, [&](int Count) {
		vSizes.push_back(Count);
		vOutput.insert(vOutput.end(), Arena.m_aVertices, Arena.m_aVertices + Count); }));
	EXPECT_EQ(vSizes, (std::vector<int>{12, 12, 6}));
	EXPECT_EQ(vOutput, (std::vector<int>{0, 1, 2, 0, 2, 3, 10, 11, 12, 10, 12, 13, 20, 21, 22, 20, 22, 23, 30, 31, 32, 30, 32, 33, 40, 41, 42, 40, 42, 43}));
	EXPECT_EQ(Arena.m_aVertices[12], -11);
	EXPECT_EQ(Arena.m_Before, -7);
	EXPECT_EQ(Arena.m_After, -9);
}

TEST(QuadContainerStream, ExplicitSubrangeDoesNotReadOrSubmitSurroundingQuads)
{
	int aVertices[8]{};
	std::vector<int> vOutput;
	ASSERT_TRUE(StreamGraphicsQuads(gs_vQuads, 2, 2, false, aVertices, [](int *, int) {}, [&](int Count) { vOutput.insert(vOutput.end(), aVertices, aVertices + Count); }));
	EXPECT_EQ(vOutput, (std::vector<int>{20, 21, 22, 23, 30, 31, 32, 33}));
}

TEST(QuadContainerStream, TransformReceivesOneCompleteQuadBeforeSubmission)
{
	int aVertices[6]{};
	int Calls = 0;
	std::vector<int> vOutput;
	ASSERT_TRUE(StreamGraphicsQuads(gs_vQuads, 1, 2, true, aVertices, [&](int *pVertices, int Count) {
		EXPECT_EQ(Count, 6);
		++Calls;
		for(int n = 0; n < Count; ++n)
			pVertices[n] += 100; }, [&](int Count) { vOutput.insert(vOutput.end(), aVertices, aVertices + Count); }));
	EXPECT_EQ(Calls, 2);
	EXPECT_EQ(vOutput, (std::vector<int>{110, 111, 112, 110, 112, 113, 120, 121, 122, 120, 122, 123}));
	EXPECT_EQ(gs_vQuads[1].m_aVertices[0], 10);
}

TEST(QuadContainerStream, InvalidRangeDoesNotTransformSubmitOrWrite)
{
	int aVertices[6]{-1, -1, -1, -1, -1, -1};
	int Calls = 0;
	EXPECT_FALSE(StreamGraphicsQuads(gs_vQuads, -1, 1, false, aVertices, [&](int *, int) { ++Calls; }, [&](int) { ++Calls; }));
	EXPECT_EQ(Calls, 0);
	for(int Vertex : aVertices)
		EXPECT_EQ(Vertex, -1);
}

TEST(QuadContainerStream, SubmissionFailureDoesNotReplayTheAcceptedPrefix)
{
	int aVertices[8]{};
	int Calls = 0;
	std::vector<int> vAccepted;
	EXPECT_THROW(StreamGraphicsQuads(gs_vQuads, 0, -1, false, aVertices, [](int *, int) {}, [&](int Count) {
		if(++Calls == 2)
			throw std::bad_alloc();
		vAccepted.insert(vAccepted.end(), aVertices, aVertices + Count); }), std::bad_alloc);
	EXPECT_EQ(Calls, 2);
	EXPECT_EQ(vAccepted, (std::vector<int>{0, 1, 2, 3, 10, 11, 12, 13}));
}

TEST(QuadContainerStream, TransformFailureDoesNotSubmitTheIncompleteBatch)
{
	int aVertices[8]{};
	int Transforms = 0, Submissions = 0;
	EXPECT_THROW(StreamGraphicsQuads(gs_vQuads, 0, -1, false, aVertices, [&](int *, int) {
		if(++Transforms == 2)
			throw std::runtime_error("transform stopped"); }, [&](int) { ++Submissions; }), std::runtime_error);
	EXPECT_EQ(Transforms, 2);
	EXPECT_EQ(Submissions, 0);
}
