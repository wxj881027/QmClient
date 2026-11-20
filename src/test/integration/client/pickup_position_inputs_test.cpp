#include <engine/shared/packer.h>

#include <generated/protocol.h>

#include <game/client/pickup_data.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	template<typename T>
	CPickupData ExtractSecured(int Type, const T &Input, const CNetObj_EntityEx *pExtension = nullptr)
	{
		CUnpacker Unpacker;
		Unpacker.Reset(&Input, sizeof(Input));
		CNetObjHandler Handler;
		const void *pSecured = Handler.SecureUnpackObj(Type, &Unpacker);
		EXPECT_NE(pSecured, nullptr);
		return pSecured ? ExtractPickupInfo(Type, pSecured, pExtension) : CPickupData{};
	}
}

TEST(PickupPositionInputs, SecureLegacyMaximumCannotBeSerializedAsValidFloatGeometry)
{
	CNetObj_Pickup Input{};
	Input.m_X = std::numeric_limits<int>::max();
	const auto Data = ExtractSecured(NETOBJTYPE_PICKUP, Input);
	EXPECT_FALSE(Data.HasValidPosition());
	CNetObj_Pickup Out{};
	Out.m_X = 50;
	EXPECT_FALSE(Data.TryFillNetObj(Out));
	EXPECT_EQ(Out.m_X, 50);
}

TEST(PickupPositionInputs, SecureExtendedMaximumIsRejectedWithoutRewritingFlags)
{
	CNetObj_DDNetPickup Input{};
	Input.m_Y = std::numeric_limits<int>::max();
	Input.m_Flags = PICKUPFLAG_NO_PREDICT;
	const auto Data = ExtractSecured(NETOBJTYPE_DDNETPICKUP, Input);
	EXPECT_FALSE(Data.HasValidPosition());
	EXPECT_EQ(Data.m_Flags, PICKUPFLAG_NO_PREDICT);
}

TEST(PickupPositionInputs, LegacyOffMapPositionAndEntitySwitchRemainCompatible)
{
	CNetObj_Pickup Input{};
	Input.m_X = -100000;
	Input.m_Y = 100000;
	CNetObj_EntityEx Extension{};
	Extension.m_SwitchNumber = 4;
	const auto Data = ExtractSecured(NETOBJTYPE_PICKUP, Input, &Extension);
	EXPECT_TRUE(Data.HasValidPosition());
	EXPECT_EQ(Data.m_SwitchNumber, 4);
	EXPECT_EQ(Data.m_Flags, 0);
	CNetObj_Pickup Out{};
	ASSERT_TRUE(Data.TryFillNetObj(Out));
	EXPECT_EQ(Out.m_X, Input.m_X);
	EXPECT_EQ(Out.m_Y, Input.m_Y);
}

TEST(PickupPositionInputs, ExtendedConversionKeepsTypeSubtypeAndSeparateFlags)
{
	CNetObj_DDNetPickup Input{};
	Input.m_X = 32;
	Input.m_Y = 64;
	Input.m_Type = POWERUP_WEAPON;
	Input.m_Subtype = WEAPON_GRENADE;
	Input.m_SwitchNumber = 7;
	Input.m_Flags = PICKUPFLAG_NO_PREDICT;
	const auto Data = ExtractSecured(NETOBJTYPE_DDNETPICKUP, Input);
	EXPECT_TRUE(Data.HasValidPosition());
	EXPECT_EQ(Data.m_SwitchNumber, 7);
	EXPECT_EQ(Data.m_Flags, PICKUPFLAG_NO_PREDICT);
	CNetObj_Pickup Out{};
	ASSERT_TRUE(Data.TryFillNetObj(Out));
	EXPECT_EQ(Out.m_Type, Input.m_Type);
	EXPECT_EQ(Out.m_Subtype, Input.m_Subtype);
}

TEST(PickupPositionInputs, RepresentableBoundsSurviveExtractionAndSerialization)
{
	CNetObj_Pickup Input{};
	Input.m_X = std::numeric_limits<int>::min();
	Input.m_Y = 2147483520;
	const auto Data = ExtractSecured(NETOBJTYPE_PICKUP, Input);
	EXPECT_TRUE(Data.HasValidPosition());
	CNetObj_Pickup Out{};
	ASSERT_TRUE(Data.TryFillNetObj(Out));
	EXPECT_EQ(Out.m_X, Input.m_X);
	EXPECT_EQ(Out.m_Y, Input.m_Y);
}
