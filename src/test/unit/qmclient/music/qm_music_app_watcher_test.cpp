#include <game/client/components/qmclient/music_app_watcher.h>
#include <game/client/components/qmclient/qm_music_hook_registry.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <memory>

TEST(QmMusicAppWatcher, SingleProcessSnapshotCombinesAllRegisteredApplications)
{
	const SQmMusicHookEntry aHooks[] = {
		{nullptr, "a", "a", L"cloudmusic.exe"},
		{nullptr, "b", "b", L"SodaMusic.exe"},
		{nullptr, "c", "c", L"Spotify.exe"},
		{nullptr, "d", "d", nullptr},
	};
	uint64_t Mask = 0;
	for(const wchar_t *pName : {L"explorer.exe", L"CLOUDMUSIC.EXE", L"spotify.exe", L"Spotify.exe"})
		Mask |= QmMusicHookMaskForProcess(pName, aHooks, std::size(aHooks));
	EXPECT_EQ(Mask, uint64_t(5));
	EXPECT_EQ(QmMusicHookMaskForProcess(L"sodamusic.exe", aHooks, std::size(aHooks)), uint64_t(2));
	EXPECT_EQ(QmMusicHookMaskForProcess(L"cloudmusic.exe.bak", aHooks, std::size(aHooks)), uint64_t(0));
	EXPECT_EQ(QmMusicHookMaskForProcess(L"", aHooks, std::size(aHooks)), uint64_t(0));
}

TEST(QmMusicAppWatcher, PendingProcessScanDoesNotWaitOrExposePartialResult)
{
	CSemaphore Started;
	CSemaphore FinishScan;
	CJobPool Pool;
	Pool.Init(1);
	auto pScan = std::make_shared<CQmMusicAppScanJob>([&] {
		Started.Signal();
		FinishScan.Wait();
		return uint64_t(5);
	});
	uint64_t Mask = 99;
	EXPECT_FALSE(pScan->TryGetResult(Mask));
	EXPECT_EQ(Mask, uint64_t(99));
	Pool.Add(pScan);
	Started.Wait();
	// worker 仍在枚举时，主线程轮询必须直接返回，且不能发布未完成的数据。
	EXPECT_FALSE(pScan->TryGetResult(Mask));
	EXPECT_EQ(Mask, uint64_t(99));
	FinishScan.Signal();
	Pool.Shutdown();
	ASSERT_TRUE(pScan->TryGetResult(Mask));
	EXPECT_EQ(Mask, uint64_t(5));
}

TEST(QmMusicAppWatcher, ProcessScanOutlivesWatcherReferenceWithoutCallback)
{
	CSemaphore Started;
	CSemaphore FinishScan;
	std::atomic<bool> Completed = false;
	CJobPool Pool;
	Pool.Init(1);
	auto pScan = std::make_shared<CQmMusicAppScanJob>([&] {
		Started.Signal();
		FinishScan.Wait();
		Completed = true;
		return uint64_t(0);
	});
	Pool.Add(pScan);
	Started.Wait();
	// 模拟组件退出时释放引用；后台任务只写自己的结果，无需访问组件。
	pScan.reset();
	FinishScan.Signal();
	Pool.Shutdown();
	EXPECT_TRUE(Completed.load());
}
