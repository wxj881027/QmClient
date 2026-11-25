#ifndef BASE_FS_PATH_H
#define BASE_FS_PATH_H

#include <filesystem>
#include <string_view>

/**
 * Converts a UTF-8 byte range to a `std::filesystem::path`.
 *
 * @ingroup Filesystem
 *
 * @param Utf8 UTF-8 encoded bytes. Not required to be null-terminated.
 *
 * @return Path that interprets the bytes as UTF-8 on every platform.
 *
 * @remark Replaces `std::filesystem::u8path`, which is deprecated in C++20 and
 *         removed in C++23: a `std::filesystem::path` built from a narrow
 *         string uses the native encoding instead (ANSI code page on Windows),
 *         so the UTF-8 bytes have to be passed as `char8_t` explicitly to keep
 *         the old behaviour for non-ASCII names.
 */
inline std::filesystem::path fs_path_from_utf8(std::string_view Utf8)
{
	return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t *>(Utf8.data()), Utf8.size()));
}

#endif
