#include <engine/client/buffer_container_pool.h>
#include <engine/client/graphics_threaded.h>

#include <gtest/gtest.h>

#include <new>

namespace
{
	// 适配器只构造固定的设备输入；索引事务和命令/数据区接管执行生产实现。
	int SubmitContainer(CBufferContainerPool<> &Pool, CCommandBuffer &Buffer, int BufferObject)
	{
		return Pool.Create(BufferObject, [&](int Index) {
			auto *pAttribute = static_cast<SBufferContainerInfo::SAttribute *>(Buffer.AllocData(sizeof(SBufferContainerInfo::SAttribute)));
			if(pAttribute == nullptr)
				throw std::bad_alloc();
			*pAttribute = {};
			pAttribute->m_DataTypeCount = 2;
			pAttribute->m_Type = GRAPHICS_TYPE_FLOAT;
			CCommandBuffer::SCommand_CreateBufferContainer Command;
			Command.m_BufferContainerIndex = Index;
			Command.m_AttrCount = 1;
			Command.m_Stride = sizeof(float) * 2;
			Command.m_VertBufferBindingIndex = BufferObject;
			Command.m_pAttributes = pAttribute;
			if(!Buffer.AddCommandUnsafe(Command))
				throw std::length_error("test command arena rejected creation");
		});
	}
}

TEST(GraphicsBufferContainerCreation, AcceptedCommandAndPublishedBindingHaveTheSameIdentity)
{
	CBufferContainerPool<> Pool;
	CCommandBuffer Buffer(1024, 128);
	const int Index = SubmitContainer(Pool, Buffer, 7);
	ASSERT_TRUE(Pool.IsAllocated(Index));
	EXPECT_EQ(Pool.AssociatedBufferObject(Index), 7);
	ASSERT_NE(Buffer.Head(), nullptr);
	const auto &Command = *static_cast<const CCommandBuffer::SCommand_CreateBufferContainer *>(Buffer.Head());
	EXPECT_EQ(Command.m_BufferContainerIndex, Index);
	EXPECT_EQ(Command.m_VertBufferBindingIndex, 7);
	EXPECT_EQ(Command.m_Stride, sizeof(float) * 2);
	ASSERT_EQ(Command.m_AttrCount, 1u);
	ASSERT_NE(Command.m_pAttributes, nullptr);
	EXPECT_EQ(Command.m_pAttributes[0].m_DataTypeCount, 2);
	EXPECT_EQ(Command.m_pAttributes[0].m_Type, GRAPHICS_TYPE_FLOAT);
}

TEST(GraphicsBufferContainerCreation, RejectedCommandReturnsItsSlotForSubmissionToAnotherBuffer)
{
	CBufferContainerPool<> Pool;
	CCommandBuffer Rejected(sizeof(CCommandBuffer::SCommand_CreateBufferContainer) - 1, 128);
	EXPECT_THROW(SubmitContainer(Pool, Rejected, 7), std::length_error);
	EXPECT_EQ(Rejected.Head(), nullptr);
	EXPECT_FALSE(Pool.IsAllocated(0));
	CCommandBuffer Retry(1024, 128);
	EXPECT_EQ(SubmitContainer(Pool, Retry, 9), 0);
	EXPECT_EQ(Pool.AssociatedBufferObject(0), 9);
	ASSERT_NE(Retry.Head(), nullptr);
	const auto &Command = *static_cast<const CCommandBuffer::SCommand_CreateBufferContainer *>(Retry.Head());
	EXPECT_EQ(Command.m_BufferContainerIndex, 0);
	EXPECT_EQ(Command.m_VertBufferBindingIndex, 9);
}

TEST(GraphicsBufferContainerCreation, RejectedAttributeDataKeepsAnEarlierCommandAndBindingUnchanged)
{
	CBufferContainerPool<> Pool;
	CCommandBuffer Accepted(1024, 128);
	const int Earlier = SubmitContainer(Pool, Accepted, 7);
	const auto *pEarlier = Accepted.Head();
	CCommandBuffer Rejected(1024, 0);
	EXPECT_THROW(SubmitContainer(Pool, Rejected, 9), std::bad_alloc);
	EXPECT_EQ(Rejected.Head(), nullptr);
	EXPECT_EQ(Accepted.Head(), pEarlier);
	ASSERT_TRUE(Pool.IsAllocated(Earlier));
	EXPECT_EQ(Pool.AssociatedBufferObject(Earlier), 7);
	EXPECT_FALSE(Pool.IsAllocated(1));
	EXPECT_EQ(SubmitContainer(Pool, Accepted, 9), 1);
	EXPECT_EQ(Pool.AssociatedBufferObject(Earlier), 7);
	EXPECT_EQ(Pool.AssociatedBufferObject(1), 9);
}
