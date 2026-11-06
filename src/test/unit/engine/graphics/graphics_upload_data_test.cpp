#include <engine/client/graphics_threaded.h>
#include <engine/client/graphics_upload_data.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/support/rejectable_allocation_resource.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
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

	struct SUploadReleaseProbe
	{
		int m_PixelFrees = 0;
		int m_Releases = 0;
		int m_PixelFreesAtRelease = -1;
		SGraphicsUploadRelease Token() noexcept
		{
			return {this, [](void *pContext) noexcept {
					auto &Probe = *static_cast<SUploadReleaseProbe *>(pContext);
					++Probe.m_Releases;
					Probe.m_PixelFreesAtRelease = Probe.m_PixelFrees;
				}};
		}
	};

	class CUploadImage
	{
	public:
		CImageInfo m_Image;
		explicit CUploadImage(CImageInfo::EImageFormat Format = CImageInfo::FORMAT_RGBA)
		{
			m_Image.m_Width = 2;
			m_Image.m_Height = 1;
			m_Image.m_Format = Format;
			m_Image.m_pData = static_cast<uint8_t *>(std::malloc(m_Image.DataSize()));
			if(m_Image.m_pData != nullptr)
				std::memset(m_Image.m_pData, 7, m_Image.DataSize());
		}
		~CUploadImage() { m_Image.Free(); }
	};

	auto AppendTextureUpload(CCommandBuffer &Buffer)
	{
		return [&Buffer](int Slot, size_t Width, size_t Height, uint8_t *pData, const SGraphicsUploadRelease &Release) {
			CCommandBuffer::SCommand_Texture_Create Command;
			Command.m_Slot = Slot;
			Command.m_Width = Width;
			Command.m_Height = Height;
			Command.m_Flags = 0;
			Command.m_pData = pData;
			Command.m_Release = Release;
			return Buffer.AddCommandUnsafe(Command);
		};
	}

	bool RefuseRgbaAllocation(uint8_t *&pData, const CImageInfo &)
	{
		pData = nullptr;
		return false;
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

TEST(GraphicsOwnedUpload, RgbaAdmissionRetainsCreditUntilTheQueuedPixelsAreDiscarded)
{
	SUploadReleaseProbe Probe;
	CCommandBuffer Buffer(1024, 64);
	CTextureSlotPool<> Slots;
	CUploadImage Image;
	ASSERT_NE(Image.m_Image.m_pData, nullptr);
	auto Release = Probe.Token();
	const auto *pPixels = Image.m_Image.m_pData;
	int Id;
	uint32_t Generation;
	ASSERT_TRUE(TryQueueGraphicsTextureUploadOwned(Image.m_Image, Release, Slots, AppendTextureUpload(Buffer), Id, Generation));
	EXPECT_TRUE(Slots.IsAllocated(Id, Generation));
	EXPECT_EQ(Image.m_Image.m_pData, nullptr);
	EXPECT_EQ(Release.m_pContext, nullptr);
	EXPECT_EQ(Release.m_pfnRelease, nullptr);
	EXPECT_EQ(Probe.m_Releases, 0);
	const auto *pQueued = static_cast<const CCommandBuffer::SCommand_Texture_Create *>(Buffer.Head());
	ASSERT_NE(pQueued, nullptr);
	EXPECT_EQ(pQueued->m_pData, pPixels);
	Buffer.DiscardUploads(CCountingRelease{Probe.m_PixelFrees});
	EXPECT_EQ(Probe.m_Releases, 1);
	EXPECT_EQ(Probe.m_PixelFreesAtRelease, 1);
	Buffer.DiscardUploads(CCountingRelease{Probe.m_PixelFrees});
	EXPECT_EQ(Probe.m_Releases, 1);
}

TEST(GraphicsOwnedUpload, NonRgbaFormatsQueueTheirActualRgbaPixelsBeforeCreditRelease)
{
	struct SCase
	{
		CImageInfo::EImageFormat m_Format;
		std::array<uint8_t, 8> m_Expected;
	};
	const SCase aCases[] = {
		{CImageInfo::FORMAT_RGB, {7, 7, 7, 255, 7, 7, 7, 255}},
		{CImageInfo::FORMAT_R, {255, 255, 255, 7, 255, 255, 255, 7}},
		{CImageInfo::FORMAT_RA, {7, 7, 7, 7, 7, 7, 7, 7}},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_Format);
		SUploadReleaseProbe Probe;
		CCommandBuffer Buffer(1024, 64);
		CTextureSlotPool<> Slots;
		CUploadImage Image(Case.m_Format);
		ASSERT_NE(Image.m_Image.m_pData, nullptr);
		auto Release = Probe.Token();
		int Id;
		uint32_t Generation;
		ASSERT_TRUE(TryQueueGraphicsTextureUploadOwned(Image.m_Image, Release, Slots, AppendTextureUpload(Buffer), Id, Generation));
		EXPECT_EQ(Image.m_Image.m_pData, nullptr);
		EXPECT_EQ(Probe.m_Releases, 0);
		const auto *pQueued = static_cast<const CCommandBuffer::SCommand_Texture_Create *>(Buffer.Head());
		ASSERT_NE(pQueued, nullptr);
		EXPECT_EQ(std::memcmp(pQueued->m_pData, Case.m_Expected.data(), Case.m_Expected.size()), 0);
		Buffer.DiscardUploads(CCountingRelease{Probe.m_PixelFrees});
		EXPECT_EQ(Probe.m_Releases, 1);
		EXPECT_EQ(Probe.m_PixelFreesAtRelease, 1);
	}
}

TEST(GraphicsOwnedUpload, SlotAllocationRefusalKeepsBothOwnersAndLaterAdmissionSucceeds)
{
	SUploadReleaseProbe Probe;
	CCommandBuffer Buffer(1024, 64);
	CRejectableAllocationResource Memory;
	CTextureSlotPool<CBorrowedAllocator<STextureSlot>> Slots{CBorrowedAllocator<STextureSlot>(&Memory)};
	CUploadImage Image;
	ASSERT_NE(Image.m_Image.m_pData, nullptr);
	const auto *pPixels = Image.m_Image.m_pData;
	auto Release = Probe.Token();
	Memory.m_Reject = true;
	int Id;
	uint32_t Generation;
	EXPECT_FALSE(TryQueueGraphicsTextureUploadOwned(Image.m_Image, Release, Slots, AppendTextureUpload(Buffer), Id, Generation));
	EXPECT_EQ(Image.m_Image.m_pData, pPixels);
	EXPECT_EQ(Release.m_pContext, &Probe);
	EXPECT_EQ(Buffer.Head(), nullptr);
	EXPECT_EQ(Probe.m_Releases, 0);
	Memory.m_Reject = false;
	ASSERT_TRUE(TryQueueGraphicsTextureUploadOwned(Image.m_Image, Release, Slots, AppendTextureUpload(Buffer), Id, Generation));
	EXPECT_TRUE(Slots.IsAllocated(Id, Generation));
	Buffer.DiscardUploads(CCountingRelease{Probe.m_PixelFrees});
	EXPECT_EQ(Probe.m_Releases, 1);
}

TEST(GraphicsOwnedUpload, FullCommandBufferKeepsTheRgbSourceAndReturnsItsReservedSlot)
{
	SUploadReleaseProbe Probe;
	CCommandBuffer Full(0, 64);
	CTextureSlotPool<> Slots;
	CUploadImage Image(CImageInfo::FORMAT_RGB);
	ASSERT_NE(Image.m_Image.m_pData, nullptr);
	const auto *pPixels = Image.m_Image.m_pData;
	auto Release = Probe.Token();
	int Id;
	uint32_t Generation;
	EXPECT_FALSE(TryQueueGraphicsTextureUploadOwned(Image.m_Image, Release, Slots, AppendTextureUpload(Full), Id, Generation));
	EXPECT_EQ(Image.m_Image.m_pData, pPixels);
	EXPECT_EQ(Image.m_Image.m_Format, CImageInfo::FORMAT_RGB);
	EXPECT_EQ(Release.m_pContext, &Probe);
	EXPECT_EQ(Probe.m_Releases, 0);
	CCommandBuffer Next(1024, 64);
	ASSERT_TRUE(TryQueueGraphicsTextureUploadOwned(Image.m_Image, Release, Slots, AppendTextureUpload(Next), Id, Generation));
	EXPECT_EQ(Id, 0);
	EXPECT_TRUE(Slots.IsAllocated(Id, Generation));
	Next.DiscardUploads(CCountingRelease{Probe.m_PixelFrees});
	EXPECT_EQ(Probe.m_Releases, 1);
}

TEST(GraphicsOwnedUpload, RgbaConversionRefusalRetainsTheOriginalPixelsAndCreditForRetry)
{
	SUploadReleaseProbe Probe;
	CCommandBuffer Buffer(1024, 64);
	CTextureSlotPool<> Slots;
	CUploadImage Image(CImageInfo::FORMAT_RGB);
	ASSERT_NE(Image.m_Image.m_pData, nullptr);
	const auto *pPixels = Image.m_Image.m_pData;
	auto Release = Probe.Token();
	int Id;
	uint32_t Generation;
	EXPECT_FALSE(TryQueueGraphicsTextureUploadOwned(Image.m_Image, Release, Slots, AppendTextureUpload(Buffer), Id, Generation, RefuseRgbaAllocation));
	EXPECT_EQ(Image.m_Image.m_pData, pPixels);
	EXPECT_EQ(Release.m_pContext, &Probe);
	EXPECT_EQ(Probe.m_Releases, 0);
	ASSERT_TRUE(TryQueueGraphicsTextureUploadOwned(Image.m_Image, Release, Slots, AppendTextureUpload(Buffer), Id, Generation));
	Buffer.DiscardUploads(CCountingRelease{Probe.m_PixelFrees});
	EXPECT_EQ(Probe.m_Releases, 1);
}

TEST(GraphicsOwnedUpload, AppendExceptionDoesNotConsumeTheSourceOrReleaseContext)
{
	SUploadReleaseProbe Probe;
	CTextureSlotPool<> Slots;
	CUploadImage Image(CImageInfo::FORMAT_RGB);
	ASSERT_NE(Image.m_Image.m_pData, nullptr);
	const auto *pPixels = Image.m_Image.m_pData;
	auto Release = Probe.Token();
	int Id;
	uint32_t Generation;
	EXPECT_THROW(TryQueueGraphicsTextureUploadOwned(Image.m_Image, Release, Slots, [](int, size_t, size_t, uint8_t *, const SGraphicsUploadRelease &) -> bool { throw std::runtime_error("old buffer submission failed"); }, Id, Generation), std::runtime_error);
	EXPECT_EQ(Image.m_Image.m_pData, pPixels);
	EXPECT_EQ(Release.m_pContext, &Probe);
	EXPECT_EQ(Probe.m_Releases, 0);
	Image.m_Image.Free();
	ReleaseGraphicsUpload(Release);
	EXPECT_EQ(Probe.m_Releases, 1);
}

TEST(GraphicsOwnedUpload, InvalidImagesAreConsumedWithoutPublishingACommand)
{
	for(int Invalid = 0; Invalid < 6; ++Invalid)
	{
		SCOPED_TRACE(Invalid);
		SUploadReleaseProbe Probe;
		CCommandBuffer Buffer(1024, 64);
		CTextureSlotPool<> Slots;
		CUploadImage Image;
		ASSERT_NE(Image.m_Image.m_pData, nullptr);
		if(Invalid == 0)
			Image.m_Image.m_Width = 0;
		else if(Invalid == 1)
			Image.m_Image.m_Format = CImageInfo::FORMAT_UNDEFINED;
		else if(Invalid == 2)
			Image.m_Image.m_Width = std::numeric_limits<size_t>::max();
		else if(Invalid == 3)
			Image.m_Image.Free();
		else if(Invalid == 4)
			Image.m_Image.m_Width = static_cast<size_t>(std::numeric_limits<int>::max()) + 1;
		else
			Image.m_Image.m_Height = static_cast<size_t>(std::numeric_limits<int>::max()) + 1;
		auto Release = Probe.Token();
		int Id;
		uint32_t Generation;
		EXPECT_TRUE(TryQueueGraphicsTextureUploadOwned(Image.m_Image, Release, Slots, AppendTextureUpload(Buffer), Id, Generation));
		EXPECT_EQ(Id, -1);
		EXPECT_EQ(Buffer.Head(), nullptr);
		EXPECT_EQ(Image.m_Image.m_pData, nullptr);
		EXPECT_EQ(Release.m_pContext, nullptr);
		EXPECT_EQ(Probe.m_Releases, 1);
	}
}

TEST(GraphicsOwnedUpload, UnsupportedGraphicsKeepsPixelsAndContextWithoutLegacyFallback)
{
	CIconBenchmarkGraphics Graphics;
	CUploadImage Image;
	ASSERT_NE(Image.m_Image.m_pData, nullptr);
	const auto *pPixels = Image.m_Image.m_pData;
	SUploadReleaseProbe Probe;
	auto Release = Probe.Token();
	IGraphics::CTextureHandle Texture;
	EXPECT_FALSE(Graphics.IGraphics::TryLoadTextureRawMoveOwned(Image.m_Image, 0, "unsupported", Release, Texture));
	EXPECT_FALSE(Texture.IsValid());
	EXPECT_EQ(Image.m_Image.m_pData, pPixels);
	EXPECT_EQ(Release.m_pContext, &Probe);
	EXPECT_EQ(Probe.m_Releases, 0);
	EXPECT_EQ(Graphics.m_Uploads, 0u);
	Image.m_Image.Free();
	ReleaseGraphicsUpload(Release);
}

TEST(GraphicsOwnedUpload, ConsumerGuardMoveReleasesCreditOnceAfterPixelCleanup)
{
	SUploadReleaseProbe Probe;
	auto Release = Probe.Token();
	auto pAllocated = AllocateUpload();
	ASSERT_NE(pAllocated, nullptr);
	uint8_t *pData = pAllocated.release();
	{
		auto First = TakeGraphicsUploadRelease(Release);
		auto Moved = std::move(First);
		auto Pixels = TakeGraphicsUpload(pData, true, CCountingRelease{Probe.m_PixelFrees});
		EXPECT_EQ(Release.m_pContext, nullptr);
		EXPECT_EQ(Probe.m_Releases, 0);
	}
	EXPECT_EQ(Probe.m_Releases, 1);
	EXPECT_EQ(Probe.m_PixelFreesAtRelease, 1);
}

TEST(GraphicsOwnedUpload, FailedConsumerReclaimsItsCreditAndDiscardReclaimsOnlyTheTail)
{
	std::array<SUploadReleaseProbe, 2> aProbes;
	CCommandBuffer Buffer(1024, 64);
	CTextureSlotPool<> Slots;
	for(auto &Probe : aProbes)
	{
		CUploadImage Image;
		ASSERT_NE(Image.m_Image.m_pData, nullptr);
		auto Release = Probe.Token();
		int Id;
		uint32_t Generation;
		ASSERT_TRUE(TryQueueGraphicsTextureUploadOwned(Image.m_Image, Release, Slots, AppendTextureUpload(Buffer), Id, Generation));
	}
	const auto *pCurrent = static_cast<const CCommandBuffer::SCommand_Texture_Create *>(Buffer.Head());
	ASSERT_NE(pCurrent, nullptr);
	EXPECT_THROW({
		auto Release = TakeGraphicsUploadRelease(pCurrent->m_Release);
		auto Pixels = TakeGraphicsUpload(pCurrent->m_pData, true, CCountingRelease{aProbes[0].m_PixelFrees});
		Buffer.DiscardUploads(CCountingRelease{aProbes[1].m_PixelFrees});
		EXPECT_EQ(aProbes[0].m_Releases, 0);
		throw std::bad_alloc(); }, std::bad_alloc);
	EXPECT_EQ(aProbes[0].m_Releases, 1);
	EXPECT_EQ(aProbes[0].m_PixelFreesAtRelease, 1);
	EXPECT_EQ(aProbes[1].m_Releases, 1);
	EXPECT_EQ(aProbes[1].m_PixelFreesAtRelease, 1);
	Buffer.DiscardUploads();
	EXPECT_EQ(aProbes[0].m_Releases, 1);
	EXPECT_EQ(aProbes[1].m_Releases, 1);
}

TEST(GraphicsOwnedUpload, ResetAndBufferDestructionReleaseUnconsumedCreditsOnce)
{
	SUploadReleaseProbe ResetProbe;
	SUploadReleaseProbe DestructionProbe;
	CTextureSlotPool<> Slots;
	{
		CCommandBuffer Buffer(1024, 64);
		CUploadImage First;
		ASSERT_NE(First.m_Image.m_pData, nullptr);
		auto ResetRelease = ResetProbe.Token();
		int Id;
		uint32_t Generation;
		ASSERT_TRUE(TryQueueGraphicsTextureUploadOwned(First.m_Image, ResetRelease, Slots, AppendTextureUpload(Buffer), Id, Generation));
		Buffer.Reset();
		EXPECT_EQ(ResetProbe.m_Releases, 1);
		CUploadImage Second;
		ASSERT_NE(Second.m_Image.m_pData, nullptr);
		auto DestructionRelease = DestructionProbe.Token();
		ASSERT_TRUE(TryQueueGraphicsTextureUploadOwned(Second.m_Image, DestructionRelease, Slots, AppendTextureUpload(Buffer), Id, Generation));
	}
	EXPECT_EQ(ResetProbe.m_Releases, 1);
	EXPECT_EQ(DestructionProbe.m_Releases, 1);
}
