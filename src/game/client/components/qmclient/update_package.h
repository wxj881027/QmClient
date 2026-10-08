#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_PACKAGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_PACKAGE_H

#include "update_request.h"

#include <base/fs.h>
#include <base/system.h>

#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <functional>

namespace qm_update
{
	// 参考 Ketch 的连续区间规划与分段边界检查，独立使用现有 HTTP/Job 接口实现。
	inline std::vector<CHttpByteRange> PlanPackageRanges(int64_t Size)
	{
		if(Size <= 0)
			return {};
		const int64_t Count = std::min<int64_t>(4, std::max<int64_t>(1, Size / (1024 * 1024)));
		std::vector<CHttpByteRange> vRanges;
		int64_t Offset = 0;
		for(int64_t Index = 0; Index < Count; ++Index)
		{
			const int64_t Length = Size / Count + (Index < Size % Count ? 1 : 0);
			vRanges.push_back({Offset, Offset + Length - 1, Size});
			Offset += Length;
		}
		return vRanges;
	}

	// 合并与 SHA-256 放后台，主线程只聚合四个请求的状态和进度。
	class CPackageMergeJob : public IJob
	{
		std::vector<std::pair<std::string, int64_t>> m_vFiles;
		std::string m_Destination;
		std::atomic<bool> m_Cancelled = false;
		bool m_Success = false;
		SHA256_DIGEST m_Digest{};

		void Run() override
		{
			const std::string Temporary = m_Destination + ".merge";
			IOHANDLE Output = io_open(Temporary.c_str(), IOFLAG_WRITE);
			if(!Output)
				return;
			SHA256_CTX Hash;
			sha256_init(&Hash);
			bool Success = true;
			unsigned char aBuffer[64 * 1024];
			for(const auto &[Path, Length] : m_vFiles)
			{
				IOHANDLE Input = io_open(Path.c_str(), IOFLAG_READ);
				if(!Input)
				{
					Success = false;
					break;
				}
				Success = io_length(Input) == Length;
				int64_t Remaining = Length;
				while(Success && Remaining > 0 && !m_Cancelled)
				{
					const unsigned Want = static_cast<unsigned>(std::min<int64_t>(sizeof(aBuffer), Remaining));
					const unsigned Read = io_read(Input, aBuffer, Want);
					Success = Read == Want && io_write(Output, aBuffer, Read) == Read;
					if(Success)
						sha256_update(&Hash, aBuffer, Read);
					Remaining -= Read;
				}
				Success &= Remaining == 0 && !io_error(Input);
				io_close(Input);
				if(!Success || m_Cancelled)
					break;
			}
			Success &= !io_error(Output);
			Success = io_close(Output) == 0 && Success;
			m_Digest = sha256_finish(&Hash);
			m_Success = Success && !m_Cancelled && fs_rename(Temporary.c_str(), m_Destination.c_str()) == 0;
			if(m_Cancelled)
			{
				fs_remove(m_Destination.c_str());
				m_Success = false;
			}
			if(!m_Success)
				fs_remove(Temporary.c_str());
			for(const auto &[Path, Length] : m_vFiles)
				fs_remove(Path.c_str());
		}

	public:
		CPackageMergeJob(std::vector<std::pair<std::string, int64_t>> vFiles, std::string Destination) :
			m_vFiles(std::move(vFiles)), m_Destination(std::move(Destination)) {}
		void Cancel()
		{
			m_Cancelled = true;
			fs_remove(m_Destination.c_str());
		}
		bool Success() const { return m_Success; }
		const SHA256_DIGEST &Digest() const { return m_Digest; }
	};

	// 聚合请求不交给 HTTP 引擎；子请求完成并合并后才发布 DONE，兼容现有验签路径。
	class CPackageFileCleanup : public IHttpRequest::IProgressCallback
	{
		std::string m_Path;
		std::atomic<bool> m_Cancelled = false;

	public:
		explicit CPackageFileCleanup(std::string Path) : m_Path(std::move(Path)) {}
		void OnProgress() override {}
		void OnCompletion(EHttpState) override
		{
			if(m_Cancelled)
				fs_remove(m_Path.c_str());
		}
		void Cancel()
		{
			m_Cancelled = true;
			// 回调发生在关闭/重命名之后；两边都尝试，覆盖完成状态发布前的取消竞态。
			fs_remove(m_Path.c_str());
		}
	};

	class CPackageDownload : public IHttpRequest
	{
	public:
		using TStart = std::function<void(const std::shared_ptr<IHttpRequest> &)>;
		using TAddJob = std::function<void(std::shared_ptr<IJob>)>;
		using TCreate = std::function<std::shared_ptr<IHttpRequest>(const std::string &)>;

	private:
		struct CPart
		{
			std::shared_ptr<IHttpRequest> m_pRequest;
			std::optional<CHttpByteRange> m_Range;
			std::string m_Path;
			CProgressDeadline m_Deadline;
			bool m_Started = false;
			unsigned m_Retries = 0;
			std::shared_ptr<CPackageFileCleanup> m_pCleanup;
		};
		enum class EPhase
		{
			PROBE,
			DOWNLOAD,
			MERGE
		};
		IStorage *m_pStorage;
		std::string m_Destination;
		int64_t m_MaxSize;
		TStart m_Start;
		TAddJob m_AddJob;
		TCreate m_Create;
		std::vector<CPart> m_vParts;
		std::shared_ptr<CPackageMergeJob> m_pMerge;
		EPhase m_Phase = EPhase::PROBE;
		bool m_TransferStarted = false;
		bool m_ResetSpeedWindow = false;
		bool m_LocalError = false;

		void StartPart(CPart &Part)
		{
			Part.m_pRequest = m_Create(Url());
			Part.m_pRequest->Timeout(CTimeout{5000, 0, 1, 20});
			Part.m_pRequest->LogProgress(HTTPLOG::FAILURE);
			Part.m_pRequest->MaxResponseSize(m_MaxSize);
			Part.m_pRequest->SkipByFileTime(false);
			if(Part.m_Range)
				Part.m_pRequest->ByteRange(Part.m_Range->m_First, Part.m_Range->m_Last);
			if(!Part.m_Path.empty())
			{
				Part.m_pRequest->WriteToFile(m_pStorage, Part.m_Path.c_str(), IStorage::TYPE_SAVE);
				char aAbsolute[IO_MAX_PATH_LENGTH];
				m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, Part.m_Path.c_str(), aAbsolute, sizeof(aAbsolute));
				Part.m_pCleanup = std::make_shared<CPackageFileCleanup>(aAbsolute);
				Part.m_pRequest->SetProgressCallback(Part.m_pCleanup);
			}
			Part.m_Started = false;
			m_Start(Part.m_pRequest);
		}
		void Complete(EHttpState State, int Status = 0, std::optional<SHA256_DIGEST> Digest = {})
		{
			m_StatusCode = Status;
			m_ActualSha256 = Digest;
			{
				std::unique_lock Lock(m_WaitMutex);
				m_State = State;
			}
			m_WaitCondition.notify_all();
		}
		void Fail(const std::shared_ptr<IHttpRequest> &Request)
		{
			m_ResultRetryAfterSeconds = Request->ResultRetryAfterSeconds();
			m_ResultUsedProxy = Request->CompletedUsedProxy();
			Proxy(Request->ProxyUrl());
			for(auto &Part : m_vParts)
			{
				if(Part.m_pCleanup)
					Part.m_pCleanup->Cancel();
				if(!Part.m_pRequest->Done())
					Part.m_pRequest->Abort();
			}
			Complete(EHttpState::ERROR, Request->CompletedStatusCode());
		}
		void StartPayload(std::optional<int64_t> Size)
		{
			for(auto &Part : m_vParts)
			{
				if(Part.m_pCleanup)
					Part.m_pCleanup->Cancel();
				if(!Part.m_pRequest->Done())
					Part.m_pRequest->Abort();
			}
			m_Phase = EPhase::DOWNLOAD;
			m_vParts.clear();
			m_TransferStarted = false;
			m_Current = 0;
			m_Size = 0;
			if(Size)
			{
				m_Size = *Size;
				for(const auto &Range : PlanPackageRanges(*Size))
					m_vParts.push_back({nullptr, Range, m_Destination + ".part" + std::to_string(m_vParts.size()), {}, false, 0});
			}
			else
				m_vParts.push_back({nullptr, {}, m_Destination, {}, false, 0});
			for(auto &Part : m_vParts)
				StartPart(Part);
		}

	public:
		CPackageDownload(const std::string &Url, IStorage *pStorage, const std::string &Destination, int64_t MaxSize, TStart Start, TAddJob AddJob, TCreate Create = [](const std::string &Address) -> std::shared_ptr<IHttpRequest> { return HttpGet(Address.c_str()); }) :
			IHttpRequest(Url.c_str()), m_pStorage(pStorage), m_Destination(Destination), m_MaxSize(MaxSize), m_Start(std::move(Start)), m_AddJob(std::move(AddJob)), m_Create(std::move(Create))
		{
			m_WriteToMemory = false;
			m_State = EHttpState::RUNNING;
			m_vParts.push_back({nullptr, CHttpByteRange{0, 0, 0}, {}, {}, false, 0});
			StartPart(m_vParts.front());
		}
		~CPackageDownload() override { Abort(); }
		void Header(const char *) override {}
		void Abort() override
		{
			if(Done())
				return;
			IHttpRequest::Abort();
			for(auto &Part : m_vParts)
			{
				if(Part.m_pCleanup)
					Part.m_pCleanup->Cancel();
				if(!Part.m_pRequest->Done())
					Part.m_pRequest->Abort();
			}
			if(m_pMerge)
				m_pMerge->Cancel();
			Complete(EHttpState::ABORTED);
		}
		bool Downloading() const { return !Done() && m_Phase == EPhase::DOWNLOAD && m_TransferStarted; }
		bool LocalError() const { return m_LocalError; }
		bool TakeSpeedWindowReset()
		{
			const bool Reset = m_ResetSpeedWindow;
			m_ResetSpeedWindow = false;
			return Reset;
		}
		void Poll(double Now)
		{
			if(Done())
				return;
			if(m_Phase == EPhase::MERGE)
			{
				if(m_pMerge->State() == IJob::STATE_DONE)
				{
					m_LocalError = !m_pMerge->Success();
					if(m_LocalError)
						for(auto &Part : m_vParts)
							Part.m_pCleanup->Cancel();
					Complete(m_LocalError ? EHttpState::ERROR : EHttpState::DONE, m_LocalError ? 0 : 200, m_pMerge->Digest());
				}
				return;
			}
			bool Finished = true;
			double Bytes = 0;
			for(auto &Part : m_vParts)
			{
				const auto &Request = Part.m_pRequest;
				if(Request->State() != EHttpState::QUEUED && !Part.m_Started)
				{
					Part.m_Started = true;
					Part.m_Deadline.Begin(Now);
					if(m_Phase == EPhase::DOWNLOAD && !m_TransferStarted)
					{
						m_TransferStarted = true;
						m_ResetSpeedWindow = true;
					}
				}
				if(!Request->Done())
				{
					Finished = false;
					if(Part.m_Started && Part.m_Deadline.Expired(Now, Request->Current()))
						Request->Abort();
				}
				Bytes += Request->Current();
			}
			if(m_Phase == EPhase::PROBE)
			{
				if(!Finished)
					return;
				const auto Request = m_vParts.front().m_pRequest;
				const auto Range = Request->ResultContentRange();
				if(Request->State() == EHttpState::DONE && Range && Range->m_Total <= m_MaxSize)
					StartPayload(Range->m_Total);
				else if(Request->CompletedStatusCode() == 200 || Request->CompletedStatusCode() == 206 || Request->CompletedStatusCode() == 416)
					StartPayload({});
				else
					Fail(Request);
				return;
			}
			m_Current = Bytes;
			if(!m_vParts.front().m_Range)
				m_Size = m_vParts.front().m_pRequest->Size();
			m_Progress = m_Size > 0 ? static_cast<int>(std::min(100.0, 100 * Bytes / m_Size.load())) : 0;
			for(auto &Part : m_vParts)
			{
				const auto Request = Part.m_pRequest;
				if(!Request->Done())
					continue;
				const auto Range = Request->ResultContentRange();
				const bool Valid = Request->State() == EHttpState::DONE &&
						   (Part.m_Range ? Request->StatusCode() == 206 && Range && Range->m_Total == Part.m_Range->m_Total &&
									   Range->m_First == Part.m_Range->m_First && Range->m_Last == Part.m_Range->m_Last :
								   Request->StatusCode() == 200);
				if(Valid)
					continue;
				// 探测支持 Range 不代表后续网关始终支持；整包回退仍需最终验签。
				if(Part.m_Range && (Request->CompletedStatusCode() == 200 ||
							   (Request->CompletedStatusCode() == 206 && (!Range || Range->m_Total != Part.m_Range->m_Total))))
				{
					StartPayload({});
					return;
				}
				// 一个分段临时断线只重试该段一次，已完成的其他分段不重下。
				if(Part.m_Range && Part.m_Retries == 0 && SourceRetryDelay(Request.get()) == 0 &&
					(Request->CompletedStatusCode() == 0 || Request->CompletedStatusCode() >= 500 ||
						(Request->CompletedStatusCode() == 206 && Range && Range->m_First == Part.m_Range->m_First && Range->m_Last == Part.m_Range->m_Last)))
				{
					++Part.m_Retries;
					StartPart(Part);
					Finished = false;
					continue;
				}
				Fail(Request);
				return;
			}
			if(!Finished)
				return;
			if(!m_vParts.front().m_Range)
			{
				Complete(EHttpState::DONE, 200, m_vParts.front().m_pRequest->ResultSha256());
				return;
			}
			std::vector<std::pair<std::string, int64_t>> vFiles;
			char aAbsolute[IO_MAX_PATH_LENGTH];
			for(const auto &Part : m_vParts)
			{
				m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, Part.m_Path.c_str(), aAbsolute, sizeof(aAbsolute));
				vFiles.emplace_back(aAbsolute, Part.m_Range->Length());
			}
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, m_Destination.c_str(), aAbsolute, sizeof(aAbsolute));
			m_pMerge = std::make_shared<CPackageMergeJob>(std::move(vFiles), aAbsolute);
			m_Phase = EPhase::MERGE;
			m_AddJob(m_pMerge);
		}
	};
}

#endif
