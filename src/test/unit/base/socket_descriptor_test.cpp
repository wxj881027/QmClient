#include <base/system.h>

#include <gtest/gtest.h>

#include <limits>
#if defined(CONF_FAMILY_UNIX)
#include <sys/select.h>
#endif

TEST(SocketDescriptor, NegativeDescriptorsAreNeverSelectable)
{
	EXPECT_FALSE(net_socket_is_selectable(-1));
	EXPECT_FALSE(net_socket_is_selectable(std::numeric_limits<int>::min()));
}

#if defined(CONF_FAMILY_UNIX)
TEST(SocketDescriptor, UnixDescriptorsMustFitTheSelectBitset)
{
	EXPECT_TRUE(net_socket_is_selectable(0));
	EXPECT_TRUE(net_socket_is_selectable(FD_SETSIZE - 1));
	EXPECT_FALSE(net_socket_is_selectable(FD_SETSIZE));
	EXPECT_FALSE(net_socket_is_selectable(std::numeric_limits<int>::max()));
}
#endif

#if defined(CONF_FAMILY_WINDOWS)
TEST(SocketDescriptor, WindowsDescriptorNumbersDoNotUseTheUnixBitsetLimit)
{
	EXPECT_TRUE(net_socket_is_selectable(0));
	EXPECT_TRUE(net_socket_is_selectable(std::numeric_limits<int>::max()));
}
#endif
