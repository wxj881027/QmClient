#include <game/qm_axiom_quad_collision.h>

#include <gtest/gtest.h>

#include <limits>
#include <vector>

TEST(QmAxiomCheckpoint, FindsHighestMapExitBelowOutOfRangeCheckpoint)
{
	const std::vector<vec2> vHigher{vec2(100.0f, 200.0f)};
	const std::vector<vec2> vLower{vec2(10.0f, 20.0f)};
	const std::vector<vec2> vEmpty;
	std::vector<int> vLookups;

	const auto *pvOuts = qm_axiom::FindTeleCheckpointOuts(std::numeric_limits<int>::max(), [&](int Index) -> const std::vector<vec2> & {
		vLookups.push_back(Index);
		if(Index == 253)
			return vHigher;
		return Index == 0 ? vLower : vEmpty;
	});

	EXPECT_EQ(pvOuts, &vHigher);
	EXPECT_EQ(vLookups, (std::vector<int>{254, 253}));
}

TEST(QmAxiomCheckpoint, NonpositiveCheckpointDoesNotReadAnyOuts)
{
	const std::vector<vec2> vEmpty;
	for(int Checkpoint : {std::numeric_limits<int>::min(), -1, 0})
	{
		const auto *pvOuts = qm_axiom::FindTeleCheckpointOuts(Checkpoint, [&vEmpty](int) -> const std::vector<vec2> & {
			ADD_FAILURE() << "no checkpoint exit should be read";
			return vEmpty;
		});
		EXPECT_EQ(pvOuts, nullptr);
	}
}
