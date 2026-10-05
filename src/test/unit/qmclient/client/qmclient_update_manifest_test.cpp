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
