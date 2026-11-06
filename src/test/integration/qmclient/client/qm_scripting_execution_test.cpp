#include <engine/storage.h>

#include <game/client/components/qmclient/scripting/impl.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
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
		size_t m_MaxRecords = std::numeric_limits<size_t>::max();
		int m_InitializedContexts = 0;

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
			CScriptingCtx::Execute(m_pStorage.get(), pFilename, pArgs, [this](CScriptingCtx &Context) {
				++m_InitializedContexts;
				Context.AddFunction("record", [this](const std::string &Value) {
					// 保护回归测试自身：生产限制失效时也在有限层数后停止递归。
					if(m_vRecorded.size() >= m_MaxRecords)
						throw std::runtime_error("test recursion budget exceeded");
					m_vRecorded.push_back(Value);
				});
				Context.AddFunction("run", [this](const std::string &Filename) { Run(Filename.c_str()); });
				Context.AddFunction("fail_native", [](const std::string &) { throw std::runtime_error("native failure"); });
				Context.AddFunction("fail_allocation", [](const std::string &) { throw std::bad_alloc(); });
			});
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

TEST_F(CQmScriptingExecution, RecursiveIncludeStopsAtLimitAndNextCommandRecovers)
{
	m_MaxRecords = CQmScriptExecutionScope::MAX_DEPTH + 1;
	Write("loop.chai", "record(\"entered\"); include(\"loop.chai\");");
	ASSERT_NO_THROW(Run("loop.chai"));
	EXPECT_EQ(m_vRecorded, std::vector<std::string>(CQmScriptExecutionScope::MAX_DEPTH, "entered"));
	Write("next.chai", "record(\"recovered\");");
	ASSERT_NO_THROW(Run("next.chai"));
	ASSERT_EQ(m_vRecorded.size(), CQmScriptExecutionScope::MAX_DEPTH + 1);
	EXPECT_EQ(m_vRecorded.back(), "recovered");
}

TEST_F(CQmScriptingExecution, MutualIncludesShareOneDepthLimit)
{
	m_MaxRecords = CQmScriptExecutionScope::MAX_DEPTH + 1;
	Write("first.chai", "record(\"first\"); include(\"second.chai\");");
	Write("second.chai", "record(\"second\"); include(\"first.chai\");");
	ASSERT_NO_THROW(Run("first.chai"));
	ASSERT_EQ(m_vRecorded.size(), CQmScriptExecutionScope::MAX_DEPTH);
	for(size_t i = 0; i < m_vRecorded.size(); ++i)
		EXPECT_EQ(m_vRecorded[i], i % 2 == 0 ? "first" : "second");
}

TEST_F(CQmScriptingExecution, ReentrantCommandsCannotResetBudgetByCreatingNewInterpreters)
{
	m_MaxRecords = CQmScriptExecutionScope::MAX_DEPTH + 1;
	Write("command.chai", "record(\"entered\"); run(\"command.chai\");");
	ASSERT_NO_THROW(Run("command.chai"));
	EXPECT_EQ(m_InitializedContexts, CQmScriptExecutionScope::MAX_DEPTH);
	EXPECT_EQ(m_vRecorded, std::vector<std::string>(CQmScriptExecutionScope::MAX_DEPTH, "entered"));
}

TEST_F(CQmScriptingExecution, IncludeAndCommandReentryConsumeTheSameBudget)
{
	m_MaxRecords = CQmScriptExecutionScope::MAX_DEPTH + 1;
	Write("command.chai", "record(\"command\"); include(\"included.chai\");");
	Write("included.chai", "record(\"included\"); run(\"command.chai\");");
	ASSERT_NO_THROW(Run("command.chai"));
	ASSERT_EQ(m_vRecorded.size(), CQmScriptExecutionScope::MAX_DEPTH);
	EXPECT_EQ(m_InitializedContexts, CQmScriptExecutionScope::MAX_DEPTH / 2);
	for(size_t i = 0; i < m_vRecorded.size(); ++i)
		EXPECT_EQ(m_vRecorded[i], i % 2 == 0 ? "command" : "included");
}

TEST_F(CQmScriptingExecution, SequentialIncludesReuseDepthAfterReturning)
{
	const int Includes = CQmScriptExecutionScope::MAX_DEPTH + 3;
	std::string Main;
	for(int i = 0; i < Includes; ++i)
		Main += "include(\"value.chai\");\n";
	Write("main.chai", Main.c_str());
	Write("value.chai", "record(\"value\");");
	ASSERT_NO_THROW(Run("main.chai"));
	EXPECT_EQ(m_vRecorded, std::vector<std::string>(Includes, "value"));
}

TEST_F(CQmScriptingExecution, ScriptCanCatchIncludeDepthErrorAndUnwindNormally)
{
	m_MaxRecords = 2 * CQmScriptExecutionScope::MAX_DEPTH + 3;
	Write("loop.chai", "record(\"entered\"); try { include(\"loop.chai\"); } catch(e) { record(\"caught\"); } record(\"returned\");");
	ASSERT_NO_THROW(Run("loop.chai"));
	EXPECT_EQ(std::count(m_vRecorded.begin(), m_vRecorded.end(), "entered"), CQmScriptExecutionScope::MAX_DEPTH);
	EXPECT_EQ(std::count(m_vRecorded.begin(), m_vRecorded.end(), "caught"), 1);
	EXPECT_EQ(std::count(m_vRecorded.begin(), m_vRecorded.end(), "returned"), CQmScriptExecutionScope::MAX_DEPTH);
}

TEST_F(CQmScriptingExecution, ErrorFormattingCannotReenterWithAResetBudget)
{
	m_MaxRecords = CQmScriptExecutionScope::MAX_DEPTH + 1;
	Write("report.chai", "class ReportError { def ReportError() {} }\n"
			     "def to_string(ReportError error) { run(\"report.chai\"); return \"report error\"; }\n"
			     "record(\"entered\"); throw(ReportError());");
	ASSERT_NO_THROW(Run("report.chai"));
	EXPECT_EQ(m_InitializedContexts, CQmScriptExecutionScope::MAX_DEPTH);
	EXPECT_EQ(m_vRecorded, std::vector<std::string>(CQmScriptExecutionScope::MAX_DEPTH, "entered"));
}

TEST_F(CQmScriptingExecution, SetupFailureReleasesRegisteredCallbacksAndAllowsNextCommand)
{
	auto pLifetime = std::make_shared<int>(0);
	Write("main.chai", "record(\"executed\");");
	ASSERT_NO_THROW(CScriptingCtx::Execute(m_pStorage.get(), "main.chai", "", [&](CScriptingCtx &Context) {
		Context.AddFunction("keep_alive", [pLifetime](const std::string &) {});
		EXPECT_GT(pLifetime.use_count(), 1);
		throw std::bad_alloc();
	}));
	EXPECT_EQ(pLifetime.use_count(), 1);
	EXPECT_TRUE(m_vRecorded.empty());
	ASSERT_NO_THROW(Run("main.chai"));
	EXPECT_EQ(m_vRecorded, (std::vector<std::string>{"executed"}));
}
