#include <base/str.h>

#include <engine/client/steam_startup.h>

#include <gtest/gtest.h>

#include <memory>
#include <new>
#include <stdexcept>

TEST(SteamStartupLaunch, DetectedCompletePathIsPassedToLauncherOnce)
{
	int Step = 0;
	EXPECT_TRUE(steam_startup::Open([&](char *pPath, int Capacity) {
		EXPECT_EQ(Step++, 0);
		str_copy(pPath, "D:/测试目录/steam.exe", Capacity);
		return true; }, [&](const char *pPath) {
		EXPECT_EQ(Step++, 1);
		EXPECT_STREQ(pPath, "D:/测试目录/steam.exe");
		return true; }));
	EXPECT_EQ(Step, 2);
}

TEST(SteamStartupLaunch, MissingClientDoesNotCallLauncher)
{
	EXPECT_FALSE(steam_startup::Open([](char *, int) { return false; }, [](const char *) {
		ADD_FAILURE() << "missing client was launched";
		return true; }));
}

TEST(SteamStartupLaunch, DiscoveryAllocationFailureReturnsFailureWithoutLaunch)
{
	EXPECT_FALSE(steam_startup::Open([](char *, int) -> bool { throw std::bad_alloc(); }, [](const char *) {
		ADD_FAILURE() << "failed discovery was launched";
		return true; }));
}

TEST(SteamStartupLaunch, UnknownDiscoveryExceptionDoesNotEscapeOptionalStartup)
{
	EXPECT_FALSE(steam_startup::Open([](char *, int) -> bool { throw 17; }, [](const char *) {
		ADD_FAILURE();
		return true; }));
}

TEST(SteamStartupLaunch, LauncherFailureIsReturned)
{
	EXPECT_FALSE(steam_startup::Open([](char *pPath, int Capacity) {
		str_copy(pPath, "steam", Capacity);
		return true; }, [](const char *) { return false; }));
}

TEST(SteamStartupLaunch, LauncherPreparationExceptionReturnsFailure)
{
	EXPECT_FALSE(steam_startup::Open([](char *pPath, int Capacity) {
		str_copy(pPath, "steam", Capacity);
		return true; }, [](const char *) -> bool { throw std::runtime_error("launch preparation"); }));
}

TEST(SteamStartupLaunch, FailedAttemptDoesNotPreventSubsequentLaunch)
{
	int Finds = 0;
	int Launches = 0;
	const auto Find = [&](char *pPath, int Capacity) {
		if(++Finds == 1)
			throw std::bad_alloc();
		str_copy(pPath, "steam", Capacity);
		return true;
	};
	const auto Launch = [&](const char *) {
		++Launches;
		return true;
	};
	EXPECT_FALSE(steam_startup::Open(Find, Launch));
	EXPECT_TRUE(steam_startup::Open(Find, Launch));
	EXPECT_EQ(Finds, 2);
	EXPECT_EQ(Launches, 1);
}

namespace
{
	class CObject
	{
	public:
		virtual ~CObject() = default;
	};

	class CThrowingObject : public CObject
	{
	public:
		CThrowingObject() { throw std::runtime_error("construction"); }
	};
}

TEST(SteamStartupSdk, FailedInitializationReturnsPreparedFallbackWithoutFactoryOrShutdown)
{
	auto pFallback = std::make_unique<CObject>();
	const CObject *pIdentity = pFallback.get();
	int Shutdowns = 0;
	auto pResult = steam_startup::Create(pFallback, [] { return false; }, []() -> std::unique_ptr<CObject> {
		ADD_FAILURE();
		return {}; }, [&]() noexcept { ++Shutdowns; });
	EXPECT_EQ(pResult.get(), pIdentity);
	EXPECT_FALSE(pFallback);
	EXPECT_EQ(Shutdowns, 0);
}

TEST(SteamStartupSdk, InitializationExceptionReturnsFallbackWithoutClaimingSdkOwnership)
{
	auto pFallback = std::make_unique<CObject>();
	const CObject *pIdentity = pFallback.get();
	int Shutdowns = 0;
	auto pResult = steam_startup::Create(pFallback, []() -> bool { throw std::bad_alloc(); }, []() -> std::unique_ptr<CObject> {
		ADD_FAILURE();
		return {}; }, [&]() noexcept { ++Shutdowns; });
	EXPECT_EQ(pResult.get(), pIdentity);
	EXPECT_EQ(Shutdowns, 0);
}

TEST(SteamStartupSdk, FactoryAllocationExceptionReleasesInitializedSdkBeforeReturningFallback)
{
	auto pFallback = std::make_unique<CObject>();
	const CObject *pIdentity = pFallback.get();
	int Step = 0;
	auto pResult = steam_startup::Create(pFallback, [&] {
		EXPECT_EQ(Step++, 0);
		return true; }, [&]() -> std::unique_ptr<CObject> {
		EXPECT_EQ(Step++, 1);
		throw std::bad_alloc(); }, [&]() noexcept {
		EXPECT_EQ(Step++, 2);
		EXPECT_EQ(pFallback.get(), pIdentity); });
	EXPECT_EQ(Step, 3);
	EXPECT_EQ(pResult.get(), pIdentity);
	EXPECT_FALSE(pFallback);
}

TEST(SteamStartupSdk, ActualObjectConstructionExceptionReleasesInitializedSdk)
{
	std::unique_ptr<CObject> pFallback = std::make_unique<CObject>();
	const CObject *pIdentity = pFallback.get();
	int Shutdowns = 0;
	auto pResult = steam_startup::Create(pFallback, [] { return true; }, [] { return std::make_unique<CThrowingObject>(); }, [&]() noexcept { ++Shutdowns; });
	EXPECT_EQ(pResult.get(), pIdentity);
	EXPECT_EQ(Shutdowns, 1);
}

TEST(SteamStartupSdk, EmptyFactoryResultReleasesInitializedSdk)
{
	auto pFallback = std::make_unique<CObject>();
	const CObject *pIdentity = pFallback.get();
	int Shutdowns = 0;
	auto pResult = steam_startup::Create(pFallback, [] { return true; }, [] { return std::unique_ptr<CObject>(); }, [&]() noexcept { ++Shutdowns; });
	EXPECT_EQ(pResult.get(), pIdentity);
	EXPECT_EQ(Shutdowns, 1);
}

TEST(SteamStartupSdk, SuccessfulFactoryTransfersObjectWithoutPrematureShutdown)
{
	auto pFallback = std::make_unique<CObject>();
	auto pCandidate = std::make_unique<CObject>();
	const CObject *pIdentity = pCandidate.get();
	int Shutdowns = 0;
	auto pResult = steam_startup::Create(pFallback, [] { return true; }, [&] { return std::move(pCandidate); }, [&]() noexcept { ++Shutdowns; });
	EXPECT_EQ(pResult.get(), pIdentity);
	EXPECT_TRUE(pFallback);
	EXPECT_FALSE(pCandidate);
	EXPECT_EQ(Shutdowns, 0);
}

TEST(SteamStartupSdk, LaterAttemptCanSucceedAfterFailedFactory)
{
	auto pFallback = std::make_unique<CObject>();
	int Shutdowns = 0;
	auto pFailed = steam_startup::Create(pFallback, [] { return true; }, []() -> std::unique_ptr<CObject> { throw std::bad_alloc(); }, [&]() noexcept { ++Shutdowns; });
	ASSERT_TRUE(pFailed);
	pFallback = std::move(pFailed);
	auto pResult = steam_startup::Create(pFallback, [] { return true; }, [] { return std::make_unique<CObject>(); }, [&]() noexcept { ++Shutdowns; });
	EXPECT_TRUE(pResult);
	EXPECT_TRUE(pFallback);
	EXPECT_NE(pResult.get(), pFallback.get());
	EXPECT_EQ(Shutdowns, 1);
}
