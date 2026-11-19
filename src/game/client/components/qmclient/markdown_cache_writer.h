#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MARKDOWN_CACHE_WRITER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MARKDOWN_CACHE_WRITER_H

#include <base/fs.h>
#include <base/system.h>

#include <engine/shared/jobs.h>
#include <engine/shared/jsonwriter.h>

#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

// 每个缓存文件使用独立实例；界面立即更新，磁盘只需保留最新完整快照。
class CQmMarkdownCacheWriter
{
public:
	class CFileOperations
	{
	public:
		virtual ~CFileOperations() = default;
		virtual IOHANDLE Open(const char *pPath) { return io_open(pPath, IOFLAG_WRITE); }
		virtual unsigned Write(IOHANDLE File, const void *pData, unsigned Size) { return io_write(File, pData, Size); }
		virtual int Sync(IOHANDLE File) { return io_sync(File); }
		virtual int Error(IOHANDLE File) { return io_error(File); }
		virtual int Close(IOHANDLE File) noexcept { return io_close(File); }
		virtual bool Publish(const char *pTemporary, const char *pTarget) { return fs_rename(pTemporary, pTarget, false) == 0; }
		virtual void Remove(const char *pPath) { (void)fs_remove(pPath); }
	};

private:
	struct SRequest
	{
		std::string m_Path;
		int m_CacheVersion;
		int m_Version;
		std::string m_Markdown;
	};
	static_assert(std::is_nothrow_move_constructible_v<SRequest>);
	struct SState
	{
		std::mutex m_Mutex;
		std::optional<SRequest> m_Pending;
		bool m_Running = false;
		CFileOperations m_DefaultOperations;
		std::shared_ptr<CFileOperations> m_pOperations;

		explicit SState(std::shared_ptr<CFileOperations> pOperations) :
			m_pOperations(std::move(pOperations)) {}
	};
	class CWriteJob : public IJob
	{
		std::shared_ptr<SState> m_pState;

		bool Write(const SRequest &Request)
		{
			// 全部序列化与路径准备成功后才创建候选文件，不访问组件或 Storage。
			CJsonStringWriter Writer;
			Writer.BeginObject();
			Writer.WriteAttribute("cache_version");
			Writer.WriteIntValue(Request.m_CacheVersion);
			Writer.WriteAttribute("version");
			Writer.WriteIntValue(Request.m_Version);
			Writer.WriteAttribute("markdown");
			Writer.WriteStrValue(Request.m_Markdown.c_str());
			Writer.EndObject();
			const std::string Output = Writer.GetOutputString();
			if(Output.size() > std::numeric_limits<unsigned>::max())
				return false;
			static std::atomic<uint64_t> s_NextTemporary{0};
			const std::string Temporary = Request.m_Path + "." + std::to_string(pid()) + "." + std::to_string(s_NextTemporary.fetch_add(1, std::memory_order_relaxed)) + ".tmp";
			if(fs_is_file(Temporary.c_str()) || fs_is_dir(Temporary.c_str()) || fs_makedir_rec_for(Request.m_Path.c_str()) != 0)
				return false;
			auto &Operations = m_pState->m_pOperations ? *m_pState->m_pOperations : m_pState->m_DefaultOperations;
			struct STemporary
			{
				CFileOperations &m_Operations;
				const std::string &m_Path;
				bool m_Owned = false;
				~STemporary()
				{
					if(m_Owned)
					{
						try
						{
							m_Operations.Remove(m_Path.c_str());
						}
						catch(...)
						{
						}
					}
				}
			} Candidate{Operations, Temporary};
			const auto Close = [&Operations](void *pFile) { Operations.Close(pFile); };
			std::unique_ptr<void, decltype(Close)> File(Operations.Open(Temporary.c_str()), Close);
			if(!File)
				return false;
			Candidate.m_Owned = true;
			const unsigned Size = static_cast<unsigned>(Output.size());
			const bool Written = Operations.Write(File.get(), Output.data(), Size) == Size;
			const bool Synced = Operations.Sync(File.get()) == 0;
			const bool Clean = Operations.Error(File.get()) == 0;
			const bool Closed = Operations.Close(File.release()) == 0;
			if(!Written || !Synced || !Clean || !Closed || !Operations.Publish(Temporary.c_str(), Request.m_Path.c_str()))
				return false;
			Candidate.m_Owned = false;
			return true;
		}

		void Retry(SRequest Request)
		{
			const std::lock_guard<std::mutex> Lock(m_pState->m_Mutex);
			// 写入期间到达的更新优先；失败快照不能覆盖它。
			if(!m_pState->m_Pending)
				m_pState->m_Pending.emplace(std::move(Request));
			m_pState->m_Running = false;
		}

		void Run() override
		{
			while(true)
			{
				SRequest Request;
				{
					const std::lock_guard<std::mutex> Lock(m_pState->m_Mutex);
					if(!m_pState->m_Pending)
					{
						m_pState->m_Running = false;
						return;
					}
					Request = std::move(*m_pState->m_Pending);
					m_pState->m_Pending.reset();
				}
				bool Success = false;
				try
				{
					Success = Write(Request);
				}
				catch(const std::bad_alloc &)
				{
				}
				catch(const std::length_error &)
				{
				}
				catch(...)
				{
					Retry(std::move(Request));
					throw;
				}
				if(!Success)
				{
					Retry(std::move(Request));
					return;
				}
			}
		}

	public:
		explicit CWriteJob(std::shared_ptr<SState> pState) :
			m_pState(std::move(pState)) {}
	};
	std::shared_ptr<SState> m_pState;

public:
	explicit CQmMarkdownCacheWriter(std::shared_ptr<CFileOperations> pOperations = {}) :
		m_pState(std::make_shared<SState>(std::move(pOperations))) {}

	// Submit 只登记异步任务；拒绝或抛异常表示任务未被接收，不能同步执行 Run。
	template<typename TSubmit>
	bool TryEnqueue(std::string_view Path, int CacheVersion, int Version, std::string_view Markdown, TSubmit &&Submit)
	{
		try
		{
			SRequest Candidate{std::string(Path), CacheVersion, Version, std::string(Markdown)};
			const std::lock_guard<std::mutex> Lock(m_pState->m_Mutex);
			if(!m_pState->m_Running)
			{
				auto pJob = std::make_shared<CWriteJob>(m_pState);
				if(!std::forward<TSubmit>(Submit)(std::move(pJob)))
					return false;
				m_pState->m_Running = true;
			}
			// string/optional 的移动不分配，接收任务后不会发布半份请求。
			m_pState->m_Pending.emplace(std::move(Candidate));
			return true;
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

	template<typename TSubmit>
	bool TryFlush(TSubmit &&Submit)
	{
		try
		{
			const std::lock_guard<std::mutex> Lock(m_pState->m_Mutex);
			if(m_pState->m_Running || !m_pState->m_Pending)
				return true;
			auto pJob = std::make_shared<CWriteJob>(m_pState);
			if(!std::forward<TSubmit>(Submit)(std::move(pJob)))
				return false;
			m_pState->m_Running = true;
			return true;
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

	bool NeedsRetry() const
	{
		const std::lock_guard<std::mutex> Lock(m_pState->m_Mutex);
		return m_pState->m_Pending.has_value() && !m_pState->m_Running;
	}
};

#endif
