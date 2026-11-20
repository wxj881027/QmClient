#include <base/detect.h>

#if defined(CONF_PLATFORM_IOS)
#include <gtest/gtest.h>
#include <ios/ios_main.h>

TEST(IosCutoutInsets, MissingNativeWindowUsesZeroInsets)
{
	int Left = 12;
	int Right = 20;
	IosDisplayCutoutInsets(nullptr, &Left, &Right);
	EXPECT_EQ(Left, 0);
	EXPECT_EQ(Right, 0);
}
#endif
