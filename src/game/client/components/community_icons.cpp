#include "community_icons.h"

#include <base/log.h>

#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/http.h>
#include <engine/storage.h>

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
	auto Icon = std::find_if(m_vCommunityIcons.begin(), m_vCommunityIcons.end(), [pCommunityId](const CCommunityIcon &Element) {
		return str_comp(Element.m_aCommunityId, pCommunityId) == 0;
	});
	return Icon == m_vCommunityIcons.end() ? nullptr : &(*Icon);
}

bool CCommunityIcons::LoadFile(const char *pPath, int DirType, CImageInfo &Info, CImageInfo &InfoGrayscale, SHA256_DIGEST &Sha256)
{
	void *pFileData = nullptr;
	unsigned FileSize = 0;
	if(!Storage()->ReadFile(pPath, DirType, &pFileData, &FileSize))
	{
		log_error("menus/browser", "Failed to read community icon from '%s'", pPath);
		return false;
	}

	if(!CImageLoader::LoadPng(pFileData, FileSize, pPath, Info))
	{
		free(pFileData);
		log_error("menus/browser", "Failed to decode community icon from '%s'", pPath);
		return false;
	}
	free(pFileData);

	if(Info.m_Format != CImageInfo::FORMAT_RGBA)
	{
		Info.Free();
		log_error("menus/browser", "Failed to load community icon from '%s': must be an RGBA image", pPath);
		return false;
	}
	if(!Storage()->CalculateHashes(pPath, DirType, &Sha256))
	{
		Info.Free();
		log_error("menus/browser", "Failed to load community icon from '%s': could not calculate hash", pPath);
		return false;
	}
	InfoGrayscale = Info.DeepCopy();
	if(InfoGrayscale.m_pData == nullptr)
	{
		Info.Free();
		log_error("menus/browser", "Failed to load community icon from '%s': could not copy image data", pPath);
		return false;
	}
	ConvertToGrayscale(InfoGrayscale);
	return true;
}

void CCommunityIcons::LoadFinish(const char *pCommunityId, CImageInfo &Info, CImageInfo &InfoGrayscale, const SHA256_DIGEST &Sha256)
{
	CCommunityIcon CommunityIcon;
	str_copy(CommunityIcon.m_aCommunityId, pCommunityId);
	CommunityIcon.m_Sha256 = Sha256;
	CommunityIcon.m_OrgTexture = Graphics()->LoadTextureRawMove(Info, 0, pCommunityId);
	CommunityIcon.m_GreyTexture = Graphics()->LoadTextureRawMove(InfoGrayscale, 0, pCommunityId);

	auto ExistingIcon = std::find_if(m_vCommunityIcons.begin(), m_vCommunityIcons.end(), [pCommunityId](const CCommunityIcon &Element) {
		return str_comp(Element.m_aCommunityId, pCommunityId) == 0;
	});
	if(ExistingIcon == m_vCommunityIcons.end())
	{
		m_vCommunityIcons.push_back(CommunityIcon);
	}
	else
	{
		Graphics()->UnloadTexture(&ExistingIcon->m_OrgTexture);
		Graphics()->UnloadTexture(&ExistingIcon->m_GreyTexture);
		*ExistingIcon = CommunityIcon;
	}

	log_trace("menus/browser", "Loaded community icon '%s'", pCommunityId);
}

void CCommunityIcons::Render(const CCommunityIcon *pIcon, CUIRect Rect, bool Active, float AlphaMul)
{
	Rect.VMargin(Rect.w / 2.0f - Rect.h, &Rect);

	Graphics()->WrapClamp();
	Graphics()->TextureSet(Active ? pIcon->m_OrgTexture : pIcon->m_GreyTexture);
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
	m_vCommunityIcons.clear();
	Storage()->ListDirectory(IStorage::TYPE_ALL, "communityicons", FileScan, this);
}

void CCommunityIcons::Shutdown()
{
	m_IconScan.Reset();
	m_ScanPreparationFailed = false;
	m_CommunityIconLoadJobs.clear();
	m_CommunityIconDownloadJobs.clear();
}

void CCommunityIcons::Update()
{
	// Update load jobs (icon is loaded from existing file)
	if(!m_CommunityIconLoadJobs.empty())
	{
		std::shared_ptr<CCommunityIconLoadJob> pJob = m_CommunityIconLoadJobs.front();
		if(pJob->Done())
		{
			if(pJob->State() == IJob::STATE_DONE && pJob->Success() &&
				qm_community_icons::AcceptsResult(ServerBrowser()->DDNetInfoAvailable(), ServerBrowser()->Community(pJob->CommunityId()), pJob->Sha256()))
			{
				LoadFinish(pJob->CommunityId(), pJob->ImageInfo(), pJob->ImageInfoGrayscale(), pJob->Sha256());
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

	// Remove icons for removed communities
	auto RemovalIterator = m_vCommunityIcons.begin();
	while(Changed && RemovalIterator != m_vCommunityIcons.end())
	{
		if(ServerBrowser()->Community(RemovalIterator->m_aCommunityId) == nullptr)
		{
			Graphics()->UnloadTexture(&RemovalIterator->m_OrgTexture);
			Graphics()->UnloadTexture(&RemovalIterator->m_GreyTexture);
			RemovalIterator = m_vCommunityIcons.erase(RemovalIterator);
		}
		else
		{
			++RemovalIterator;
		}
	}

	const auto HasCurrent = [&](const qm_community_icons::SRequest &Request) {
		return std::any_of(m_vCommunityIcons.begin(), m_vCommunityIcons.end(), [&](const auto &Icon) {
			return str_comp(Icon.m_aCommunityId, Request.m_aId) == 0 && Icon.m_Sha256 == Request.m_Sha256;
		});
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
