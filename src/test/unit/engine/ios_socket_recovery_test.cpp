#include <engine/client/ios_socket_recovery.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
	struct CRecordingClient
	{
		std::vector<std::string> m_vCalls;
		bool m_ResetSucceeds = true;

		void DisconnectInternal() { m_vCalls.emplace_back("disconnect"); }
		bool ResetSocket()
		{
			m_vCalls.emplace_back("reset");
			return m_ResetSucceeds;
		}
		void LoadDDNetInfo() { m_vCalls.emplace_back("load info"); }
		void ConnectInternal(const char *pAddress) { m_vCalls.emplace_back(pAddress); }
	};

	struct CRecordingSocket
	{
		CRecordingClient &m_Client;
		const char *m_pCloseEvent;

		void Close() { m_Client.m_vCalls.emplace_back(m_pCloseEvent); }
	};

	class IosSocketRecovery : public ::testing::Test
	{
	protected:
		CRecordingClient m_Client;
		CRecordingSocket m_aSockets[2] = {{m_Client, "close main"}, {m_Client, "close dummy"}};

		bool Restore(const char *pAddress, bool ReconnectDummy)
		{
			return RestoreIosSockets(m_Client, m_aSockets, pAddress, ReconnectDummy,
				[this]() { m_Client.m_vCalls.emplace_back("queue dummy"); });
		}
	};
}

TEST_F(IosSocketRecovery, ReconnectsToSavedAddressBeforeQueuingDummy)
{
	EXPECT_TRUE(Restore("server:8303", true));
	EXPECT_EQ(m_Client.m_vCalls, (std::vector<std::string>{"disconnect", "close main", "close dummy", "reset", "load info", "server:8303", "queue dummy"}));
}

TEST_F(IosSocketRecovery, RestoresMainWithoutQueuingAnAbsentDummy)
{
	EXPECT_TRUE(Restore("server:8303", false));
	EXPECT_EQ(m_Client.m_vCalls, (std::vector<std::string>{"disconnect", "close main", "close dummy", "reset", "load info", "server:8303"}));
}

TEST_F(IosSocketRecovery, OfflineClientOnlyRefreshesServerInfo)
{
	EXPECT_TRUE(Restore(nullptr, false));
	EXPECT_EQ(m_Client.m_vCalls, (std::vector<std::string>{"disconnect", "close main", "close dummy", "reset", "load info"}));
}

TEST_F(IosSocketRecovery, ResetFailureStopsBeforeServerInfoAndReconnect)
{
	m_Client.m_ResetSucceeds = false;
	EXPECT_FALSE(Restore("server:8303", true));
	EXPECT_EQ(m_Client.m_vCalls, (std::vector<std::string>{"disconnect", "close main", "close dummy", "reset"}));
}
