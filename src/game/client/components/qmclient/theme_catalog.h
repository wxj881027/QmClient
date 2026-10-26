#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_THEME_CATALOG_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_THEME_CATALOG_H

#include "theme_icon_load_job.h"

#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/storage.h>

#include <game/client/components/theme_scan.h>

#include <initializer_list>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

class CTheme
{
public:
	CTheme(const char *pName, bool HasDay, bool HasNight) :
		m_Name(pName), m_HasDay(HasDay), m_HasNight(HasNight) {}
	std::string m_Name;
	bool m_HasDay;
	bool m_HasNight;
	IGraphics::CTextureHandle m_IconTexture;
	std::string m_IconPath;
	bool m_IconLoadRequested = false;
	bool m_IconLoadFailed = false;
	bool operator<(const CTheme &Other) const { return m_Name < Other.m_Name; }
};

// 清理成功前保留目录所有权；已释放句柄被图形接口作废，重试不会重复卸载。
inline bool ClearThemeCatalog(std::vector<CTheme> &Themes, IGraphics *pGraphics)
{
	bool Complete = true;
	for(auto &Theme : Themes)
	{
		if(!Theme.m_IconTexture.IsValid())
			continue;
		if(pGraphics == nullptr)
		{
			Complete = false;
			continue;
		}
		try
		{
			pGraphics->UnloadTexture(&Theme.m_IconTexture);
		}
		catch(const std::bad_alloc &)
		{
			Complete = false;
		}
	}
	if(Complete)
		Themes.clear();
	return Complete;
}

// 分配器沿用调用方；完整默认列表准备成功后交换，失败不留下半成品。
template<typename TAllocator>
bool EnsureDefaultThemeEntries(std::vector<CTheme, TAllocator> &Themes)
{
	if(!Themes.empty())
		return true;
	try
	{
		std::vector<CTheme, TAllocator> Candidate(Themes.get_allocator());
		Candidate.reserve(3);
		for(const char *pName : {"", "auto", "rand"})
		{
			Candidate.emplace_back(pName, true, true);
			Candidate.back().m_IconPath = ThemeIconPathFromName(pName);
		}
		Themes.swap(Candidate);
		return true;
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
}

// 暂存跟踪记录以准备容量；接收失败撤回，确认接收后才发布请求标记。
template<typename TAllocator>
bool TryQueueThemeIcon(CTheme &Theme, std::vector<std::shared_ptr<CQmThemeIconLoadJob>, TAllocator> &Jobs,
	IStorage &Storage, IEngine &Engine, CQmThemeIconLoadJob::FDecode pDecode)
{
	if(Theme.m_IconLoadRequested || Theme.m_IconLoadFailed || Theme.m_IconPath.empty())
		return false;
	bool Tracked = false;
	try
	{
		if(!Storage.FileExists(Theme.m_IconPath.c_str(), IStorage::TYPE_ALL))
		{
			Theme.m_IconLoadFailed = true;
			return false;
		}
		auto pJob = std::make_shared<CQmThemeIconLoadJob>(Theme.m_Name.c_str(), Theme.m_IconPath.c_str(), &Storage, pDecode);
		Jobs.push_back(pJob);
		Tracked = true;
		if(!Engine.TryAddJob(pJob))
		{
			Jobs.pop_back();
			return false;
		}
		Theme.m_IconLoadRequested = true;
		return true;
	}
	catch(const std::bad_alloc &)
	{
		if(Tracked)
			Jobs.pop_back();
		return false;
	}
}

#endif
