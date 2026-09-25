// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <base/system.h>

#include <engine/server/databases/connection.h>

#include <gtest/gtest.h>

#include <memory>

// GetOptionalFloat 必须走 GetFloat：此前误用 GetInt，会把小数位截断
// （对应上游 d1c518a5f9 的修复，本地在 S10 手工等价落地）。
// 注意列号是 1-based（sqlite 实现里用 Col - 1）。
TEST(DbConnection, OptionalFloatKeepsFractionalPart)
{
	std::unique_ptr<IDbConnection> pConn = CreateSqliteConnection(":memory:", true);
	ASSERT_NE(pConn, nullptr);

	char aError[256];
	ASSERT_TRUE(pConn->Connect(aError, sizeof(aError))) << aError;

	ASSERT_TRUE(pConn->PrepareStatement("SELECT CAST(42.87 AS REAL), NULL, 7", aError, sizeof(aError))) << aError;

	bool End = true;
	ASSERT_TRUE(pConn->Step(&End, aError, sizeof(aError))) << aError;
	ASSERT_FALSE(End);

	// 关键断言：小数位不能被丢掉
	const std::optional<float> Value = pConn->GetOptionalFloat(1);
	ASSERT_TRUE(Value.has_value());
	EXPECT_NEAR(Value.value(), 42.87f, 0.0001f);
	// int getter 仍按整数语义截断（用于对照，说明两者语义不同）
	const std::optional<int> IntValue = pConn->GetOptionalInt(1);
	ASSERT_TRUE(IntValue.has_value());
	EXPECT_EQ(IntValue.value(), 42);

	// NULL 列返回 nullopt
	EXPECT_FALSE(pConn->GetOptionalFloat(2).has_value());
	EXPECT_FALSE(pConn->GetOptionalInt(2).has_value());

	// 整数值不受影响
	const std::optional<float> Integer = pConn->GetOptionalFloat(3);
	ASSERT_TRUE(Integer.has_value());
	EXPECT_FLOAT_EQ(Integer.value(), 7.0f);

	pConn->Disconnect();
}

// 负数与零值也要按浮点语义返回
TEST(DbConnection, OptionalFloatHandlesNegativeAndZero)
{
	std::unique_ptr<IDbConnection> pConn = CreateSqliteConnection(":memory:", true);
	ASSERT_NE(pConn, nullptr);

	char aError[256];
	ASSERT_TRUE(pConn->Connect(aError, sizeof(aError))) << aError;
	ASSERT_TRUE(pConn->PrepareStatement("SELECT CAST(-0.5 AS REAL), CAST(0 AS REAL)", aError, sizeof(aError))) << aError;

	bool End = true;
	ASSERT_TRUE(pConn->Step(&End, aError, sizeof(aError))) << aError;
	ASSERT_FALSE(End);

	const std::optional<float> Negative = pConn->GetOptionalFloat(1);
	ASSERT_TRUE(Negative.has_value());
	EXPECT_FLOAT_EQ(Negative.value(), -0.5f);

	const std::optional<float> Zero = pConn->GetOptionalFloat(2);
	ASSERT_TRUE(Zero.has_value());
	EXPECT_FLOAT_EQ(Zero.value(), 0.0f);

	pConn->Disconnect();
}
