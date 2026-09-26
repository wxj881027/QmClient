// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <game/client/components/qmclient/update_manifest.h>

#include <gtest/gtest.h>

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
