#include <base/detect.h>

#include <gtest/gtest.h>

#if defined(_M_ARM64)
TEST(DetectContract, DetectsNativeWindowsArm64)
{
#if !defined(CONF_ARCH_ARM64) || defined(CONF_ARCH_AMD64)
	ADD_FAILURE() << "Expected the ARM64 architecture flag";
#endif
	EXPECT_STREQ(CONF_ARCH_STRING, "arm64");
}
#endif
