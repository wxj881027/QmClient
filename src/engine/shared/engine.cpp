/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <base/logger.h>
#include <base/os.h>
#include <base/rust.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/shared/network.h>
#include <engine/storage.h>

#include <memory>
#include <new>
#include <thread>

namespace
{

	class CEngine : public IEngine
	{
		IConsole *m_pConsole;
		IStorage *m_pStorage;

		bool m_Logging;
		std::shared_ptr<CFutureLogger> m_pFutureLogger;

		char m_aAppName[256];

		CJobPool m_JobPool;

		void OpenNetworkLog()
		try
		{
			using CFileOwner = std::unique_ptr<void, decltype(&io_close)>;
			const auto IsOccupied = [this](const char *pName) {
				return m_pStorage->FileExists(pName, IStorage::TYPE_SAVE) ||
				       m_pStorage->FolderExists(pName, IStorage::TYPE_SAVE);
			};

			char aTimestamp[32];
			str_timestamp(aTimestamp, sizeof(aTimestamp));
			for(int Attempt = 0; Attempt < 32; ++Attempt)
			{
				char aCapture[40];
				if(Attempt == 0)
					str_copy(aCapture, aTimestamp);
				else
					str_format(aCapture, sizeof(aCapture), "%s_%d", aTimestamp, Attempt);
				char aFilenameSent[IO_MAX_PATH_LENGTH], aFilenameRecv[IO_MAX_PATH_LENGTH];
				str_format(aFilenameSent, sizeof(aFilenameSent), "dumps/network_sent_%s.txt", aCapture);
				str_format(aFilenameRecv, sizeof(aFilenameRecv), "dumps/network_recv_%s.txt", aCapture);
				if(IsOccupied(aFilenameSent) || IsOccupied(aFilenameRecv))
					continue;

				constexpr int OpenFlags = IOFLAG_WRITE | IOFLAG_EXCLUSIVE;
				CFileOwner pSent(m_pStorage->OpenFile(aFilenameSent, OpenFlags, IStorage::TYPE_SAVE), io_close);
				// 两个实例同时开始采集时，发送文件由先取得独占句柄的实例保留。
				if(!pSent && IsOccupied(aFilenameSent))
					continue;
				CFileOwner pRecv(m_pStorage->OpenFile(aFilenameRecv, OpenFlags, IStorage::TYPE_SAVE), io_close);
				if(!pSent && !pRecv && IsOccupied(aFilenameRecv))
					continue;

				m_Logging = pSent || pRecv;
				CNetBase::OpenLog(pSent.release(), pRecv.release());
				return;
			}
			log_error("network", "no available filename for network logs");
		}
		catch(const std::bad_alloc &)
		{
			log_error("network", "could not prepare network log files");
		}

		static void Con_DbgLognetwork(IConsole::IResult *pResult, void *pUserData)
		{
			CEngine *pEngine = static_cast<CEngine *>(pUserData);

			if(pEngine->m_Logging)
			{
				CNetBase::CloseLog();
				pEngine->m_Logging = false;
			}
			else
			{
				pEngine->OpenNetworkLog();
			}
		}

	public:
		CEngine(bool Test, const char *pAppname, std::shared_ptr<CFutureLogger> pFutureLogger) :
			m_pFutureLogger(std::move(pFutureLogger))
		{
			str_copy(m_aAppName, pAppname);
			if(!Test)
			{
				log_info("engine", "running on %s-%s-%s", CONF_FAMILY_STRING, CONF_PLATFORM_STRING, CONF_ARCH_STRING);
				log_info("engine", "arch is %s", CONF_ARCH_ENDIAN_STRING);

				char aVersionStr[128];
				if(os_version_str(aVersionStr, sizeof(aVersionStr)))
				{
					log_info("engine", "operating system version: %s", aVersionStr);
				}

				rust_panic_use_dbg_assert();

				// init the network
				net_init();
				CNetBase::Init();
			}
			m_Logging = false;
		}

		bool StartJobs(THREAD_CREATE pfnCreate)
		{
#if defined(CONF_PLATFORM_EMSCRIPTEN)
			// Make sure we don't use more threads than allowed in total (see PTHREAD_POOL_SIZE in Emscripten.toolchain)
			// otherwise starting more threads may lead to deadlocks as the threads will simply not start.
			const size_t ThreadCount = 4;
#else
			const size_t ThreadCount = std::max(4, (int)std::thread::hardware_concurrency()) - 2;
#endif
			return m_JobPool.TryInit(ThreadCount, pfnCreate);
		}

		~CEngine() override
		{
			CNetBase::CloseLog();
		}

		void Init() override
		{
			m_pConsole = Kernel()->RequestInterface<IConsole>();
			m_pStorage = Kernel()->RequestInterface<IStorage>();

			if(!m_pConsole || !m_pStorage)
				return;

			m_pConsole->Register("dbg_lognetwork", "", CFGFLAG_SERVER | CFGFLAG_CLIENT, Con_DbgLognetwork, this, "Log the network");
		}

		void AddJob(std::shared_ptr<IJob> pJob) override
		{
			m_JobPool.Add(std::move(pJob));
		}

		bool TryAddJob(std::shared_ptr<IJob> pJob) override
		{
			return m_JobPool.TryAdd(std::move(pJob));
		}

		void ShutdownJobs() override
		{
			m_JobPool.Shutdown();
		}

		void SetAdditionalLogger(std::shared_ptr<ILogger> &&pLogger) override
		{
			m_pFutureLogger->Set(pLogger);
		}
	};

} // namespace

IEngine *CreateEngine(const char *pAppname, std::shared_ptr<CFutureLogger> pFutureLogger)
{
	auto pEngine = std::make_unique<CEngine>(false, pAppname, std::move(pFutureLogger));
	if(!pEngine->StartJobs(thread_try_init))
		return nullptr;
	return pEngine.release();
}

IEngine *CreateTestEngine(const char *pAppname, THREAD_CREATE pfnCreate)
{
	auto pEngine = std::make_unique<CEngine>(true, pAppname, nullptr);
	if(!pEngine->StartJobs(pfnCreate))
		return nullptr;
	return pEngine.release();
}
