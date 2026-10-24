#include <base/logger.h>

#include <gtest/gtest.h>

#include <memory>
#include <new>
#include <stdexcept>

namespace
{
	class CProbeLogger : public ILogger
	{
		bool m_Available;
		int *m_pDestroyed;

	public:
		explicit CProbeLogger(bool Available = true, int *pDestroyed = nullptr) :
			m_Available(Available), m_pDestroyed(pDestroyed) {}
		~CProbeLogger() override
		{
			if(m_pDestroyed)
				++*m_pDestroyed;
		}
		bool IsAvailable() const noexcept override { return m_Available; }
		void Log(const CLogMessage *) override {}
	};
}

TEST(LoggerPrepare, EmptyCandidateKeepsTheExistingBackend)
{
	std::shared_ptr<ILogger> pTarget = std::make_shared<CProbeLogger>();
	const auto pOld = pTarget;
	EXPECT_FALSE(log_prepare_backend(pTarget, [] { return std::unique_ptr<ILogger>{}; }));
	EXPECT_EQ(pTarget, pOld);
}

TEST(LoggerPrepare, AllocationExceptionKeepsTheExistingBackend)
{
	std::shared_ptr<ILogger> pTarget = std::make_shared<CProbeLogger>();
	const auto pOld = pTarget;
	EXPECT_FALSE(log_prepare_backend(pTarget, []() -> std::unique_ptr<ILogger> { throw std::bad_alloc(); }));
	EXPECT_EQ(pTarget, pOld);
}

TEST(LoggerPrepare, OtherExceptionKeepsTheExistingBackend)
{
	std::shared_ptr<ILogger> pTarget = std::make_shared<CProbeLogger>();
	const auto pOld = pTarget;
	EXPECT_FALSE(log_prepare_backend(pTarget, []() -> std::unique_ptr<ILogger> { throw std::runtime_error("backend creation"); }));
	EXPECT_EQ(pTarget, pOld);
}

TEST(LoggerPrepare, UnavailableCandidateIsReleasedWithoutReplacingTheBackend)
{
	int Destroyed = 0;
	std::shared_ptr<ILogger> pTarget = std::make_shared<CProbeLogger>();
	const auto pOld = pTarget;
	EXPECT_FALSE(log_prepare_backend(pTarget, [&] { return std::make_unique<CProbeLogger>(false, &Destroyed); }));
	EXPECT_EQ(pTarget, pOld);
	EXPECT_EQ(Destroyed, 1);
}

TEST(LoggerPrepare, PreparedCandidateReplacesTheOldBackend)
{
	int Destroyed = 0;
	std::shared_ptr<ILogger> pTarget = std::make_shared<CProbeLogger>(true, &Destroyed);
	EXPECT_TRUE(log_prepare_backend(pTarget, [] { return std::make_unique<CProbeLogger>(); }));
	ASSERT_NE(pTarget, nullptr);
	EXPECT_TRUE(pTarget->IsAvailable());
	EXPECT_EQ(Destroyed, 1);
}

TEST(LoggerPrepare, AFailedAttemptCanBeRetried)
{
	std::shared_ptr<ILogger> pTarget;
	EXPECT_FALSE(log_prepare_backend(pTarget, []() -> std::unique_ptr<ILogger> { throw std::bad_alloc(); }));
	EXPECT_EQ(pTarget, nullptr);
	EXPECT_TRUE(log_prepare_backend(pTarget, [] { return std::make_unique<CProbeLogger>(); }));
	EXPECT_NE(pTarget, nullptr);
}
