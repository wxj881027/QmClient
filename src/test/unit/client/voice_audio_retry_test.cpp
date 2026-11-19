#include <game/client/components/qmclient/voice/voice_audio_retry.h>

#include <gtest/gtest.h>

TEST(VoiceAudioRetry, ReadyAudioDoesNotInitializeWithoutARequest)
{
	CVoiceAudioRetry Retry;
	int Calls = 0;
	Retry.Update(false, false, 100, 100, [&] { ++Calls; return true; });
	EXPECT_EQ(Calls, 0);
}

TEST(VoiceAudioRetry, ExplicitRefreshInitializesOnce)
{
	CVoiceAudioRetry Retry;
	int Calls = 0;
	const auto Initialize = [&] { ++Calls; return true; };
	Retry.Update(true, false, 100, 100, Initialize);
	Retry.Update(false, false, 101, 100, Initialize);
	EXPECT_EQ(Calls, 1);
}

TEST(VoiceAudioRetry, InitializationFailureRetriesAtDeadlineWithoutDeviceFailureFlags)
{
	CVoiceAudioRetry Retry;
	int Calls = 0;
	const auto Initialize = [&] { ++Calls; return false; };
	Retry.Update(true, false, 100, 100, Initialize);
	Retry.Update(false, false, 199, 100, Initialize);
	EXPECT_EQ(Calls, 1);
	Retry.Update(false, false, 200, 100, Initialize);
	EXPECT_EQ(Calls, 2);
}

TEST(VoiceAudioRetry, RepeatedFailureKeepsAFullIntervalBetweenAttempts)
{
	CVoiceAudioRetry Retry;
	int Calls = 0;
	const auto Initialize = [&] { ++Calls; return false; };
	Retry.Update(true, false, 0, 100, Initialize);
	Retry.Update(false, false, 100, 100, Initialize);
	Retry.Update(false, false, 101, 100, Initialize);
	EXPECT_EQ(Calls, 2);
	Retry.Update(false, false, 200, 100, Initialize);
	EXPECT_EQ(Calls, 3);
}

TEST(VoiceAudioRetry, RecoveryStopsAutomaticInitialization)
{
	CVoiceAudioRetry Retry;
	int Calls = 0;
	bool Ready = false;
	const auto Initialize = [&] { ++Calls; return Ready; };
	Retry.Update(true, false, 0, 100, Initialize);
	Ready = true;
	Retry.Update(false, false, 100, 100, Initialize);
	Retry.Update(false, false, 200, 100, Initialize);
	EXPECT_EQ(Calls, 2);
}

TEST(VoiceAudioRetry, MissingDeviceRetriesEvenWhenInitializationReturnsSuccess)
{
	CVoiceAudioRetry Retry;
	int Calls = 0;
	const auto Initialize = [&] { ++Calls; return true; };
	Retry.Update(true, false, 0, 100, Initialize);
	Retry.Update(false, true, 99, 100, Initialize);
	EXPECT_EQ(Calls, 1);
	Retry.Update(false, true, 100, 100, Initialize);
	EXPECT_EQ(Calls, 2);
	Retry.Update(false, false, 200, 100, Initialize);
	EXPECT_EQ(Calls, 2);
}

TEST(VoiceAudioRetry, ConfigurationRefreshBypassesThePreviousRetryDeadline)
{
	CVoiceAudioRetry Retry;
	int Calls = 0;
	bool Ready = false;
	const auto Initialize = [&] { ++Calls; return Ready; };
	Retry.Update(true, false, 0, 100, Initialize);
	Ready = true;
	Retry.Update(true, false, 1, 100, Initialize);
	EXPECT_EQ(Calls, 2);
}

TEST(VoiceAudioRetry, ResetClearsFailureAndAllowsImmediateReopening)
{
	CVoiceAudioRetry Retry;
	int Calls = 0;
	const auto Initialize = [&] { ++Calls; return false; };
	Retry.Update(true, false, 100, 100, Initialize);
	Retry.Reset();
	Retry.Update(false, false, 300, 100, Initialize);
	EXPECT_EQ(Calls, 1);
	Retry.Update(true, false, 0, 100, Initialize);
	EXPECT_EQ(Calls, 2);
}
