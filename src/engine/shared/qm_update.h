#ifndef ENGINE_SHARED_QM_UPDATE_H
#define ENGINE_SHARED_QM_UPDATE_H

#include <cstddef>
#include <cstdint>

extern "C" {

bool qm_update_verify_manifest(const uint8_t *pManifest, size_t ManifestSize, const uint8_t *pSignature, size_t SignatureSize, char *pError, size_t ErrorSize);
bool qm_update_verify_manifest_package(const uint8_t *pManifest, size_t ManifestSize, const uint8_t *pSignature, size_t SignatureSize, uint64_t *pPackageSize, uint8_t *pPackageDigest, size_t PackageDigestSize, char *pError, size_t ErrorSize);
bool qm_update_verify_setup_files(const char *pPackagePath, const char *pPackageSignaturePath, const char *pManifestPath, const char *pManifestSignaturePath, const char *pCurrentVersion, char *pError, size_t ErrorSize);
bool qm_update_verify_setup_manifest(const uint8_t *pManifest, size_t ManifestSize, const uint8_t *pSignature, size_t SignatureSize, uint64_t *pPackageSize, uint8_t *pPackageDigest, size_t PackageDigestSize, char *pError, size_t ErrorSize);
bool qm_update_verify_package_digest(const uint8_t *pDigest, size_t DigestSize, const uint8_t *pSignature, size_t SignatureSize, char *pError, size_t ErrorSize);
bool qm_update_extract_bootstrap_updater(const char *pPackagePath, const uint8_t *pManifest, size_t ManifestSize, const uint8_t *pSignature, size_t SignatureSize, const char *pDestination, char *pError, size_t ErrorSize);
bool qm_update_apply(const char *pPackagePath, const char *pPackageSignaturePath, const char *pManifestPath, const char *pManifestSignaturePath, const char *pInstallPath, const char *pCurrentVersion, char *pError, size_t ErrorSize);
}

#endif
