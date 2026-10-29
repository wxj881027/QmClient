#include <game/client/components/local_server_launch.h>

#include <android/jni_calls.h>

#include <gtest/gtest.h>
#include <test/support/android_jni_env.h>

TEST(LocalServerLaunchJni, PreparedArgumentsAndAuthReachJavaBeforeInputsExpire)
{
	CLocalServerLaunch Launch;
	{
		const std::string Argument = "sv_name 中文\xf0\x9f\x98\x80";
		const std::string Auth = "auth_add local admin prepared-password";
		const char *apArguments[] = {Argument.c_str()};
		ASSERT_TRUE(Launch.TryPrepare(apArguments, 1, Auth.c_str(), true));
	}
	CAndroidJniEnv Env;
	ASSERT_TRUE(android_jni::Call(&Env, [&] { return Env.Activity(); }, [&](auto Activity) { return android_jni::StartServer(Env, Activity, Launch.Data(), Launch.Count()); }));
	EXPECT_EQ(Env.m_ArrayValues, (std::vector<std::u16string>{u"sv_name 中文\U0001f600", u"auth_add local admin prepared-password"}));
	ASSERT_TRUE(Launch.MarkBinding(100));
	EXPECT_TRUE(Launch.IsStarting());
	EXPECT_EQ(Env.m_Live, 0);
	EXPECT_EQ(Env.m_BadCalls, 0);
}

TEST(LocalServerLaunchJni, FailedJavaConversionDoesNotMutatePreparedArguments)
{
	CLocalServerLaunch Launch;
	ASSERT_TRUE(Launch.TryPrepare(nullptr, 0, "auth_add local admin original", false));
	CAndroidJniEnv Env;
	Env.m_Failure = CAndroidJniEnv::EFailure::STRING;
	EXPECT_FALSE(android_jni::Call(&Env, [&] { return Env.Activity(); }, [&](auto Activity) { return android_jni::StartServer(Env, Activity, Launch.Data(), Launch.Count()); }));
	EXPECT_TRUE(Launch.IsReady());
	Env.m_Failure = CAndroidJniEnv::EFailure::NONE;
	ASSERT_TRUE(android_jni::Call(&Env, [&] { return Env.Activity(); }, [&](auto Activity) { return android_jni::StartServer(Env, Activity, Launch.Data(), Launch.Count()); }));
	ASSERT_EQ(Env.m_ArrayValues.size(), 1u);
	EXPECT_EQ(Env.m_ArrayValues[0], u"auth_add local admin original");
	EXPECT_EQ(Env.m_Live, 0);
	EXPECT_EQ(Env.m_BadCalls, 0);
}
