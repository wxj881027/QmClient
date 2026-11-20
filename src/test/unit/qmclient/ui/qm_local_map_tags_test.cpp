#include <engine/shared/qm_storage_layout.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/local_map_tags.h>

#include <gtest/gtest.h>
#include <test/test.h>

namespace
{
	std::vector<std::string> TagsOf(const QmMapTags::CLocalTags &Tags, const char *pMap)
	{
		return Tags.Get("community", pMap);
	}
} // namespace

// 拖动排序是玩家自己排的顺序：Move 之后 Get 必须按新顺序返回，同一张图之外的
// 标签（别的 map/community）不能跟着挪位。
TEST(QmLocalMapTags, MoveReordersOnlyThisMap)
{
	QmMapTags::CLocalTags Tags;
	ASSERT_TRUE(Tags.Add(nullptr, "community", "map", "alpha"));
	ASSERT_TRUE(Tags.Add(nullptr, "community", "map", "beta"));
	ASSERT_TRUE(Tags.Add(nullptr, "community", "map", "gamma"));
	// 另一张图的标签夹在中间：跨图重排是错误来源，这里显式验证它不动。
	ASSERT_TRUE(Tags.Add(nullptr, "community", "other", "delta"));
	ASSERT_TRUE(Tags.Add(nullptr, "community", "map", "omega"));

	EXPECT_EQ(TagsOf(Tags, "map"), (std::vector<std::string>{"alpha", "beta", "gamma", "omega"}));

	// 首项拖到末尾。
	ASSERT_TRUE(Tags.Move(nullptr, "community", "map", 0, 3));
	EXPECT_EQ(TagsOf(Tags, "map"), (std::vector<std::string>{"beta", "gamma", "omega", "alpha"}));
	EXPECT_EQ(TagsOf(Tags, "other"), (std::vector<std::string>{"delta"}));

	// 末项拖回首位。
	ASSERT_TRUE(Tags.Move(nullptr, "community", "map", 3, 0));
	EXPECT_EQ(TagsOf(Tags, "map"), (std::vector<std::string>{"alpha", "beta", "gamma", "omega"}));

	// 中间相邻交换。
	ASSERT_TRUE(Tags.Move(nullptr, "community", "map", 1, 2));
	EXPECT_EQ(TagsOf(Tags, "map"), (std::vector<std::string>{"alpha", "gamma", "beta", "omega"}));

	// 越界与原地移动都是空操作，返回 false（调用点据此避免无谓落盘）。
	EXPECT_FALSE(Tags.Move(nullptr, "community", "map", 1, 1));
	EXPECT_FALSE(Tags.Move(nullptr, "community", "map", 9, 0));
	EXPECT_FALSE(Tags.Move(nullptr, "community", "map", 0, 99));
	EXPECT_EQ(TagsOf(Tags, "map"), (std::vector<std::string>{"alpha", "gamma", "beta", "omega"}));
}

TEST(QmLocalMapTags, MoveToLastIndexKeepsEveryTag)
{
	QmMapTags::CLocalTags Tags;
	ASSERT_TRUE(Tags.Add(nullptr, "", "map", "one"));
	ASSERT_TRUE(Tags.Add(nullptr, "", "map", "two"));
	// 第一张拖到末尾：两张都还在，只是顺序换了。
	ASSERT_TRUE(Tags.Move(nullptr, "", "map", 0, 1));
	EXPECT_EQ(Tags.Get("", "map"), (std::vector<std::string>{"two", "one"}));
	EXPECT_TRUE(Tags.Has("", "map", "one"));
	EXPECT_TRUE(Tags.Has("", "map", "two"));
}

TEST(QmLocalMapTags, LoadDropsTagsTheUploadEndpointWouldReject)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_TRUE(pStorage);
	ASSERT_TRUE(qm_storage::EnsureFolder(pStorage.get(), qm_storage::STATE_MAPS_DIR));
	IOHANDLE File = pStorage->OpenFile(qm_storage::LOCAL_MAP_TAGS_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_TRUE(File);
	const char aContent[] = "community\tmap\tvalid\n"
				"community\tmap\tthis-tag-is-too-long\n"
				"community\tmap\tcontrol\x01tag\n";
	ASSERT_EQ(io_write(File, aContent, sizeof(aContent) - 1), sizeof(aContent) - 1);
	ASSERT_EQ(io_close(File), 0);

	QmMapTags::CLocalTags Tags;
	Tags.Load(pStorage.get());
	EXPECT_EQ(TagsOf(Tags, "map"), (std::vector<std::string>{"valid"}));
}
