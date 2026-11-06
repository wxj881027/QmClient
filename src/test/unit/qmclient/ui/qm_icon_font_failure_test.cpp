#include <game/client/qm_icon_font_render.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <new>
#include <stdexcept>

namespace
{
	enum class EFailure
	{
		NONE,
		BEFORE_SLOT,
		AFTER_SLOT,
		DRAW,
		DELETION,
	};

	struct STextRender
	{
		ColorRGBA m_Color{0.2f, 0.3f, 0.4f, 0.5f};
		ColorRGBA m_Outline{0.7f, 0.6f, 0.5f, 0.4f};
		unsigned m_Flags = TEXT_RENDER_FLAG_NO_X_BEARING;
		EFailure m_Failure = EFailure::NONE;
		int m_Creates = 0, m_Draws = 0, m_Deletes = 0, m_Deferred = 0;
		int m_DeferredIndex = -2;
		bool m_BuiltWhiteWithoutSplits = false;
		bool m_DrewWithCallerState = false;
		ColorRGBA GetTextColor() const { return m_Color; }
		ColorRGBA GetTextOutlineColor() const { return m_Outline; }
		unsigned GetRenderFlags() const { return m_Flags; }
		void TextColor(ColorRGBA Color) { m_Color = Color; }
		void TextOutlineColor(ColorRGBA Color) { m_Outline = Color; }
		void SetRenderFlags(unsigned Flags) { m_Flags = Flags; }
		void CreateTextContainer(STextContainerIndex &Index, CTextCursor *pCursor, const char *, int)
		{
			++m_Creates;
			m_BuiltWhiteWithoutSplits = m_Color == ColorRGBA(1, 1, 1, 1) && m_Outline == ColorRGBA(1, 1, 1, 1) &&
						    (m_Flags & TEXT_RENDER_FLAG_ONE_TIME_USE) != 0 && pCursor->m_vColorSplits.empty();
			if(m_Failure == EFailure::BEFORE_SLOT)
				throw std::bad_alloc();
			Index.m_Index = 7;
			pCursor->m_X += 7.0f;
			if(m_Failure == EFailure::AFTER_SLOT)
				throw std::length_error("icon upload");
		}
		void RenderTextContainer(STextContainerIndex, ColorRGBA, ColorRGBA)
		{
			++m_Draws;
			m_DrewWithCallerState = m_Color == ColorRGBA(0.2f, 0.3f, 0.4f, 0.5f) && m_Outline == ColorRGBA(0.7f, 0.6f, 0.5f, 0.4f) && m_Flags == TEXT_RENDER_FLAG_NO_X_BEARING;
			if(m_Failure == EFailure::DRAW)
				throw 17;
		}
		void DeleteTextContainer(STextContainerIndex &Index)
		{
			++m_Deletes;
			if(m_Failure == EFailure::DELETION)
				throw std::length_error("icon delete");
			Index.Reset();
		}
		void ReleaseTemporaryTextContainer(STextContainerIndex &Index) noexcept
		{
			++m_Deferred;
			m_DeferredIndex = Index.m_Index;
			Index.Reset();
		}
	};

	CTextCursor Cursor()
	{
		CTextCursor Result;
		Result.m_Flags = TEXTFLAG_RENDER;
		Result.m_vColorSplits.emplace_back(0, 1, ColorRGBA(0.1f, 0.2f, 0.3f, 0.4f), ColorRGBA(0.5f, 0.6f, 0.7f, 0.8f));
		return Result;
	}

	void Draw(STextRender &Text, CTextCursor &Cursor)
	{
		QmRenderImmediateFontIcon(Text, &Cursor, "icon", -1, {0.9f, 0.8f, 0.2f, 0.25f}, {0, 0, 0, 0.0875f});
	}

	void ExpectRestored(const STextRender &Text, const CTextCursor &Cursor, const STextColorSplit *pSplits)
	{
		EXPECT_EQ(Text.m_Color, ColorRGBA(0.2f, 0.3f, 0.4f, 0.5f));
		EXPECT_EQ(Text.m_Outline, ColorRGBA(0.7f, 0.6f, 0.5f, 0.4f));
		EXPECT_EQ(Text.m_Flags, TEXT_RENDER_FLAG_NO_X_BEARING);
		EXPECT_EQ(Cursor.m_vColorSplits.data(), pSplits);
		ASSERT_EQ(Cursor.m_vColorSplits.size(), 1u);
		EXPECT_EQ(Cursor.m_vColorSplits[0].m_CharIndex, 0);
		EXPECT_EQ(Cursor.m_vColorSplits[0].m_Length, 1);
		EXPECT_EQ(Cursor.m_vColorSplits[0].m_Color, ColorRGBA(0.1f, 0.2f, 0.3f, 0.4f));
		EXPECT_EQ(Cursor.m_vColorSplits[0].m_ColorEnd, ColorRGBA(0.5f, 0.6f, 0.7f, 0.8f));
	}
}

TEST(QmIconFontFailure, PreparationAllocationFailureRestoresStateWithoutFurtherAllocationOrDeviceCalls)
{
	bool ReachedSuccess = false;
	for(size_t BeforeFailure = 0; BeforeFailure < 64; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		STextRender Text;
		auto Input = Cursor();
		const auto *pSplits = Input.m_vColorSplits.data();
		bool Threw = false, Triggered = false;
		size_t Attempts = 0;
		{
			CPredictionAllocationFailure Failure(BeforeFailure);
			try
			{
				Draw(Text, Input);
			}
			catch(const std::bad_alloc &)
			{
				Threw = true;
			}
			Triggered = Failure.Triggered();
			Attempts = Failure.Attempts();
		}
		ExpectRestored(Text, Input, pSplits);
		EXPECT_EQ(Text.m_Deferred, 0);
		if(!Triggered)
		{
			EXPECT_FALSE(Threw);
			EXPECT_EQ(Text.m_Creates, 1);
			EXPECT_EQ(Text.m_Draws, 1);
			EXPECT_EQ(Text.m_Deletes, 1);
			ReachedSuccess = true;
			break;
		}
		EXPECT_TRUE(Threw);
		EXPECT_EQ(Attempts, BeforeFailure + 1);
		EXPECT_EQ(Text.m_Creates, 0);
		EXPECT_EQ(Text.m_Draws, 0);
		EXPECT_EQ(Text.m_Deletes, 0);
	}
	EXPECT_TRUE(ReachedSuccess);
}

TEST(QmIconFontFailure, CreateExceptionRestoresOriginalSplitStorageAndTransfersOnlyItsHandle)
{
	for(bool HasSlot : {false, true})
	{
		SCOPED_TRACE(HasSlot);
		STextRender Text;
		Text.m_Failure = HasSlot ? EFailure::AFTER_SLOT : EFailure::BEFORE_SLOT;
		auto Input = Cursor();
		const auto *pSplits = Input.m_vColorSplits.data();
		if(HasSlot)
			EXPECT_THROW(Draw(Text, Input), std::length_error);
		else
			EXPECT_THROW(Draw(Text, Input), std::bad_alloc);
		EXPECT_TRUE(Text.m_BuiltWhiteWithoutSplits);
		EXPECT_EQ(Text.m_Creates, 1);
		EXPECT_EQ(Text.m_Draws, 0);
		EXPECT_EQ(Text.m_Deletes, 0);
		EXPECT_EQ(Text.m_Deferred, 1);
		EXPECT_EQ(Text.m_DeferredIndex, HasSlot ? 7 : -1);
		ExpectRestored(Text, Input, pSplits);
	}
}

TEST(QmIconFontFailure, DrawExceptionPropagatesAfterCallerStateRestorationAndTransfersOnce)
{
	STextRender Text;
	Text.m_Failure = EFailure::DRAW;
	auto Input = Cursor();
	const auto *pSplits = Input.m_vColorSplits.data();
	int Error = 0;
	try
	{
		Draw(Text, Input);
	}
	catch(int Value)
	{
		Error = Value;
	}
	EXPECT_EQ(Error, 17);
	EXPECT_TRUE(Text.m_DrewWithCallerState);
	EXPECT_EQ(Text.m_Draws, 1);
	EXPECT_EQ(Text.m_Deletes, 0);
	EXPECT_EQ(Text.m_Deferred, 1);
	EXPECT_EQ(Text.m_DeferredIndex, 7);
	ExpectRestored(Text, Input, pSplits);
}

TEST(QmIconFontFailure, DeleteExceptionTransfersOnceWithoutRetryingDeviceDeletion)
{
	STextRender Text;
	Text.m_Failure = EFailure::DELETION;
	auto Input = Cursor();
	const auto *pSplits = Input.m_vColorSplits.data();
	EXPECT_THROW(Draw(Text, Input), std::length_error);
	EXPECT_EQ(Text.m_Draws, 1);
	EXPECT_EQ(Text.m_Deletes, 1);
	EXPECT_EQ(Text.m_Deferred, 1);
	EXPECT_EQ(Text.m_DeferredIndex, 7);
	ExpectRestored(Text, Input, pSplits);
}

TEST(QmIconFontFailure, RepeatedFailuresAndLaterSuccessKeepCallerSplitStorage)
{
	STextRender Text;
	auto Input = Cursor();
	const auto *pSplits = Input.m_vColorSplits.data();
	for(EFailure Failure : {EFailure::AFTER_SLOT, EFailure::DRAW, EFailure::NONE})
	{
		Text.m_Failure = Failure;
		bool Threw = false;
		try
		{
			Draw(Text, Input);
		}
		catch(...)
		{
			Threw = true;
		}
		EXPECT_EQ(Threw, Failure != EFailure::NONE);
		ExpectRestored(Text, Input, pSplits);
	}
	EXPECT_EQ(Text.m_Creates, 3);
	EXPECT_EQ(Text.m_Draws, 2);
	EXPECT_EQ(Text.m_Deletes, 1);
	EXPECT_EQ(Text.m_Deferred, 2);
}
