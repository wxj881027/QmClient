#include <engine/shared/packer.h>

#include <generated/protocol.h>

#include <game/client/components/qmclient/item_render_math.h>
#include <game/client/laser_data.h>
#include <game/client/projectile_data.h>

#include <gtest/gtest.h>

#include <limits>

TEST(ItemRenderInputs, SecureLaserKeepsExtremeStartTickAndRenderingWidensDelta)
{
	CNetObj_Laser Input{};
	Input.m_StartTick = std::numeric_limits<int>::min();
	CUnpacker Unpacker;
	Unpacker.Reset(&Input, sizeof(Input));
	CNetObjHandler Handler;
	const auto *pSecured = static_cast<const CNetObj_Laser *>(Handler.SecureUnpackObj(NETOBJTYPE_LASER, &Unpacker));
	ASSERT_NE(pSecured, nullptr);
	ASSERT_EQ(pSecured->m_StartTick, std::numeric_limits<int>::min());
	const auto Laser = ExtractLaserInfo(NETOBJTYPE_LASER, pSecured, nullptr, nullptr);
	EXPECT_FLOAT_EQ(QmItemTickDelta(std::numeric_limits<int>::max(), Laser.m_StartTick), 4294967296.0f);
}

TEST(ItemRenderInputs, SecureProjectileBoundsOwnerWhileKeepingExtremeTick)
{
	CNetObj_DDNetProjectile Input{};
	Input.m_StartTick = std::numeric_limits<int>::min();
	Input.m_Owner = std::numeric_limits<int>::max();
	CUnpacker Unpacker;
	Unpacker.Reset(&Input, sizeof(Input));
	CNetObjHandler Handler;
	const auto *pSecured = static_cast<const CNetObj_DDNetProjectile *>(Handler.SecureUnpackObj(NETOBJTYPE_DDNETPROJECTILE, &Unpacker));
	ASSERT_NE(pSecured, nullptr);
	const auto Projectile = ExtractProjectileInfo(NETOBJTYPE_DDNETPROJECTILE, pSecured, nullptr, nullptr);
	EXPECT_EQ(Projectile.m_Owner, MAX_CLIENTS - 1);
	ASSERT_EQ(Projectile.m_StartTick, std::numeric_limits<int>::min());
	EXPECT_FLOAT_EQ(QmItemTickDelta(std::numeric_limits<int>::max(), Projectile.m_StartTick, -1), 4294967296.0f);
}

TEST(ItemRenderInputs, SecureProjectileCanProduceTrailCountersBeyondIntRange)
{
	CNetObj_Projectile Input{};
	Input.m_StartTick = std::numeric_limits<int>::min();
	CUnpacker Unpacker;
	Unpacker.Reset(&Input, sizeof(Input));
	CNetObjHandler Handler;
	const auto *pSecured = static_cast<const CNetObj_Projectile *>(Handler.SecureUnpackObj(NETOBJTYPE_PROJECTILE, &Unpacker));
	ASSERT_NE(pSecured, nullptr);
	const auto Projectile = ExtractProjectileInfo(NETOBJTYPE_PROJECTILE, pSecured, nullptr, nullptr);
	const float Time = QmItemTickDelta(0, Projectile.m_StartTick) / 50.0f;
	const auto Range = QmItemTrailRange(Time, Time + 32.0f, 0.01f);
	EXPECT_GT(Range.m_First, std::numeric_limits<int>::max());
	EXPECT_GT(Range.m_End, Range.m_First);
	int Calls = 0;
	QmForEachItemTrailSample(Range, [&](int64_t) { ++Calls; return false; });
	EXPECT_EQ(Calls, 1);
}
