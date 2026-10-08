// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <engine/shared/jsonwriter.h>

#include <game/client/components/qmclient/update_manifest.h>

#include <gtest/gtest.h>

#include <utility>

namespace
{
	void WriteRelease(CJsonWriter &Writer, const char *pTag, bool Prerelease, bool Draft = false, bool Complete = true)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("tag_name");
		Writer.WriteStrValue(pTag);
		Writer.WriteAttribute("draft");
		Writer.WriteBoolValue(Draft);
		Writer.WriteAttribute("prerelease");
		Writer.WriteBoolValue(Prerelease);
		Writer.WriteAttribute("assets");
		Writer.BeginArray();
		for(const char *pName : {"QmClient-windows.zip", "QmClient-windows.zip.sig", "QmClient-windows-update.json", "QmClient-windows-update.json.sig"})
		{
			if(!Complete)
				break;
			Writer.BeginObject();
			Writer.WriteAttribute("name");
			Writer.WriteStrValue(pName);
			Writer.WriteAttribute("browser_download_url");
			const std::string Url = std::string("https://github.com/wxj881027/QmClient/releases/download/") + pTag + "/" + pName;
			Writer.WriteStrValue(Url.c_str());
			Writer.EndObject();
		}
		Writer.EndArray();
		Writer.EndObject();
	}
}

TEST(QmClientUpdateManifest, AcceptsSignedManifestShapeForNewerStableVersion)
{
	const char *pJson = R"({"schema":1,"version":"2.80.0","package":{"name":"QmClient-windows.zip","size":123,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},"files":[]})";
	SQmClientUpdateManifest Manifest;
	char aError[256];
	ASSERT_TRUE(ParseQmClientUpdateManifest(pJson, str_length(pJson), "2.79.21", Manifest, aError, sizeof(aError))) << aError;
	EXPECT_STREQ(Manifest.m_aVersion, "2.80.0");
	EXPECT_EQ(Manifest.m_PackageSize, 123);
}

TEST(QmClientUpdateManifest, RejectsUnexpectedAssetOrInvalidHash)
{
	for(const char *pJson : {
		    R"({"schema":1,"version":"2.80.0","package":{"name":"other.zip","size":123,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},"files":[]})",
		    R"({"schema":1,"version":"2.80.0","package":{"name":"QmClient-windows.zip","size":123,"sha256":"not-a-hash"},"files":[]})"})
	{
		SQmClientUpdateManifest Manifest;
		char aError[256];
		EXPECT_FALSE(ParseQmClientUpdateManifest(pJson, str_length(pJson), "2.79.21", Manifest, aError, sizeof(aError)));
	}
}

TEST(QmClientUpdateManifest, RejectsEqualOlderPrereleaseAndOversizedPackages)
{
	for(const char *pVersion : {"2.79.21", "2.79.20", "2.80.0-rc1"})
	{
		char aJson[512];
		str_format(aJson, sizeof(aJson), R"({"schema":1,"version":"%s","package":{"name":"QmClient-windows.zip","size":123,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},"files":[]})", pVersion);
		SQmClientUpdateManifest Manifest;
		char aError[256];
		EXPECT_FALSE(ParseQmClientUpdateManifest(aJson, str_length(aJson), "2.79.21", Manifest, aError, sizeof(aError)));
	}

	const char *pOversized = R"({"schema":1,"version":"2.80.0","package":{"name":"QmClient-windows.zip","size":5368709121,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},"files":[]})";
	SQmClientUpdateManifest Manifest;
	char aError[256];
	EXPECT_FALSE(ParseQmClientUpdateManifest(pOversized, str_length(pOversized), "2.79.21", Manifest, aError, sizeof(aError)));
}

TEST(QmClientUpdateManifest, DevelopmentBuildCanInstallMatchingFormalVersion)
{
	const char *pJson = R"({"schema":1,"version":"3","package":{"name":"QmClient-windows.zip","size":123,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},"files":[]})";
	SQmClientUpdateManifest Manifest;
	char aError[256];
	ASSERT_TRUE(ParseQmClientUpdateManifest(pJson, str_length(pJson), "3", Manifest, aError, sizeof(aError), true)) << aError;
	EXPECT_STREQ(Manifest.m_aVersion, "3");
}

TEST(QmClientUpdateRelease, AcceptsStableReleaseWithExactlyRequiredAssets)
{
	const char *pJson = R"({"tag_name":"v2.80.0","draft":false,"prerelease":false,"assets":[{"name":"QmClient-windows.zip","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v2.80.0/QmClient-windows.zip"},{"name":"QmClient-windows.zip.sig","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v2.80.0/QmClient-windows.zip.sig"},{"name":"QmClient-windows-update.json","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v2.80.0/QmClient-windows-update.json"},{"name":"QmClient-windows-update.json.sig","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v2.80.0/QmClient-windows-update.json.sig"}]})";
	SQmClientUpdateRelease Release;
	char aError[256];
	ASSERT_TRUE(ParseQmClientUpdateRelease(pJson, str_length(pJson), "2.79.21", Release, aError, sizeof(aError))) << aError;
	EXPECT_STREQ(Release.m_aVersion, "2.80.0");
	EXPECT_TRUE(str_endswith(Release.m_aManifestSignatureUrl, "QmClient-windows-update.json.sig"));
}

TEST(QmClientUpdateRelease, AcceptsMajorOnlyFormalReleaseForDevelopmentBuild)
{
	const char *pJson = R"({"tag_name":"v3","draft":false,"prerelease":false,"assets":[{"name":"QmClient-windows.zip","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v3/QmClient-windows.zip"},{"name":"QmClient-windows.zip.sig","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v3/QmClient-windows.zip.sig"},{"name":"QmClient-windows-update.json","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v3/QmClient-windows-update.json"},{"name":"QmClient-windows-update.json.sig","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v3/QmClient-windows-update.json.sig"}]})";
	SQmClientUpdateRelease Release;
	char aError[256];
	ASSERT_TRUE(ParseQmClientUpdateRelease(pJson, str_length(pJson), "3", Release, aError, sizeof(aError), true)) << aError;
	EXPECT_STREQ(Release.m_aVersion, "3");
}

TEST(QmClientUpdateRelease, RejectsPrereleaseMissingAssetAndForeignDownloadUrl)
{
	for(const char *pJson : {
		    R"({"tag_name":"v2.80.0","draft":false,"prerelease":true,"assets":[]})",
		    R"({"tag_name":"v2.80.0","draft":false,"prerelease":false,"assets":[]})",
		    R"({"tag_name":"v2.80.0","draft":false,"prerelease":false,"assets":[{"name":"QmClient-windows.zip","browser_download_url":"https://example.com/QmClient-windows.zip"},{"name":"QmClient-windows.zip.sig","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v2.80.0/QmClient-windows.zip.sig"},{"name":"QmClient-windows-update.json","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v2.80.0/QmClient-windows-update.json"},{"name":"QmClient-windows-update.json.sig","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v2.80.0/QmClient-windows-update.json.sig"}]})"})
	{
		SQmClientUpdateRelease Release;
		char aError[256];
		EXPECT_FALSE(ParseQmClientUpdateRelease(pJson, str_length(pJson), "2.79.21", Release, aError, sizeof(aError)));
	}
}

TEST(QmClientUpdateRelease, PreviewChannelSelectsNewestCompleteVersionRegardlessOfListOrder)
{
	CJsonStringWriter Writer;
	Writer.BeginArray();
	WriteRelease(Writer, "v3.3", false);
	WriteRelease(Writer, "v3.4-preview.10", true);
	WriteRelease(Writer, "v3.4-preview.2", true);
	WriteRelease(Writer, "v3.5-preview.1", true, false, false);
	WriteRelease(Writer, "v3.4-preview.99", true, true);
	Writer.EndArray();
	const std::string Json = Writer.GetOutputString();
	SQmClientUpdateRelease Release;
	char aError[256];
	ASSERT_TRUE(ParseQmClientUpdateRelease(Json.c_str(), Json.size(), "3.3-preview.1", Release, aError, sizeof(aError), true)) << aError;
	EXPECT_STREQ(Release.m_aVersion, "3.4-preview.10");
	ASSERT_TRUE(ParseQmClientUpdateRelease(Json.c_str(), Json.size(), "3.2", Release, aError, sizeof(aError))) << aError;
	EXPECT_STREQ(Release.m_aVersion, "3.3");
}

TEST(QmClientUpdateRelease, MatchingFormalReleaseTakesPriorityOverItsPreviewBuilds)
{
	CJsonStringWriter Writer;
	Writer.BeginArray();
	WriteRelease(Writer, "v3.3", false);
	WriteRelease(Writer, "v3.3-preview.10", true);
	Writer.EndArray();
	const std::string Json = Writer.GetOutputString();
	SQmClientUpdateRelease Release;
	char aError[256];
	ASSERT_TRUE(ParseQmClientUpdateRelease(Json.c_str(), Json.size(), "3.3-preview.1", Release, aError, sizeof(aError), true)) << aError;
	EXPECT_STREQ(Release.m_aVersion, "3.3");
}

TEST(QmClientUpdateRelease, RejectsTagsWhoseChannelDoesNotMatchGithubMetadata)
{
	for(const auto &[pTag, Prerelease] : {std::pair{"v3.3-preview.1", false}, std::pair{"v3.3", true}})
	{
		CJsonStringWriter Writer;
		WriteRelease(Writer, pTag, Prerelease);
		const std::string Json = Writer.GetOutputString();
		SQmClientUpdateRelease Release;
		char aError[256];
		EXPECT_FALSE(ParseQmClientUpdateRelease(Json.c_str(), Json.size(), "3.2-preview.1", Release, aError, sizeof(aError), true));
	}
}

TEST(QmClientUpdateManifest, PreviewManifestRequiresAnOptedInClientAndNewerBatch)
{
	const char *pJson = R"({"schema":1,"version":"3.4-preview.2","package":{"name":"QmClient-windows.zip","size":123,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},"files":[]})";
	SQmClientUpdateManifest Manifest;
	char aError[256];
	EXPECT_FALSE(ParseQmClientUpdateManifest(pJson, str_length(pJson), "3.3", Manifest, aError, sizeof(aError)));
	ASSERT_TRUE(ParseQmClientUpdateManifest(pJson, str_length(pJson), "3.4-preview.1", Manifest, aError, sizeof(aError), true)) << aError;
	EXPECT_STREQ(Manifest.m_aVersion, "3.4-preview.2");
	EXPECT_FALSE(ParseQmClientUpdateManifest(pJson, str_length(pJson), "3.4-preview.2", Manifest, aError, sizeof(aError), true));
}

TEST(QmClientUpdateManifest, SetupSelectionRequiresInstalledMarkerAndCompleteAssets)
{
	SQmClientUpdateRelease Release;
	EXPECT_FALSE(UseQmClientSetupUpdate(true, Release));
	str_copy(Release.m_aSetupUrl, "setup");
	str_copy(Release.m_aSetupSignatureUrl, "signature");
	str_copy(Release.m_aSetupManifestUrl, "manifest");
	EXPECT_FALSE(UseQmClientSetupUpdate(true, Release));
	str_copy(Release.m_aSetupManifestSignatureUrl, "manifest-signature");
	EXPECT_TRUE(UseQmClientSetupUpdate(true, Release));
	EXPECT_FALSE(UseQmClientSetupUpdate(false, Release));
}

TEST(QmClientUpdateManifest, SetupManifestCannotBeUsedAsZipAndMustBeNewer)
{
	const std::string Json = R"({"schema":1,"version":"3.4","package":{"name":"QmClient-Setup.exe","size":10,"sha256":")" + std::string(64, 'a') + R"("},"files":[]})";
	SQmClientUpdateManifest Manifest;
	char aError[256];
	EXPECT_TRUE(ParseQmClientUpdateManifest(Json.c_str(), Json.size(), "3.3", Manifest, aError, sizeof(aError), false, true));
	EXPECT_EQ(Manifest.m_PackageSize, 10u);
	EXPECT_FALSE(ParseQmClientUpdateManifest(Json.c_str(), Json.size(), "3.3", Manifest, aError, sizeof(aError)));
	EXPECT_FALSE(ParseQmClientUpdateManifest(Json.c_str(), Json.size(), "3.4", Manifest, aError, sizeof(aError), false, true));
}

TEST(QmClientUpdateManifest, ReleaseFindsSetupAssetsAlongsideLegacyZip)
{
	CJsonStringWriter Writer;
	WriteRelease(Writer, "v3.4", false);
	std::string Json = Writer.GetOutputString();
	const size_t ArrayEnd = Json.rfind(']');
	ASSERT_NE(ArrayEnd, std::string::npos);
	std::string Assets;
	for(const char *pName : {"QmClient-Setup.exe", "QmClient-Setup.exe.sig", "QmClient-windows-setup-update.json", "QmClient-windows-setup-update.json.sig"})
		Assets += std::string(",{\"name\":\"") + pName + "\",\"browser_download_url\":\"https://github.com/wxj881027/QmClient/releases/download/v3.4/" + pName + "\"}";
	Json.insert(ArrayEnd, Assets);
	SQmClientUpdateRelease Release;
	char aError[256];
	ASSERT_TRUE(ParseQmClientUpdateRelease(Json.c_str(), Json.size(), "3.3", Release, aError, sizeof(aError)));
	EXPECT_TRUE(Release.HasSetup());
	EXPECT_NE(str_find(Release.m_aPackageUrl, "QmClient-windows.zip"), nullptr);
	EXPECT_NE(str_find(Release.m_aSetupUrl, "QmClient-Setup.exe"), nullptr);
}

TEST(QmClientUpdateManifest, PortableManifestCannotCrossNormalPackageBoundary)
{
	const char *pJson = R"({"schema":1,"version":"2.80.0","package":{"name":"QmClient-windows-portable.zip","size":123,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},"files":[]})";
	SQmClientUpdateManifest Manifest;
	char aError[256];
	EXPECT_TRUE(ParseQmClientUpdateManifest(pJson, str_length(pJson), "2.79.21", Manifest, aError, sizeof(aError), false, false, true));
	EXPECT_FALSE(ParseQmClientUpdateManifest(pJson, str_length(pJson), "2.79.21", Manifest, aError, sizeof(aError)));
}

TEST(QmClientUpdateManifest, PortableReleaseRequiresPortableAssetsAndNeverUsesSetup)
{
	CJsonStringWriter Writer;
	WriteRelease(Writer, "v2.80.0", false);
	std::string Json = Writer.GetOutputString();
	SQmClientUpdateRelease Release;
	char aError[256];
	EXPECT_FALSE(ParseQmClientUpdateRelease(Json.c_str(), Json.size(), "2.79.21", Release, aError, sizeof(aError), false, true));
	size_t Offset = 0;
	while((Offset = Json.find("QmClient-windows", Offset)) != std::string::npos)
	{
		Json.insert(Offset + 16, "-portable");
		Offset += 25;
	}
	ASSERT_TRUE(ParseQmClientUpdateRelease(Json.c_str(), Json.size(), "2.79.21", Release, aError, sizeof(aError), false, true)) << aError;
	EXPECT_NE(std::string(Release.m_aPackageUrl).find("QmClient-windows-portable.zip"), std::string::npos);
	str_copy(Release.m_aSetupUrl, "setup");
	str_copy(Release.m_aSetupSignatureUrl, "signature");
	str_copy(Release.m_aSetupManifestUrl, "manifest");
	str_copy(Release.m_aSetupManifestSignatureUrl, "signature");
	EXPECT_FALSE(UseQmClientSetupUpdate(true, Release, true));
}

TEST(QmClientUpdateManifest, SignedSevenZipIsPreferredAndPortableUsesSeparateManifest)
{
	for(bool Portable : {false, true})
	{
		CJsonStringWriter Writer;
		WriteRelease(Writer, "v3.4", false);
		std::string Json = Writer.GetOutputString();
		const std::string Stem = Portable ? "QmClient-windows-portable" : "QmClient-windows";
		if(Portable)
		{
			size_t Offset = 0;
			while((Offset = Json.find("QmClient-windows", Offset)) != std::string::npos)
			{
				Json.insert(Offset + 16, "-portable");
				Offset += 25;
			}
		}
		std::string Assets;
		for(const auto &Suffix : {".7z", ".7z.sig", "-7z-update.json", "-7z-update.json.sig"})
			Assets += ",{\"name\":\"" + Stem + Suffix + "\",\"browser_download_url\":\"https://github.com/wxj881027/QmClient/releases/download/v3.4/" + Stem + Suffix + "\"}";
		Json.insert(Json.rfind(']'), Assets);
		SQmClientUpdateRelease Release;
		char aError[256];
		ASSERT_TRUE(ParseQmClientUpdateRelease(Json.c_str(), Json.size(), "3.3", Release, aError, sizeof(aError), false, Portable)) << aError;
		EXPECT_TRUE(Release.m_SevenZip);
		EXPECT_NE(std::string(Release.m_aPackageUrl).find(Stem + ".7z"), std::string::npos);
		EXPECT_NE(std::string(Release.m_aManifestUrl).find(Stem + "-7z-update.json"), std::string::npos);
	}
}

TEST(QmClientUpdateManifest, IncompleteSevenZipFallsBackToCompleteZipUntilAllAssetsArePublished)
{
	for(bool Portable : {false, true})
	{
		CJsonStringWriter Writer;
		WriteRelease(Writer, "v3.4", false);
		std::string Json = Writer.GetOutputString();
		const std::string Stem = Portable ? "QmClient-windows-portable" : "QmClient-windows";
		if(Portable)
		{
			size_t Offset = 0;
			while((Offset = Json.find("QmClient-windows", Offset)) != std::string::npos)
			{
				Json.insert(Offset + 16, "-portable");
				Offset += 25;
			}
		}
		int Published = 0;
		for(const char *pSuffix : {".7z", ".7z.sig", "-7z-update.json", "-7z-update.json.sig"})
		{
			const std::string Asset = ",{\"name\":\"" + Stem + pSuffix + "\",\"browser_download_url\":\"https://github.com/wxj881027/QmClient/releases/download/v3.4/" + Stem + pSuffix + "\"}";
			Json.insert(Json.rfind(']'), Asset);
			SQmClientUpdateRelease Release;
			char aError[256];
			SCOPED_TRACE(Stem + pSuffix);
			ASSERT_TRUE(ParseQmClientReleaseInfo(Json.c_str(), Json.size(), "3.3", Release, aError, sizeof(aError), false, Portable)) << aError;
			EXPECT_TRUE(Release.m_PackageAvailable);
			EXPECT_EQ(Release.m_SevenZip, ++Published == 4);
			EXPECT_EQ(std::string(Release.m_aPackageUrl), "https://github.com/wxj881027/QmClient/releases/download/v3.4/" + Stem + (Published == 4 ? ".7z" : ".zip"));
			EXPECT_TRUE(ParseQmClientUpdateRelease(Json.c_str(), Json.size(), "3.3", Release, aError, sizeof(aError), false, Portable)) << aError;
		}
	}
}

TEST(QmClientUpdateManifest, SevenZipManifestCannotCrossPortableOrSetupBoundary)
{
	const std::string Json = R"({"schema":1,"version":"3.4","package":{"name":"QmClient-windows-portable.7z","size":10,"sha256":")" + std::string(64, 'a') + R"("},"files":[]})";
	SQmClientUpdateManifest Manifest;
	char aError[256];
	EXPECT_TRUE(ParseQmClientUpdateManifest(Json.c_str(), Json.size(), "3.3", Manifest, aError, sizeof(aError), false, false, true, true));
	EXPECT_FALSE(ParseQmClientUpdateManifest(Json.c_str(), Json.size(), "3.3", Manifest, aError, sizeof(aError), false, false, false, true));
	EXPECT_FALSE(ParseQmClientUpdateManifest(Json.c_str(), Json.size(), "3.3", Manifest, aError, sizeof(aError), false, true, true, true));
}

TEST(QmClientUpdateManifest, EmptyOrMalformedPreviewListIsFailureNotLatest)
{
	for(const char *pJson : {"[]", "[{}]", R"([{"tag_name":"v3.5-preview.1","draft":false,"prerelease":true,"assets":[]}])"})
	{
		SQmClientUpdateRelease Release;
		char aError[256];
		EXPECT_FALSE(ParseQmClientUpdateRelease(pJson, str_length(pJson), "3.4-preview.1", Release, aError, sizeof(aError), true));
		EXPECT_STREQ(aError, "Invalid GitHub release metadata");
	}
}

TEST(QmClientUpdateManifest, OlderIncompleteReleaseIsFailureNotLatest)
{
	const char *pJson = R"({"tag_name":"v3.3","draft":false,"prerelease":false,"assets":[]})";
	SQmClientUpdateRelease Release;
	char aError[256];
	EXPECT_FALSE(ParseQmClientUpdateRelease(pJson, str_length(pJson), "3.4", Release, aError, sizeof(aError)));
	EXPECT_STRNE(aError, "GitHub release version is not newer");
}

TEST(QmClientUpdateManifest, LargeDuplicateSevenZipAssetListIsRejected)
{
	std::string Json = R"({"tag_name":"v3.4","draft":false,"prerelease":false,"assets":[)";
	const std::string Asset = R"({"name":"QmClient-windows.7z","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v3.4/QmClient-windows.7z"},)";
	for(int Index = 0; Index < 5000; ++Index)
		Json += Asset;
	Json += R"({"name":"QmClient-windows.7z.sig","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v3.4/QmClient-windows.7z.sig"}]})";
	SQmClientUpdateRelease Release;
	char aError[256];
	EXPECT_FALSE(ParseQmClientUpdateRelease(Json.c_str(), Json.size(), "3.3", Release, aError, sizeof(aError)));
	EXPECT_STRNE(aError, "GitHub release version is not newer");
}

TEST(QmClientUpdateRelease, EqualAndOlderNormalOnlyReleaseDoesNotRequirePortableDownloadAssets)
{
	CJsonStringWriter Writer;
	WriteRelease(Writer, "v3.3", false);
	const std::string Json = Writer.GetOutputString();
	for(const char *pCurrentVersion : {"3.3", "3.4"})
	{
		SCOPED_TRACE(pCurrentVersion);
		SQmClientUpdateRelease Release;
		char aError[256];
		EXPECT_FALSE(ParseQmClientUpdateRelease(Json.c_str(), Json.size(), pCurrentVersion, Release, aError, sizeof(aError), false, true));
		EXPECT_STREQ(aError, "GitHub release version is not newer");
		EXPECT_STREQ(Release.m_aPackageUrl, "");
	}
}

TEST(QmClientUpdateRelease, NewerNormalOnlyReleaseStillRejectsPortableUpdate)
{
	CJsonStringWriter Writer;
	WriteRelease(Writer, "v3.4", false);
	const std::string Json = Writer.GetOutputString();
	SQmClientUpdateRelease Release;
	char aError[256];
	EXPECT_FALSE(ParseQmClientUpdateRelease(Json.c_str(), Json.size(), "3.3", Release, aError, sizeof(aError), false, true));
	EXPECT_STREQ(aError, "GitHub release is missing a required update asset");
}

TEST(QmClientUpdateRelease, OlderMalformedMetadataCannotBecomeNoUpdate)
{
	for(const char *pJson : {
		    R"({"tag_name":"v3.3","draft":false,"assets":[]})",
		    R"({"tag_name":"v3.3","draft":false,"prerelease":false,"assets":[{"name":"QmClient-windows.zip"}]})",
		    R"({"tag_name":"v3.3","draft":false,"prerelease":false,"assets":[{"name":"QmClient-windows.zip","browser_download_url":"https://example.com/file"}]})"})
	{
		SCOPED_TRACE(pJson);
		SQmClientUpdateRelease Release;
		char aError[256];
		EXPECT_FALSE(ParseQmClientUpdateRelease(pJson, str_length(pJson), "3.4", Release, aError, sizeof(aError), false, true));
		EXPECT_STRNE(aError, "GitHub release version is not newer");
	}
}

TEST(QmClientUpdateRelease, PreviewListRecognizesEqualNormalOnlyReleaseForPortableClient)
{
	CJsonStringWriter Writer;
	Writer.BeginArray();
	WriteRelease(Writer, "v3.3", false);
	Writer.EndArray();
	const std::string Json = Writer.GetOutputString();
	SQmClientUpdateRelease Release;
	char aError[256];
	EXPECT_FALSE(ParseQmClientUpdateRelease(Json.c_str(), Json.size(), "3.3", Release, aError, sizeof(aError), false, true));
	EXPECT_STREQ(aError, "GitHub release version is not newer");
}

TEST(QmClientReleaseInfo, EqualNormalReleaseRetainsNotesAndReportsPortablePackageUnavailable)
{
	const char *pJson = R"({"tag_name":"v3.3","draft":false,"prerelease":false,"body":"## Changes\nFixed update checks","assets":[{"name":"QmClient-windows.zip","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v3.3/QmClient-windows.zip"}]})";
	SQmClientUpdateRelease Release;
	char aError[256];
	ASSERT_TRUE(ParseQmClientReleaseInfo(pJson, str_length(pJson), "3.3", Release, aError, sizeof(aError), false, true)) << aError;
	EXPECT_STREQ(Release.m_aVersion, "3.3");
	EXPECT_EQ(Release.m_Notes, "## Changes\nFixed update checks");
	EXPECT_FALSE(Release.m_NewVersion);
	EXPECT_FALSE(Release.m_PackageAvailable);
	EXPECT_FALSE(CanDownloadQmClientRelease(Release, false, true));
}

TEST(QmClientReleaseInfo, NewerReleaseWithoutMatchingPackageIsInformationSuccessAndCannotDownload)
{
	CJsonStringWriter Writer;
	WriteRelease(Writer, "v3.4", false);
	const std::string Json = Writer.GetOutputString();
	SQmClientUpdateRelease Release;
	char aError[256];
	ASSERT_TRUE(ParseQmClientReleaseInfo(Json.c_str(), Json.size(), "3.3", Release, aError, sizeof(aError), false, true)) << aError;
	EXPECT_TRUE(Release.m_NewVersion);
	EXPECT_FALSE(Release.m_PackageAvailable);
	EXPECT_FALSE(CanDownloadQmClientRelease(Release, true, true));
	EXPECT_STREQ(aError, "");
	EXPECT_STREQ(Release.m_aPackageUrl, "");
}

TEST(QmClientReleaseInfo, NewerEmptyAssetReleaseStillExposesVersionAndNotes)
{
	const char *pJson = R"({"tag_name":"v3.4","draft":false,"prerelease":false,"body":"Packages will follow","assets":[]})";
	SQmClientUpdateRelease Release;
	char aError[256];
	ASSERT_TRUE(ParseQmClientReleaseInfo(pJson, str_length(pJson), "3.3", Release, aError, sizeof(aError))) << aError;
	EXPECT_TRUE(Release.m_NewVersion);
	EXPECT_EQ(Release.m_Notes, "Packages will follow");
	EXPECT_FALSE(CanDownloadQmClientRelease(Release, false, false));
}

TEST(QmClientReleaseInfo, CompleteSignedReleaseCanDownloadOnlyWhenNewer)
{
	CJsonStringWriter Writer;
	WriteRelease(Writer, "v3.4", false);
	const std::string Json = Writer.GetOutputString();
	SQmClientUpdateRelease Release;
	char aError[256];
	ASSERT_TRUE(ParseQmClientReleaseInfo(Json.c_str(), Json.size(), "3.3", Release, aError, sizeof(aError))) << aError;
	EXPECT_TRUE(Release.m_PackageAvailable);
	EXPECT_TRUE(CanDownloadQmClientRelease(Release, false, false));
	ASSERT_TRUE(ParseQmClientReleaseInfo(Json.c_str(), Json.size(), "3.4", Release, aError, sizeof(aError))) << aError;
	EXPECT_TRUE(Release.m_PackageAvailable);
	EXPECT_FALSE(CanDownloadQmClientRelease(Release, false, false));
}

TEST(QmClientReleaseInfo, MissingSignatureCannotAuthorizeDownload)
{
	const char *pJson = R"({"tag_name":"v3.4","draft":false,"prerelease":false,"assets":[{"name":"QmClient-windows.zip","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v3.4/QmClient-windows.zip"}]})";
	SQmClientUpdateRelease Release;
	char aError[256];
	ASSERT_TRUE(ParseQmClientReleaseInfo(pJson, str_length(pJson), "3.3", Release, aError, sizeof(aError))) << aError;
	EXPECT_FALSE(Release.m_PackageAvailable);
	EXPECT_FALSE(CanDownloadQmClientRelease(Release, false, false));
}

TEST(QmClientReleaseInfo, MalformedMetadataNotesAndForeignAssetsRemainFailures)
{
	for(const char *pJson : {
		    "<html>gateway error</html>",
		    R"({"tag_name":"v3.3","draft":false,"assets":[]})",
		    R"({"tag_name":"v3.3","draft":false,"prerelease":false,"body":{},"assets":[]})",
		    R"({"tag_name":"v3.3","draft":false,"prerelease":false,"assets":[{"name":"QmClient-windows.zip"}]})",
		    R"({"tag_name":"v3.3","draft":false,"prerelease":false,"assets":[{"name":"QmClient-windows.zip","browser_download_url":"https://example.com/file"}]})"})
	{
		SCOPED_TRACE(pJson);
		SQmClientUpdateRelease Release;
		char aError[256];
		EXPECT_FALSE(ParseQmClientReleaseInfo(pJson, str_length(pJson), "3.4", Release, aError, sizeof(aError), false, true));
	}
}

TEST(QmClientReleaseInfo, PreviewListKeepsNewestReleaseEvenWhenItsPackageIsNotPublished)
{
	CJsonStringWriter Writer;
	Writer.BeginArray();
	WriteRelease(Writer, "v3.4-preview.1", true);
	WriteRelease(Writer, "v3.4-preview.2", true, false, false);
	Writer.EndArray();
	const std::string Json = Writer.GetOutputString();
	SQmClientUpdateRelease Release;
	char aError[256];
	ASSERT_TRUE(ParseQmClientReleaseInfo(Json.c_str(), Json.size(), "3.3-preview.1", Release, aError, sizeof(aError), true)) << aError;
	EXPECT_STREQ(Release.m_aVersion, "3.4-preview.2");
	EXPECT_TRUE(Release.m_NewVersion);
	EXPECT_FALSE(Release.m_PackageAvailable);
}

TEST(QmClientReleaseInfo, CompleteSetupWithoutArchiveRequiresExistingNormalSetupInstallation)
{
	const char *pJson = R"({"tag_name":"v3.4","draft":false,"prerelease":false,"assets":[{"name":"QmClient-Setup.exe","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v3.4/QmClient-Setup.exe"},{"name":"QmClient-Setup.exe.sig","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v3.4/QmClient-Setup.exe.sig"},{"name":"QmClient-windows-setup-update.json","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v3.4/QmClient-windows-setup-update.json"},{"name":"QmClient-windows-setup-update.json.sig","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v3.4/QmClient-windows-setup-update.json.sig"}]})";
	SQmClientUpdateRelease Release;
	char aError[256];
	ASSERT_TRUE(ParseQmClientReleaseInfo(pJson, str_length(pJson), "3.3", Release, aError, sizeof(aError))) << aError;
	EXPECT_FALSE(Release.m_PackageAvailable);
	EXPECT_TRUE(CanDownloadQmClientRelease(Release, true, false));
	EXPECT_FALSE(CanDownloadQmClientRelease(Release, false, false));
	EXPECT_FALSE(CanDownloadQmClientRelease(Release, true, true));
}

TEST(QmClientReleaseInfo, OptionalNotesAcceptMissingNullAndStringsInInformationAndInstallParsers)
{
	struct SCase
	{
		const char *m_pField;
		const char *m_pNotes;
	};
	for(const auto &Case : {SCase{"", ""}, SCase{",\"body\":null", ""}, SCase{",\"body\":\"\"", ""}, SCase{",\"body\":\"Release notes\"", "Release notes"}})
	{
		SCOPED_TRACE(Case.m_pField);
		CJsonStringWriter Writer;
		WriteRelease(Writer, "v3.4", false);
		std::string Json = Writer.GetOutputString();
		Json.insert(Json.rfind('}'), Case.m_pField);
		SQmClientUpdateRelease Release;
		char aError[256];
		ASSERT_TRUE(ParseQmClientReleaseInfo(Json.c_str(), Json.size(), "3.3", Release, aError, sizeof(aError))) << aError;
		EXPECT_EQ(Release.m_Notes, Case.m_pNotes);
		ASSERT_TRUE(ParseQmClientUpdateRelease(Json.c_str(), Json.size(), "3.3", Release, aError, sizeof(aError))) << aError;
		EXPECT_EQ(Release.m_Notes, Case.m_pNotes);
	}
}

TEST(QmClientReleaseInfo, OptionalNotesRejectNonStringValuesInInformationAndInstallParsers)
{
	for(const char *pField : {",\"body\":{}", ",\"body\":[]", ",\"body\":42", ",\"body\":true"})
	{
		SCOPED_TRACE(pField);
		CJsonStringWriter Writer;
		WriteRelease(Writer, "v3.4", false);
		std::string Json = Writer.GetOutputString();
		Json.insert(Json.rfind('}'), pField);
		SQmClientUpdateRelease Release;
		char aError[256];
		EXPECT_FALSE(ParseQmClientReleaseInfo(Json.c_str(), Json.size(), "3.3", Release, aError, sizeof(aError)));
		EXPECT_FALSE(ParseQmClientUpdateRelease(Json.c_str(), Json.size(), "3.3", Release, aError, sizeof(aError)));
	}
}
