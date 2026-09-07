/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_UI_ASSET_PAGE_RESOURCES_H
#define GAME_CLIENT_UI_ASSET_PAGE_RESOURCES_H

#include "resource_page_graphics.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class IEngine;
class IStorage;

enum class EAssetPageKind
{
	ENTITIES,
	GAME,
	EMOTICONS,
	PARTICLES,
	HUD,
	EXTRAS,
	COUNT,
};

enum class EAssetListState
{
	UNLOADED,
	LOADING,
	READY,
	FAILED,
};

struct SAssetPageSnapshot
{
	EAssetListState m_State = EAssetListState::UNLOADED;
	uint32_t m_Revision = 0;
	const std::vector<std::string> *m_pNames = nullptr;
	const char *m_pError = nullptr;
};

struct SAssetPreviewSpec
{
	std::string m_PageId;
	SResourcePageResource m_Resource;
};

SAssetPreviewSpec QmAssetPreviewSpec(EAssetPageKind Kind, const char *pName, int PreviewSize);
const char *QmAssetDirectory(EAssetPageKind Kind);

class CAssetPageResources final
{
	class CScanJob;
	struct SPage;
	struct SResident
	{
		std::string m_Id;
		uint64_t m_LastFrame = 0;
		EAssetPageKind m_Kind;
		std::string m_Name;
	};

	IEngine *m_pEngine;
	IStorage *m_pStorage;
	CResourcePageLoader m_Loader;
	CResourcePageGraphics m_Graphics;
	std::array<std::unique_ptr<SPage>, static_cast<size_t>(EAssetPageKind::COUNT)> m_apPages;
	std::vector<SResident> m_vResidents;
	uint32_t m_Generation = 1;
	uint64_t m_Frame = 0;
	uint64_t m_Visit = 0;
	bool m_RenderedThisFrame = false;
	bool m_Open = false;
	bool m_Shutdown = false;
	std::function<void(const char *, const SResourcePageMetrics &, int)> m_Reporter;
	EAssetPageKind m_Current = EAssetPageKind::ENTITIES;

	SPage &Page(EAssetPageKind Kind);
	const SPage &Page(EAssetPageKind Kind) const;
	void StartScan(EAssetPageKind Kind);
	void PollScan(EAssetPageKind Kind);
	void ReleaseTransient();
	void Report();
	void Evict(const SResident &Resident);

public:
	CAssetPageResources(IEngine *pEngine, IStorage *pStorage, IGraphics *pGraphics);
	~CAssetPageResources();
	CAssetPageResources(const CAssetPageResources &) = delete;
	CAssetPageResources &operator=(const CAssetPageResources &) = delete;

	SAssetPageSnapshot BeginFrame(EAssetPageKind Kind);
	IGraphics::CTextureHandle Preview(EAssetPageKind Kind, const char *pName, int PreviewSize = 256);
	void Reload(EAssetPageKind Kind);
	void Invalidate();
	void EndFrame();
	void Shutdown();
	const SResourcePageMetrics *PreviewMetrics(EAssetPageKind Kind, const char *pName, int PreviewSize = 256) const;
	EResourcePageState PreviewState(EAssetPageKind Kind, const char *pName) const;
	size_t ResidentCount() const { return m_vResidents.size(); }
	size_t ManifestCount() const { return m_Loader.ManifestCount(); }
	void SetReporter(std::function<void(const char *, const SResourcePageMetrics &, int)> Reporter);
};

#endif
