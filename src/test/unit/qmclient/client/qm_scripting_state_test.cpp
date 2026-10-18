#include <game/client/components/qmclient/scripting/state.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>

TEST(QmScriptingState, MissingLocalPlayerDoesNotReadTeamArrays)
{
	for(const int Id : std::array<int, 4>{-1, MAX_CLIENTS, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
	{
		const auto Result = QmScriptLocalTeam(Id, [](int) { ADD_FAILURE() << "invalid player accessed team data"; return 0; });
		EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(Result)) << Id;
	}
}

TEST(QmScriptingState, CurrentLocalPlayerIsReadAfterReconnectOrDummySwitch)
{
	std::array<int, MAX_CLIENTS> aTeams{};
	aTeams[0] = -1;
	aTeams[7] = 3;
	aTeams[MAX_CLIENTS - 1] = 9;
	const auto Read = [&](int Id) { return aTeams.at(Id); };
	EXPECT_EQ(std::get<int>(QmScriptLocalTeam(0, Read)), -1);
	EXPECT_EQ(std::get<int>(QmScriptLocalTeam(7, Read)), 3);
	EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(QmScriptLocalTeam(-1, Read)));
	EXPECT_EQ(std::get<int>(QmScriptLocalTeam(MAX_CLIENTS - 1, Read)), 9);
}

TEST(QmScriptingState, MissingMapOrNonFinitePositionReturnsEmptyState)
{
	EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(QmScriptLocation(0, 100, 0, 0)));
	EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(QmScriptLocation(100, -1, 0, 0)));
	EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(QmScriptLocation(100, 100, std::numeric_limits<float>::infinity(), 0)));
	EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(QmScriptLocation(100, 100, 0, std::numeric_limits<float>::quiet_NaN())));
}

TEST(QmScriptingState, LocationPreservesMapSectionsAndClampsBeforeIntegerConversion)
{
	EXPECT_EQ(std::get<std::string>(QmScriptLocation(100, 100, 0, 0)), "NW");
	EXPECT_EQ(std::get<std::string>(QmScriptLocation(100, 100, 1500, 1500)), "C");
	EXPECT_EQ(std::get<std::string>(QmScriptLocation(100, 100, 2999, 2999)), "SE");
	EXPECT_EQ(std::get<std::string>(QmScriptLocation(1, 1, std::numeric_limits<float>::max(), -std::numeric_limits<float>::max())), "NE");
}
