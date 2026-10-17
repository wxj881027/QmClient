#ifndef ENGINE_CLIENT_VIDEO_RECORDER_LIFECYCLE_H
#define ENGINE_CLIENT_VIDEO_RECORDER_LIFECYCLE_H

#include <base/log.h>

#include <engine/shared/video.h>

#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// 发布录像对象时暂停声卡；创建失败也必须恢复设备。
template<typename TSound, typename TCreate>
bool StartVideoRecorder(TSound &Sound, TCreate Create)
{
	class CScopedAudioPause
	{
		TSound &m_Sound;

	public:
		explicit CScopedAudioPause(TSound &Sound) :
			m_Sound(Sound) { m_Sound.PauseAudioDevice(); }
		~CScopedAudioPause() { m_Sound.UnpauseAudioDevice(); }
	};

	IVideo *pVideo = nullptr;
	try
	{
		{
			const CScopedAudioPause Pause(Sound);
			pVideo = Create();
		}
		if(pVideo && pVideo->Start())
			return true;
	}
	catch(const std::exception &Error)
	{
		log_error("videorecorder", "Could not initialize recording: %s", Error.what());
	}
	catch(...)
	{
		log_error("videorecorder", "Could not initialize recording: unknown exception");
	}

	// Stop 同时释放对象；此时启动可能只完成了部分缓冲或线程。
	if(pVideo)
		pVideo->Stop();
	return false;
}

class CVideoRecorderWorker
{
public:
	std::thread m_Thread;
	std::mutex m_Mutex;
	std::condition_variable m_Cond;
	bool m_Started = false;
	bool m_Finished = false;
};

template<typename TWorker>
void StopVideoRecorderThreads(std::vector<std::unique_ptr<TWorker>> &vpWorkers)
{
	// 分配失败会留下空槽位，线程创建失败会留下尚不可 join 的对象。
	for(auto &pWorker : vpWorkers)
	{
		if(!pWorker)
			continue;
		{
			std::unique_lock<std::mutex> Lock(pWorker->m_Mutex);
			pWorker->m_Finished = true;
			pWorker->m_Cond.notify_all();
		}
		if(pWorker->m_Thread.joinable())
			pWorker->m_Thread.join();
	}
	vpWorkers.clear();
}

#endif // ENGINE_CLIENT_VIDEO_RECORDER_LIFECYCLE_H
