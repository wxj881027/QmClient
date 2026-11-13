#include <engine/client/graphics_buffer_pair.h>

#include <gtest/gtest.h>

#include <new>
#include <stdexcept>
#include <utility>

namespace
{
	enum class EFailure
	{
		NONE,
		ALLOCATION,
		LENGTH,
		OTHER,
	};
	struct CDevice
	{
		int m_ContainerCalls = 0, m_ObjectCalls = 0;
		int m_LastContainer = -1, m_LastObject = -1;
		bool m_DestroyAll = false;
		EFailure m_ContainerFailure = EFailure::NONE, m_ObjectFailure = EFailure::NONE;
		void Fail(EFailure &Failure)
		{
			const EFailure Requested = std::exchange(Failure, EFailure::NONE);
			if(Requested == EFailure::ALLOCATION)
				throw std::bad_alloc();
			if(Requested == EFailure::LENGTH)
				throw std::length_error("controlled device deletion limit");
			if(Requested == EFailure::OTHER)
				throw std::runtime_error("unrelated device deletion error");
		}
		// 替身只记录设备调用并在操作前失败，身份清理执行生产函数。
		void DeleteBufferContainer(int &Index, bool DestroyAll)
		{
			++m_ContainerCalls;
			m_LastContainer = Index;
			m_DestroyAll = DestroyAll;
			Fail(m_ContainerFailure);
		}
		void DeleteBufferObject(int Index)
		{
			++m_ObjectCalls;
			m_LastObject = Index;
			Fail(m_ObjectFailure);
		}
	};
}

TEST(GraphicsBufferPair, CompletePairDeletesTheContainerWithItsAssociatedObject)
{
	CDevice Device;
	int Container = 4, Object = 8;
	DeleteGraphicsBufferPair(&Device, Container, Object);
	EXPECT_EQ(Device.m_LastContainer, 4);
	EXPECT_TRUE(Device.m_DestroyAll);
	EXPECT_EQ(Device.m_ContainerCalls, 1);
	EXPECT_EQ(Device.m_ObjectCalls, 0);
	EXPECT_EQ(Container, -1);
	EXPECT_EQ(Object, -1);
}

TEST(GraphicsBufferPair, ObjectWithoutAContainerIsDeletedDirectly)
{
	CDevice Device;
	int Container = -1, Object = 8;
	DeleteGraphicsBufferPair(&Device, Container, Object);
	EXPECT_EQ(Device.m_LastObject, 8);
	EXPECT_EQ(Device.m_ObjectCalls, 1);
	EXPECT_EQ(Device.m_ContainerCalls, 0);
	EXPECT_EQ(Container, -1);
	EXPECT_EQ(Object, -1);
}

TEST(GraphicsBufferPair, EmptyPairCanBeClearedBeforeTheDeviceIsInitialized)
{
	int Container = -1, Object = -1;
	EXPECT_NO_THROW(DeleteGraphicsBufferPair(static_cast<CDevice *>(nullptr), Container, Object));
	EXPECT_EQ(Container, -1);
	EXPECT_EQ(Object, -1);
}

TEST(GraphicsBufferPair, ContainerAllocationFailureLeavesThePairAvailableForRetry)
{
	CDevice Device;
	int Container = 4, Object = 8;
	Device.m_ContainerFailure = EFailure::ALLOCATION;
	EXPECT_THROW(DeleteGraphicsBufferPair(&Device, Container, Object), std::bad_alloc);
	EXPECT_EQ(Container, 4);
	EXPECT_EQ(Object, 8);
	DeleteGraphicsBufferPair(&Device, Container, Object);
	EXPECT_EQ(Container, -1);
	EXPECT_EQ(Object, -1);
	EXPECT_EQ(Device.m_ContainerCalls, 2);
	EXPECT_EQ(Device.m_ObjectCalls, 0);
}

TEST(GraphicsBufferPair, ObjectAllocationFailureKeepsItsIdentityUntilRetrySucceeds)
{
	CDevice Device;
	int Container = -1, Object = 8;
	Device.m_ObjectFailure = EFailure::ALLOCATION;
	EXPECT_THROW(DeleteGraphicsBufferPair(&Device, Container, Object), std::bad_alloc);
	EXPECT_EQ(Object, 8);
	DeleteGraphicsBufferPair(&Device, Container, Object);
	EXPECT_EQ(Object, -1);
	EXPECT_EQ(Device.m_ObjectCalls, 2);
}

TEST(GraphicsBufferPair, ContainerLengthFailurePropagatesWithoutLosingEitherIdentity)
{
	CDevice Device;
	int Container = 4, Object = 8;
	Device.m_ContainerFailure = EFailure::LENGTH;
	EXPECT_THROW(DeleteGraphicsBufferPair(&Device, Container, Object), std::length_error);
	EXPECT_EQ(Container, 4);
	EXPECT_EQ(Object, 8);
	EXPECT_EQ(Device.m_ObjectCalls, 0);
}

TEST(GraphicsBufferPair, UnrelatedObjectDeletionErrorAlsoPreservesTheKnownOwner)
{
	CDevice Device;
	int Container = -1, Object = 8;
	Device.m_ObjectFailure = EFailure::OTHER;
	EXPECT_THROW(DeleteGraphicsBufferPair(&Device, Container, Object), std::runtime_error);
	EXPECT_EQ(Object, 8);
	EXPECT_EQ(Container, -1);
	DeleteGraphicsBufferPair(&Device, Container, Object);
	EXPECT_EQ(Object, -1);
}

TEST(GraphicsBufferPair, RepeatingSuccessfulDeletionDoesNotDeleteTheObjectAgain)
{
	CDevice Device;
	int Container = 4, Object = 8;
	DeleteGraphicsBufferPair(&Device, Container, Object);
	DeleteGraphicsBufferPair(&Device, Container, Object);
	EXPECT_EQ(Device.m_ContainerCalls, 1);
	EXPECT_EQ(Device.m_ObjectCalls, 0);
}
