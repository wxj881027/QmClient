#include "fifo.h"

#include <base/log.h>
#include <base/system.h>

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <utility>

#if defined(CONF_FAMILY_UNIX)

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>

void CFifo::Init(IConsole *pConsole, const char *pFifoFile, int Flag) noexcept
try
{
	Shutdown();
	m_pConsole = pConsole;
	m_IsInit = true;
	if(pFifoFile[0] == '\0')
		return;
	if(str_length(pFifoFile) >= static_cast<int>(sizeof(m_aFilename)))
		return;

	str_copy(m_aFilename, pFifoFile);
	m_Flag = Flag;

	const bool Created = mkfifo(m_aFilename, 0600) == 0;
	if(!Created && errno != EEXIST)
	{
		log_error("fifo", "Failed to create fifo '%s' (%d '%s')", m_aFilename, errno, strerror(errno));
		return;
	}
	struct stat Attribute;
	if(lstat(m_aFilename, &Attribute) != 0 || !S_ISFIFO(Attribute.st_mode))
	{
		log_error("fifo", "Path '%s' is not an accessible fifo; keeping existing entry", m_aFilename);
		return;
	}
	if(Created)
	{
		m_OwnsFifo = true;
		m_Device = Attribute.st_dev;
		m_Inode = Attribute.st_ino;
	}

	int OpenFlags = O_RDONLY | O_NONBLOCK;
#if defined(O_NOFOLLOW)
	OpenFlags |= O_NOFOLLOW;
#endif
	m_File = open(m_aFilename, OpenFlags);
	if(m_File < 0)
	{
		log_error("fifo", "Failed to open fifo '%s' (%d '%s')", m_aFilename, errno, strerror(errno));
	}
	else if(fstat(m_File, &Attribute) != 0 || !S_ISFIFO(Attribute.st_mode) ||
		(m_OwnsFifo && (Attribute.st_dev != m_Device || Attribute.st_ino != m_Inode)))
	{
		close(std::exchange(m_File, -1));
		log_error("fifo", "Opened path '%s' does not match the fifo", m_aFilename);
	}
	else
	{
		log_info("fifo", "Opened fifo '%s'", m_aFilename);
	}
}
catch(...)
{
	Shutdown();
	m_IsInit = true;
}

void CFifo::Shutdown() noexcept
{
	if(m_File >= 0)
		close(std::exchange(m_File, -1));
	if(std::exchange(m_OwnsFifo, false))
	{
		struct stat Attribute;
		if(lstat(m_aFilename, &Attribute) == 0 && S_ISFIFO(Attribute.st_mode) &&
			Attribute.st_dev == m_Device && Attribute.st_ino == m_Inode)
			(void)fs_remove(m_aFilename);
	}
	m_IsInit = false;
}

void CFifo::Update() noexcept
try
{
	if(m_File < 0)
		return;

	char aBuf[8192];
	int Length = read(m_File, aBuf, sizeof(aBuf) - 1);
	if(Length <= 0)
		return;
	aBuf[Length] = '\0';

	char *pCur = aBuf;
	for(int i = 0; i < Length; ++i)
	{
		if(aBuf[i] != '\n')
			continue;
		aBuf[i] = '\0';
		if(str_utf8_check(pCur))
		{
			m_pConsole->ExecuteLineFlag(pCur, m_Flag, IConsole::CLIENT_ID_UNSPECIFIED);
		}
		pCur = aBuf + i + 1;
	}
	if(pCur < aBuf + Length && str_utf8_check(pCur)) // missed the last line
	{
		m_pConsole->ExecuteLineFlag(pCur, m_Flag, IConsole::CLIENT_ID_UNSPECIFIED);
	}
}
catch(...)
{
	// 本批字节已消费，异常时不重放已执行命令；后续Update仍可接收新输入。
}

#elif defined(CONF_FAMILY_WINDOWS)

#include <base/windows.h>

#include <windows.h>

void CFifo::Init(IConsole *pConsole, const char *pFifoFile, int Flag) noexcept
try
{
	Shutdown();
	m_pConsole = pConsole;
	m_IsInit = true;
	if(pFifoFile[0] == '\0')
	{
		return;
	}

	str_copy(m_aFilename, "\\\\.\\pipe\\");
	if(static_cast<size_t>(str_length(pFifoFile)) >= sizeof(m_aFilename) - str_length(m_aFilename))
		return;
	str_append(m_aFilename, pFifoFile);
	m_Flag = Flag;

	const std::wstring WideFilename = windows_utf8_to_wide(m_aFilename);
	m_pPipe = CreateNamedPipeW(WideFilename.c_str(),
		PIPE_ACCESS_DUPLEX,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
		PIPE_UNLIMITED_INSTANCES,
		8192,
		8192,
		NMPWAIT_USE_DEFAULT_WAIT,
		nullptr);
	if(m_pPipe == INVALID_HANDLE_VALUE)
	{
		const DWORD LastError = GetLastError();
		m_pPipe = nullptr;
		log_error("fifo", "Failed to create named pipe '%s' (%ld '%s')", m_aFilename, LastError, windows_format_system_message(LastError).c_str());
	}
	else
	{
		log_info("fifo", "Created named pipe '%s'", m_aFilename);
	}
}
catch(...)
{
	Shutdown();
	m_IsInit = true;
}

void CFifo::Shutdown() noexcept
{
	m_IsInit = false;
	const auto Pipe = std::exchange(m_pPipe, nullptr);
	if(!Pipe)
		return;

	DisconnectNamedPipe(Pipe);
	CloseHandle(Pipe);
}

void CFifo::Update() noexcept
try
{
	if(!m_pPipe)
		return;

	if(!ConnectNamedPipe(m_pPipe, nullptr))
	{
		const DWORD LastError = GetLastError();
		if(LastError == ERROR_PIPE_LISTENING) // waiting for clients to connect
			return;
		if(LastError == ERROR_NO_DATA) // pipe was disconnected from the other end, also disconnect it from this end
		{
			// disconnect the previous client so we can connect to a new one
			DisconnectNamedPipe(m_pPipe);
			return;
		}
		if(LastError != ERROR_PIPE_CONNECTED) // pipe already connected, not an error
		{
			log_error("fifo", "Failed to connect named pipe '%s' (%ld '%s')", m_aFilename, LastError, windows_format_system_message(LastError).c_str());
			return;
		}
	}

	while(m_pPipe) // read all messages from the pipe
	{
		DWORD BytesAvailable;
		if(!PeekNamedPipe(m_pPipe, nullptr, 0, nullptr, &BytesAvailable, nullptr))
		{
			const DWORD LastError = GetLastError();
			if(LastError == ERROR_BROKEN_PIPE)
			{
				// Pipe was disconnected from the other side, either immediately
				// after connecting or after reading the previous message.
				DisconnectNamedPipe(m_pPipe);
			}
			else
			{
				log_error("fifo", "Failed to peek at pipe '%s' (%ld '%s')", m_aFilename, LastError, windows_format_system_message(LastError).c_str());
			}
			return;
		}
		if(BytesAvailable == 0) // pipe connected but no data available
			return;

		if(static_cast<uint64_t>(BytesAvailable) >= std::numeric_limits<size_t>::max())
			return;
		std::unique_ptr<char, decltype(&free)> Buffer(static_cast<char *>(malloc(static_cast<size_t>(BytesAvailable) + 1)), &free);
		if(!Buffer)
			return;
		char *pBuf = Buffer.get();
		DWORD Length;
		if(!ReadFile(m_pPipe, pBuf, BytesAvailable, &Length, nullptr))
		{
			const DWORD LastError = GetLastError();
			log_error("fifo", "Failed to read from pipe '%s' (%ld '%s')", m_aFilename, LastError, windows_format_system_message(LastError).c_str());
			return;
		}
		pBuf[Length] = '\0';

		char *pCur = pBuf;
		for(DWORD i = 0; i < Length; ++i)
		{
			if(pBuf[i] != '\n' && (pBuf[i] != '\r' || pBuf[i + 1] != '\n'))
			{
				continue;
			}
			if(pBuf[i] == '\r')
			{
				pBuf[i] = '\0';
				++i;
			}
			pBuf[i] = '\0';
			if(pCur[0] != '\0' && str_utf8_check(pCur))
			{
				m_pConsole->ExecuteLineFlag(pCur, m_Flag, IConsole::CLIENT_ID_UNSPECIFIED);
			}
			pCur = pBuf + i + 1;
		}
		if(pCur < pBuf + Length && pCur[0] != '\0' && str_utf8_check(pCur)) // missed the last line
		{
			m_pConsole->ExecuteLineFlag(pCur, m_Flag, IConsole::CLIENT_ID_UNSPECIFIED);
		}
	}
}
catch(...)
{
	// 缓冲由局部owner释放；命令可能已有副作用，不能自动重放本批数据。
}
#endif
