#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_THEME_LIST_LOAD_REQUEST_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_THEME_LIST_LOAD_REQUEST_H

#include "theme_catalog.h"

#include <base/lock.h>

#include <engine/engine.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <new>
#include <utility>
#include <vector>

class CQmThemeListLoadJob : public IJob
{
	IStorage *m_pStorage;
	mutable CLock m_Lock;
	std::vector<SThemeFileEntry> m_vEntries;
	bool m_Completed = false;

	static int ScanCallback(const char *pName, int IsDir, int DirType, void *pUser)
	{
		(void)DirType;
		AppendThemeFileEntry(*static_cast<std::vector<SThemeFileEntry> *>(pUser), pName, IsDir != 0);
		return 0;
	}

	void Run() override REQUIRES(!m_Lock)
	{
		std::vector<SThemeFileEntry> vEntries;
		m_pStorage->ListDirectory(IStorage::TYPE_ALL, "themes", ScanCallback, &vEntries);
		std::sort(vEntries.begin(), vEntries.end(), [](const auto &Left, const auto &Right) {
			return Left.m_Name < Right.m_Name;
		});
		const CLockScope Lock(m_Lock);
		m_vEntries = std::move(vEntries);
		m_Completed = true;
	}

public:
	explicit CQmThemeListLoadJob(IStorage &Storage) :
		m_pStorage(&Storage) {}

	bool IsCompleted() const REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_Completed || Finished();
	}

	bool Succeeded() const REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_Completed;
	}

	std::vector<SThemeFileEntry> Entries() const REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		return m_vEntries;
	}
};

// 任务执行失败时保留当前目录并延迟重扫；完整结果的发布失败只重试发布。
class CQmThemeListLoadRequest
{
	std::shared_ptr<CQmThemeListLoadJob> m_pJob;
	std::chrono::nanoseconds m_RetryAt{0};

public:
	bool TryStart(IStorage &Storage, IEngine &Engine, std::chrono::nanoseconds Now)
	{
		if(m_pJob || Now < m_RetryAt)
			return false;
		try
		{
			auto pJob = std::make_shared<CQmThemeListLoadJob>(Storage);
			if(!Engine.TryAddJob(pJob))
				return false;
			m_pJob = std::move(pJob);
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
	}

	template<typename TAllocator>
	bool Poll(std::vector<CTheme, TAllocator> &Themes, size_t PreservedCount, std::chrono::nanoseconds Now)
	{
		if(!m_pJob || !m_pJob->IsCompleted())
			return false;
		if(!m_pJob->Succeeded())
		{
			m_pJob.reset();
			m_RetryAt = Now + std::chrono::seconds(1);
			return false;
		}
		if(!PublishThemeEntries(Themes, PreservedCount, [this] { return m_pJob->Entries(); }))
			return false;
		m_pJob.reset();
		m_RetryAt = std::chrono::nanoseconds(0);
		return true;
	}

	void Reset()
	{
		m_pJob.reset();
		m_RetryAt = std::chrono::nanoseconds(0);
	}
};

#endif
