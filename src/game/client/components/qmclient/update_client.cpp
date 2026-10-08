// 更新检查、下载、验签和退出安装的 CTClient 适配层；策略由独立更新模块负责。
#include <base/hash.h>
#include <base/log.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/client/qm_storage_mode.h>
#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/update_package.h>
#include <game/client/components/qmclient/update_version.h>
#include <game/client/components/tclient/tclient.h>
#include <game/client/gameclient.h>
#include <game/localization.h>
#include <game/version.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <engine/shared/qm_update.h>

#include <windows.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>

static constexpr int64_t QMCLIENT_UPDATE_RETRY_INTERVAL = 15 * 60;
static constexpr int64_t QMCLIENT_UPDATE_CHECK_INTERVAL = 6 * 60 * 60;
static constexpr int64_t QMCLIENT_UPDATE_MAX_PACKAGE_SIZE = 5LL * 1024 * 1024 * 1024;
#if defined(CONF_FAMILY_WINDOWS)
static constexpr const char *QMCLIENT_INFO_URL = "https://api.github.com/repos/wxj881027/QmClient/releases/latest";
static constexpr const char *QMCLIENT_PREVIEW_INFO_URL = "https://api.github.com/repos/wxj881027/QmClient/releases?per_page=100";
static constexpr int64_t QMCLIENT_UPDATE_MAX_MANIFEST_SIZE = 32 * 1024 * 1024;
#endif

void CTClient::InitUpdateLifecycle()
{
	m_UpdateAutoEnabled = g_Config.m_QmAutoUpdate != 0;
	if(g_Config.m_QmAutoUpdate)
		FetchQmClientUpdateInfo();
}

void CTClient::ShutdownUpdateLifecycle()
{
	ResetUpdateTasks();
	if(!m_UpdateInstallerStarted)
		RemoveUpdateTempFiles();
}

void CTClient::RegisterUpdateCommands()
{
	Console()->Register("qm_update", "?s[action]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUserData) {
		auto *pSelf = static_cast<CTClient *>(pUserData);
		const char *pAction = pResult->NumArguments() > 0 ? pResult->GetString(0) : "check";
		if(str_comp(pAction, "cancel") == 0)
			pSelf->CancelQmClientUpdate();
		else if(str_comp(pAction, "status") == 0)
		{
			const char *pState = pSelf->IsUpdateChecking() ? "checking" : pSelf->IsUpdateDownloading() ? "downloading" :
				pSelf->m_UpdateReady ? "ready" : pSelf->m_UpdateCheckFailed ? "failed" : pSelf->m_FetchedQmClientUpdateInfo ? "checked" : "idle";
			log_info("qm-update", "state=%s network_error=%d", pState, pSelf->m_UpdateNetworkError ? 1 : 0);
		}
		else
		{
			pSelf->RequestQmClientUpdateCheckAndUpdate();
			pSelf->GameClient()->m_Menus.SetActive(true);
			pSelf->GameClient()->m_Menus.ShowQmUpdatePopup();
		} }, this, "Update");
}

void CTClient::TickUpdateLifecycle()
{
#if defined(CONF_FAMILY_WINDOWS)
	const bool AutoUpdateEnabled = g_Config.m_QmAutoUpdate != 0;
	if(AutoUpdateEnabled != m_UpdateAutoEnabled && !m_UpdateShutdownRequested)
	{
		m_UpdateAutoEnabled = AutoUpdateEnabled;
		if(AutoUpdateEnabled)
			m_UpdateNextCheck = 0;
		else
		{
			ResetUpdateTasks();
			RemoveUpdateTempFiles();
			m_UpdateReady = false;
			m_UpdateCheckFailed = false;
			m_UpdateNetworkError = false;
			m_UpdateFailureExitAt = 0;
			m_FetchedQmClientUpdateInfo = false;
			m_UpdatePopupRequested = false;
			m_UpdateDownloadAttempt.Cancel();
		}
	}
#endif
	StartUpdateCheckIfDue();

	PollUpdateProxyRequests();
	PollUpdateRequests();
	if(m_pQmClientUpdateInfoTask)
	{
		const auto State = m_UpdateMetadataRequest.Poll(time_get() / static_cast<double>(time_freq()));
		m_pQmClientUpdateInfoTask = m_UpdateMetadataRequest.Request();
		if(State == qm_update::CUpdateRequest::EState::SUCCEEDED)
		{
			str_copy(g_Config.m_QmUpdateRecentSource, m_UpdateSources.Recent().c_str());
			FinishQmClientUpdateInfo();
			ResetQmClientUpdateInfoTask();
			m_QmClientAutoUpdateAfterCheck = false;
		}
		else if(State == qm_update::CUpdateRequest::EState::FAILED)
		{
			m_UpdateCheckFailed = true;
			m_UpdateNetworkError = !m_UpdateMetadataRequest.RejectedContent();
			m_FetchedQmClientUpdateInfo = false;
			str_copy(m_aUpdateError, "Failed to check for updates");
			log_error("qm-update", "All approved metadata sources failed");
			m_UpdateNextCheck = time_get() + time_freq() * QMCLIENT_UPDATE_RETRY_INTERVAL;
			if(m_UpdateShutdownRequested)
				m_UpdateFailureExitAt = time_get() + 2 * time_freq();
			ResetQmClientUpdateInfoTask();
			m_QmClientAutoUpdateAfterCheck = false;
		}
	}

	if(m_UpdateSurvey.Running() && m_UpdateSurvey.Poll(m_UpdateSources, time_get() / static_cast<double>(time_freq())))
		StartUpdateDownload();
	if(m_pUpdatePackageTask && m_pUpdatePackageSignatureTask && m_pUpdateManifestTask && m_pUpdateManifestSignatureTask &&
		m_pUpdatePackageTask->Done() && m_pUpdatePackageSignatureTask->Done() && m_pUpdateManifestTask->Done() && m_pUpdateManifestSignatureTask->Done() &&
		!m_UpdateReady && !m_UpdateCheckFailed)
	{
		FinishUpdateDownloads();
	}
	if(m_UpdateShutdownRequested && !IsUpdateChecking() && !IsUpdateDownloading() && !m_UpdateReady && !m_UpdateCheckFailed)
	{
		m_UpdateShutdownRequested = false;
		Client()->Quit();
	}

	if(m_UpdateShutdownRequested && (m_UpdateReady || m_UpdateCheckFailed) &&
		(!m_UpdateCheckFailed || m_UpdateFailureExitAt == 0 || time_get() >= m_UpdateFailureExitAt))
	{
		if(m_UpdateReady && !m_UpdateInstallerStarted)
		{
			if(!LaunchUpdateInstaller())
			{
				m_UpdateReady = false;
				m_UpdateCheckFailed = true;
				m_UpdateFailureExitAt = time_get() + 2 * time_freq();
				RemoveUpdateTempFiles();
				m_UpdatePopupRequested = true;
			}
		}
		if((!m_UpdateReady || m_UpdateInstallerStarted || m_UpdateCheckFailed) &&
			(!m_UpdateCheckFailed || m_UpdateFailureExitAt == 0 || time_get() >= m_UpdateFailureExitAt))
		{
			m_UpdateShutdownRequested = false;
			Client()->Quit();
		}
	}
}

bool CTClient::NeedQmClientUpdate()
{
	return str_comp(m_aQmClientLatestVersionStr, "0") != 0;
}

void CTClient::RequestQmClientUpdateCheckAndUpdate()
{
	if(IsUpdateChecking() || IsUpdateDownloading() || m_UpdateReady || m_UpdateInstallerStarted)
		return;

	m_QmClientAutoUpdateAfterCheck = true;
	m_FetchedQmClientUpdateInfo = false;
	m_UpdateCheckFailed = false;
	FetchQmClientUpdateInfo();
}

void CTClient::StartUpdateSourceSurvey()
{
	char aSetupMarker[IO_MAX_PATH_LENGTH];
	Storage()->GetBinaryPath("QmClient-Setup.ini", aSetupMarker, sizeof(aSetupMarker));
	m_UpdateUseSetup = UseQmClientSetupUpdate(fs_is_file(aSetupMarker), m_UpdateRelease, IsQmClientPortableBuild());
	if(!CanDownloadQmClientRelease(m_UpdateRelease, fs_is_file(aSetupMarker), IsQmClientPortableBuild()))
		return;
	const char *pSignatureUrl = m_UpdateUseSetup ? m_UpdateRelease.m_aSetupSignatureUrl : m_UpdateRelease.m_aPackageSignatureUrl;
	const double Now = time_get() / static_cast<double>(time_freq());
	const auto StartProbe = [this](const std::string &Url, bool Direct) -> std::shared_ptr<IHttpRequest> {
		std::shared_ptr<IHttpRequest> pRequest = HttpGet(Url.c_str());
		pRequest->Timeout(CTimeout{5000, 10000, 1, 10});
		pRequest->MaxResponseSize(64);
		pRequest->LogProgress(HTTPLOG::FAILURE);
		if(Direct)
		{
			pRequest->Proxy("");
			Http()->Run(pRequest);
		}
		else
			RunUpdateHttp(pRequest);
		return pRequest;
	};
	const char *pPackageUrl = m_UpdateUseSetup ? m_UpdateRelease.m_aSetupUrl : m_UpdateRelease.m_aPackageUrl;
	const auto StartSample = [this](const std::string &Url, bool Direct) -> std::shared_ptr<IHttpRequest> {
		std::shared_ptr<IHttpRequest> pRequest = HttpGet(Url.c_str());
		pRequest->Timeout(CTimeout{5000, 8000, 1, 5});
		pRequest->MaxResponseSize(QMCLIENT_UPDATE_MAX_PACKAGE_SIZE);
		pRequest->ResponseSample(qm_update::CSourceSurvey::SAMPLE_BYTES);
		pRequest->LogProgress(HTTPLOG::FAILURE);
		if(Direct)
		{
			pRequest->Proxy("");
			Http()->Run(pRequest);
		}
		else
			RunUpdateHttp(pRequest);
		return pRequest;
	};
	m_UpdateSurvey.Begin(m_UpdateSources.Candidates(pSignatureUrl, qm_update::RELEASE, Now), pSignatureUrl, Now, [StartProbe](const std::string &Url) { return StartProbe(Url, false); }, [StartProbe](const std::string &Url) { return StartProbe(Url, true); }, pPackageUrl, StartSample);
	if(!m_UpdateSurvey.Running())
		StartUpdateDownload();
}

void CTClient::StartUpdateDownload(bool NextSource)
{
#if !defined(CONF_FAMILY_WINDOWS)
	return;
#else
	const std::shared_ptr<IHttpRequest> apRequests[] = {m_pUpdatePackageTask, m_pUpdatePackageSignatureTask, m_pUpdateManifestTask, m_pUpdateManifestSignatureTask};
	if(!qm_update::CanStartDownloadBatch(m_UpdateSurvey.Running(), NextSource, apRequests))
		return;

	if(!NextSource)
	{
		m_UpdateDownloadDirect = false;
		m_UpdateDownloadAttempt.Begin(m_UpdateSurvey.Candidates());
	}
	if(!m_UpdateDownloadAttempt.Current())
	{
		m_UpdateCheckFailed = true;
		m_UpdateNetworkError = true;
		str_copy(m_aUpdateError, "Update sources are temporarily unavailable");
		return;
	}
	if(!NextSource)
		m_UpdateDownloadDirect = m_UpdateSurvey.Direct(*m_UpdateDownloadAttempt.Current());
	m_UpdateSwitchForSpeed = false;
	m_UpdateSpeedMonitor.Begin(time_get() / static_cast<double>(time_freq()));
	ResetUpdateDownloadTasks();
	RemoveUpdateTempFiles();
	m_UpdateReady = false;
	m_UpdateCheckFailed = false;
	m_UpdateFailureNoticeShown = false;
	m_UpdateFailureExitAt = 0;
	// 安装版使用 Setup 更新卸载记录；便携版和旧 Release 保留 ZIP 流程。
	char aSetupMarker[IO_MAX_PATH_LENGTH];
	Storage()->GetBinaryPath("QmClient-Setup.ini", aSetupMarker, sizeof(aSetupMarker));
	m_UpdateUseSetup = UseQmClientSetupUpdate(fs_is_file(aSetupMarker), m_UpdateRelease, IsQmClientPortableBuild());
	// 每次换源使用独立目录，旧请求异步关闭时不会覆盖新请求的文件。
	str_format(m_aUpdateAssetDirectory, sizeof(m_aUpdateAssetDirectory), "qmclient/update-%d-%llu", pid(), static_cast<unsigned long long>(++m_UpdateDownloadSession));
	if(!Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE) || !Storage()->CreateFolder(m_aUpdateAssetDirectory, IStorage::TYPE_SAVE))
	{
		m_UpdateCheckFailed = true;
		str_copy(m_aUpdateError, "Failed to create the update working directory");
		return;
	}
	const auto TempPath = [&](char *pDestination, size_t Size, const char *pUrl) {
		const char *pName = str_rchr(pUrl, '/');
		str_format(pDestination, Size, "%s/%s.%d.tmp", m_aUpdateAssetDirectory, pName ? pName + 1 : "invalid", pid());
	};
	TempPath(m_aUpdatePackageTmp, sizeof(m_aUpdatePackageTmp), m_UpdateUseSetup ? m_UpdateRelease.m_aSetupUrl : m_UpdateRelease.m_aPackageUrl);
	TempPath(m_aUpdatePackageSignatureTmp, sizeof(m_aUpdatePackageSignatureTmp), m_UpdateUseSetup ? m_UpdateRelease.m_aSetupSignatureUrl : m_UpdateRelease.m_aPackageSignatureUrl);
	TempPath(m_aUpdateManifestTmp, sizeof(m_aUpdateManifestTmp), m_UpdateUseSetup ? m_UpdateRelease.m_aSetupManifestUrl : m_UpdateRelease.m_aManifestUrl);
	TempPath(m_aUpdateManifestSignatureTmp, sizeof(m_aUpdateManifestSignatureTmp), m_UpdateUseSetup ? m_UpdateRelease.m_aSetupManifestSignatureUrl : m_UpdateRelease.m_aManifestSignatureUrl);

	const auto StartDownload = [&](std::shared_ptr<IHttpRequest> &pTask, const char *pUrl, const char *pDestination, int64_t MaxResponseSize) {
		const std::string Url = m_UpdateDownloadAttempt.Current()->m_Prefix + pUrl;
		pTask = HttpGet(Url.c_str());
		pTask->Timeout(CTimeout{5000, 0, 1, 20});
		pTask->MaxResponseSize(MaxResponseSize);
		pTask->SkipByFileTime(false);
		pTask->LogProgress(HTTPLOG::FAILURE);
		pTask->WriteToFile(Storage(), pDestination, IStorage::TYPE_SAVE);
		if(m_UpdateDownloadDirect)
		{
			pTask->Proxy("");
			Http()->Run(pTask);
		}
		else
			RunUpdateHttp(pTask);
	};
	for(int Index = 0; Index < 4; ++Index)
		m_UpdateDeadlines[Index].Begin(time_get() / static_cast<double>(time_freq()));
	const std::string PackageUrl = m_UpdateDownloadAttempt.Current()->m_Prefix + (m_UpdateUseSetup ? m_UpdateRelease.m_aSetupUrl : m_UpdateRelease.m_aPackageUrl);
	m_pUpdatePackageDownload = std::make_shared<qm_update::CPackageDownload>(PackageUrl, Storage(), m_aUpdatePackageTmp, QMCLIENT_UPDATE_MAX_PACKAGE_SIZE, [this](const std::shared_ptr<IHttpRequest> &Request) {
			if(m_UpdateDownloadDirect)
			{
				Request->Proxy("");
				Http()->Run(Request);
			}
			else
				RunUpdateHttp(Request); }, [this](std::shared_ptr<IJob> Job) { Engine()->AddJob(std::move(Job)); });
	m_pUpdatePackageTask = m_pUpdatePackageDownload;
	StartDownload(m_pUpdatePackageSignatureTask, m_UpdateUseSetup ? m_UpdateRelease.m_aSetupSignatureUrl : m_UpdateRelease.m_aPackageSignatureUrl, m_aUpdatePackageSignatureTmp, 64);
	StartDownload(m_pUpdateManifestTask, m_UpdateUseSetup ? m_UpdateRelease.m_aSetupManifestUrl : m_UpdateRelease.m_aManifestUrl, m_aUpdateManifestTmp, QMCLIENT_UPDATE_MAX_MANIFEST_SIZE);
	StartDownload(m_pUpdateManifestSignatureTask, m_UpdateUseSetup ? m_UpdateRelease.m_aSetupManifestSignatureUrl : m_UpdateRelease.m_aManifestSignatureUrl, m_aUpdateManifestSignatureTmp, 64);
#endif
}

void CTClient::ResetUpdateDownloadTasks()
{
	const auto ResetTask = [](std::shared_ptr<IHttpRequest> &pTask) {
		if(pTask)
			pTask->Abort();
		pTask = nullptr;
	};
	ResetTask(m_pUpdatePackageTask);
	m_pUpdatePackageDownload.reset();
	ResetTask(m_pUpdatePackageSignatureTask);
	ResetTask(m_pUpdateManifestTask);
	ResetTask(m_pUpdateManifestSignatureTask);
}

void CTClient::RemoveUpdateTempFiles()
{
	if(m_aUpdatePackageTmp[0] != '\0')
		Storage()->RemoveFile(m_aUpdatePackageTmp, IStorage::TYPE_SAVE);
	if(m_aUpdatePackageSignatureTmp[0] != '\0')
		Storage()->RemoveFile(m_aUpdatePackageSignatureTmp, IStorage::TYPE_SAVE);
	if(m_aUpdateManifestTmp[0] != '\0')
		Storage()->RemoveFile(m_aUpdateManifestTmp, IStorage::TYPE_SAVE);
	if(m_aUpdateManifestSignatureTmp[0] != '\0')
		Storage()->RemoveFile(m_aUpdateManifestSignatureTmp, IStorage::TYPE_SAVE);
	if(m_aUpdateInstallerTmp[0] != '\0' && !m_UpdateInstallerStarted)
	{
		Storage()->RemoveFile(m_aUpdateInstallerTmp, IStorage::TYPE_ABSOLUTE);
	}
	if(!m_UpdateInstallerStarted)
		m_aUpdateInstallerTmp[0] = '\0';
	m_aUpdatePackageTmp[0] = '\0';
	m_aUpdatePackageSignatureTmp[0] = '\0';
	m_aUpdateManifestTmp[0] = '\0';
	m_aUpdateManifestSignatureTmp[0] = '\0';
}

void CTClient::ResetUpdateTasks()
{
	for(auto &Pending : m_vUpdateProxyRequests)
		Pending.m_pRequest->Abort();
	m_vUpdateProxyRequests.clear();
	m_UpdateSurvey.Cancel();
	ResetQmClientUpdateInfoTask();
	ResetUpdateDownloadTasks();
}

void CTClient::ResetQmClientUpdateInfoTask()
{
	m_UpdateMetadataRequest.Cancel();
	if(m_pQmClientUpdateInfoTask)
	{
		m_pQmClientUpdateInfoTask->Abort();
		m_pQmClientUpdateInfoTask = NULL;
	}
}

std::shared_ptr<IHttpRequest> CTClient::CreateUpdateInfoRequest(const std::string &Url, bool Direct)
{
	log_info("qm-update", "Checking approved metadata source: %s%s", Url.c_str(), Direct ? " (direct fallback)" : "");
	std::shared_ptr<IHttpRequest> Request = HttpGet(Url.c_str());
	Request->Timeout(CTimeout{5000, 30000, 1, 10});
	Request->MaxResponseSize(4 * 1024 * 1024);
	Request->LogProgress(HTTPLOG::FAILURE);
	if(Direct)
	{
		Request->Proxy("");
		Http()->Run(Request);
	}
	else
		RunUpdateHttp(Request);
	return Request;
}

void CTClient::FetchQmClientUpdateInfo()
{
#if !defined(CONF_FAMILY_WINDOWS) || !defined(CONF_ARCH_AMD64)
	m_UpdateCheckFailed = true;
	m_UpdatePopupRequested = true;
	m_UpdateNextCheck = time_get() + time_freq() * QMCLIENT_UPDATE_RETRY_INTERVAL;
	str_copy(m_aUpdateError, "Automatic updates require Windows x64");
	return;
#else
	if(m_pQmClientUpdateInfoTask && !m_pQmClientUpdateInfoTask->Done())
		return;
	m_FetchedQmClientUpdateInfo = false;
	m_UpdateCheckFailed = false;
	m_UpdateFailureNoticeShown = false;
	m_UpdateFailureExitAt = 0;
	m_aUpdateError[0] = '\0';
	m_UpdateNextCheck = time_get() + time_freq() * QMCLIENT_UPDATE_CHECK_INTERVAL;
	m_UpdatePopupRequested = true;
	m_UpdateRelease = {};
	str_copy(m_aQmClientLatestVersionStr, "0");
	m_UpdateSources.SetRecent(g_Config.m_QmUpdateRecentSource);
	m_UpdateMetadataRequest.Begin(m_UpdateSources, QMCLIENT_IS_DEVELOPMENT_BUILD ? QMCLIENT_PREVIEW_INFO_URL : QMCLIENT_INFO_URL, qm_update::API, time_get() / static_cast<double>(time_freq()), [this](const std::string &Url) -> std::shared_ptr<IHttpRequest> { return CreateUpdateInfoRequest(Url); }, [](const IHttpRequest &Request) {
			unsigned char *pResult = nullptr;
			size_t Size = 0;
			Request.Result(&pResult, &Size);
			SQmClientUpdateRelease Release;
			char aError[256];
			return ParseQmClientReleaseInfo(reinterpret_cast<const char *>(pResult), Size, QMCLIENT_VERSION, Release, aError, sizeof(aError), QMCLIENT_IS_DEVELOPMENT_BUILD, IsQmClientPortableBuild()); }, [this](const std::string &Url) { return CreateUpdateInfoRequest(Url, true); });
	m_pQmClientUpdateInfoTask = m_UpdateMetadataRequest.Request();
	if(!m_pQmClientUpdateInfoTask)
	{
		m_UpdateCheckFailed = true;
		m_UpdateNetworkError = true;
		m_UpdateNextCheck = time_get() + time_freq() * QMCLIENT_UPDATE_RETRY_INTERVAL;
		str_copy(m_aUpdateError, "Update sources are temporarily unavailable");
	}
#endif
}

void CTClient::CancelQmClientUpdate()
{
	if(m_UpdateInstallerStarted)
		return;
	log_info("qm-update", "Update cancelled");
	m_UpdateMetadataRequest.Cancel();
	m_UpdateDownloadAttempt.Cancel();
	ResetUpdateTasks();
	RemoveUpdateTempFiles();
	m_QmClientAutoUpdateAfterCheck = false;
	m_UpdateReady = false;
	m_UpdatePopupRequested = false;
	m_UpdateShutdownRequested = false;
	m_UpdateNextCheck = time_get() + time_freq() * QMCLIENT_UPDATE_CHECK_INTERVAL;
}

void CTClient::RunUpdateHttp(std::shared_ptr<IHttpRequest> pRequest)
{
	const std::string Url = pRequest->Url();
	// 镜像固定直连，显式空代理同时屏蔽 curl 的环境代理和系统代理。
	if(!qm_update::UpdateUsesSystemProxy(Url))
	{
		pRequest->Proxy("");
		Http()->Run(pRequest);
		return;
	}
#if defined(CONF_FAMILY_WINDOWS)
	if(!qm_update::HasEnvironmentProxy())
	{
		auto pJob = std::make_shared<qm_update::CSystemProxyJob>(Url);
		m_vUpdateProxyRequests.push_back({pJob, pRequest, time_get() / static_cast<double>(time_freq())});
		Engine()->AddJob(pJob);
		return;
	}
#endif
	Http()->Run(pRequest);
}

void CTClient::PollUpdateProxyRequests()
{
	const double Now = time_get() / static_cast<double>(time_freq());
	for(auto It = m_vUpdateProxyRequests.begin(); It != m_vUpdateProxyRequests.end();)
	{
		if(qm_update::CompleteAbortedUpdateRequest(*Http(), It->m_pRequest))
		{
			It = m_vUpdateProxyRequests.erase(It);
			continue;
		}
		if(It->m_pJob->State() != IJob::STATE_DONE && Now - It->m_Start < 8)
		{
			++It;
			continue;
		}
		// 只在主线程移交 HTTP；后台任务不持有 Http 指针，退出时不存在晚到 Run。
		if(It->m_pJob->State() == IJob::STATE_DONE && It->m_pJob->HasDecision())
			It->m_pRequest->Proxy(It->m_pJob->Proxy().c_str());
		m_UpdateSurvey.BeginTransfer(It->m_pRequest, Now);
		if(It->m_pRequest == m_UpdateMetadataRequest.Request())
			m_UpdateMetadataRequest.BeginTransfer(Now);
		const std::shared_ptr<IHttpRequest> apTasks[] = {m_pUpdatePackageTask, m_pUpdatePackageSignatureTask, m_pUpdateManifestTask, m_pUpdateManifestSignatureTask};
		for(size_t Index = 0; Index < std::size(apTasks); ++Index)
			if(It->m_pRequest == apTasks[Index])
			{
				m_UpdateDeadlines[Index].Begin(Now);
				if(Index == 0)
					m_UpdateSpeedMonitor.Begin(Now);
			}
		Http()->Run(It->m_pRequest);
		It = m_vUpdateProxyRequests.erase(It);
	}
}

void CTClient::PollUpdateRequests()
{
	const double Now = time_get() / static_cast<double>(time_freq());
	if(m_pUpdatePackageDownload)
	{
		m_pUpdatePackageDownload->Poll(Now);
		if(m_pUpdatePackageDownload->TakeSpeedWindowReset())
			m_UpdateSpeedMonitor.Begin(Now);
	}
	// 包的分段有各自的传输期限；后台合并不受网络无进展期限影响。
	const std::shared_ptr<IHttpRequest> apRequests[] = {m_pUpdatePackageDownload ? nullptr : m_pUpdatePackageTask, m_pUpdatePackageSignatureTask, m_pUpdateManifestTask, m_pUpdateManifestSignatureTask};
	qm_update::PollDownloadBatch(apRequests, m_UpdateDeadlines, Now);
	bool Failed = m_pUpdatePackageTask && m_pUpdatePackageTask->Done() && m_pUpdatePackageTask->State() != EHttpState::DONE;
	for(const auto &Request : apRequests)
		Failed |= Request && Request->Done() && Request->State() != EHttpState::DONE;
	if(Failed && m_pUpdatePackageTask && !m_pUpdatePackageTask->Done())
		m_pUpdatePackageTask->Abort();
	if(Failed)
		for(const auto &Request : apRequests)
			if(Request && !Request->Done())
				Request->Abort();
	if(m_pUpdatePackageDownload && m_pUpdatePackageDownload->Downloading() && m_UpdateDownloadAttempt.Current() &&
		m_UpdateSpeedMonitor.ShouldSwitch(Now, m_pUpdatePackageTask->Current(), m_pUpdatePackageTask->Size(), m_UpdateSurvey.Speed(m_UpdateDownloadAttempt.NextCandidate()), m_UpdateDownloadAttempt.Current()->m_Prefix.empty()))
	{
		m_UpdateSwitchForSpeed = true;
		log_info("qm-update", "Package remained below 100 KiB/s for 5 seconds; switching approved source");
		m_pUpdatePackageTask->Abort();
		for(const auto &pRequest : apRequests)
			if(pRequest && !pRequest->Done())
				pRequest->Abort();
	}
}

void CTClient::FinishQmClientUpdateInfo()
{
	unsigned char *pResult = nullptr;
	size_t ResultSize = 0;
	m_pQmClientUpdateInfoTask->Result(&pResult, &ResultSize);
	char aError[256];
	SQmClientUpdateRelease Release;
	if(!ParseQmClientReleaseInfo(reinterpret_cast<const char *>(pResult), ResultSize, QMCLIENT_VERSION, Release, aError, sizeof(aError), QMCLIENT_IS_DEVELOPMENT_BUILD, IsQmClientPortableBuild()))
	{
		m_FetchedQmClientUpdateInfo = false;
		m_UpdateCheckFailed = true;
		m_UpdateNetworkError = false;
		m_UpdateNextCheck = time_get() + time_freq() * QMCLIENT_UPDATE_RETRY_INTERVAL;
		str_copy(m_aUpdateError, aError, sizeof(m_aUpdateError));
		log_error("qm-update", "release metadata rejected: %s", m_aUpdateError);
		return;
	}
	m_UpdateRelease = Release;
	char aSetupMarker[IO_MAX_PATH_LENGTH];
	Storage()->GetBinaryPath("QmClient-Setup.ini", aSetupMarker, sizeof(aSetupMarker));
	m_UpdateUseSetup = UseQmClientSetupUpdate(fs_is_file(aSetupMarker), Release, IsQmClientPortableBuild());
	str_copy(m_aQmClientLatestVersionStr, Release.m_NewVersion ? Release.m_aVersion : "0", sizeof(m_aQmClientLatestVersionStr));
	m_FetchedQmClientUpdateInfo = true;
	m_UpdateCheckFailed = false;
	m_UpdateNetworkError = false;
	++m_UpdateInfoRevision;
	log_info("qm-update", "release_info version=%s newer=%d package_available=%d", Release.m_aVersion, Release.m_NewVersion, Release.m_PackageAvailable);
	if(Release.m_NewVersion)
		StartUpdateSourceSurvey();
}

void CTClient::StartUpdateCheckIfDue()
{
#if defined(CONF_FAMILY_WINDOWS)
	if(!g_Config.m_QmAutoUpdate || m_UpdateShutdownRequested || m_UpdateReady || IsUpdateChecking() || IsUpdateDownloading())
		return;
	if(m_UpdateNextCheck == 0 || time_get() >= m_UpdateNextCheck)
		FetchQmClientUpdateInfo();
#endif
}

void CTClient::FinishUpdateDownloads()
{
#if !defined(CONF_FAMILY_WINDOWS)
	return;
#else
	auto Fail = [&](const char *pMessage, bool Retryable = true, bool NetworkError = false) {
		if(Retryable && m_UpdateDownloadAttempt.Current())
		{
			double RetryAfter = 0;
			for(const auto &pTask : {m_pUpdatePackageTask, m_pUpdatePackageSignatureTask, m_pUpdateManifestTask, m_pUpdateManifestSignatureTask})
				if(pTask)
					RetryAfter = std::max(RetryAfter, qm_update::SourceRetryDelay(pTask.get()));
			const std::shared_ptr<IHttpRequest> apRequests[] = {m_pUpdatePackageTask, m_pUpdatePackageSignatureTask, m_pUpdateManifestTask, m_pUpdateManifestSignatureTask};
			if(!m_UpdateSwitchForSpeed && qm_update::CanRetryOfficialDirect(m_UpdateDownloadAttempt.Current(), m_UpdateDownloadDirect, apRequests))
			{
				m_UpdateDownloadDirect = true;
				log_info("qm-update", "Official proxy route failed; trying one direct route");
				StartUpdateDownload(true);
				return;
			}
			m_UpdateSources.Failed(*m_UpdateDownloadAttempt.Current(), time_get() / static_cast<double>(time_freq()), RetryAfter);
			if(m_UpdateDownloadAttempt.Next())
			{
				m_UpdateDownloadDirect = m_UpdateSurvey.Direct(*m_UpdateDownloadAttempt.Current());
				log_info("qm-update", "Trying next approved update source after: %s", pMessage);
				StartUpdateDownload(true);
				return;
			}
		}
		m_UpdateReady = false;
		m_UpdateCheckFailed = true;
		m_UpdateNetworkError = NetworkError;
		m_UpdateNextCheck = time_get() + time_freq() * QMCLIENT_UPDATE_RETRY_INTERVAL;
		str_copy(m_aUpdateError, pMessage != nullptr && pMessage[0] != '\0' ? pMessage : "Update failed", sizeof(m_aUpdateError));
		log_error("qm-update", "update download or validation failed: %s", m_aUpdateError);
		ResetUpdateDownloadTasks();
		RemoveUpdateTempFiles();
		if(m_UpdateShutdownRequested)
			m_UpdateFailureExitAt = time_get() + 2 * time_freq();
		m_UpdatePopupRequested = true;
	};

	const auto IsSuccessful = [](const std::shared_ptr<IHttpRequest> &pTask) {
		return pTask && pTask->State() == EHttpState::DONE && pTask->StatusCode() == 200;
	};
	if(!IsSuccessful(m_pUpdatePackageTask) || !IsSuccessful(m_pUpdatePackageSignatureTask) ||
		!IsSuccessful(m_pUpdateManifestTask) || !IsSuccessful(m_pUpdateManifestSignatureTask))
	{
		const bool LocalError = m_pUpdatePackageDownload && m_pUpdatePackageDownload->LocalError();
		Fail(LocalError ? "Failed to assemble the downloaded update package" : "All approved update sources failed to download", !LocalError, !LocalError);
		return;
	}

	using TUpdateData = std::unique_ptr<unsigned char, void (*)(void *)>;
	TUpdateData ManifestData(nullptr, std::free);
	TUpdateData ManifestSignatureData(nullptr, std::free);
	TUpdateData PackageSignatureData(nullptr, std::free);
	unsigned ManifestSize = 0;
	unsigned ManifestSignatureSize = 0;
	unsigned PackageSignatureSize = 0;
	void *pRawData = nullptr;
	if(!Storage()->ReadFile(m_aUpdateManifestTmp, IStorage::TYPE_SAVE, &pRawData, &ManifestSize))
	{
		Fail("Failed to read the downloaded update manifest");
		return;
	}
	ManifestData.reset(static_cast<unsigned char *>(pRawData));
	pRawData = nullptr;
	if(!Storage()->ReadFile(m_aUpdateManifestSignatureTmp, IStorage::TYPE_SAVE, &pRawData, &ManifestSignatureSize))
	{
		Fail("Failed to read the downloaded manifest signature");
		return;
	}
	ManifestSignatureData.reset(static_cast<unsigned char *>(pRawData));
	pRawData = nullptr;
	if(!Storage()->ReadFile(m_aUpdatePackageSignatureTmp, IStorage::TYPE_SAVE, &pRawData, &PackageSignatureSize))
	{
		Fail("Failed to read the downloaded package signature");
		return;
	}
	PackageSignatureData.reset(static_cast<unsigned char *>(pRawData));

	char aError[256] = "";
	uint64_t SignedPackageSize = 0;
	uint8_t aSignedPackageDigest[SHA256_DIGEST_LENGTH] = {};
	const auto VerifyManifest = m_UpdateUseSetup ? qm_update_verify_setup_manifest : qm_update_verify_manifest_package;
	if(!VerifyManifest(ManifestData.get(), ManifestSize, ManifestSignatureData.get(), ManifestSignatureSize,
		   &SignedPackageSize, aSignedPackageDigest, sizeof(aSignedPackageDigest), aError, sizeof(aError)))
	{
		Fail(aError);
		return;
	}

	SQmClientUpdateManifest Manifest;
	if(!ParseQmClientUpdateManifest(reinterpret_cast<const char *>(ManifestData.get()), ManifestSize, QMCLIENT_VERSION, Manifest, aError, sizeof(aError), QMCLIENT_IS_DEVELOPMENT_BUILD, m_UpdateUseSetup, IsQmClientPortableBuild(), m_UpdateRelease.m_SevenZip) ||
		str_comp(Manifest.m_aVersion, m_UpdateRelease.m_aVersion) != 0 ||
		Manifest.m_PackageSize != SignedPackageSize || mem_comp(Manifest.m_PackageSha256.data, aSignedPackageDigest, sizeof(aSignedPackageDigest)) != 0)
	{
		Fail(aError[0] != '\0' ? aError : "Update manifest does not match the selected release");
		return;
	}

	const SHA256_DIGEST ActualDigest = m_pUpdatePackageTask->ResultSha256();
	IOHANDLE PackageFile = Storage()->OpenFile(m_aUpdatePackageTmp, IOFLAG_READ, IStorage::TYPE_SAVE);
	const int64_t ActualSize = PackageFile ? io_length(PackageFile) : -1;
	if(PackageFile)
		io_close(PackageFile);
	if(ActualSize < 0 || static_cast<uint64_t>(ActualSize) != SignedPackageSize || ActualDigest != Manifest.m_PackageSha256)
	{
		Fail("Downloaded update package size or SHA-256 is invalid");
		return;
	}
	if(!qm_update_verify_package_digest(ActualDigest.data, sizeof(ActualDigest.data), PackageSignatureData.get(), PackageSignatureSize, aError, sizeof(aError)))
	{
		Fail(aError);
		return;
	}
	if(!Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE))
	{
		Fail("Failed to create the update working directory", false);
		return;
	}
	char aPackagePath[IO_MAX_PATH_LENGTH] = "";
	char aInstallerPath[IO_MAX_PATH_LENGTH] = "";
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, m_aUpdatePackageTmp, aPackagePath, sizeof(aPackagePath));
	if(m_UpdateUseSetup)
	{
		// 下载临时名以 .tmp 结尾，Windows 执行带参数的程序必须保留 .exe 扩展名。
		char aSetupRelativePath[IO_MAX_PATH_LENGTH];
		str_format(aSetupRelativePath, sizeof(aSetupRelativePath), "%s/QmClient-Setup-%d.exe", m_aUpdateAssetDirectory, pid());
		if(!Storage()->RenameFile(m_aUpdatePackageTmp, aSetupRelativePath, IStorage::TYPE_SAVE))
		{
			Fail("Failed to prepare the Setup executable", false);
			return;
		}
		str_copy(m_aUpdatePackageTmp, aSetupRelativePath);
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, m_aUpdatePackageTmp, aInstallerPath, sizeof(aInstallerPath));
		char aHelperDirectory[IO_MAX_PATH_LENGTH];
		str_format(aHelperDirectory, sizeof(aHelperDirectory), "%s/qmclient", m_aUpdateAssetDirectory);
		if(!Storage()->CreateFolder(aHelperDirectory, IStorage::TYPE_SAVE))
		{
			Fail("Failed to create the Setup session directory", false);
			return;
		}
		char aHelperRelativePath[IO_MAX_PATH_LENGTH];
		str_format(aHelperRelativePath, sizeof(aHelperRelativePath), "%s/QmClient-Updater-%d.exe", aHelperDirectory, pid());
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, aHelperRelativePath, aInstallerPath, sizeof(aInstallerPath));
		char aInstalledHelper[IO_MAX_PATH_LENGTH];
		Storage()->GetBinaryPathAbsolute("QmClient-Updater.exe", aInstalledHelper, sizeof(aInstalledHelper));
		str_copy(m_aUpdateInstallerTmp, aInstallerPath);
		std::error_code CopyError;
		if(!std::filesystem::copy_file(std::filesystem::u8path(aInstalledHelper), std::filesystem::u8path(aInstallerPath), CopyError))
		{
			Fail("Failed to prepare the Setup session helper", false);
			return;
		}
		m_UpdateSources.Succeeded(*m_UpdateDownloadAttempt.Current());
		str_copy(g_Config.m_QmUpdateRecentSource, m_UpdateSources.Recent().c_str());
		m_UpdateReady = true;
		m_UpdateCheckFailed = false;
		m_aUpdateError[0] = '\0';
		ResetUpdateDownloadTasks();
		m_UpdatePopupRequested = true;
		return;
	}
	char aBootstrapDirectory[IO_MAX_PATH_LENGTH];
	str_format(aBootstrapDirectory, sizeof(aBootstrapDirectory), "%s/qmclient", m_aUpdateAssetDirectory);
	if(!Storage()->CreateFolder(aBootstrapDirectory, IStorage::TYPE_SAVE))
	{
		Fail("Failed to create the update working directory", false);
		return;
	}
	str_format(m_aUpdateInstallerTmp, sizeof(m_aUpdateInstallerTmp), "%s/qmclient/QmClient-Updater-%d.exe", m_aUpdateAssetDirectory, pid());
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, m_aUpdateInstallerTmp, aInstallerPath, sizeof(aInstallerPath));
	str_copy(m_aUpdateInstallerTmp, aInstallerPath, sizeof(m_aUpdateInstallerTmp));
	Storage()->RemoveFile(aInstallerPath, IStorage::TYPE_ABSOLUTE);
	if(!qm_update_extract_bootstrap_updater(aPackagePath, ManifestData.get(), ManifestSize,
		   ManifestSignatureData.get(), ManifestSignatureSize, aInstallerPath, aError, sizeof(aError)))
	{
		Fail(aError);
		return;
	}

	m_UpdateSources.Succeeded(*m_UpdateDownloadAttempt.Current());
	str_copy(g_Config.m_QmUpdateRecentSource, m_UpdateSources.Recent().c_str());
	m_UpdateReady = true;
	m_UpdateCheckFailed = false;
	m_aUpdateError[0] = '\0';
	ResetUpdateDownloadTasks();
	m_UpdatePopupRequested = true;
#endif
}

bool CTClient::LaunchUpdateInstaller()
{
#if !defined(CONF_FAMILY_WINDOWS)
	return false;
#else
	if(!m_UpdateReady || m_UpdateInstallerStarted)
		return m_UpdateInstallerStarted;

	char aPackagePath[IO_MAX_PATH_LENGTH] = "";
	char aPackageSignaturePath[IO_MAX_PATH_LENGTH] = "";
	char aManifestPath[IO_MAX_PATH_LENGTH] = "";
	char aManifestSignaturePath[IO_MAX_PATH_LENGTH] = "";
	char aInstallPath[IO_MAX_PATH_LENGTH] = "";
	if(!fs_is_file(m_aUpdateInstallerTmp))
		return false;

	Storage()->GetCompletePath(IStorage::TYPE_SAVE, m_aUpdatePackageTmp, aPackagePath, sizeof(aPackagePath));
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, m_aUpdatePackageSignatureTmp, aPackageSignaturePath, sizeof(aPackageSignaturePath));
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, m_aUpdateManifestTmp, aManifestPath, sizeof(aManifestPath));
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, m_aUpdateManifestSignatureTmp, aManifestSignaturePath, sizeof(aManifestSignaturePath));
	Storage()->GetBinaryPathAbsolute(PLAT_CLIENT_EXEC, aInstallPath, sizeof(aInstallPath));
	if(fs_parent_dir(aInstallPath) != 0)
	{
		RemoveUpdateTempFiles();
		return false;
	}

	char aPid[32];
	str_format(aPid, sizeof(aPid), "%d", pid());
	std::vector<const char *> vArguments = {
		"--parent-pid",
		aPid,
		"--package",
		aPackagePath,
		"--package-signature",
		aPackageSignaturePath,
		"--manifest",
		aManifestPath,
		"--manifest-signature",
		aManifestSignaturePath,
		"--install",
		aInstallPath,
	};
	// 两种安装路径共用会话 owner；Setup 的退出后复验、等待与清理由 helper 负责。
	if(m_UpdateUseSetup)
		vArguments.push_back("--setup");
	const PROCESS Process = shell_execute(m_aUpdateInstallerTmp, EShellExecuteWindowState::FOREGROUND, vArguments.data(), vArguments.size());
	if(Process == INVALID_PROCESS)
	{
		RemoveUpdateTempFiles();
		return false;
	}
	CloseHandle(static_cast<HANDLE>(Process));
	m_UpdateInstallerStarted = true;
	return true;
#endif
}

bool CTClient::PrepareForShutdown(bool Force)
{
#if defined(CONF_FAMILY_WINDOWS)
	if(m_UpdateInstallerStarted)
		return false;
	if(Force && m_UpdateShutdownRequested && (m_UpdateReady || IsUpdateChecking() || IsUpdateDownloading()))
	{
		ResetUpdateTasks();
		RemoveUpdateTempFiles();
		return false;
	}
	if(m_UpdateReady)
	{
		m_UpdateShutdownRequested = true;
		return true;
	}
	if(!g_Config.m_QmAutoUpdate || m_UpdateCheckFailed)
		return false;
	if(IsUpdateChecking() || IsUpdateDownloading())
	{
		m_UpdateShutdownRequested = true;
		return true;
	}
#else
	(void)Force;
#endif
	return false;
}

const char *CTClient::UpdateShutdownMessage() const
{
	if(m_UpdateCheckFailed)
		return Localize("Update failed. Please try again");
	if(m_UpdateReady)
		return Localize("Installing update. Please wait...");
	return Localize("Downloading update...");
}
