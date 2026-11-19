/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#ifndef BASE_FS_H
#define BASE_FS_H

#include "types.h"

#include <cstddef> // size_t
#include <ctime> // time_t

/**
 * Utilities for accessing the file system.
 *
 * @defgroup Filesystem Filesystem
 */

/**
 * Creates a directory.
 *
 * @ingroup Filesystem
 *
 * @param path Directory to create.
 *
 * @return `0` on success. Negative value on failure.
 *
 * @remark Does not create several directories if needed. "a/b/c" will
 *         result in a failure if b or a does not exist.
 *
 * @remark The strings are treated as null-terminated strings.
 */
[[nodiscard]] int fs_makedir(const char *path);

/**
 * Recursively creates parent directories for a file or directory.
 *
 * @ingroup Filesystem
 *
 * @param path File or directory for which to create parent directories.
 *
 * @return `0` on success. Negative value on failure.
 * @remark The complete path must fit in `IO_MAX_PATH_LENGTH` bytes including its terminator.
 * @remark 创建失败时，已经创建的父目录会保留。
 *
 * @remark The strings are treated as null-terminated strings.
 */
int fs_makedir_rec_for(const char *path);

/**
 * Removes a directory.
 *
 * @ingroup Filesystem
 *
 * @param path Directory to remove.
 *
 * @return `0` on success. Negative value on failure.
 *
 * @remark Cannot remove a non-empty directory.
 *
 * @remark The strings are treated as null-terminated strings.
 */
[[nodiscard]] int fs_removedir(const char *path);

/**
 * Lists the files and folders in a directory.
 *
 * @ingroup Filesystem
 *
 * @param dir Directory to list.
 * @param cb Callback function to call for each entry.
 * @param type Type of the directory.
 * @param user Pointer to give to the callback.
 *
 * @remark Paths that do not fit in IO_MAX_PATH_LENGTH bytes including the terminator are skipped.
 * @remark 回调返回非零时停止枚举。名称仅在本次回调期间有效，后续使用时须复制。
 * @remark The strings are treated as null-terminated strings.
 */
void fs_listdir(const char *dir, FS_LISTDIR_CALLBACK cb, int type, void *user);

enum class EFsListDirectoryResult
{
	SUCCESS,
	NOT_FOUND,
	STOPPED,
	IO_FAILURE,
	UNSUPPORTED,
};

/**
 * 枚举目录并返回状态。回调参数的寿命和停止条件与 @ref fs_listdir 相同。
 *
 * @ingroup Filesystem
 *
 * @remark 完整路径超出容量时返回 `IO_FAILURE`。编码无效的条目名称会跳过。
 * @remark 返回 `IO_FAILURE` 前可能已调用回调。调用方负责处理已收集的结果。
 * @remark 返回或传播异常时关闭目录句柄。`UNSUPPORTED` 供未实现该接口的存储后端使用。
 */
EFsListDirectoryResult fs_listdir_checked(const char *dir, FS_LISTDIR_CALLBACK cb, int type, void *user);

/**
 * Lists the files and folders in a directory and gets additional file information.
 *
 * @ingroup Filesystem
 *
 * @param dir Directory to list.
 * @param cb Callback function to call for each entry.
 * @param type Type of the directory.
 * @param user Pointer to give to the callback.
 *
 * @remark Paths that do not fit in IO_MAX_PATH_LENGTH bytes including the terminator are skipped.
 * @remark 回调返回非零时停止枚举。`info` 和 `info->m_pName` 仅在本次回调期间有效。
 * @remark Unix 下 `m_TimeCreated` 表示文件状态变更时间。读取时间失败时两个时间字段均为 `-1`。
 * @remark The strings are treated as null-terminated strings.
 */
void fs_listdir_fileinfo(const char *dir, FS_LISTDIR_CALLBACK_FILEINFO cb, int type, void *user);

/**
 * Changes the current working directory.
 *
 * @ingroup Filesystem
 *
 * @param path New working directory path.
 *
 * @return `0` on success. `1` on failure.
 *
 * @remark The strings are treated as null-terminated strings.
 */
int fs_chdir(const char *path);

/**
 * Gets the current working directory.
 *
 * @ingroup Filesystem
 *
 * @param buffer Buffer that will receive the current working directory.
 * @param buffer_size Size of the buffer.
 *
 * @return Pointer to the buffer on success, `nullptr` on failure.
 *
 * @remark The output buffer must fit the complete path and its terminator.
 * @remark 返回 `nullptr` 时，若 `buffer` 非空且容量大于零，输出会被置为空字符串。
 * @remark The strings are treated as null-terminated strings.
 */
char *fs_getcwd(char *buffer, int buffer_size);

/**
 * Fetches per user configuration directory.
 *
 * @ingroup Filesystem
 *
 * @param appname Name of the application.
 * @param path Buffer that will receive the storage path.
 * @param max Size of the buffer.
 *
 * @return `0` on success. Negative value on failure.
 *
 * @remark The output buffer must fit the complete path and its terminator.
 * @remark On failure, a writable output buffer is set to an empty string.
 * @remark Linux 使用 `XDG_DATA_HOME` 指定的数据根目录，默认使用 `$HOME/.local/share`。
 *         应用名转为小写。旧版 Teeworlds 使用 `$HOME/.teeworlds`。
 * @remark macOS 使用 `~/Library/Application Support/appname`。
 * @remark Windows 使用漫游 AppData 下的 `appname` 目录。
 *
 * @remark The strings are treated as null-terminated strings.
 */
int fs_storage_path(const char *appname, char *path, int max);

/**
 * Gets the absolute path to the executable.
 *
 * @ingroup Filesystem
 *
 * @param buffer Buffer that will receive the path of the executable.
 * @param buffer_size Size of the buffer.
 *
 * @return `0` on success. Negative value on failure.
 *
 * @remark The executable name is included in the result.
 * @remark The output buffer must fit the complete path and its terminator.
 * @remark 返回失败时，若 `buffer` 非空且容量大于零，输出会被置为空字符串。
 *
 * @remark The strings are treated as null-terminated strings.
 */
int fs_executable_path(char *buffer, int buffer_size);

/**
 * Checks if a file exists.
 *
 * @ingroup Filesystem
 *
 * @param path The path to check.
 *
 * @return `1` if a file with the given path exists.
 * @return `0` on failure or if the file does not exist.
 *
 * @remark The strings are treated as null-terminated strings.
 */
int fs_is_file(const char *path);

/**
 * Checks if a folder exists.
 *
 * @ingroup Filesystem
 *
 * @param path The path to check.
 *
 * @return `1` if a folder with the given path exists.
 * @return `0` on failure or if the folder does not exist.
 *
 * @remark The strings are treated as null-terminated strings.
 */
int fs_is_dir(const char *path);

/**
 * Checks whether a given path is relative or absolute.
 *
 * @ingroup Filesystem
 *
 * @param path Path to check.
 *
 * @return `1` if relative, `0` if absolute.
 *
 * @remark The strings are treated as null-terminated strings.
 */
int fs_is_relative_path(const char *path);

/**
 * Gets the name of a file or folder specified by a path,
 * i.e. the last segment of the path.
 *
 * @ingroup Filesystem
 *
 * @param path Path from which to retrieve the filename.
 *
 * @return 指向 `path` 内最后一段的指针，其寿命取决于 `path` 的缓冲区。
 *
 * @remark Supports forward and backward slashes as path segment separator.
 * @remark No distinction between files and folders is being made.
 * @remark The strings are treated as null-terminated strings.
 */
const char *fs_filename(const char *path);

/**
 * Splits a filename into name (without extension) and file extension.
 *
 * @ingroup Filesystem
 *
 * @param filename The filename to split.
 * @param name Buffer that will receive the name without extension, may be nullptr.
 * @param name_size Size of the name buffer (ignored if name is nullptr).
 * @param extension Buffer that will receive the extension, may be nullptr.
 * @param extension_size Size of the extension buffer (ignored if extension is nullptr).
 *
 * @remark 至少提供一个输出缓冲区。提供缓冲区时，容量至少为 1 字节。
 * @remark Does NOT handle forward and backward slashes.
 * @remark No distinction between files and folders is being made.
 * @remark The strings are treated as null-terminated strings.
 */
void fs_split_file_extension(const char *filename, char *name, size_t name_size, char *extension = nullptr, size_t extension_size = 0);

/**
 * Get the parent directory of a directory.
 *
 * @ingroup Filesystem
 *
 * @param path Path of the directory. The parent will be store in this buffer as well.
 *
 * @return `0` on success. `1` on failure.
 *
 * @remark The strings are treated as null-terminated strings.
 */
int fs_parent_dir(char *path);

/**
 * Normalizes the given path: replaces backslashes with regular slashes
 * and removes trailing slashes.
 *
 * @ingroup Filesystem
 *
 * @param path Path to normalize.
 *
 * @remark The strings are treated as null-terminated strings.
 */
void fs_normalize_path(char *path);

/**
 * Deletes a file.
 *
 * @ingroup Filesystem
 *
 * @param filename Path of the file to delete.
 *
 * @return `0` on success. `1` on failure.
 *
 * @remark The strings are treated as null-terminated strings.
 * @remark Returns an error if the path specifies a directory name.
 * @remark Windows 下，移到临时路径后若删除失败，会尝试恢复原名并报告失败。
 */
int fs_remove(const char *filename);

/**
 * Renames the file or directory. If the paths differ the file will be moved.
 *
 * @ingroup Filesystem
 *
 * @param oldname The current path of a file or directory.
 * @param newname The new path for the file or directory.
 * @param allow_destination_removal Windows 直接替换遇到访问拒绝时，是否允许先在同目录暂存旧目标。
 *
 * @return `0` on success. `1` on failure.
 *
 * @remark Windows 下传 `false` 时，遇到访问拒绝或共享冲突只会重试直接替换。
 * @remark Windows 暂存后若替换失败，会尝试还原旧目标。还原失败时保留备份并记录路径。
 * @remark The strings are treated as null-terminated strings.
 */
int fs_rename(const char *oldname, const char *newname, bool allow_destination_removal = true);

/**
 * 取得文件或目录的时间戳。
 *
 * @ingroup Filesystem
 *
 * @param name Path of a file or directory.
 * @param created 存放 Windows 创建时间或 Unix 文件状态变更时间的可写指针。
 * @param modified 存放最后修改时间的可写指针。
 *
 * @return 成功返回 `0`。失败时返回非零，两个输出值保持原样。
 *
 * @remark The strings are treated as null-terminated strings.
 * @remark Returned time is in seconds since UNIX Epoch.
 */
int fs_file_time(const char *name, time_t *created, time_t *modified);

#endif
