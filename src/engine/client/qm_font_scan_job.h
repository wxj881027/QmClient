#ifndef ENGINE_CLIENT_QM_FONT_SCAN_JOB_H
#define ENGINE_CLIENT_QM_FONT_SCAN_JOB_H

#include <base/hash.h>
#include <base/log.h>
#include <base/system.h>

#include <engine/shared/jobs.h>

#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// worker 只持有文件字节；FreeType、字形图集和候选列表由主线程分帧接收。
class CQmFontScanJob : public IJob
{
public:
	struct SFile
	{
		std::string m_Path;
		std::unique_ptr<void, decltype(&free)> m_pData{nullptr, &free};
		unsigned m_Size = 0;
		std::string m_Hash;
	};
	using FCancelled = std::function<bool()>;
	using FEnumerate = std::function<std::vector<std::string>(const FCancelled &)>;
	using FRead = std::function<bool(const char *, void **, unsigned *)>;

private:
	FEnumerate m_Enumerate;
	FRead m_Read;
	std::vector<SFile> m_vFiles;
	size_t m_Cursor = 0;
	bool m_Failed = false;

	void Scan()
	{
		if(State() == STATE_ABORTED)
			return;
		const FCancelled Cancelled = [this] { return State() == STATE_ABORTED; };
		// 枚举结果先落到独立变量：range-for 的初始化表达式在部分 libc++ 版本上
		// 抛出时会绕过调用方的 catch 直接 terminate，而枚举回调来自文件系统，
		// 抛异常是它的正常失败方式。这里显式接住并转成作业失败。
		std::vector<std::string> vPaths;
		try
		{
			vPaths = m_Enumerate(Cancelled);
		}
		catch(const std::exception &Error)
		{
			m_Failed = true;
			log_error("textrender/font", "Font enumeration stopped: %s", Error.what());
			return;
		}
		catch(...)
		{
			m_Failed = true;
			log_error("textrender/font", "Font enumeration stopped after an unexpected exception.");
			return;
		}
		for(const std::string &Path : vPaths)
		{
			if(Cancelled())
				return;
			void *pData = nullptr;
			unsigned Size = 0;
			bool Read;
			try
			{
				Read = m_Read(Path.c_str(), &pData, &Size);
			}
			catch(...)
			{
				// 读取边界可能已交出缓冲，再报告异常。
				free(pData);
				throw;
			}
			SFile File;
			File.m_pData.reset(pData);
			// 已开始的文件读取完成后接管缓冲，再响应取消。
			if(Cancelled())
				return;
			if(!Read || pData == nullptr || Size == 0)
				continue;
			File.m_Path = Path;
			File.m_Size = Size;
			char aHash[SHA256_MAXSTRSIZE];
			sha256_str(sha256(pData, Size), aHash, sizeof(aHash));
			File.m_Hash = aHash;
			m_vFiles.push_back(std::move(File));
		}
	}

	void Run() override
	{
		try
		{
			Scan();
		}
		catch(const std::exception &Error)
		{
			// 作业池不捕获异常；保留已完成的文件，回到主线程继续使用字体回退。
			m_Failed = true;
			log_error("textrender/font", "Font scan stopped: %s", Error.what());
		}
		catch(...)
		{
			m_Failed = true;
			log_error("textrender/font", "Font scan stopped after an unexpected exception.");
		}
		// 只有 worker 清理在途输出；STATE_ABORTED 不表示线程已经停止。
		if(State() == STATE_ABORTED)
			m_vFiles.clear();
	}

public:
	CQmFontScanJob(FEnumerate Enumerate, FRead Read) :
		m_Enumerate(std::move(Enumerate)), m_Read(std::move(Read))
	{
		Abortable(true);
	}

	static bool ReadAbsolute(const char *pPath, void **ppData, unsigned *pSize)
	{
		IOHANDLE File = io_open(pPath, IOFLAG_READ);
		if(File == nullptr)
			return false;
		const bool Result = io_read_all(File, ppData, pSize);
		io_close(File);
		return Result;
	}

	// 未完成时不暴露 worker 输出；每次仅转移一个文件的所有权。
	bool TakeNext(SFile &File)
	{
		if(State() != STATE_DONE || m_Cursor == m_vFiles.size())
			return false;
		File = std::move(m_vFiles[m_Cursor++]);
		return true;
	}

	bool Exhausted() const
	{
		return State() == STATE_DONE && m_Cursor == m_vFiles.size();
	}

	bool Failed() const
	{
		return State() == STATE_DONE && m_Failed;
	}
};

#endif
