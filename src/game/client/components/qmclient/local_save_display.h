#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_LOCAL_SAVE_DISPLAY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_LOCAL_SAVE_DISPLAY_H

#include <base/system.h>

#include <engine/shared/jobs.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace qm_local_saves
{
	struct SLocalSaveDisplayEntry
	{
		std::string m_Time;
		std::string m_Players;
		std::string m_Map;
		std::string m_Code;
		std::string m_RawLine;
	};

	inline void TrimDisplayField(std::string &Field)
	{
		while(!Field.empty() && str_isspace(Field.front()))
			Field.erase(Field.begin());
		while(!Field.empty() && str_isspace(Field.back()))
			Field.pop_back();
	}

	inline std::array<std::string, 4> ParseSaveCsvFields(const char *pLine)
	{
		std::array<std::string, 4> aFields;
		int FieldIndex = 0;
		bool InQuotes = false;

		for(int CharIndex = 0; pLine[CharIndex] != '\0' && FieldIndex < (int)aFields.size(); ++CharIndex)
		{
			if(pLine[CharIndex] == '"')
			{
				if(InQuotes && pLine[CharIndex + 1] == '"')
				{
					aFields[FieldIndex].push_back('"');
					++CharIndex;
				}
				else
				{
					InQuotes = !InQuotes;
				}
			}
			else if(pLine[CharIndex] == ',' && !InQuotes)
			{
				++FieldIndex;
			}
			else
			{
				aFields[FieldIndex].push_back(pLine[CharIndex]);
			}
		}

		for(std::string &Field : aFields)
			TrimDisplayField(Field);
		return aFields;
	}

	template<typename TAllocator = std::allocator<SLocalSaveDisplayEntry>>
	inline std::vector<SLocalSaveDisplayEntry, TAllocator> ParseEntries(const char *pFileContent, const TAllocator &Allocator = {})
	{
		std::vector<SLocalSaveDisplayEntry, TAllocator> vEntries(Allocator);
		const char *pCursor = pFileContent;
		char aLine[2048];
		bool FirstLine = true;
		while((pCursor = str_next_token(pCursor, "\n", aLine, sizeof(aLine))))
		{
			str_utf8_trim_right(aLine);
			if(aLine[0] == '\0')
				continue;
			if(FirstLine)
			{
				FirstLine = false;
				if(str_startswith(aLine, "Time"))
					continue;
			}

			std::array<std::string, 4> aFields = ParseSaveCsvFields(aLine);
			SLocalSaveDisplayEntry Entry;
			Entry.m_Time = aFields[0];
			Entry.m_Players = aFields[1];
			Entry.m_Map = aFields[2];
			Entry.m_Code = aFields[3];
			Entry.m_RawLine = aLine;
			vEntries.push_back(std::move(Entry));
		}

		return vEntries;
	}

	struct CDisplayFileOperations
	{
		IOHANDLE Open(const char *pPath, bool &NotFound) const { return io_open_read_checked(pPath, &NotFound); }
		char *Read(IOHANDLE File) const noexcept { return io_read_all_str(File); }
		bool Error(IOHANDLE File) const noexcept { return io_error(File) != 0; }
		bool Close(IOHANDLE File) const noexcept { return io_close(File) == 0; }
		void Free(char *pContent) const noexcept { free(pContent); }
	};

	// 候选解析完成前不改显示结果；句柄和正文在失败或异常时仍自动回收。
	template<typename TOperations, typename TAllocator>
	bool ReadDisplayEntries(TOperations &Operations, const char *pPath,
		std::vector<SLocalSaveDisplayEntry, TAllocator> &vOutput, bool &FileExists) noexcept
	{
		try
		{
			bool NotFound = false;
			struct CFile
			{
				TOperations &m_Operations;
				IOHANDLE m_File;
				~CFile()
				{
					if(m_File)
						m_Operations.Close(m_File);
				}
			} File{Operations, Operations.Open(pPath, NotFound)};
			if(!File.m_File)
			{
				if(!NotFound)
					return false;
				vOutput.clear();
				FileExists = false;
				return true;
			}
			const auto Free = [&Operations](char *pContent) noexcept { Operations.Free(pContent); };
			std::unique_ptr<char, decltype(Free)> pContent(Operations.Read(File.m_File), Free);
			const bool ReadError = Operations.Error(File.m_File);
			const bool Closed = Operations.Close(std::exchange(File.m_File, nullptr));
			if(!pContent || ReadError || !Closed)
				return false;
			auto vCandidate = ParseEntries(pContent.get(), vOutput.get_allocator());
			vOutput.swap(vCandidate);
			FileExists = true;
			return true;
		}
		catch(...)
		{
			return false;
		}
	}
}

// 任务只拥有路径和解析结果；菜单释放后仍可安全完成，不访问客户端或 Storage。
class CQmLocalSaveDisplayCache
{
public:
	class CLoadJob : public IJob
	{
		std::string m_Path;
		void Run() override
		{
			qm_local_saves::CDisplayFileOperations Operations;
			m_Succeeded = qm_local_saves::ReadDisplayEntries(Operations, m_Path.c_str(), m_vEntries, m_FileExists);
		}

	public:
		bool m_Succeeded = false;
		bool m_FileExists = false;
		std::vector<qm_local_saves::SLocalSaveDisplayEntry> m_vEntries;
		explicit CLoadJob(const char *pPath) :
			m_Path(pPath) {}
	};

private:
	std::shared_ptr<CLoadJob> m_pJob;
	std::vector<qm_local_saves::SLocalSaveDisplayEntry> m_vEntries;
	int64_t m_LastRequest = 0;
	bool m_Requested = false;
	bool m_Ready = false;
	bool m_FileExists = false;

public:
	// 主线程只发布成功且真正完成的结果；拒收和取消不登记永久等待。
	template<typename TSubmit, typename TFactory>
	bool Refresh(const char *pPath, int64_t Now, int64_t Interval, TSubmit &&Submit, TFactory &&Factory) noexcept
	{
		if(m_pJob)
		{
			const auto State = m_pJob->State();
			if(State != IJob::STATE_DONE && State != IJob::STATE_ABORTED)
				return false;
			// 已取消作业可能仍在运行，不能读取其结果。
			if(State == IJob::STATE_DONE && m_pJob->m_Succeeded)
			{
				m_vEntries.swap(m_pJob->m_vEntries);
				m_FileExists = m_pJob->m_FileExists;
				m_Ready = true;
			}
			m_pJob.reset();
		}
		if(!pPath || !pPath[0])
			return false;
		if(m_Requested && Now >= m_LastRequest && Interval >= 0 &&
			static_cast<uint64_t>(Now) - static_cast<uint64_t>(m_LastRequest) <= static_cast<uint64_t>(Interval))
			return false;
		// 失败尝试也沿用刷新间隔，避免每帧重复申请；接收前不发布在途任务。
		m_LastRequest = Now;
		m_Requested = true;
		try
		{
			auto pCandidate = std::forward<TFactory>(Factory)(pPath);
			if(!pCandidate || !std::forward<TSubmit>(Submit)(pCandidate))
				return false;
			m_pJob = std::move(pCandidate);
			return true;
		}
		catch(...)
		{
			return false;
		}
	}

	template<typename TSubmit>
	bool Refresh(const char *pPath, int64_t Now, int64_t Interval, TSubmit &&Submit) noexcept
	{
		return Refresh(pPath, Now, Interval, std::forward<TSubmit>(Submit),
			[](const char *pJobPath) { return std::make_shared<CLoadJob>(pJobPath); });
	}

	bool Ready() const { return m_Ready; }
	bool FileExists() const { return m_FileExists; }
	const std::vector<qm_local_saves::SLocalSaveDisplayEntry> &Entries() const { return m_vEntries; }
	void Reset()
	{
		m_pJob.reset();
		m_vEntries.clear();
		m_LastRequest = 0;
		m_Requested = false;
		m_Ready = false;
		m_FileExists = false;
	}
};

#endif
