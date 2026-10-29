#ifndef ANDROID_ANDROID_MAIN_H
#define ANDROID_ANDROID_MAIN_H

#include <base/detect.h>
#if !defined(CONF_PLATFORM_ANDROID)
#error "This header should only be included when compiling for Android"
#endif

#include <cstddef>

/**
 * @defgroup Android Android
 *
 * Android-specific functions to interact with the ClientActivity.
 *
 * Important note: These functions may only be called from the main native thread
 * which is created by the SDLActivity (super class of ClientActivity), otherwise
 * JNI calls are not possible because the JNI environment is not attached to that
 * thread. See https://developer.android.com/training/articles/perf-jni#threads
 */

/**
 * Initializes the Android storage. Must be called on Android-systems
 * before using any of the I/O and storage functions.
 *
 * @ingroup Android
 *
 * This will change the current working directory to the app specific external
 * storage location and unpack the assets from the APK file to the `data` folder.
 * The folder `user` is created in the external storage to store the user data.
 *
 * Failure must be handled by exiting the app.
 *
 * @return `nullptr` on success, error message on failure.
 */
const char *InitAndroid();

/**
 * Sends an intent to the Android system to restart the app.
 *
 * @ingroup Android
 *
 * This will restart the main activity in a new task. The current process
 * must immediately terminate after this function is called.
 */
void RestartAndroidApp();

/**
 * Starts the local server as an Android service.
 *
 * @ingroup Android
 *
 * 调用方先异步取得通知权限，再在主线程启动服务。
 *
 * @param ppArguments Array of arguments to pass to the server on launch.
 * @param NumArguments The number of arguments.
 *
 * @return `true` on success, `false` on error.
 */
bool StartAndroidServer(const char **ppArguments, size_t NumArguments);

// 取消待绑定启动，或停止已启动的本地服务；无需等待Messenger可用。
void StopAndroidServer();

/**
 * Adds a command to the execution queue of the local server.
 *
 * @ingroup Android
 *
 * @param pCommand The command to enqueue.
 */
void ExecuteAndroidServerCommand(const char *pCommand);

/**
 * Returns whether the local server and its Android service are running.
 *
 * @ingroup Android
 *
 * @return `true` if the server is running, `false` if the server is stopped.
 */
bool IsAndroidServerRunning();

// 打开应用内媒体列表，导出与分享由系统 UI 完成；排除仍在录制的文件。
bool ShowAndroidMediaFiles(const char *pDirectory, bool Videos, const char *const *ppExcluded, size_t NumExcluded);

/**
 * Requests the audio record permission on Android.
 *
 * @ingroup Android
 *
 * This will show a permission dialog to the user if the permission is not already granted.
 *
 * 申请结果通过 AndroidAudioRecordPermission 读取，不阻塞主线程。
 */
enum class EAndroidPermissionState
{
	UNKNOWN,
	PENDING,
	GRANTED,
	DENIED,
};

void RequestAndroidAudioRecordPermission();
EAndroidPermissionState AndroidAudioRecordPermission();
void RequestAndroidNotificationPermission();
EAndroidPermissionState AndroidNotificationPermission();

#endif // ANDROID_ANDROID_MAIN_H
