/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "io.h"

#include "dbg.h"
#include "detect.h"
#include "fs.h"
#include "io_read_all.h"
#include "mem.h"
#include "str.h"
#include "windows.h"
#include "windows_io.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h> // FlushFileBuffers

#include <io.h> // _get_osfhandle
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h> // fsync
#endif

#if defined(CONF_FAMILY_WINDOWS)
int CWindowsFileOperations::OpenDescriptor(intptr_t Handle, int OpenFlags) noexcept
{
	return _open_osfhandle(Handle, OpenFlags);
}

FILE *CWindowsFileOperations::OpenStream(int Descriptor, const char *pMode) noexcept
{
	return _fdopen(Descriptor, pMode);
}

void CWindowsFileOperations::CloseNativeHandle(intptr_t Handle) noexcept
{
	CloseHandle(reinterpret_cast<HANDLE>(Handle));
}

void CWindowsFileOperations::CloseDescriptor(int Descriptor) noexcept
{
	_close(Descriptor);
}

IOHANDLE windows_io_from_handle(intptr_t Handle, const char *pMode, CWindowsFileOperations *pOperations, int OpenFlags) noexcept
{
	CWindowsFileOperations DefaultOperations;
	CWindowsFileOperations &Operations = pOperations ? *pOperations : DefaultOperations;
	const int Descriptor = Operations.OpenDescriptor(Handle, OpenFlags);
	if(Descriptor == -1)
	{
		const int Error = errno;
		Operations.CloseNativeHandle(Handle);
		errno = Error;
		return nullptr;
	}

	FILE *pStream = Operations.OpenStream(Descriptor, pMode);
	if(!pStream)
	{
		const int Error = errno;
		// 句柄已交给 CRT，关闭描述符会同时关闭原生句柄，不能再次 CloseHandle。
		Operations.CloseDescriptor(Descriptor);
		errno = Error;
	}
	return pStream;
}

#endif

static IOHANDLE io_open_impl(const char *filename, int flags, bool *pNotFound, bool RegularOnly = false)
{
	if(pNotFound)
		*pNotFound = false;
	if(!filename)
		return nullptr;
	dbg_assert(flags == IOFLAG_READ || flags == IOFLAG_WRITE || flags == IOFLAG_APPEND || flags == (IOFLAG_WRITE | IOFLAG_EXCLUSIVE), "invalid file open flags");
#if defined(CONF_FAMILY_WINDOWS)
	if(!str_utf8_check(filename))
		return nullptr;

	std::wstring wide_filename;
	try
	{
		wide_filename = windows_utf8_to_wide(filename);
	}
	catch(const std::bad_alloc &)
	{
		errno = ENOMEM;
		return nullptr;
	}
	DWORD desired_access;
	DWORD creation_disposition;
	const char *open_mode;
	if((flags & IOFLAG_READ) != 0)
	{
		desired_access = FILE_READ_DATA;
		creation_disposition = OPEN_EXISTING;
		open_mode = "rb";
	}
	else if((flags & IOFLAG_WRITE) != 0)
	{
		desired_access = FILE_WRITE_DATA;
		creation_disposition = (flags & IOFLAG_EXCLUSIVE) != 0 ? CREATE_NEW : CREATE_ALWAYS;
		open_mode = "wb";
	}
	else if(flags == IOFLAG_APPEND)
	{
		desired_access = FILE_APPEND_DATA;
		creation_disposition = OPEN_ALWAYS;
		open_mode = "ab";
	}
	else
	{
		dbg_assert_failed("logic error");
	}
	HANDLE handle = CreateFileW(wide_filename.c_str(), desired_access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, creation_disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(handle == INVALID_HANDLE_VALUE)
	{
		if(pNotFound)
		{
			const DWORD Error = GetLastError();
			*pNotFound = Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND;
		}
		return nullptr;
	}
	if(RegularOnly)
	{
		BY_HANDLE_FILE_INFORMATION Information{};
		if(GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &Information) || (Information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			CloseHandle(handle);
			return nullptr;
		}
	}
	return windows_io_from_handle(reinterpret_cast<intptr_t>(handle), open_mode);
#else
	if(pNotFound && !str_utf8_check(filename))
		return nullptr;

	if(RegularOnly)
	{
		int OpenFlags = O_RDONLY | O_NONBLOCK | O_NOCTTY;
#if defined(O_CLOEXEC)
		OpenFlags |= O_CLOEXEC;
#endif
		int Descriptor;
		do
		{
			Descriptor = open(filename, OpenFlags);
		} while(Descriptor == -1 && errno == EINTR);
		if(Descriptor == -1)
		{
			if(pNotFound)
				*pNotFound = errno == ENOENT;
			return nullptr;
		}
		struct SDescriptor
		{
			int m_Value;
			~SDescriptor()
			{
				if(m_Value != -1)
				{
					const int Error = errno;
					close(m_Value);
					errno = Error;
				}
			}
		} Owner{Descriptor};
		struct stat Information;
		if(fstat(Descriptor, &Information) != 0)
			return nullptr;
		if(!S_ISREG(Information.st_mode))
		{
			errno = EINVAL;
			return nullptr;
		}
#if !defined(O_CLOEXEC)
		const int DescriptorFlags = fcntl(Descriptor, F_GETFD);
		if(DescriptorFlags == -1 || fcntl(Descriptor, F_SETFD, DescriptorFlags | FD_CLOEXEC) == -1)
			return nullptr;
#endif
		// 类型固定在已打开描述符上，恢复普通文件原有的阻塞读取行为。
		const int StatusFlags = fcntl(Descriptor, F_GETFL);
		if(StatusFlags == -1 || fcntl(Descriptor, F_SETFL, StatusFlags & ~O_NONBLOCK) == -1)
			return nullptr;
		FILE *pStream = fdopen(Descriptor, "rb");
		if(pStream)
			Owner.m_Value = -1;
		return pStream;
	}
	const char *open_mode;
	if((flags & IOFLAG_READ) != 0)
	{
		open_mode = "rb";
	}
	else if((flags & IOFLAG_WRITE) != 0)
	{
		open_mode = (flags & IOFLAG_EXCLUSIVE) != 0 ? "wbx" : "wb";
	}
	else if(flags == IOFLAG_APPEND)
	{
		open_mode = "ab";
	}
	else
	{
		dbg_assert_failed("Invalid flags: %d", flags);
	}
	FILE *pFile = fopen(filename, open_mode);
	if(!pFile && pNotFound)
		*pNotFound = errno == ENOENT;
	return pFile;
#endif
}

IOHANDLE io_open(const char *filename, int flags)
{
	return io_open_impl(filename, flags, nullptr);
}

IOHANDLE io_open_read_checked(const char *pFilename, bool *pNotFound)
{
	return io_open_impl(pFilename, IOFLAG_READ, pNotFound);
}

IOHANDLE io_open_read_regular_checked(const char *pFilename, bool *pNotFound)
{
	bool NotFound = false;
	if(!pNotFound)
		pNotFound = &NotFound;
	*pNotFound = false;
	if(!pFilename || pFilename[0] == '\0')
	{
		errno = EINVAL;
		return nullptr;
	}
	return io_open_impl(pFilename, IOFLAG_READ, pNotFound, true);
}

unsigned io_read(IOHANDLE io, void *buffer, unsigned size)
{
	return fread(buffer, 1, size, (FILE *)io);
}

bool io_read_all(IOHANDLE io, void **result, unsigned *result_len)
{
	const SIOReadAllAllocator Allocator{
		[](void *pBuffer, size_t Size, void *) { return realloc(pBuffer, Size); },
		[](void *pBuffer, void *) { free(pBuffer); },
		nullptr};
	return io_read_all_with_allocator(io, result, result_len, Allocator);
}

bool io_read_all_with_allocator(IOHANDLE io, void **result, unsigned *result_len, const SIOReadAllAllocator &Allocator)
{
	*result = nullptr;
	*result_len = 0;
	// Loading files larger than 1 GiB into memory is not supported.
	constexpr int64_t MAX_FILE_SIZE = (int64_t)1024 * 1024 * 1024;

	int64_t real_len = io_length(io);
	if(real_len > MAX_FILE_SIZE)
	{
		return false;
	}

	int64_t len = real_len < 0 ? 1024 : real_len; // use default initial size if we couldn't get the length
	char *buffer = (char *)Allocator.m_pfnReallocate(nullptr, len + 1, Allocator.m_pUser);
	if(buffer == nullptr)
		return false;
	auto resize = [&](size_t size) {
		void *candidate = Allocator.m_pfnReallocate(buffer, size, Allocator.m_pUser);
		if(candidate == nullptr)
			return false;
		buffer = (char *)candidate;
		return true;
	};
	auto fail = [&]() {
		Allocator.m_pfnFree(buffer, Allocator.m_pUser);
		return false;
	};
	int64_t read = io_read(io, buffer, len + 1); // +1 to check if the file size is larger than expected
	if(io_error(io))
		return fail();
	if(read < len)
	{
		// 缩小容量不是读取成功的必要条件，失败时保留原缓冲。
		resize(read + 1);
		len = read;
	}
	else if(read > len)
	{
		int64_t cap = 2 * read;
		if(cap > MAX_FILE_SIZE)
		{
			return fail();
		}
		len = read;
		if(!resize(cap))
			return fail();
		while((read = io_read(io, buffer + len, cap - len)) != 0)
		{
			len += read;
			if(len == cap)
			{
				cap *= 2;
				if(cap > MAX_FILE_SIZE)
				{
					return fail();
				}
				if(!resize(cap))
					return fail();
			}
		}
		if(io_error(io))
			return fail();
		resize(len + 1);
	}
	buffer[len] = 0;
	*result = buffer;
	*result_len = len;
	return true;
}

char *io_read_all_str(IOHANDLE io)
{
	void *buffer;
	unsigned len;
	if(!io_read_all(io, &buffer, &len))
	{
		return nullptr;
	}
	if(mem_has_null(buffer, len))
	{
		free(buffer);
		return nullptr;
	}
	return (char *)buffer;
}

int io_skip(IOHANDLE io, int64_t size)
{
	return io_seek(io, size, IOSEEK_CUR);
}

int io_seek(IOHANDLE io, int64_t offset, ESeekOrigin origin)
{
	int real_origin;
	switch(origin)
	{
	case IOSEEK_START:
		real_origin = SEEK_SET;
		break;
	case IOSEEK_CUR:
		real_origin = SEEK_CUR;
		break;
	case IOSEEK_END:
		real_origin = SEEK_END;
		break;
	default:
		dbg_assert_failed("Invalid origin: %d", origin);
	}
#if defined(CONF_FAMILY_WINDOWS)
	return _fseeki64((FILE *)io, offset, real_origin);
#else
	return fseeko((FILE *)io, offset, real_origin);
#endif
}

int64_t io_tell(IOHANDLE io)
{
#if defined(CONF_FAMILY_WINDOWS)
	return _ftelli64((FILE *)io);
#else
	return ftello((FILE *)io);
#endif
}

int64_t io_length(IOHANDLE io)
{
	if(io_seek(io, 0, IOSEEK_END) != 0)
	{
		return -1;
	}
	const int64_t length = io_tell(io);
	if(io_seek(io, 0, IOSEEK_START) != 0)
	{
		return -1;
	}
	return length;
}

unsigned io_write(IOHANDLE io, const void *buffer, unsigned size)
{
	return fwrite(buffer, 1, size, (FILE *)io);
}

bool io_write_newline(IOHANDLE io)
{
#if defined(CONF_FAMILY_WINDOWS)
	return io_write(io, "\r\n", 2) == 2;
#else
	return io_write(io, "\n", 1) == 1;
#endif
}

int io_close(IOHANDLE io)
{
	return fclose((FILE *)io) != 0;
}

int io_flush(IOHANDLE io)
{
	return fflush((FILE *)io);
}

int io_sync(IOHANDLE io)
{
	if(io_flush(io))
	{
		return 1;
	}
#if defined(CONF_FAMILY_WINDOWS)
	return FlushFileBuffers((HANDLE)_get_osfhandle(_fileno((FILE *)io))) == FALSE;
#else
	return fsync(fileno((FILE *)io)) != 0;
#endif
}

int io_error(IOHANDLE io)
{
	return ferror((FILE *)io);
}

IOHANDLE io_stdin()
{
	return stdin;
}

IOHANDLE io_stdout()
{
	return stdout;
}

IOHANDLE io_stderr()
{
	return stderr;
}

IOHANDLE io_current_exe()
{
	char path[IO_MAX_PATH_LENGTH];
	if(fs_executable_path(path, sizeof(path)) != 0)
	{
		return nullptr;
	}
	return io_open(path, IOFLAG_READ);
}
