#include <gtest/gtest.h>
#include <test/support/script_allocation_failure.h>

#include <cstdlib>
#include <exception>
#include <language/chaiscript_common.hpp>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using CError = chaiscript::exception::eval_error;

	// 错误格式化只借用调度器的类型查询，测试不会调用解析器。
	class CUnusedParser : public chaiscript::parser::ChaiScript_Parser_Base
	{
	public:
		chaiscript::AST_NodePtr parse(const std::string &, const std::string &) override { return nullptr; }
		void debug_print(const chaiscript::AST_Node &, std::string) const override {}
		void *get_tracer_ptr() override { return nullptr; }
	};

	template<typename TOperation, typename TRecovery>
	[[noreturn]] void CheckFailureAndRecovery(TOperation Operation, TRecovery Recovery)
	{
		// 回归到 noexcept 时让子进程返回失败码，避免系统崩溃对话框。
		std::set_terminate([] { std::_Exit(6); });
		bool Caught = false;
		bool Triggered = false;
		{
			CQmScriptAllocationFailure Failure;
			try
			{
				Operation();
			}
			catch(const std::bad_alloc &)
			{
				Caught = true;
			}
			catch(...)
			{
				std::_Exit(5);
			}
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
			std::_Exit(2);
		if(!Caught)
			std::_Exit(3);
		try
		{
			if(!Recovery())
				std::_Exit(4);
		}
		catch(...)
		{
			std::_Exit(5);
		}
		std::_Exit(0);
	}

	struct CErrorInputs
	{
		// 长消息确保被测构造函数发生真实分配，不依赖字符串的小对象容量。
		const std::string m_Reason = std::string(4096, 'r');
		const std::string m_Filename = "script.chai";
		const chaiscript::File_Position m_Position{7, 9};
		CUnusedParser m_Parser;
		chaiscript::detail::Dispatch_Engine m_Engine{m_Parser};
		const std::vector<chaiscript::Boxed_Value> m_vParameters;
		const std::vector<chaiscript::Const_Proxy_Function> m_vFunctions;

		bool MessageMatches(const CError &Error) const
		{
			return Error.reason == m_Reason && std::string_view(Error.what()).find(m_Reason) != std::string_view::npos;
		}
		bool LocationMatches(const CError &Error) const
		{
			return Error.filename == m_Filename && Error.start_position.line == 7 && Error.start_position.column == 9;
		}
	};
}

TEST(QmScriptingAllocationDeathTest, MessageErrorPropagatesAllocationFailureAndRecovers)
{
	EXPECT_EXIT(
		{
			CErrorInputs Inputs;
			CheckFailureAndRecovery([&] { CError Error(Inputs.m_Reason); }, [&] { return Inputs.MessageMatches(CError(Inputs.m_Reason)); });
		},
		::testing::ExitedWithCode(0), "");
}

TEST(QmScriptingAllocationDeathTest, LocatedErrorPropagatesAllocationFailureAndRecovers)
{
	EXPECT_EXIT(
		{
			CErrorInputs Inputs;
			const auto Construct = [&] { return CError(Inputs.m_Reason, Inputs.m_Position, Inputs.m_Filename); };
			CheckFailureAndRecovery(Construct, [&] {
				const auto Error = Construct();
				return Inputs.MessageMatches(Error) && Inputs.LocationMatches(Error);
			});
		},
		::testing::ExitedWithCode(0), "");
}

TEST(QmScriptingAllocationDeathTest, DispatchErrorPropagatesAllocationFailureAndRecovers)
{
	EXPECT_EXIT(
		{
			CErrorInputs Inputs;
			const auto Construct = [&] { return CError(Inputs.m_Reason, Inputs.m_vParameters, Inputs.m_vFunctions, false, Inputs.m_Engine); };
			CheckFailureAndRecovery(Construct, [&] {
				const auto Error = Construct();
				return Inputs.MessageMatches(Error) && Error.detail.find("0 overloads") != std::string::npos;
			});
		},
		::testing::ExitedWithCode(0), "");
}

TEST(QmScriptingAllocationDeathTest, LocatedDispatchErrorPropagatesAllocationFailureAndRecovers)
{
	EXPECT_EXIT(
		{
			CErrorInputs Inputs;
			const auto Construct = [&] { return CError(Inputs.m_Reason, Inputs.m_Position, Inputs.m_Filename, Inputs.m_vParameters, Inputs.m_vFunctions, false, Inputs.m_Engine); };
			CheckFailureAndRecovery(Construct, [&] {
				const auto Error = Construct();
				return Inputs.MessageMatches(Error) && Inputs.LocationMatches(Error) && Error.detail.find("0 overloads") != std::string::npos;
			});
		},
		::testing::ExitedWithCode(0), "");
}

TEST(QmScriptingAllocationDeathTest, ReservedWordTableCanRetryAfterFirstAllocationFails)
{
	EXPECT_EXIT(
		{
			const std::string Name = "def";
			CheckFailureAndRecovery([&] { chaiscript::Name_Validator::is_reserved_word(Name); }, [&] { return chaiscript::Name_Validator::is_reserved_word(Name) && !chaiscript::Name_Validator::is_reserved_word(std::string("client_var")); });
		},
		::testing::ExitedWithCode(0), "");
}

TEST(QmScriptingAllocationDeathTest, ParserNameValidationPropagatesTableInitializationFailure)
{
	EXPECT_EXIT(
		{
			// 解析器使用 string_view，此实例的静态表需要独立验证首次初始化。
			const std::string_view Name = "client_var";
			CheckFailureAndRecovery([&] { chaiscript::Name_Validator::valid_object_name(Name); }, [&] { return chaiscript::Name_Validator::valid_object_name(Name) &&
															   !chaiscript::Name_Validator::valid_object_name(std::string_view("def")) &&
															   !chaiscript::Name_Validator::valid_object_name(std::string_view("Game::State")); });
		},
		::testing::ExitedWithCode(0), "");
}
