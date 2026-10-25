#include <game/client/components/qmclient/community_icon_store.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <cstdlib>
#include <map>
#include <memory_resource>
#include <new>
#include <set>
#include <stdexcept>

namespace
{
	using qm_community_icons::CStore;
	using qm_community_icons::EPublishResult;

	SHA256_DIGEST Digest(unsigned char Value)
	{
		SHA256_DIGEST Result{};
		Result.data[0] = Value;
		return Result;
	}

	class CFailureResource : public std::pmr::memory_resource
	{
		void *do_allocate(size_t Bytes, size_t Alignment) override
		{
			if(m_Fail)
				throw std::bad_alloc();
			return std::pmr::new_delete_resource()->allocate(Bytes, Alignment);
		}
		void do_deallocate(void *pMemory, size_t Bytes, size_t Alignment) override
		{
			std::pmr::new_delete_resource()->deallocate(pMemory, Bytes, Alignment);
		}
		bool do_is_equal(const std::pmr::memory_resource &Other) const noexcept override { return this == &Other; }

	public:
		bool m_Fail = false;
	};

	// 只替换上传/释放设备边界，不重写生产发布与回收逻辑。
	class CIconGraphics : public CIconBenchmarkGraphics
	{
	public:
		int m_LoadCalls = 0;
		int m_InvalidAt = 0;
		int m_ThrowAt = 0;
		bool m_NullTexture = false;
		std::set<int> m_Live;
		std::set<int> m_FailDeletes;
		std::map<int, int> m_DeleteAttempts;

		CTextureHandle LoadTextureRawMove(CImageInfo &Image, int Flags, const char *pName = nullptr) override
		{
			++m_LoadCalls;
			if(m_LoadCalls == m_ThrowAt)
				throw std::runtime_error("upload failed before transfer");
			Image.Free();
			if(m_LoadCalls == m_InvalidAt)
				return {};
			if(m_NullTexture)
				return CreateTextureHandle(0);
			const int Id = m_NextId++;
			m_Live.insert(Id);
			return CreateTextureHandle(Id, 7);
		}

		void UnloadTexture(CTextureHandle *pTexture) override
		{
			const int Id = pTexture->Id();
			++m_DeleteAttempts[Id];
			if(m_FailDeletes.contains(Id))
				throw std::runtime_error("delete command not accepted");
			EXPECT_EQ(m_Live.erase(Id), 1u);
			pTexture->Invalidate();
		}
	};

	struct SImages
	{
		CImageInfo m_Original;
		CImageInfo m_Grayscale;
		SImages()
		{
			for(auto *pImage : {&m_Original, &m_Grayscale})
			{
				pImage->m_Width = pImage->m_Height = 1;
				pImage->m_Format = CImageInfo::FORMAT_RGBA;
				pImage->m_pData = static_cast<uint8_t *>(std::malloc(4));
				if(!pImage->m_pData)
				{
					m_Original.Free();
					throw std::bad_alloc();
				}
			}
		}
		~SImages()
		{
			m_Original.Free();
			m_Grayscale.Free();
		}
	};

	class QmCommunityIconStore : public ::testing::Test
	{
	protected:
		CFailureResource m_Memory;
		CIconGraphics m_Graphics;
		CStore m_Store{&m_Memory};

		EPublishResult Publish(const char *pId = "a", unsigned char Hash = 1)
		{
			SImages Images;
			return m_Store.Publish(&m_Graphics, pId, Digest(Hash), Images.m_Original, Images.m_Grayscale);
		}
		void TearDown() override
		{
			m_Graphics.m_FailDeletes.clear();
			EXPECT_TRUE(m_Store.Clear(&m_Graphics));
			EXPECT_TRUE(m_Graphics.m_Live.empty());
		}
	};
}

TEST_F(QmCommunityIconStore, CompletePairPublishesBothGenerationsAndCurrentDigest)
{
	ASSERT_EQ(Publish(), EPublishResult::PUBLISHED);
	const auto *pIcon = m_Store.Find("a");
	ASSERT_NE(pIcon, nullptr);
	EXPECT_EQ(pIcon->Texture(true).Id(), 1);
	EXPECT_EQ(pIcon->Texture(false).Id(), 2);
	EXPECT_EQ(pIcon->Texture(true).Generation(), 7u);
	EXPECT_TRUE(m_Store.IsCurrent("a", Digest(1)));
	EXPECT_FALSE(m_Store.IsCurrent("a", Digest(2)));
}

TEST_F(QmCommunityIconStore, FirstInvalidUploadDoesNotPublishOrUploadGrayscale)
{
	m_Graphics.m_InvalidAt = 1;
	EXPECT_EQ(Publish(), EPublishResult::FAILED);
	EXPECT_EQ(m_Store.Find("a"), nullptr);
	EXPECT_EQ(m_Graphics.m_LoadCalls, 1);
	EXPECT_TRUE(m_Graphics.m_Live.empty());
}

TEST_F(QmCommunityIconStore, SecondInvalidUploadKeepsOldPairAndReleasesCandidate)
{
	ASSERT_EQ(Publish(), EPublishResult::PUBLISHED);
	m_Graphics.m_InvalidAt = 4;
	EXPECT_EQ(Publish("a", 2), EPublishResult::FAILED);
	ASSERT_NE(m_Store.Find("a"), nullptr);
	EXPECT_EQ(m_Store.Find("a")->Texture(true).Id(), 1);
	EXPECT_TRUE(m_Store.IsCurrent("a", Digest(1)));
	EXPECT_EQ(m_Graphics.m_Live, (std::set<int>{1, 2}));
	EXPECT_EQ(m_Graphics.m_DeleteAttempts[3], 1);
}

TEST_F(QmCommunityIconStore, ThrownSecondUploadRetainsFailedCandidateCleanupAndDefersNextImages)
{
	ASSERT_EQ(Publish(), EPublishResult::PUBLISHED);
	m_Graphics.m_ThrowAt = 4;
	m_Graphics.m_FailDeletes.insert(3);
	EXPECT_EQ(Publish("a", 2), EPublishResult::FAILED);
	EXPECT_TRUE(m_Store.IsCurrent("a", Digest(1)));
	SImages Images;
	const auto *pOriginal = Images.m_Original.m_pData;
	const auto *pGray = Images.m_Grayscale.m_pData;
	EXPECT_EQ(m_Store.Publish(&m_Graphics, "a", Digest(2), Images.m_Original, Images.m_Grayscale), EPublishResult::DEFERRED);
	EXPECT_EQ(m_Graphics.m_LoadCalls, 4);
	EXPECT_EQ(Images.m_Original.m_pData, pOriginal);
	EXPECT_EQ(Images.m_Grayscale.m_pData, pGray);
	m_Graphics.m_FailDeletes.clear();
	EXPECT_EQ(m_Store.Publish(&m_Graphics, "a", Digest(2), Images.m_Original, Images.m_Grayscale), EPublishResult::PUBLISHED);
	EXPECT_TRUE(m_Store.IsCurrent("a", Digest(2)));
	EXPECT_EQ(m_Graphics.m_Live, (std::set<int>{4, 5}));
}

TEST_F(QmCommunityIconStore, StorageAllocationFailureLeavesImagesUntouchedAndRetryable)
{
	SImages Images;
	const auto *pOriginal = Images.m_Original.m_pData;
	const auto *pGray = Images.m_Grayscale.m_pData;
	m_Memory.m_Fail = true;
	EXPECT_EQ(m_Store.Publish(&m_Graphics, "a", Digest(1), Images.m_Original, Images.m_Grayscale), EPublishResult::DEFERRED);
	EXPECT_EQ(m_Graphics.m_LoadCalls, 0);
	EXPECT_EQ(Images.m_Original.m_pData, pOriginal);
	EXPECT_EQ(Images.m_Grayscale.m_pData, pGray);
	m_Memory.m_Fail = false;
	EXPECT_EQ(m_Store.Publish(&m_Graphics, "a", Digest(1), Images.m_Original, Images.m_Grayscale), EPublishResult::PUBLISHED);
	EXPECT_EQ(Images.m_Original.m_pData, nullptr);
	EXPECT_EQ(Images.m_Grayscale.m_pData, nullptr);
}

TEST_F(QmCommunityIconStore, ReplacementNeedsNoListAllocationAndKeepsRetiredPairUntilReleased)
{
	ASSERT_EQ(Publish(), EPublishResult::PUBLISHED);
	m_Memory.m_Fail = true;
	m_Graphics.m_FailDeletes.insert(1);
	ASSERT_EQ(Publish("a", 2), EPublishResult::PUBLISHED);
	ASSERT_NE(m_Store.Find("a"), nullptr);
	EXPECT_EQ(m_Store.Find("a")->Texture(true).Id(), 3);
	EXPECT_EQ(m_Store.Find("a")->Texture(false).Id(), 4);
	EXPECT_TRUE(m_Store.IsCurrent("a", Digest(2)));
	EXPECT_EQ(m_Graphics.m_Live, (std::set<int>{1, 3, 4}));
	EXPECT_FALSE(m_Store.Drain(&m_Graphics));
	EXPECT_EQ(m_Graphics.m_DeleteAttempts[2], 1);
	m_Graphics.m_FailDeletes.clear();
	EXPECT_TRUE(m_Store.Drain(&m_Graphics));
	EXPECT_EQ(m_Graphics.m_Live, (std::set<int>{3, 4}));
	EXPECT_EQ(m_Graphics.m_DeleteAttempts[2], 1);
}

TEST_F(QmCommunityIconStore, NewCommunityAllocationFailureKeepsPublishedPairAndRetriesLater)
{
	ASSERT_EQ(Publish(), EPublishResult::PUBLISHED);
	m_Memory.m_Fail = true;
	EXPECT_EQ(Publish("b"), EPublishResult::DEFERRED);
	EXPECT_EQ(m_Graphics.m_LoadCalls, 2);
	EXPECT_TRUE(m_Store.IsCurrent("a", Digest(1)));
	EXPECT_EQ(m_Store.Find("b"), nullptr);
	m_Memory.m_Fail = false;
	EXPECT_EQ(Publish("b"), EPublishResult::PUBLISHED);
	EXPECT_TRUE(m_Store.IsCurrent("a", Digest(1)));
	EXPECT_TRUE(m_Store.IsCurrent("b", Digest(1)));
}

TEST_F(QmCommunityIconStore, PruneHidesRemovedCommunityAndRetriesDeletionWithoutRemovingSurvivor)
{
	ASSERT_EQ(Publish(), EPublishResult::PUBLISHED);
	ASSERT_EQ(Publish("b"), EPublishResult::PUBLISHED);
	const std::vector<CCommunity> Communities{CCommunity("b", "B", Digest(1), "https://example.test/b")};
	m_Graphics.m_FailDeletes.insert(1);
	EXPECT_FALSE(m_Store.Prune(Communities, &m_Graphics));
	EXPECT_EQ(m_Store.Find("a"), nullptr);
	EXPECT_TRUE(m_Store.IsCurrent("b", Digest(1)));
	EXPECT_EQ(m_Graphics.m_Live, (std::set<int>{1, 3, 4}));
	m_Graphics.m_FailDeletes.clear();
	EXPECT_TRUE(m_Store.Drain(&m_Graphics));
	EXPECT_EQ(m_Graphics.m_Live, (std::set<int>{3, 4}));
	EXPECT_EQ(m_Graphics.m_DeleteAttempts[2], 1);
}

TEST_F(QmCommunityIconStore, ClearHidesEntriesAndPreservesFailedHandleForRepeatedClear)
{
	ASSERT_EQ(Publish(), EPublishResult::PUBLISHED);
	m_Graphics.m_FailDeletes.insert(2);
	EXPECT_FALSE(m_Store.Clear(&m_Graphics));
	EXPECT_EQ(m_Store.Find("a"), nullptr);
	EXPECT_EQ(m_Graphics.m_Live, (std::set<int>{2}));
	m_Graphics.m_FailDeletes.clear();
	EXPECT_TRUE(m_Store.Clear(&m_Graphics));
	EXPECT_TRUE(m_Store.Clear(&m_Graphics));
	EXPECT_EQ(m_Graphics.m_DeleteAttempts[1], 1);
	EXPECT_EQ(m_Graphics.m_DeleteAttempts[2], 2);
	EXPECT_TRUE(m_Graphics.m_Live.empty());
}

TEST_F(QmCommunityIconStore, MissingDeviceDefersUploadAndRetainsExistingCleanup)
{
	EXPECT_TRUE(m_Store.Clear(nullptr));
	SImages Images;
	EXPECT_EQ(m_Store.Publish(nullptr, "a", Digest(1), Images.m_Original, Images.m_Grayscale), EPublishResult::DEFERRED);
	EXPECT_NE(Images.m_Original.m_pData, nullptr);
	ASSERT_EQ(Publish(), EPublishResult::PUBLISHED);
	EXPECT_FALSE(m_Store.Clear(nullptr));
	EXPECT_EQ(m_Store.Find("a"), nullptr);
	EXPECT_EQ(m_Graphics.m_Live.size(), 2u);
	EXPECT_TRUE(m_Store.Drain(&m_Graphics));
	EXPECT_TRUE(m_Graphics.m_Live.empty());
}

TEST_F(QmCommunityIconStore, UploadExceptionAllowsLaterIndependentPublication)
{
	m_Graphics.m_ThrowAt = 1;
	EXPECT_EQ(Publish(), EPublishResult::FAILED);
	EXPECT_EQ(m_Store.Find("a"), nullptr);
	EXPECT_EQ(Publish(), EPublishResult::PUBLISHED);
	EXPECT_TRUE(m_Store.IsCurrent("a", Digest(1)));
}

TEST_F(QmCommunityIconStore, SharedNullTextureRemainsValidAndIsNeverDeleted)
{
	m_Graphics.m_NullTexture = true;
	ASSERT_EQ(Publish(), EPublishResult::PUBLISHED);
	ASSERT_NE(m_Store.Find("a"), nullptr);
	EXPECT_TRUE(m_Store.Find("a")->Texture(true).IsNullTexture());
	EXPECT_TRUE(m_Store.Find("a")->Texture(false).IsNullTexture());
	EXPECT_TRUE(m_Store.Clear(&m_Graphics));
	EXPECT_TRUE(m_Graphics.m_DeleteAttempts.empty());
}

TEST_F(QmCommunityIconStore, PublishedMetadataDoesNotBorrowCallerStorage)
{
	char aId[] = "a";
	auto Sha256 = Digest(1);
	SImages Images;
	ASSERT_EQ(m_Store.Publish(&m_Graphics, aId, Sha256, Images.m_Original, Images.m_Grayscale), EPublishResult::PUBLISHED);
	aId[0] = 'b';
	Sha256 = Digest(2);
	EXPECT_TRUE(m_Store.IsCurrent("a", Digest(1)));
	EXPECT_EQ(m_Store.Find("b"), nullptr);
}
