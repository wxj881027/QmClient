#include <game/client/components/tclient/qm_tee_trail.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

namespace
{
	qm_tee_trail::SQuad Quad(float X, bool Additive = false)
	{
		qm_tee_trail::SQuad Result;
		for(int i = 0; i < 4; ++i)
		{
			Result.m_aPos[i] = vec2(X + i, i);
			Result.m_aColor[i] = ColorRGBA(i / 4.0f, 0.5f, 1, 0.75f);
		}
		Result.m_Additive = Additive;
		return Result;
	}
}

TEST(QmTeeTrailFrame, CompletePreparationPreservesPlayerQuadAndBlendOrder)
{
	std::vector<qm_tee_trail::SQuad> vFrame;
	int Prepared = 0;
	bool Drawn = false;
	EXPECT_TRUE(qm_tee_trail::RenderFrame(vFrame, [&](auto &vOut) {
		vOut.push_back(Quad(10));
		++Prepared;
		vOut.push_back(Quad(20, true));
		vOut.push_back(Quad(30));
		++Prepared; }, [&](const auto &vOut) {
		EXPECT_EQ(Prepared, 2);
		ASSERT_EQ(vOut.size(), 3u);
		EXPECT_EQ(vOut[0].m_aPos[0], vec2(10, 0));
		EXPECT_EQ(vOut[1].m_aPos[3], vec2(23, 3));
		EXPECT_EQ(vOut[1].m_aColor[2].r, 0.5f);
		EXPECT_FALSE(vOut[0].m_Additive);
		EXPECT_TRUE(vOut[1].m_Additive);
		EXPECT_FALSE(vOut[2].m_Additive);
		Drawn = true; }));
	EXPECT_TRUE(Drawn);
}

TEST(QmTeeTrailFrame, ANewFrameDiscardsPreviousGeometryBeforePreparation)
{
	std::vector<qm_tee_trail::SQuad> vFrame = {Quad(100)};
	bool EmptyAtEntry = false;
	ASSERT_TRUE(qm_tee_trail::RenderFrame(vFrame, [&](auto &vOut) {
		EmptyAtEntry = vOut.empty();
		vOut.push_back(Quad(200)); }, [](const auto &) {}));
	EXPECT_TRUE(EmptyAtEntry);
	ASSERT_EQ(vFrame.size(), 1u);
	EXPECT_EQ(vFrame[0].m_aPos[0], vec2(200, 0));
}

TEST(QmTeeTrailFrame, SuccessfulEmptyPreparationStillInvokesTheDrawingPhase)
{
	std::vector<qm_tee_trail::SQuad> vFrame = {Quad(100)};
	bool Drawn = false;
	EXPECT_TRUE(qm_tee_trail::RenderFrame(vFrame, [](auto &) {}, [&](const auto &vOut) {
		EXPECT_TRUE(vOut.empty());
		Drawn = true; }));
	EXPECT_TRUE(Drawn);
}

TEST(QmTeeTrailFrame, InitialVectorAllocationFailureDoesNotBeginDrawing)
{
	std::vector<qm_tee_trail::SQuad> vFrame;
	bool Complete, Triggered, Drawn = false;
	{
		CPredictionAllocationFailure Fault;
		Complete = qm_tee_trail::RenderFrame(vFrame, [](auto &vOut) { vOut.push_back(Quad(10)); }, [&](const auto &) { Drawn = true; });
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Complete);
	EXPECT_FALSE(Drawn);
	EXPECT_TRUE(vFrame.empty());
}

TEST(QmTeeTrailFrame, GrowthFailureDiscardsTheAlreadyPreparedPrefix)
{
	std::vector<qm_tee_trail::SQuad> vFrame;
	vFrame.reserve(1);
	bool Complete, Triggered, Drawn = false;
	size_t PrefixSize = 0;
	{
		CPredictionAllocationFailure Fault;
		Complete = qm_tee_trail::RenderFrame(vFrame, [&](auto &vOut) {
			vOut.push_back(Quad(10));
			PrefixSize = vOut.size();
			vOut.reserve(vOut.capacity() + 1); }, [&](const auto &) { Drawn = true; });
		Triggered = Fault.Triggered();
	}
	EXPECT_EQ(PrefixSize, 1u);
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Complete);
	EXPECT_FALSE(Drawn);
	EXPECT_TRUE(vFrame.empty());
}

TEST(QmTeeTrailFrame, PreparationLengthFailureClearsPartialGeometryWithoutDrawing)
{
	std::vector<qm_tee_trail::SQuad> vFrame;
	bool Drawn = false;
	EXPECT_FALSE(qm_tee_trail::RenderFrame(vFrame, [](auto &vOut) {
		vOut.push_back(Quad(10));
		throw std::length_error("trail preparation"); }, [&](const auto &) { Drawn = true; }));
	EXPECT_FALSE(Drawn);
	EXPECT_TRUE(vFrame.empty());
}

TEST(QmTeeTrailFrame, TheFrameAfterAllocationFailureCanDrawOnlyItsNewGeometry)
{
	std::vector<qm_tee_trail::SQuad> vFrame;
	bool Failed, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Failed = !qm_tee_trail::RenderFrame(vFrame, [](auto &vOut) { vOut.push_back(Quad(10)); }, [](const auto &) {});
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Failed);
	ASSERT_TRUE(Triggered);
	bool Drawn = false;
	EXPECT_TRUE(qm_tee_trail::RenderFrame(vFrame, [](auto &vOut) { vOut.push_back(Quad(20)); }, [&](const auto &vOut) {
		ASSERT_EQ(vOut.size(), 1u);
		EXPECT_EQ(vOut[0].m_aPos[0], vec2(20, 0));
		Drawn = true; }));
	EXPECT_TRUE(Drawn);
}

TEST(QmTeeTrailFrame, UnexpectedPreparationFailureClearsTheFrameAndPropagates)
{
	std::vector<qm_tee_trail::SQuad> vFrame;
	bool Drawn = false;
	EXPECT_THROW(qm_tee_trail::RenderFrame(vFrame, [](auto &vOut) {
		vOut.push_back(Quad(10));
		throw std::logic_error("unexpected preparation failure"); }, [&](const auto &) { Drawn = true; }), std::logic_error);
	EXPECT_FALSE(Drawn);
	EXPECT_TRUE(vFrame.empty());
}

TEST(QmTeeTrailFrame, DrawingAllocationFailurePropagatesWithoutPretendingToRecover)
{
	std::vector<qm_tee_trail::SQuad> vFrame;
	EXPECT_THROW(qm_tee_trail::RenderFrame(vFrame, [](auto &vOut) { vOut.push_back(Quad(10)); }, [](const auto &) { throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_EQ(vFrame.size(), 1u);
}

TEST(QmTeeTrailFrame, DrawingLengthFailureStillPropagates)
{
	std::vector<qm_tee_trail::SQuad> vFrame;
	EXPECT_THROW(qm_tee_trail::RenderFrame(vFrame, [](auto &) {}, [](const auto &) { throw std::length_error("draw failure"); }), std::length_error);
}

TEST(QmTeeTrailFrame, ReservedFrameStorageCanBeReusedWithoutAnAllocation)
{
	std::vector<qm_tee_trail::SQuad> vFrame;
	vFrame.reserve(2);
	const auto *pStorage = vFrame.data();
	bool Complete, Triggered;
	size_t DrawnCount = 0;
	{
		CPredictionAllocationFailure Fault;
		Complete = qm_tee_trail::RenderFrame(vFrame, [](auto &vOut) {
			vOut.push_back(Quad(10));
			vOut.push_back(Quad(20)); }, [&](const auto &vOut) { DrawnCount = vOut.size(); });
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Complete);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(DrawnCount, 2u);
	EXPECT_EQ(vFrame.data(), pStorage);
}

TEST(QmTeeTrailFrame, PreparationFailureDoesNotRollBackExternalSamplingProgress)
{
	std::vector<qm_tee_trail::SQuad> vFrame;
	int SampledPlayers = 0;
	bool Complete, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Complete = qm_tee_trail::RenderFrame(vFrame, [&](auto &vOut) {
			++SampledPlayers;
			vOut.push_back(Quad(10)); }, [](const auto &) {});
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Complete);
	EXPECT_EQ(SampledPlayers, 1);
	EXPECT_TRUE(vFrame.empty());
}
