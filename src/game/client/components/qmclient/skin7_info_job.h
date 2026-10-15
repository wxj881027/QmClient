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

	explicit CQmSkin7InfoJob(IStorage *pStorage) : m_pStorage(pStorage) { Abortable(true); }
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

protected:
	void Run() override
	{
		if(State() != STATE_ABORTED)
			m_pStorage->ListDirectory(IStorage::TYPE_ALL, "skins7", Scan, this);
		// 目录扫描和附加 PNG 也会读盘，不能留在加载页的主线程。
		const char *apParts[] = {"body", "marking", "decoration", "hands", "feet", "eyes"};
		for(int Part = 0; Part < protocol7::NUM_SKINPARTS && State() != STATE_ABORTED; ++Part)
		{
			m_ScanPart = Part;
			const std::string Path = "skins7/" + std::string(apParts[Part]);
			m_pStorage->ListDirectory(IStorage::TYPE_ALL, Path.c_str(), ScanPart, this);
		}
		LoadAccessory("skins7/xmas_hat.png", 1, 4, m_XmasHat);
		LoadAccessory("skins7/bot.png", 12, 5, m_Bot);
	}

private:
	IStorage *m_pStorage;
	std::vector<SEntry> m_vEntries;
	std::vector<SPart> m_vParts;
	int m_ScanPart = 0;
	CImageInfo m_XmasHat;
	CImageInfo m_Bot;

	void LoadAccessory(const char *pPath, int Columns, int Rows, CImageInfo &Image)
	{
		if(State() == STATE_ABORTED)
			return;
		void *pData = nullptr;
		unsigned Size = 0;
		if(!m_pStorage->ReadFile(pPath, IStorage::TYPE_ALL, &pData, &Size))
			return;
		const bool Loaded = State() != STATE_ABORTED && CImageLoader::LoadPng(static_cast<uint8_t *>(pData), Size, pPath, Image);
		free(pData);
		if(!Loaded || Image.m_Format != CImageInfo::FORMAT_RGBA || Image.m_Width == 0 || Image.m_Height == 0 || Image.m_Width % Columns != 0 || Image.m_Height % Rows != 0 || State() == STATE_ABORTED)
			Image.Free();
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
		void *pData = nullptr;
		unsigned Size = 0;
		if(!pSelf->m_pStorage->ReadFile(Path.c_str(), StorageType, &pData, &Size))
			return 0;
		auto pInfo = QmParseSkin7Info(pData, Size);
		free(pData);
		if(pInfo && pSelf->State() != STATE_ABORTED)
			pSelf->m_vEntries.push_back({std::move(Name), StorageType, std::move(pInfo)});
		else if(pSelf->State() != STATE_ABORTED)
			log_error("skins7", "Failed to parse skin json file '%s'", Path.c_str());
		return 0;
	}
};

#endif
