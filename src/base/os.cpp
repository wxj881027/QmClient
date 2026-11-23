#include "os.h"

#include "fs.h"
#include "io.h"
#include "mem.h"
#include "os_locale.h"
#include "str.h"
#include "windows.h"

#include <cstdlib>
#include <iterator> // std::size
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>

#if defined(CONF_FAMILY_UNIX)
#if !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_EMSCRIPTEN)
#include "thread.h"

#include <sys/wait.h>

#include <cerrno>
#include <future>
#endif

#include <sys/utsname.h> // uname, utsname
#include <unistd.h> // execlp, fork

#if defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)
#include <CoreFoundation/CoreFoundation.h>
#endif
#elif defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <objbase.h> // required for shellapi.h
#include <shellapi.h> // ShellExecuteExW

#include <cfenv> // fesetenv
#else
#error NOT IMPLEMENTED
#endif

bool cmdline_fix(int *argc, const char ***argv)
{
#if defined(CONF_FAMILY_WINDOWS)
	int wide_argc = 0;
	const std::unique_ptr<WCHAR *, decltype(&LocalFree)> wide_argv(CommandLineToArgvW(GetCommandLineW(), &wide_argc), LocalFree);
	if(!wide_argv || wide_argc <= 0)
		return false;

	int total_size = 0;
	for(int i = 0; i < wide_argc; i++)
	{
		const int size = WideCharToMultiByte(CP_UTF8, 0, wide_argv.get()[i], -1, nullptr, 0, nullptr, nullptr);
		if(size <= 0 || size > std::numeric_limits<int>::max() - total_size)
			return false;
		total_size += size;
	}

	// 指针表和参数字符共用一块内存，调用者改写参数项后也能释放原缓冲。
	const size_t pointer_count = static_cast<size_t>(wide_argc) + 1;
	if(pointer_count > (std::numeric_limits<size_t>::max() - total_size) / sizeof(const char *))
		return false;
	std::unique_ptr<const char *, decltype(&std::free)> new_argv(
		static_cast<const char **>(std::malloc(pointer_count * sizeof(const char *) + total_size)), std::free);
	if(!new_argv)
		return false;

	char *next_argument = reinterpret_cast<char *>(new_argv.get() + pointer_count);
	int remaining_size = total_size;
	for(int i = 0; i < wide_argc; i++)
	{
		const int size = WideCharToMultiByte(CP_UTF8, 0, wide_argv.get()[i], -1, next_argument, remaining_size, nullptr, nullptr);
		if(size <= 0)
			return false;
		new_argv.get()[i] = next_argument;
		remaining_size -= size;
		next_argument += size;
	}

	new_argv.get()[wide_argc] = nullptr;
	*argc = wide_argc;
	*argv = new_argv.release();
	return true;
#else
	return false;
#endif
}

void cmdline_free(int argc, const char **argv)
{
#if defined(CONF_FAMILY_WINDOWS)
	std::free(argv);
#endif
}

#if !defined(CONF_PLATFORM_ANDROID)
#if defined(CONF_FAMILY_UNIX) && !defined(CONF_PLATFORM_EMSCRIPTEN)
static int os_open_link_unix(const char *opener, const char *link)
try
{
	// 在 fork 前准备好回收线程，避免启动后无法接管子进程。
	std::promise<pid_t> launch;
	auto result = std::make_unique<std::future<pid_t>>(launch.get_future());
	void *reaper = thread_try_init([](void *user) {
		const std::unique_ptr<std::future<pid_t>> child_result(static_cast<std::future<pid_t> *>(user));
		const pid_t child = child_result->get();
		if(child <= 0)
			return;

		while(true)
		{
			int status;
			const pid_t waited = waitpid(child, &status, 0);
			if(waited == child && (WIFEXITED(status) || WIFSIGNALED(status)))
				return;
			if(waited < 0 && errno != EINTR)
				return;
		}
	},
		result.get(), "open link reaper");
	if(!reaper)
		return 0;
	result.release();
	thread_detach(reaper);

	const pid_t child = fork();
	if(child == 0)
	{
		execlp(opener, opener, link, nullptr);
		// exec 失败后不能返回继承了其他线程状态的客户端调用栈。
		_exit(1);
	}
	launch.set_value(child);
	return child > 0;
}
catch(const std::bad_alloc &)
{
	return 0;
}
#endif

int os_open_link(const char *link)
{
#if defined(CONF_FAMILY_WINDOWS)
	if(!link || !str_utf8_check(link))
		return 0;
	std::wstring wide_link;
	try
	{
		wide_link = windows_utf8_to_wide(link);
	}
	catch(const std::bad_alloc &)
	{
		return 0;
	}

	SHELLEXECUTEINFOW info;
	mem_zero(&info, sizeof(SHELLEXECUTEINFOW));
	info.cbSize = sizeof(SHELLEXECUTEINFOW);
	info.lpVerb = nullptr; // NULL to use the default verb, as "open" may not be available
	info.lpFile = wide_link.c_str();
	info.nShow = SW_SHOWNORMAL;
	// The SEE_MASK_NOASYNC flag ensures that the ShellExecuteEx function
	// finishes its DDE conversation before it returns, so it's not necessary
	// to pump messages in the calling thread.
	// The SEE_MASK_FLAG_NO_UI flag suppresses error messages that would pop up
	// when the link cannot be opened, e.g. when a folder does not exist.
	// The SEE_MASK_ASYNCOK flag is not used. It would allow the call to
	// ShellExecuteEx to return earlier, but it also prevents us from doing
	// our own error handling, as the function would always return TRUE.
	info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
	// Save and restore the FPU control word because ShellExecute might change it
	fenv_t floating_point_environment;
	int fegetenv_result = fegetenv(&floating_point_environment);
	BOOL success = ShellExecuteExW(&info);
	if(fegetenv_result == 0)
		fesetenv(&floating_point_environment);
	return success;
#elif defined(CONF_PLATFORM_EMSCRIPTEN)
	const int pid = fork();
	if(pid == 0)
	{
		execlp("open", "open", link, nullptr);
		_exit(1);
	}
	return pid > 0;
#elif defined(CONF_PLATFORM_LINUX)
	return os_open_link_unix("xdg-open", link);
#elif defined(CONF_FAMILY_UNIX)
	return os_open_link_unix("open", link);
#endif
}

int os_open_file(const char *path)
{
#if defined(CONF_PLATFORM_MACOS)
	return os_open_link(path);
#else
	// Create a file link so the path can contain forward and
	// backward slashes. But the file link must be absolute.
	char buf[512];
	char workingDir[IO_MAX_PATH_LENGTH];
	if(fs_is_relative_path(path))
	{
		if(!fs_getcwd(workingDir, sizeof(workingDir)))
			return 0;
		str_append(workingDir, "/");
	}
	else
		workingDir[0] = '\0';
	str_format(buf, sizeof(buf), "file://%s%s", workingDir, path);
	return os_open_link(buf);
#endif
}
#endif // !defined(CONF_PLATFORM_ANDROID)

bool os_version_str(char *version, size_t length)
{
#if defined(CONF_FAMILY_WINDOWS)
	const WCHAR *module_path = L"kernel32.dll";
	DWORD handle;
	DWORD size = GetFileVersionInfoSizeW(module_path, &handle);
	if(!size)
	{
		return false;
	}
	const std::unique_ptr<void, decltype(&std::free)> data(std::malloc(size), std::free);
	if(!data || !GetFileVersionInfoW(module_path, handle, size, data.get()))
		return false;
	void *version_info = nullptr;
	UINT version_info_size = 0;
	if(!VerQueryValueW(data.get(), L"\\", &version_info, &version_info_size) ||
		!version_info || version_info_size < sizeof(VS_FIXEDFILEINFO))
		return false;
	const auto *fileinfo = static_cast<const VS_FIXEDFILEINFO *>(version_info);
	str_format(version, length, "Windows %hu.%hu.%hu.%hu",
		HIWORD(fileinfo->dwProductVersionMS),
		LOWORD(fileinfo->dwProductVersionMS),
		HIWORD(fileinfo->dwProductVersionLS),
		LOWORD(fileinfo->dwProductVersionLS));
	return true;
#else
	struct utsname u;
	if(uname(&u))
	{
		return false;
	}
	char extra[128];
	extra[0] = 0;

	do
	{
		IOHANDLE os_release = io_open("/etc/os-release", IOFLAG_READ);
		char buf[4096];
		int read;
		int offset;
		char *newline;
		if(!os_release)
		{
			break;
		}
		read = io_read(os_release, buf, sizeof(buf) - 1);
		io_close(os_release);
		buf[read] = 0;
		if(str_startswith(buf, "PRETTY_NAME="))
		{
			offset = 0;
		}
		else
		{
			const char *found = str_find(buf, "\nPRETTY_NAME=");
			if(!found)
			{
				break;
			}
			offset = found - buf + 1;
		}
		newline = (char *)str_find(buf + offset, "\n");
		if(newline)
		{
			*newline = 0;
		}
		str_format(extra, sizeof(extra), "; %s", buf + offset + 12);
	} while(false);

	str_format(version, length, "%s %s (%s, %s)%s", u.sysname, u.release, u.machine, u.version, extra);
	return true;
#endif
}

static bool query_os_locale(char *locale, size_t length)
{
#if defined(CONF_FAMILY_WINDOWS)
	wchar_t wide_buffer[LOCALE_NAME_MAX_LENGTH]{};
	const int count = GetUserDefaultLocaleName(wide_buffer, std::size(wide_buffer));
	if(count <= 0 || static_cast<size_t>(count) > std::size(wide_buffer) || wide_buffer[count - 1] != L'\0')
		return false;
	const std::optional<std::string> buffer = windows_wide_to_utf8_bounded(wide_buffer, wide_buffer + count);
	if(!buffer)
		return false;
	str_copy(locale, buffer.value().c_str(), length);
	return true;
#elif defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)
	struct CAppleLocaleOperations
	{
		CFLocaleRef Acquire() { return CFLocaleCopyCurrent(); }
		void Release(CFLocaleRef Locale) noexcept { CFRelease(Locale); }
		CFStringRef Identifier(CFLocaleRef Locale)
		{
			const CFTypeRef Value = CFLocaleGetValue(Locale, kCFLocaleIdentifier);
			return Value && CFGetTypeID(Value) == CFStringGetTypeID() ? static_cast<CFStringRef>(Value) : nullptr;
		}
		CFIndex Length(CFStringRef Identifier) { return CFStringGetLength(Identifier); }
		char *Allocate(size_t Bytes) { return static_cast<char *>(malloc(Bytes)); }
		void Free(char *pBuffer) noexcept { free(pBuffer); }
		bool Convert(CFStringRef Identifier, char *pBuffer, CFIndex Capacity) { return CFStringGetCString(Identifier, pBuffer, Capacity, kCFStringEncodingUTF8); }
	} Operations;
	return os_locale::ReadOwnedIdentifier(locale, length, Operations);
#else
	static const char *ENV_VARIABLES[] = {
		"LC_ALL",
		"LC_MESSAGES",
		"LANG",
	};

	for(const char *env_variable : ENV_VARIABLES)
	{
		const char *env_value = getenv(env_variable);
		if(env_value)
		{
			str_copy(locale, env_value, length);
			return true;
		}
	}
	return false;
#endif
}

void os_locale_str(char *locale, size_t length) noexcept
{
	os_locale::Read(locale, length, query_os_locale);
}
