#include <gtest/gtest.h>

#include <game/client/components/background.h>

#include <vector>

namespace
{
enum class EResource
{
	RENDERER,
	IMAGES,
	LAYERS,
	MAP,
};

struct SMapResource
{
	EResource m_Resource;
	std::vector<EResource> &m_vReleased;
	bool m_FailUnloadOnce = false;
	bool m_Released = false;

	void UnloadMap() { Unload(); }

	void Unload()
	{
		if(m_FailUnloadOnce)
		{
			m_FailUnloadOnce = false;
			throw std::bad_alloc();
		}
		if(!m_Released)
		{
			m_vReleased.push_back(m_Resource);
			m_Released = true;
		}
	}
};

struct SBackgroundResources
{
	std::vector<EResource> m_vReleased;
	SMapResource m_Renderer{EResource::RENDERER, m_vReleased};
	SMapResource m_Images{EResource::IMAGES, m_vReleased};
	SMapResource m_Layers{EResource::LAYERS, m_vReleased};
	SMapResource m_Map{EResource::MAP, m_vReleased};

	bool Release()
	{
		return EntityBackgroundMap::ReleaseResources(m_Renderer, m_Images, m_Layers, m_Map);
	}
};

TEST(EntityBackgroundLoad, PendingStepKeepsItsSeparateResult)
{
	EXPECT_EQ(EntityBackgroundMap::TryLoadStep([] { return false; }), EntityBackgroundMap::ELoadResult::NOT_READY);
	EXPECT_EQ(EntityBackgroundMap::TryLoadStep([] { return true; }), EntityBackgroundMap::ELoadResult::READY);
}

TEST(EntityBackgroundLoad, KnownPreparationExceptionsReportFailure)
{
	EXPECT_EQ(EntityBackgroundMap::TryLoadStep([]() -> bool { throw std::bad_alloc(); }), EntityBackgroundMap::ELoadResult::FAILED);
	EXPECT_EQ(EntityBackgroundMap::TryLoadStep([]() -> bool { throw std::length_error("test size"); }), EntityBackgroundMap::ELoadResult::FAILED);
}

TEST(EntityBackgroundLoad, ReleasesPrivateMapAfterAllBorrowers)
{
	SBackgroundResources Background;
	EXPECT_TRUE(Background.Release());
	const std::vector<EResource> vExpected{EResource::RENDERER, EResource::IMAGES, EResource::LAYERS, EResource::MAP};
	EXPECT_EQ(Background.m_vReleased, vExpected);
}

TEST(EntityBackgroundLoad, RendererCleanupFailureLeavesAllPrivateResourcesForRetry)
{
	SBackgroundResources Background;
	Background.m_Renderer.m_FailUnloadOnce = true;
	EXPECT_FALSE(Background.Release());
	EXPECT_TRUE(Background.m_vReleased.empty());
	ASSERT_TRUE(Background.Release());
	const std::vector<EResource> vExpected{EResource::RENDERER, EResource::IMAGES, EResource::LAYERS, EResource::MAP};
	EXPECT_EQ(Background.m_vReleased, vExpected);
}

TEST(EntityBackgroundLoad, TextureCleanupFailureLeavesPrivateLayerAndMapBorrowingIntact)
{
	SBackgroundResources Background;
	Background.m_Images.m_FailUnloadOnce = true;
	EXPECT_FALSE(Background.Release());
	EXPECT_EQ(Background.m_vReleased, std::vector<EResource>{EResource::RENDERER});
	ASSERT_TRUE(Background.Release());
	const std::vector<EResource> vExpected{EResource::RENDERER, EResource::IMAGES, EResource::LAYERS, EResource::MAP};
	EXPECT_EQ(Background.m_vReleased, vExpected);
}
} // namespace
