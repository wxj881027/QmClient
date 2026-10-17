#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/shared/console.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/ui.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/test.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace
{
	// 仅替换设备：重建丢弃所有 GPU 资源并复用编号，保留代数用于检查跨设备访问。
	class CRecoveryGraphics : public CIconBenchmarkGraphics
	{
		uint32_t m_Generation = 1;
		std::unordered_map<int, uint32_t> m_Textures;

		void RequireCurrent(CTextureHandle Handle) const
		{
			if(!IsTextureHandleAllocated(Handle))
				throw std::logic_error("access to text texture from another device");
		}

	public:
		struct SUpdate
		{
			CTextureHandle m_Texture;
			int m_X, m_Y;
			size_t m_Width, m_Height;
			std::vector<uint8_t> m_vPixels;
		};
		bool m_CaptureUpdates = false;
		bool m_RejectUpdates = false;
		bool m_ThrowUpdates = false;
		int m_RejectUpdateId = -1;
		CTextureHandle m_Fill, m_Outline;
		size_t m_Dimension = 0;
		std::vector<SUpdate> m_vUpdates;
		bool m_RejectUpload = false;
		bool m_ThrowUpload = false;
		bool m_TextBuffering = true;
		int m_UploadAttempts = 0;
		int m_TextureUpdates = 0;
		int m_TextureDeletes = 0;
		int m_ContainerDeletes = 0;
		int m_QuadContainerDeletes = 0;
		int m_QuadBegins = 0;
		std::unordered_map<int, int> m_QuadCounts;
		std::vector<uint8_t> m_vLastFill;

		void ResetDevice()
		{
			++m_Generation;
			m_Textures.clear();
			m_Containers.clear();
			m_Buffers.clear();
			m_QuadCounts.clear();
			m_NextId = 1;
		}
		bool IsTextureHandleAllocated(CTextureHandle Handle) const override
		{
			const auto It = m_Textures.find(Handle.Id());
			return It != m_Textures.end() && It->second == Handle.Generation();
		}
		bool LoadTextTextures(size_t Width, size_t Height, CTextureHandle &Fill, CTextureHandle &Outline, uint8_t *pFill, uint8_t *pOutline) override
		{
			std::unique_ptr<uint8_t, decltype(&free)> pFillOwner(pFill, free), pOutlineOwner(pOutline, free);
			++m_UploadAttempts;
			if(m_ThrowUpload)
				throw std::bad_alloc();
			if(m_RejectUpload)
				return false;
			m_vLastFill.assign(pFill, pFill + Width * Height);
			Fill = CreateTextureHandle(m_NextId++, m_Generation);
			Outline = CreateTextureHandle(m_NextId++, m_Generation);
			m_Textures.emplace(Fill.Id(), Fill.Generation());
			m_Textures.emplace(Outline.Id(), Outline.Generation());
			m_Fill = Fill;
			m_Outline = Outline;
			m_Dimension = Width;
			return true;
		}
		bool UnloadTextTextures(CTextureHandle &Fill, CTextureHandle &Outline) override
		{
			for(auto *pHandle : {&Fill, &Outline})
			{
				if(!pHandle->IsValid())
					continue;
				RequireCurrent(*pHandle);
				m_Textures.erase(pHandle->Id());
				pHandle->Invalidate();
				++m_TextureDeletes;
			}
			return true;
		}
		bool UpdateTextTexture(CTextureHandle Handle, int X, int Y, size_t Width, size_t Height, uint8_t *pData, bool Moved) override
		{
			std::unique_ptr<uint8_t, decltype(&free)> pOwner(Moved ? pData : nullptr, free);
			RequireCurrent(Handle);
			++m_TextureUpdates;
			if(m_CaptureUpdates)
				m_vUpdates.push_back({Handle, X, Y, Width, Height, std::vector<uint8_t>(pData, pData + Width * Height)});
			if(m_ThrowUpdates)
				throw std::bad_alloc();
			if(m_RejectUpdates || Handle.Id() == m_RejectUpdateId)
				return false;
			pOwner.release();
			return CIconBenchmarkGraphics::UpdateTextTexture(Handle, X, Y, Width, Height, pData, Moved);
		}
		void RenderText(int Container, int Quads, int Size, CTextureHandle Fill, CTextureHandle Outline, const ColorRGBA &Color, const ColorRGBA &OutlineColor) override
		{
			RequireCurrent(Fill);
			RequireCurrent(Outline);
			CIconBenchmarkGraphics::RenderText(Container, Quads, Size, Fill, Outline, Color, OutlineColor);
		}
		void DeleteBufferContainer(int &Container, bool DestroyAll = true) override
		{
			if(Container >= 0)
				++m_ContainerDeletes;
			CIconBenchmarkGraphics::DeleteBufferContainer(Container, DestroyAll);
		}
		bool IsTextBufferingEnabled() override { return m_TextBuffering; }
		void QuadsBegin() override { ++m_QuadBegins; }
		int CreateQuadContainer(bool = true) override
		{
			const int Id = m_NextId++;
			m_QuadCounts.emplace(Id, 0);
			return Id;
		}
		int QuadContainerAddQuads(int Container, CQuadItem *, int Num) override
		{
			int &Count = m_QuadCounts.at(Container);
			const int Offset = Count;
			Count += Num;
			return Offset;
		}
		void QuadContainerReset(int Container) override
		{
			if(Container >= 0)
				m_QuadCounts.at(Container) = 0;
		}
		void QuadContainerUpload(int Container) override { (void)m_QuadCounts.at(Container); }
		void DeleteQuadContainer(int &Container) override
		{
			if(Container >= 0)
			{
				++m_QuadContainerDeletes;
				m_QuadCounts.erase(Container);
				Container = -1;
			}
		}
		void TextureSet(CTextureHandle Handle) override
		{
			if(Handle.IsValid())
				RequireCurrent(Handle);
		}
	};

	class QmTextDeviceRecovery : public ::testing::Test
	{
		CConfig m_PreviousConfig = g_Config;
		CTestInfo m_Info;
		CConsole m_Console{CFGFLAG_CLIENT};
		std::unique_ptr<IKernel> m_pKernel;
		std::unique_ptr<IStorage> m_pStorage;

	protected:
		CRecoveryGraphics m_Graphics;
		std::unique_ptr<IEngineTextRender> m_pText;
		STextContainerIndex m_Text;

		void SetUp() override
		{
			g_Config = CConfig();
			m_Info.m_DeleteTestStorageFilesOnSuccess = false;
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pKernel.reset(IKernel::Create());
			m_Graphics.m_pStorage = m_pStorage.get();
			m_pKernel->RegisterInterface<IConsole>(&m_Console, false);
			m_pKernel->RegisterInterface<IStorage>(m_pStorage.get(), false);
			m_pKernel->RegisterInterface<IGraphics>(&m_Graphics, false);
			m_pText.reset(CreateEngineTextRender());
			m_pKernel->RegisterInterface<IEngineTextRender>(m_pText.get(), false);
			ASSERT_TRUE(m_pText->Init());
			ASSERT_TRUE(m_pText->LoadFonts());
		}
		void TearDown() override
		{
			if(m_pText != nullptr)
				m_pText->Shutdown();
			g_Config = m_PreviousConfig;
		}
		bool Create(const char *pString = "Recovery", bool TrackLines = false)
		{
			CTextCursor Cursor;
			Cursor.m_FontSize = 16.0f;
			Cursor.m_TrackLineRanges = TrackLines;
			return m_pText->CreateTextContainer(m_Text, &Cursor, pString);
		}
		void CaptureUpdates()
		{
			m_pText->QmTextFrameEnd();
			m_Graphics.m_CaptureUpdates = true;
			m_Graphics.m_vUpdates.clear();
		}
		void ExpectSameUpdate(const CRecoveryGraphics::SUpdate &Before, const CRecoveryGraphics::SUpdate &After)
		{
			EXPECT_EQ(Before.m_Texture.Id(), After.m_Texture.Id());
			EXPECT_EQ(Before.m_Texture.Generation(), After.m_Texture.Generation());
			EXPECT_EQ(Before.m_X, After.m_X);
			EXPECT_EQ(Before.m_Y, After.m_Y);
			EXPECT_EQ(Before.m_Width, After.m_Width);
			EXPECT_EQ(Before.m_Height, After.m_Height);
			EXPECT_EQ(Before.m_vPixels, After.m_vPixels);
		}
		void Draw()
		{
			m_pText->RenderTextContainer(m_Text, ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 0.3f));
		}
		void DrawSweep()
		{
			STextSweepParams Params;
			Params.m_Progress = 0.5f;
			Params.m_HalfWidth = 200.0f;
			m_pText->RenderTextContainerSweep(m_Text, Params, 0.0f, 0.0f);
		}
	};
}

TEST_F(QmTextDeviceRecovery, ResetInvalidatesAliasesWithoutDeletingReusedDeviceBuffers)
{
	ASSERT_TRUE(Create());
	STextContainerIndex Alias = m_Text;
	const auto OldBuffers = m_Graphics.m_Buffers;
	const auto OldContainers = m_Graphics.m_Containers;
	ASSERT_FALSE(OldContainers.empty());
	m_Graphics.ResetDevice();
	// 新设备的其他 owner 恰好拿到旧文字的编号，旧文字不能向它们发删除命令。
	m_Graphics.m_Buffers = OldBuffers;
	m_Graphics.m_Containers = OldContainers;
	m_Graphics.m_NextId = 100;
	ASSERT_TRUE(m_pText->OnGraphicsResourcesReset());
	EXPECT_FALSE(m_Text.Valid());
	EXPECT_FALSE(Alias.Valid());
	Draw();
	m_pText->DeleteTextContainer(Alias);
	m_pText->DeleteTextContainer(m_Text);
	EXPECT_EQ(m_Graphics.m_Draws, 0u);
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 0);
	EXPECT_EQ(m_Graphics.m_Containers, OldContainers);
	EXPECT_EQ(m_Graphics.m_Buffers, OldBuffers);
}

TEST_F(QmTextDeviceRecovery, RecreatedTextUsesCurrentTexturesAndPreservesLayout)
{
	ASSERT_TRUE(Create());
	const auto Before = m_pText->GetBoundingBoxTextContainer(m_Text);
	const uint64_t Revision = m_pText->GlyphAtlasRevision();
	m_Graphics.ResetDevice();
	ASSERT_TRUE(m_pText->OnGraphicsResourcesReset());
	ASSERT_TRUE(Create());
	const auto After = m_pText->GetBoundingBoxTextContainer(m_Text);
	EXPECT_EQ(m_pText->GlyphAtlasRevision(), Revision);
	EXPECT_EQ(After.m_W, Before.m_W);
	EXPECT_EQ(After.m_H, Before.m_H);
	EXPECT_NO_THROW(Draw());
	EXPECT_EQ(m_Graphics.m_Draws, 1u);
}

TEST_F(QmTextDeviceRecovery, OldCursorCannotDeleteAReusedQuadContainer)
{
	CTextCursor Cursor;
	Cursor.m_FontSize = 16.0f;
	Cursor.m_CursorMode = TEXT_CURSOR_CURSOR_MODE_SET;
	Cursor.m_CursorCharacter = 1;
	ASSERT_TRUE(m_pText->CreateTextContainer(m_Text, &Cursor, "AB"));
	ASSERT_FALSE(m_Graphics.m_QuadCounts.empty());
	const auto OldQuads = m_Graphics.m_QuadCounts;
	m_Graphics.ResetDevice();
	m_Graphics.m_QuadCounts = OldQuads;
	m_Graphics.m_NextId = 100;
	ASSERT_TRUE(m_pText->OnGraphicsResourcesReset());
	m_pText->DeleteTextContainer(m_Text);
	EXPECT_EQ(m_Graphics.m_QuadContainerDeletes, 0);
	EXPECT_EQ(m_Graphics.m_QuadCounts, OldQuads);
}

TEST_F(QmTextDeviceRecovery, CachedLabelRebuildsAfterResetWithoutChangingItsText)
{
	CCachedText Label;
	Label.Update(m_pText.get(), "Cached label", 16.0f);
	Label.Render(m_pText.get(), vec2(0, 0), ColorRGBA(1, 1, 1, 1));
	const float Width = Label.Width();
	ASSERT_GT(Width, 0.0f);
	const auto Draws = m_Graphics.m_Draws;
	m_Graphics.ResetDevice();
	ASSERT_TRUE(m_pText->OnGraphicsResourcesReset());
	Label.Update(m_pText.get(), "Cached label", 16.0f);
	EXPECT_EQ(Label.Width(), Width);
	EXPECT_NO_THROW(Label.Render(m_pText.get(), vec2(0, 0), ColorRGBA(1, 1, 1, 1)));
	EXPECT_EQ(m_Graphics.m_Draws, Draws + 1);
}

TEST_F(QmTextDeviceRecovery, RejectedUploadSuppressesBufferedDrawAndPendingUpdates)
{
	m_Graphics.ResetDevice();
	m_Graphics.m_RejectUpload = true;
	ASSERT_FALSE(m_pText->OnGraphicsResourcesReset());
	const int Updates = m_Graphics.m_TextureUpdates;
	ASSERT_TRUE(Create());
	EXPECT_NO_THROW(Draw());
	EXPECT_NO_THROW(m_pText->QmTextFrameEnd());
	EXPECT_EQ(m_Graphics.m_Draws, 0u);
	EXPECT_EQ(m_Graphics.m_TextureUpdates, Updates);
	EXPECT_TRUE(m_Graphics.m_Containers.empty());
}

TEST_F(QmTextDeviceRecovery, RejectedUploadSuppressesUnbufferedDraw)
{
	m_Graphics.ResetDevice();
	m_Graphics.m_RejectUpload = true;
	m_Graphics.m_TextBuffering = false;
	ASSERT_FALSE(m_pText->OnGraphicsResourcesReset());
	ASSERT_TRUE(Create());
	EXPECT_NO_THROW(Draw());
	EXPECT_EQ(m_Graphics.m_QuadBegins, 0);
}

TEST_F(QmTextDeviceRecovery, RetryRestoresTextBuiltWhileTexturesWereUnavailable)
{
	m_Graphics.ResetDevice();
	m_Graphics.m_RejectUpload = true;
	ASSERT_FALSE(m_pText->OnGraphicsResourcesReset());
	ASSERT_TRUE(Create());
	Draw();
	EXPECT_EQ(m_Graphics.m_Draws, 0u);
	m_Graphics.m_RejectUpload = false;
	ASSERT_TRUE(m_pText->RestoreGraphicsResources());
	EXPECT_NO_THROW(Draw());
	EXPECT_EQ(m_Graphics.m_Draws, 1u);
	EXPECT_FALSE(m_Graphics.m_Containers.empty());
}

TEST_F(QmTextDeviceRecovery, RetryUploadsGlyphsRasterizedDuringTheFailure)
{
	m_pText->QmPrewarmGlyph('A', 16);
	m_Graphics.ResetDevice();
	ASSERT_TRUE(m_pText->OnGraphicsResourcesReset());
	const auto CountInk = [](const auto &vPixels) { return std::count_if(vPixels.begin(), vPixels.end(), [](uint8_t Pixel) { return Pixel != 0; }); };
	const auto OldInk = CountInk(m_Graphics.m_vLastFill);
	ASSERT_GT(OldInk, 0);
	m_Graphics.ResetDevice();
	m_Graphics.m_RejectUpload = true;
	ASSERT_FALSE(m_pText->OnGraphicsResourcesReset());
	m_pText->QmPrewarmGlyph('Z', 16);
	m_Graphics.m_RejectUpload = false;
	ASSERT_TRUE(m_pText->RestoreGraphicsResources());
	EXPECT_GT(CountInk(m_Graphics.m_vLastFill), OldInk);
}

TEST_F(QmTextDeviceRecovery, UploadExceptionKeepsRecoveryRetryable)
{
	m_Graphics.ResetDevice();
	m_Graphics.m_ThrowUpload = true;
	EXPECT_FALSE(m_pText->OnGraphicsResourcesReset());
	m_Graphics.m_ThrowUpload = false;
	EXPECT_TRUE(m_pText->RestoreGraphicsResources());
	ASSERT_TRUE(Create());
	EXPECT_NO_THROW(Draw());
}

TEST_F(QmTextDeviceRecovery, RepeatedRestoreKeepsTheExistingTextures)
{
	const int Attempts = m_Graphics.m_UploadAttempts;
	EXPECT_TRUE(m_pText->RestoreGraphicsResources());
	EXPECT_TRUE(m_pText->RestoreGraphicsResources());
	EXPECT_EQ(m_Graphics.m_UploadAttempts, Attempts);
	EXPECT_EQ(m_Graphics.m_TextureDeletes, 0);
}

TEST_F(QmTextDeviceRecovery, SweepRebuildsBuffersAfterResetAndWaitsForTextures)
{
	ASSERT_TRUE(Create("Sweep", true));
	DrawSweep();
	ASSERT_GT(m_Graphics.m_Draws, 0u);
	m_Graphics.ResetDevice();
	m_Graphics.m_RejectUpload = true;
	ASSERT_FALSE(m_pText->OnGraphicsResourcesReset());
	ASSERT_TRUE(Create("Sweep", true));
	const auto Draws = m_Graphics.m_Draws;
	EXPECT_NO_THROW(DrawSweep());
	EXPECT_EQ(m_Graphics.m_Draws, Draws);
	m_Graphics.m_RejectUpload = false;
	ASSERT_TRUE(m_pText->RestoreGraphicsResources());
	EXPECT_NO_THROW(DrawSweep());
	EXPECT_GT(m_Graphics.m_Draws, Draws);
}

TEST_F(QmTextDeviceRecovery, AnotherResetInvalidatesContainersCreatedDuringFailure)
{
	m_Graphics.ResetDevice();
	m_Graphics.m_RejectUpload = true;
	ASSERT_FALSE(m_pText->OnGraphicsResourcesReset());
	ASSERT_TRUE(Create());
	STextContainerIndex Alias = m_Text;
	m_Graphics.ResetDevice();
	m_Graphics.m_RejectUpload = false;
	ASSERT_TRUE(m_pText->OnGraphicsResourcesReset());
	EXPECT_FALSE(Alias.Valid());
	ASSERT_TRUE(Create());
	m_pText->DeleteTextContainer(Alias);
	EXPECT_NO_THROW(Draw());
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 0);
}

TEST_F(QmTextDeviceRecovery, ShutdownWhileRecoveryIsPendingDoesNotDeleteOldDeviceTextures)
{
	m_Graphics.ResetDevice();
	m_Graphics.m_RejectUpload = true;
	ASSERT_FALSE(m_pText->OnGraphicsResourcesReset());
	EXPECT_NO_THROW(m_pText->Shutdown());
	EXPECT_EQ(m_Graphics.m_TextureDeletes, 0);
	EXPECT_FALSE(m_pText->RestoreGraphicsResources());
}

TEST_F(QmTextDeviceRecovery, SuccessfulIncrementalUploadIsNotRepeatedForCachedGlyphs)
{
	CaptureUpdates();
	ASSERT_TRUE(Create("A"));
	Draw();
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 2u);
	EXPECT_EQ(m_Graphics.m_vUpdates[0].m_Texture.Id(), m_Graphics.m_Fill.Id());
	EXPECT_EQ(m_Graphics.m_vUpdates[1].m_Texture.Id(), m_Graphics.m_Outline.Id());
	m_pText->QmTextFrameEnd();
	m_pText->DeleteTextContainer(m_Text);
	ASSERT_TRUE(Create("A"));
	Draw();
	EXPECT_EQ(m_Graphics.m_vUpdates.size(), 2u);
	EXPECT_EQ(m_Graphics.m_Draws, 2u);
}

TEST_F(QmTextDeviceRecovery, RejectedIncrementalFillRetriesTheSamePixelsWithoutRebuildingText)
{
	CaptureUpdates();
	ASSERT_TRUE(Create("A"));
	m_Graphics.m_RejectUpdateId = m_Graphics.m_Fill.Id();
	Draw();
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 2u);
	EXPECT_EQ(m_Graphics.m_Draws, 0u);
	const auto Rejected = m_Graphics.m_vUpdates[0];
	ASSERT_TRUE(std::any_of(Rejected.m_vPixels.begin(), Rejected.m_vPixels.end(), [](uint8_t Pixel) { return Pixel != 0; }));
	m_Graphics.m_RejectUpdateId = -1;
	m_pText->QmTextFrameEnd();
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 3u);
	ExpectSameUpdate(Rejected, m_Graphics.m_vUpdates[2]);
	Draw();
	EXPECT_EQ(m_Graphics.m_Draws, 1u);
	EXPECT_EQ(m_Graphics.m_vUpdates.size(), 3u);
}

TEST_F(QmTextDeviceRecovery, RejectedOutlineRetriesWithoutResubmittingAcceptedFill)
{
	CaptureUpdates();
	ASSERT_TRUE(Create("A"));
	m_Graphics.m_RejectUpdateId = m_Graphics.m_Outline.Id();
	Draw();
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 2u);
	const auto Rejected = m_Graphics.m_vUpdates[1];
	m_Graphics.m_RejectUpdateId = -1;
	m_pText->QmTextFrameEnd();
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 3u);
	ExpectSameUpdate(Rejected, m_Graphics.m_vUpdates[2]);
	Draw();
	EXPECT_EQ(m_Graphics.m_Draws, 1u);
}

TEST_F(QmTextDeviceRecovery, IncrementalUploadExceptionRetainsBothTexturesForRetry)
{
	CaptureUpdates();
	ASSERT_TRUE(Create("A"));
	m_Graphics.m_ThrowUpdates = true;
	EXPECT_NO_THROW(Draw());
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 2u);
	const auto Rejected = m_Graphics.m_vUpdates;
	EXPECT_EQ(m_Graphics.m_Draws, 0u);
	m_Graphics.m_ThrowUpdates = false;
	EXPECT_NO_THROW(m_pText->QmTextFrameEnd());
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 4u);
	ExpectSameUpdate(Rejected[0], m_Graphics.m_vUpdates[2]);
	ExpectSameUpdate(Rejected[1], m_Graphics.m_vUpdates[3]);
	Draw();
	EXPECT_EQ(m_Graphics.m_Draws, 1u);
}

TEST_F(QmTextDeviceRecovery, FailedIncrementalUploadWaitsForFrameEndBetweenAttempts)
{
	CaptureUpdates();
	ASSERT_TRUE(Create("A"));
	m_Graphics.m_RejectUpdates = true;
	Draw();
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 2u);
	Draw();
	Draw();
	EXPECT_EQ(m_Graphics.m_vUpdates.size(), 2u);
	m_pText->QmTextFrameEnd();
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 4u);
	Draw();
	EXPECT_EQ(m_Graphics.m_vUpdates.size(), 4u);
	m_Graphics.m_RejectUpdates = false;
	m_pText->QmTextFrameEnd();
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 6u);
	Draw();
	EXPECT_EQ(m_Graphics.m_Draws, 1u);
}

TEST_F(QmTextDeviceRecovery, RetryIncludesGlyphsAddedAfterAnIncrementalFailure)
{
	CaptureUpdates();
	m_Graphics.m_RejectUpdates = true;
	m_pText->QmPrewarmGlyph('A', 16);
	m_pText->QmTextFrameEnd();
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 2u);
	const auto Rejected = m_Graphics.m_vUpdates[0];
	m_pText->QmPrewarmGlyph('Z', 16);
	m_Graphics.m_RejectUpdates = false;
	m_pText->QmTextFrameEnd();
	ASSERT_GT(m_Graphics.m_vUpdates.size(), 2u);
	const auto CountInk = [](const auto &vPixels) { return std::count_if(vPixels.begin(), vPixels.end(), [](uint8_t Pixel) { return Pixel != 0; }); };
	std::ptrdiff_t RetriedInk = 0;
	const CRecoveryGraphics::SUpdate *pRetried = nullptr;
	// 只约束像素保留，不依赖图集把两个字形分配在同一个行带。
	for(size_t Index = 2; Index < m_Graphics.m_vUpdates.size(); ++Index)
	{
		const auto &Update = m_Graphics.m_vUpdates[Index];
		if(Update.m_Texture.Id() != m_Graphics.m_Fill.Id())
			continue;
		RetriedInk += CountInk(Update.m_vPixels);
		if(Update.m_X <= Rejected.m_X && Update.m_Y <= Rejected.m_Y &&
			Update.m_X + Update.m_Width >= Rejected.m_X + Rejected.m_Width &&
			Update.m_Y + Update.m_Height >= Rejected.m_Y + Rejected.m_Height)
			pRetried = &Update;
	}
	EXPECT_GT(RetriedInk, CountInk(Rejected.m_vPixels));
	ASSERT_NE(pRetried, nullptr);
	const auto &Retried = *pRetried;
	for(size_t Row = 0; Row < Rejected.m_Height; ++Row)
	{
		const auto *pBefore = Rejected.m_vPixels.data() + Row * Rejected.m_Width;
		const auto *pAfter = Retried.m_vPixels.data() + (Rejected.m_Y - Retried.m_Y + Row) * Retried.m_Width + Rejected.m_X - Retried.m_X;
		EXPECT_TRUE(std::equal(pBefore, pBefore + Rejected.m_Width, pAfter));
	}
}

TEST_F(QmTextDeviceRecovery, FailedAtlasClearRetriesZeroPixelsInsteadOfDroppingTheUpdate)
{
	m_pText->QmPrewarmGlyph('A', 16);
	CaptureUpdates();
	m_Graphics.m_RejectUpdates = true;
	const auto Revision = m_pText->GlyphAtlasRevision();
	// 语言变体切换会清空图集并把两张纹理标记为待全量上传；这里验证这张全尺寸
	// 更新被设备拒绝后，帧末重试仍然提交完整尺寸，而不是把它当成空更新丢弃。
	// 参数是生产里传到该接口的键（g_Config.m_ClLanguagefile），不是语言名本身。
	m_pText->SetFontLanguageVariant("languages/japanese.txt");
	ASSERT_GT(m_pText->GlyphAtlasRevision(), Revision);
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 2u);
	m_Graphics.m_RejectUpdates = false;
	m_pText->QmTextFrameEnd();
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 4u);
	for(size_t Index = 2; Index < 4; ++Index)
	{
		const auto &Update = m_Graphics.m_vUpdates[Index];
		EXPECT_EQ(Update.m_Width, m_Graphics.m_Dimension);
		EXPECT_EQ(Update.m_Height, m_Graphics.m_Dimension);
		EXPECT_TRUE(std::all_of(Update.m_vPixels.begin(), Update.m_vPixels.end(), [](uint8_t Pixel) { return Pixel == 0; }));
	}
}

TEST_F(QmTextDeviceRecovery, FailedAtlasClearRetryIncludesGlyphsCreatedBeforeRecovery)
{
	CaptureUpdates();
	m_Graphics.m_RejectUpdates = true;
	m_pText->SetFontLanguageVariant("languages/japanese.txt");
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 2u);
	ASSERT_TRUE(Create("Z"));
	Draw();
	EXPECT_EQ(m_Graphics.m_Draws, 0u);
	m_Graphics.m_RejectUpdates = false;
	m_pText->QmTextFrameEnd();
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 4u);
	for(size_t Index = 2; Index < 4; ++Index)
	{
		const auto &Update = m_Graphics.m_vUpdates[Index];
		EXPECT_EQ(Update.m_Width, m_Graphics.m_Dimension);
		EXPECT_EQ(Update.m_Height, m_Graphics.m_Dimension);
		EXPECT_TRUE(std::any_of(Update.m_vPixels.begin(), Update.m_vPixels.end(), [](uint8_t Pixel) { return Pixel != 0; }));
	}
	Draw();
	EXPECT_EQ(m_Graphics.m_Draws, 1u);
}

TEST_F(QmTextDeviceRecovery, DeviceResetReplacesFailedIncrementalRegionsWithAFullUpload)
{
	CaptureUpdates();
	ASSERT_TRUE(Create("A"));
	m_Graphics.m_RejectUpdates = true;
	Draw();
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 2u);
	m_Graphics.ResetDevice();
	ASSERT_TRUE(m_pText->OnGraphicsResourcesReset());
	EXPECT_TRUE(std::any_of(m_Graphics.m_vLastFill.begin(), m_Graphics.m_vLastFill.end(), [](uint8_t Pixel) { return Pixel != 0; }));
	m_Graphics.m_RejectUpdates = false;
	m_pText->QmTextFrameEnd();
	ASSERT_TRUE(Create("A"));
	EXPECT_NO_THROW(Draw());
	EXPECT_EQ(m_Graphics.m_Draws, 1u);
	EXPECT_EQ(m_Graphics.m_vUpdates.size(), 2u);
}

TEST_F(QmTextDeviceRecovery, FailedIncrementalUpdateSuppressesUnbufferedDrawing)
{
	CaptureUpdates();
	m_Graphics.m_TextBuffering = false;
	ASSERT_TRUE(Create("A"));
	m_Graphics.m_RejectUpdates = true;
	EXPECT_NO_THROW(Draw());
	EXPECT_EQ(m_Graphics.m_QuadBegins, 0);
	EXPECT_EQ(m_Graphics.m_vUpdates.size(), 2u);
}

TEST_F(QmTextDeviceRecovery, SweepWaitsForFailedIncrementalUpdatesToRecover)
{
	CaptureUpdates();
	ASSERT_TRUE(Create("A", true));
	m_Graphics.m_RejectUpdates = true;
	DrawSweep();
	EXPECT_EQ(m_Graphics.m_Draws, 0u);
	ASSERT_EQ(m_Graphics.m_vUpdates.size(), 2u);
	m_Graphics.m_RejectUpdates = false;
	m_pText->QmTextFrameEnd();
	DrawSweep();
	EXPECT_GT(m_Graphics.m_Draws, 0u);
	EXPECT_EQ(m_Graphics.m_vUpdates.size(), 4u);
}
