#include <android/jni_calls.h>

#include <gtest/gtest.h>
#include <test/support/android_jni_env.h>

#include <cstdint>
#include <limits>
#include <new>

namespace
{
	using CEnv = CAndroidJniEnv;
	using EFailure = CEnv::EFailure;
	bool Start(CEnv &Env, const char *const *ppArgs = nullptr, size_t Count = 0)
	{
		return android_jni::Call(&Env, [&] { return Env.Activity(); }, [&](auto Activity) { return android_jni::StartServer(Env, Activity, ppArgs, Count); });
	}
	bool Execute(CEnv &Env, const char *pText)
	{
		return android_jni::Call(&Env, [&] { return Env.Activity(); }, [&](auto Activity) { return android_jni::ExecuteCommand(Env, Activity, pText); });
	}
	bool Running(CEnv &Env)
	{
		return android_jni::Call(&Env, [&] { return Env.Activity(); }, [&](auto Activity) { return android_jni::IsRunning(Env, Activity); });
	}
	bool Media(CEnv &Env, const char *pPath, const char *const *ppExcluded = nullptr, size_t Count = 0)
	{
		return android_jni::Call(&Env, [&] { return Env.Activity(); }, [&](auto Activity) { return android_jni::ShowMedia(Env, Activity, pPath, true, ppExcluded, Count); });
	}
	void ExpectClean(const CEnv &Env)
	{
		EXPECT_EQ(Env.m_Live, 0);
		EXPECT_EQ(Env.m_Depth, 0);
		EXPECT_EQ(Env.m_BadCalls, 0);
		EXPECT_FALSE(Env.m_Pending);
	}
}

TEST(AndroidJniCalls, NullEnvironmentDoesNotAcquireActivity)
{
	EXPECT_FALSE(android_jni::Call(static_cast<CEnv *>(nullptr), []() -> CEnv::TObject { ADD_FAILURE(); return nullptr; }, [](auto) { ADD_FAILURE(); return true; }));
}

TEST(AndroidJniCalls, PendingExceptionIsClearedWithoutAnotherJavaOperation)
{
	CEnv Env;
	Env.m_Pending = true;
	EXPECT_FALSE(Start(Env));
	EXPECT_EQ(Env.m_Pushes, 0);
	EXPECT_EQ(Env.m_Pops, 0);
	EXPECT_EQ(Env.m_Clears, 1);
	ExpectClean(Env);
}

TEST(AndroidJniCalls, FailedFrameCreationDoesNotPopUncreatedFrame)
{
	CEnv Env;
	Env.m_Failure = EFailure::PUSH;
	EXPECT_FALSE(Start(Env));
	EXPECT_EQ(Env.m_Pushes, 1);
	EXPECT_EQ(Env.m_Pops, 0);
	EXPECT_EQ(Env.m_Clears, 1);
	ExpectClean(Env);
}

TEST(AndroidJniCalls, MissingActivityReleasesEstablishedFrame)
{
	CEnv Env;
	Env.m_Failure = EFailure::ACTIVITY;
	Env.m_FailWithException = false;
	EXPECT_FALSE(Start(Env));
	EXPECT_EQ(Env.m_Pops, 1);
	EXPECT_EQ(Env.m_VoidCalls, 0);
	ExpectClean(Env);
}

TEST(AndroidJniCalls, StartPublishesCompleteUnicodeArgumentsAndReleasesReferences)
{
	CEnv Env;
	const char *apArguments[] = {"sv_name 测试", "emoji \xf0\x9f\x98\x80"};
	EXPECT_TRUE(Start(Env, apArguments, 2));
	ASSERT_EQ(Env.m_ArrayValues.size(), 2u);
	EXPECT_EQ(Env.m_ArrayValues[0], u"sv_name 测试");
	EXPECT_EQ(Env.m_ArrayValues[1], u"emoji \U0001f600");
	EXPECT_STREQ(Env.m_pMethod, "startServer");
	EXPECT_STREQ(Env.m_pSignature, "([Ljava/lang/String;)V");
	EXPECT_EQ(Env.m_VoidCalls, 1);
	EXPECT_EQ(Env.m_Pops, 1);
	ExpectClean(Env);
}

TEST(AndroidJniCalls, StartStopsAtEveryFailedJavaPreparationOrCall)
{
	for(const auto Failure : {EFailure::CLASS, EFailure::STRING_CLASS, EFailure::ARRAY, EFailure::STRING, EFailure::ELEMENT, EFailure::METHOD, EFailure::VOID_CALL})
	{
		SCOPED_TRACE(static_cast<int>(Failure));
		CEnv Env;
		Env.m_Failure = Failure;
		const char *apArguments[] = {"sv_name test"};
		EXPECT_FALSE(Start(Env, apArguments, 1));
		EXPECT_EQ(Env.m_Pops, 1);
		EXPECT_EQ(Env.m_Clears, 1);
		ExpectClean(Env);
	}
}

TEST(AndroidJniCalls, EmptyArgumentArrayRemainsValid)
{
	CEnv Env;
	EXPECT_TRUE(Start(Env));
	EXPECT_TRUE(Env.m_ArrayValues.empty());
	EXPECT_EQ(Env.m_VoidCalls, 1);
	ExpectClean(Env);
}

TEST(AndroidJniCalls, InvalidArgumentArrayOrElementDoesNotInvokeJavaMethod)
{
	CEnv Env;
	EXPECT_FALSE(Start(Env, nullptr, 1));
	const char *apArguments[] = {nullptr};
	EXPECT_FALSE(Start(Env, apArguments, 1));
	EXPECT_EQ(Env.m_VoidCalls, 0);
	ExpectClean(Env);
}

TEST(AndroidJniCalls, ExcessArgumentCountIsRejectedBeforeArrayAllocation)
{
	CEnv Env;
	const char *apArguments[] = {"a"};
	const size_t Count = static_cast<size_t>(std::numeric_limits<std::int32_t>::max()) + 1;
	EXPECT_FALSE(Start(Env, apArguments, Count));
	EXPECT_EQ(Env.m_Arrays, 0);
	ExpectClean(Env);
}

TEST(AndroidJniCalls, LargeArgumentListKeepsLocalReferenceCountBounded)
{
	CEnv Env;
	const std::vector<const char *> vArguments(100, "argument");
	EXPECT_TRUE(Start(Env, vArguments.data(), vArguments.size()));
	EXPECT_EQ(Env.m_Elements, 100);
	EXPECT_LE(Env.m_PeakLive, 8);
	ExpectClean(Env);
}

TEST(AndroidJniCalls, JavaStartExceptionReturnsFailureAndNextAttemptCanRecover)
{
	CEnv Env;
	Env.m_Failure = EFailure::VOID_CALL;
	EXPECT_FALSE(Start(Env));
	ExpectClean(Env);
	Env.m_Failure = EFailure::NONE;
	EXPECT_TRUE(Start(Env));
	ExpectClean(Env);
}

TEST(AndroidJniCalls, ExecuteUsesUnicodeStringAndCorrectMethod)
{
	CEnv Env;
	EXPECT_TRUE(Execute(Env, "echo \xf0\x9f\x98\x80"));
	EXPECT_EQ(Env.m_LastString, u"echo \U0001f600");
	EXPECT_STREQ(Env.m_pMethod, "executeCommand");
	EXPECT_STREQ(Env.m_pSignature, "(Ljava/lang/String;)V");
	ExpectClean(Env);
}

TEST(AndroidJniCalls, ExecuteFailureDoesNotLeavePendingException)
{
	for(const auto Failure : {EFailure::CLASS, EFailure::STRING, EFailure::METHOD, EFailure::VOID_CALL})
	{
		SCOPED_TRACE(static_cast<int>(Failure));
		CEnv Env;
		Env.m_Failure = Failure;
		EXPECT_FALSE(Execute(Env, "shutdown"));
		ExpectClean(Env);
	}
}

TEST(AndroidJniCalls, EmptyCommandCreatesNonNullEmptyJavaString)
{
	CEnv Env;
	EXPECT_TRUE(Execute(Env, ""));
	EXPECT_TRUE(Env.m_LastString.empty());
	ExpectClean(Env);
}

TEST(AndroidJniCalls, NullCommandDoesNotInvokeJava)
{
	CEnv Env;
	EXPECT_FALSE(Execute(Env, nullptr));
	EXPECT_EQ(Env.m_VoidCalls, 0);
	ExpectClean(Env);
}

TEST(AndroidJniCalls, RunningQueryReturnsJavaTrueOrFalse)
{
	CEnv Env;
	EXPECT_TRUE(Running(Env));
	Env.m_Running = false;
	EXPECT_FALSE(Running(Env));
	EXPECT_EQ(Env.m_BooleanCalls, 2);
	EXPECT_STREQ(Env.m_pSignature, "()Z");
	ExpectClean(Env);
}

TEST(AndroidJniCalls, RunningQueryDiscardsValueReturnedWithException)
{
	CEnv Env;
	Env.m_Failure = EFailure::BOOLEAN_CALL;
	Env.m_Running = true;
	EXPECT_FALSE(Running(Env));
	ExpectClean(Env);
}

TEST(AndroidJniCalls, RunningLookupFailureStopsBeforeBooleanCall)
{
	for(const auto Failure : {EFailure::CLASS, EFailure::METHOD})
	{
		CEnv Env;
		Env.m_Failure = Failure;
		EXPECT_FALSE(Running(Env));
		EXPECT_EQ(Env.m_BooleanCalls, 0);
		ExpectClean(Env);
	}
}

TEST(AndroidJniCalls, MediaPublishesDirectoryAndCompleteExclusionArray)
{
	CEnv Env;
	const char *apExcluded[] = {"/videos/recording.mp4", "/videos/\xf0\x9f\x98\x80.mp4"};
	EXPECT_TRUE(Media(Env, "/videos", apExcluded, 2));
	ASSERT_EQ(Env.m_ArrayValues.size(), 2u);
	EXPECT_EQ(Env.m_ArrayValues[1], u"/videos/\U0001f600.mp4");
	EXPECT_STREQ(Env.m_pMethod, "showMediaFiles");
	EXPECT_STREQ(Env.m_pSignature, "(Ljava/lang/String;Z[Ljava/lang/String;)V");
	ExpectClean(Env);
}

TEST(AndroidJniCalls, MediaFailureReleasesAllPreviouslyCreatedReferences)
{
	for(const auto Failure : {EFailure::CLASS, EFailure::METHOD, EFailure::STRING_CLASS, EFailure::STRING, EFailure::ARRAY, EFailure::ELEMENT, EFailure::VOID_CALL})
	{
		SCOPED_TRACE(static_cast<int>(Failure));
		CEnv Env;
		Env.m_Failure = Failure;
		const char *apExcluded[] = {"file"};
		EXPECT_FALSE(Media(Env, "/videos", apExcluded, 1));
		ExpectClean(Env);
	}
}

TEST(AndroidJniCalls, MediaCppPreparationExceptionStillPopsFrame)
{
	CEnv Env;
	Env.m_ThrowAtString = 2;
	const char *apExcluded[] = {"file"};
	EXPECT_FALSE(Media(Env, "/videos", apExcluded, 1));
	EXPECT_EQ(Env.m_VoidCalls, 0);
	EXPECT_EQ(Env.m_Pops, 1);
	ExpectClean(Env);
}

TEST(AndroidJniCalls, InvalidMediaInputsDoNotInvokeJavaMethod)
{
	CEnv Env;
	EXPECT_FALSE(Media(Env, nullptr));
	EXPECT_FALSE(Media(Env, "/videos", nullptr, 1));
	const char *apExcluded[] = {nullptr};
	EXPECT_FALSE(Media(Env, "/videos", apExcluded, 1));
	EXPECT_FALSE(Media(Env, "/videos", apExcluded, static_cast<size_t>(std::numeric_limits<std::int32_t>::max()) + 1));
	EXPECT_EQ(Env.m_VoidCalls, 0);
	ExpectClean(Env);
}

TEST(AndroidJniCalls, RepeatedQueriesDoNotAccumulateLocalReferences)
{
	CEnv Env;
	for(int i = 0; i < 100; ++i)
		EXPECT_TRUE(Running(Env));
	EXPECT_EQ(Env.m_Pops, 100);
	EXPECT_EQ(Env.m_Pushes, 100);
	ExpectClean(Env);
}
