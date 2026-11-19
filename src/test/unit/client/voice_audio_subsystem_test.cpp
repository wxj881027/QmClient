#include <game/client/components/qmclient/voice/voice_audio_retry.h>

#include <gtest/gtest.h>

#include <algorithm>

class CVoiceAudioSubsystemTest : public ::testing::Test
{
protected:
	CVoiceAudioSubsystem m_Subsystem;
	int64_t m_Now = 0;
	int64_t m_InitializationDuration = 0;
	int m_Initializations = 0;
	int m_Shutdowns = 0;
	bool m_InitializeSucceeds = false;

	bool Ensure(const char *pBackend = "")
	{
		return m_Subsystem.Ensure(pBackend, 100, [this] { return m_Now; }, [this] {
			++m_Initializations;
			m_Now += m_InitializationDuration;
			return m_InitializeSucceeds; });
	}

	void Shutdown()
	{
		m_Subsystem.Shutdown([this] { ++m_Shutdowns; });
	}
};

TEST_F(CVoiceAudioSubsystemTest, FirstDefaultBackendAttemptDoesNotWait)
{
	EXPECT_FALSE(Ensure());
	EXPECT_EQ(m_Initializations, 1);
	EXPECT_FALSE(m_Subsystem.IsInitialized());
}

TEST_F(CVoiceAudioSubsystemTest, FailedInitializationWaitsAcrossRenderFrames)
{
	EXPECT_FALSE(Ensure("wasapi"));
	for(m_Now = 1; m_Now < 100; ++m_Now)
		EXPECT_FALSE(Ensure("wasapi"));
	EXPECT_EQ(m_Initializations, 1);
	EXPECT_FALSE(Ensure("wasapi"));
	EXPECT_EQ(m_Initializations, 2);
	++m_Now;
	EXPECT_FALSE(Ensure("wasapi"));
	EXPECT_EQ(m_Initializations, 2);
}

TEST_F(CVoiceAudioSubsystemTest, SlowFailureGetsAFullIntervalAfterItReturns)
{
	m_InitializationDuration = 300;
	EXPECT_FALSE(Ensure());
	EXPECT_EQ(m_Now, 300);
	EXPECT_FALSE(Ensure());
	m_Now = 399;
	EXPECT_FALSE(Ensure());
	EXPECT_EQ(m_Initializations, 1);
	m_Now = 400;
	EXPECT_FALSE(Ensure());
	EXPECT_EQ(m_Initializations, 2);
}

TEST_F(CVoiceAudioSubsystemTest, SuccessfulRecoveryStopsFurtherInitialization)
{
	EXPECT_FALSE(Ensure());
	m_InitializeSucceeds = true;
	m_Now = 100;
	EXPECT_TRUE(Ensure());
	EXPECT_TRUE(m_Subsystem.IsInitialized());
	m_Now = 1000;
	EXPECT_TRUE(Ensure());
	EXPECT_EQ(m_Initializations, 2);
}

TEST_F(CVoiceAudioSubsystemTest, ChangingFailedBackendRetriesImmediatelyAndThenWaits)
{
	EXPECT_FALSE(Ensure("wasapi"));
	++m_Now;
	EXPECT_FALSE(Ensure("directsound"));
	EXPECT_EQ(m_Initializations, 2);
	++m_Now;
	EXPECT_FALSE(Ensure("directsound"));
	EXPECT_EQ(m_Initializations, 2);
	EXPECT_FALSE(Ensure());
	EXPECT_EQ(m_Initializations, 3);
}

TEST_F(CVoiceAudioSubsystemTest, FullLengthBackendNameIsRememberedDuringRetryWait)
{
	char aBackend[64];
	std::fill_n(aBackend, sizeof(aBackend) - 1, 'a');
	aBackend[sizeof(aBackend) - 1] = '\0';
	EXPECT_FALSE(Ensure(aBackend));
	++m_Now;
	EXPECT_FALSE(Ensure(aBackend));
	EXPECT_EQ(m_Initializations, 1);
}

TEST_F(CVoiceAudioSubsystemTest, InitializedSubsystemKeepsTheExistingDriverUntilShutdown)
{
	m_InitializeSucceeds = true;
	EXPECT_TRUE(Ensure("wasapi"));
	EXPECT_TRUE(Ensure("directsound"));
	EXPECT_EQ(m_Initializations, 1);
	Shutdown();
	EXPECT_TRUE(Ensure("directsound"));
	EXPECT_EQ(m_Initializations, 2);
}

TEST_F(CVoiceAudioSubsystemTest, ShutdownReleasesSuccessfulInitializationOnlyOnce)
{
	m_InitializeSucceeds = true;
	EXPECT_TRUE(Ensure());
	Shutdown();
	Shutdown();
	EXPECT_EQ(m_Shutdowns, 1);
	EXPECT_FALSE(m_Subsystem.IsInitialized());
	EXPECT_TRUE(Ensure());
	Shutdown();
	EXPECT_EQ(m_Shutdowns, 2);
}

TEST_F(CVoiceAudioSubsystemTest, ShutdownAfterFailureAllowsImmediateRetryWithoutRelease)
{
	EXPECT_FALSE(Ensure("wasapi"));
	Shutdown();
	EXPECT_EQ(m_Shutdowns, 0);
	EXPECT_FALSE(m_Subsystem.IsInitialized());
	++m_Now;
	EXPECT_FALSE(Ensure("wasapi"));
	EXPECT_EQ(m_Initializations, 2);
}
