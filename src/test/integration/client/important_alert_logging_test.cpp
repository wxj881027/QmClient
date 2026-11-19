#include <base/logger.h>
#include <base/str.h>

#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/components/important_alert.h>

#include <test/support/qm_ui_test_client.h>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	class CAlertPlaybackClient final : public qm_ui_test::CTestUiClient
	{
	public:
		CAlertPlaybackClient() { m_State = STATE_DEMOPLAYBACK; }
	};

	struct SAlertLoggingInterfaces
	{
		CAlertPlaybackClient m_Client;
		std::unique_ptr<IEngineTextRender> m_pTextRender{CreateEngineTextRender()};
	};

	thread_local SAlertLoggingInterfaces *s_pAlertLoggingInterfaces = nullptr;

	class CAlertLogCapture final : public ILogger
	{
	public:
		std::vector<std::string> m_vMessages;

		void Log(const CLogMessage *pMessage) override
		{
			if(str_comp(pMessage->m_aSystem, "server_alert") == 0)
				m_vMessages.emplace_back(pMessage->Message());
		}
	};

	class ImportantAlertLogging : public ::testing::Test
	{
		SAlertLoggingInterfaces m_Interfaces;

		void SetUp() override { s_pAlertLoggingInterfaces = &m_Interfaces; }
		void TearDown() override { s_pAlertLoggingInterfaces = nullptr; }
	};
}

// testrunner 没有完整游戏客户端。这里接入回放时钟和真实的文字容器清理入口。
IClient *CComponentInterfaces::Client() const
{
	if(s_pAlertLoggingInterfaces == nullptr)
		throw std::logic_error("Client accessed outside the important alert logging fixture");
	return &s_pAlertLoggingInterfaces->m_Client;
}

ITextRender *CComponentInterfaces::TextRender() const
{
	if(s_pAlertLoggingInterfaces == nullptr)
		throw std::logic_error("Text renderer accessed outside the important alert logging fixture");
	return s_pAlertLoggingInterfaces->m_pTextRender.get();
}

IGraphics *CComponentInterfaces::Graphics() const
{
	throw std::logic_error("Important alert logging must not render");
}

TEST_F(ImportantAlertLogging, LongUnicodeMessageReachesLoggerAsCompleteUtf8)
{
	std::string Message = "游戏公告 ";
	for(int Index = 0; Index < 320; ++Index)
		Message += "📣";

	CNetMsg_Sv_ServerAlert AlertMessage{};
	AlertMessage.m_pMessage = Message.c_str();
	CImportantAlert Alert;
	CAlertLogCapture Capture;
	{
		CLogScope Scope(&Capture);
		Alert.OnMessage(NETMSGTYPE_SV_SERVERALERT, &AlertMessage);
	}

	ASSERT_FALSE(Capture.m_vMessages.empty());
	std::string LoggedMessage;
	for(const std::string &Line : Capture.m_vMessages)
	{
		EXPECT_TRUE(str_utf8_check(Line.c_str()));
		LoggedMessage += Line;
	}
	EXPECT_EQ(LoggedMessage, Message);
}
