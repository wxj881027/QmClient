#include <base/system.h>

#include <game/client/components/qmclient/translate/translate_backend_http.h>

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <string>

namespace
{
	void ExpectCode(const char *pBody, bool Valid, int Expected = 0)
	{
		const std::unique_ptr<json_value, decltype(&json_value_free)> pJson(json_parse(pBody, str_length(pBody)), json_value_free);
		ASSERT_NE(pJson, nullptr);
		int Code = 12345;
		EXPECT_EQ(TryParseTranslateServiceCode(pJson.get(), Code), Valid);
		EXPECT_EQ(Code, Valid ? Expected : 12345);
	}
}

TEST(TranslateServiceCode, IntegerLimitsAreAcceptedExactly)
{
	for(int Code : {std::numeric_limits<int>::min(), 200, 52000, std::numeric_limits<int>::max()})
	{
		SCOPED_TRACE(Code);
		ExpectCode(std::to_string(Code).c_str(), true, Code);
	}
}

TEST(TranslateServiceCode, WideIntegersCannotWrapIntoValidStatus)
{
	for(const char *pBody : {"2147483648", "-2147483649", "4294967496", "4295019296"})
	{
		SCOPED_TRACE(pBody);
		ExpectCode(pBody, false);
	}
}

TEST(TranslateServiceCode, WholeDoublesWithinRangeAreAccepted)
{
	ExpectCode("200.0", true, 200);
	ExpectCode("2147483647.0", true, std::numeric_limits<int>::max());
	ExpectCode("-2147483648.0", true, std::numeric_limits<int>::min());
}

TEST(TranslateServiceCode, FractionalAndOutOfRangeDoublesAreRejected)
{
	for(const char *pBody : {"200.5", "200.00000001", "1e100", "-1e100", "2147483648.0", "-2147483649.0"})
	{
		SCOPED_TRACE(pBody);
		ExpectCode(pBody, false);
	}
}

TEST(TranslateServiceCode, NonfiniteTypedValuesAreRejectedWithoutModifyingOutput)
{
	for(double Value : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		json_value Json{};
		Json.type = json_double;
		Json.u.dbl = Value;
		int Code = 12345;
		EXPECT_FALSE(TryParseTranslateServiceCode(&Json, Code));
		EXPECT_EQ(Code, 12345);
	}
}

TEST(TranslateServiceCode, CompleteDecimalStringsAreAccepted)
{
	ExpectCode(R"("200")", true, 200);
	ExpectCode(R"("52000")", true, 52000);
	ExpectCode(R"("-1")", true, -1);
	ExpectCode(R"("2147483647")", true, std::numeric_limits<int>::max());
}

TEST(TranslateServiceCode, InvalidOrOverflowingDecimalStringsAreRejected)
{
	for(const char *pBody : {R"("4295019296")", R"("52000junk")", R"("52000\u0000junk")", R"("")", R"(" 52000")", R"("+52000")"})
	{
		SCOPED_TRACE(pBody);
		ExpectCode(pBody, false);
	}
}

TEST(TranslateServiceCode, MissingAndNonnumericValuesAreRejected)
{
	for(const char *pBody : {"null", "true", "{}", "[]"})
	{
		SCOPED_TRACE(pBody);
		ExpectCode(pBody, false);
	}
	int Code = 12345;
	EXPECT_FALSE(TryParseTranslateServiceCode(nullptr, Code));
	EXPECT_FALSE(TryParseTranslateServiceCode(&json_value_none, Code));
	EXPECT_EQ(Code, 12345);
}
