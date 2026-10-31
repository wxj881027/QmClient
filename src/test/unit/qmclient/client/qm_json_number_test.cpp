#include <game/client/components/qmclient/qm_json_fields.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <utility>

TEST(QmJsonNumber, ZeroAndMaximumIntegerArePreserved)
{
	json_value Value;
	Value.type = json_integer;
	for(const int64_t Number : {int64_t{0}, std::numeric_limits<int64_t>::max()})
	{
		Value.u.integer = Number;
		int64_t Out = -1;
		ASSERT_TRUE(qm_json::NonNegativeInt64(&Value, Out));
		EXPECT_EQ(Out, Number);
	}
}

TEST(QmJsonNumber, NegativeIntegerLeavesPreviousOutput)
{
	json_value Value;
	Value.type = json_integer;
	Value.u.integer = -1;
	int64_t Out = 42;
	EXPECT_FALSE(qm_json::NonNegativeInt64(&Value, Out));
	EXPECT_EQ(Out, 42);
}

TEST(QmJsonNumber, FiniteNonnegativeDoubleTruncatesTowardZero)
{
	json_value Value;
	Value.type = json_double;
	for(const auto &[Number, Expected] : {std::pair{0.0, int64_t{0}}, std::pair{-0.0, int64_t{0}}, std::pair{0.75, int64_t{0}}, std::pair{1234.75, int64_t{1234}}})
	{
		SCOPED_TRACE(Number);
		Value.u.dbl = Number;
		int64_t Out = -1;
		ASSERT_TRUE(qm_json::NonNegativeInt64(&Value, Out));
		EXPECT_EQ(Out, Expected);
	}
}

TEST(QmJsonNumber, NegativeFractionIsRejectedBeforeTruncation)
{
	json_value Value;
	Value.type = json_double;
	Value.u.dbl = -0.25;
	int64_t Out = 42;
	EXPECT_FALSE(qm_json::NonNegativeInt64(&Value, Out));
	EXPECT_EQ(Out, 42);
}

TEST(QmJsonNumber, NonfiniteDoubleLeavesPreviousOutput)
{
	json_value Value;
	Value.type = json_double;
	for(const double Number : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		SCOPED_TRACE(Number);
		Value.u.dbl = Number;
		int64_t Out = 42;
		EXPECT_FALSE(qm_json::NonNegativeInt64(&Value, Out));
		EXPECT_EQ(Out, 42);
	}
}

TEST(QmJsonNumber, DoubleAtOrAboveTwoToThe63CannotConvert)
{
	json_value Value;
	Value.type = json_double;
	for(const double Number : {std::ldexp(1.0, 63), static_cast<double>(std::numeric_limits<int64_t>::max()), std::numeric_limits<double>::max()})
	{
		SCOPED_TRACE(Number);
		Value.u.dbl = Number;
		int64_t Out = 42;
		EXPECT_FALSE(qm_json::NonNegativeInt64(&Value, Out));
		EXPECT_EQ(Out, 42);
	}
}

TEST(QmJsonNumber, LargestDoubleBelowTwoToThe63IsRepresentable)
{
	json_value Value;
	Value.type = json_double;
	Value.u.dbl = std::nextafter(std::ldexp(1.0, 63), 0.0);
	int64_t Out = -1;
	ASSERT_TRUE(qm_json::NonNegativeInt64(&Value, Out));
	EXPECT_EQ(Out, std::numeric_limits<int64_t>::max() - 1023);
}

TEST(QmJsonNumber, NullAndOtherJsonTypesDoNotChangeOutput)
{
	int64_t Out = 42;
	EXPECT_FALSE(qm_json::NonNegativeInt64(nullptr, Out));
	EXPECT_EQ(Out, 42);
	for(const json_type Type : {json_none, json_null, json_string, json_boolean, json_array, json_object})
	{
		SCOPED_TRACE(Type);
		json_value Value;
		Value.type = Type;
		EXPECT_FALSE(qm_json::NonNegativeInt64(&Value, Out));
		EXPECT_EQ(Out, 42);
	}
}
