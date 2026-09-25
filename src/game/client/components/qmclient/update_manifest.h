#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_MANIFEST_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_MANIFEST_H

#include <base/hash.h>

#include <cstddef>
#include <cstdint>

struct SQmClientUpdateManifest
{
	char m_aVersion[32] = "";
	uint64_t m_PackageSize = 0;
	SHA256_DIGEST m_PackageSha256{};
};

struct SQmClientUpdateRelease
{
	char m_aVersion[32] = "";
	char m_aPackageUrl[2048] = "";
	char m_aPackageSignatureUrl[2048] = "";
	char m_aManifestUrl[2048] = "";
	char m_aManifestSignatureUrl[2048] = "";
};

bool ParseQmClientUpdateManifest(const char *pJson, size_t JsonSize, const char *pCurrentVersion, SQmClientUpdateManifest &Manifest, char *pError, size_t ErrorSize, bool LocalIsDevelopmentBuild = false);
bool ParseQmClientUpdateRelease(const char *pJson, size_t JsonSize, const char *pCurrentVersion, SQmClientUpdateRelease &Release, char *pError, size_t ErrorSize, bool LocalIsDevelopmentBuild = false);

#endif
