#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_RACE_DEMO_STORAGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_RACE_DEMO_STORAGE_H

#include "media_paths.h"

#include <game/client/race.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace qm_race_demo
{
	class CStorageOperations
	{
		IStorage &m_Storage;

		std::string PhysicalPath(const char *pPath, int Type)
		{
			if(Type == IStorage::TYPE_ABSOLUTE)
				return str_length(pPath) < IO_MAX_PATH_LENGTH ? pPath : "";
			char aRoot[IO_MAX_PATH_LENGTH];
			m_Storage.GetCompletePath(Type, "", aRoot, sizeof(aRoot));
			return qmclient::media_paths::Join(aRoot[0] ? aRoot : ".", pPath);
		}

		bool Exists(const char *pPath, int Type, bool &Present)
		{
			const std::string Physical = PhysicalPath(pPath, Type);
			if(Physical.empty())
				return false;
			std::error_code Error;
			const auto Status = std::filesystem::symlink_status(std::filesystem::u8path(Physical), Error);
			if(Error && Error != std::errc::no_such_file_or_directory)
				return false;
			Present = std::filesystem::exists(Status);
			return true;
		}

	public:
		explicit CStorageOperations(IStorage &Storage) :
			m_Storage(Storage) {}

		template<typename TVisitor>
		bool Enumerate(const char *pDirectory, int Type, const TVisitor &Visitor)
		{
			const std::string Physical = PhysicalPath(pDirectory, Type);
			if(Physical.empty())
				return false;
			std::error_code Error;
			std::filesystem::directory_iterator Iterator(std::filesystem::u8path(Physical), Error), End;
			if(Error)
				return false;
			while(Iterator != End)
			{
				const bool IsDirectory = Iterator->is_directory(Error);
				if(Error)
					return false;
				const auto Utf8Name = Iterator->path().filename().u8string();
				const std::string Name(reinterpret_cast<const char *>(Utf8Name.data()), Utf8Name.size());
				if(str_utf8_check(Name.c_str()) && !Visitor(Name.c_str(), IsDirectory))
					return false;
				Iterator.increment(Error);
				if(Error)
					return false;
			}
			return true;
		}

		bool DestinationAvailable(const char *pPath, int Type)
		{
			bool Present;
			return Exists(pPath, Type, Present) && !Present;
		}

		bool Rename(const char *pSource, const char *pDestination, int Type)
		{
			if(PhysicalPath(pSource, Type).empty() || PhysicalPath(pDestination, Type).empty())
				return false;
			// 禁止 Windows 在重命名失败后删除目标再重试。
			return m_Storage.RenameFile(pSource, pDestination, Type, false);
		}

		bool Remove(const char *pPath, int Type)
		{
			if(PhysicalPath(pPath, Type).empty())
				return false;
			if(m_Storage.RemoveFile(pPath, Type))
				return true;
			bool Present;
			return Exists(pPath, Type, Present) && !Present;
		}
	};

	class CFinalizer
	{
		struct CRequest
		{
			char m_aSource[IO_MAX_PATH_LENGTH] = "";
			char m_aMap[IO_MAX_PATH_LENGTH] = "";
			char m_aPlayer[IO_MAX_PATH_LENGTH] = "";
			int m_Time = -1;
			bool m_WithPlayer = false;
		};

		CRequest m_Request;
		bool m_Pending = false;
		bool m_Busy = false;
		bool m_ReportedFailure = false;
		std::chrono::nanoseconds m_NextAttempt{0};

		template<typename TNotify>
		void ReportOnce(const TNotify &Notify, bool AfterCommit) noexcept
		{
			if(m_ReportedFailure)
				return;
			m_ReportedFailure = true;
			try
			{
				Notify(AfterCommit);
			}
			catch(...)
			{
			}
		}

	public:
		enum class EOutcome
		{
			IDLE,
			DEFERRED,
			RETRY,
			SAVED,
			DISCARDED,
			SAVED_CLEANUP_FAILED,
			DISCARDED_CLEANUP_FAILED,
		};

		bool Pending() const { return m_Pending; }
		bool CanStartCapture() const { return !m_Pending && !m_Busy; }

		// 首次停止时固定文件与命名上下文；复位或后续失败不能改写待处理录像。
		bool Queue(const char *pSource, const char *pMap, const char *pPlayer, bool WithPlayer, int Time) noexcept
		{
			if(!CanStartCapture() || pSource[0] == '\0' || str_length(pSource) >= IO_MAX_PATH_LENGTH ||
				str_length(pMap) >= IO_MAX_PATH_LENGTH || str_length(pPlayer) >= IO_MAX_PATH_LENGTH ||
				str_find(pMap, "/") || str_find(pMap, "\\") || str_find(pPlayer, "/") || str_find(pPlayer, "\\"))
				return false;
			str_copy(m_Request.m_aSource, pSource);
			str_copy(m_Request.m_aMap, pMap);
			str_copy(m_Request.m_aPlayer, pPlayer);
			m_Request.m_WithPlayer = WithPlayer;
			m_Request.m_Time = Time;
			m_NextAttempt = std::chrono::nanoseconds(0);
			m_ReportedFailure = false;
			m_Pending = true;
			return true;
		}

		template<typename TOperations, typename TStop, typename TProgress, typename TNotify>
		EOutcome Try(std::chrono::nanoseconds Now, TOperations &Operations, const TStop &Stop, const TProgress &Progress, const TNotify &Notify, bool Force = false) noexcept
		{
			if(!m_Pending)
				return EOutcome::IDLE;
			if(m_Busy || (!Force && Now < m_NextAttempt))
				return EOutcome::DEFERRED;
			m_Busy = true;
			struct CBusyGuard
			{
				bool &m_Busy;
				~CBusyGuard() { m_Busy = false; }
			} BusyGuard{m_Busy};
			const auto Interval = std::chrono::seconds(1);
			m_NextAttempt = Now > std::chrono::nanoseconds::max() - Interval ? std::chrono::nanoseconds::max() : Now + Interval;
			const CRequest Request = m_Request;
			try
			{
				if(!Stop())
				{
					ReportOnce(Notify, false);
					return EOutcome::RETRY;
				}
				const int Type = qmclient::media_paths::StorageType(Request.m_aSource);
				if(Request.m_Time <= 0)
				{
					if(!Operations.Remove(Request.m_aSource, Type))
					{
						ReportOnce(Notify, false);
						return EOutcome::RETRY;
					}
					m_Pending = false;
					return EOutcome::DISCARDED;
				}

				char aDirectory[IO_MAX_PATH_LENGTH];
				str_copy(aDirectory, Request.m_aSource);
				if(fs_parent_dir(aDirectory) != 0)
				{
					ReportOnce(Notify, false);
					return EOutcome::RETRY;
				}
				char aName[IO_MAX_PATH_LENGTH];
				if(Request.m_WithPlayer)
					str_format(aName, sizeof(aName), "%s_%d.%03d_%s.demo", Request.m_aMap, Request.m_Time / 1000, Request.m_Time % 1000, Request.m_aPlayer);
				else
					str_format(aName, sizeof(aName), "%s_%d.%03d.demo", Request.m_aMap, Request.m_Time / 1000, Request.m_Time % 1000);
				const std::string Destination = qmclient::media_paths::Join(aDirectory, aName);
				std::vector<std::string> vCleanup;
				bool NewBest = true;
				const auto Collect = [&](const char *pFilename, bool IsDirectory) {
					if(IsDirectory)
						return true;
					const int Time = CRaceHelper::TimeFromDemoFilename(pFilename, Request.m_aMap, Request.m_WithPlayer ? Request.m_aPlayer : nullptr);
					if(Time < 0)
						return true;
					if(Time > 0 && NewBest)
					{
						if(Request.m_Time >= Time)
							NewBest = false;
						else
						{
							std::string Path = qmclient::media_paths::Join(aDirectory, pFilename);
							if(Path.empty())
								return false;
							vCleanup.push_back(std::move(Path));
						}
					}
					Progress();
					return true;
				};
				if(Destination.empty() || !Operations.Enumerate(aDirectory, Type, Collect))
				{
					ReportOnce(Notify, false);
					return EOutcome::RETRY;
				}
				if(NewBest)
				{
					if(!Operations.DestinationAvailable(Destination.c_str(), Type) ||
						!Operations.Rename(Request.m_aSource, Destination.c_str(), Type))
					{
						ReportOnce(Notify, false);
						return EOutcome::RETRY;
					}
				}
				else if(!Operations.Remove(Request.m_aSource, Type))
				{
					ReportOnce(Notify, false);
					return EOutcome::RETRY;
				}

				// 已保存或已有更好记录后才清理旧项，清理失败不重放已完成的保存。
				m_Pending = false;
				bool CleanupFailed = false;
				for(const auto &Path : vCleanup)
				{
					try
					{
						CleanupFailed |= !Operations.Remove(Path.c_str(), Type);
					}
					catch(...)
					{
						CleanupFailed = true;
					}
				}
				if(CleanupFailed)
				{
					ReportOnce(Notify, true);
					return NewBest ? EOutcome::SAVED_CLEANUP_FAILED : EOutcome::DISCARDED_CLEANUP_FAILED;
				}
				return NewBest ? EOutcome::SAVED : EOutcome::DISCARDED;
			}
			catch(...)
			{
				ReportOnce(Notify, false);
				return EOutcome::RETRY;
			}
		}
	};
}

#endif
