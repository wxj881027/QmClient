#include <engine/client/buffer_object_pool.h>
#include <engine/client/graphics_threaded.h>
#include <engine/client/graphics_upload_data.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <new>

namespace
{
	template<typename T>
	void Append(CCommandBuffer &Buffer, const T &Command)
	{
		if(!Buffer.AddCommandUnsafe(Command))
			throw std::length_error("test command arena rejected submission");
	}
	CCommandBuffer::SCommand_CreateBufferObject CreateCommand(int Index, void *pData = nullptr, bool Owned = false)
	{
		CCommandBuffer::SCommand_CreateBufferObject Command;
		Command.m_BufferIndex = Index;
		Command.m_DataSize = 4;
		Command.m_pUploadData = pData;
		Command.m_DeletePointer = Owned;
		Command.m_Flags = 0;
		return Command;
	}
	int SubmitEmpty(CBufferObjectPool<> &Pool, CCommandBuffer &Buffer)
	{
		return Pool.Create([&](auto &Creation) {
			Append(Buffer, CreateCommand(Creation.Index()));
			Creation.Accepted();
		});
	}
	void SubmitPrefixThenFail(CBufferObjectPool<> &Pool, CCommandBuffer &Buffer)
	{
		// 固定输入表示已接管创建和一个更新片段，不重写生产分块算法。
		Pool.Create([&](auto &Creation) {
			Append(Buffer, CreateCommand(Creation.Index()));
			Creation.Accepted();
			auto *pBytes = static_cast<unsigned char *>(Buffer.AllocData(2));
			if(pBytes == nullptr)
				throw std::bad_alloc();
			pBytes[0] = 7;
			pBytes[1] = 9;
			CCommandBuffer::SCommand_UpdateBufferObject Update;
			Update.m_BufferIndex = Creation.Index();
			Update.m_DataSize = 2;
			Update.m_pUploadData = pBytes;
			Update.m_DeletePointer = false;
			Update.m_pOffset = nullptr;
			Append(Buffer, Update);
			throw std::bad_alloc();
		});
	}
	void SubmitDelete(CCommandBuffer &Buffer, int Index)
	{
		CCommandBuffer::SCommand_DeleteBufferObject Delete;
		Delete.m_BufferIndex = Index;
		Append(Buffer, Delete);
	}
	using TEvents = std::vector<std::pair<int, int>>;
	TEvents Events(const CCommandBuffer &Buffer)
	{
		TEvents vResult;
		for(auto *pCommand = Buffer.Head(); pCommand != nullptr; pCommand = pCommand->m_pNext)
		{
			if(pCommand->m_Cmd == CCommandBuffer::CMD_CREATE_BUFFER_OBJECT)
				vResult.emplace_back(pCommand->m_Cmd, static_cast<const CCommandBuffer::SCommand_CreateBufferObject *>(pCommand)->m_BufferIndex);
			else if(pCommand->m_Cmd == CCommandBuffer::CMD_UPDATE_BUFFER_OBJECT)
				vResult.emplace_back(pCommand->m_Cmd, static_cast<const CCommandBuffer::SCommand_UpdateBufferObject *>(pCommand)->m_BufferIndex);
			else if(pCommand->m_Cmd == CCommandBuffer::CMD_DELETE_BUFFER_OBJECT)
				vResult.emplace_back(pCommand->m_Cmd, static_cast<const CCommandBuffer::SCommand_DeleteBufferObject *>(pCommand)->m_BufferIndex);
			else
				ADD_FAILURE() << "Unexpected command in BO creation pipeline";
		}
		return vResult;
	}
}

TEST(GraphicsBufferObjectCreation, AcceptedOwnedUploadTransfersToTheStoredCommandExactlyOnce)
{
	CBufferObjectPool<> Pool;
	CCommandBuffer Buffer(1024, 128);
	CGraphicsUploadData<unsigned char> Data(static_cast<unsigned char *>(std::malloc(4)));
	ASSERT_NE(Data, nullptr);
	Data.get()[0] = 42;
	const int Index = Pool.Create([&](auto &Creation) {
		Append(Buffer, CreateCommand(Creation.Index(), Data.get(), true));
		Creation.Accepted();
		Data.release();
	});
	EXPECT_TRUE(Pool.IsLive(Index));
	EXPECT_EQ(Data, nullptr);
	ASSERT_NE(Buffer.Head(), nullptr);
	auto *pStored = static_cast<CCommandBuffer::SCommand_CreateBufferObject *>(Buffer.Head());
	EXPECT_EQ(pStored->m_BufferIndex, Index);
	auto Taken = TakeGraphicsUpload(pStored->m_pUploadData);
	ASSERT_NE(Taken, nullptr);
	EXPECT_EQ(static_cast<unsigned char *>(Taken.get())[0], 42);
	EXPECT_EQ(pStored->m_pUploadData, nullptr);
	Taken.reset();
	Buffer.Reset();
	EXPECT_EQ(Buffer.Head(), nullptr);
}

TEST(GraphicsBufferObjectCreation, PartialUploadCannotReuseItsIndexUntilADeleteCommandPrecedesTheNextCreate)
{
	CBufferObjectPool<> Pool;
	CCommandBuffer Buffer(2048, 128);
	EXPECT_THROW(SubmitPrefixThenFail(Pool, Buffer), std::bad_alloc);
	EXPECT_TRUE(Pool.HasPending());
	EXPECT_FALSE(Pool.IsLive(0));
	EXPECT_EQ(SubmitEmpty(Pool, Buffer), 1);
	Pool.FlushPending([&](int Index) { SubmitDelete(Buffer, Index); });
	EXPECT_EQ(SubmitEmpty(Pool, Buffer), 0);
	EXPECT_EQ(Events(Buffer), (TEvents{
					  {CCommandBuffer::CMD_CREATE_BUFFER_OBJECT, 0},
					  {CCommandBuffer::CMD_UPDATE_BUFFER_OBJECT, 0},
					  {CCommandBuffer::CMD_CREATE_BUFFER_OBJECT, 1},
					  {CCommandBuffer::CMD_DELETE_BUFFER_OBJECT, 0},
					  {CCommandBuffer::CMD_CREATE_BUFFER_OBJECT, 0}}));
}

TEST(GraphicsBufferObjectCreation, RejectedDeferredDeleteRetainsTheOldIndexAndTheAcceptedUploadPrefix)
{
	CBufferObjectPool<> Pool;
	CCommandBuffer Buffer(2048, 128);
	EXPECT_THROW(SubmitPrefixThenFail(Pool, Buffer), std::bad_alloc);
	const auto Prefix = Events(Buffer);
	CCommandBuffer Rejected(1, 0);
	EXPECT_THROW(Pool.FlushPending([&](int Index) { SubmitDelete(Rejected, Index); }), std::length_error);
	EXPECT_EQ(Rejected.Head(), nullptr);
	EXPECT_EQ(Events(Buffer), Prefix);
	EXPECT_TRUE(Pool.HasPending());
	EXPECT_FALSE(Pool.IsLive(0));
	EXPECT_EQ(SubmitEmpty(Pool, Buffer), 1);
	Pool.FlushPending([&](int Index) { SubmitDelete(Buffer, Index); });
	EXPECT_EQ(SubmitEmpty(Pool, Buffer), 0);
	EXPECT_FALSE(Pool.HasPending());
}

TEST(GraphicsBufferObjectCreation, UnacceptedCreationLeavesTheUploadWithItsCurrentOwnerAndReturnsTheSlot)
{
	CBufferObjectPool<> Pool;
	CCommandBuffer Rejected(1, 0);
	CGraphicsUploadData<unsigned char> Data(static_cast<unsigned char *>(std::malloc(4)));
	ASSERT_NE(Data, nullptr);
	Data.get()[0] = 42;
	EXPECT_THROW(Pool.Create([&](auto &Creation) {
		Append(Rejected, CreateCommand(Creation.Index(), Data.get(), true));
		Creation.Accepted();
		Data.release();
	}),
		std::length_error);
	ASSERT_NE(Data, nullptr);
	EXPECT_EQ(Data.get()[0], 42);
	EXPECT_FALSE(Pool.HasPending());
	EXPECT_EQ(Rejected.Head(), nullptr);
	CCommandBuffer Retry(1024, 128);
	EXPECT_EQ(SubmitEmpty(Pool, Retry), 0);
}
