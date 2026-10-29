#ifndef GAME_CLIENT_QMUI_CARDS_QMSTEAMPROBESTATE_H
#define GAME_CLIENT_QMUI_CARDS_QMSTEAMPROBESTATE_H

#include <engine/shared/jobs.h>
#include <engine/steam.h>

#include <memory>
#include <string>
#include <utility>

class CSteamProbeState;

class CSteamProbeJob : public IJob
{
	friend class CSteamProbeState;
	std::string m_ManualPath;
	SSteamClientInfo m_Info;
	bool m_Succeeded = false;
	SSteamClientInfo (*m_pfnInspect)(const char *);

	void Run() override
	{
		m_Info = m_pfnInspect(m_ManualPath.c_str());
		// 线程池捕获异常后也会进入DONE，只有完整返回才可发布。
		m_Succeeded = true;
	}

public:
	CSteamProbeJob(const char *pManualPath, SSteamClientInfo (*pfnInspect)(const char *)) :
		m_ManualPath(pManualPath), m_pfnInspect(pfnInspect) {}
};

// 状态只由UI线程推进，worker仅填写任务结果；只读布局不调用推进接口。
class CSteamProbeState
{
	std::shared_ptr<CSteamProbeJob> m_pProbe;
	SSteamClientInfo m_Info;
	std::string m_CheckedManualPath;
	bool m_HasResult = false;
	bool m_RefreshRequested = false;

public:
	bool Pending() const noexcept { return m_pProbe != nullptr; }
	bool HasResult() const noexcept { return m_HasResult; }
	const SSteamClientInfo &Info() const noexcept { return m_Info; }
	void RequestRefresh() noexcept { m_RefreshRequested = true; }

	void Finish(const char *pManualPath) noexcept
	{
		if(!m_pProbe || !m_pProbe->Done())
			return;
		if(m_pProbe->State() == IJob::STATE_DONE)
		{
			if(m_pProbe->m_Succeeded && m_pProbe->m_ManualPath == pManualPath)
			{
				// 路径与结果一起无分配发布，失败准备不会破坏已有显示。
				m_CheckedManualPath.swap(m_pProbe->m_ManualPath);
				m_Info = m_pProbe->m_Info;
				m_HasResult = true;
			}
			else if(!m_pProbe->m_Succeeded)
				m_RefreshRequested = true;
		}
		else
			m_RefreshRequested = true;
		// ABORTED可能仍在运行；不读取其结果，线程池继续持有任务。
		m_pProbe.reset();
	}

	template<typename TFactory, typename TSubmit>
	bool TryStart(const char *pManualPath, bool AllowAutomatic, TFactory &&Factory, TSubmit &&Submit) noexcept
	{
		if(m_pProbe || !(m_RefreshRequested || (AllowAutomatic && (!m_HasResult || m_CheckedManualPath != pManualPath))))
			return false;
		try
		{
			auto pCandidate = Factory(pManualPath);
			if(!pCandidate || !Submit(pCandidate))
				return false;
			// 拒收或构造异常时不登记等待身份，也不消耗刷新请求。
			m_pProbe = std::move(pCandidate);
			m_RefreshRequested = false;
			return true;
		}
		catch(...)
		{
			return false;
		}
	}
};

#endif
