#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>

#include <engine/client/qm_festive_message_box.h>

#include <windows.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cwchar>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <thread>

namespace QmFestiveMessageBox
{
	// 只替换可选音乐，窗口消息和退出仍走正式实现。
	std::optional<int> ShowWithPlayback(const IGraphics::CMessageBox &MessageBox,
		std::function<void(const std::atomic_bool &)> Playback, bool HiddenForTest);
}

namespace
{
	struct SPlaybackProbe
	{
		std::promise<DWORD> m_DialogThreadId;
		std::promise<HWND> m_PlaybackWindow;
		std::promise<std::optional<int>> m_DialogResult;
		std::promise<void> m_Release;
		std::shared_future<void> m_ReleasePlayback = m_Release.get_future().share();
		std::promise<bool> m_SawCancellation;
	};

	BOOL CALLBACK FindFestiveWindow(HWND Window, LPARAM Arg)
	{
		wchar_t aClass[64];
		if(GetClassNameW(Window, aClass, std::size(aClass)) > 0 && std::wcscmp(aClass, L"QmClientFestiveCrashWindow") == 0)
		{
			*reinterpret_cast<HWND *>(Arg) = Window;
			return FALSE;
		}
		return TRUE;
	}
}

TEST(QmFestiveMessageBox, ClosingReturnsWhilePlaybackIsBlocked)
{
	using namespace std::chrono_literals;
	auto pProbe = std::make_shared<SPlaybackProbe>();
	auto DialogThreadId = pProbe->m_DialogThreadId.get_future();
	auto PlaybackWindow = pProbe->m_PlaybackWindow.get_future();
	auto DialogResult = pProbe->m_DialogResult.get_future();
	auto SawCancellation = pProbe->m_SawCancellation.get_future();

	std::thread Dialog([pProbe] {
		MSG Message{};
		PeekMessageW(&Message, nullptr, 0, 0, PM_NOREMOVE);
		const DWORD ThreadId = GetCurrentThreadId();
		pProbe->m_DialogThreadId.set_value(ThreadId);

		const IGraphics::CMessageBox MessageBox{
			.m_pTitle = "QmClient Crash Report",
			.m_pMessage = "Report type: fatal-crash",
			.m_vButtons = {{.m_pLabel = "Close report", .m_Cancel = true}},
		};
		const auto Result = QmFestiveMessageBox::ShowWithPlayback(MessageBox, [pProbe, ThreadId](const std::atomic_bool &Stop) {
			HWND Window = nullptr;
			EnumThreadWindows(ThreadId, FindFestiveWindow, reinterpret_cast<LPARAM>(&Window));
			pProbe->m_PlaybackWindow.set_value(Window);
			const bool Released = pProbe->m_ReleasePlayback.wait_for(20s) == std::future_status::ready;
			pProbe->m_SawCancellation.set_value(Released && Stop.load()); }, true);
		pProbe->m_DialogResult.set_value(Result);
	});

	const DWORD ThreadId = DialogThreadId.wait_for(5s) == std::future_status::ready ? DialogThreadId.get() : 0;
	const HWND Window = PlaybackWindow.wait_for(5s) == std::future_status::ready ? PlaybackWindow.get() : nullptr;
	const bool ClosePosted = Window != nullptr && PostMessageW(Window, WM_CLOSE, 0, 0) != FALSE;
	const bool ReturnedBeforeRelease = ClosePosted && DialogResult.wait_for(3s) == std::future_status::ready;
	pProbe->m_Release.set_value();
	if(!ReturnedBeforeRelease && ThreadId != 0 && DialogResult.wait_for(0s) != std::future_status::ready)
		PostThreadMessageW(ThreadId, WM_QUIT, 0, 0);
	const bool DialogFinished = DialogResult.wait_for(5s) == std::future_status::ready;
	if(DialogFinished)
		Dialog.join();
	else
		Dialog.detach();

	ASSERT_NE(Window, nullptr);
	ASSERT_TRUE(ClosePosted);
	EXPECT_TRUE(ReturnedBeforeRelease);
	ASSERT_TRUE(DialogFinished);
	EXPECT_EQ(DialogResult.get(), std::optional<int>{0});
	ASSERT_EQ(SawCancellation.wait_for(5s), std::future_status::ready);
	EXPECT_TRUE(SawCancellation.get());
}

#endif
