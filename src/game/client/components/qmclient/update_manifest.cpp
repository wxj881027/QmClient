#include "update_manifest.h"

#include "update_version.h"

#include <base/system.h>

#include <engine/external/json-parser/json.h>
#include <engine/shared/json.h>

#include <limits>
#include <memory>
#include <string>

namespace
{
	constexpr uint64_t MAX_UPDATE_PACKAGE_SIZE = 5ULL * 1024 * 1024 * 1024;
	constexpr const char *UPDATE_ASSET_URL_PREFIX = "https://github.com/wxj881027/QmClient/releases/download/";
	using TJson = std::unique_ptr<json_value, decltype(&json_value_free)>;

	void SetError(char *pError, size_t ErrorSize, const char *pMessage)
	{
		if(pError != nullptr && ErrorSize > 0)
			str_copy(pError, pMessage, ErrorSize);
	}

	bool NormalizeVersion(const char *pVersion, char *pBuffer, size_t BufferSize, SQmClientVersion &Version)
	{
		if(!pVersion || !pBuffer || BufferSize == 0 || !ParseQmClientVersion(pVersion, Version))
			return false;
		if(*pVersion == 'v' || *pVersion == 'V')
			++pVersion;
		if(static_cast<size_t>(str_length(pVersion)) >= BufferSize)
			return false;
		str_copy(pBuffer, pVersion, BufferSize);
		return true;
	}

	bool ReadReleaseAsset(const json_value *pAsset, const char *pExpectedName, char *pUrl, size_t UrlSize)
	{
		if(!pAsset || pAsset->type != json_object)
			return false;
		const json_value *pName = json_object_get(pAsset, "name");
		const json_value *pDownloadUrl = json_object_get(pAsset, "browser_download_url");
		if(!pName || !pDownloadUrl || pName->type != json_string || pDownloadUrl->type != json_string ||
			str_comp(json_string_get(pName), pExpectedName) != 0)
			return false;
		const char *pValue = json_string_get(pDownloadUrl);
		if(!str_startswith(pValue, UPDATE_ASSET_URL_PREFIX) || static_cast<size_t>(str_length(pValue)) >= UrlSize)
			return false;
		str_copy(pUrl, pValue, UrlSize);
		return true;
	}

	bool ParseReleaseObject(const json_value *pRoot, const char *pCurrentVersion, SQmClientUpdateRelease &Release, char *pError, size_t ErrorSize, bool LocalIsDevelopmentBuild, bool PortableBuild, bool InfoOnly)
	{
		if(!pRoot || pRoot->type != json_object)
			return false;
		const json_value *pTagName = json_object_get(pRoot, "tag_name");
		const json_value *pDraft = json_object_get(pRoot, "draft");
		const json_value *pPrerelease = json_object_get(pRoot, "prerelease");
		const json_value *pAssets = json_object_get(pRoot, "assets");
		SQmClientVersion Version;
		if(!pTagName || !pDraft || !pPrerelease || !pAssets || pTagName->type != json_string ||
			pDraft->type != json_boolean || pPrerelease->type != json_boolean || pDraft->u.boolean || pAssets->type != json_array ||
			!NormalizeVersion(json_string_get(pTagName), Release.m_aVersion, sizeof(Release.m_aVersion), Version) ||
			(pPrerelease->u.boolean != 0) != (Version.m_Preview != 0))
			return false;

		SQmClientVersion LocalVersion;
		if(!ParseQmClientVersion(pCurrentVersion, LocalVersion) || (Version.m_Preview != 0 && LocalVersion.m_Preview == 0 && !LocalIsDevelopmentBuild))
			return false;
		Release.m_NewVersion = IsQmClientRemoteVersionNewer(Release.m_aVersion, pCurrentVersion, LocalIsDevelopmentBuild);
		const json_value *pBody = json_object_get(pRoot, "body");
		if(pBody && pBody->type != json_none && pBody->type != json_null && pBody->type != json_string)
			return false;
		if(pBody && pBody->type == json_string)
			Release.m_Notes = json_string_get(pBody);
		// 这里只校验附件结构与来源，匹配平台/签名是否齐全是独立的下载能力。
		const std::string ReleasePrefix = std::string(UPDATE_ASSET_URL_PREFIX) + json_string_get(pTagName) + "/";
		for(unsigned Index = 0; Index < pAssets->u.array.length; ++Index)
		{
			const auto *pAsset = json_array_get(pAssets, Index);
			const auto *pName = pAsset && pAsset->type == json_object ? json_object_get(pAsset, "name") : nullptr;
			const auto *pUrl = pAsset && pAsset->type == json_object ? json_object_get(pAsset, "browser_download_url") : nullptr;
			if(!pName || pName->type != json_string || json_string_get(pName)[0] == '\0' ||
				!pUrl || pUrl->type != json_string || !str_startswith(json_string_get(pUrl), ReleasePrefix.c_str()) ||
				static_cast<size_t>(str_length(json_string_get(pUrl))) >= sizeof(Release.m_aPackageUrl))
				return false;
		}

		// 各格式独立收集完整四件套；后补的部分 7z 附件不能遮蔽已有可用 ZIP。
		char aaSevenZipUrls[4][2048] = {};
		struct SExpectedAsset
		{
			const char *m_pName;
			char *m_pUrl;
			size_t m_UrlSize;
			bool m_Found = false;
		};
		SExpectedAsset aExpected[] = {
			{PortableBuild ? "QmClient-windows-portable.zip" : "QmClient-windows.zip", Release.m_aPackageUrl, sizeof(Release.m_aPackageUrl)},
			{PortableBuild ? "QmClient-windows-portable.zip.sig" : "QmClient-windows.zip.sig", Release.m_aPackageSignatureUrl, sizeof(Release.m_aPackageSignatureUrl)},
			{PortableBuild ? "QmClient-windows-portable-update.json" : "QmClient-windows-update.json", Release.m_aManifestUrl, sizeof(Release.m_aManifestUrl)},
			{PortableBuild ? "QmClient-windows-portable-update.json.sig" : "QmClient-windows-update.json.sig", Release.m_aManifestSignatureUrl, sizeof(Release.m_aManifestSignatureUrl)},
			{PortableBuild ? "QmClient-windows-portable.7z" : "QmClient-windows.7z", aaSevenZipUrls[0], sizeof(aaSevenZipUrls[0])},
			{PortableBuild ? "QmClient-windows-portable.7z.sig" : "QmClient-windows.7z.sig", aaSevenZipUrls[1], sizeof(aaSevenZipUrls[1])},
			{PortableBuild ? "QmClient-windows-portable-7z-update.json" : "QmClient-windows-7z-update.json", aaSevenZipUrls[2], sizeof(aaSevenZipUrls[2])},
			{PortableBuild ? "QmClient-windows-portable-7z-update.json.sig" : "QmClient-windows-7z-update.json.sig", aaSevenZipUrls[3], sizeof(aaSevenZipUrls[3])},
			{"QmClient-Setup.exe", Release.m_aSetupUrl, sizeof(Release.m_aSetupUrl)},
			{"QmClient-Setup.exe.sig", Release.m_aSetupSignatureUrl, sizeof(Release.m_aSetupSignatureUrl)},
			{"QmClient-windows-setup-update.json", Release.m_aSetupManifestUrl, sizeof(Release.m_aSetupManifestUrl)},
			{"QmClient-windows-setup-update.json.sig", Release.m_aSetupManifestSignatureUrl, sizeof(Release.m_aSetupManifestSignatureUrl)},
		};
		for(unsigned Index = 0; Index < pAssets->u.array.length; ++Index)
		{
			const json_value *pAsset = json_array_get(pAssets, Index);
			const json_value *pName = pAsset && pAsset->type == json_object ? json_object_get(pAsset, "name") : nullptr;
			if(!pName || pName->type != json_string)
				continue;
			for(size_t AssetIndex = 0; AssetIndex < std::size(aExpected); ++AssetIndex)
			{
				auto &Expected = aExpected[AssetIndex];
				if(PortableBuild && AssetIndex >= 8)
					continue;
				if(str_comp(json_string_get(pName), Expected.m_pName) != 0)
					continue;
				const std::string ExpectedUrl = std::string(UPDATE_ASSET_URL_PREFIX) + json_string_get(pTagName) + "/" + Expected.m_pName;
				if(Expected.m_Found || !ReadReleaseAsset(pAsset, Expected.m_pName, Expected.m_pUrl, Expected.m_UrlSize) || ExpectedUrl != Expected.m_pUrl)
					return false;
				Expected.m_Found = true;
			}
		}
		Release.m_SevenZip = aExpected[4].m_Found && aExpected[5].m_Found && aExpected[6].m_Found && aExpected[7].m_Found;
		Release.m_PackageAvailable = Release.m_SevenZip || (aExpected[0].m_Found && aExpected[1].m_Found && aExpected[2].m_Found && aExpected[3].m_Found);
		if(Release.m_SevenZip)
		{
			str_copy(Release.m_aPackageUrl, aaSevenZipUrls[0]);
			str_copy(Release.m_aPackageSignatureUrl, aaSevenZipUrls[1]);
			str_copy(Release.m_aManifestUrl, aaSevenZipUrls[2]);
			str_copy(Release.m_aManifestSignatureUrl, aaSevenZipUrls[3]);
		}
		if(!InfoOnly)
		{
			if(!Release.m_NewVersion)
			{
				if(pAssets->u.array.length == 0)
					return false;
				SetError(pError, ErrorSize, "GitHub release version is not newer");
				return false;
			}
			if(!Release.m_PackageAvailable)
			{
				SetError(pError, ErrorSize, "GitHub release is missing a required update asset");
				return false;
			}
		}
		SetError(pError, ErrorSize, "");
		return true;
	}
}

static bool ParseReleaseDocument(const char *pJson, size_t JsonSize, const char *pCurrentVersion, SQmClientUpdateRelease &Release, char *pError, size_t ErrorSize, bool LocalIsDevelopmentBuild, bool PortableBuild, bool InfoOnly)
{
	Release = {};
	SetError(pError, ErrorSize, "Invalid GitHub release metadata");
	if(!pJson || JsonSize == 0 || JsonSize > 4 * 1024 * 1024 || JsonSize > std::numeric_limits<unsigned>::max())
		return false;
	TJson Root(JsonParse(pJson, static_cast<unsigned>(JsonSize)), json_value_free);
	if(!Root)
		return false;
	if(Root->type == json_object)
		return ParseReleaseObject(Root.get(), pCurrentVersion, Release, pError, ErrorSize, LocalIsDevelopmentBuild, PortableBuild, InfoOnly);
	if(Root->type != json_array)
		return false;

	// 信息入口按版本选择最新有效发布；安装入口仍只选择附件完整的发布。
	bool ValidOlderRelease = false;
	for(unsigned Index = 0; Index < Root->u.array.length; ++Index)
	{
		SQmClientUpdateRelease Candidate;
		char aCandidateError[256] = "Invalid GitHub release metadata";
		if(ParseReleaseObject(json_array_get(Root.get(), Index), pCurrentVersion, Candidate, aCandidateError, sizeof(aCandidateError), LocalIsDevelopmentBuild, PortableBuild, InfoOnly) &&
			(Release.m_aVersion[0] == '\0' || IsQmClientRemoteVersionNewer(Candidate.m_aVersion, Release.m_aVersion, LocalIsDevelopmentBuild)))
			Release = Candidate;
		ValidOlderRelease |= str_comp(aCandidateError, "GitHub release version is not newer") == 0;
	}
	const bool Found = Release.m_aVersion[0] != '\0';
	SetError(pError, ErrorSize, Found ? "" : (ValidOlderRelease ? "GitHub release version is not newer" : "Invalid GitHub release metadata"));
	return Found;
}

bool ParseQmClientReleaseInfo(const char *pJson, size_t JsonSize, const char *pCurrentVersion, SQmClientUpdateRelease &Release, char *pError, size_t ErrorSize, bool LocalIsDevelopmentBuild, bool PortableBuild)
{
	return ParseReleaseDocument(pJson, JsonSize, pCurrentVersion, Release, pError, ErrorSize, LocalIsDevelopmentBuild, PortableBuild, true);
}

bool ParseQmClientUpdateRelease(const char *pJson, size_t JsonSize, const char *pCurrentVersion, SQmClientUpdateRelease &Release, char *pError, size_t ErrorSize, bool LocalIsDevelopmentBuild, bool PortableBuild)
{
	return ParseReleaseDocument(pJson, JsonSize, pCurrentVersion, Release, pError, ErrorSize, LocalIsDevelopmentBuild, PortableBuild, false);
}

bool ParseQmClientUpdateManifest(const char *pJson, size_t JsonSize, const char *pCurrentVersion, SQmClientUpdateManifest &Manifest, char *pError, size_t ErrorSize, bool LocalIsDevelopmentBuild, bool SetupPackage, bool PortableBuild, bool SevenZipPackage)
{
	Manifest = {};
	SetError(pError, ErrorSize, "Invalid update manifest");
	if((PortableBuild && SetupPackage) || pJson == nullptr || JsonSize == 0 || JsonSize > std::numeric_limits<unsigned>::max())
		return false;
	TJson Root(JsonParse(pJson, static_cast<unsigned>(JsonSize)), json_value_free);
	if(!Root || Root->type != json_object)
		return false;
	const json_value *pSchema = json_object_get(Root.get(), "schema");
	const json_value *pVersion = json_object_get(Root.get(), "version");
	const json_value *pPackage = json_object_get(Root.get(), "package");
	const json_value *pFiles = json_object_get(Root.get(), "files");
	if(!pSchema || !pVersion || !pPackage || !pFiles || pSchema->type != json_integer || pSchema->u.integer != 1 ||
		pVersion->type != json_string || pPackage->type != json_object || pFiles->type != json_array)
		return false;

	SQmClientVersion Version;
	if(!NormalizeVersion(json_string_get(pVersion), Manifest.m_aVersion, sizeof(Manifest.m_aVersion), Version) ||
		!IsQmClientRemoteVersionNewer(Manifest.m_aVersion, pCurrentVersion, LocalIsDevelopmentBuild))
	{
		SetError(pError, ErrorSize, "Update manifest version is not newer");
		return false;
	}
	const json_value *pName = json_object_get(pPackage, "name");
	const json_value *pSize = json_object_get(pPackage, "size");
	const json_value *pSha256 = json_object_get(pPackage, "sha256");
	if(!pName || !pSize || !pSha256 || pName->type != json_string || str_comp(json_string_get(pName), SetupPackage ? "QmClient-Setup.exe" : (SevenZipPackage ? (PortableBuild ? "QmClient-windows-portable.7z" : "QmClient-windows.7z") : (PortableBuild ? "QmClient-windows-portable.zip" : "QmClient-windows.zip"))) != 0 ||
		pSize->type != json_integer || pSize->u.integer <= 0 || static_cast<uint64_t>(pSize->u.integer) > MAX_UPDATE_PACKAGE_SIZE ||
		pSha256->type != json_string || sha256_from_str(&Manifest.m_PackageSha256, json_string_get(pSha256)) != 0)
	{
		SetError(pError, ErrorSize, "Update manifest package metadata is invalid");
		return false;
	}
	Manifest.m_PackageSize = static_cast<uint64_t>(pSize->u.integer);
	SetError(pError, ErrorSize, "");
	return true;
}
