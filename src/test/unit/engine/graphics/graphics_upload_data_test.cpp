#include <engine/client/graphics_threaded.h>
#include <engine/client/graphics_upload_data.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <new>
#include <type_traits>

namespace
{
	struct CCountingRelease
	{
		int &m_Count;
		void operator()(void *pData) const noexcept
		{
			++m_Count;
			std::free(pData);
		}
	};

	CGraphicsUploadData<uint8_t> AllocateUpload()
	{
		return CGraphicsUploadData<uint8_t>(static_cast<uint8_t *>(std::malloc(16)));
	}

	template<typename TCommand, typename TData>
	void CheckDiscardedCommand(const char *pName, TData *TCommand::*pField)
	{
		SCOPED_TRACE(pName);
		CCommandBuffer Buffer(1024, 64);
		auto pData = AllocateUpload();
		ASSERT_NE(pData, nullptr);
		TCommand Command{};
		Command.*pField = pData.get();
		if constexpr(std::is_same_v<TData, void>)
			Command.m_DeletePointer = true;
		ASSERT_TRUE(Buffer.AddCommandUnsafe(Command));
		pData.release();
		const auto *pQueued = static_cast<const TCommand *>(Buffer.Head());
		int Released = 0;
		Buffer.DiscardUploads(CCountingRelease{Released});
		EXPECT_EQ(Released, 1);
		EXPECT_EQ(pQueued->*pField, nullptr);
		Buffer.DiscardUploads(CCountingRelease{Released});
		EXPECT_EQ(Released, 1);
	}
} // namespace

TEST(GraphicsUploadData, TakingOwnedDataClearsSourceBeforeConsumption)
{
	auto pAllocated = AllocateUpload();
	ASSERT_NE(pAllocated, nullptr);
	uint8_t *pData = pAllocated.release();
	int Released = 0;
	{
		auto pOwned = TakeGraphicsUpload(pData, true, CCountingRelease{Released});
		ASSERT_NE(pOwned, nullptr);
		EXPECT_EQ(pData, nullptr);
		EXPECT_EQ(Released, 0);
		*pOwned = 42;
		EXPECT_EQ(*pOwned, 42);
	}
	EXPECT_EQ(Released, 1);
}

TEST(GraphicsUploadData, BorrowedDataStaysAvailableAndIsNotReleased)
{
	uint8_t Value = 42;
	uint8_t *pData = &Value;
	int Released = 0;
	{
		auto pOwned = TakeGraphicsUpload(pData, false, CCountingRelease{Released});
		EXPECT_EQ(pOwned, nullptr);
		EXPECT_EQ(pData, &Value);
	}
	EXPECT_EQ(Released, 0);
	EXPECT_EQ(Value, 42);
}

TEST(GraphicsUploadData, TakingNullDataDoesNotCallRelease)
{
	uint8_t *pData = nullptr;
	int Released = 0;
	{
		auto pOwned = TakeGraphicsUpload(pData, true, CCountingRelease{Released});
		EXPECT_EQ(pOwned, nullptr);
	}
	EXPECT_EQ(Released, 0);
}

TEST(GraphicsUploadData, ConsumerExceptionReleasesTakenData)
{
	auto pAllocated = AllocateUpload();
	ASSERT_NE(pAllocated, nullptr);
	uint8_t *pData = pAllocated.release();
	int Released = 0;
	EXPECT_THROW(
		{
			auto pOwned = TakeGraphicsUpload(pData, true, CCountingRelease{Released});
			throw std::bad_alloc();
		},
		std::bad_alloc);
	EXPECT_EQ(pData, nullptr);
	EXPECT_EQ(Released, 1);
}

TEST(GraphicsUploadCommandBuffer, DiscardReleasesEachSingleUploadCommandKind)
{
	CheckDiscardedCommand("texture create", &CCommandBuffer::SCommand_Texture_Create::m_pData);
	CheckDiscardedCommand("texture update", &CCommandBuffer::SCommand_Texture_Update::m_pData);
	CheckDiscardedCommand("text update", &CCommandBuffer::SCommand_TextTexture_Update::m_pData);
	CheckDiscardedCommand("buffer create", &CCommandBuffer::SCommand_CreateBufferObject::m_pUploadData);
	CheckDiscardedCommand("buffer recreate", &CCommandBuffer::SCommand_RecreateBufferObject::m_pUploadData);
	CheckDiscardedCommand("buffer update", &CCommandBuffer::SCommand_UpdateBufferObject::m_pUploadData);
}

TEST(GraphicsUploadCommandBuffer, DiscardReleasesBothTextTextures)
{
	CCommandBuffer Buffer(1024, 64);
	auto pText = AllocateUpload();
	auto pOutline = AllocateUpload();
	ASSERT_NE(pText, nullptr);
	ASSERT_NE(pOutline, nullptr);
	CCommandBuffer::SCommand_TextTextures_Create Command;
	Command.m_pTextData = pText.get();
	Command.m_pTextOutlineData = pOutline.get();
	ASSERT_TRUE(Buffer.AddCommandUnsafe(Command));
	pText.release();
	pOutline.release();
	const auto *pQueued = static_cast<const CCommandBuffer::SCommand_TextTextures_Create *>(Buffer.Head());
	int Released = 0;
	Buffer.DiscardUploads(CCountingRelease{Released});
	EXPECT_EQ(Released, 2);
	EXPECT_EQ(pQueued->m_pTextData, nullptr);
	EXPECT_EQ(pQueued->m_pTextOutlineData, nullptr);
	Buffer.DiscardUploads(CCountingRelease{Released});
	EXPECT_EQ(Released, 2);
}

TEST(GraphicsUploadCommandBuffer, DiscardLeavesBorrowedBufferDataIntact)
{
	CCommandBuffer Buffer(1024, 64);
	int Data = 42;
	CCommandBuffer::SCommand_UpdateBufferObject Borrowed;
	Borrowed.m_pUploadData = &Data;
	Borrowed.m_DeletePointer = false;
	ASSERT_TRUE(Buffer.AddCommandUnsafe(Borrowed));
	const auto *pQueued = static_cast<const CCommandBuffer::SCommand_UpdateBufferObject *>(Buffer.Head());
	int Released = 0;
	Buffer.DiscardUploads(CCountingRelease{Released});
	EXPECT_EQ(Released, 0);
	EXPECT_EQ(pQueued->m_pUploadData, &Data);
	EXPECT_EQ(Data, 42);
}

TEST(GraphicsUploadCommandBuffer, DiscardDoesNotReleaseDataAlreadyTakenByConsumer)
{
	CCommandBuffer Buffer(1024, 64);
	auto pData = AllocateUpload();
	ASSERT_NE(pData, nullptr);
	CCommandBuffer::SCommand_Texture_Create Command;
	Command.m_pData = pData.get();
	ASSERT_TRUE(Buffer.AddCommandUnsafe(Command));
	pData.release();
	const auto *pQueued = static_cast<const CCommandBuffer::SCommand_Texture_Create *>(Buffer.Head());
	int Released = 0;
	auto pOwned = TakeGraphicsUpload(pQueued->m_pData, true, CCountingRelease{Released});
	Buffer.DiscardUploads(CCountingRelease{Released});
	EXPECT_EQ(Released, 0);
	EXPECT_EQ(pQueued->m_pData, nullptr);
	pOwned.reset();
	EXPECT_EQ(Released, 1);
}

TEST(GraphicsUploadCommandBuffer, FailedConsumerReleasesCurrentUploadAndDiscardReclaimsTail)
{
	CCommandBuffer Buffer(1024, 64);
	for(int i = 0; i < 2; ++i)
	{
		auto pData = AllocateUpload();
		ASSERT_NE(pData, nullptr);
		CCommandBuffer::SCommand_Texture_Update Command;
		Command.m_pData = pData.get();
		ASSERT_TRUE(Buffer.AddCommandUnsafe(Command));
		pData.release();
	}
	const auto *pCurrent = static_cast<const CCommandBuffer::SCommand_Texture_Update *>(Buffer.Head());
	const auto *pTail = static_cast<const CCommandBuffer::SCommand_Texture_Update *>(pCurrent->m_pNext);
	int Released = 0;
	EXPECT_THROW(
		{
			auto pOwned = TakeGraphicsUpload(pCurrent->m_pData, true, CCountingRelease{Released});
			throw std::bad_alloc();
		},
		std::bad_alloc);
	EXPECT_EQ(Released, 1);
	Buffer.DiscardUploads(CCountingRelease{Released});
	EXPECT_EQ(Released, 2);
	EXPECT_EQ(pCurrent->m_pData, nullptr);
	EXPECT_EQ(pTail->m_pData, nullptr);
	Buffer.DiscardUploads(CCountingRelease{Released});
	EXPECT_EQ(Released, 2);
}

TEST(GraphicsUploadCommandBuffer, MissingTextOutlineDoesNotPreventTextCleanup)
{
	CCommandBuffer Buffer(1024, 64);
	auto pData = AllocateUpload();
	ASSERT_NE(pData, nullptr);
	CCommandBuffer::SCommand_TextTextures_Create Command;
	Command.m_pTextData = pData.get();
	ASSERT_TRUE(Buffer.AddCommandUnsafe(Command));
	pData.release();
	int Released = 0;
	Buffer.DiscardUploads(CCountingRelease{Released});
	EXPECT_EQ(Released, 1);
}

TEST(GraphicsUploadCommandBuffer, FailedEnqueueKeepsCallerOwnership)
{
	CCommandBuffer Buffer(0, 64);
	auto pData = AllocateUpload();
	ASSERT_NE(pData, nullptr);
	CCommandBuffer::SCommand_Texture_Create Command;
	Command.m_pData = pData.get();
	EXPECT_FALSE(Buffer.AddCommandUnsafe(Command));
	EXPECT_EQ(Command.m_pData, pData.get());
	EXPECT_EQ(Buffer.Head(), nullptr);
	int Released = 0;
	Buffer.DiscardUploads(CCountingRelease{Released});
	EXPECT_EQ(Released, 0);
}

TEST(GraphicsUploadCommandBuffer, ResetAllowsNewUploadsAfterDiscardedBatch)
{
	CCommandBuffer Buffer(1024, 64);
	for(int i = 0; i < 2; ++i)
	{
		auto pData = AllocateUpload();
		ASSERT_NE(pData, nullptr);
		CCommandBuffer::SCommand_Texture_Create Command;
		Command.m_pData = pData.get();
		ASSERT_TRUE(Buffer.AddCommandUnsafe(Command));
		pData.release();
		int Released = 0;
		Buffer.DiscardUploads(CCountingRelease{Released});
		EXPECT_EQ(Released, 1);
		Buffer.Reset();
		EXPECT_EQ(Buffer.Head(), nullptr);
		EXPECT_EQ(Buffer.m_CommandCount, 0u);
	}
}
