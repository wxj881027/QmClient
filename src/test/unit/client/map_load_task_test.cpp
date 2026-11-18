#include <game/map/map_load_task.h>

#include <gtest/gtest.h>

#include <memory>

namespace
{
	CMapLoadTask PreparePixels(std::shared_ptr<int> pPixels)
	{
		++*pPixels;
		co_await std::suspend_always{};
		++*pPixels;
	}

	CMapLoadTask PrepareLayer(std::shared_ptr<int> pPixels, int &Published)
	{
		co_await PreparePixels(pPixels);
		Published = *pPixels;
	}
}

TEST(MapLoadTask, LayerPublishesOnlyAfterItsChildFinishes)
{
	auto pPixels = std::make_shared<int>(0);
	int Published = 0;
	auto Task = PrepareLayer(pPixels, Published);
	for(int Step = 0; *pPixels == 0 && Step < 8; ++Step)
		EXPECT_FALSE(Task.RunUntil(CMapLoadTask::TClock::time_point::min()));
	ASSERT_EQ(*pPixels, 1);
	EXPECT_EQ(Published, 0);
	Task.Finish();
	EXPECT_TRUE(Task.Resume());
	EXPECT_EQ(Published, 2);
}

TEST(MapLoadTask, ReplacingASuspendedLayerReleasesItsBuffersWithoutPublishing)
{
	auto pOld = std::make_shared<int>(0);
	std::weak_ptr<int> OldPixels = pOld;
	int Published = 0;
	auto Task = PrepareLayer(pOld, Published);
	for(int Step = 0; *pOld == 0 && Step < 8; ++Step)
		ASSERT_FALSE(Task.Resume());
	ASSERT_EQ(*pOld, 1);
	pOld.reset();
	Task = PrepareLayer(std::make_shared<int>(20), Published);
	EXPECT_TRUE(OldPixels.expired());
	EXPECT_EQ(Published, 0);
	Task.Finish();
	EXPECT_EQ(Published, 22);
}
