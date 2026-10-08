#include <game/client/components/qmclient/decorative_throw_policy.h>
#include <game/client/components/qmclient/emoticon_projectile.h>
#include <game/client/components/qmclient/qm_realtime.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <string>

namespace
{
	std::string ThrowMessage(const char *pType, const char *pOrigin = "{\"x\":100,\"y\":-20}", const char *pDirection = "{\"x\":0.6,\"y\":-0.8}")
	{
		return std::string("{\"type\":\"decorative_throw\",\"data\":{\"client_id\":\"remote\",\"player_id\":3,\"player_name\":\"tee\",\"server_address\":\"one:8303\",\"projectile\":\"") +
		       pType + "\",\"origin\":" + pOrigin + ",\"direction\":" + pDirection + "}}";
	}

	QmEmoticon::CAlphaMask OpaqueMask()
	{
		std::array<unsigned char, 16> aPixels;
		aPixels.fill(255);
		QmEmoticon::CAlphaMask Mask;
		Mask.Build(aPixels.data(), 2, 2);
		return Mask;
	}
}

TEST(QmDecorativeThrow, ParsesEverySupportedSpriteAndContinuousDirection)
{
	for(int Type = 0; Type < QmDecorativeThrow::COUNT; ++Type)
	{
		SCOPED_TRACE(Type);
		const auto Body = ThrowMessage(QmDecorativeThrow::NAMES[Type]);
		SQmRealtimeMessage Message;
		ASSERT_TRUE(ParseQmRealtimeMessage(Body.data(), Body.size(), Message));
		EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::DECORATIVE_THROW);
		ASSERT_TRUE(Message.m_HasDecorativeThrow);
		EXPECT_EQ(Message.m_ThrowType, Type);
		EXPECT_EQ(Message.m_PlayerId, 3);
		EXPECT_EQ(Message.m_ThrowPlayerName, "tee");
		EXPECT_FLOAT_EQ(Message.m_ThrowDirection.x, 0.6f);
		EXPECT_FLOAT_EQ(Message.m_ThrowDirection.y, -0.8f);
		EXPECT_FALSE(Message.m_HasEmoticon);
	}
}

TEST(QmDecorativeThrow, UnknownTypeOrUnboundedGeometryCannotBecomeAnEvent)
{
	const std::string aBodies[] = {ThrowMessage("unknown"), ThrowMessage("egg", "{\"x\":1000000000,\"y\":0}"),
		ThrowMessage("tomato", "{\"x\":0,\"y\":0}", "{\"x\":0,\"y\":0}"),
		ThrowMessage("grass", "{\"x\":\"zero\",\"y\":0}")};
	for(const auto &Body : aBodies)
	{
		SCOPED_TRACE(Body);
		SQmRealtimeMessage Message;
		ASSERT_TRUE(ParseQmRealtimeMessage(Body.data(), Body.size(), Message));
		EXPECT_FALSE(Message.m_HasDecorativeThrow);
	}
	EXPECT_FALSE(QmDecorativeThrow::ValidGeometry(vec2(std::numeric_limits<float>::infinity(), 0), vec2(1, 0)));
}

TEST(QmDecorativeThrow, RemoteDisplayRequiresCurrentServerPlayerAndVisibility)
{
	const auto Show = [](bool Enabled, bool Emotes, bool Launch, bool Ignored, const char *pServer, const char *pName, vec2 Origin) {
		return QmDecorativeThrow::ShouldShowRemote(Enabled, Emotes, Launch, Ignored, pServer, "one:8303", pName, "tee", Origin, vec2(100, 100));
	};
	EXPECT_TRUE(Show(true, true, true, false, "one:8303", "tee", vec2(100, 80)));
	EXPECT_FALSE(Show(false, true, true, false, "one:8303", "tee", vec2(100, 80)));
	EXPECT_FALSE(Show(true, false, true, false, "one:8303", "tee", vec2(100, 80)));
	EXPECT_FALSE(Show(true, true, false, false, "one:8303", "tee", vec2(100, 80)));
	EXPECT_FALSE(Show(true, true, true, true, "one:8303", "tee", vec2(100, 80)));
	EXPECT_FALSE(Show(true, true, true, false, "two:8303", "tee", vec2(100, 80)));
	EXPECT_FALSE(Show(true, true, true, false, "one:8303", "replaced player", vec2(100, 80)));
	EXPECT_FALSE(Show(true, true, true, false, "one:8303", "tee", vec2(500, 80)));
}

TEST(QmDecorativeThrow, WallImpactStopsOnlyTheDecorativeInstance)
{
	const auto Mask = OpaqueMask();
	const auto Wall = [](int X, int) { return X >= 1; };
	CEmoticonProjectile Decorative;
	Decorative.Init(vec2(16, 16), vec2(1000, 0), 0, 0.1f);
	Decorative.m_AngVel = 0;
	Decorative.m_StopOnCollision = true;
	Decorative.Update(0.04f, Mask, Wall);
	EXPECT_FALSE(Decorative.m_Active);
	EXPECT_TRUE(Decorative.m_Impacted);
	EXPECT_LT(Decorative.m_Pos.x, 32.0f);
	CEmoticonProjectile Emoticon;
	Emoticon.Init(vec2(16, 16), vec2(1000, 0), 0, 0.1f);
	Emoticon.m_AngVel = 0;
	Emoticon.Update(0.04f, Mask, Wall);
	EXPECT_TRUE(Emoticon.m_Active);
	EXPECT_FALSE(Emoticon.m_Impacted);
	EXPECT_LT(Emoticon.m_Vel.x, 0.0f);
}

TEST(QmDecorativeThrow, PlayerImpactStopsWithoutChangingThePlayerBox)
{
	const auto Mask = OpaqueMask();
	const QmEmoticon::SPlayerBox Box{2, vec2(32, 16), 4};
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(16, 16), vec2(1000, 0), 0, 0.1f, 1);
	Projectile.m_AngVel = 0;
	Projectile.m_StopOnCollision = true;
	Projectile.Update(0.04f, Mask, [](int, int) { return false; }, &Box, 1);
	EXPECT_FALSE(Projectile.m_Active);
	EXPECT_TRUE(Projectile.m_Impacted);
	EXPECT_EQ(Box.m_Pos, vec2(32, 16));
}

TEST(QmDecorativeThrow, ExpiryDoesNotEmitImpactAndReusedSlotClearsImpactPolicy)
{
	const auto Mask = OpaqueMask();
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(0, 0), vec2(1, 0), 0, 0.1f, 1, 1);
	Projectile.m_StopOnCollision = true;
	Projectile.Update(1.1f, Mask, [](int, int) { return false; });
	EXPECT_FALSE(Projectile.m_Active);
	EXPECT_FALSE(Projectile.m_Impacted);
	Projectile.m_Impacted = true;
	Projectile.Init(vec2(0, 0), vec2(1, 0), 0);
	EXPECT_TRUE(Projectile.m_Active);
	EXPECT_FALSE(Projectile.m_StopOnCollision);
	EXPECT_FALSE(Projectile.m_Impacted);
}
