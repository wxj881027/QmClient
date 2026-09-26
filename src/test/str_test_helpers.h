#ifndef TEST_STR_TEST_HELPERS_H
#define TEST_STR_TEST_HELPERS_H

#pragma once

#include <base/str.h>

#include <gtest/gtest.h>

using TStringArgumentFunction = void (*)(char *pStr);

template<TStringArgumentFunction Func>
static void TestInplace(const char *pInput, const char *pOutput)
{
	char aBuf[512];
	str_copy(aBuf, pInput);
	Func(aBuf);
	EXPECT_STREQ(aBuf, pOutput);
}

#endif
