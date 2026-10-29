#include <base/crashdump.h>
#include <base/detect.h>
#include <base/logger.h>
#include <base/system.h>
#include <base/windows.h>

#include <engine/console.h>
#include <engine/engine.h>
#include <engine/http.h>
#include <engine/map.h>
#include <engine/server.h>
#include <engine/server/antibot.h>
#include <engine/server/databases/connection.h>
#include <engine/server/server.h>
#include <engine/server/server_logger.h>
#include <engine/shared/assertion_logger.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/version.h>

#include <mutex>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#elif defined(CONF_PLATFORM_ANDROID)
#include <android/jni_server.h>

#include <jni.h>
#endif

#include <csignal>

static volatile sig_atomic_t InterruptSignaled = 0;

bool IsInterrupted()
{
	return InterruptSignaled;
}

static void HandleSigIntTerm(int Param)
{
	InterruptSignaled = 1;

	// Exit the next time a signal is received
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
}

int main(int argc, const char **argv)
{
	const int64_t MainStart = time_get();

	CCmdlineFix CmdlineFix(&argc, &argv);

#if !defined(CONF_PLATFORM_ANDROID)
	bool Silent = false;

	for(int i = 1; i < argc; i++)
	{
		if(str_comp("-s", argv[i]) == 0 || str_comp("--silent", argv[i]) == 0)
		{
			Silent = true;
#if defined(CONF_FAMILY_WINDOWS)
			ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif
			break;
		}
	}
#endif

#if defined(CONF_FAMILY_WINDOWS)
	CWindowsComLifecycle WindowsComLifecycle(false);
#endif

	std::vector<std::shared_ptr<ILogger>> vpLoggers;
	std::shared_ptr<ILogger> pStdoutLogger;
#if defined(CONF_PLATFORM_ANDROID)
	pStdoutLogger = std::shared_ptr<ILogger>(log_logger_android());
#else
	if(!Silent)
	{
		pStdoutLogger = std::shared_ptr<ILogger>(log_logger_stdout());
	}
#endif
	if(pStdoutLogger)
	{
		vpLoggers.push_back(pStdoutLogger);
	}
	std::shared_ptr<CFutureLogger> pFutureFileLogger = std::make_shared<CFutureLogger>();
	vpLoggers.push_back(pFutureFileLogger);
	std::shared_ptr<CFutureLogger> pFutureConsoleLogger = std::make_shared<CFutureLogger>();
	vpLoggers.push_back(pFutureConsoleLogger);
	std::shared_ptr<CFutureLogger> pFutureAssertionLogger = std::make_shared<CFutureLogger>();
	vpLoggers.push_back(pFutureAssertionLogger);
	log_set_global_logger(log_logger_collection(std::move(vpLoggers)).release());

	if(MysqlInit() != 0)
	{
		log_error("mysql", "failed to initialize MySQL library");
		return -1;
	}

	signal(SIGINT, HandleSigIntTerm);
	signal(SIGTERM, HandleSigIntTerm);

	CServer *pServer = CreateServer();
	pServer->SetLoggers(pFutureFileLogger, std::move(pStdoutLogger));

	IKernel *pKernel = IKernel::Create();
	pKernel->RegisterInterface(pServer);

	// create the components
	IEngine *pEngine = CreateEngine(GAME_NAME, pFutureConsoleLogger);
	if(!pEngine)
	{
		log_error("server", "failed to initialize worker threads");
		return -1;
	}
	pKernel->RegisterInterface(pEngine);

	IStorage *pStorage = CreateStorage(IStorage::EInitializationType::SERVER, argc, argv);
	if(!pStorage)
	{
		log_error("server", "failed to initialize storage");
		return -1;
	}
	pKernel->RegisterInterface(pStorage);

	pFutureAssertionLogger->Set(CreateAssertionLogger(pStorage, GAME_NAME));

	{
		char aBuf[IO_MAX_PATH_LENGTH];
		char aBufName[IO_MAX_PATH_LENGTH];
		char aDate[64];
		str_timestamp(aDate, sizeof(aDate));
		str_format(aBufName, sizeof(aBufName), "dumps/" GAME_NAME "-Server_%s_crash_log_%s_%d_%s.RTP", CONF_PLATFORM_STRING, aDate, pid(), GIT_SHORTREV_HASH != nullptr ? GIT_SHORTREV_HASH : "");
		pStorage->GetCompletePath(IStorage::TYPE_SAVE, aBufName, aBuf, sizeof(aBuf));
		crashdump_init_if_available(aBuf);
	}

	IConsole *pConsole = CreateConsole(CFGFLAG_SERVER | CFGFLAG_ECON).release();
	pKernel->RegisterInterface(pConsole);

	IConfigManager *pConfigManager = CreateConfigManager();
	pKernel->RegisterInterface(pConfigManager);

	IEngineMap *pEngineMap = CreateEngineMap();
	pKernel->RegisterInterface(pEngineMap); // IEngineMap
	pKernel->RegisterInterface(static_cast<IMap *>(pEngineMap), false);

	IEngineHttp *pEngineHttp = CreateEngineHttp();
	pKernel->RegisterInterface(pEngineHttp); // IEngineHttp
	pKernel->RegisterInterface(static_cast<IHttp *>(pEngineHttp), false);

	IEngineAntibot *pEngineAntibot = CreateEngineAntibot();
	pKernel->RegisterInterface(pEngineAntibot); // IEngineAntibot
	pKernel->RegisterInterface(static_cast<IAntibot *>(pEngineAntibot), false);

	IGameServer *pGameServer = CreateGameServer();
	pKernel->RegisterInterface(pGameServer);

	pEngine->Init();
	pConsole->Init();
	pConfigManager->Init();

	// register all console commands
	pServer->RegisterCommands();

	// execute autoexec file
	if(pStorage->FileExists(AUTOEXEC_SERVER_FILE, IStorage::TYPE_ALL))
	{
		pConsole->ExecuteFile(AUTOEXEC_SERVER_FILE, IConsole::CLIENT_ID_UNSPECIFIED);
	}
	else // fallback
	{
		pConsole->ExecuteFile(AUTOEXEC_FILE, IConsole::CLIENT_ID_UNSPECIFIED);
	}

	// parse the command line arguments
	if(argc > 1)
		pConsole->ParseArguments(argc - 1, &argv[1]);

	pConfigManager->SetReadOnly("sv_max_clients", true);
	pConfigManager->SetReadOnly("sv_test_cmds", true);
	pConfigManager->SetReadOnly("sv_rescue", true);
	pConfigManager->SetReadOnly("sv_port", true);
	pConfigManager->SetReadOnly("bindaddr", true);
	pConfigManager->SetReadOnly("logfile", true);

	if(g_Config.m_Logfile[0])
	{
		const int Mode = g_Config.m_Logappend ? IOFLAG_APPEND : IOFLAG_WRITE;
		IOHANDLE Logfile = pStorage->OpenFile(g_Config.m_Logfile, Mode, IStorage::TYPE_SAVE_OR_ABSOLUTE);
		if(Logfile)
		{
			auto pFileLogger = log_logger_file(Logfile);
			pFileLogger->SetFilter(CLogFilter{IConsole::ToLogLevelFilter(g_Config.m_Loglevel)});
			pFutureFileLogger->Set(std::move(pFileLogger));
		}
		else
		{
			log_error("server", "failed to open '%s' for logging", g_Config.m_Logfile);
			pFutureFileLogger->Set(log_logger_noop());
		}
	}
	else
	{
		pFutureFileLogger->Set(log_logger_noop());
	}

	auto pServerLogger = std::make_shared<CServerLogger>(pServer);
	pEngine->SetAdditionalLogger(pServerLogger);

	// run the server
	log_trace("server", "initialization finished after %.2fms, starting...", (time_get() - MainStart) * 1000.0f / (float)time_freq());
	int Ret = pServer->Run();

	pServerLogger->OnServerDeletion();
	// free
	delete pKernel;

	MysqlUninit();

	return Ret;
}

#if defined(CONF_PLATFORM_ANDROID)
#if !defined(ANDROID_PACKAGE_NAME_JNI)
#error "ANDROID_PACKAGE_NAME_JNI must define the package name when compiling for Android (using underscores instead of dots, e.g. org_example_app)"
#endif
// Helpers to force macro expansion else the ANDROID_PACKAGE_NAME_JNI macro is not expanded
#define EXPAND_MACRO(x) x
#define JNI_MAKE_NAME(PACKAGE, CLASS, FUNCTION) Java_##PACKAGE##_##CLASS##_##FUNCTION
#define JNI_EXPORTED_FUNCTION(PACKAGE, CLASS, FUNCTION, RETURN_TYPE, ...) \
	extern "C" JNIEXPORT RETURN_TYPE JNICALL EXPAND_MACRO(JNI_MAKE_NAME(PACKAGE, CLASS, FUNCTION))(__VA_ARGS__)

std::mutex AndroidNativeMutex;
std::vector<std::string> vAndroidCommandQueue;

std::vector<std::string> FetchAndroidServerCommandQueue()
{
	std::vector<std::string> vResult;
	{
		const std::unique_lock Lock(AndroidNativeMutex);
		vResult.swap(vAndroidCommandQueue);
	}
	return vResult;
}

JNI_EXPORTED_FUNCTION(ANDROID_PACKAGE_NAME_JNI, NativeServer, runServer, jint, JNIEnv *pEnv, jobject Object, jstring WorkingDirectory, jobjectArray ArgumentsArray)
{
	return android_jni::RunServer(pEnv, WorkingDirectory, ArgumentsArray, GAME_NAME "-Server", [](const char *pDirectory) { return fs_chdir(pDirectory) == 0; }, [](int Count, const char **ppArguments) { return main(Count, ppArguments); });
}

JNI_EXPORTED_FUNCTION(ANDROID_PACKAGE_NAME_JNI, NativeServer, executeCommand, void, JNIEnv *pEnv, jobject Object, jstring Command)
{
	const bool Accepted = android_jni::ReceiveCommand(pEnv, Command, [](std::string Text) {
		const std::unique_lock Lock(AndroidNativeMutex);
		vAndroidCommandQueue.push_back(std::move(Text));
		return true;
	});
	if(!Accepted)
	{
		try
		{
			log_error("android", "Could not receive local server command");
		}
		catch(...)
		{
		}
	}
}

#undef EXPAND_MACRO
#undef JNI_MAKE_NAME
#undef JNI_EXPORTED_FUNCTION
#endif
