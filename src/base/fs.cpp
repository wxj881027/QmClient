/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <base/dbg.h>
#include <base/fs.h>
#include <base/log.h>
#include <base/secure.h>
#include <base/str.h>
#include <base/types.h>
#include <base/windows.h>

#include <cerrno>
#include <cstring>
#include <iterator>
#include <memory>

#if defined(CONF_FAMILY_UNIX)
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype> // tolower
#include <cstdio> // rename
#include <cstdlib> // getenv
#if defined(CONF_PLATFORM_LINUX)
#include <fcntl.h>
#include <sys/syscall.h>
#endif
#elif defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <io.h>
#include <shlobj.h> // SHGetKnownFolderPath
#include <shlwapi.h>

#include <new>
#include <string>
#include <utility>
#else
#error NOT IMPLEMENTED
#endif

#if defined(CONF_PLATFORM_FREEBSD) || defined(CONF_PLATFORM_NETBSD)
#include <sys/sysctl.h>
#endif

#if defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)
#include <mach-o/dyld.h> // _NSGetExecutablePath
#endif

static bool fs_is_path_separator(char c)
{
#if defined(CONF_FAMILY_WINDOWS)
	return c == '/' || c == '\\';
#else
	return c == '/';
#endif
}

#if defined(CONF_FAMILY_WINDOWS)
static std::optional<std::wstring> fs_windows_path(const char *path)
{
	if(path == nullptr || !str_utf8_check(path))
		return std::nullopt;
	return windows_utf8_to_wide(path);
}

static bool fs_windows_path_not_found(DWORD error)
{
	return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}
#endif

int fs_makedir(const char *path)
{
#if defined(CONF_FAMILY_WINDOWS)
	const auto wide_path = fs_windows_path(path);
	if(!wide_path)
		return -1;
	if(CreateDirectoryW(wide_path->c_str(), nullptr) != 0)
		return 0;

	const DWORD error = GetLastError();
	if(error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
	{
		const DWORD attributes = GetFileAttributesW(wide_path->c_str());
		if(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			return 0;
	}
	log_error("filesystem", "Failed to create folder '%s' (%ld '%s')", path, error, windows_format_system_message(error).c_str());
	return -1;
#else
#if defined(CONF_PLATFORM_HAIKU)
	if(fs_is_dir(path))
	{
		return 0;
	}
#endif
	if(mkdir(path, 0755) == 0)
		return 0;

	const int error = errno;
	if(error == EEXIST && fs_is_dir(path))
		return 0;

	log_error("filesystem", "Failed to create folder '%s' (%d '%s')", path, error, strerror(error));
	return -1;
#endif
}

int fs_makedir_rec_for(const char *path)
{
	if(path == nullptr)
		return -1;
	char buffer[IO_MAX_PATH_LENGTH];
	const size_t length = std::strlen(path);
	if(length >= sizeof(buffer))
		return -1;
	std::memcpy(buffer, path, length + 1);

#if defined(CONF_FAMILY_WINDOWS)
	if(!str_utf8_check(path))
		return -1;

	size_t first = 1;
	if(length >= 2 && fs_is_path_separator(buffer[0]) && fs_is_path_separator(buffer[1]))
	{
		// UNC 根包含服务器和共享名。设备路径需要先跳过命名空间前缀。
		first = 2;
		int root_components = 2;
		if(length >= 4 && (buffer[2] == '?' || buffer[2] == '.') && fs_is_path_separator(buffer[3]))
		{
			first = 4;
			if(length >= 8 && str_startswith_nocase(buffer + 4, "UNC") && fs_is_path_separator(buffer[7]))
				first = 8;
			else
				root_components = 1;
		}
		for(int part = 0; part < root_components; ++part)
		{
			const size_t start = first;
			while(first < length && !fs_is_path_separator(buffer[first]))
				++first;
			if(first == start)
				return -1;
			if(first == length)
				return part + 1 == root_components ? 0 : -1;
			++first;
		}
	}
	else if(length >= 3 && buffer[1] == ':' && fs_is_path_separator(buffer[2]))
		first = 3;
#else
	const size_t first = 1;
#endif
	size_t end = length;
	while(end > first && fs_is_path_separator(buffer[end - 1]))
		--end;
	for(size_t index = first; index < end; ++index)
	{
		const char separator = buffer[index];
		if(!fs_is_path_separator(separator) || fs_is_path_separator(buffer[index - 1]))
			continue;
		buffer[index] = '\0';
		if(fs_makedir(buffer) < 0)
			return -1;
		buffer[index] = separator;
	}
	return 0;
}

int fs_removedir(const char *path)
{
#if defined(CONF_FAMILY_WINDOWS)
	const auto wide_path = fs_windows_path(path);
	if(!wide_path)
		return -1;
	if(RemoveDirectoryW(wide_path->c_str()) != 0)
	{
		return 0;
	}
	const DWORD error = GetLastError();
	if(fs_windows_path_not_found(error))
	{
		return 0;
	}
	log_error("filesystem", "Failed to remove folder '%s' (%ld '%s')", path, error, windows_format_system_message(error).c_str());
	return -1;
#else
	if(rmdir(path) == 0 || errno == ENOENT)
	{
		return 0;
	}
	log_error("filesystem", "Failed to remove folder '%s' (%d '%s')", path, errno, strerror(errno));
	return -1;
#endif
}

#if defined(CONF_FAMILY_WINDOWS)
static inline time_t filetime_to_unixtime(LPFILETIME filetime)
{
	const uint64_t ticks = (static_cast<uint64_t>(filetime->dwHighDateTime) << 32) | filetime->dwLowDateTime;
	const int64_t seconds = static_cast<int64_t>(ticks / 10000000) - 11644473600LL;
	return std::in_range<time_t>(seconds) ? static_cast<time_t>(seconds) : static_cast<time_t>(-1);
}

static std::optional<std::wstring> fs_windows_listdir_pattern(const char *dir, size_t &name_capacity)
{
	if(dir == nullptr)
		return std::nullopt;
	char buffer[IO_MAX_PATH_LENGTH];
	const size_t dir_length = std::strlen(dir);
	const size_t separator_length = dir_length == 0 || !fs_is_path_separator(dir[dir_length - 1]) ? 1 : 0;
	if(dir_length > sizeof(buffer) - separator_length - 2)
		return std::nullopt;
	name_capacity = sizeof(buffer) - dir_length - separator_length;
	str_format(buffer, sizeof(buffer), "%s%s*", dir, separator_length ? "\\" : "");
	return fs_windows_path(buffer);
}
#endif

static EFsListDirectoryResult fs_listdir_impl(const char *dir, FS_LISTDIR_CALLBACK cb, int type, void *user, bool checked)
{
#if defined(CONF_FAMILY_WINDOWS)
	size_t name_capacity = 0;
	const auto wide_pattern = fs_windows_listdir_pattern(dir, name_capacity);
	if(!wide_pattern)
		return EFsListDirectoryResult::IO_FAILURE;

	WIN32_FIND_DATAW finddata;
	HANDLE handle = FindFirstFileExW(wide_pattern->c_str(), FindExInfoBasic, &finddata, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
	if(handle == INVALID_HANDLE_VALUE)
	{
		if(!checked)
			return EFsListDirectoryResult::IO_FAILURE;
		const DWORD error = GetLastError();
		if(!fs_windows_path_not_found(error))
			return EFsListDirectoryResult::IO_FAILURE;
		const std::wstring wide_dir = windows_utf8_to_wide(dir);
		const DWORD attributes = GetFileAttributesW(wide_dir.c_str());
		if(attributes != INVALID_FILE_ATTRIBUTES)
			return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? EFsListDirectoryResult::SUCCESS : EFsListDirectoryResult::IO_FAILURE;
		const DWORD attribute_error = GetLastError();
		return fs_windows_path_not_found(attribute_error) ? EFsListDirectoryResult::NOT_FOUND : EFsListDirectoryResult::IO_FAILURE;
	}
	const std::unique_ptr<void, decltype(&FindClose)> owned_handle(handle, FindClose);

	while(true)
	{
		const std::optional<std::string> current_entry = windows_wide_to_utf8(finddata.cFileName);
		if(!current_entry.has_value())
			log_error("filesystem", "ERROR: file/folder name containing invalid UTF-16 found in folder '%s'", dir);
		else if(current_entry->size() >= name_capacity)
		{
			if(checked)
				return EFsListDirectoryResult::IO_FAILURE;
		}
		else if(cb(current_entry->c_str(), (finddata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, type, user))
			return EFsListDirectoryResult::STOPPED;
		if(!FindNextFileW(owned_handle.get(), &finddata))
			return !checked || GetLastError() == ERROR_NO_MORE_FILES ? EFsListDirectoryResult::SUCCESS : EFsListDirectoryResult::IO_FAILURE;
	}
#else
	char buffer[IO_MAX_PATH_LENGTH];
	if(std::strlen(dir) > sizeof(buffer) - 2)
		return EFsListDirectoryResult::IO_FAILURE;
	const std::unique_ptr<DIR, decltype(&closedir)> dir_handle(opendir(dir), closedir);
	if(dir_handle == nullptr)
		return checked && errno == ENOENT ? EFsListDirectoryResult::NOT_FOUND : EFsListDirectoryResult::IO_FAILURE;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	const size_t length = str_length(buffer);
	while(true)
	{
		// 回调和stat也会设置errno，不能把它们残留的值当作readdir错误。
		if(checked)
			errno = 0;
		struct dirent *entry = readdir(dir_handle.get());
		if(entry == nullptr)
			return !checked || errno == 0 ? EFsListDirectoryResult::SUCCESS : EFsListDirectoryResult::IO_FAILURE;
		if(!str_utf8_check(entry->d_name))
		{
			log_error("filesystem", "ERROR: file/folder name containing invalid UTF-8 found in folder '%s'", dir);
			continue;
		}
		if(std::strlen(entry->d_name) >= sizeof(buffer) - length)
		{
			if(checked)
				return EFsListDirectoryResult::IO_FAILURE;
			continue;
		}
		str_copy(buffer + length, entry->d_name, sizeof(buffer) - length);
		bool is_dir;
		if(checked)
		{
			struct stat info;
			if(stat(buffer, &info) == 0)
				is_dir = S_ISDIR(info.st_mode);
			else if(errno == ENOENT || errno == ENOTDIR)
				is_dir = false; // 消失的条目/断链仍交给回调，保留存储根名称遮蔽规则。
			else
				return EFsListDirectoryResult::IO_FAILURE;
		}
		else
			is_dir = fs_is_dir(buffer);
		if(cb(entry->d_name, is_dir, type, user))
			return EFsListDirectoryResult::STOPPED;
	}
#endif
}

void fs_listdir(const char *dir, FS_LISTDIR_CALLBACK cb, int type, void *user)
{
	(void)fs_listdir_impl(dir, cb, type, user, false);
}

EFsListDirectoryResult fs_listdir_checked(const char *dir, FS_LISTDIR_CALLBACK cb, int type, void *user)
{
	return fs_listdir_impl(dir, cb, type, user, true);
}

void fs_listdir_fileinfo(const char *dir, FS_LISTDIR_CALLBACK_FILEINFO cb, int type, void *user)
{
#if defined(CONF_FAMILY_WINDOWS)
	size_t name_capacity = 0;
	const auto wide_pattern = fs_windows_listdir_pattern(dir, name_capacity);
	if(!wide_pattern)
		return;

	WIN32_FIND_DATAW finddata;
	HANDLE handle = FindFirstFileExW(wide_pattern->c_str(), FindExInfoBasic, &finddata, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
	if(handle == INVALID_HANDLE_VALUE)
		return;
	const std::unique_ptr<void, decltype(&FindClose)> owned_handle(handle, FindClose);

	do
	{
		const std::optional<std::string> current_entry = windows_wide_to_utf8(finddata.cFileName);
		if(!current_entry.has_value())
		{
			log_error("filesystem", "ERROR: file/folder name containing invalid UTF-16 found in folder '%s'", dir);
			continue;
		}

		if(current_entry->size() >= name_capacity)
			continue;

		CFsFileInfo info;
		info.m_pName = current_entry->c_str();
		info.m_TimeCreated = filetime_to_unixtime(&finddata.ftCreationTime);
		info.m_TimeModified = filetime_to_unixtime(&finddata.ftLastWriteTime);

		if(cb(&info, (finddata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, type, user))
			break;
	} while(FindNextFileW(owned_handle.get(), &finddata));

#else
	char buffer[IO_MAX_PATH_LENGTH];
	if(std::strlen(dir) > sizeof(buffer) - 2)
		return;
	const std::unique_ptr<DIR, decltype(&closedir)> dir_handle(opendir(dir), closedir);
	if(dir_handle == nullptr)
		return;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	const size_t length = str_length(buffer);

	while(true)
	{
		struct dirent *entry = readdir(dir_handle.get());
		if(entry == nullptr)
			break;
		if(!str_utf8_check(entry->d_name))
		{
			log_error("filesystem", "ERROR: file/folder name containing invalid UTF-8 found in folder '%s'", dir);
			continue;
		}
		if(std::strlen(entry->d_name) >= sizeof(buffer) - length)
			continue;
		str_copy(buffer + length, entry->d_name, sizeof(buffer) - length);
		time_t created = -1, modified = -1;
		fs_file_time(buffer, &created, &modified);

		CFsFileInfo info;
		info.m_pName = entry->d_name;
		info.m_TimeCreated = created;
		info.m_TimeModified = modified;

		if(cb(&info, fs_is_dir(buffer), type, user))
			break;
	}

#endif
}

int fs_chdir(const char *path)
{
#if defined(CONF_FAMILY_WINDOWS)
	const auto wide_path = fs_windows_path(path);
	return wide_path && SetCurrentDirectoryW(wide_path->c_str()) != 0 ? 0 : 1;
#else
	return chdir(path) ? 1 : 0;
#endif
}

char *fs_getcwd(char *buffer, int buffer_size)
{
	if(buffer == nullptr || buffer_size <= 0)
		return nullptr;
#if defined(CONF_FAMILY_WINDOWS)
	buffer[0] = '\0';
	const DWORD size_needed = GetCurrentDirectoryW(0, nullptr);
	if(size_needed == 0)
		return nullptr;
	const std::unique_ptr<wchar_t[]> wide_current_dir(new(std::nothrow) wchar_t[size_needed]);
	if(!wide_current_dir)
		return nullptr;

	const DWORD length = GetCurrentDirectoryW(size_needed, wide_current_dir.get());
	if(length == 0 || length >= size_needed)
		return nullptr;
	auto current_dir = windows_wide_to_utf8_bounded(wide_current_dir.get(), wide_current_dir.get() + length + 1);
	if(!current_dir)
		return nullptr;
	// C: 表示该盘的当前目录，保留 C:/ 的根目录含义。
	if(current_dir->size() == 3 && (*current_dir)[1] == ':' && (*current_dir)[2] == '\\')
		(*current_dir)[2] = '/';
	else
		fs_normalize_path(current_dir->data());
	if(str_length(current_dir->c_str()) >= buffer_size)
		return nullptr;

	str_copy(buffer, current_dir->c_str(), buffer_size);
	return buffer;
#else
	char *result = getcwd(buffer, buffer_size);
	if(result == nullptr || !str_utf8_check(result))
	{
		buffer[0] = '\0';
		return nullptr;
	}
	return result;
#endif
}

int fs_storage_path(const char *appname, char *path, int max)
{
	if(path == nullptr || max <= 0)
		return -1;
	path[0] = '\0';

	const char *root;
	const char *separator;
#if defined(CONF_FAMILY_WINDOWS)
	WCHAR *wide_home = nullptr;
	const HRESULT result = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, /* current user */ nullptr, &wide_home);
	const std::unique_ptr<WCHAR, decltype(&CoTaskMemFree)> home_owner(wide_home, CoTaskMemFree);
	if(result != S_OK)
	{
		log_error("filesystem", "ERROR: could not determine location of Roaming/AppData folder");
		return -1;
	}
	std::optional<std::string> home = windows_wide_to_utf8(home_owner.get());
	if(!home)
	{
		log_error("filesystem", "ERROR: path of Roaming/AppData folder contains invalid UTF-16");
		return -1;
	}
	fs_normalize_path(home->data());
	root = home->c_str();
	separator = "/";
#else
	root = getenv("HOME");
	const char *root_variable = "HOME";
#if defined(CONF_PLATFORM_HAIKU)
	separator = "/config/settings/";
#elif defined(CONF_PLATFORM_IOS)
	separator = "/Documents";
	appname = "";
#elif defined(CONF_PLATFORM_MACOS)
	separator = "/Library/Application Support/";
#else
	if(str_comp(appname, "Teeworlds") == 0)
	{
		// 旧 Teeworlds 目录仍优先从 HOME 中定位。
		separator = "/.";
	}
	else if(const char *data_home = getenv("XDG_DATA_HOME"); data_home && data_home[0] == '/')
	{
		root = data_home;
		root_variable = "XDG_DATA_HOME";
		separator = "/";
	}
	else
	{
		separator = "/.local/share/";
	}
#endif
	if(!root || root[0] != '/')
		return -1;
	if(!str_utf8_check(root))
	{
		log_error("filesystem", "ERROR: the %s environment variable contains invalid UTF-8", root_variable);
		return -1;
	}
#endif

	const size_t prefix_length = std::strlen(root) + std::strlen(separator);
	if(prefix_length + std::strlen(appname) >= static_cast<size_t>(max))
		return -1;

	str_format(path, max, "%s%s%s", root, separator, appname);
#if defined(CONF_FAMILY_UNIX) && !defined(CONF_PLATFORM_HAIKU) && !defined(CONF_PLATFORM_IOS) && !defined(CONF_PLATFORM_MACOS)
	for(char *p = path + prefix_length; *p; ++p)
		*p = tolower(static_cast<unsigned char>(*p));
#endif
	return 0;
}

int fs_executable_path(char *buffer, int buffer_size)
{
	if(buffer == nullptr || buffer_size <= 0)
		return -1;
	// https://stackoverflow.com/a/1024937
#if defined(CONF_FAMILY_WINDOWS)
	buffer[0] = '\0';
	wchar_t wide_path[IO_MAX_PATH_LENGTH];
	const DWORD length = GetModuleFileNameW(nullptr, wide_path, std::size(wide_path));
	if(length == 0 || length >= std::size(wide_path))
		return -1;
	const auto path = windows_wide_to_utf8_bounded(wide_path, wide_path + length + 1);
	if(!path || path->size() >= static_cast<size_t>(buffer_size))
		return -1;
	str_copy(buffer, path->c_str(), buffer_size);
	return 0;
#elif defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)
	uint32_t path_size = buffer_size;
	if(_NSGetExecutablePath(buffer, &path_size) != 0 || !str_utf8_check(buffer))
	{
		buffer[0] = '\0';
		return -1;
	}
	return 0;
#else
	char path[IO_MAX_PATH_LENGTH];
#if defined(CONF_PLATFORM_FREEBSD) || (defined(CONF_PLATFORM_NETBSD) && defined(KERN_PROC_PATHNAME))
#if defined(CONF_PLATFORM_FREEBSD)
	int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
#else
	int mib[] = {CTL_KERN, KERN_PROC_ARGS, static_cast<int>(getpid()), KERN_PROC_PATHNAME};
#endif
	size_t path_size = sizeof(path);
	if(sysctl(mib, std::size(mib), path, &path_size, nullptr, 0) == 0 &&
		path_size > 1 && path_size <= sizeof(path) && path[path_size - 1] == '\0' &&
		path[0] == '/' && str_utf8_check(path))
	{
		if(path_size <= static_cast<size_t>(buffer_size))
		{
			str_copy(buffer, path, buffer_size);
			return 0;
		}
		buffer[0] = '\0';
		return -1;
	}
#endif
	static const char *NAMES[] = {
		"/proc/self/exe", // Linux, Android
		"/proc/curproc/exe", // NetBSD
		"/proc/curproc/file", // DragonFly
	};
	for(auto &name : NAMES)
	{
		if(const ssize_t bytes_written = readlink(name, path, sizeof(path)); bytes_written >= 0 && static_cast<size_t>(bytes_written) < sizeof(path))
		{
			path[bytes_written] = '\0'; // readlink does NOT null-terminate
			// if the file gets deleted or replaced (not renamed) linux appends (deleted) to the symlink (see https://man7.org/linux/man-pages/man5/proc_pid_exe.5.html)
			if(const char *deleted = str_endswith(path, " (deleted)"); deleted != nullptr)
			{
				path[deleted - path] = '\0';
			}
			if(std::strlen(path) >= static_cast<size_t>(buffer_size) || !str_utf8_check(path))
				break;
			str_copy(buffer, path, buffer_size);
			return 0;
		}
	}
	buffer[0] = '\0';
	return -1;
#endif
}

int fs_is_file(const char *path)
{
#if defined(CONF_FAMILY_WINDOWS)
	const auto wide_path = fs_windows_path(path);
	if(!wide_path)
		return 0;
	const DWORD attributes = GetFileAttributesW(wide_path->c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
#else
	struct stat sb;
	if(stat(path, &sb) == -1)
		return 0;
	return S_ISREG(sb.st_mode) ? 1 : 0;
#endif
}

int fs_is_dir(const char *path)
{
#if defined(CONF_FAMILY_WINDOWS)
	const auto wide_path = fs_windows_path(path);
	if(!wide_path)
		return 0;
	const DWORD attributes = GetFileAttributesW(wide_path->c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
#else
	struct stat sb;
	if(stat(path, &sb) == -1)
		return 0;
	return S_ISDIR(sb.st_mode) ? 1 : 0;
#endif
}

int fs_is_relative_path(const char *path)
{
#if defined(CONF_FAMILY_WINDOWS)
	const auto wide_path = fs_windows_path(path);
	return !wide_path || PathIsRelativeW(wide_path->c_str()) ? 1 : 0;
#else
	return path[0] == '/' ? 0 : 1; // yes, it's that simple
#endif
}

const char *fs_filename(const char *path)
{
	const char *filename = path + str_length(path);
	while(filename != path)
	{
		--filename;
		if(filename[0] == '/' || filename[0] == '\\')
			return filename + 1;
	}
	return path;
}

void fs_split_file_extension(const char *filename, char *name, size_t name_size, char *extension, size_t extension_size)
{
	dbg_assert(name != nullptr || extension != nullptr, "name or extension parameter required");
	dbg_assert(name == nullptr || name_size > 0, "name_size invalid");
	dbg_assert(extension == nullptr || extension_size > 0, "extension_size invalid");

	const char *last_dot = str_rchr(filename, '.');
	if(last_dot == nullptr || last_dot == filename)
	{
		if(extension != nullptr)
			extension[0] = '\0';
		if(name != nullptr)
			str_copy(name, filename, name_size);
	}
	else
	{
		if(extension != nullptr)
			str_copy(extension, last_dot + 1, extension_size);
		if(name != nullptr)
			str_truncate(name, name_size, filename, last_dot - filename);
	}
}

int fs_parent_dir(char *path)
{
	char *parent = nullptr;
	for(; *path; ++path)
	{
		if(*path == '/' || *path == '\\')
			parent = path;
	}

	if(parent)
	{
		*parent = 0;
		return 0;
	}
	return 1;
}

void fs_normalize_path(char *path)
{
	for(int i = 0; path[i] != '\0';)
	{
		if(path[i] == '\\')
		{
			path[i] = '/';
		}
		if(i > 0 && path[i] == '/' && path[i + 1] == '\0')
		{
			path[i] = '\0';
			--i;
		}
		else
		{
			++i;
		}
	}
}

#if defined(CONF_FAMILY_WINDOWS)
static DWORD fs_windows_move_to_temporary_sibling(const std::wstring &wide_filename, const char *suffix, std::wstring &temporary)
{
	size_t parent_length = wide_filename.find_last_of(L"/\\");
	if(parent_length == std::wstring::npos)
		parent_length = wide_filename.size() >= 2 && wide_filename[1] == L':' ? 2 : 0;
	else
		++parent_length;

	unsigned random_num;
	secure_random_fill(&random_num, sizeof(random_num));
	temporary.assign(wide_filename, 0, parent_length);
	constexpr unsigned MAX_ATTEMPTS = 16;
	for(unsigned attempt = 0; attempt < MAX_ATTEMPTS; ++attempt)
	{
		// 同目录的短文件名避免超出单个文件名的长度上限。
		char name[32];
		str_format(name, sizeof(name), ".%08X.%s", random_num + attempt, suffix);
		temporary.resize(parent_length);
		temporary += windows_utf8_to_wide(name);
		if(CompareStringOrdinal(wide_filename.c_str(), -1, temporary.c_str(), -1, TRUE) == CSTR_EQUAL)
			continue;
		if(MoveFileExW(wide_filename.c_str(), temporary.c_str(), MOVEFILE_WRITE_THROUGH) != 0)
			return ERROR_SUCCESS;
		const DWORD error = GetLastError();
		if(error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
			return error;
	}
	return ERROR_ALREADY_EXISTS;
}

static bool fs_windows_delete_temporary_file(const std::wstring &wide_filename)
{
	if(DeleteFileW(wide_filename.c_str()) != 0)
		return true;
	const DWORD error = GetLastError();
	if(fs_windows_path_not_found(error))
		return true;
	const auto filename = windows_wide_to_utf8(wide_filename.c_str());
	log_error("filesystem", "Failed to remove file '%s' (%ld '%s')", filename ? filename->c_str() : "(invalid filename)", error, windows_format_system_message(error).c_str());
	return false;
}

static void fs_windows_restore_temporary_file(const std::wstring &backup, const std::wstring &destination, const char *filename)
{
	// 原路径可能已被另一写入者占用，恢复时不得覆盖它。
	if(MoveFileExW(backup.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != 0)
		return;
	const DWORD error = GetLastError();
	const auto filename_backup = windows_wide_to_utf8(backup.c_str());
	log_error("filesystem", "Failed to restore '%s' from '%s' (%ld '%s')", filename, filename_backup ? filename_backup->c_str() : "(invalid filename)", error, windows_format_system_message(error).c_str());
}
#endif

int fs_remove(const char *filename)
{
#if defined(CONF_FAMILY_WINDOWS)
	const auto wide_filename = fs_windows_path(filename);
	if(!wide_filename)
		return 1;
	const DWORD attributes = GetFileAttributesW(wide_filename->c_str());
	if(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		return 1;

	// 先移出原路径。若打开的句柄推迟实际删除，原文件名也可立即复用。
	std::wstring wide_filename_temp;
	const DWORD error = fs_windows_move_to_temporary_sibling(*wide_filename, "toberemoved", wide_filename_temp);
	if(error != ERROR_SUCCESS)
	{
		if(fs_windows_path_not_found(error))
		{
			const DWORD current_attributes = GetFileAttributesW(wide_filename->c_str());
			if(current_attributes == INVALID_FILE_ATTRIBUTES && fs_windows_path_not_found(GetLastError()))
				return 0;
		}
		const auto filename_temp = windows_wide_to_utf8(wide_filename_temp.c_str());
		log_error("filesystem", "Failed to rename file '%s' to '%s' for removal (%ld '%s')", filename, filename_temp ? filename_temp->c_str() : "(invalid filename)", error, windows_format_system_message(error).c_str());
		return 1;
	}
	if(fs_windows_delete_temporary_file(wide_filename_temp))
		return 0;
	fs_windows_restore_temporary_file(wide_filename_temp, *wide_filename, filename);
	return 1;
#else
	if(unlink(filename) == 0 || errno == ENOENT)
	{
		return 0;
	}
	log_error("filesystem", "Failed to remove file '%s' (%d '%s')", filename, errno, strerror(errno));
	return 1;
#endif
}

int fs_rename(const char *oldname, const char *newname, bool allow_destination_removal)
{
#if defined(CONF_FAMILY_WINDOWS)
	const auto wide_oldname = fs_windows_path(oldname);
	const auto wide_newname = fs_windows_path(newname);
	if(!wide_oldname || !wide_newname)
		return 1;
	constexpr DWORD MOVE_FLAGS = MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH;
	if(MoveFileExW(wide_oldname->c_str(), wide_newname->c_str(), MOVE_FLAGS | MOVEFILE_REPLACE_EXISTING) != 0)
	{
		return 0;
	}

	DWORD error = GetLastError();
	if(!allow_destination_removal && (error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION))
	{
		// 配置和缓存类写入只等待瞬时句柄释放，再尝试原地替换。
		// 杀毒扫描、索引器或另一个实例刚读完文件，不应让整次保存失败。
		constexpr int RETRY_COUNT = 10;
		constexpr DWORD RETRY_DELAY_MS = 20;
		for(int Attempt = 0; Attempt < RETRY_COUNT; ++Attempt)
		{
			Sleep(RETRY_DELAY_MS);
			if(MoveFileExW(wide_oldname->c_str(), wide_newname->c_str(), MOVE_FLAGS | MOVEFILE_REPLACE_EXISTING) != 0)
				return 0;
			error = GetLastError();
			if(error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION)
				break;
		}
	}
	else if(allow_destination_removal && error == ERROR_ACCESS_DENIED)
	{
		// 目标文件有打开句柄时，先在同目录暂存旧文件，等替换成功后再删除。
		std::wstring backup;
		const DWORD attributes = GetFileAttributesW(wide_newname->c_str());
		if(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
			fs_windows_move_to_temporary_sibling(*wide_newname, "rename-backup", backup) != ERROR_SUCCESS)
			backup.clear();

		// 暂存期间如果有其他写入者发布到目标名，保留对方写入和我们的备份。
		const DWORD retry_flags = backup.empty() ? MOVE_FLAGS | MOVEFILE_REPLACE_EXISTING : MOVE_FLAGS;
		if(MoveFileExW(wide_oldname->c_str(), wide_newname->c_str(), retry_flags) != 0)
		{
			if(!backup.empty())
				(void)fs_windows_delete_temporary_file(backup);
			return 0;
		}
		error = GetLastError();
		if(!backup.empty())
			fs_windows_restore_temporary_file(backup, *wide_newname, newname);
	}
	log_error("filesystem", "Failed to rename file '%s' to '%s' (%ld '%s')", oldname, newname, error, windows_format_system_message(error).c_str());
	return 1;
#else
	(void)allow_destination_removal;
	if(rename(oldname, newname) == 0)
	{
		return 0;
	}
	log_error("filesystem", "Failed to rename file '%s' to '%s' (%d '%s')", oldname, newname, errno, strerror(errno));
	return 1;
#endif
}

int fs_move_file_no_replace(const char *oldname, const char *newname)
{
#if defined(CONF_FAMILY_WINDOWS)
	const auto wide_oldname = fs_windows_path(oldname);
	const auto wide_newname = fs_windows_path(newname);
	if(!wide_oldname || !wide_newname)
		return 1;
	return MoveFileExW(wide_oldname->c_str(), wide_newname->c_str(), MOVEFILE_WRITE_THROUGH) == 0;
#else
#if defined(CONF_PLATFORM_LINUX) && defined(SYS_renameat2)
	return syscall(SYS_renameat2, AT_FDCWD, oldname, AT_FDCWD, newname, 1 /* RENAME_NOREPLACE */) != 0;
#elif defined(CONF_PLATFORM_MACOS) && defined(RENAME_EXCL)
	return renamex_np(oldname, newname, RENAME_EXCL) != 0;
#else
	(void)oldname;
	(void)newname;
	return 1;
#endif
#endif
}

int fs_file_time(const char *name, time_t *created, time_t *modified)
{
#if defined(CONF_FAMILY_WINDOWS)
	WIN32_FILE_ATTRIBUTE_DATA attribute_data;
	const auto wide_name = fs_windows_path(name);
	if(!wide_name || !GetFileAttributesExW(wide_name->c_str(), GetFileExInfoStandard, &attribute_data))
		return 1;

	*created = filetime_to_unixtime(&attribute_data.ftCreationTime);
	*modified = filetime_to_unixtime(&attribute_data.ftLastWriteTime);
#elif defined(CONF_FAMILY_UNIX)
	struct stat sb;
	if(stat(name, &sb))
		return 1;

	*created = sb.st_ctime;
	*modified = sb.st_mtime;
#else
#error not implemented
#endif

	return 0;
}
