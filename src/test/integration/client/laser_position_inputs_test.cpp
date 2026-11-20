#include <engine/shared/packer.h>

#include <generated/protocol.h>

#include <game/client/laser_data.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>

#include <limits>

TEST(LaserPositionInputs, SecureLegacyLaserCanKeepAnUnsafeFloatEndpoint)
{
	CNetObj_Laser Input{};
	Input.m_FromX = std::numeric_limits<int>::max();
	CUnpacker Unpacker;
	Unpacker.Reset(&Input, sizeof(Input));
	CNetObjHandler Handler;
	const auto *pSecured = static_cast<const CNetObj_Laser *>(Handler.SecureUnpackObj(NETOBJTYPE_LASER, &Unpacker));
	ASSERT_NE(pSecured, nullptr);
	ASSERT_EQ(pSecured->m_FromX, std::numeric_limits<int>::max());
	const auto Data = ExtractLaserInfo(NETOBJTYPE_LASER, pSecured, nullptr, nullptr);
	EXPECT_FALSE(Data.HasValidPositions());
	EXPECT_EQ(Data.m_TuneZone, 0);
}

TEST(LaserPositionInputs, SecureExtendedDoorCanKeepAnUnsafeToEndpoint)
{
	CNetObj_DDNetLaser Input{};
	Input.m_ToY = std::numeric_limits<int>::max();
	Input.m_Type = LASERTYPE_DOOR;
	Input.m_Owner = -1;
	CUnpacker Unpacker;
	Unpacker.Reset(&Input, sizeof(Input));
	CNetObjHandler Handler;
	const auto *pSecured = static_cast<const CNetObj_DDNetLaser *>(Handler.SecureUnpackObj(NETOBJTYPE_DDNETLASER, &Unpacker));
	ASSERT_NE(pSecured, nullptr);
	ASSERT_EQ(pSecured->m_ToY, std::numeric_limits<int>::max());
	const auto Data = ExtractLaserInfo(NETOBJTYPE_DDNETLASER, pSecured, nullptr, nullptr);
	EXPECT_FALSE(Data.HasValidPositions());
	EXPECT_EQ(Data.m_Type, LASERTYPE_DOOR);
}

TEST(LaserPositionInputs, SecureLowerBoundaryIsKeptExactly)
{
	CNetObj_Laser Input{};
	Input.m_FromX = std::numeric_limits<int>::min();
	Input.m_Y = std::numeric_limits<int>::min();
	CUnpacker Unpacker;
	Unpacker.Reset(&Input, sizeof(Input));
	CNetObjHandler Handler;
	const auto *pSecured = static_cast<const CNetObj_Laser *>(Handler.SecureUnpackObj(NETOBJTYPE_LASER, &Unpacker));
	ASSERT_NE(pSecured, nullptr);
	const auto Data = ExtractLaserInfo(NETOBJTYPE_LASER, pSecured, nullptr, nullptr);
	EXPECT_TRUE(Data.HasValidPositions());
	EXPECT_FLOAT_EQ(Data.m_From.x, static_cast<float>(std::numeric_limits<int>::min()));
	EXPECT_FLOAT_EQ(Data.m_To.y, static_cast<float>(std::numeric_limits<int>::min()));
}

TEST(LaserPositionInputs, DirectExtendedExtractionUsesTheSamePositionContract)
{
	CNetObj_DDNetLaser Input{};
	Input.m_FromY = std::numeric_limits<int>::max();
	const auto Data = ExtractLaserInfoDDNet(&Input, nullptr);
	EXPECT_FALSE(Data.HasValidPositions());
	EXPECT_EQ(Data.m_TuneZone, 0);
}

TEST(LaserPositionInputs, ValidOffMapGeometryKeepsExtendedFlagsAndOwner)
{
	CNetObj_DDNetLaser Input{};
	Input.m_FromX = -100000;
	Input.m_FromY = 100000;
	Input.m_ToX = 100000;
	Input.m_ToY = -100000;
	Input.m_Owner = 3;
	Input.m_Flags = LASERFLAG_NO_PREDICT;
	CUnpacker Unpacker;
	Unpacker.Reset(&Input, sizeof(Input));
	CNetObjHandler Handler;
	const auto *pSecured = static_cast<const CNetObj_DDNetLaser *>(Handler.SecureUnpackObj(NETOBJTYPE_DDNETLASER, &Unpacker));
	ASSERT_NE(pSecured, nullptr);
	const auto Data = ExtractLaserInfo(NETOBJTYPE_DDNETLASER, pSecured, nullptr, nullptr);
	EXPECT_TRUE(Data.HasValidPositions());
	EXPECT_FALSE(Data.m_Predict);
	EXPECT_TRUE(Data.m_ExtraInfo);
	EXPECT_EQ(Data.m_Owner, 3);
	EXPECT_EQ(Data.m_From, vec2(-100000, 100000));
	EXPECT_EQ(Data.m_To, vec2(100000, -100000));
}

TEST(LaserPositionInputs, ValidLegacyEntityExtensionKeepsDoorClassification)
{
	CNetObj_Laser Input{};
	Input.m_FromX = 64;
	Input.m_X = 96;
	CNetObj_EntityEx Extension{};
	Extension.m_SwitchNumber = 4;
	Extension.m_EntityClass = ENTITYCLASS_DOOR;
	CUnpacker Unpacker;
	Unpacker.Reset(&Input, sizeof(Input));
	CNetObjHandler Handler;
	const auto *pSecured = static_cast<const CNetObj_Laser *>(Handler.SecureUnpackObj(NETOBJTYPE_LASER, &Unpacker));
	ASSERT_NE(pSecured, nullptr);
	const auto Data = ExtractLaserInfo(NETOBJTYPE_LASER, pSecured, nullptr, &Extension);
	EXPECT_TRUE(Data.HasValidPositions());
	EXPECT_EQ(Data.m_Type, LASERTYPE_DOOR);
	EXPECT_EQ(Data.m_SwitchNumber, 4);
	EXPECT_TRUE(Data.m_Predict);
}
