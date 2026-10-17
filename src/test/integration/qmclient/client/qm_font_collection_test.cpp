#include <engine/client/qm_font_collection.h>
#include <engine/client/qm_font_face_registry.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <new>
#include <vector>

namespace
{
	struct SReleaseState
	{
		int m_FacesReleased = 0;
		int m_DataReleases = 0;
		int m_FacesAtDataRelease = 0;
		bool m_FaceReleasedAfterData = false;
	};

	struct STrackedData
	{
		SReleaseState *m_pState;
	};

	void FreeTrackedData(void *pData) noexcept
	{
		auto *pHeader = static_cast<STrackedData *>(pData) - 1;
		++pHeader->m_pState->m_DataReleases;
		pHeader->m_pState->m_FacesAtDataRelease = pHeader->m_pState->m_FacesReleased;
		free(pHeader);
	}

	CQmFontCollection::TData CopyData(const void *pData, size_t Size, SReleaseState &State)
	{
		void *pMemory = malloc(sizeof(STrackedData) + Size);
		if(pMemory == nullptr)
			throw std::bad_alloc();
		auto *pHeader = new(pMemory) STrackedData{&State};
		std::memcpy(pHeader + 1, pData, Size);
		return {pHeader + 1, FreeTrackedData};
	}

	void TrackFaces(const CQmFontCollection &Collection, SReleaseState &State)
	{
		for(FT_Face Face : Collection.Faces())
		{
			Face->generic.data = &State;
			// FreeType 在销毁 face 时传入该 face，自定义数据从 generic 取回。
			Face->generic.finalizer = [](void *pObject) {
				auto &Release = *static_cast<SReleaseState *>(static_cast<FT_Face>(pObject)->generic.data);
				++Release.m_FacesReleased;
				Release.m_FaceReleasedAfterData |= Release.m_DataReleases != 0;
			};
		}
	}

	class QmFontCollection : public ::testing::Test
	{
	protected:
		FT_Library m_Library = nullptr;
		void SetUp() override { ASSERT_EQ(FT_Init_FreeType(&m_Library), 0); }
		void TearDown() override
		{
			if(m_Library != nullptr)
				FT_Done_FreeType(m_Library);
		}
	};
}

TEST_F(QmFontCollection, LoadedFaceUsesOwnedBytesAndReleasesThemAfterTheFace)
{
	std::ifstream File(TestSourcePath("data/fonts/DejaVuSans.ttf"), std::ios::binary);
	ASSERT_TRUE(File.good());
	const std::vector<char> vBytes{std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>()};
	ASSERT_FALSE(vBytes.empty());
	SReleaseState Released;
	auto pData = CopyData(vBytes.data(), vBytes.size(), Released);
	const void *pOriginalData = pData.get();
	auto pCollection = CQmFontCollection::Load(m_Library, "fixture", std::move(pData), vBytes.size());
	ASSERT_NE(pCollection, nullptr);
	EXPECT_EQ(pData, nullptr);
	EXPECT_EQ(pCollection->Data(), pOriginalData);
	ASSERT_EQ(pCollection->Faces().size(), 1u);
	TrackFaces(*pCollection, Released);
	{
		CQmFontFaceRegistry Registry;
		Registry.AddCollection(pCollection->Faces(), false);
		EXPECT_EQ(Registry.Names().at(pCollection->Faces().front()).m_Family, "DejaVu Sans");
		EXPECT_EQ(FT_Set_Pixel_Sizes(pCollection->Faces().front(), 0, 16), 0);
		EXPECT_EQ(FT_Load_Char(pCollection->Faces().front(), 'A', FT_LOAD_RENDER), 0);
	}
	EXPECT_EQ(Released.m_DataReleases, 0);
	pCollection.reset();
	EXPECT_EQ(Released.m_FacesReleased, 1);
	EXPECT_EQ(Released.m_DataReleases, 1);
	EXPECT_EQ(Released.m_FacesAtDataRelease, 1);
	EXPECT_FALSE(Released.m_FaceReleasedAfterData);
}

TEST_F(QmFontCollection, EveryCollectionFaceIsReleasedBeforeTheSharedFile)
{
	std::ifstream File(TestSourcePath("data/fonts/SourceHanSans.ttc"), std::ios::binary);
	ASSERT_TRUE(File.good());
	const std::vector<char> vBytes{std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>()};
	ASSERT_FALSE(vBytes.empty());
	SReleaseState Released;
	auto pCollection = CQmFontCollection::Load(m_Library, "collection fixture", CopyData(vBytes.data(), vBytes.size(), Released), vBytes.size());
	ASSERT_NE(pCollection, nullptr);
	const int Count = static_cast<int>(pCollection->Faces().size());
	ASSERT_GT(Count, 1);
	TrackFaces(*pCollection, Released);
	pCollection.reset();
	EXPECT_EQ(Released.m_FacesReleased, Count);
	EXPECT_EQ(Released.m_FacesAtDataRelease, Count);
	EXPECT_EQ(Released.m_DataReleases, 1);
	EXPECT_FALSE(Released.m_FaceReleasedAfterData);
}

TEST_F(QmFontCollection, InvalidFontFreesTheAdoptedFile)
{
	SReleaseState Released;
	const char aInvalid[] = "not a font";
	EXPECT_EQ(CQmFontCollection::Load(m_Library, "invalid", CopyData(aInvalid, sizeof(aInvalid), Released), sizeof(aInvalid)), nullptr);
	EXPECT_EQ(Released.m_DataReleases, 1);
	EXPECT_EQ(Released.m_FacesReleased, 0);
}

TEST_F(QmFontCollection, EmptyFileIsRejectedAndReleased)
{
	SReleaseState Released;
	EXPECT_EQ(CQmFontCollection::Load(m_Library, "empty", CopyData("", 0, Released), 0), nullptr);
	EXPECT_EQ(Released.m_DataReleases, 1);
}

TEST_F(QmFontCollection, UnrepresentableLengthIsRejectedBeforeReadingTheBuffer)
{
	SReleaseState Released;
	const size_t TooLarge = static_cast<size_t>(std::numeric_limits<FT_Long>::max()) + 1;
	EXPECT_EQ(CQmFontCollection::Load(m_Library, "oversized", CopyData("a", 1, Released), TooLarge), nullptr);
	EXPECT_EQ(Released.m_DataReleases, 1);
}

TEST_F(QmFontCollection, MissingLibraryStillReleasesTheTransferredFile)
{
	SReleaseState Released;
	EXPECT_EQ(CQmFontCollection::Load(nullptr, "uninitialized", CopyData("a", 1, Released), 1), nullptr);
	EXPECT_EQ(Released.m_DataReleases, 1);
}
