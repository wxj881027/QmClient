#include <game/client/components/qmclient/media_paths.h>

#include <gtest/gtest.h>

#include <new>

using qmclient::media_paths::TryOpenMedia;

TEST(QmMediaOpen, CompleteActivePathsReachPlatformInRecorderOrder)
{
	std::string Root;
	EXPECT_TRUE(TryOpenMedia<3>(Root, [] { return std::string("/目录"); }, [](size_t i) { return i != 1; }, [](size_t i) { return i == 0 ? "/目录/first.demo" : "/目录/last.demo"; }, [](const char *pRoot, const char *const *ppExcluded, size_t Count) {
		EXPECT_STREQ(pRoot, "/目录");
		EXPECT_EQ(Count, 2u);
		if(Count == 2)
		{
			EXPECT_STREQ(ppExcluded[0], "/目录/first.demo");
			EXPECT_STREQ(ppExcluded[1], "/目录/last.demo");
		}
		return true; }));
}

TEST(QmMediaOpen, NoRecordingStillOpensRoot)
{
	std::string Root;
	EXPECT_TRUE(TryOpenMedia<2>(Root, [] { return "root"; }, [](size_t) { return false; }, [](size_t) -> std::string { ADD_FAILURE(); return {}; }, [](const char *, const char *const *, size_t Count) { EXPECT_EQ(Count, 0u); return true; }));
}

TEST(QmMediaOpen, EmptyRootStopsBeforeInspectingRecorders)
{
	std::string Root;
	EXPECT_FALSE(TryOpenMedia<2>(Root, [] { return ""; }, [](size_t) { ADD_FAILURE(); return true; }, [](size_t) { return "path"; }, [](const char *, const char *const *, size_t) { ADD_FAILURE(); return true; }));
}

TEST(QmMediaOpen, RootAllocationExceptionDoesNotReachPlatform)
{
	std::string Root;
	EXPECT_FALSE(TryOpenMedia<2>(Root, []() -> std::string { throw std::bad_alloc(); }, [](size_t) { ADD_FAILURE(); return true; }, [](size_t) { return "path"; }, [](const char *, const char *const *, size_t) { ADD_FAILURE(); return true; }));
}

TEST(QmMediaOpen, ExclusionPreparationExceptionDoesNotPublishPrefix)
{
	std::string Root;
	EXPECT_FALSE(TryOpenMedia<2>(Root, [] { return "root"; }, [](size_t) { return true; }, [](size_t i) { if(i == 1) throw std::bad_alloc(); return "first"; }, [](const char *, const char *const *, size_t) { ADD_FAILURE(); return true; }));
	EXPECT_EQ(Root, "root");
}

TEST(QmMediaOpen, UnresolvedActivePathDoesNotDropExclusion)
{
	std::string Root;
	EXPECT_FALSE(TryOpenMedia<2>(Root, [] { return "root"; }, [](size_t) { return true; }, [](size_t i) { return i == 0 ? "first" : ""; }, [](const char *, const char *const *, size_t) { ADD_FAILURE(); return true; }));
}

TEST(QmMediaOpen, PlatformFailureIsReturnedToCaller)
{
	std::string Root;
	EXPECT_FALSE(TryOpenMedia<1>(Root, [] { return "root"; }, [](size_t) { return true; }, [](size_t) { return "first"; }, [](const char *, const char *const *, size_t) { return false; }));
}

TEST(QmMediaOpen, PlatformExceptionCanBeRetriedWithFreshPreparation)
{
	std::string Root;
	EXPECT_FALSE(TryOpenMedia<1>(Root, [] { return "root"; }, [](size_t) { return true; }, [](size_t) { return "first"; }, [](const char *, const char *const *, size_t) -> bool { throw std::bad_alloc(); }));
	EXPECT_TRUE(TryOpenMedia<1>(Root, [] { return "retry"; }, [](size_t) { return false; }, [](size_t) { return "unused"; }, [](const char *pRoot, const char *const *, size_t Count) { EXPECT_STREQ(pRoot, "retry"); EXPECT_EQ(Count, 0u); return true; }));
}
