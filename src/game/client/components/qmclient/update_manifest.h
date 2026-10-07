#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_MANIFEST_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_MANIFEST_H

#include <base/hash.h>

#include <cstddef>
#include <cstdint>
#include <string>

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
	char m_aSetupUrl[2048] = "";
	char m_aSetupSignatureUrl[2048] = "";
	char m_aSetupManifestUrl[2048] = "";
	char m_aSetupManifestSignatureUrl[2048] = "";
	std::string m_Notes;
	bool m_NewVersion = false;
	bool m_PackageAvailable = false;
	bool m_SevenZip = false;
	bool HasSetup() const { return m_aSetupUrl[0] && m_aSetupSignatureUrl[0] && m_aSetupManifestUrl[0] && m_aSetupManifestSignatureUrl[0]; }
};

// 安装标记与完整 Setup 附件共同决定更新路径，旧发布保持 ZIP 回退。
inline bool UseQmClientSetupUpdate(bool SetupInstalled, const SQmClientUpdateRelease &Release, bool PortableBuild = false) { return !PortableBuild && SetupInstalled && Release.HasSetup(); }

// 发布信息与安装包能力分别解析；有效发布没有匹配附件时仍返回版本和说明。
bool ParseQmClientReleaseInfo(const char *pJson, size_t JsonSize, const char *pCurrentVersion, SQmClientUpdateRelease &Release, char *pError, size_t ErrorSize, bool LocalIsDevelopmentBuild = false, bool PortableBuild = false);

inline bool CanDownloadQmClientRelease(const SQmClientUpdateRelease &Release, bool SetupInstalled, bool PortableBuild)
{
	return Release.m_NewVersion && (Release.m_PackageAvailable || UseQmClientSetupUpdate(SetupInstalled, Release, PortableBuild));
}

bool ParseQmClientUpdateManifest(const char *pJson, size_t JsonSize, const char *pCurrentVersion, SQmClientUpdateManifest &Manifest, char *pError, size_t ErrorSize, bool LocalIsDevelopmentBuild = false, bool SetupPackage = false, bool PortableBuild = false, bool SevenZipPackage = false);
bool ParseQmClientUpdateRelease(const char *pJson, size_t JsonSize, const char *pCurrentVersion, SQmClientUpdateRelease &Release, char *pError, size_t ErrorSize, bool LocalIsDevelopmentBuild = false, bool PortableBuild = false);

#endif
