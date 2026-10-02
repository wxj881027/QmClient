#include <engine/shared/websocket_client.h>

#include <gtest/gtest.h>

TEST(QmWebSocket, ParsesWsAndWssUrls)
{
	SQmWebSocketConnectConfig Config;
	EXPECT_EQ(ParseQmWebSocketUrl("ws://127.0.0.1:9000/qm", Config), "");
	EXPECT_FALSE(Config.m_UseTls);
	EXPECT_EQ(Config.m_Host, "127.0.0.1");
	EXPECT_EQ(Config.m_Port, 9000);
	EXPECT_EQ(Config.m_Path, "/qm");
	EXPECT_EQ(ParseQmWebSocketUrl("wss://qmclient.icu/ws", Config), "");
	EXPECT_TRUE(Config.m_UseTls);
	EXPECT_EQ(Config.m_Port, 443);
}

TEST(QmWebSocket, RejectsInvalidUrlsAndTlsBypass)
{
	SQmWebSocketConnectConfig Config;
	EXPECT_NE(ParseQmWebSocketUrl("http://example.invalid/ws", Config), "");
	EXPECT_NE(ParseQmWebSocketUrl("ws://bad host/ws", Config), "");
	EXPECT_NE(ParseQmWebSocketUrl("ws://example.invalid:0/ws", Config), "");
	EXPECT_NE(ParseQmWebSocketUrl("ws://[::1/ws", Config), "");

	auto pClient = CreateQmWebSocketClient({});
	ASSERT_EQ(ParseQmWebSocketUrl("wss://example.invalid/ws", Config), "");
	Config.m_AllowInsecureTls = true;
	std::string Error;
	EXPECT_FALSE(pClient->Connect(Config, Error));
	EXPECT_FALSE(Error.empty());
}

TEST(QmWebSocket, BackoffIsBounded)
{
	for(int Attempt = 0; Attempt < 12; ++Attempt)
	{
		const int Delay = QmWebSocketBackoffDelayMs(Attempt, 1000, 5000);
		EXPECT_GE(Delay, 1000);
		EXPECT_LE(Delay, 6251);
	}
}
