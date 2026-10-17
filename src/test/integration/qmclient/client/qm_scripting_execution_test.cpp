#include <engine/storage.h>

#include <game/client/components/qmclient/scripting/impl.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	class CQmScriptingExecution : public testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::vector<std::string> m_vRecorded;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}

		void Write(const char *pFilename, const char *pText)
		{
			std::ofstream File(std::filesystem::path(m_Info.StoragePath()) / pFilename, std::ios::binary);
			ASSERT_TRUE(File.is_open());
			File << pText;
			ASSERT_TRUE(File.good());
		}

		void Run(const char *pFilename, const char *pArgs = "")
		{
			// 控制台每次执行都会新建解释器，失败后重试遵循同样的生命周期。
			CScriptingCtx Context;
			Context.AddFunction("record", [this](const std::string &Value) { m_vRecorded.push_back(Value); });
			Context.AddFunction("fail_native", [](const std::string &) { throw std::runtime_error("native failure"); });
			Context.AddFunction("fail_allocation", [](const std::string &) { throw std::bad_alloc(); });
			Context.Run(m_pStorage.get(), pFilename, pArgs);
		}
	};
}

TEST_F(CQmScriptingExecution, MainAndIncludedScriptsRetainArgumentsAndReturnValues)
{
	Write("main.chai", "record(args); record(include(\"value.chai\")); record(\"done\");");
	Write("value.chai", "record(\"included\"); \"result\";");
	ASSERT_NO_THROW(Run("main.chai", "argument text"));
	EXPECT_EQ(m_vRecorded, (std::vector<std::string>{"argument text", "included", "result", "done"}));
}

TEST_F(CQmScriptingExecution, MissingIncludeStopsOuterScriptAndAllowsNextCommand)
{
	Write("main.chai", "record(\"before\"); include(\"missing.chai\"); record(\"after\");");
	ASSERT_NO_THROW(Run("main.chai"));
	EXPECT_EQ(m_vRecorded, (std::vector<std::string>{"before"}));
	Write("missing.chai", "record(\"created\");");
	ASSERT_NO_THROW(Run("main.chai"));
	EXPECT_EQ(m_vRecorded, (std::vector<std::string>{"before", "before", "created", "after"}));
}

TEST_F(CQmScriptingExecution, EmptyIncludeStopsOuterScript)
{
	Write("main.chai", "record(\"before\"); include(\"empty.chai\"); record(\"after\");");
	Write("empty.chai", "");
	ASSERT_NO_THROW(Run("main.chai"));
	EXPECT_EQ(m_vRecorded, (std::vector<std::string>{"before"}));
}

TEST_F(CQmScriptingExecution, IncludedSyntaxErrorReturnsToCallerWithoutRunningOuterTail)
{
	Write("main.chai", "record(\"before\"); include(\"syntax.chai\"); record(\"after\");");
	Write("syntax.chai", "var = ;");
	ASSERT_NO_THROW(Run("main.chai"));
	EXPECT_EQ(m_vRecorded, (std::vector<std::string>{"before"}));
}

TEST_F(CQmScriptingExecution, ThrownScriptValueStopsNestedExecution)
{
	Write("main.chai", "record(\"before\"); include(\"throw.chai\"); record(\"after\");");
	Write("throw.chai", "record(\"included\"); throw(\"script failure\");");
	ASSERT_NO_THROW(Run("main.chai"));
	EXPECT_EQ(m_vRecorded, (std::vector<std::string>{"before", "included"}));
}

TEST_F(CQmScriptingExecution, NativeExceptionStopsScriptWithoutEscapingRun)
{
	Write("main.chai", "record(\"before\"); fail_native(\"\"); record(\"after\");");
	ASSERT_NO_THROW(Run("main.chai"));
	EXPECT_EQ(m_vRecorded, (std::vector<std::string>{"before"}));
}

TEST_F(CQmScriptingExecution, NativeAllocationFailureStopsScriptWithoutEscapingRun)
{
	Write("main.chai", "record(\"before\"); fail_allocation(\"\"); record(\"after\");");
	ASSERT_NO_THROW(Run("main.chai"));
	EXPECT_EQ(m_vRecorded, (std::vector<std::string>{"before"}));
}

TEST_F(CQmScriptingExecution, CaughtIncludeFailureCanBeFollowedBySuccessfulInclude)
{
	Write("main.chai", "try { include(\"throw.chai\"); } catch(e) { record(\"caught\"); } record(include(\"good.chai\"));");
	Write("throw.chai", "throw(\"script failure\");");
	Write("good.chai", "\"recovered\";");
	ASSERT_NO_THROW(Run("main.chai"));
	EXPECT_EQ(m_vRecorded, (std::vector<std::string>{"caught", "recovered"}));
}
