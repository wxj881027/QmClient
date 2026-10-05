#include "update_manifest.h"

#include "update_version.h"

#include <base/system.h>

#include <engine/external/json-parser/json.h>
#include <engine/shared/json.h>

#include <limits>
#include <memory>

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

	bool ParseReleaseObject(const json_value *pRoot, const char *pCurrentVersion, SQmClientUpdateRelease &Release, char *pError, size_t ErrorSize, bool LocalIsDevelopmentBuild, bool PortableBuild)
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
		if(!IsQmClientRemoteVersionNewer(Release.m_aVersion, pCurrentVersion, LocalIsDevelopmentBuild))
		{
			SetError(pError, ErrorSize, "GitHub release version is not newer");
			return false;
		}

		struct SExpectedAsset
		{
			const char *m_pName;
			char *m_pUrl;
			size_t m_UrlSize;
			bool m_Found = false;
			bool m_Optional = false;
		};
		SExpectedAsset aExpected[] = {
			{PortableBuild ? "QmClient-windows-portable.zip" : "QmClient-windows.zip", Release.m_aPackageUrl, sizeof(Release.m_aPackageUrl)},
			{PortableBuild ? "QmClient-windows-portable.zip.sig" : "QmClient-windows.zip.sig", Release.m_aPackageSignatureUrl, sizeof(Release.m_aPackageSignatureUrl)},
			{PortableBuild ? "QmClient-windows-portable-update.json" : "QmClient-windows-update.json", Release.m_aManifestUrl, sizeof(Release.m_aManifestUrl)},
			{PortableBuild ? "QmClient-windows-portable-update.json.sig" : "QmClient-windows-update.json.sig", Release.m_aManifestSignatureUrl, sizeof(Release.m_aManifestSignatureUrl)},
			{"QmClient-Setup.exe", Release.m_aSetupUrl, sizeof(Release.m_aSetupUrl), false, true},
			{"QmClient-Setup.exe.sig", Release.m_aSetupSignatureUrl, sizeof(Release.m_aSetupSignatureUrl), false, true},
			{"QmClient-windows-setup-update.json", Release.m_aSetupManifestUrl, sizeof(Release.m_aSetupManifestUrl), false, true},
			{"QmClient-windows-setup-update.json.sig", Release.m_aSetupManifestSignatureUrl, sizeof(Release.m_aSetupManifestSignatureUrl), false, true},
		};
		for(unsigned Index = 0; Index < pAssets->u.array.length; ++Index)
		{
			const json_value *pAsset = json_array_get(pAssets, Index);
			const json_value *pName = pAsset && pAsset->type == json_object ? json_object_get(pAsset, "name") : nullptr;
			if(!pName || pName->type != json_string)
				continue;
			for(auto &Expected : aExpected)
			{
				if(PortableBuild && Expected.m_Optional)
					continue;
				if(str_comp(json_string_get(pName), Expected.m_pName) != 0)
					continue;
				if(Expected.m_Found || !ReadReleaseAsset(pAsset, Expected.m_pName, Expected.m_pUrl, Expected.m_UrlSize))
					return false;
				Expected.m_Found = true;
			}
		}
		for(const auto &Expected : aExpected)
		{
			if(!Expected.m_Found && !Expected.m_Optional)
			{
				SetError(pError, ErrorSize, "GitHub release is missing a required update asset");
				return false;
			}
		}
		SetError(pError, ErrorSize, "");
		return true;
	}
}

bool ParseQmClientUpdateRelease(const char *pJson, size_t JsonSize, const char *pCurrentVersion, SQmClientUpdateRelease &Release, char *pError, size_t ErrorSize, bool LocalIsDevelopmentBuild, bool PortableBuild)
{
	Release = {};
	SetError(pError, ErrorSize, "Invalid GitHub release metadata");
	if(!pJson || JsonSize == 0 || JsonSize > 4 * 1024 * 1024 || JsonSize > std::numeric_limits<unsigned>::max())
		return false;
	TJson Root(JsonParse(pJson, static_cast<unsigned>(JsonSize)), json_value_free);
	if(!Root)
		return false;
	if(Root->type == json_object)
		return ParseReleaseObject(Root.get(), pCurrentVersion, Release, pError, ErrorSize, LocalIsDevelopmentBuild, PortableBuild);
	if(Root->type != json_array)
		return false;

	// 预览通道读取发布列表，按版本排序，忽略草稿、附件不完整和其他用途的 Release。
	for(unsigned Index = 0; Index < Root->u.array.length; ++Index)
	{
		SQmClientUpdateRelease Candidate;
		char aCandidateError[256];
		if(ParseReleaseObject(json_array_get(Root.get(), Index), pCurrentVersion, Candidate, aCandidateError, sizeof(aCandidateError), LocalIsDevelopmentBuild, PortableBuild) &&
			(Release.m_aVersion[0] == '\0' || IsQmClientRemoteVersionNewer(Candidate.m_aVersion, Release.m_aVersion, LocalIsDevelopmentBuild)))
			Release = Candidate;
	}
	const bool Found = Release.m_aVersion[0] != '\0';
	SetError(pError, ErrorSize, Found ? "" : "GitHub release version is not newer");
	return Found;
}

bool ParseQmClientUpdateManifest(const char *pJson, size_t JsonSize, const char *pCurrentVersion, SQmClientUpdateManifest &Manifest, char *pError, size_t ErrorSize, bool LocalIsDevelopmentBuild, bool SetupPackage, bool PortableBuild)
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
	if(!pName || !pSize || !pSha256 || pName->type != json_string || str_comp(json_string_get(pName), SetupPackage ? "QmClient-Setup.exe" : (PortableBuild ? "QmClient-windows-portable.zip" : "QmClient-windows.zip")) != 0 ||
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
