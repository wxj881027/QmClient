#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_CAMERA_SESSION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_CAMERA_SESSION_H

#include "demo_camera_file.h"

#include <map>
#include <string_view>
#include <type_traits>
#include <utility>

namespace qm_demo_camera
{
	// 组件主线程串行使用；I/O回调不得重入本对象。失败编辑按录像身份保留。
	class CSessionStore
	{
		struct SSession
		{
			std::string m_Filename;
			std::string m_Sidecar;
			SProject m_Project;
			bool m_Dirty = false;
			bool m_SaveFailed = false;
			bool m_LoadFailed = false;
		};
		static_assert(std::is_nothrow_move_assignable_v<SSession>);
		SSession m_Current;
		std::map<std::string, SSession, std::less<>> m_Retained;
		bool m_PrepareFailed = false;

		template<typename FSave>
		static bool SaveOne(SSession &Session, FSave &Save) noexcept
		{
			if(!Session.m_Dirty)
				return true;
			try
			{
				if(Save(Session.m_Sidecar, Session.m_Project))
				{
					Session.m_Dirty = false;
					Session.m_SaveFailed = false;
					return true;
				}
			}
			catch(...)
			{
			}
			Session.m_SaveFailed = true;
			return false;
		}

	public:
		CSessionStore() = default;
		CSessionStore(const CSessionStore &) = delete;
		CSessionStore &operator=(const CSessionStore &) = delete;
		bool Matches(std::string_view Filename) const noexcept { return !Filename.empty() && m_Current.m_Filename == Filename; }
		// 只有Open成功且仍绑定当前录像时才编辑，组件负责遮蔽其他录像的只读视图。
		const SProject &Project() const noexcept { return m_Current.m_Project; }
		SProject &Project() noexcept { return m_Current.m_Project; }
		const std::string &Filename() const noexcept { return m_Current.m_Filename; }
		bool Dirty() const noexcept { return m_Current.m_Dirty || !m_Retained.empty(); }
		bool SaveFailed() const noexcept { return m_Current.m_SaveFailed || !m_Retained.empty(); }
		bool LoadFailed() const noexcept { return m_PrepareFailed || m_Current.m_LoadFailed; }
		size_t RetainedCount() const noexcept { return m_Retained.size(); }
		void Changed() noexcept
		{
			m_Current.m_Dirty = true;
			m_Current.m_LoadFailed = false;
		}

		template<typename FResolve, typename FLoad, typename FSave>
		bool Open(std::string_view Filename, FResolve &&Resolve, FLoad &&Load, FSave &&Save) noexcept
		{
			try
			{
				if(Filename.empty())
					return false;
				SSession Candidate;
				Candidate.m_Filename = Filename;
				Candidate.m_Sidecar = Resolve();
				if(Candidate.m_Sidecar.empty())
				{
					m_PrepareFailed = true;
					return false;
				}
				if(Matches(Filename) && m_Current.m_Sidecar == Candidate.m_Sidecar)
				{
					m_PrepareFailed = false;
					return true;
				}
				auto Retained = m_Retained.find(Candidate.m_Sidecar);
				if(Retained == m_Retained.end())
				{
					const ELoadResult Result = Load(Candidate.m_Sidecar, Candidate.m_Project);
					if(Result == ELoadResult::UNAVAILABLE)
					{
						m_PrepareFailed = true;
						return false;
					}
					Candidate.m_LoadFailed = Result == ELoadResult::FAILED;
				}
				if(!SaveOne(m_Current, Save))
				{
					// 先分配节点，再移动旧项目；分配失败仍完整保留当前编辑。
					auto Slot = m_Retained.try_emplace(m_Current.m_Sidecar).first;
					Slot->second = std::move(m_Current);
				}
				if(Retained != m_Retained.end())
				{
					m_Current = std::move(Retained->second);
					m_Retained.erase(Retained);
				}
				else
					m_Current = std::move(Candidate);
				m_PrepareFailed = false;
				return true;
			}
			catch(...)
			{
				m_PrepareFailed = true;
				return false;
			}
		}

		template<typename FSave>
		bool Save(FSave &&Save) noexcept
		{
			bool Success = SaveOne(m_Current, Save);
			for(auto It = m_Retained.begin(); It != m_Retained.end();)
			{
				if(SaveOne(It->second, Save))
					It = m_Retained.erase(It);
				else
				{
					Success = false;
					++It;
				}
			}
			return Success;
		}
		template<typename FSave>
		void Close(FSave &&Save) noexcept
		{
			this->Save(Save);
			if(!m_Current.m_Dirty)
				m_Current = {};
			m_PrepareFailed = false;
		}
	};
}

#endif
