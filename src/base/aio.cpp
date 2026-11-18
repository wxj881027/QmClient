#include "aio.h"

#include "aio_buffer.h"
#include "io.h"
#include "lock.h"
#include "sphore.h"
#include "thread.h"

#include <new>

#define ASYNC_LOCAL_BUFSIZE (64 * 1024)

struct ASYNCIO
{
	CLock lock;
	IOHANDLE io;
	SEMAPHORE sphore;
	void *thread;

	CAsyncIOBuffer buffer;

	int error;
	unsigned char finish;
	unsigned char refcount;
};

enum
{
	ASYNCIO_RUNNING,
	ASYNCIO_CLOSE,
	ASYNCIO_EXIT,
};

static void aio_handle_free_and_unlock(ASYNCIO *aio) RELEASE(aio->lock)
{
	int do_free;
	aio->refcount--;

	do_free = aio->refcount == 0;
	aio->lock.unlock();
	if(do_free)
	{
		sphore_destroy(&aio->sphore);
		delete aio;
	}
}

static void aio_thread(void *user)
{
	ASYNCIO *aio = (ASYNCIO *)user;

	aio->lock.lock();
	while(true)
	{
		int result_io_error;
		unsigned char local_buffer[ASYNC_LOCAL_BUFSIZE];

		if(aio->buffer.Empty())
		{
			if(aio->finish != ASYNCIO_RUNNING)
			{
				if(aio->finish == ASYNCIO_CLOSE)
				{
					io_close(aio->io);
				}
				aio_handle_free_and_unlock(aio);
				break;
			}
			aio->lock.unlock();
			sphore_wait(&aio->sphore);
			aio->lock.lock();
			continue;
		}

		const unsigned int local_buffer_len = aio->buffer.Read(local_buffer, sizeof(local_buffer));
		aio->lock.unlock();

		io_write(aio->io, local_buffer, local_buffer_len);
		io_flush(aio->io);
		result_io_error = io_error(aio->io);

		aio->lock.lock();
		if(aio->error == 0)
			aio->error = result_io_error;
	}
}

ASYNCIO *aio_new(IOHANDLE io)
{
	ASYNCIO *aio = new(std::nothrow) ASYNCIO;
	if(!aio || !aio->buffer.IsValid())
	{
		delete aio;
		return nullptr;
	}
	aio->io = io;
	if(!sphore_try_init(&aio->sphore))
	{
		delete aio;
		return nullptr;
	}
	aio->thread = nullptr;

	aio->error = 0;
	aio->finish = ASYNCIO_RUNNING;
	aio->refcount = 2;

	aio->thread = thread_try_init(aio_thread, aio, "aio");
	if(!aio->thread)
	{
		sphore_destroy(&aio->sphore);
		delete aio;
		return nullptr;
	}
	return aio;
}

void aio_lock(ASYNCIO *aio) ACQUIRE(aio->lock)
{
	aio->lock.lock();
}

void aio_unlock(ASYNCIO *aio) RELEASE(aio->lock)
{
	aio->lock.unlock();
	sphore_signal(&aio->sphore);
}

void aio_write_unlocked(ASYNCIO *aio, const void *buffer, unsigned size)
{
	if(aio->finish != ASYNCIO_RUNNING)
		return;
	// 后续成功的写入或刷盘不能抹去已发生的数据丢失。
	if(!aio->buffer.Append(buffer, size) && aio->error == 0)
		aio->error = -1;
}

void aio_write(ASYNCIO *aio, const void *buffer, unsigned size)
{
	aio_lock(aio);
	aio_write_unlocked(aio, buffer, size);
	aio_unlock(aio);
}

void aio_write_newline_unlocked(ASYNCIO *aio)
{
#if defined(CONF_FAMILY_WINDOWS)
	aio_write_unlocked(aio, "\r\n", 2);
#else
	aio_write_unlocked(aio, "\n", 1);
#endif
}

void aio_write_newline(ASYNCIO *aio)
{
	aio_lock(aio);
	aio_write_newline_unlocked(aio);
	aio_unlock(aio);
}

int aio_error(ASYNCIO *aio)
{
	CLockScope ls(aio->lock);
	return aio->error;
}

void aio_close(ASYNCIO *aio)
{
	{
		CLockScope ls(aio->lock);
		aio->finish = ASYNCIO_CLOSE;
	}
	sphore_signal(&aio->sphore);
}

void aio_wait(ASYNCIO *aio)
{
	void *thread;
	{
		CLockScope ls(aio->lock);
		thread = aio->thread;
		aio->thread = nullptr;
		if(aio->finish == ASYNCIO_RUNNING)
		{
			aio->finish = ASYNCIO_EXIT;
		}
	}
	sphore_signal(&aio->sphore);
	thread_wait(thread);
}

void aio_free(ASYNCIO *aio)
{
	aio->lock.lock();
	if(aio->thread)
	{
		thread_detach(aio->thread);
		aio->thread = nullptr;
	}
	aio_handle_free_and_unlock(aio);
}
