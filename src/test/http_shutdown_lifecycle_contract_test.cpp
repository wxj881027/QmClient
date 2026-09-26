#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

namespace
{
	std::string FunctionBody(const std::string &Source, const std::string &Signature)
	{
		const size_t FunctionStart = Source.find(Signature);
		EXPECT_NE(FunctionStart, std::string::npos) << Signature;
		if(FunctionStart == std::string::npos)
			return {};

		const size_t BodyStart = Source.find('{', FunctionStart);
		EXPECT_NE(BodyStart, std::string::npos) << Signature;
		if(BodyStart == std::string::npos)
			return {};

		int Depth = 0;
		for(size_t Index = BodyStart; Index < Source.size(); ++Index)
		{
			if(Source[Index] == '{')
				++Depth;
			else if(Source[Index] == '}')
			{
				--Depth;
				if(Depth == 0)
					return Source.substr(BodyStart, Index - BodyStart);
			}
		}

		ADD_FAILURE() << Signature;
		return {};
	}
}

TEST(HttpShutdownLifecycle, ClientStopsServerBrowserBeforeHttpAndJobs)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/client.cpp");
	const size_t ServerBrowserShutdown = Source.find("m_ServerBrowser.Shutdown();");
	const size_t HttpShutdown = Source.find("m_pHttp->Shutdown();", ServerBrowserShutdown);
	const size_t JobsShutdown = Source.find("Engine()->ShutdownJobs();", HttpShutdown);

	ASSERT_NE(ServerBrowserShutdown, std::string::npos);
	ASSERT_NE(HttpShutdown, std::string::npos);
	ASSERT_NE(JobsShutdown, std::string::npos);
	EXPECT_LT(ServerBrowserShutdown, HttpShutdown);
	EXPECT_LT(HttpShutdown, JobsShutdown);
}

TEST(HttpShutdownLifecycle, ServerBrowserShutdownHandlesEarlyInitialization)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/serverbrowser.cpp");
	const std::string Body = FunctionBody(Source, "void CServerBrowser::Shutdown()");

	EXPECT_NE(Body.find("if(m_pHttp != nullptr)"), std::string::npos);
	EXPECT_NE(Body.find("m_pHttp->Shutdown();"), std::string::npos);
}

TEST(HttpShutdownLifecycle, ServerBrowserHttpDestructorsOnlyAssertClearedRequests)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/serverbrowser_http.cpp");
	const std::string ChooseMasterDestructor = FunctionBody(Source, "CChooseMaster::~CChooseMaster()");
	const std::string ServerBrowserDestructor = FunctionBody(Source, "CServerBrowserHttp::~CServerBrowserHttp()");
	const std::string Shutdown = FunctionBody(Source, "void CServerBrowserHttp::Shutdown()");

	EXPECT_EQ(ChooseMasterDestructor.find(".Abort("), std::string::npos);
	EXPECT_EQ(ServerBrowserDestructor.find(".Abort("), std::string::npos);
	EXPECT_NE(ChooseMasterDestructor.find("dbg_assert(m_pJob == nullptr"), std::string::npos);
	EXPECT_NE(ServerBrowserDestructor.find("dbg_assert(m_pGetServers == nullptr"), std::string::npos);
	EXPECT_NE(Shutdown.find("m_pGetServers->Abort();"), std::string::npos);
	EXPECT_NE(Shutdown.find("m_pGetServers = nullptr;"), std::string::npos);
	EXPECT_NE(Shutdown.find("m_pChooseMaster->Shutdown();"), std::string::npos);
	EXPECT_NE(Source.find("if(State() == IJob::STATE_ABORTED)"), std::string::npos);
}

TEST(HttpShutdownLifecycle, SkinShutdownClearsLoadAndHttpRequests)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const std::string ContainerDestructor = FunctionBody(Source, "CSkins::CSkinContainer::~CSkinContainer()");
	const std::string Shutdown = FunctionBody(Source, "void CSkins::OnShutdown()");

	EXPECT_EQ(ContainerDestructor.find(".Abort("), std::string::npos);
	EXPECT_NE(ContainerDestructor.find("dbg_assert(m_pLoadJob == nullptr"), std::string::npos);
	EXPECT_NE(Shutdown.find("m_pSkinDirectoryScanJob = nullptr;"), std::string::npos);
	EXPECT_NE(Shutdown.find("m_pSkinListPlanJob = nullptr;"), std::string::npos);
	EXPECT_NE(Shutdown.find("m_pOfficialSkinIndexRequest = nullptr;"), std::string::npos);
	EXPECT_NE(Shutdown.find("pSkinContainer->m_pLoadJob = nullptr;"), std::string::npos);
}
