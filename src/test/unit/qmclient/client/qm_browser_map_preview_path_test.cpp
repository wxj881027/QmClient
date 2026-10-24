#include <game/client/components/qmclient/browser_map_preview_loader.h>

#include <gtest/gtest.h>

#include <memory>

namespace
{
	void Probe(SMapDownloadProbeContext &Context, const char *pName, time_t Modified, bool Directory = false)
	{
		const CFsFileInfo Info{pName, 0, Modified};
		EXPECT_EQ(ProbeDownloadedMap(&Info, Directory, IStorage::TYPE_SAVE, &Context), 0);
	}
}

TEST(QmBrowserMapPreviewPath, ShortNamesDoNotReadBeforeFilenameOrReplaceSelection)
{
	SMapDownloadProbeContext Context{"m", {}, 0};
	for(const char *pName : {"m", "m_", "m_a", "m_ab"})
	{
		SCOPED_TRACE(pName);
		// 独立分配使 ASan 可以观察到字符串起点之前的读取。
		const size_t NameSize = str_length(pName) + 1;
		auto pCopy = std::make_unique<char[]>(NameSize);
		str_copy(pCopy.get(), pName, NameSize);
		Probe(Context, pCopy.get(), 10);
		EXPECT_STREQ(Context.m_aBest, "");
		EXPECT_EQ(Context.m_BestTime, 0);
	}
}

TEST(QmBrowserMapPreviewPath, CrcFilenamePreservesResolvedDownloadPath)
{
	SMapDownloadProbeContext Context{"m", {}, 0};
	Probe(Context, "m_1234abcd.map", 10);
	EXPECT_STREQ(Context.m_aBest, "downloadedmaps/m_1234abcd.map");
	EXPECT_EQ(Context.m_BestTime, 10);
}

TEST(QmBrowserMapPreviewPath, ShaFilenameIsAcceptedWithoutShorteningHash)
{
	SMapDownloadProbeContext Context{"m", {}, 0};
	Probe(Context, "m_0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef.map", 10);
	EXPECT_STREQ(Context.m_aBest, "downloadedmaps/m_0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef.map");
}

TEST(QmBrowserMapPreviewPath, LongerMapNameAndOtherSuffixDoNotReplaceValidMatch)
{
	SMapDownloadProbeContext Context{"m", {}, 0};
	Probe(Context, "m_12345678.map", 10);
	for(const char *pName : {"more_12345678.map", "m_12345678.map.bak", "m_12345678.MAP"})
		Probe(Context, pName, 20);
	EXPECT_STREQ(Context.m_aBest, "downloadedmaps/m_12345678.map");
	EXPECT_EQ(Context.m_BestTime, 10);
}

TEST(QmBrowserMapPreviewPath, MostRecentlyModifiedMatchReplacesOlderSelection)
{
	SMapDownloadProbeContext Context{"m", {}, 0};
	Probe(Context, "m_11111111.map", 10);
	Probe(Context, "m_22222222.map", 20);
	Probe(Context, "m_33333333.map", 15);
	EXPECT_STREQ(Context.m_aBest, "downloadedmaps/m_22222222.map");
	EXPECT_EQ(Context.m_BestTime, 20);
}

TEST(QmBrowserMapPreviewPath, MatchingDirectoryDoesNotBecomePreviewSource)
{
	SMapDownloadProbeContext Context{"m", {}, 0};
	Probe(Context, "m_12345678.map", 10, true);
	EXPECT_STREQ(Context.m_aBest, "");
	EXPECT_EQ(Context.m_BestTime, 0);
}

TEST(QmBrowserMapPreviewPath, EqualModificationTimeKeepsFirstMatch)
{
	SMapDownloadProbeContext Context{"m", {}, 0};
	Probe(Context, "m_11111111.map", 10);
	Probe(Context, "m_22222222.map", 10);
	EXPECT_STREQ(Context.m_aBest, "downloadedmaps/m_11111111.map");
}
