#include <android/jni_calls.h>
#include <android/jni_server.h>

#include <gtest/gtest.h>
#include <test/support/android_jni_input.h>

// 两侧生产编码/workflow协作；fake仅承载Java字符串/数组，不运行JVM、IPC或服务进程。
TEST(AndroidJniRoundtrip, ClientServerArgumentsReachNativeMainAsOriginalUtf8)
{
	CAndroidJniEnv Client;
	const char *apArguments[] = {"sv_name 中文\xf0\x9f\x98\x80", "password \xf4\x8f\xbf\xbf", ""};
	ASSERT_TRUE(android_jni::Call(&Client, [&] { return Client.Activity(); }, [&](auto Activity) { return android_jni::StartServer(Client, Activity, apArguments, 3); }));
	CAndroidJniInput Server;
	Server.m_Env.m_ArrayValues = Client.m_ArrayValues;
	bool Ran = false;
	EXPECT_EQ(android_jni::RunServer(&Server.m_Env, Server.m_Directory, Server.m_Arguments, "server", [](const char *) { return true; }, [&](int Count, const char **ppArguments) {
			Ran = true;
			EXPECT_EQ(Count, 4);
			if(Count == 4)
				for(int i = 0; i < 3; ++i)
					EXPECT_STREQ(ppArguments[i + 1], apArguments[i]);
			return 0; }), 0);
	EXPECT_TRUE(Ran);
	EXPECT_EQ(Client.m_Live, 0);
	EXPECT_EQ(Client.m_BadCalls, 0);
	EXPECT_EQ(Server.m_Env.m_Borrows, 0);
	EXPECT_EQ(Server.m_Env.m_BadCalls, 0);
}

TEST(AndroidJniRoundtrip, ClientCommandReachesNativeQueueAsOriginalUtf8)
{
	CAndroidJniEnv Client;
	const char *pCommand = "say 中文\xf0\x9f\x98\x80";
	ASSERT_TRUE(android_jni::Call(&Client, [&] { return Client.Activity(); }, [&](auto Activity) { return android_jni::ExecuteCommand(Client, Activity, pCommand); }));
	CAndroidJniInput Server;
	Server.m_Directory->m_Text = Client.m_LastString;
	std::vector<std::string> vQueue{"earlier"};
	EXPECT_TRUE(android_jni::ReceiveCommand(&Server.m_Env, Server.m_Directory, [&](std::string Text) {
		vQueue.push_back(std::move(Text));
		return true;
	}));
	EXPECT_EQ(vQueue, (std::vector<std::string>{"earlier", pCommand}));
	EXPECT_EQ(Client.m_BadCalls, 0);
	EXPECT_EQ(Server.m_Env.m_BadCalls, 0);
}

TEST(AndroidJniRoundtrip, InvalidClientBytesRetainReplacementPolicyThroughReceiver)
{
	CAndroidJniEnv Client;
	ASSERT_TRUE(android_jni::Call(&Client, [&] { return Client.Activity(); }, [&](auto Activity) { return android_jni::ExecuteCommand(Client, Activity, "say \xff"); }));
	CAndroidJniInput Server;
	Server.m_Directory->m_Text = Client.m_LastString;
	std::string Received;
	ASSERT_TRUE(android_jni::ReceiveCommand(&Server.m_Env, Server.m_Directory, [&](std::string Text) { Received = std::move(Text); return true; }));
	EXPECT_EQ(Received, "say \xef\xbf\xbd");
}
