#include <base/system.h>

#include <gtest/gtest.h>

#include <array>

#if defined(CONF_FAMILY_UNIX)
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <vector>
#endif

namespace
{
	void *FailAllocation(size_t) { return nullptr; }

	struct SSocketOwner
	{
		NETSOCKET m_Socket;
		~SSocketOwner()
		{
			if(m_Socket)
				net_udp_close(m_Socket);
		}
	};
}

TEST(NetSocketAllocation, UdpAllocationFailureReturnsNullBeforeBindingAnyAddressFamily)
{
	for(const int Type : std::array<int, 3>{NETTYPE_IPV4, NETTYPE_IPV6, NETTYPE_IPV4 | NETTYPE_IPV6})
	{
		SCOPED_TRACE(Type);
		NETADDR BindAddr = NETADDR_ZEROED;
		BindAddr.type = Type;
		SSocketOwner Socket{net_udp_create_with_allocator(BindAddr, FailAllocation)};
		EXPECT_EQ(Socket.m_Socket, nullptr);
	}
}

TEST(NetSocketAllocation, NativeUdpCreationCanRetryAfterAllocationBoundaryFailure)
{
	NETADDR BindAddr = NETADDR_ZEROED;
	BindAddr.type = NETTYPE_IPV4;
	BindAddr.ip[0] = 127;
	BindAddr.ip[3] = 1;
	SSocketOwner Failed{net_udp_create_with_allocator(BindAddr, FailAllocation)};
	ASSERT_EQ(Failed.m_Socket, nullptr);
	// 系统选择临时端口；不连接互联网，不依赖固定端口或其他进程。
	SSocketOwner Ready{net_udp_create(BindAddr)};
	ASSERT_NE(Ready.m_Socket, nullptr);
	EXPECT_EQ(net_socket_type(Ready.m_Socket), NETTYPE_IPV4);
}

#if defined(CONF_FAMILY_UNIX)
#if GTEST_HAS_DEATH_TEST && !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_EMSCRIPTEN)
TEST(NetSocketAllocationDeathTest, NativeUdpSocketIsCloseOnExec)
{
	ASSERT_EXIT(
		{
			alarm(3);
			const int Descriptor = socket(AF_INET, SOCK_DGRAM, 0);
			if(Descriptor == -1 || close(Descriptor) != 0)
				_exit(2);

			NETADDR BindAddr = NETADDR_ZEROED;
			BindAddr.type = NETTYPE_IPV4;
			BindAddr.ip[0] = 127;
			BindAddr.ip[3] = 1;
			SSocketOwner Socket{net_udp_create(BindAddr)};
			if(!Socket.m_Socket)
				_exit(2);

			// 子进程只有本测试分配描述符。确认重新占用的位置属于 UDP socket。
			int SocketType = 0;
			socklen_t TypeLength = sizeof(SocketType);
			if(getsockopt(Descriptor, SOL_SOCKET, SO_TYPE, &SocketType, &TypeLength) != 0 || SocketType != SOCK_DGRAM)
				_exit(2);
			const int Flags = fcntl(Descriptor, F_GETFD);
			_exit(Flags != -1 && (Flags & FD_CLOEXEC) != 0 ? 0 : 3);
		},
		testing::ExitedWithCode(0), "");
}
#endif

TEST(NetSocketAllocation, NativeUdpCreationRejectsDescriptorOutsideSelectCapacity)
{
	struct SDescriptors
	{
		std::vector<int> m_vOwned;
		~SDescriptors()
		{
			for(int Descriptor : m_vOwned)
				close(Descriptor);
		}
	} Descriptors;
	Descriptors.m_vOwned.reserve(FD_SETSIZE + 1);
	bool ReachedLimit = false;
	for(int i = 0; i <= FD_SETSIZE; ++i)
	{
		const int Descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
		if(Descriptor < 0)
			GTEST_SKIP() << "process descriptor limit prevents reaching FD_SETSIZE";
		Descriptors.m_vOwned.push_back(Descriptor);
		if(Descriptor >= FD_SETSIZE)
		{
			ReachedLimit = true;
			break;
		}
	}
	ASSERT_TRUE(ReachedLimit);
	NETADDR BindAddr = NETADDR_ZEROED;
	BindAddr.type = NETTYPE_IPV4;
	BindAddr.ip[0] = 127;
	BindAddr.ip[3] = 1;
	SSocketOwner Socket{net_udp_create(BindAddr)};
	EXPECT_EQ(Socket.m_Socket, nullptr);
}
#endif
