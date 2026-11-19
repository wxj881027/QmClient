/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "sphore.h"

#include "dbg.h"

#include <cerrno>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <limits>
#elif defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)
#include "secure.h"
#include "str.h"
#include "system.h" // pid

#include <fcntl.h> // O_* constants
#include <sys/stat.h> // S_* constants
#endif

#if defined(CONF_FAMILY_WINDOWS)
bool sphore_try_init(SEMAPHORE *sem)
{
	*sem = CreateSemaphoreW(nullptr, 0, std::numeric_limits<LONG>::max(), nullptr);
	return *sem != nullptr;
}
void sphore_wait(SEMAPHORE *sem)
{
	dbg_assert(WaitForSingleObject((HANDLE)*sem, INFINITE) == WAIT_OBJECT_0, "WaitForSingleObject failure");
}
void sphore_signal(SEMAPHORE *sem)
{
	dbg_assert(ReleaseSemaphore((HANDLE)*sem, 1, nullptr), "ReleaseSemaphore failure");
}
void sphore_destroy(SEMAPHORE *sem)
{
	dbg_assert(CloseHandle((HANDLE)*sem), "CloseHandle failure");
}
#elif defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)
bool sphore_try_init(SEMAPHORE *sem)
{
	unsigned Random;
	secure_random_fill(&Random, sizeof(Random));
	char aBuf[32];
	str_format(aBuf, sizeof(aBuf), "/%d.%08x", pid(), Random);
	*sem = sem_open(aBuf, O_CREAT | O_EXCL, S_IRWXU | S_IRWXG, 0);
	if(*sem == SEM_FAILED)
		return false;
	if(sem_unlink(aBuf) != 0)
	{
		sem_close(*sem);
		*sem = SEM_FAILED;
		return false;
	}
	return true;
}
void sphore_wait(SEMAPHORE *sem)
{
	while(true)
	{
		if(sem_wait(*sem) == 0)
			break;
		dbg_assert(errno == EINTR, "sem_wait failure");
	}
}
void sphore_signal(SEMAPHORE *sem)
{
	dbg_assert(sem_post(*sem) == 0, "sem_post failure");
}
void sphore_destroy(SEMAPHORE *sem)
{
	dbg_assert(sem_close(*sem) == 0, "sem_close failure");
}
#elif defined(CONF_FAMILY_UNIX)
bool sphore_try_init(SEMAPHORE *sem)
{
	return sem_init(sem, 0, 0) == 0;
}
void sphore_wait(SEMAPHORE *sem)
{
	while(true)
	{
		if(sem_wait(sem) == 0)
			break;
		dbg_assert(errno == EINTR, "sem_wait failure");
	}
}
void sphore_signal(SEMAPHORE *sem)
{
	dbg_assert(sem_post(sem) == 0, "sem_post failure");
}
void sphore_destroy(SEMAPHORE *sem)
{
	dbg_assert(sem_destroy(sem) == 0, "sem_destroy failure");
}
#endif

bool sphore_try_wait(SEMAPHORE *sem)
{
#if defined(CONF_FAMILY_WINDOWS)
	const DWORD result = WaitForSingleObject(*sem, 0);
	dbg_assert(result == WAIT_OBJECT_0 || result == WAIT_TIMEOUT, "WaitForSingleObject failure");
	return result == WAIT_OBJECT_0;
#elif defined(CONF_FAMILY_UNIX)
#if defined(CONF_PLATFORM_MACOS) || defined(CONF_PLATFORM_IOS)
	sem_t *native_sem = *sem;
#else
	sem_t *native_sem = sem;
#endif
	while(true)
	{
		if(sem_trywait(native_sem) == 0)
			return true;
		const int error = errno;
		if(error == EAGAIN)
			return false;
		dbg_assert(error == EINTR, "sem_trywait failure");
	}
#endif
}

void sphore_init(SEMAPHORE *sem)
{
	dbg_assert(sphore_try_init(sem), "semaphore creation failure");
}
