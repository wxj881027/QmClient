#include <engine/shared/packer.h>

#include <generated/protocol.h>

#include <game/client/components/qmclient/nameplate_coordinates.h>
#include <game/gamecore.h>

#include <gtest/gtest.h>

namespace
{
	std::optional<vec2> ReadPosition(int X, int Y)
	{
		CNetObj_Character Character{};
		Character.m_Tick = 7;
		Character.m_X = X;
		Character.m_Y = Y;
		Character.m_HookedPlayer = -1;
		CNetObjHandler Handler;
		CUnpacker Unpacker;
		Unpacker.Reset(&Character, sizeof(Character));
		const auto *pUnpacked = static_cast<const CNetObj_Character *>(Handler.SecureUnpackObj(NETOBJTYPE_CHARACTER, &Unpacker));
		if(!pUnpacked)
			return std::nullopt;
		CCharacterCore Core{};
		Core.Read(pUnpacked);
		return Core.m_Pos;
	}
}

TEST(QmNameplateCoordinatesIntegration, ExtremeSnapshotPositionsCanBeDisplayedWithoutNarrowingToInt)
{
	const auto Positive = ReadPosition(std::numeric_limits<int>::max(), 0);
	const auto Negative = ReadPosition(std::numeric_limits<int>::min(), 0);
	ASSERT_TRUE(Positive);
	ASSERT_TRUE(Negative);
	const auto PositiveUnits = QmNameplateCentitiles(Positive->x / 32.0f);
	const auto NegativeUnits = QmNameplateCentitiles(Negative->x / 32.0f);
	ASSERT_TRUE(PositiveUnits);
	ASSERT_TRUE(NegativeUnits);
	EXPECT_EQ(*PositiveUnits, INT64_C(6710886400));
	EXPECT_EQ(*NegativeUnits, -INT64_C(6710886400));
	const auto Display = QmNameplateDisplayCoordinate(Positive->x / 32.0f);
	ASSERT_TRUE(Display);
	EXPECT_FLOAT_EQ(*Display, 67108864.0f);
}

TEST(QmNameplateCoordinatesIntegration, IndividuallyRepresentablePositionsHaveAWideAlignmentDistance)
{
	const auto Left = ReadPosition(-640000000, 0);
	const auto Right = ReadPosition(640000000, 0);
	ASSERT_TRUE(Left);
	ASSERT_TRUE(Right);
	const auto LeftUnits = QmNameplateCentitiles(Left->x / 32.0f);
	const auto RightUnits = QmNameplateCentitiles(Right->x / 32.0f);
	ASSERT_TRUE(LeftUnits);
	ASSERT_TRUE(RightUnits);
	EXPECT_EQ(*LeftUnits, -INT64_C(2000000000));
	EXPECT_EQ(*RightUnits, INT64_C(2000000000));
	EXPECT_EQ(QmNameplateCoordinateDistance(*LeftUnits, *RightUnits), UINT64_C(4000000000));
}

TEST(QmNameplateCoordinatesIntegration, OrdinarySnapshotInterpolationKeepsCoordinateNormalization)
{
	const auto Previous = ReadPosition(160, 320);
	const auto Current = ReadPosition(192, 384);
	ASSERT_TRUE(Previous);
	ASSERT_TRUE(Current);
	const vec2 Position = mix(*Previous, *Current, 0.25f);
	const auto X = QmNameplateDisplayCoordinate(Position.x / 32.0f);
	const auto Y = QmNameplateDisplayCoordinate(Position.y / 32.0f);
	ASSERT_TRUE(X);
	ASSERT_TRUE(Y);
	EXPECT_FLOAT_EQ(*X, 5.25f);
	EXPECT_FLOAT_EQ(*Y, 10.5f);
}
