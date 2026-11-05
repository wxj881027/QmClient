#include <game/client/components/qmclient/statistics_file.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>

namespace
{
	using CRoot = std::unique_ptr<json_value, decltype(&json_value_free)>;
	constexpr char DOCUMENT[] = R"({"local":{"modes":[]},"remote":{}})";
	auto Reader(std::string_view Text)
	{
		return [Text](void **ppData, unsigned *pSize) {
			*ppData = std::malloc(Text.size() + 1);
			if(!*ppData)
				throw std::bad_alloc();
			if(!Text.empty())
				std::memcpy(*ppData, Text.data(), Text.size());
			static_cast<char *>(*ppData)[Text.size()] = '\0';
			*pSize = static_cast<unsigned>(Text.size());
			return IStorage::EReadFileResult::SUCCESS;
		};
	}
}

TEST(QmStatisticsDocumentRead, OnlyConfirmedMissingReturnsNotFound)
{
	const IStorage::EReadFileResult Results[] = {IStorage::EReadFileResult::NOT_FOUND, IStorage::EReadFileResult::IO_FAILURE, IStorage::EReadFileResult::UNSUPPORTED};
	for(const auto Result : Results)
	{
		SCOPED_TRACE(static_cast<int>(Result));
		json_value Dummy;
		json_value *pRoot = &Dummy;
		EXPECT_EQ(QmLoadStatisticsFile(&pRoot, [Result](void **, unsigned *) { return Result; }),
			Result == IStorage::EReadFileResult::NOT_FOUND ? EQmStatisticsFileLoadResult::NOT_FOUND : EQmStatisticsFileLoadResult::INVALID);
		EXPECT_EQ(pRoot, nullptr);
	}
}

TEST(QmStatisticsDocumentRead, ReaderExceptionDoesNotEscapeOrPublishRoot)
{
	json_value *pRoot = nullptr;
	EXPECT_EQ(QmLoadStatisticsFile(&pRoot, [](void **, unsigned *) -> IStorage::EReadFileResult { throw std::bad_alloc(); }), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);
}

TEST(QmStatisticsDocumentRead, ExceptionAfterBufferTransferDoesNotPublishPartialRoot)
{
	json_value *pRoot = nullptr;
	EXPECT_EQ(QmLoadStatisticsFile(&pRoot, [](void **ppData, unsigned *pSize) -> IStorage::EReadFileResult {
		Reader(DOCUMENT)(ppData, pSize);
		throw 7;
	}),
		EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);
	ASSERT_EQ(QmLoadStatisticsFile(&pRoot, Reader(DOCUMENT)), EQmStatisticsFileLoadResult::VALID);
	CRoot Root(pRoot, json_value_free);
	ASSERT_NE(Root, nullptr);
}

TEST(QmStatisticsDocumentRead, EmptyOrEmbeddedZeroInputIsInvalid)
{
	const std::string WithZero = std::string(DOCUMENT) + '\0' + DOCUMENT;
	const std::string_view Inputs[] = {"", WithZero};
	for(const auto Input : Inputs)
	{
		json_value *pRoot = nullptr;
		EXPECT_EQ(QmLoadStatisticsFile(&pRoot, Reader(Input)), EQmStatisticsFileLoadResult::INVALID);
		EXPECT_EQ(pRoot, nullptr);
	}
}

TEST(QmStatisticsDocumentRead, FullMemberNameRequiredForDocumentSections)
{
	constexpr char INVALID[] = R"({"local\u0000other":{},"remote":{}})";
	json_value *pRoot = nullptr;
	EXPECT_EQ(QmLoadStatisticsFile(&pRoot, Reader(INVALID)), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_EQ(pRoot, nullptr);
	constexpr char VALID[] = R"({"local\u0000other":false,"local":{},"remote":{}})";
	ASSERT_EQ(QmLoadStatisticsFile(&pRoot, Reader(VALID)), EQmStatisticsFileLoadResult::VALID);
	CRoot Root(pRoot, json_value_free);
	EXPECT_EQ(qm_json::Field(Root.get(), "local")->type, json_object);
}

TEST(QmStatisticsDocumentRead, NullOutputDoesNotInvokeReader)
{
	bool Called = false;
	EXPECT_EQ(QmLoadStatisticsFile(nullptr, [&Called](void **, unsigned *) { Called = true; return IStorage::EReadFileResult::NOT_FOUND; }), EQmStatisticsFileLoadResult::INVALID);
	EXPECT_FALSE(Called);
}

TEST(QmStatisticsDocumentRead, EveryValidationAllocationFailureCanRetryWithoutPublishingRoot)
{
	int Failures = 0;
	bool Completed = false;
	for(size_t Budget = 0; Budget < 64; ++Budget)
	{
		SCOPED_TRACE(Budget);
		json_value *pRoot = nullptr;
		EQmStatisticsFileLoadResult Result;
		bool Triggered;
		{
			CPredictionAllocationFailure Fault(Budget);
			Result = QmLoadStatisticsFile(&pRoot, Reader(DOCUMENT));
			Triggered = Fault.Triggered();
		}
		CRoot Root(pRoot, json_value_free);
		if(!Triggered)
		{
			EXPECT_EQ(Result, EQmStatisticsFileLoadResult::VALID);
			EXPECT_NE(Root, nullptr);
			Completed = true;
			break;
		}
		++Failures;
		EXPECT_EQ(Result, EQmStatisticsFileLoadResult::INVALID);
		EXPECT_EQ(Root, nullptr);
		ASSERT_EQ(QmLoadStatisticsFile(&pRoot, Reader(DOCUMENT)), EQmStatisticsFileLoadResult::VALID);
		Root.reset(pRoot);
	}
	EXPECT_GT(Failures, 0);
	EXPECT_TRUE(Completed);
}

TEST(QmStatisticsStoredInteger, CanonicalDecimalStringsRestoreFullPrecision)
{
	const char *apJson[] = {"\"0\"", "\"9007199254740993\"", "\"9223372036854775807\""};
	const int64_t Expected[] = {0, INT64_C(9007199254740993), std::numeric_limits<int64_t>::max()};
	for(size_t Index = 0; Index < std::size(apJson); ++Index)
	{
		SCOPED_TRACE(apJson[Index]);
		CRoot Root(JsonParse(apJson[Index], std::strlen(apJson[Index])), json_value_free);
		int64_t Value = -1;
		ASSERT_TRUE(QmStatisticsNonNegativeInt64(Root.get(), Value));
		EXPECT_EQ(Value, Expected[Index]);
	}
}

TEST(QmStatisticsStoredInteger, LegacyJsonNumbersKeepExistingTruncation)
{
	CRoot Integer(JsonParse("42", 2), json_value_free);
	CRoot Decimal(JsonParse("42.75", 5), json_value_free);
	int64_t Value = -1;
	ASSERT_TRUE(QmStatisticsNonNegativeInt64(Integer.get(), Value));
	EXPECT_EQ(Value, 42);
	ASSERT_TRUE(QmStatisticsNonNegativeInt64(Decimal.get(), Value));
	EXPECT_EQ(Value, 42);
}

TEST(QmStatisticsStoredInteger, InvalidDecimalStringsLeaveOutputUnchanged)
{
	const char *apJson[] = {"\"\"", "\"-1\"", "\"+1\"", "\"01\"", "\" 1\"", "\"1.0\"", "\"9223372036854775808\"", "\"18446744073709551616\"", "\"12\\u0000other\""};
	for(const char *pJson : apJson)
	{
		SCOPED_TRACE(pJson);
		CRoot Root(JsonParse(pJson, std::strlen(pJson)), json_value_free);
		int64_t Value = 99;
		EXPECT_FALSE(QmStatisticsNonNegativeInt64(Root.get(), Value));
		EXPECT_EQ(Value, 99);
	}
}

TEST(QmStatisticsStoredInteger, InvalidNumericTypesLeaveOutputUnchanged)
{
	const char *apJson[] = {"-1", "9223372036854775808.0", "true", "null", "{}", "[]"};
	for(const char *pJson : apJson)
	{
		SCOPED_TRACE(pJson);
		CRoot Root(JsonParse(pJson, std::strlen(pJson)), json_value_free);
		int64_t Value = 99;
		EXPECT_FALSE(QmStatisticsNonNegativeInt64(Root.get(), Value));
		EXPECT_EQ(Value, 99);
	}
}
