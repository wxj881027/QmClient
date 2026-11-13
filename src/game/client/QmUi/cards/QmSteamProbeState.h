#ifndef GAME_CLIENT_QMUI_CARDS_QMSTEAMPROBESTATE_H
#define GAME_CLIENT_QMUI_CARDS_QMSTEAMPROBESTATE_H

#include <engine/shared/jobs.h>
#include <engine/steam.h>

#include <memory>
#include <new>
#include <stdexcept>
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
		// 正常返回和池的成功终态都成立时才发布。
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

	// 采用自动路径时先准备检查键；失败保留已完成任务，配置只在返回成功后回填。
	bool Finish(const char *pManualPath, bool AdoptDetectedPath = false, void (*pfnNormalizePath)(char *) noexcept = nullptr) noexcept
	{
		if(!m_pProbe || !m_pProbe->Done())
			return false;
		bool Published = false;
		if(m_pProbe->State() == IJob::STATE_DONE)
		{
			if(m_pProbe->m_Succeeded && m_pProbe->m_ManualPath == pManualPath)
			{
				if(AdoptDetectedPath && pManualPath[0] == '\0' && m_pProbe->m_Info.m_aPath[0] != '\0')
				{
					try
					{
						SSteamClientInfo Info = m_pProbe->m_Info;
						if(pfnNormalizePath != nullptr)
							pfnNormalizePath(Info.m_aPath);
						std::string CheckedPath(Info.m_aPath);
						// 字符串准备完成后，检查键与显示结果一起无分配发布。
						m_CheckedManualPath.swap(CheckedPath);
						m_Info = Info;
					}
					catch(const std::bad_alloc &)
					{
						return false;
					}
					catch(const std::length_error &)
					{
						return false;
					}
				}
				else
				{
					// 路径与结果一起无分配发布，失败准备不会破坏已有显示。
					m_CheckedManualPath.swap(m_pProbe->m_ManualPath);
					m_Info = m_pProbe->m_Info;
				}
				m_HasResult = true;
				Published = true;
			}
			else if(!m_pProbe->m_Succeeded)
				m_RefreshRequested = true;
		}
		else
			m_RefreshRequested = true;
		// ABORTED可能仍在运行；不读取其结果，线程池继续持有任务。
		m_pProbe.reset();
		return Published;
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
