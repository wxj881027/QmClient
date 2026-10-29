#include "android_main.h"

#include "asset_unpack.h"

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
		str_format(aCandidate, sizeof(aCandidate), "%s.tmp.XXXXXX", pFilename);
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

class CIntegrityFileLine
{
public:
	char m_aFilename[IO_MAX_PATH_LENGTH];
	SHA256_DIGEST m_Sha256;
};

static std::vector<CIntegrityFileLine> ReadIntegrityFile(const char *pFilename)
{
	CLineReader LineReader;
	if(!LineReader.OpenFile(io_open(pFilename, IOFLAG_READ)))
	{
		return {};
	}

	std::vector<CIntegrityFileLine> vLines;
	while(const char *pReadLine = LineReader.Get())
	{
		const char *pSpaceInLine = str_rchr(pReadLine, ' ');
		CIntegrityFileLine Line;
		char aSha256[SHA256_MAXSTRSIZE];
		if(pSpaceInLine == nullptr)
		{
			if(!vLines.empty())
			{
				// Only the first line is allowed to not contain a filename
				log_error("android", "Failed to parse line %" PRIzu " of '%s': line does not contain space", vLines.size() + 1, pFilename);
				return {};
			}
			Line.m_aFilename[0] = '\0';
			str_copy(aSha256, pReadLine);
		}
		else
		{
			str_truncate(Line.m_aFilename, sizeof(Line.m_aFilename), pReadLine, pSpaceInLine - pReadLine);
			str_copy(aSha256, pSpaceInLine + 1);
		}
		if(sha256_from_str(&Line.m_Sha256, aSha256) != 0)
		{
			log_error("android", "Failed to parse line %" PRIzu " of '%s': invalid SHA256 string", vLines.size() + 1, pFilename);
			return {};
		}
		vLines.emplace_back(std::move(Line));
	}

	return vLines;
}

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

	std::vector<CIntegrityFileLine> vIntegrityLines = ReadIntegrityFile(INTEGRITY_INDEX);
	if(vIntegrityLines.empty())
	{
		return "Failed to load the integrity index file. Consider reinstalling the app.";
	}

	std::vector<CIntegrityFileLine> vIntegritySaveLines = ReadIntegrityFile(INTEGRITY_INDEX_SAVE);

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
	jobject Activity = (jobject)SDL_GetAndroidActivity();
	jclass ActivityClass = pEnv->GetObjectClass(Activity);

	jclass StringClass = pEnv->FindClass("java/lang/String");
	jobjectArray ArgumentsArray = pEnv->NewObjectArray(NumArguments, StringClass, nullptr);
	pEnv->DeleteLocalRef(StringClass);
	for(size_t ArgumentIndex = 0; ArgumentIndex < NumArguments; ArgumentIndex++)
	{
		jstring ArgumentString = pEnv->NewStringUTF(ppArguments[ArgumentIndex]);
		pEnv->SetObjectArrayElement(ArgumentsArray, ArgumentIndex, ArgumentString);
		pEnv->DeleteLocalRef(ArgumentString);
	}

	jmethodID MethodId = pEnv->GetMethodID(ActivityClass, "startServer", "([Ljava/lang/String;)V");
	pEnv->CallVoidMethod(Activity, MethodId, ArgumentsArray);

	pEnv->DeleteLocalRef(ArgumentsArray);
	pEnv->DeleteLocalRef(Activity);
	pEnv->DeleteLocalRef(ActivityClass);

	return true;
}

void ExecuteAndroidServerCommand(const char *pCommand)
{
	JNIEnv *pEnv = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	jobject Activity = (jobject)SDL_GetAndroidActivity();
	jclass ActivityClass = pEnv->GetObjectClass(Activity);

	jstring Command = pEnv->NewStringUTF(pCommand);

	jmethodID MethodId = pEnv->GetMethodID(ActivityClass, "executeCommand", "(Ljava/lang/String;)V");
	pEnv->CallVoidMethod(Activity, MethodId, Command);

	pEnv->DeleteLocalRef(Command);
	pEnv->DeleteLocalRef(Activity);
	pEnv->DeleteLocalRef(ActivityClass);
}

bool IsAndroidServerRunning()
{
	JNIEnv *pEnv = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	jobject Activity = (jobject)SDL_GetAndroidActivity();
	jclass ActivityClass = pEnv->GetObjectClass(Activity);

	jmethodID MethodId = pEnv->GetMethodID(ActivityClass, "isServerRunning", "()Z");
	const bool Result = pEnv->CallBooleanMethod(Activity, MethodId);

	pEnv->DeleteLocalRef(Activity);
	pEnv->DeleteLocalRef(ActivityClass);

	return Result;
}

bool ShowAndroidMediaFiles(const char *pDirectory, bool Videos, const char *const *ppExcluded, size_t NumExcluded)
{
	JNIEnv *pEnv = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	if(pEnv == nullptr)
		return false;
	jobject Activity = static_cast<jobject>(SDL_GetAndroidActivity());
	if(Activity == nullptr)
		return false;
	const bool HasFrame = pEnv->PushLocalFrame(8) == 0;
	// JNI 的 NewStringUTF 使用 modified UTF-8，路径中的表情等字符需转为 UTF-16。
	auto MakeString = [&](const char *pText) {
		std::vector<jchar> vCharacters;
		while(*pText != '\0')
		{
			const int Codepoint = str_utf8_decode(&pText);
			if(Codepoint < 0)
				vCharacters.push_back(0xfffd);
			else if(Codepoint <= 0xffff)
				vCharacters.push_back((jchar)Codepoint);
			else
			{
				vCharacters.push_back((jchar)(0xd800 + ((Codepoint - 0x10000) >> 10)));
				vCharacters.push_back((jchar)(0xdc00 + ((Codepoint - 0x10000) & 0x3ff)));
			}
		}
		return pEnv->NewString(vCharacters.data(), (jsize)vCharacters.size());
	};
	const bool Success = HasFrame && [&] {
		jclass ActivityClass = pEnv->GetObjectClass(Activity);
		if(ActivityClass == nullptr)
			return false;
		jmethodID Method = pEnv->GetMethodID(ActivityClass, "showMediaFiles", "(Ljava/lang/String;Z[Ljava/lang/String;)V");
		if(Method == nullptr)
			return false;
		jclass StringClass = pEnv->FindClass("java/lang/String");
		if(StringClass == nullptr)
			return false;
		jstring Directory = MakeString(pDirectory);
		if(Directory == nullptr)
			return false;
		jobjectArray Excluded = pEnv->NewObjectArray((jsize)NumExcluded, StringClass, nullptr);
		if(Excluded == nullptr)
			return false;
		for(size_t i = 0; i < NumExcluded; ++i)
		{
			jstring Path = MakeString(ppExcluded[i]);
			if(Path == nullptr)
				return false;
			pEnv->SetObjectArrayElement(Excluded, (jsize)i, Path);
			pEnv->DeleteLocalRef(Path);
			if(pEnv->ExceptionCheck())
				return false;
		}
		pEnv->CallVoidMethod(Activity, Method, Directory, (jboolean)Videos, Excluded);
		return !pEnv->ExceptionCheck();
	}();
	if(pEnv->ExceptionCheck())
	{
		pEnv->ExceptionDescribe();
		pEnv->ExceptionClear();
	}
	if(HasFrame)
		pEnv->PopLocalFrame(nullptr);
	pEnv->DeleteLocalRef(Activity);
	return Success;
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
