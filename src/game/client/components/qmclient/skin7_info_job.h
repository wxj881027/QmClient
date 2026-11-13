#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_INFO_JOB_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_INFO_JOB_H

#include <base/log.h>
#include <base/str.h>

#include <engine/gfx/image_loader.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/shared/protocol7.h>
#include <engine/storage.h>

#include <generated/protocol7.h>

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using TQmSkin7Info = std::unique_ptr<json_value, decltype(&json_value_free)>;

inline TQmSkin7Info QmParseSkin7Info(const void *pData, size_t Size)
{
	if(!pData || Size == 0)
		return {nullptr, json_value_free};
	json_settings Settings{};
	char aError[256];
	TQmSkin7Info pInfo(JsonParseEx(&Settings, static_cast<const json_char *>(pData), Size, aError), json_value_free);
	if(!pInfo || pInfo->type != json_object || (*pInfo)["skin"].type != json_object)
		pInfo.reset();
	return pInfo;
}

// 后台扫描资源、读取/解析描述并解码附加图片；主线程只上传纹理和绑定部件指针。
class CQmSkin7InfoJob : public IJob
{
public:
	using FDecodeAccessory = bool (*)(const void *, size_t, const char *, CImageInfo &);

	struct SPart
	{
		int m_Part;
		int m_StorageType;
		std::string m_Name;
	};
	struct SEntry
	{
		std::string m_Name;
		int m_StorageType;
		TQmSkin7Info m_pInfo;
	};

	explicit CQmSkin7InfoJob(IStorage *pStorage, FDecodeAccessory pfnDecodeAccessory = DecodeAccessory) :
		m_pStorage(pStorage), m_pfnDecodeAccessory(pfnDecodeAccessory) { Abortable(true); }
	~CQmSkin7InfoJob() override
	{
		m_XmasHat.Free();
		m_Bot.Free();
	}
	std::vector<SEntry> TakeEntries() { return std::move(m_vEntries); }
	const std::vector<SEntry> &Entries() const { return m_vEntries; }
	void ReleaseEntry(size_t Index) { m_vEntries[Index].m_pInfo.reset(); }
	const std::vector<SPart> &Parts() const { return m_vParts; }
	std::vector<SPart> TakeParts() { return std::move(m_vParts); }
	CImageInfo TakeXmasHat() { return std::exchange(m_XmasHat, CImageInfo{}); }
	CImageInfo TakeBot() { return std::exchange(m_Bot, CImageInfo{}); }
	// 只表示扫描流程正常结束，文件缺失/格式无效仍跳过，实际读盘失败必须重扫。
	// DONE的发布同步后台结果；ABORTED不代表worker已退出，不能据此读取结果。
	bool Succeeded() const { return State() == STATE_DONE && m_Succeeded; }

	// 只在主线程消费已结束任务；回调不得重入，失败保留图像和附件游标。
	template<typename TPublish>
	bool PublishNextAccessory(int &NextAccessory, TPublish &&Publish)
	{
		if(!Succeeded() || NextAccessory < 0 || NextAccessory >= 2)
			return false;
		auto &Image = NextAccessory == 0 ? m_XmasHat : m_Bot;
		try
		{
			if(!std::forward<TPublish>(Publish)(static_cast<const CImageInfo &>(Image), NextAccessory == 0))
				return false;
		}
		catch(...)
		{
			return false;
		}
		Image.Free();
		++NextAccessory;
		return true;
	}

protected:
	void Run() override
	{
		try
		{
			if(State() != STATE_ABORTED)
				ScanDirectory("skins7", Scan);
			// 目录扫描和附加 PNG 也会读盘，不能留在加载页的主线程。
			const char *apParts[] = {"body", "marking", "decoration", "hands", "feet", "eyes"};
			for(int Part = 0; Part < protocol7::NUM_SKINPARTS && State() != STATE_ABORTED; ++Part)
			{
				m_ScanPart = Part;
				const std::string Path = "skins7/" + std::string(apParts[Part]);
				ScanDirectory(Path.c_str(), ScanPart);
			}
			LoadAccessory("skins7/xmas_hat.png", 1, 4, m_XmasHat);
			LoadAccessory("skins7/bot.png", 12, 5, m_Bot);
			if(State() != STATE_ABORTED)
			{
				m_Succeeded = true;
				return;
			}
		}
		catch(...)
		{
			// 残缺扫描不发布；失败诊断也不能再次逃逸到作业池的异常路径。
			DiscardResults();
			try
			{
				log_error("skins7", "Failed to prepare skin scan; retry required");
			}
			catch(...)
			{
			}
			return;
		}
		DiscardResults();
	}

private:
	struct SFileData
	{
		void *m_pData = nullptr;
		~SFileData() { Free(); }
		void Free()
		{
			free(m_pData);
			m_pData = nullptr;
		}
	};

	struct SImageOwner
	{
		CImageInfo m_Image;
		~SImageOwner() { m_Image.Free(); }
	};

	static bool DecodeAccessory(const void *pData, size_t Size, const char *pPath, CImageInfo &Image)
	{
		return CImageLoader::LoadPng(pData, Size, pPath, Image);
	}

	IStorage *m_pStorage;
	FDecodeAccessory m_pfnDecodeAccessory;
	std::vector<SEntry> m_vEntries;
	std::vector<SPart> m_vParts;
	int m_ScanPart = 0;
	CImageInfo m_XmasHat;
	CImageInfo m_Bot;
	bool m_Succeeded = false;

	void DiscardResults()
	{
		m_vEntries.clear();
		m_vParts.clear();
		m_XmasHat.Free();
		m_Bot.Free();
		m_Succeeded = false;
	}

	void ScanDirectory(const char *pPath, FS_LISTDIR_CALLBACK Callback)
	{
		const auto Result = m_pStorage->ListDirectoryChecked(IStorage::TYPE_ALL, pPath, Callback, this);
		if(State() != STATE_ABORTED && Result != EFsListDirectoryResult::SUCCESS && Result != EFsListDirectoryResult::NOT_FOUND)
			throw std::runtime_error("skin directory enumeration failed");
	}

	bool ReadFile(const char *pPath, int StorageType, SFileData &Data, unsigned &Size)
	{
		const auto Result = m_pStorage->ReadFileChecked(pPath, StorageType, &Data.m_pData, &Size);
		if(Result == IStorage::EReadFileResult::SUCCESS)
			return true;
		if(Result == IStorage::EReadFileResult::NOT_FOUND || State() == STATE_ABORTED)
			return false;
		throw std::runtime_error("skin file read failed");
	}

	void LoadAccessory(const char *pPath, int Columns, int Rows, CImageInfo &Image)
	{
		if(State() == STATE_ABORTED)
			return;
		SFileData Data;
		unsigned Size = 0;
		if(!ReadFile(pPath, IStorage::TYPE_ALL, Data, Size))
			return;
		SImageOwner Candidate;
		auto &Prepared = Candidate.m_Image;
		const bool Loaded = State() != STATE_ABORTED && m_pfnDecodeAccessory(Data.m_pData, Size, pPath, Prepared);
		Data.Free();
		if(Loaded && Prepared.m_Format == CImageInfo::FORMAT_RGBA && Prepared.m_Width != 0 && Prepared.m_Height != 0 && Prepared.m_Width % Columns == 0 && Prepared.m_Height % Rows == 0 && State() != STATE_ABORTED)
			Image = std::move(Prepared);
	}

	static int ScanPart(const char *pName, int IsDir, int StorageType, void *pUser)
	{
		auto *pSelf = static_cast<CQmSkin7InfoJob *>(pUser);
		if(pSelf->State() == STATE_ABORTED)
			return 1;
		if(!IsDir && str_endswith(pName, ".png"))
			pSelf->m_vParts.push_back({pSelf->m_ScanPart, StorageType, pName});
		return 0;
	}

	static int Scan(const char *pName, int IsDir, int StorageType, void *pUser)
	{
		auto *pSelf = static_cast<CQmSkin7InfoJob *>(pUser);
		if(pSelf->State() == STATE_ABORTED)
			return 1;
		const char *pSuffix = str_endswith(pName, ".json");
		if(IsDir || !pSuffix)
			return 0;
		std::string Name(pName, pSuffix);
		if(Name.size() >= protocol7::MAX_SKIN_ARRAY_SIZE || !str_valid_filename(Name.c_str()))
		{
			log_error("skins7", "Skin name is not valid: %s", Name.c_str());
			return 0;
		}
		std::string Path = "skins7/" + std::string(pName);
		SFileData Data;
		unsigned Size = 0;
		if(!pSelf->ReadFile(Path.c_str(), StorageType, Data, Size))
			return 0;
		auto pInfo = QmParseSkin7Info(Data.m_pData, Size);
		Data.Free();
		if(pInfo && pSelf->State() != STATE_ABORTED)
			pSelf->m_vEntries.push_back({std::move(Name), StorageType, std::move(pInfo)});
		else if(pSelf->State() != STATE_ABORTED)
			log_error("skins7", "Failed to parse skin json file '%s'", Path.c_str());
		return 0;
	}
};

#endif
