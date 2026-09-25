#include "update_manifest.h"

#include "update_version.h"

#include <base/system.h>

#include <engine/external/json-parser/json.h>
#include <engine/shared/json.h>

#include <limits>

namespace
{
	constexpr uint64_t MAX_UPDATE_PACKAGE_SIZE = 5ULL * 1024 * 1024 * 1024;
	constexpr const char *UPDATE_ASSET_URL_PREFIX = "https://github.com/wxj881027/QmClient/releases/download/";

	void SetError(char *pError, size_t ErrorSize, const char *pMessage)
	{
		if(pError != nullptr && ErrorSize > 0)
			str_copy(pError, pMessage, ErrorSize);
	}

	bool NormalizeStableVersion(const char *pVersion, char *pBuffer, size_t BufferSize)
	{
		if(!pVersion || !pBuffer || BufferSize == 0)
			return false;
		if(*pVersion == 'v' || *pVersion == 'V')
			++pVersion;
		const size_t Length = str_length(pVersion);
		if(Length == 0 || Length >= BufferSize)
			return false;
		int Parts = 1;
		bool HasDigit = false;
		for(const char *pCursor = pVersion; *pCursor; ++pCursor)
		{
			if(*pCursor >= '0' && *pCursor <= '9')
			{
				HasDigit = true;
				continue;
			}
			if(*pCursor != '.' || !HasDigit || Parts == 4)
				return false;
			++Parts;
			HasDigit = false;
		}
		if(!HasDigit)
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
		if(!str_startswith(pValue, UPDATE_ASSET_URL_PREFIX) || str_length(pValue) >= UrlSize)
			return false;
		str_copy(pUrl, pValue, UrlSize);
		return true;
	}
}

bool ParseQmClientUpdateRelease(const char *pJson, size_t JsonSize, const char *pCurrentVersion, SQmClientUpdateRelease &Release, char *pError, size_t ErrorSize, bool LocalIsDevelopmentBuild)
{
	Release = {};
	SetError(pError, ErrorSize, "Invalid GitHub release metadata");
	if(!pJson || JsonSize == 0 || JsonSize > 4 * 1024 * 1024 || JsonSize > std::numeric_limits<unsigned>::max())
		return false;

	json_value *pRoot = JsonParse(pJson, static_cast<unsigned>(JsonSize));
	if(!pRoot)
		return false;
	const auto FreeRoot = [&pRoot]() { json_value_free(pRoot); };
	if(pRoot->type != json_object)
	{
		FreeRoot();
		return false;
	}

	const json_value *pTagName = json_object_get(pRoot, "tag_name");
	const json_value *pDraft = json_object_get(pRoot, "draft");
	const json_value *pPrerelease = json_object_get(pRoot, "prerelease");
	const json_value *pAssets = json_object_get(pRoot, "assets");
	if(!pTagName || !pDraft || !pPrerelease || !pAssets || pTagName->type != json_string ||
		pDraft->type != json_boolean || pPrerelease->type != json_boolean || pDraft->u.boolean || pPrerelease->u.boolean || pAssets->type != json_array ||
		!NormalizeStableVersion(json_string_get(pTagName), Release.m_aVersion, sizeof(Release.m_aVersion)))
	{
		FreeRoot();
		return false;
	}
	if(!IsQmClientRemoteVersionNewer(Release.m_aVersion, pCurrentVersion, LocalIsDevelopmentBuild))
	{
		SetError(pError, ErrorSize, "GitHub release version is not newer");
		FreeRoot();
		return false;
	}

	struct SExpectedAsset
	{
		const char *m_pName;
		char *m_pUrl;
		size_t m_UrlSize;
		bool m_Found = false;
	};
	SExpectedAsset aExpected[] = {
		{"QmClient-windows.zip", Release.m_aPackageUrl, sizeof(Release.m_aPackageUrl)},
		{"QmClient-windows.zip.sig", Release.m_aPackageSignatureUrl, sizeof(Release.m_aPackageSignatureUrl)},
		{"QmClient-windows-update.json", Release.m_aManifestUrl, sizeof(Release.m_aManifestUrl)},
		{"QmClient-windows-update.json.sig", Release.m_aManifestSignatureUrl, sizeof(Release.m_aManifestSignatureUrl)},
	};
	for(unsigned Index = 0; Index < pAssets->u.array.length; ++Index)
	{
		const json_value *pAsset = json_array_get(pAssets, Index);
		const json_value *pName = pAsset && pAsset->type == json_object ? json_object_get(pAsset, "name") : nullptr;
		if(!pName || pName->type != json_string)
			continue;
		for(auto &Expected : aExpected)
		{
			if(str_comp(json_string_get(pName), Expected.m_pName) != 0)
				continue;
			if(Expected.m_Found || !ReadReleaseAsset(pAsset, Expected.m_pName, Expected.m_pUrl, Expected.m_UrlSize))
			{
				FreeRoot();
				return false;
			}
			Expected.m_Found = true;
		}
	}
	for(const auto &Expected : aExpected)
	{
		if(!Expected.m_Found)
		{
			SetError(pError, ErrorSize, "GitHub release is missing a required update asset");
			FreeRoot();
			return false;
		}
	}

	SetError(pError, ErrorSize, "");
	FreeRoot();
	return true;
}

bool ParseQmClientUpdateManifest(const char *pJson, size_t JsonSize, const char *pCurrentVersion, SQmClientUpdateManifest &Manifest, char *pError, size_t ErrorSize, bool LocalIsDevelopmentBuild)
{
	Manifest = {};
	SetError(pError, ErrorSize, "Invalid update manifest");
	if(pJson == nullptr || JsonSize == 0 || JsonSize > std::numeric_limits<unsigned>::max())
		return false;

	json_value *pRoot = JsonParse(pJson, static_cast<unsigned>(JsonSize));
	if(pRoot == nullptr)
		return false;
	const auto FreeRoot = [&pRoot]() { json_value_free(pRoot); };
	if(pRoot->type != json_object)
	{
		FreeRoot();
		return false;
	}

	const json_value *pSchema = json_object_get(pRoot, "schema");
	const json_value *pVersion = json_object_get(pRoot, "version");
	const json_value *pPackage = json_object_get(pRoot, "package");
	const json_value *pFiles = json_object_get(pRoot, "files");
	if(!pSchema || !pVersion || !pPackage || !pFiles || pSchema->type != json_integer || pSchema->u.integer != 1 ||
		pVersion->type != json_string || pPackage->type != json_object || pFiles->type != json_array)
	{
		FreeRoot();
		return false;
	}

	const char *pRemoteVersion = json_string_get(pVersion);
	if(!IsQmClientRemoteVersionNewer(pRemoteVersion, pCurrentVersion, LocalIsDevelopmentBuild))
	{
		SetError(pError, ErrorSize, "Update manifest version is not newer");
		FreeRoot();
		return false;
	}

	const json_value *pName = json_object_get(pPackage, "name");
	const json_value *pSize = json_object_get(pPackage, "size");
	const json_value *pSha256 = json_object_get(pPackage, "sha256");
	if(!pName || !pSize || !pSha256 || pName->type != json_string || str_comp(json_string_get(pName), "QmClient-windows.zip") != 0 ||
		pSize->type != json_integer || pSize->u.integer <= 0 || static_cast<uint64_t>(pSize->u.integer) > MAX_UPDATE_PACKAGE_SIZE ||
		pSha256->type != json_string || sha256_from_str(&Manifest.m_PackageSha256, json_string_get(pSha256)) != 0)
	{
		SetError(pError, ErrorSize, "Update manifest package metadata is invalid");
		FreeRoot();
		return false;
	}

	str_copy(Manifest.m_aVersion, pRemoteVersion, sizeof(Manifest.m_aVersion));
	Manifest.m_PackageSize = static_cast<uint64_t>(pSize->u.integer);
	SetError(pError, ErrorSize, "");
	FreeRoot();
	return true;
}
