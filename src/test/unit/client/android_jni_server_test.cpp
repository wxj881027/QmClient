#include <android/jni_server.h>

#include <gtest/gtest.h>
#include <test/support/android_jni_input.h>

#include <limits>
#include <new>

namespace
{
	using EFailure = CAndroidJniEnv::EFailure;
	class CAndroidJniServer : public ::testing::Test
	{
	protected:
		CAndroidJniInput m_Input;
		int m_Changes = 0, m_Runs = 0;
		bool m_ChangeSucceeds = true;
		std::string m_Directory;
		std::vector<std::string> m_vArguments;

		void ExpectReleased()
		{
			EXPECT_EQ(m_Input.m_Env.m_Depth, 1);
			EXPECT_EQ(m_Input.m_Env.m_Live, 2);
			EXPECT_EQ(m_Input.m_Env.m_Borrows, 0);
			EXPECT_EQ(m_Input.m_Env.m_BadCalls, 0);
			EXPECT_FALSE(m_Input.m_Env.m_Pending);
		}
		int Run()
		{
			return android_jni::RunServer(&m_Input.m_Env, m_Input.m_Directory, m_Input.m_Arguments, "DDNet-Server", [&](const char *pDirectory) {
					ExpectReleased();
					++m_Changes;
					m_Directory = pDirectory;
					return m_ChangeSucceeds; }, [&](int Count, const char **ppArguments) {
					ExpectReleased();
					++m_Runs;
					EXPECT_EQ(m_Changes, 1);
					for(int i = 0; i < Count; ++i)
						m_vArguments.emplace_back(ppArguments[i]);
					return 17; });
		}
		void ExpectPreparationFailure()
		{
			EXPECT_EQ(Run(), android_jni::PREPARATION_ERROR);
			EXPECT_EQ(m_Changes, 0);
			EXPECT_EQ(m_Runs, 0);
			ExpectReleased();
		}
	};
}

TEST_F(CAndroidJniServer, CompleteArgumentsAndProgramPathReachNativeMain)
{
	m_Input.m_Directory->m_Text = u"/目录/\U0001f600";
	m_Input.m_Env.m_ArrayValues = {u"sv_name \U0001f600", u"", u"sv_port 8303"};
	EXPECT_EQ(Run(), 17);
	EXPECT_EQ(m_Directory, "/目录/\xf0\x9f\x98\x80");
	EXPECT_EQ(m_vArguments, (std::vector<std::string>{m_Directory + "/DDNet-Server", "sv_name \xf0\x9f\x98\x80", "", "sv_port 8303"}));
	ExpectReleased();
}

TEST_F(CAndroidJniServer, EmptyArgumentArrayStillSuppliesProgramName)
{
	EXPECT_EQ(Run(), 17);
	EXPECT_EQ(m_vArguments, (std::vector<std::string>{"/data/DDNet-Server"}));
}

TEST_F(CAndroidJniServer, NullEnvironmentDoesNotChangeDirectoryOrRun)
{
	EXPECT_EQ(android_jni::RunServer(static_cast<CAndroidJniEnv *>(nullptr), m_Input.m_Directory, m_Input.m_Arguments, "server", [](const char *) { ADD_FAILURE(); return true; }, [](int, const char **) { ADD_FAILURE(); return 0; }), android_jni::PREPARATION_ERROR);
}

TEST_F(CAndroidJniServer, NullInputsStopBeforeFilesystemSideEffects)
{
	auto Directory = m_Input.m_Directory;
	m_Input.m_Directory = nullptr;
	ExpectPreparationFailure();
	m_Input.m_Directory = Directory;
	m_Input.m_Arguments = nullptr;
	ExpectPreparationFailure();
}

TEST_F(CAndroidJniServer, PendingJavaExceptionStopsAndIsCleared)
{
	m_Input.m_Env.m_Pending = true;
	ExpectPreparationFailure();
	EXPECT_EQ(m_Input.m_Env.m_Clears, 1);
}

TEST_F(CAndroidJniServer, FailedFrameDoesNotPopJavaCallersFrame)
{
	m_Input.m_Env.m_Failure = EFailure::PUSH;
	ExpectPreparationFailure();
	EXPECT_EQ(m_Input.m_Env.m_Pops, 0);
}

TEST_F(CAndroidJniServer, JavaPreparationExceptionsStopBeforeNextStage)
{
	for(const auto Stage : {EFailure::STRING_LENGTH, EFailure::CHARS, EFailure::ARRAY_LENGTH, EFailure::GET_ELEMENT})
	{
		SCOPED_TRACE(static_cast<int>(Stage));
		m_Input.m_Env.m_ArrayValues = {u"sv_name test"};
		m_Input.m_Env.m_Failure = Stage;
		ExpectPreparationFailure();
	}
}

TEST_F(CAndroidJniServer, CharacterBorrowFailureWithoutExceptionIsRejected)
{
	m_Input.m_Env.m_Failure = EFailure::CHARS;
	m_Input.m_Env.m_FailWithException = false;
	ExpectPreparationFailure();
}

TEST_F(CAndroidJniServer, ReturnedBorrowWithPendingExceptionIsReleased)
{
	m_Input.m_Env.m_CharsWithException = true;
	ExpectPreparationFailure();
	EXPECT_EQ(m_Input.m_Env.m_Releases, 1);
}

TEST_F(CAndroidJniServer, NegativeStringLengthIsRejected)
{
	m_Input.m_Env.m_StringLengthOverride = -1;
	ExpectPreparationFailure();
}

TEST_F(CAndroidJniServer, ArrayCountCannotOverflowArgcIncludingProgramName)
{
	for(const auto Count : {-1, std::numeric_limits<std::int32_t>::max()})
	{
		SCOPED_TRACE(Count);
		m_Input.m_Env.m_ArrayLengthOverride = Count;
		ExpectPreparationFailure();
	}
}

TEST_F(CAndroidJniServer, NullArrayElementRejectsEntireLaunch)
{
	m_Input.m_Env.m_ArrayValues = {u"sv_name valid", u"ignored"};
	m_Input.m_Env.m_NullElement = 1;
	ExpectPreparationFailure();
}

TEST_F(CAndroidJniServer, EmbeddedNulInArgumentDoesNotRunTruncatedCommand)
{
	m_Input.m_Env.m_ArrayValues = {std::u16string{u'a', 0, u'b'}};
	ExpectPreparationFailure();
}

TEST_F(CAndroidJniServer, CppPreparationExceptionReleasesEarlierReadsAndFrame)
{
	m_Input.m_Env.m_ArrayValues = {u"valid", u"fails"};
	m_Input.m_Env.m_ThrowAtReadString = 3;
	ExpectPreparationFailure();
	EXPECT_EQ(m_Input.m_Env.m_Releases, 2);
}

TEST_F(CAndroidJniServer, DirectoryFailureKeepsOriginalExitCodeWithoutRunning)
{
	m_ChangeSucceeds = false;
	EXPECT_EQ(Run(), -1001);
	EXPECT_EQ(m_Changes, 1);
	EXPECT_EQ(m_Runs, 0);
	ExpectReleased();
}

TEST_F(CAndroidJniServer, NativeExceptionDoesNotEscapeJniBoundary)
{
	EXPECT_EQ(android_jni::RunServer(&m_Input.m_Env, m_Input.m_Directory, m_Input.m_Arguments, "server", [](const char *) { return true; }, [](int, const char **) -> int { throw std::bad_alloc(); }), android_jni::PREPARATION_ERROR);
	ExpectReleased();
}

TEST_F(CAndroidJniServer, LongArgumentListUsesBoundedLocalReferences)
{
	m_Input.m_Env.m_ArrayValues.assign(100, u"sv_name repeated");
	EXPECT_EQ(Run(), 17);
	EXPECT_EQ(m_vArguments.size(), 101u);
	EXPECT_LE(m_Input.m_Env.m_PeakLive, 3);
	ExpectReleased();
}

TEST_F(CAndroidJniServer, FailedPreparationCanRetryWithOriginalJavaReferences)
{
	m_Input.m_Env.m_Failure = EFailure::CHARS;
	ExpectPreparationFailure();
	m_Input.m_Env.m_Failure = EFailure::NONE;
	EXPECT_EQ(Run(), 17);
	ExpectReleased();
}

TEST_F(CAndroidJniServer, CommandIsSubmittedAfterBorrowAndFrameRelease)
{
	m_Input.m_Directory->m_Text = u"say \U0001f600";
	std::vector<std::string> vQueue{"older"};
	EXPECT_TRUE(android_jni::ReceiveCommand(&m_Input.m_Env, m_Input.m_Directory, [&](std::string Text) {
		ExpectReleased();
		vQueue.push_back(std::move(Text));
		return true;
	}));
	EXPECT_EQ(vQueue, (std::vector<std::string>{"older", "say \xf0\x9f\x98\x80"}));
}

TEST_F(CAndroidJniServer, InvalidCommandPreservesQueuedCommands)
{
	m_Input.m_Directory->m_Text.assign({u'a', 0, u'b'});
	std::vector<std::string> vQueue{"older"};
	EXPECT_FALSE(android_jni::ReceiveCommand(&m_Input.m_Env, m_Input.m_Directory, [&](std::string Text) { vQueue.push_back(std::move(Text)); return true; }));
	EXPECT_EQ(vQueue, (std::vector<std::string>{"older"}));
	ExpectReleased();
}

TEST_F(CAndroidJniServer, CommandAdmissionExceptionIsContainedAfterRelease)
{
	EXPECT_FALSE(android_jni::ReceiveCommand(&m_Input.m_Env, m_Input.m_Directory, [&](std::string) -> bool {
		ExpectReleased();
		throw std::bad_alloc();
	}));
	ExpectReleased();
}

TEST_F(CAndroidJniServer, CommandJavaFailureStopsBeforeAdmission)
{
	m_Input.m_Env.m_Failure = EFailure::CHARS;
	EXPECT_FALSE(android_jni::ReceiveCommand(&m_Input.m_Env, m_Input.m_Directory, [](std::string) { ADD_FAILURE(); return true; }));
	ExpectReleased();
}

TEST_F(CAndroidJniServer, EmptyCommandStillReachesExistingQueuePolicy)
{
	m_Input.m_Directory->m_Text.clear();
	bool Called = false;
	EXPECT_FALSE(android_jni::ReceiveCommand(&m_Input.m_Env, m_Input.m_Directory, [&](std::string Text) {
		EXPECT_TRUE(Text.empty());
		Called = true;
		return false;
	}));
	EXPECT_TRUE(Called);
	ExpectReleased();
}
