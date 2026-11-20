#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_AXIOM_SCORES_PARSE_JOB_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_AXIOM_SCORES_PARSE_JOB_H

#include "axiom_scores.h"

#include <engine/shared/jobs.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// 持有已完成的响应，解析期间不借用组件；主线程只接收完整结果。
class CQmAxiomDdStatsParseJob : public IJob
{
	std::shared_ptr<IQmAxiomHttpRequest> m_pRequest;
	std::mutex m_InputMutex;
	bool m_Started = false;
	bool m_Cancelled = false;
	std::string m_PlayerName;
	std::atomic<bool> m_Ready{false};
	EQmAxiomParseResult m_Result = EQmAxiomParseResult::INVALID_RESPONSE;
	std::vector<SQmDdStatsGameType> m_vGameTypes;

	void Run() override { Parse(); }

public:
	CQmAxiomDdStatsParseJob(std::shared_ptr<IQmAxiomHttpRequest> pRequest, std::string PlayerName) :
		m_pRequest(std::move(pRequest)),
		m_PlayerName(std::move(PlayerName)) {}

	void Parse()
	{
		std::shared_ptr<IQmAxiomHttpRequest> pRequest;
		{
			const std::lock_guard<std::mutex> Lock(m_InputMutex);
			if(m_Started || m_Cancelled)
				return;
			m_Started = true;
			pRequest = std::move(m_pRequest);
		}
		const unsigned char *pData = nullptr;
		size_t DataSize = 0;
		if(pRequest)
			pRequest->Result(&pData, &DataSize);
		m_Result = QmParseDdStatsPlayerResponse(reinterpret_cast<const char *>(pData), DataSize, m_PlayerName.c_str(), m_vGameTypes);
		// 输入仅在解析中借用；失败展开也由本地owner释放，不随作业结果长期保活。
		pRequest.reset();
		m_Ready.store(true, std::memory_order_release);
	}

	void Cancel() noexcept
	try
	{
		std::shared_ptr<IQmAxiomHttpRequest> pDiscarded;
		{
			const std::lock_guard<std::mutex> Lock(m_InputMutex);
			m_Cancelled = true;
			pDiscarded = std::move(m_pRequest);
		}
	}
	catch(...)
	{
		// 不能取得锁时保留输入，已失效代际仍阻止主线程发布旧结果。
	}

	bool Ready() const { return m_Ready.load(std::memory_order_acquire); }
	EQmAxiomParseResult Result() const { return m_Result; }
	std::vector<SQmDdStatsGameType> TakeGameTypes() { return std::move(m_vGameTypes); }
};

#endif
