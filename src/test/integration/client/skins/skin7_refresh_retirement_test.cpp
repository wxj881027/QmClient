#include <game/client/components/skins7.h>

#include <gtest/gtest.h>
#include <test/support/skin7_test_graphics.h>

#include <array>

namespace
{
	class CRetirementGraphics : public CSkin7TestGraphics
	{
	public:
		int m_FailUnloadId = -1;
		int m_KeepHandleId = -1;
		std::array<int, 64> m_aUnloads{};
		void UnloadTexture(CTextureHandle *pHandle) override
		{
			if(pHandle->IsValid() && !pHandle->IsNullTexture())
			{
				++m_aUnloads.at(pHandle->Id());
				if(pHandle->Id() == m_FailUnloadId)
					throw std::bad_alloc();
				if(pHandle->Id() == m_KeepHandleId)
					return;
			}
			CSkin7TestGraphics::UnloadTexture(pHandle);
		}
		void QuadsDraw(CQuadItem *, int Num) override { m_Quads += Num; }
	};

	class CQmSkin7RefreshRetirementTest : public ::testing::Test
	{
	protected:
		std::vector<CSkins7::CSkinPart> m_aActive[protocol7::NUM_SKINPARTS];
		std::array<std::vector<CSkins7::CSkinPart>, protocol7::NUM_SKINPARTS> m_aPrepared;
		CRetirementGraphics m_Graphics;
		CQmSkin7RefreshRetirement<CSkins7::CSkinPart> m_Retirement;

		void AddPart(int Type, bool Outline = false)
		{
			std::array<uint8_t, 8> aPixels{12, 34, 56, 255, 12, 34, 56, 255};
			CImageInfo Image;
			Image.m_Width = 2;
			Image.m_Height = 1;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = aPixels.data();
			CSkins7::CSkinPart Part;
			Part.m_Type = Type;
			Part.m_Flags = CSkins7::SKINFLAG_STANDARD;
			str_copy(Part.m_aName, "old");
			Part.m_OriginalTexture = m_Graphics.LoadTextureRaw(Image, 0, "original");
			Part.m_ColorableTexture = m_Graphics.LoadTextureRaw(Image, 0, "gray");
			if(Outline)
			{
				Part.m_QmSkinOutline = std::make_shared<CQmSkinOutline>(Image, ivec2(0, 0), ivec2(1, 0), ivec2(1, 1), vec2(1, 1));
				Part.m_QmSkinOutline->Render(&m_Graphics, vec2(0, 0), vec2(1, 1), 0, ColorRGBA(1, 1, 1, 1), 1);
			}
			m_aActive[Type].push_back(std::move(Part));
		}

		void Adopt() { m_Retirement.Adopt(m_aActive, m_aPrepared); }

		void TearDown() override
		{
			m_Graphics.m_FailUnloadId = m_Graphics.m_KeepHandleId = -1;
			m_Graphics.m_FailUnload = false;
			EXPECT_TRUE(m_Retirement.Drain(m_Graphics, 64));
			Adopt();
			EXPECT_TRUE(m_Retirement.Drain(m_Graphics, 64));
			EXPECT_EQ(m_Graphics.LiveCount(), 0);
		}
	};
}

TEST_F(CQmSkin7RefreshRetirementTest, AdoptMovesOldOwnershipAndPublishesPreparedVectorWithoutUnloading)
{
	AddPart(0);
	auto *pOld = m_aActive[0].data();
	CSkins7::CSkinPart NewPart;
	NewPart.m_Type = 0;
	NewPart.m_Flags = 0;
	str_copy(NewPart.m_aName, "new placeholder");
	m_aPrepared[0].push_back(std::move(NewPart));
	Adopt();
	ASSERT_EQ(m_aActive[0].size(), 1u);
	EXPECT_STREQ(m_aActive[0][0].m_aName, "new placeholder");
	EXPECT_NE(m_aActive[0].data(), pOld);
	EXPECT_FALSE(m_Retirement.IsEmpty());
	EXPECT_EQ(m_Graphics.LiveCount(), 2);
	EXPECT_TRUE(m_Retirement.Drain(m_Graphics, 1));
	EXPECT_TRUE(m_Retirement.IsEmpty());
}

TEST_F(CQmSkin7RefreshRetirementTest, FirstTextureFailureRetainsBothHandlesForLaterCall)
{
	AddPart(0);
	const int Original = m_aActive[0][0].m_OriginalTexture.Id();
	m_Graphics.m_FailUnloadId = Original;
	Adopt();
	EXPECT_FALSE(m_Retirement.Drain(m_Graphics, 32));
	EXPECT_FALSE(m_Retirement.IsEmpty());
	EXPECT_EQ(m_Graphics.LiveCount(), 2);
	m_Graphics.m_FailUnloadId = -1;
	EXPECT_TRUE(m_Retirement.Drain(m_Graphics, 32));
	EXPECT_EQ(m_Graphics.m_aUnloads[Original], 2);
	EXPECT_EQ(m_Graphics.LiveCount(), 0);
}

TEST_F(CQmSkin7RefreshRetirementTest, SecondTextureFailureDoesNotRepeatSuccessfulOriginalUnload)
{
	AddPart(0);
	const int Original = m_aActive[0][0].m_OriginalTexture.Id();
	const int Gray = m_aActive[0][0].m_ColorableTexture.Id();
	m_Graphics.m_FailUnloadId = Gray;
	Adopt();
	EXPECT_FALSE(m_Retirement.Drain(m_Graphics, 32));
	EXPECT_EQ(m_Graphics.LiveCount(), 1);
	m_Graphics.m_FailUnloadId = -1;
	EXPECT_TRUE(m_Retirement.Drain(m_Graphics, 32));
	EXPECT_EQ(m_Graphics.m_aUnloads[Original], 1);
	EXPECT_EQ(m_Graphics.m_aUnloads[Gray], 2);
}

TEST_F(CQmSkin7RefreshRetirementTest, NativeOutlineUnloadFailureRetainsMaskAndTextureUntilRetry)
{
	AddPart(0, true);
	auto pOutline = m_aActive[0][0].m_QmSkinOutline;
	const int OutlineId = m_Graphics.m_NextId - 1;
	ASSERT_EQ(m_Graphics.LiveCount(), 3);
	m_Graphics.m_FailUnloadId = OutlineId;
	Adopt();
	EXPECT_FALSE(m_Retirement.Drain(m_Graphics, 32));
	EXPECT_EQ(m_Graphics.LiveCount(), 1);
	auto Before = pOutline->BuildImage(1);
	EXPECT_NE(Before.m_pData, nullptr);
	Before.Free();
	m_Graphics.m_FailUnloadId = -1;
	EXPECT_TRUE(m_Retirement.Drain(m_Graphics, 32));
	auto After = pOutline->BuildImage(1);
	EXPECT_EQ(After.m_pData, nullptr);
	After.Free();
	EXPECT_EQ(m_Graphics.LiveCount(), 0);
}

TEST_F(CQmSkin7RefreshRetirementTest, PartBudgetDefersRemainingRecordsWithoutReleasingThem)
{
	AddPart(0);
	AddPart(0);
	AddPart(5);
	Adopt();
	EXPECT_FALSE(m_Retirement.Drain(m_Graphics, 1));
	EXPECT_EQ(m_Graphics.LiveCount(), 4);
	EXPECT_FALSE(m_Retirement.Drain(m_Graphics, 1));
	EXPECT_EQ(m_Graphics.LiveCount(), 2);
	EXPECT_TRUE(m_Retirement.Drain(m_Graphics, 1));
	EXPECT_EQ(m_Graphics.LiveCount(), 0);
}

TEST_F(CQmSkin7RefreshRetirementTest, DeadlinePredicateCanDeferBeforeFirstResource)
{
	AddPart(0);
	Adopt();
	EXPECT_FALSE(m_Retirement.Drain(m_Graphics, 32, [] { return false; }));
	EXPECT_EQ(m_Graphics.LiveCount(), 2);
	EXPECT_TRUE(m_Retirement.Drain(m_Graphics, 32, [] { return true; }));
}

TEST_F(CQmSkin7RefreshRetirementTest, ThrowingBudgetPredicateRetainsWholeRecord)
{
	AddPart(0);
	Adopt();
	EXPECT_FALSE(m_Retirement.Drain(m_Graphics, 32, []() -> bool { throw std::bad_alloc(); }));
	EXPECT_EQ(m_Graphics.LiveCount(), 2);
	EXPECT_TRUE(m_Retirement.Drain(m_Graphics, 32));
}

TEST_F(CQmSkin7RefreshRetirementTest, ZeroBudgetDoesNotReleaseOwnedTextures)
{
	AddPart(0);
	Adopt();
	EXPECT_FALSE(m_Retirement.Drain(m_Graphics, 0));
	EXPECT_EQ(m_Graphics.LiveCount(), 2);
	EXPECT_TRUE(m_Retirement.Drain(m_Graphics, 1));
}

TEST_F(CQmSkin7RefreshRetirementTest, SuccessfulDrainCanBeRepeatedWithoutDoubleUnload)
{
	AddPart(0);
	const int Original = m_aActive[0][0].m_OriginalTexture.Id();
	Adopt();
	ASSERT_TRUE(m_Retirement.Drain(m_Graphics, 32));
	EXPECT_TRUE(m_Retirement.Drain(m_Graphics, 32));
	EXPECT_EQ(m_Graphics.m_aUnloads[Original], 1);
}

TEST_F(CQmSkin7RefreshRetirementTest, NextGenerationCanReuseRetirementAfterPreviousRecordsFinish)
{
	AddPart(0);
	Adopt();
	ASSERT_TRUE(m_Retirement.Drain(m_Graphics, 32));
	AddPart(1);
	Adopt();
	EXPECT_FALSE(m_Retirement.IsEmpty());
	EXPECT_EQ(m_Graphics.LiveCount(), 2);
	EXPECT_TRUE(m_Retirement.Drain(m_Graphics, 32));
}

TEST_F(CQmSkin7RefreshRetirementTest, LiveHandleNotInvalidatedByBoundaryIsNotDiscarded)
{
	AddPart(0);
	m_Graphics.m_KeepHandleId = m_aActive[0][0].m_OriginalTexture.Id();
	Adopt();
	EXPECT_FALSE(m_Retirement.Drain(m_Graphics, 32));
	EXPECT_FALSE(m_Retirement.IsEmpty());
	EXPECT_EQ(m_Graphics.LiveCount(), 2);
	m_Graphics.m_KeepHandleId = -1;
	EXPECT_TRUE(m_Retirement.Drain(m_Graphics, 32));
}

TEST_F(CQmSkin7RefreshRetirementTest, CpuPayloadRemainsOwnedWhileTextureReleaseFails)
{
	AddPart(0);
	auto pAvatar = std::make_shared<QmChatAvatar::SSource>();
	std::weak_ptr<const QmChatAvatar::SSource> Weak = pAvatar;
	m_aActive[0][0].m_pChatAvatarOriginal = std::move(pAvatar);
	m_Graphics.m_FailUnload = true;
	Adopt();
	EXPECT_FALSE(m_Retirement.Drain(m_Graphics, 32));
	EXPECT_FALSE(Weak.expired());
	m_Graphics.m_FailUnload = false;
	EXPECT_TRUE(m_Retirement.Drain(m_Graphics, 32));
	EXPECT_TRUE(Weak.expired());
}

TEST_F(CQmSkin7RefreshRetirementTest, AllPartTypesAndEmptyRecordsDrainWithoutGpuAllocations)
{
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; ++Part)
	{
		CSkins7::CSkinPart Empty;
		Empty.m_Type = Part;
		Empty.m_Flags = 0;
		Empty.m_aName[0] = '\0';
		m_aActive[Part].push_back(std::move(Empty));
	}
	Adopt();
	EXPECT_TRUE(m_Retirement.Drain(m_Graphics, protocol7::NUM_SKINPARTS));
	EXPECT_EQ(m_Graphics.m_Attempts, 0);
}
