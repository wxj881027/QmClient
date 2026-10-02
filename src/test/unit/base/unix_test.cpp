// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <gtest/gtest.h>

#if defined(CONF_FAMILY_UNIX)
TEST(Unix, Create)
{
	UNIXSOCKET Socket = net_unix_create_unnamed();
	ASSERT_GE(Socket, 0);
	net_unix_close(Socket);
}
#endif
