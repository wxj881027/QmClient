#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BG_DRAW_PERSISTENCE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BG_DRAW_PERSISTENCE_H

#include <base/str.h>
#include <base/system.h>

#include <engine/storage.h>

#include <game/client/components/tclient/bg_draw_file.h>

#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <string_view>

namespace qm_bg_draw_persistence
{
	inline constexpr int MAX_ITEMS_TO_LOAD = 65536;

	class CFileOperations
	{
	public:
		virtual ~CFileOperations() = default;
		virtual bool Write(IOHANDLE File, const CBgDrawItemData &Data) { return BgDrawFile::Write(File, Data); }
		virtual bool Read(IOHANDLE File, CBgDrawItemData &Data) { return BgDrawFile::Read(File, Data); }
		virtual int Sync(IOHANDLE File) { return io_sync(File); }
		virtual int Error(IOHANDLE File) { return io_error(File); }
		virtual int Close(IOHANDLE File) noexcept { return io_close(File); }
		// false或异常必须保留旧目标，不能先删除再重试。
		virtual bool Publish(IStorage &Storage, const char *pTemporary, const char *pTarget)
		{
			return Storage.RenameFile(pTemporary, pTarget, IStorage::TYPE_SAVE, false);
		}
	};

	struct SFileCloser
	{
		CFileOperations &m_Operations;
		void operator()(void *pFile) const noexcept { m_Operations.Close(pFile); }
	};

	struct STemporaryFile
	{
		IStorage &m_Storage;
		const std::string &m_Path;
		bool m_Owned = false;
		~STemporaryFile()
		{
			if(m_Owned)
			{
				try
				{
					m_Storage.RemoveFile(m_Path.c_str(), IStorage::TYPE_SAVE);
				}
				catch(...)
				{
				}
			}
		}
	};

	inline std::string TemporaryPath(const char *pTarget)
	{
		return std::string(pTarget) + ".qm-bgdraw." + std::to_string(pid()) + ".tmp";
	}

	inline bool ValidPath(const char *pPath)
	{
		return pPath && pPath[0] && pPath[0] != '/' && pPath[0] != '\\' &&
		       !str_find(pPath, "../") && !str_find(pPath, "..\\")
#if defined(CONF_FAMILY_WINDOWS)
		       && pPath[1] != ':'
#endif
			;
	}

	inline bool FitsStoragePath(IStorage &Storage, std::string_view Path)
	{
		char aRoot[IO_MAX_PATH_LENGTH] = "";
		Storage.GetCompletePath(IStorage::TYPE_SAVE, "", aRoot, sizeof(aRoot));
		const size_t RootLength = std::strlen(aRoot);
		return RootLength > 0 && RootLength + 1 < sizeof(aRoot) && Path.size() < sizeof(aRoot) - RootLength - 1;
	}

	struct SSaveResult
	{
		bool m_Success = false;
		bool m_FolderFailed = false;
		bool m_WriteFailed = false;
		int m_Written = 0;
	};

	// Items同步借用当前笔画；提示在返回后发出，文件仍打开时不调用UI。
	template<typename TItems>
	SSaveResult Save(IStorage *pStorage, const char *pTarget, const TItems &Items, CFileOperations *pOperations = nullptr) noexcept
	{
		SSaveResult Result;
		try
		{
			if(!pStorage || !ValidPath(pTarget))
				return Result;
			const std::string Temporary = TemporaryPath(pTarget);
			if(!FitsStoragePath(*pStorage, Temporary) || pStorage->FileExists(Temporary.c_str(), IStorage::TYPE_SAVE) ||
				pStorage->FolderExists(Temporary.c_str(), IStorage::TYPE_SAVE))
				return Result;
			if(!pStorage->CreateFolder("bgdraw", IStorage::TYPE_SAVE) && !pStorage->FolderExists("bgdraw", IStorage::TYPE_SAVE))
			{
				Result.m_FolderFailed = true;
				return Result;
			}
			CFileOperations Default;
			auto &Operations = pOperations ? *pOperations : Default;
			STemporaryFile Candidate{*pStorage, Temporary};
			// 已确认候选不存在，从创建尝试开始接管清理，覆盖打开后包装句柄失败。
			Candidate.m_Owned = true;
			std::unique_ptr<void, SFileCloser> File(pStorage->OpenFile(Temporary.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE), {Operations});
			if(!File)
				return Result;
			for(const auto &Item : Items)
			{
				if(!Operations.Write(File.get(), Item.Data()))
				{
					Result.m_WriteFailed = true;
					break;
				}
				++Result.m_Written;
			}
			const bool Synced = Operations.Sync(File.get()) == 0;
			const bool Clean = Operations.Error(File.get()) == 0;
			const bool Closed = Operations.Close(File.release()) == 0;
			if(Result.m_WriteFailed || !Synced || !Clean || !Closed || !Operations.Publish(*pStorage, Temporary.c_str(), pTarget))
				return Result;
			Candidate.m_Owned = false;
			Result.m_Success = true;
		}
		catch(...)
		{
		}
		return Result;
	}

	struct SLoadedData
	{
		std::deque<CBgDrawItemData> m_Queue;
		int m_Loaded = 0;
		int m_Discarded = 0;
	};

	// 完整读入并关闭后才发布解析结果；容量与扫描上限沿用原画笔政策。
	inline bool Read(IStorage *pStorage, const char *pTarget, int Capacity, SLoadedData &Output, CFileOperations *pOperations = nullptr) noexcept
	{
		try
		{
			if(!pStorage || Capacity < 0 || !ValidPath(pTarget) || !FitsStoragePath(*pStorage, pTarget))
				return false;
			SLoadedData Candidate;
			CFileOperations Default;
			auto &Operations = pOperations ? *pOperations : Default;
			std::unique_ptr<void, SFileCloser> File(pStorage->OpenFile(pTarget, IOFLAG_READ, IStorage::TYPE_SAVE), {Operations});
			if(!File)
				return false;
			CBgDrawItemData Data;
			while(Candidate.m_Loaded < MAX_ITEMS_TO_LOAD && Operations.Read(File.get(), Data))
			{
				++Candidate.m_Loaded;
				if(Capacity == 0)
				{
					++Candidate.m_Discarded;
					continue;
				}
				if(Candidate.m_Queue.size() >= static_cast<size_t>(Capacity))
				{
					++Candidate.m_Discarded;
					Candidate.m_Queue.pop_front();
				}
				Candidate.m_Queue.push_back(Data);
			}
			const bool Clean = Operations.Error(File.get()) == 0;
			const bool Closed = Operations.Close(File.release()) == 0;
			if(!Clean || !Closed)
				return false;
			static_assert(noexcept(Output.m_Queue.swap(Candidate.m_Queue)));
			Output.m_Queue.swap(Candidate.m_Queue);
			Output.m_Loaded = Candidate.m_Loaded;
			Output.m_Discarded = Candidate.m_Discarded;
			return true;
		}
		catch(...)
		{
			return false;
		}
	}
}

#endif
