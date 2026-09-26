// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/skins.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

TEST(SkinsOfficialIndexContract, CreatesMissingDownloadEntries)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/skins.cpp");
	const size_t ApplyIndexPos = Source.find("bool CSkins::ApplyOfficialSkinIndexJson(const char *pJson, size_t JsonSize)");
	ASSERT_NE(ApplyIndexPos, std::string::npos);
	const size_t ApplyIndexEnd = Source.find("void CSkins::ProcessSkinListPlanJob()", ApplyIndexPos);
	ASSERT_NE(ApplyIndexEnd, std::string::npos);
	const std::string ApplyIndexBody = Source.substr(ApplyIndexPos, ApplyIndexEnd - ApplyIndexPos);

	EXPECT_NE(ApplyIndexBody.find("CSkinContainer SkinContainer(this, pName, CSkinContainer::EType::DOWNLOAD, IStorage::TYPE_SAVE);"), std::string::npos);
	EXPECT_NE(ApplyIndexBody.find("pSkinContainer->SetState(pSkinContainer->DetermineInitialState());"), std::string::npos);
	EXPECT_NE(ApplyIndexBody.find("ExistingSkin = m_Skins.insert({pSkinContainer->Name(), std::move(pSkinContainer)}).first;"), std::string::npos);
	EXPECT_NE(ApplyIndexBody.find("SetOfficialReleaseDate(ReleaseDate)"), std::string::npos);
	EXPECT_EQ(ApplyIndexBody.find("if(ExistingSkin == m_Skins.end())\n\t\t\t\tcontinue;"), std::string::npos);
}
