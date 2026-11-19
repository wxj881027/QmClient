#include <game/client/components/qmclient/music_lyrics/music_world_lyrics.h>

#include <test/support/prediction_allocation_failure.h>
#include <test/support/qm_real_ui_fixture.h>

#include <cstddef>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
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

	void FailOnce(EFailure &Failure)
	{
		const EFailure Requested = std::exchange(Failure, EFailure::NONE);
		if(Requested == EFailure::ALLOCATION)
			throw std::bad_alloc();
		if(Requested == EFailure::LENGTH)
			throw std::length_error("controlled world lyric device limit");
		if(Requested == EFailure::OTHER)
			throw std::logic_error("unrelated world lyric device failure");
	}

	class CWorldGraphics : public qm_ui_test::CRealUiTestGraphics
	{
	public:
		EFailure m_DrawFailure = EFailure::NONE;
		EFailure m_ContainerDeleteFailure = EFailure::NONE;
		int m_ContainerDeleteCalls = 0;

		void DeleteBufferContainer(int &Index, bool DestroyAll = true) override
		{
			++m_ContainerDeleteCalls;
			FailOnce(m_ContainerDeleteFailure);
			CRealUiTestGraphics::DeleteBufferContainer(Index, DestroyAll);
		}

		void RenderText(int Container, int Quads, int TextureSize, CTextureHandle Texture, CTextureHandle Outline, const ColorRGBA &Color, const ColorRGBA &OutlineColor) override
		{
			FailOnce(m_DrawFailure);
			CRealUiTestGraphics::RenderText(Container, Quads, TextureSize, Texture, Outline, Color, OutlineColor);
		}
	};

	class MusicWorldLyricsRenderer : public qm_ui_test::CRealUiFixture
	{
		CWorldGraphics m_Graphics;
		bool m_Ready = false;

	protected:
		QmMusicLyrics::CWorldLyricsState m_State;
		QmMusicLyrics::CWorldLyricsRenderer m_Renderer;
		qm_ui_test::CRealUiTestGraphics *UiGraphics() override { return &m_Graphics; }
		CWorldGraphics &Device() { return m_Graphics; }
		ITextRender &Text() { return *m_Ui.TextRender(); }

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			m_Ready = true;
			SetLine("abc");
			ASSERT_TRUE(m_State.Current().m_Active);
			PrepareCallerState();
		}

		void TearDown() override
		{
			if(m_Ready)
			{
				Device().m_DrawFailure = Device().m_ContainerDeleteFailure = EFailure::NONE;
				Device().m_FailBufferObjectCreate = Device().m_FailBufferContainerCreate = false;
				Device().m_BufferObjectDeleteFailure = qm_ui_test::CRealUiTestGraphics::EDeleteFailure::NONE;
				m_Renderer.Reset(Text());
			}
			CRealUiFixture::TearDown();
		}

		void SetLine(std::string_view Original, std::string_view Translation = {}, int Language = 0)
		{
			QmMusicLyrics::SSourceSnapshot Source{42, 1, 100, true, nullptr, Original, Translation, 1000};
			m_State.Update(true, 1, Source, Language, vec2(40, 80), 0.0);
		}

		void PrepareCallerState()
		{
			Device().MapScreen(100, 200, 900, 700);
			Text().SetFontPreset(EFontPreset::DEFAULT_FONT);
			Text().SetRenderFlags(TEXT_RENDER_FLAG_NO_X_BEARING);
			Text().TextColor({0.2f, 0.3f, 0.4f, 0.5f});
			Text().TextOutlineColor({0.6f, 0.7f, 0.8f, 0.9f});
			Text().TextSelectionColor({0.3f, 0.5f, 0.7f, 0.8f});
		}

		void ExpectCallerState()
		{
			float X0, Y0, X1, Y1;
			Device().GetScreen(&X0, &Y0, &X1, &Y1);
			EXPECT_FLOAT_EQ(X0, 100);
			EXPECT_FLOAT_EQ(Y0, 200);
			EXPECT_FLOAT_EQ(X1, 900);
			EXPECT_FLOAT_EQ(Y1, 700);
			EXPECT_EQ(Text().GetFontPreset(), EFontPreset::DEFAULT_FONT);
			EXPECT_EQ(Text().GetRenderFlags(), TEXT_RENDER_FLAG_NO_X_BEARING);
			EXPECT_EQ(Text().GetTextColor(), ColorRGBA(0.2f, 0.3f, 0.4f, 0.5f));
			EXPECT_EQ(Text().GetTextOutlineColor(), ColorRGBA(0.6f, 0.7f, 0.8f, 0.9f));
			EXPECT_EQ(Text().GetTextSelectionColor(), ColorRGBA(0.3f, 0.5f, 0.7f, 0.8f));
		}

		void Draw()
		{
			m_Renderer.Render(Text(), Device(), m_State, vec2(40, 80), 2.0f, 1.0);
		}
	};
}

TEST_F(MusicWorldLyricsRenderer, SuccessfulRowsReuseTheirContainerAndRestoreCallerState)
{
	ASSERT_NO_THROW(Draw());
	ExpectCallerState();
	ASSERT_EQ(Device().m_Buffers.size(), 1u);
	ASSERT_EQ(Device().m_Containers.size(), 1u);
	ASSERT_FALSE(Device().m_vTextSubmissions.empty());
	const int Container = Device().m_Containers.begin()->first;
	const int Buffer = Device().m_Containers.begin()->second;
	const int Deletes = Device().m_ContainerDeleteCalls;
	const auto Draws = Device().m_Draws;
	ASSERT_NO_THROW(Draw());
	ExpectCallerState();
	EXPECT_EQ(Device().m_ContainerDeleteCalls, Deletes);
	ASSERT_EQ(Device().m_Containers.size(), 1u);
	EXPECT_EQ(Device().m_Containers.begin()->first, Container);
	EXPECT_EQ(Device().m_Containers.begin()->second, Buffer);
	EXPECT_GT(Device().m_Draws, Draws);
	m_Renderer.Reset(Text());
	EXPECT_TRUE(Device().m_Buffers.empty());
	EXPECT_TRUE(Device().m_Containers.empty());
}

TEST_F(MusicWorldLyricsRenderer, MetadataAllocationFailureKeepsTheOldContainerUntilTheSameChangeCanRetry)
{
	const std::string Original(96, 'o');
	const std::string Translation(96, 't');
	m_State.Reset();
	SetLine(Original, Translation);
	ASSERT_NO_THROW(Draw());
	ASSERT_EQ(Device().m_Containers.size(), 1u);
	const int Container = Device().m_Containers.begin()->first;
	const int Buffer = Device().m_Containers.begin()->second;
	const int Deletes = Device().m_ContainerDeleteCalls;
	const auto Draws = Device().m_Draws;
	SetLine(Original, Translation, 1);
	bool Triggered;
	{
		CPredictionAllocationFailure Failure(0);
		Draw();
		Triggered = Failure.Triggered();
	}
	ASSERT_TRUE(Triggered);
	ExpectCallerState();
	EXPECT_EQ(Device().m_ContainerDeleteCalls, Deletes);
	EXPECT_EQ(Device().m_Draws, Draws);
	ASSERT_EQ(Device().m_Containers.size(), 1u);
	EXPECT_EQ(Device().m_Containers.begin()->first, Container);
	EXPECT_EQ(Device().m_Containers.begin()->second, Buffer);
	ASSERT_NO_THROW(Draw());
	ExpectCallerState();
	ASSERT_EQ(Device().m_Containers.size(), 1u);
	EXPECT_NE(Device().m_Containers.begin()->first, Container);
	EXPECT_FALSE(Device().m_Buffers.contains(Buffer));
	EXPECT_GT(Device().m_Draws, Draws);
}

TEST_F(MusicWorldLyricsRenderer, NativePoolRefusalReturnsFalseAndTheSameRowRetries)
{
	STextContainerIndex Occupied;
	CTextCursor Probe;
	Probe.m_Flags = 0;
	Probe.m_FontSize = 22.0f;
	// 保持唯一已分配槽位存活，使下一次领取必须经过真实pool的make_unique。
	ASSERT_TRUE(Text().CreateTextContainer(Occupied, &Probe, ""));
	ASSERT_EQ(Occupied.m_Index, 0);
	g_Config.m_QmPerfDebug = 1;
	Text().FlushQmTextRuntimeBudgetLog();
	bool Triggered;
	std::size_t Attempts;
	{
		CPredictionAllocationFailure Failure(0);
		Draw();
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_EQ(Attempts, 1u);
	// 真实Create的计数在pool领取前递增，排除前面的文本候选先抛异常。
	Text().FlushQmTextRuntimeBudgetLog();
	EXPECT_EQ(Text().QmTextRuntimeBudgetSnapshot().m_TextContainerNew, 1);
	ExpectCallerState();
	EXPECT_TRUE(Device().m_Buffers.empty());
	EXPECT_TRUE(Device().m_Containers.empty());
	EXPECT_TRUE(Device().m_vTextSubmissions.empty());
	ASSERT_NO_THROW(Draw());
	ExpectCallerState();
	EXPECT_EQ(Device().m_Buffers.size(), 1u);
	EXPECT_EQ(Device().m_Containers.size(), 1u);
	EXPECT_FALSE(Device().m_vTextSubmissions.empty());
	m_Renderer.Reset(Text());
	Text().DeleteTextContainer(Occupied);
	EXPECT_TRUE(Device().m_Buffers.empty());
	EXPECT_TRUE(Device().m_Containers.empty());
}

TEST_F(MusicWorldLyricsRenderer, BufferSubmissionFailuresKeepPartialOwnershipAndRetryWithoutLeaks)
{
	for(const bool FailContainer : {false, true})
	{
		SCOPED_TRACE(FailContainer);
		if(FailContainer)
			Device().m_FailBufferContainerCreate = true;
		else
			Device().m_FailBufferObjectCreate = true;
		ASSERT_NO_THROW(Draw());
		ExpectCallerState();
		EXPECT_EQ(Device().m_Buffers.size(), FailContainer ? 1u : 0u);
		EXPECT_TRUE(Device().m_Containers.empty());
		EXPECT_TRUE(Device().m_vTextSubmissions.empty());
		ASSERT_NO_THROW(Draw());
		ExpectCallerState();
		EXPECT_EQ(Device().m_Buffers.size(), 1u);
		EXPECT_EQ(Device().m_Containers.size(), 1u);
		EXPECT_FALSE(Device().m_vTextSubmissions.empty());
		m_Renderer.Reset(Text());
		EXPECT_TRUE(Device().m_Buffers.empty());
		EXPECT_TRUE(Device().m_Containers.empty());
		Device().m_vTextSubmissions.clear();
	}
}

TEST_F(MusicWorldLyricsRenderer, DrawFailuresRestoreCallerStateAndOnlyUnknownErrorsPropagate)
{
	ASSERT_NO_THROW(Draw());
	ASSERT_EQ(Device().m_Containers.size(), 1u);
	const int Container = Device().m_Containers.begin()->first;
	for(const EFailure Failure : {EFailure::ALLOCATION, EFailure::LENGTH, EFailure::OTHER})
	{
		SCOPED_TRACE(static_cast<int>(Failure));
		const auto Draws = Device().m_Draws;
		Device().m_DrawFailure = Failure;
		if(Failure == EFailure::OTHER)
			EXPECT_THROW(Draw(), std::logic_error);
		else
			EXPECT_NO_THROW(Draw());
		ExpectCallerState();
		EXPECT_EQ(Device().m_Draws, Draws);
		ASSERT_EQ(Device().m_Containers.size(), 1u);
		EXPECT_EQ(Device().m_Containers.begin()->first, Container);
		ASSERT_NO_THROW(Draw());
		ExpectCallerState();
		EXPECT_GT(Device().m_Draws, Draws);
		EXPECT_EQ(Device().m_Containers.begin()->first, Container);
	}
}

TEST_F(MusicWorldLyricsRenderer, ResetDeletionFailuresKeepTheOwnerAndRebuildTheSameContent)
{
	for(const EFailure Failure : {EFailure::ALLOCATION, EFailure::LENGTH, EFailure::OTHER})
	{
		SCOPED_TRACE(static_cast<int>(Failure));
		ASSERT_NO_THROW(Draw());
		ASSERT_EQ(Device().m_Containers.size(), 1u);
		const int Container = Device().m_Containers.begin()->first;
		const int Buffer = Device().m_Containers.begin()->second;
		Device().m_ContainerDeleteFailure = Failure;
		if(Failure == EFailure::OTHER)
			EXPECT_THROW(m_Renderer.Reset(Text()), std::logic_error);
		else
			EXPECT_NO_THROW(m_Renderer.Reset(Text()));
		ExpectCallerState();
		ASSERT_EQ(Device().m_Containers.size(), 1u);
		EXPECT_TRUE(Device().m_Containers.contains(Container));
		EXPECT_TRUE(Device().m_Buffers.contains(Buffer));
		ASSERT_NO_THROW(Draw());
		ExpectCallerState();
		ASSERT_EQ(Device().m_Containers.size(), 1u);
		EXPECT_NE(Device().m_Containers.begin()->first, Container);
		EXPECT_FALSE(Device().m_Buffers.contains(Buffer));
		m_Renderer.Reset(Text());
		EXPECT_TRUE(Device().m_Buffers.empty());
		EXPECT_TRUE(Device().m_Containers.empty());
	}
}
