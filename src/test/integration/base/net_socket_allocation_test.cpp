#include <base/system.h>

#include <gtest/gtest.h>

#include <array>

#if defined(CONF_FAMILY_UNIX)
#include <fcntl.h>
#include <sys/select.h>
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
