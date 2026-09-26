#include "test.h"

#include <gtest/gtest.h>

#include <string>

namespace
{
	std::string ExtractFunctionBody(const std::string &Source, const char *pSignature)
	{
		const size_t SignaturePos = Source.find(pSignature);
		EXPECT_NE(SignaturePos, std::string::npos) << pSignature;
		if(SignaturePos == std::string::npos)
		{
			return {};
		}

		const size_t BodyStart = Source.find('{', SignaturePos);
		EXPECT_NE(BodyStart, std::string::npos) << pSignature;
		if(BodyStart == std::string::npos)
		{
			return {};
		}

		int Depth = 1;
		size_t Pos = BodyStart + 1;
		for(; Pos < Source.size() && Depth > 0; ++Pos)
		{
			if(Source[Pos] == '{')
				++Depth;
			else if(Source[Pos] == '}')
				--Depth;
		}

		EXPECT_EQ(Depth, 0) << pSignature;
		return Depth == 0 ? Source.substr(BodyStart + 1, Pos - BodyStart - 2) : std::string();
	}
}

TEST(IosRuntime, SocketBreakageIsOnlyDetectedFromIosEpipeFailures)
{
	const std::string SystemSource = ReadTestSourceFile("src/base/system.cpp");
	const std::string SystemHeader = ReadTestSourceFile("src/base/system.h");
	const std::string SendBody = ExtractFunctionBody(SystemSource, "int net_udp_send(NETSOCKET sock, const NETADDR *addr, const void *data, int size)");

	ASSERT_FALSE(SendBody.empty());
	EXPECT_NE(SystemHeader.find("bool net_udp_is_broken(NETSOCKET sock);"), std::string::npos);
	EXPECT_NE(SystemSource.find("#if defined(CONF_PLATFORM_IOS)\nstatic void priv_net_udp_send_failed"), std::string::npos);
	EXPECT_NE(SystemSource.find("net_errno() != EPIPE"), std::string::npos);
	EXPECT_NE(SystemSource.find("sock->broken = true;"), std::string::npos);
	EXPECT_NE(SendBody.find("#if defined(CONF_PLATFORM_IOS)"), std::string::npos);
	EXPECT_NE(SendBody.find("priv_net_udp_send_failed(sock, addr);"), std::string::npos);
}

TEST(IosRuntime, ResumeRecreatesSocketsBeforeRestoringMainAndDummyConnections)
{
	const std::string ClientSource = ReadTestSourceFile("src/engine/client/client.cpp");
	const std::string ClientHeader = ReadTestSourceFile("src/engine/client/client.h");
	const std::string NetworkHeader = ReadTestSourceFile("src/engine/shared/network.h");
	const std::string RecreateBody = ExtractFunctionBody(ClientSource, "void CClient::RecreateBrokenSockets()");

	ASSERT_FALSE(RecreateBody.empty());
	EXPECT_NE(NetworkHeader.find("bool SocketIsBroken() const"), std::string::npos);
	EXPECT_NE(ClientHeader.find("bool m_DummyReconnectOnResume = false;"), std::string::npos);
	EXPECT_NE(ClientHeader.find("bool ResetSocket();"), std::string::npos);
	EXPECT_NE(ClientHeader.find("void RecreateBrokenSockets();"), std::string::npos);
	EXPECT_NE(RecreateBody.find("NetClient.SocketIsBroken()"), std::string::npos);
	EXPECT_NE(RecreateBody.find("Disconnect();"), std::string::npos);
	EXPECT_NE(RecreateBody.find("NetClient.Close();"), std::string::npos);
	const size_t ResetResult = RecreateBody.find("if(!ResetSocket())");
	ASSERT_NE(ResetResult, std::string::npos);
	EXPECT_GT(RecreateBody.find("LoadDDNetInfo();"), ResetResult);
	EXPECT_GT(RecreateBody.find("Connect(aConnectAddress);"), ResetResult);
	EXPECT_NE(ClientSource.find("else if(m_DummyReconnectOnResume)"), std::string::npos);
	EXPECT_NE(ClientSource.find("RecreateBrokenSockets();"), std::string::npos);
}
