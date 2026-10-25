#include "community_icons.h"

#include <base/log.h>

#include <engine/engine.h>
#include <engine/http.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/community_icon_image.h>

static constexpr size_t MAX_CONCURRENT_ICON_DOWNLOADS = 4;

CCommunityIcons::CAbstractCommunityIconJob::CAbstractCommunityIconJob(CCommunityIcons *pCommunityIcons, const char *pCommunityId, int StorageType) :
	m_pCommunityIcons(pCommunityIcons),
	m_StorageType(StorageType)
{
	str_copy(m_aCommunityId, pCommunityId);
	str_format(m_aPath, sizeof(m_aPath), "communityicons/%s.png", pCommunityId);
}

CCommunityIcons::CCommunityIconDownloadJob::CCommunityIconDownloadJob(CCommunityIcons *pCommunityIcons, const char *pCommunityId, const char *pUrl, const SHA256_DIGEST &Sha256) :
	CAbstractCommunityIconJob(pCommunityIcons, pCommunityId, IStorage::TYPE_SAVE)
{
	m_Sha256 = Sha256;
	m_pHttpRequest = std::shared_ptr<IHttpRequest>(CreateHttpRequest(pUrl));
	m_pHttpRequest->WriteToFile(pCommunityIcons->Storage(), m_aPath, IStorage::TYPE_SAVE);
	m_pHttpRequest->ExpectSha256(Sha256);
	// Keep icon downloads bounded so broken remote endpoints can't hang forever.
	m_pHttpRequest->Timeout(CTimeout{10000, 30000, 500, 10});
	m_pHttpRequest->LogProgress(HTTPLOG::FAILURE);
}

void CCommunityIcons::CCommunityIconLoadJob::Run()
{
	m_Success = m_pCommunityIcons->LoadFile(m_aPath, m_StorageType, m_ImageInfo, m_ImageInfoGrayscale, m_Sha256);
}

CCommunityIcons::CCommunityIconLoadJob::CCommunityIconLoadJob(CCommunityIcons *pCommunityIcons, const char *pCommunityId, int StorageType) :
	CAbstractCommunityIconJob(pCommunityIcons, pCommunityId, StorageType)
{
	Abortable(true);
}

CCommunityIcons::CCommunityIconLoadJob::~CCommunityIconLoadJob()
{
	m_ImageInfo.Free();
	m_ImageInfoGrayscale.Free();
}

int CCommunityIcons::FileScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	const char *pExtension = ".png";
	CCommunityIcons *pSelf = static_cast<CCommunityIcons *>(pUser);
	if(IsDir || !str_endswith(pName, pExtension) || str_length(pName) - str_length(pExtension) >= (int)CServerInfo::MAX_COMMUNITY_ID_LENGTH)
		return 0;

	char aCommunityId[CServerInfo::MAX_COMMUNITY_ID_LENGTH];
	str_truncate(aCommunityId, sizeof(aCommunityId), pName, str_length(pName) - str_length(pExtension));

	if(!pSelf->QueueLoadJob(aCommunityId, DirType))
		log_warn("menus/browser", "Could not queue cached community icon '%s'", aCommunityId);
	return 0;
}

bool CCommunityIcons::QueueLoadJob(const char *pCommunityId, int StorageType)
{
	return qm_community_icons::ObserveAndSubmit(m_CommunityIconLoadJobs, [&] { return std::make_shared<CCommunityIconLoadJob>(this, pCommunityId, StorageType); }, [this](const auto &pJob) { return Engine()->TryAddJob(pJob); });
}

const CCommunityIcon *CCommunityIcons::Find(const char *pCommunityId)
{
	return m_Icons.Find(pCommunityId);
}

bool CCommunityIcons::LoadFile(const char *pPath, int DirType, CImageInfo &Info, CImageInfo &InfoGrayscale, SHA256_DIGEST &Sha256)
{
	const auto Result = qm_community_icons::LoadImage(*Storage(), pPath, DirType, Info, InfoGrayscale, Sha256);
	if(Result == qm_community_icons::ELoadResult::LOADED)
		return true;
	const char *pReason;
	switch(Result)
	{
	case qm_community_icons::ELoadResult::READ_FAILED: pReason = "could not read file"; break;
	case qm_community_icons::ELoadResult::DECODE_FAILED: pReason = "could not decode PNG"; break;
	case qm_community_icons::ELoadResult::FORMAT_UNSUPPORTED: pReason = "must be an RGBA image"; break;
	case qm_community_icons::ELoadResult::COPY_FAILED: pReason = "could not copy image data"; break;
	default: pReason = "image processing failed"; break;
	}
	log_error("menus/browser", "Failed to load community icon from '%s': %s", pPath, pReason);
	return false;
}

bool CCommunityIcons::LoadFinish(const char *pCommunityId, CImageInfo &Info, CImageInfo &InfoGrayscale, const SHA256_DIGEST &Sha256)
{
	const auto Result = m_Icons.Publish(Graphics(), pCommunityId, Sha256, Info, InfoGrayscale);
	if(Result == qm_community_icons::EPublishResult::DEFERRED)
		return false;
	if(Result == qm_community_icons::EPublishResult::FAILED)
		log_warn("menus/browser", "Could not upload community icon '%s'; keeping previous icon", pCommunityId);
	else
		log_trace("menus/browser", "Loaded community icon '%s'", pCommunityId);
	return true;
}

void CCommunityIcons::Render(const CCommunityIcon *pIcon, CUIRect Rect, bool Active, float AlphaMul)
{
	Rect.VMargin(Rect.w / 2.0f - Rect.h, &Rect);

	Graphics()->WrapClamp();
	Graphics()->TextureSet(pIcon->Texture(Active));
	Graphics()->QuadsBegin();
	// AlphaMul 供列表行淡入使用（裸纹理 quad 不经过 CUi 的显隐系数）。
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, (Active ? 1.0f : 0.5f) * std::clamp(AlphaMul, 0.0f, 1.0f));
	IGraphics::CQuadItem QuadItem(Rect.x, Rect.y, Rect.w, Rect.h);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();
	Graphics()->WrapNormal();
}

void CCommunityIcons::Load()
{
	m_IconScan.Reset();
	m_ScanPreparationFailed = false;
	if(!m_Icons.Clear(Graphics()))
		log_warn("menus/browser", "Could not release community icon textures; retaining handles");
	Storage()->ListDirectory(IStorage::TYPE_ALL, "communityicons", FileScan, this);
}

void CCommunityIcons::Shutdown()
{
	m_IconScan.Reset();
	m_ScanPreparationFailed = false;
	m_CommunityIconLoadJobs.clear();
	m_CommunityIconDownloadJobs.clear();
	if(!m_Icons.Clear(Graphics()))
		log_warn("menus/browser", "Could not release community icon textures at shutdown");
}

void CCommunityIcons::Update()
{
	if(!m_Icons.Drain(Graphics()))
	{
		if(!m_CleanupFailed)
			log_warn("menus/browser", "Could not release retired community icon textures; retrying later");
		m_CleanupFailed = true;
		return;
	}
	m_CleanupFailed = false;
	// Update load jobs (icon is loaded from existing file)
	if(!m_CommunityIconLoadJobs.empty())
	{
		std::shared_ptr<CCommunityIconLoadJob> pJob = m_CommunityIconLoadJobs.front();
		if(pJob->Done())
		{
			if(pJob->State() == IJob::STATE_DONE && pJob->Success() &&
				qm_community_icons::AcceptsResult(ServerBrowser()->DDNetInfoAvailable(), ServerBrowser()->Community(pJob->CommunityId()), pJob->Sha256()))
			{
				if(!LoadFinish(pJob->CommunityId(), pJob->ImageInfo(), pJob->ImageInfoGrayscale(), pJob->Sha256()))
					return;
			}
			m_CommunityIconLoadJobs.pop_front();
		}

		// Don't start download jobs until all load jobs are done
		if(!m_CommunityIconLoadJobs.empty())
			return;
	}

	// Update download jobs (icon is downloaded and loaded from new file)
	if(!m_CommunityIconDownloadJobs.empty())
	{
		std::shared_ptr<CCommunityIconDownloadJob> pJob = m_CommunityIconDownloadJobs.front();
		if(pJob->HttpRequest()->Done())
		{
			if(pJob->HttpRequest()->State() == EHttpState::DONE)
			{
				if(!QueueLoadJob(pJob->CommunityId(), IStorage::TYPE_SAVE))
					return;
			}
			m_CommunityIconDownloadJobs.pop_front();
		}
	}

	// 新增的解码任务也先完成，避免同一ID的新下载覆盖正在读取的缓存文件。
	if(!m_CommunityIconLoadJobs.empty() || !ServerBrowser()->DDNetInfoAvailable())
		return;
	const auto InfoSha256 = ServerBrowser()->DDNetInfoSha256();
	if(!InfoSha256.has_value())
		return;
	const bool Changed = !m_IconScan.Matches(InfoSha256.value());
	if(!m_IconScan.Prepare(InfoSha256.value(), ServerBrowser()->Communities()))
	{
		if(!m_ScanPreparationFailed)
			log_warn("menus/browser", "Could not prepare community icon scan; retrying later");
		m_ScanPreparationFailed = true;
		return;
	}
	m_ScanPreparationFailed = false;

	if(Changed && !m_Icons.Prune(ServerBrowser()->Communities(), Graphics()))
		return;

	const auto HasCurrent = [&](const qm_community_icons::SRequest &Request) {
		return m_Icons.IsCurrent(Request.m_aId, Request.m_Sha256);
	};
	const auto FindActive = [&](const qm_community_icons::SRequest &Request) {
		const auto Existing = std::find_if(m_CommunityIconDownloadJobs.begin(), m_CommunityIconDownloadJobs.end(), [&](const auto &pJob) {
			return str_comp(pJob->CommunityId(), Request.m_aId) == 0;
		});
		if(Existing == m_CommunityIconDownloadJobs.end())
			return qm_community_icons::EActive::NONE;
		return Request.Matches((*Existing)->CommunityId(), (*Existing)->HttpRequest()->Url(), (*Existing)->Sha256()) ? qm_community_icons::EActive::SAME : qm_community_icons::EActive::OTHER;
	};
	const auto Start = [&](const qm_community_icons::SRequest &Request) {
		return qm_community_icons::ObserveAndSubmit(m_CommunityIconDownloadJobs, [&] { return std::make_shared<CCommunityIconDownloadJob>(this, Request.m_aId, Request.m_aUrl, Request.m_Sha256); }, [this](const auto &pJob) {
				Http()->Run(pJob->HttpRequest());
				return true; });
	};
	m_IconScan.Pump(m_CommunityIconDownloadJobs.size(), MAX_CONCURRENT_ICON_DOWNLOADS, HasCurrent, FindActive, Start);
}
