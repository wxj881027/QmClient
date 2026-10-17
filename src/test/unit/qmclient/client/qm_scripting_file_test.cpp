#include <game/client/components/qmclient/scripting/script_file.h>

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
	struct CReleaseScript
	{
		int *m_pLiveSources;
		void operator()(char *pText) const
		{
			--*m_pLiveSources;
			delete[] pText;
		}
	};

	class CQmScriptingFile : public testing::Test
	{
	protected:
		int m_LiveSources = 0;
		using CSource = std::unique_ptr<char, CReleaseScript>;

		CSource Read(const char *pText)
		{
			CSource pSource(nullptr, CReleaseScript{&m_LiveSources});
			if(pText)
			{
				const size_t Length = std::strlen(pText) + 1;
				pSource.reset(new char[Length]);
				++m_LiveSources;
				std::memcpy(pSource.get(), pText, Length);
			}
			return pSource;
		}

		template<typename TEvaluate>
		auto EvaluateFile(const char *pFilename, const char *pText, TEvaluate &&Evaluate)
		{
			const auto ReadSource = [&](const char *pRequestedFilename) {
				EXPECT_STREQ(pRequestedFilename, pFilename);
				return Read(pText);
			};
			return QmEvaluateScriptFile(pFilename, ReadSource, Evaluate);
		}
	};
}

TEST_F(CQmScriptingFile, SuccessfulEvaluationOwnsTextUntilResultIsReturned)
{
	const std::string Result = EvaluateFile("main.chai", "source text", [&](const char *pText) {
		EXPECT_EQ(m_LiveSources, 1);
		return std::string(pText);
	});
	EXPECT_EQ(Result, "source text");
	EXPECT_EQ(m_LiveSources, 0);
}

TEST_F(CQmScriptingFile, EmptyFileIsReleasedBeforeReadErrorReachesCaller)
{
	bool Evaluated = false;
	try
	{
		EvaluateFile("empty.chai", "", [&](const char *) { Evaluated = true; });
		FAIL() << "Expected empty-file error";
	}
	catch(const std::string &Error)
	{
		EXPECT_EQ(Error, "Failed to open script 'empty.chai'");
		EXPECT_EQ(m_LiveSources, 0);
	}
	EXPECT_FALSE(Evaluated);
}

TEST_F(CQmScriptingFile, MissingFileDoesNotEvaluateNullText)
{
	bool Evaluated = false;
	EXPECT_THROW(EvaluateFile("missing.chai", nullptr, [&](const char *) { Evaluated = true; }), std::string);
	EXPECT_FALSE(Evaluated);
	EXPECT_EQ(m_LiveSources, 0);
}

TEST_F(CQmScriptingFile, EvaluationExceptionReleasesSourceBeforeReporting)
{
	try
	{
		EvaluateFile("broken.chai", "broken", [&](const char *) {
			EXPECT_EQ(m_LiveSources, 1);
			throw std::runtime_error("evaluation failed");
		});
		FAIL() << "Expected evaluation error";
	}
	catch(const std::runtime_error &Error)
	{
		EXPECT_STREQ(Error.what(), "evaluation failed");
		EXPECT_EQ(m_LiveSources, 0);
	}
}

TEST_F(CQmScriptingFile, ScriptValueExceptionAlsoReleasesSource)
{
	EXPECT_THROW(EvaluateFile("throw.chai", "throw(42)", [](const char *) { throw 42; }), int);
	EXPECT_EQ(m_LiveSources, 0);
}

TEST_F(CQmScriptingFile, NestedFailureReleasesBothIncludedAndOuterSources)
{
	const auto EvaluateOuter = [&](const char *) {
		EvaluateFile("inner.chai", "failure", [&](const char *) {
			EXPECT_EQ(m_LiveSources, 2);
			throw std::runtime_error("included failure");
		});
	};
	EXPECT_THROW(EvaluateFile("outer.chai", "include", EvaluateOuter), std::runtime_error);
	EXPECT_EQ(m_LiveSources, 0);
}

TEST_F(CQmScriptingFile, CaughtIncludeFailureLeavesOuterSourceAliveForNextInclude)
{
	const std::string Result = EvaluateFile("outer.chai", "include twice", [&](const char *) {
		EXPECT_THROW(EvaluateFile("bad.chai", "bad", [](const char *) { throw std::runtime_error("failed"); }), std::runtime_error);
		EXPECT_EQ(m_LiveSources, 1);
		return EvaluateFile("good.chai", "recovered", [&](const char *pText) {
			EXPECT_EQ(m_LiveSources, 2);
			return std::string(pText);
		});
	});
	EXPECT_EQ(Result, "recovered");
	EXPECT_EQ(m_LiveSources, 0);
}
