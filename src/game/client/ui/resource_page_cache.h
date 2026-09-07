/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_UI_RESOURCE_PAGE_CACHE_H
#define GAME_CLIENT_UI_RESOURCE_PAGE_CACHE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

enum class EResourcePageState
{
	UNLOADED,
	LOADING,
	READY,
	FAILED,
};

struct SResourcePageState
{
	std::string m_Id;
	EResourcePageState m_State = EResourcePageState::UNLOADED;
	uint32_t m_Generation = 0;
	uint64_t m_RequestId = 0;
	std::string m_Error;
};

struct SResourcePageResource
{
	std::string m_Path;
	std::vector<std::string> m_vFallbackPaths;
	int m_PreviewSize = 0;
};

struct SResourcePageManifest
{
	std::string m_Id;
	std::vector<SResourcePageResource> m_vResources;
};

class CResourcePageCache final
{
	std::map<std::string, SResourcePageState, std::less<>> m_Pages;
	uint64_t m_NextRequestId = 0;

public:
	bool RegisterPage(const char *pId);
	bool BeginLoad(const char *pId, uint32_t Generation);
	bool IsCurrentLoad(const char *pId, uint32_t Generation, uint64_t RequestId) const;
	bool CompleteLoad(const char *pId, uint32_t Generation, uint64_t RequestId, bool Success, const char *pError = nullptr);
	bool NeedsLoad(const char *pId, uint32_t Generation) const;
	void Invalidate(uint32_t Generation);
	void InvalidatePage(const char *pId, uint32_t Generation);
	void UnregisterPage(const char *pId);
	void Clear();
	const SResourcePageState *Find(const char *pId) const;
};

#endif
