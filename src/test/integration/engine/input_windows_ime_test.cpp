#include <engine/client/input_windows_ime.h>

#include <gtest/gtest.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <vector>

namespace
{
	class CWindowsImeHookTest : public testing::Test
	{
	protected:
		HWND m_Window = nullptr;
		CWindowsImeHook m_Hook;
		std::vector<WPARAM> m_vNotifications;

		static void Notify(void *pUser, HWND, WPARAM Notification, LPARAM)
		{
			static_cast<CWindowsImeHookTest *>(pUser)->m_vNotifications.push_back(Notification);
		}

		void SetUp() override
		{
			// 隐藏的消息窗口无需 SDL、输入法安装或真实用户配置。
			m_Window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
			ASSERT_NE(m_Window, nullptr);
			ASSERT_TRUE(m_Hook.Attach(m_Window, Notify, this));
		}

		void TearDown() override
		{
			m_Hook.Detach();
			if(m_Window != nullptr)
				DestroyWindow(m_Window);
		}
	};
}

TEST_F(CWindowsImeHookTest, SynchronousCandidateNotificationsArriveInOrder)
{
	for(const auto Notification : {IMN_OPENCANDIDATE, IMN_CHANGECANDIDATE, IMN_CLOSECANDIDATE})
		SendMessageW(m_Window, WM_IME_NOTIFY, Notification, 1);
	EXPECT_EQ(m_vNotifications, (std::vector<WPARAM>{IMN_OPENCANDIDATE, IMN_CHANGECANDIDATE, IMN_CLOSECANDIDATE}));
}

TEST_F(CWindowsImeHookTest, UnrelatedImeNotificationsLeaveCandidateStateAlone)
{
	SendMessageW(m_Window, WM_IME_STARTCOMPOSITION, 0, 0);
	SendMessageW(m_Window, WM_IME_ENDCOMPOSITION, 0, 0);
	SendMessageW(m_Window, WM_IME_NOTIFY, IMN_SETOPENSTATUS, 0);
	EXPECT_TRUE(m_vNotifications.empty());
	// 消息仍传给原窗口过程。
	SendMessageW(m_Window, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"forwarded"));
	wchar_t aText[32]{};
	GetWindowTextW(m_Window, aText, 32);
	EXPECT_STREQ(aText, L"forwarded");
}

TEST_F(CWindowsImeHookTest, RepeatedAttachDoesNotDuplicateNotifications)
{
	ASSERT_TRUE(m_Hook.Attach(m_Window, Notify, this));
	SendMessageW(m_Window, WM_IME_NOTIFY, IMN_OPENCANDIDATE, 1);
	EXPECT_EQ(m_vNotifications.size(), 1u);
}

TEST_F(CWindowsImeHookTest, DetachStopsNotificationsAndAllowsReopening)
{
	m_Hook.Detach();
	m_Hook.Detach();
	SendMessageW(m_Window, WM_IME_NOTIFY, IMN_OPENCANDIDATE, 1);
	EXPECT_TRUE(m_vNotifications.empty());
	ASSERT_TRUE(m_Hook.Attach(m_Window, Notify, this));
	SendMessageW(m_Window, WM_IME_NOTIFY, IMN_CHANGECANDIDATE, 1);
	EXPECT_EQ(m_vNotifications, (std::vector<WPARAM>{IMN_CHANGECANDIDATE}));
}

TEST_F(CWindowsImeHookTest, DestroyedWindowReleasesHookBeforeReuse)
{
	ASSERT_TRUE(DestroyWindow(m_Window));
	m_Window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
	ASSERT_NE(m_Window, nullptr);
	ASSERT_TRUE(m_Hook.Attach(m_Window, Notify, this));
	SendMessageW(m_Window, WM_IME_NOTIFY, IMN_OPENCANDIDATE, 1);
	EXPECT_EQ(m_vNotifications, (std::vector<WPARAM>{IMN_OPENCANDIDATE}));
}
#endif
