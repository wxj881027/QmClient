#include <base/windows.h>

#include <gtest/gtest.h>

#if defined(CONF_FAMILY_WINDOWS)

#include <array>
#include <iterator>
#include <limits>

TEST(WindowsRegistryString, TerminatedAsciiRetainsCaseAndBytes)
{
	const wchar_t aValue[] = L"AbC-123";
	const auto Result = windows_registry_string_to_utf8(aValue, std::size(aValue), sizeof(aValue));
	ASSERT_TRUE(Result.has_value());
	EXPECT_EQ(*Result, "AbC-123");
}

TEST(WindowsRegistryString, UnicodeAndSurrogatePairBecomeUtf8)
{
	const wchar_t aValue[] = {0x4e2d, 0x6587, 0xd83d, 0xde00, 0};
	const auto Result = windows_registry_string_to_utf8(aValue, std::size(aValue), sizeof(aValue));
	ASSERT_TRUE(Result.has_value());
	EXPECT_EQ(*Result, "中文😀");
}

TEST(WindowsRegistryString, MissingBufferOrReturnedExtentIsRejected)
{
	const wchar_t aValue[] = L"a";
	EXPECT_FALSE(windows_registry_string_to_utf8(nullptr, 2, sizeof(aValue)).has_value());
	EXPECT_FALSE(windows_registry_string_to_utf8(aValue, std::size(aValue), 0).has_value());
}

TEST(WindowsRegistryString, OddByteLengthIsRejected)
{
	const wchar_t aValue[] = L"a";
	EXPECT_FALSE(windows_registry_string_to_utf8(aValue, std::size(aValue), sizeof(aValue) - 1).has_value());
}

TEST(WindowsRegistryString, ReturnedBytesCannotExceedCapacity)
{
	const wchar_t aValue[] = L"a";
	EXPECT_FALSE(windows_registry_string_to_utf8(aValue, 1, sizeof(aValue)).has_value());
	EXPECT_FALSE(windows_registry_string_to_utf8(aValue, std::size(aValue), std::numeric_limits<size_t>::max() - 1).has_value());
}

TEST(WindowsRegistryString, FullUnterminatedMachineIdentityBufferIsRejected)
{
	std::array<wchar_t, 256> aValue;
	aValue.fill(L'x');
	EXPECT_FALSE(windows_registry_string_to_utf8(aValue.data(), aValue.size(), aValue.size() * sizeof(wchar_t)).has_value());
}

TEST(WindowsRegistryString, TerminatorOutsideReturnedBytesCannotBeBorrowed)
{
	const wchar_t aValue[] = {L'a', L'b', 0};
	EXPECT_FALSE(windows_registry_string_to_utf8(aValue, std::size(aValue), 2 * sizeof(wchar_t)).has_value());
}

TEST(WindowsRegistryString, InvalidUtf16BeforeTerminatorIsRejected)
{
	const wchar_t aValue[] = {0xd800, L'x', 0};
	EXPECT_FALSE(windows_registry_string_to_utf8(aValue, std::size(aValue), sizeof(aValue)).has_value());
}

TEST(WindowsRegistryString, TerminatedEmptyValueIsDistinctFromInvalidExtent)
{
	const wchar_t aValue[] = {0};
	const auto Result = windows_registry_string_to_utf8(aValue, std::size(aValue), sizeof(aValue));
	ASSERT_TRUE(Result.has_value());
	EXPECT_TRUE(Result->empty());
}

TEST(WindowsRegistryString, FirstTerminatorPreservesExistingStringSemantics)
{
	const wchar_t aValue[] = {L'a', 0, 0xd800};
	const auto Result = windows_registry_string_to_utf8(aValue, std::size(aValue), sizeof(aValue));
	ASSERT_TRUE(Result.has_value());
	EXPECT_EQ(*Result, "a");
}

#endif
