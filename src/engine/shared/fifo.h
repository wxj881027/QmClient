#ifndef ENGINE_SHARED_FIFO_H
#define ENGINE_SHARED_FIFO_H

#include <base/detect.h>

#include <engine/console.h>

#if defined(CONF_FAMILY_UNIX)
#include <sys/types.h>
#endif

class CFifo
{
	IConsole *m_pConsole = nullptr;
	char m_aFilename[IO_MAX_PATH_LENGTH]{};
	int m_Flag = 0;
	bool m_IsInit = false;
#if defined(CONF_FAMILY_UNIX)
	int m_File = -1;
	bool m_OwnsFifo = false;
	dev_t m_Device{};
	ino_t m_Inode{};
#elif defined(CONF_FAMILY_WINDOWS)
	void *m_pPipe = nullptr;
#endif

public:
	CFifo() noexcept = default;
	CFifo(const CFifo &) = delete;
	CFifo &operator=(const CFifo &) = delete;
	~CFifo() { Shutdown(); }
	void Init(IConsole *pConsole, const char *pFifoFile, int Flag) noexcept;
	void Update() noexcept;
	void Shutdown() noexcept;
	bool IsInit() const { return m_IsInit; }
};

#endif
