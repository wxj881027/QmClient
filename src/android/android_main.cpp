#include "android_main.h"

#include "asset_integrity.h"
#include "asset_unpack.h"
#include "jni_calls.h"

#include <base/hash.h>
#include <base/log.h>
#include <base/system.h>

#include <engine/shared/linereader.h>

#include <SDL3/SDL.h>
#include <jni.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

static bool UnpackAsset(const char *pFilename) noexcept
{
	try
	{
		constexpr const char *pPrefix = "asset_integrity_files/";
		char aAssetFilename[IO_MAX_PATH_LENGTH];
		if(!pFilename || !pFilename[0] || str_length(pFilename) >= static_cast<int>(sizeof(aAssetFilename)) - str_length(pPrefix))
			return false;
		str_copy(aAssetFilename, pPrefix);
		str_append(aAssetFilename, pFilename);
		std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> pAssetFile(SDL_IOFromFile(aAssetFilename, "rb"), &SDL_CloseIO);
		if(!pAssetFile)
		{
			log_error("android", "Failed to open asset '%s' for reading", pFilename);
			return false;
		}
		const auto FileLength = SDL_GetIOSize(pAssetFile.get());
		if(FileLength < 0)
		{
			log_error("android", "Failed to determine length of asset '%s'", pFilename);
			return false;
		}
		// 同目录独占候选，不截断正式资源，也不复用其他解包留下的候选。
		char aCandidate[IO_MAX_PATH_LENGTH + 16];
		const char *pLastSlash = str_rchr(pFilename, '/');
		const int ParentLength = pLastSlash ? static_cast<int>(pLastSlash - pFilename + 1) : 0;
		str_format(aCandidate, sizeof(aCandidate), "%.*s.qm-asset-XXXXXX", ParentLength, pFilename);
		const int CandidateDescriptor = mkstemp(aCandidate);
		if(CandidateDescriptor < 0)
			return false;
		FILE *pCandidateFile = fdopen(CandidateDescriptor, "wb");
		if(!pCandidateFile)
		{
			close(CandidateDescriptor);
			return false;
		}
		std::unique_ptr<void, decltype(&io_close)> Target(pCandidateFile, &io_close);
		const auto Read = [&](void *pData, size_t Size, size_t &ReadBytes) {
			ReadBytes = SDL_ReadIO(pAssetFile.get(), pData, Size);
			return SDL_GetIOStatus(pAssetFile.get()) != SDL_IO_STATUS_ERROR;
		};
		const auto Write = [&](const void *pData, size_t Size) {
			return io_write(Target.get(), pData, static_cast<unsigned>(Size)) == Size && io_error(Target.get()) == 0;
		};
		const auto CloseInput = [&]() noexcept { return SDL_CloseIO(pAssetFile.release()); };
		const auto CloseOutput = [&](bool Complete) noexcept {
			const bool Flushed = !Complete || io_flush(Target.get()) == 0;
			const bool Closed = io_close(Target.release()) == 0;
			return Complete && Flushed && Closed;
		};
		const auto Publish = [&] { return fs_rename(aCandidate, pFilename, false) == 0; };
		const auto Stage = android_assets::Unpack(FileLength, Read, Write, CloseInput, CloseOutput, Publish);
		if(Stage != android_assets::EStage::DONE)
			log_error("android", "Failed to unpack asset '%s' at stage %d (candidate '%s')", pFilename, static_cast<int>(Stage), aCandidate);
		return Stage == android_assets::EStage::DONE;
	}
	catch(...)
	{
		return false;
	}
}

constexpr const char *INTEGRITY_INDEX = "integrity.txt";
constexpr const char *INTEGRITY_INDEX_SAVE = "integrity_save.txt";

// The first line of each integrity file contains the combined hash for all files,
// if the hashes match then we assume that the unpacked data folder is up-to-date.
static bool EqualIntegrityFiles(const char *pAssetFilename, const char *pStorageFilename)
{
	IOHANDLE StorageFile = io_open(pStorageFilename, IOFLAG_READ);
	if(!StorageFile)
	{
		return false;
	}

	char aStorageMainSha256[SHA256_MAXSTRSIZE];
	const size_t StorageReadLength = io_read(StorageFile, aStorageMainSha256, sizeof(aStorageMainSha256) - 1);
	io_close(StorageFile);
	if(StorageReadLength != sizeof(aStorageMainSha256) - 1)
	{
		return false;
	}
	aStorageMainSha256[sizeof(aStorageMainSha256) - 1] = '\0';

	char aAssetFilename[IO_MAX_PATH_LENGTH] = "asset_integrity_files/";
	str_append(aAssetFilename, pAssetFilename);

	SDL_IOStream *pAssetFile = SDL_IOFromFile(aAssetFilename, "rb");
	if(!pAssetFile)
	{
		return false;
	}

	char aAssetMainSha256[SHA256_MAXSTRSIZE];
	const size_t AssetReadLength = SDL_ReadIO(pAssetFile, aAssetMainSha256, sizeof(aAssetMainSha256) - 1);
	SDL_CloseIO(pAssetFile);
	if(AssetReadLength != sizeof(aAssetMainSha256) - 1)
	{
		return false;
	}
	aAssetMainSha256[sizeof(aAssetMainSha256) - 1] = '\0';

	return str_comp(aStorageMainSha256, aAssetMainSha256) == 0;
}

using android_assets::CIntegrityFileLine;
using android_assets::ReadIntegrityFile;

const char *InitAndroid()
{
	// Change current working directory to our external storage location
	const char *pPath = SDL_GetAndroidExternalStoragePath();
	if(pPath == nullptr)
	{
		return "The external storage is not available.";
	}
	if(fs_chdir(pPath) != 0)
	{
		return "Failed to change current directory to external storage.";
	}
	log_info("android", "Changed current directory to '%s'", pPath);

	if(fs_makedir("data") != 0 || fs_makedir("user") != 0)
	{
		return "Failed to create 'data' and 'user' directories in external storage.";
	}

	if(EqualIntegrityFiles(INTEGRITY_INDEX, INTEGRITY_INDEX_SAVE))
	{
		return nullptr;
	}

	if(!UnpackAsset(INTEGRITY_INDEX))
	{
		return "Failed to unpack the integrity index file. Consider reinstalling the app.";
	}

	auto vIntegrityLines = ReadIntegrityFile(INTEGRITY_INDEX);
	if(vIntegrityLines.empty())
	{
		return "Failed to load the integrity index file. Consider reinstalling the app.";
	}

	auto vIntegritySaveLines = ReadIntegrityFile(INTEGRITY_INDEX_SAVE);

	// The remaining lines of each integrity file list all assets and their hashes
	for(size_t i = 1; i < vIntegrityLines.size(); ++i)
	{
		const CIntegrityFileLine &IntegrityLine = vIntegrityLines[i];

		// Check if the asset is unchanged from the last unpacking
		const auto IntegritySaveLine = std::find_if(vIntegritySaveLines.begin(), vIntegritySaveLines.end(), [&](const CIntegrityFileLine &Line) {
			return str_comp(Line.m_aFilename, IntegrityLine.m_aFilename) == 0;
		});
		if(IntegritySaveLine != vIntegritySaveLines.end() && IntegritySaveLine->m_Sha256 == IntegrityLine.m_Sha256)
		{
			continue;
		}

		if(fs_makedir_rec_for(IntegrityLine.m_aFilename) != 0 || !UnpackAsset(IntegrityLine.m_aFilename))
		{
			return "Failed to unpack game assets, consider reinstalling the app.";
		}
	}

	for(size_t i = 1; i < vIntegritySaveLines.size(); ++i)
	{
		const CIntegrityFileLine &IntegritySaveLine = vIntegritySaveLines[i];

		// Check if the asset was deleted since the last unpacking
		const bool LineFound = std::any_of(vIntegrityLines.begin(), vIntegrityLines.end(), [&](const CIntegrityFileLine &Line) {
			return str_comp(Line.m_aFilename, IntegritySaveLine.m_aFilename) == 0;
		});
		if(LineFound)
		{
			continue;
		}

		if(fs_remove(IntegritySaveLine.m_aFilename) != 0)
		{
			log_warn("android", "Failed to delete unused asset '%s'", IntegritySaveLine.m_aFilename);
		}
	}

	// The integrity file will be unpacked every time when launching,
	// so we can simply rename it to update the saved integrity file.
	if(fs_rename(INTEGRITY_INDEX, INTEGRITY_INDEX_SAVE, false) != 0)
	{
		return "Failed to update the saved integrity index file.";
	}

	return nullptr;
}

// See ClientActivity.java
constexpr uint32_t COMMAND_USER = 0x8000;
constexpr uint32_t COMMAND_RESTART_APP = COMMAND_USER + 1;

void RestartAndroidApp()
{
	SDL_SendAndroidMessage(COMMAND_RESTART_APP, 0);
}

bool StartAndroidServer(const char **ppArguments, size_t NumArguments)
{
	JNIEnv *pEnv = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	return android_jni::Call(pEnv, [] { return static_cast<jobject>(SDL_GetAndroidActivity()); }, [&](jobject Activity) { return android_jni::StartServer(*pEnv, Activity, ppArguments, NumArguments); });
}

void StopAndroidServer()
{
	JNIEnv *pEnv = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	android_jni::Call(pEnv, [] { return static_cast<jobject>(SDL_GetAndroidActivity()); }, [&](jobject Activity) { return android_jni::StopServer(*pEnv, Activity); });
}

void ExecuteAndroidServerCommand(const char *pCommand)
{
	JNIEnv *pEnv = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	android_jni::Call(pEnv, [] { return static_cast<jobject>(SDL_GetAndroidActivity()); }, [&](jobject Activity) { return android_jni::ExecuteCommand(*pEnv, Activity, pCommand); });
}

bool IsAndroidServerRunning()
{
	JNIEnv *pEnv = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	return android_jni::Call(pEnv, [] { return static_cast<jobject>(SDL_GetAndroidActivity()); }, [&](jobject Activity) { return android_jni::IsRunning(*pEnv, Activity); });
}

bool ShowAndroidMediaFiles(const char *pDirectory, bool Videos, const char *const *ppExcluded, size_t NumExcluded)
{
	JNIEnv *pEnv = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	return android_jni::Call(pEnv, [] { return static_cast<jobject>(SDL_GetAndroidActivity()); }, [&](jobject Activity) { return android_jni::ShowMedia(*pEnv, Activity, pDirectory, Videos, ppExcluded, NumExcluded); });
}

namespace
{
	// SDL 回调可能晚于组件销毁，用户数据只指向进程期的原子状态。
	std::atomic<EAndroidPermissionState> gs_RecordPermission = EAndroidPermissionState::UNKNOWN;
	std::atomic<EAndroidPermissionState> gs_NotificationPermission = EAndroidPermissionState::UNKNOWN;

	void SDLCALL PermissionResult(void *pUser, const char *pPermission, bool Granted)
	{
		static_cast<std::atomic<EAndroidPermissionState> *>(pUser)->store(Granted ? EAndroidPermissionState::GRANTED : EAndroidPermissionState::DENIED);
	}

	void RequestPermission(const char *pPermission, std::atomic<EAndroidPermissionState> &State)
	{
		if(State.exchange(EAndroidPermissionState::PENDING) == EAndroidPermissionState::PENDING)
			return;
		if(!SDL_RequestAndroidPermission(pPermission, PermissionResult, &State))
			State.store(EAndroidPermissionState::DENIED);
	}
}

void RequestAndroidAudioRecordPermission()
{
	RequestPermission("android.permission.RECORD_AUDIO", gs_RecordPermission);
}

EAndroidPermissionState AndroidAudioRecordPermission()
{
	return gs_RecordPermission.load();
}

void RequestAndroidNotificationPermission()
{
	if(SDL_GetAndroidSDKVersion() < 33)
		gs_NotificationPermission.store(EAndroidPermissionState::GRANTED);
	else
		RequestPermission("android.permission.POST_NOTIFICATIONS", gs_NotificationPermission);
}

EAndroidPermissionState AndroidNotificationPermission()
{
	return gs_NotificationPermission.load();
}
