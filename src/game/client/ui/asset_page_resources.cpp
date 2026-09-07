/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "asset_page_resources.h"

#include <base/str.h>
#include <engine/engine.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <utility>

namespace
{
constexpr size_t MAX_ASSET_NAMES = 4096;
constexpr size_t MAX_RESIDENT_PREVIEWS = 64;

bool ValidKind(EAssetPageKind Kind)
{
	return Kind >= EAssetPageKind::ENTITIES && Kind < EAssetPageKind::COUNT;
}

const char *AssetName(EAssetPageKind Kind)
{
	switch(Kind)
	{
	case EAssetPageKind::ENTITIES: return "entities";
	case EAssetPageKind::GAME: return "game";
	case EAssetPageKind::EMOTICONS: return "emoticons";
	case EAssetPageKind::PARTICLES: return "particles";
	case EAssetPageKind::HUD: return "hud";
	case EAssetPageKind::EXTRAS: return "extras";
	default: return "";
	}
}

std::string StripPng(const char *pName)
{
	std::string Name = pName ? pName : "";
	if(Name.size() > 4 && Name.ends_with(".png"))
		Name.resize(Name.size() - 4);
	return Name;
}
}

const char *QmAssetDirectory(EAssetPageKind Kind)
{
	switch(Kind)
	{
	case EAssetPageKind::ENTITIES: return "assets/entities";
	case EAssetPageKind::GAME: return "assets/game";
	case EAssetPageKind::EMOTICONS: return "assets/emoticons";
	case EAssetPageKind::PARTICLES: return "assets/particles";
	case EAssetPageKind::HUD: return "assets/hud";
	case EAssetPageKind::EXTRAS: return "assets/extras";
	default: return "";
	}
}

SAssetPreviewSpec QmAssetPreviewSpec(EAssetPageKind Kind, const char *pName, int PreviewSize)
{
	SAssetPreviewSpec Result;
	if(!ValidKind(Kind) || !pName || pName[0] == '\0' || str_length(pName) >= 50)
		return Result;
	const char *pAsset = AssetName(Kind);
	Result.m_PageId = "qm.assets.";
	Result.m_PageId += pAsset;
	Result.m_PageId += ".";
	Result.m_PageId += pName;
	Result.m_Resource.m_PreviewSize = std::clamp(PreviewSize, 32, 512);
	Result.m_PageId += "." + std::to_string(Result.m_Resource.m_PreviewSize);
	if(str_comp(pName, "default") == 0)
	{
		if(Kind == EAssetPageKind::ENTITIES)
		{
			Result.m_Resource.m_Path = "editor/entities_clear/ddnet.png";
			for(const char *pMod : {"ddrace", "race", "blockworlds", "fng", "vanilla", "f-ddrace"})
				Result.m_Resource.m_vFallbackPaths.push_back(std::string("editor/entities_clear/") + pMod + ".png");
		}
		else
			Result.m_Resource.m_Path = std::string(pAsset) + ".png";
		return Result;
	}
	Result.m_Resource.m_Path = std::string(QmAssetDirectory(Kind)) + "/" + pName + ".png";
	if(Kind == EAssetPageKind::ENTITIES)
	{
		const std::string FlatPath = Result.m_Resource.m_Path;
		Result.m_Resource.m_Path = std::string(QmAssetDirectory(Kind)) + "/" + pName + "/ddnet.png";
		Result.m_Resource.m_vFallbackPaths.push_back(FlatPath);
		for(const char *pMod : {"ddrace", "race", "blockworlds", "fng", "vanilla", "f-ddrace"})
			Result.m_Resource.m_vFallbackPaths.push_back(std::string(QmAssetDirectory(Kind)) + "/" + pName + "/" + pMod + ".png");
	}
	else
		Result.m_Resource.m_vFallbackPaths.push_back(std::string(QmAssetDirectory(Kind)) + "/" + pName + "/" + pAsset + ".png");
	return Result;
}

struct CAssetPageResources::SPage
{
	struct SPreview
	{
		SAssetPreviewSpec m_Spec;
		uint64_t m_LastHitVisit = 0;
	};
	EAssetListState m_State = EAssetListState::UNLOADED;
	uint32_t m_Revision = 0;
	uint32_t m_Generation = 0;
	std::vector<std::string> m_vNames;
	std::string m_Error;
	std::shared_ptr<CScanJob> m_pJob;
	bool m_JobCancelled = false;
	std::map<std::string, SPreview, std::less<>> m_Previews;
};

class CAssetPageResources::CScanJob final : public IJob
{
	IStorage *m_pStorage;
	EAssetPageKind m_Kind;
	std::atomic<bool> m_Cancelled{false};

	static int Scan(const char *pName, int IsDir, int DirType, void *pUser)
	{
		auto *pThis = static_cast<CScanJob *>(pUser);
		if(pThis->m_Cancelled.load() || pThis->m_vNames.size() >= MAX_ASSET_NAMES)
			return 1;
		if(!pName || pName[0] == '.' || str_comp(pName, "default") == 0 || str_comp(pName, "default.png") == 0)
			return 0;
		if((IsDir || str_endswith(pName, ".png")) && str_length(pName) < 50)
			pThis->m_vNames.push_back(IsDir ? pName : StripPng(pName));
		return pThis->m_vNames.size() >= MAX_ASSET_NAMES ? 1 : 0;
	}

protected:
	void Run() override
	{
		m_vNames.emplace_back("default");
		m_pStorage->ListDirectory(IStorage::TYPE_ALL, QmAssetDirectory(m_Kind), Scan, this);
		if(m_Cancelled.load())
		{
			m_vNames.clear();
			return;
		}
		std::sort(m_vNames.begin(), m_vNames.end());
		m_vNames.erase(std::unique(m_vNames.begin(), m_vNames.end()), m_vNames.end());
	}

public:
	std::vector<std::string> m_vNames;

	CScanJob(IStorage *pStorage, EAssetPageKind Kind) :
		m_pStorage(pStorage), m_Kind(Kind) {}
	void Cancel() { m_Cancelled.store(true); }
};

CAssetPageResources::CAssetPageResources(IEngine *pEngine, IStorage *pStorage, IGraphics *pGraphics) :
	m_pEngine(pEngine), m_pStorage(pStorage), m_Loader(pEngine, pStorage), m_Graphics(pGraphics)
{
	for(auto &pPage : m_apPages)
		pPage = std::make_unique<SPage>();
	m_vResidents.reserve(MAX_RESIDENT_PREVIEWS);
}

CAssetPageResources::~CAssetPageResources()
{
	Shutdown();
}

CAssetPageResources::SPage &CAssetPageResources::Page(EAssetPageKind Kind)
{
	return *m_apPages[static_cast<size_t>(Kind)];
}

const CAssetPageResources::SPage &CAssetPageResources::Page(EAssetPageKind Kind) const
{
	return *m_apPages[static_cast<size_t>(Kind)];
}

void CAssetPageResources::StartScan(EAssetPageKind Kind)
{
	SPage &State = Page(Kind);
	if(m_Shutdown || State.m_pJob)
		return;
	if(!m_pEngine || !m_pStorage)
	{
		State.m_State = EAssetListState::FAILED;
		State.m_Error = "asset page adapters unavailable";
		++State.m_Revision;
		return;
	}
	State.m_State = EAssetListState::LOADING;
	State.m_Error.clear();
	State.m_Generation = m_Generation;
	State.m_JobCancelled = false;
	State.m_pJob = std::make_shared<CScanJob>(m_pStorage, Kind);
	m_pEngine->AddJob(State.m_pJob);
}

void CAssetPageResources::PollScan(EAssetPageKind Kind)
{
	SPage &State = Page(Kind);
	if(!State.m_pJob || State.m_pJob->State() != IJob::STATE_DONE)
		return;
	if(State.m_Generation == m_Generation && !State.m_JobCancelled)
	{
		State.m_vNames = std::move(State.m_pJob->m_vNames);
		State.m_State = State.m_vNames.empty() ? EAssetListState::FAILED : EAssetListState::READY;
		State.m_Error = State.m_vNames.empty() ? "asset directory scan failed" : "";
		++State.m_Revision;
	}
	State.m_pJob.reset();
	State.m_JobCancelled = false;
}

SAssetPageSnapshot CAssetPageResources::BeginFrame(EAssetPageKind Kind)
{
	if(m_Shutdown || !ValidKind(Kind))
		return {};
	++m_Frame;
	if(!m_Open || m_Current != Kind)
		++m_Visit;
	m_RenderedThisFrame = true;
	if(m_Current != Kind)
	{
		Report();
		ReleaseTransient();
		m_Current = Kind;
	}
	m_Open = true;
	PollScan(Kind);
	m_Loader.Poll(m_Graphics);
	SPage &State = Page(Kind);
	if(State.m_State == EAssetListState::UNLOADED)
		StartScan(Kind);
	return {State.m_State, State.m_Revision, &State.m_vNames, State.m_Error.c_str()};
}

IGraphics::CTextureHandle CAssetPageResources::Preview(EAssetPageKind Kind, const char *pName, int PreviewSize)
{
	if(m_Shutdown || !m_Open || !ValidKind(Kind) || Kind != m_Current || !pName)
		return {};
	auto &Previews = Page(Kind).m_Previews;
	auto It = Previews.find(pName);
	const int BoundedPreviewSize = std::clamp(PreviewSize, 32, 512);
	if(It != Previews.end() && It->second.m_Spec.m_Resource.m_PreviewSize != BoundedPreviewSize)
	{
		const std::string OldId = It->second.m_Spec.m_PageId;
		m_Loader.UnregisterManifest(OldId.c_str(), m_Generation, m_Graphics);
		std::erase_if(m_vResidents, [&](const auto &Entry) { return Entry.m_Id == OldId; });
		Previews.erase(It);
		It = Previews.end();
	}
	if(It == Previews.end())
	{
		if(!m_Loader.HasRequestCapacity())
			return {};
		if(m_vResidents.size() == MAX_RESIDENT_PREVIEWS)
		{
			auto Victim = std::min_element(m_vResidents.begin(), m_vResidents.end(), [](const auto &A, const auto &B) { return A.m_LastFrame < B.m_LastFrame; });
			if(Victim->m_LastFrame == m_Frame)
				return {};
			Evict(*Victim);
			m_vResidents.erase(Victim);
		}
		auto Spec = QmAssetPreviewSpec(Kind, pName, BoundedPreviewSize);
		if(Spec.m_PageId.empty() || !m_Loader.RegisterManifest({Spec.m_PageId, {Spec.m_Resource}}))
			return {};
		m_vResidents.push_back({Spec.m_PageId, m_Frame, Kind, pName});
		It = Previews.emplace(pName, SPage::SPreview{std::move(Spec)}).first;
	}
	auto &Record = It->second;
	const auto &Spec = Record.m_Spec;
	const auto *pState = m_Loader.Cache().Find(Spec.m_PageId.c_str());
	auto Resident = std::find_if(m_vResidents.begin(), m_vResidents.end(), [&](const auto &Entry) { return Entry.m_Id == Spec.m_PageId; });
	if(Resident != m_vResidents.end())
		Resident->m_LastFrame = m_Frame;
	if(pState && pState->m_State == EResourcePageState::UNLOADED)
	{
		if(m_Loader.Request(Spec.m_PageId.c_str(), m_Generation))
			Record.m_LastHitVisit = m_Visit;
	}
	else if(pState && pState->m_State == EResourcePageState::READY && Record.m_LastHitVisit != m_Visit)
	{
		m_Loader.Request(Spec.m_PageId.c_str(), m_Generation);
		Record.m_LastHitVisit = m_Visit;
	}
	return m_Graphics.Find(Spec.m_PageId.c_str(), Spec.m_Resource.m_Path.c_str());
}

void CAssetPageResources::Evict(const SResident &Resident)
{
	m_Loader.UnregisterManifest(Resident.m_Id.c_str(), m_Generation, m_Graphics);
	Page(Resident.m_Kind).m_Previews.erase(Resident.m_Name);
}

void CAssetPageResources::Reload(EAssetPageKind Kind)
{
	if(!ValidKind(Kind))
		return;
	Invalidate();
}

void CAssetPageResources::Invalidate()
{
	++m_Generation;
	for(auto &pPage : m_apPages)
	{
		if(pPage->m_pJob)
		{
			pPage->m_pJob->Cancel();
			pPage->m_JobCancelled = true;
		}
		pPage->m_State = EAssetListState::UNLOADED;
		pPage->m_vNames.clear();
		pPage->m_Error.clear();
		pPage->m_Previews.clear();
		++pPage->m_Revision;
	}
	m_Loader.ClearManifests(m_Generation, m_Graphics);
	m_vResidents.clear();
}

void CAssetPageResources::ReleaseTransient()
{
	for(auto &pPage : m_apPages)
	{
		if(pPage->m_pJob)
		{
			pPage->m_pJob->Cancel();
			pPage->m_JobCancelled = true;
			pPage->m_State = EAssetListState::UNLOADED;
		}
	}
	for(auto It = m_vResidents.begin(); It != m_vResidents.end();)
	{
		const auto *pState = m_Loader.Cache().Find(It->m_Id.c_str());
		if(pState && pState->m_State == EResourcePageState::LOADING)
		{
			Evict(*It);
			It = m_vResidents.erase(It);
		}
		else
			++It;
	}
}

void CAssetPageResources::EndFrame()
{
	if(!m_RenderedThisFrame && m_Open)
	{
		Report();
		ReleaseTransient();
		m_Open = false;
	}
	m_RenderedThisFrame = false;
	for(size_t Index = 0; Index < m_apPages.size(); ++Index)
		PollScan(static_cast<EAssetPageKind>(Index));
	m_Loader.Poll(m_Graphics, 0);
}

void CAssetPageResources::Shutdown()
{
	if(m_Shutdown)
		return;
	Report();
	m_Open = false;
	m_Shutdown = true;
	for(auto &pPage : m_apPages)
		if(pPage && pPage->m_pJob)
			pPage->m_pJob->Cancel();
	m_Loader.Shutdown(m_Graphics);
}

EResourcePageState CAssetPageResources::PreviewState(EAssetPageKind Kind, const char *pName) const
{
	if(!ValidKind(Kind) || !pName)
		return EResourcePageState::UNLOADED;
	const auto &Previews = Page(Kind).m_Previews;
	const auto It = Previews.find(pName);
	if(It == Previews.end())
		return EResourcePageState::UNLOADED;
	const auto *pState = m_Loader.Cache().Find(It->second.m_Spec.m_PageId.c_str());
	return pState ? pState->m_State : EResourcePageState::UNLOADED;
}

const SResourcePageMetrics *CAssetPageResources::PreviewMetrics(EAssetPageKind Kind, const char *pName, int PreviewSize) const
{
	const SAssetPreviewSpec Spec = QmAssetPreviewSpec(Kind, pName, PreviewSize);
	return m_Loader.Metrics(Spec.m_PageId.c_str());
}

void CAssetPageResources::SetReporter(std::function<void(const char *, const SResourcePageMetrics &, int)> Reporter)
{
	m_Reporter = std::move(Reporter);
}

void CAssetPageResources::Report()
{
	if(!m_Open || !m_Reporter)
		return;
	// 仅在离开页面时汇总；字节数是当前目录各缩略图的保留像素，不是进程峰值。
	SResourcePageMetrics Total;
	for(const auto &[Name, Preview] : Page(m_Current).m_Previews)
	{
		const auto *pMetrics = m_Loader.Metrics(Preview.m_Spec.m_PageId.c_str());
		if(!pMetrics)
			continue;
		Total.m_LoadAttempts += pMetrics->m_LoadAttempts;
		Total.m_CacheHits += pMetrics->m_CacheHits;
		Total.m_CancelledLoads += pMetrics->m_CancelledLoads;
		Total.m_FailedLoads += pMetrics->m_FailedLoads;
		Total.m_UploadedResources += pMetrics->m_UploadedResources;
		Total.m_LastLoadNanoseconds += pMetrics->m_LastLoadNanoseconds;
		Total.m_LastDecodedBytes += pMetrics->m_LastDecodedBytes;
		Total.m_PeakDecodedBytes += pMetrics->m_PeakDecodedBytes;
	}
	const std::string Id = std::string("qm.assets.") + AssetName(m_Current);
	m_Reporter(Id.c_str(), Total, 0);
}
