#include <game/client/components/qmclient/monitoring/monitoring.h>

#include <gtest/gtest.h>

#include <new>
#include <system_error>

namespace
{
	class CFailedThreadOperations : public CQmAsyncDevicePerfSampler::COperations
	{
	public:
		int m_Mode;
		int m_Calls = 0;
		explicit CFailedThreadOperations(int Mode) :
			m_Mode(Mode) {}
		std::thread CreateThread(std::function<void()>) override
		{
			++m_Calls;
			switch(m_Mode)
			{
			case 1: throw std::bad_alloc();
			case 2: throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
			case 3: throw 1;
			default: return {};
			}
		}
	};

	void ExpectRetryableFailure(int Mode)
	{
		CFailedThreadOperations Operations(Mode);
		CQmAsyncDevicePerfSampler Sampler({}, std::chrono::milliseconds(1), &Operations);
		QmUpdateDevicePerfSamplerState(Sampler, true);
		EXPECT_EQ(Operations.m_Calls, 1);
		EXPECT_FALSE(Sampler.EnsureStarted());
		EXPECT_EQ(Operations.m_Calls, 2);
		EXPECT_EQ(Sampler.Snapshot().m_Version, 0u);
		EXPECT_FALSE(Sampler.Snapshot().m_Sample.m_Available);
		Sampler.Stop();
		Sampler.Stop();
	}
}

TEST(QmDeviceSamplerFailure, ThreadAllocationFailureDoesNotLatchStartedState)
{
	ExpectRetryableFailure(1);
}

TEST(QmDeviceSamplerFailure, NativeThreadCreationErrorDoesNotEscapeStateUpdater)
{
	ExpectRetryableFailure(2);
}

TEST(QmDeviceSamplerFailure, UnknownThreadCreationFailureRemainsRetryable)
{
	ExpectRetryableFailure(3);
}

TEST(QmDeviceSamplerFailure, EmptyCreatedThreadDoesNotEnableSampling)
{
	ExpectRetryableFailure(0);
}
