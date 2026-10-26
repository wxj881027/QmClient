#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QMCLIENT_USERS_PARSE_JOB_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QMCLIENT_USERS_PARSE_JOB_H

#include "qmclient_utils.h"

#include <base/lock.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/engine.h>
#include <engine/shared/jobs.h>

#include <memory>
#include <new>
#include <utility>

class CQmClientUsersParseJob : public IJob
{
public:
	using SResult = SQmClientUsersParseResult;

private:
	std::shared_ptr<const json_value> m_pPayload;
	char m_aServerAddress[NETADDR_MAXSTRSIZE] = "";
	int64_t m_ExpireTick = 0;
	int64_t m_ConnectionTick = 0;
	CLock m_Lock;
	SResult m_Result;

protected:
	void Run() override REQUIRES(!m_Lock)
	{
		SResult Result;
		if(m_pPayload)
			ParseQmClientUsersJson(m_pPayload.get(), m_aServerAddress, Result);
		{
			const CLockScope Lock(m_Lock);
			m_Result = std::move(Result);
		}
		m_pPayload.reset();
	}

public:
	CQmClientUsersParseJob(std::shared_ptr<const json_value> pPayload, const char *pServerAddress, int64_t ExpireTick, int64_t ConnectionTick) :
		m_pPayload(std::move(pPayload)),
		m_ExpireTick(ExpireTick),
		m_ConnectionTick(ConnectionTick)
	{
		str_copy(m_aServerAddress, pServerAddress, sizeof(m_aServerAddress));
	}

	SResult TakeResult() REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		SResult Result = std::move(m_Result);
		m_Result = SResult();
		return Result;
	}

	int64_t ExpireTick() const { return m_ExpireTick; }
	int64_t ConnectionTick() const { return m_ConnectionTick; }
	const char *ServerAddress() const { return m_aServerAddress; }
};

// 快照和等待身份都在引擎确认接收后移交；准备失败仍可沿原入口重试。
template<typename TFactory>
bool TryQueueQmClientUsersParseJob(std::shared_ptr<IJob> &pPending, std::shared_ptr<const json_value> &pPayload, IEngine &Engine, TFactory &&Factory)
{
	if(pPending || !pPayload)
		return false;
	try
	{
		std::shared_ptr<IJob> pCandidate = std::forward<TFactory>(Factory)(pPayload);
		if(!pCandidate || !Engine.TryAddJob(pCandidate))
			return false;
		pPending = std::move(pCandidate);
		pPayload.reset();
		return true;
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
}

#endif
