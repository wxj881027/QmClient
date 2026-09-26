#include "qmclient_source_contract_test.h"

#include <gtest/gtest.h>

TEST(QmIconLoaderContract, RejectsIncompleteOrDuplicateKnownManifestEntries)
{
	const std::string Source = ReadRepoFile("src/game/client/qm_icon_manager.cpp");
	const size_t LoadManifest = Source.find("bool CQmIconManager::LoadMsdfManifest(");
	const size_t PixelAlignedRect = Source.find("CUIRect CQmIconManager::PixelAlignedRect", LoadManifest);
	ASSERT_NE(LoadManifest, std::string::npos);
	ASSERT_NE(PixelAlignedRect, std::string::npos);
	const std::string Loader = Source.substr(LoadManifest, PixelAlignedRect - LoadManifest);
	EXPECT_NE(Loader.find("bool InvalidKnownEntry = false"), std::string::npos);
	EXPECT_NE(Loader.find("if(Entry.m_Valid)"), std::string::npos);
	EXPECT_NE(Loader.find("LoadedIconCount != static_cast<int>(EQmIcon::COUNT)"), std::string::npos);
	EXPECT_NE(Loader.find("QmIconTextureCanCommit(Texture.IsValid(), Texture.IsNullTexture())"), std::string::npos);
	EXPECT_NE(Loader.find("Atlas.m_LoadedIconCount = LoadedIconCount"), std::string::npos);
	// 位图 alpha 图集已移除：加载器必须不再区分图集类型。
	EXPECT_EQ(Loader.find("EType::ALPHA"), std::string::npos);

	const std::string Header = ReadRepoFile("src/game/client/qm_icon_manager.h");
	EXPECT_NE(Header.find("m_LoadedIconCount == static_cast<int>(EQmIcon::COUNT)"), std::string::npos);
	EXPECT_NE(Header.find("void Swap(CQmIconAtlas &Other)"), std::string::npos);
	// 字体兜底取代位图兜底：图集不可用即优先字体路径。
	EXPECT_NE(Header.find("bool PreferFontFallback() const { return !IsReady(); }"), std::string::npos);
}

TEST(QmIconLoaderContract, ReloadCommitsCandidateOnlyAfterCompleteLoad)
{
	const std::string Source = ReadRepoFile("src/game/client/qm_icon_manager.cpp");
	const size_t Reload = Source.find("bool CQmIconManager::Reload()");
	const size_t LoadManifest = Source.find("bool CQmIconManager::LoadMsdfManifest(", Reload);
	ASSERT_NE(Reload, std::string::npos);
	ASSERT_NE(LoadManifest, std::string::npos);
	const std::string ReloadBody = Source.substr(Reload, LoadManifest - Reload);
	EXPECT_NE(ReloadBody.find("CQmIconAtlas Candidate"), std::string::npos);
	EXPECT_NE(ReloadBody.find("if(!Success)"), std::string::npos);
	EXPECT_NE(ReloadBody.find("m_Atlas.Swap(Candidate);"), std::string::npos);
	// 失败即清空 resident：没有位图兜底后，旧图集不再跨失败保留。
	EXPECT_NE(ReloadBody.find("ClearAtlas(m_Atlas);"), std::string::npos);
	EXPECT_NE(ReadRepoFile("src/game/client/qm_icon_manager.h").find("void Swap(CQmIconAtlas &Other)"), std::string::npos);
}
