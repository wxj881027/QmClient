#include <game/client/components/tclient/qm_mod_weapon_text.h>

#include <gtest/gtest.h>

#include <new>
#include <stdexcept>
#include <string>

namespace
{
	enum class EFailure
	{
		NONE,
		EMPTY,
		BEFORE_SLOT,
		AFTER_SLOT,
		MEASURE,
		DRAW,
		DELETE,
	};
	struct SText
	{
		unsigned m_Flags = TEXT_RENDER_FLAG_NO_X_BEARING;
		unsigned m_CreateFlags = 0, m_DrawFlags = 0;
		EFailure m_Failure = EFailure::NONE;
		int m_Creates = 0, m_Draws = 0, m_Deletes = 0, m_Releases = 0, m_ReleasedIndex = -2;
		float m_FontSize = 0;
		std::string m_Text;
		vec2 m_Position;
		ColorRGBA m_Color, m_Outline;
		unsigned GetRenderFlags() const { return m_Flags; }
		void SetRenderFlags(unsigned Flags) { m_Flags = Flags; }
		ColorRGBA DefaultTextOutlineColor() const { return {0, 0, 0, 0.3f}; }
		bool CreateTextContainer(STextContainerIndex &Index, CTextCursor *pCursor, const char *pText)
		{
			++m_Creates;
			m_CreateFlags = m_Flags;
			m_FontSize = pCursor->m_FontSize;
			m_Text = pText;
			if(m_Failure == EFailure::BEFORE_SLOT)
				throw std::bad_alloc();
			if(m_Failure == EFailure::EMPTY)
				return false;
			Index.m_Index = 17;
			if(m_Failure == EFailure::AFTER_SLOT)
				throw std::length_error("partial mod text creation");
			return true;
		}
		STextBoundingBox GetBoundingBoxTextContainer(STextContainerIndex) const
		{
			if(m_Failure == EFailure::MEASURE)
				throw 23;
			return {0, 0, 80, 25};
		}
		void RenderTextContainer(STextContainerIndex, ColorRGBA Color, ColorRGBA Outline, float X, float Y)
		{
			++m_Draws;
			m_DrawFlags = m_Flags;
			m_Position = {X, Y};
			m_Color = Color;
			m_Outline = Outline;
			if(m_Failure == EFailure::DRAW)
				throw std::bad_alloc();
		}
		void DeleteTextContainer(STextContainerIndex &Index)
		{
			++m_Deletes;
			if(m_Failure == EFailure::DELETE)
				throw std::length_error("mod text deletion");
			Index.Reset();
		}
		void ReleaseTemporaryTextContainer(STextContainerIndex &Index) noexcept
		{
			++m_Releases;
			m_ReleasedIndex = Index.m_Index;
			Index.Reset();
		}
	};
	std::optional<float> Draw(SText &Text)
	{
		return QmRenderModWeaponText(Text, "rcon kill_pl", 15, {100, 200}, {0.1f, 0.2f, 0.3f, 0.4f});
	}
}

TEST(QmModWeaponText, SuccessKeepsFontColorAndCenteredPlacementAndRestoresIncomingFlags)
{
	SText Text;
	const auto Height = Draw(Text);
	ASSERT_TRUE(Height);
	EXPECT_EQ(*Height, 25);
	EXPECT_EQ(Text.m_CreateFlags, TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | TEXT_RENDER_FLAG_ONE_TIME_USE);
	EXPECT_EQ(Text.m_DrawFlags, TEXT_RENDER_FLAG_NO_X_BEARING);
	EXPECT_EQ(Text.m_Flags, TEXT_RENDER_FLAG_NO_X_BEARING);
	EXPECT_EQ(Text.m_FontSize, 15);
	EXPECT_EQ(Text.m_Text, "rcon kill_pl");
	EXPECT_EQ(Text.m_Position, vec2(60, 200));
	EXPECT_EQ(Text.m_Color, ColorRGBA(0.1f, 0.2f, 0.3f, 0.4f));
	EXPECT_EQ(Text.m_Outline, ColorRGBA(0, 0, 0, 0.3f));
	EXPECT_EQ(Text.m_Deletes, 1);
	EXPECT_EQ(Text.m_Releases, 0);
}

TEST(QmModWeaponText, UnpublishedContainerHasNoDrawOrHeightAndRestoresFlags)
{
	SText Text;
	Text.m_Failure = EFailure::EMPTY;
	EXPECT_FALSE(Draw(Text));
	EXPECT_EQ(Text.m_Flags, TEXT_RENDER_FLAG_NO_X_BEARING);
	EXPECT_EQ(Text.m_Draws, 0);
	EXPECT_EQ(Text.m_Deletes, 1);
	EXPECT_EQ(Text.m_Releases, 0);
}

TEST(QmModWeaponText, CreationFailureBeforeSlotRestoresFlagsAndDoesNotAttemptDeviceDeletion)
{
	SText Text;
	Text.m_Failure = EFailure::BEFORE_SLOT;
	EXPECT_THROW(Draw(Text), std::bad_alloc);
	EXPECT_EQ(Text.m_Flags, TEXT_RENDER_FLAG_NO_X_BEARING);
	EXPECT_EQ(Text.m_Deletes, 0);
	EXPECT_EQ(Text.m_Draws, 0);
	EXPECT_EQ(Text.m_Releases, 1);
	EXPECT_EQ(Text.m_ReleasedIndex, -1);
}

TEST(QmModWeaponText, PartialCreationTransfersItsExistingSlotAndPreservesTheError)
{
	SText Text;
	Text.m_Failure = EFailure::AFTER_SLOT;
	EXPECT_THROW(Draw(Text), std::length_error);
	EXPECT_EQ(Text.m_Flags, TEXT_RENDER_FLAG_NO_X_BEARING);
	EXPECT_EQ(Text.m_Deletes, 0);
	EXPECT_EQ(Text.m_Draws, 0);
	EXPECT_EQ(Text.m_Releases, 1);
	EXPECT_EQ(Text.m_ReleasedIndex, 17);
}

TEST(QmModWeaponText, MeasurementFailureTransfersTheSlotAndStillPropagates)
{
	SText Text;
	Text.m_Failure = EFailure::MEASURE;
	EXPECT_THROW(Draw(Text), int);
	EXPECT_EQ(Text.m_Flags, TEXT_RENDER_FLAG_NO_X_BEARING);
	EXPECT_EQ(Text.m_Draws, 0);
	EXPECT_EQ(Text.m_Deletes, 0);
	EXPECT_EQ(Text.m_Releases, 1);
	EXPECT_EQ(Text.m_ReleasedIndex, 17);
}

TEST(QmModWeaponText, DrawingFailureTransfersTheSlotWithFlagsAlreadyRestored)
{
	SText Text;
	Text.m_Failure = EFailure::DRAW;
	EXPECT_THROW(Draw(Text), std::bad_alloc);
	EXPECT_EQ(Text.m_DrawFlags, TEXT_RENDER_FLAG_NO_X_BEARING);
	EXPECT_EQ(Text.m_Flags, TEXT_RENDER_FLAG_NO_X_BEARING);
	EXPECT_EQ(Text.m_Deletes, 0);
	EXPECT_EQ(Text.m_Releases, 1);
	EXPECT_EQ(Text.m_ReleasedIndex, 17);
}

TEST(QmModWeaponText, DeleteFailureTransfersOwnershipWithoutASecondDeleteAttempt)
{
	SText Text;
	Text.m_Failure = EFailure::DELETE;
	EXPECT_THROW(Draw(Text), std::length_error);
	EXPECT_EQ(Text.m_Flags, TEXT_RENDER_FLAG_NO_X_BEARING);
	EXPECT_EQ(Text.m_Deletes, 1);
	EXPECT_EQ(Text.m_Releases, 1);
	EXPECT_EQ(Text.m_ReleasedIndex, 17);
}

TEST(QmModWeaponText, LaterCallCanCreateAndDeleteNormallyAfterPartialCreationFailure)
{
	SText Text;
	Text.m_Failure = EFailure::AFTER_SLOT;
	EXPECT_THROW(Draw(Text), std::length_error);
	Text.m_Failure = EFailure::NONE;
	EXPECT_TRUE(Draw(Text));
	EXPECT_EQ(Text.m_Flags, TEXT_RENDER_FLAG_NO_X_BEARING);
	EXPECT_EQ(Text.m_Creates, 2);
	EXPECT_EQ(Text.m_Draws, 1);
	EXPECT_EQ(Text.m_Deletes, 1);
	EXPECT_EQ(Text.m_Releases, 1);
}
