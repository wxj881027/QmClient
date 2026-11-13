/* Copyright © 2026 BestProject Team */
// Rank 1 回放的拉取阶段：清单请求、demo 下载与下载后校验。
// 与集合查询、缓存加载、查看模式分开，网络与文件校验只在这里维护。

#include "rank_ghost_fetch.h"

#include "rank_demo_cache.h"
#include "rank_demo_manifest.h"
#include "rank_ghost.h"

#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/QmUi/QmToast.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <zlib.h>

#include <algorithm>
#include <vector>

using qmclient::rank_demo::FindBrowserDownloadedDemo;
using rank_ghost_fetch::ValidateOrUnpackDemo;

namespace
{
	constexpr size_t MAX_MANIFEST_BYTES = 16 * 1024 * 1024;
	constexpr size_t MAX_DEMO_BYTES = 128 * 1024 * 1024;
	constexpr size_t MAX_UNPACKED_DEMO_BYTES = 256 * 1024 * 1024;
	constexpr const char *USER_AGENT = "QmClient (https://github.com/wxj881027/QmClient)";

	bool IsGzipData(const unsigned char *pData, size_t Size)
	{
		return Size >= 2 && pData[0] == 0x1f && pData[1] == 0x8b;
	}

	bool HasDemoMagic(const unsigned char *pData, size_t Size)
	{
		return Size >= 7 && mem_comp(pData, "TWDEMO\0", 7) == 0;
	}
	// 同存储文件流式复制（固定 4 MiB 缓冲，demo 可达数十 MB，不整块进内存）
	bool CopyStorageFile(IStorage *pStorage, const char *pFrom, const char *pTo)
	{
		if(str_comp(pFrom, pTo) == 0)
			return true;
		char aFromPath[IO_MAX_PATH_LENGTH];
		char aToPath[IO_MAX_PATH_LENGTH];
		pStorage->GetCompletePath(IStorage::TYPE_SAVE_OR_ABSOLUTE, pFrom, aFromPath, sizeof(aFromPath));
		pStorage->GetCompletePath(IStorage::TYPE_SAVE, pTo, aToPath, sizeof(aToPath));
		if(aFromPath[0] == '\0' || aToPath[0] == '\0')
			return false;
		IOHANDLE pInput = io_open(aFromPath, IOFLAG_READ);
		if(pInput == nullptr)
			return false;
		IOHANDLE pOutput = io_open(aToPath, IOFLAG_WRITE);
		if(pOutput == nullptr)
		{
			io_close(pInput);
			return false;
		}
		std::vector<unsigned char> Buffer(4 * 1024 * 1024);
		bool Ok = true;
		unsigned Read;
		while((Read = io_read(pInput, Buffer.data(), (unsigned)Buffer.size())) > 0)
		{
			if(io_write(pOutput, Buffer.data(), Read) != Read)
			{
				Ok = false;
				break;
			}
		}
		io_close(pInput);
		io_close(pOutput);
		if(!Ok)
			pStorage->RemoveFile(pTo, IStorage::TYPE_SAVE);
		return Ok;
	}
} // namespace

// 读取文件头，判断是否为有效的 demo（服务端通常已解压，个别缓存路径可能返回原始 gzip 字节）
bool rank_ghost_fetch::ValidateOrUnpackDemo(IStorage *pStorage, const char *pStoragePath)
{
	void *pData = nullptr;
	unsigned DataSize = 0;
	if(!pStorage->ReadFile(pStoragePath, IStorage::TYPE_SAVE, &pData, &DataSize) || pData == nullptr || DataSize == 0)
	{
		free(pData);
		return false;
	}

	if(!IsGzipData((const unsigned char *)pData, DataSize))
	{
		const bool Valid = HasDemoMagic((const unsigned char *)pData, DataSize);
		free(pData);
		return Valid;
	}

	// gzip：解压到内存后写回
	z_stream Stream = {};
	Stream.next_in = (Bytef *)pData;
	Stream.avail_in = DataSize;
	if(inflateInit2(&Stream, 15 + 32) != Z_OK)
	{
		free(pData);
		return false;
	}

	std::vector<unsigned char> Output;
	Output.resize(std::min<size_t>(std::max<size_t>(DataSize * 4, 1024 * 1024), MAX_UNPACKED_DEMO_BYTES));
	size_t OutputLength = 0;
	int Result = Z_OK;
	while(Result == Z_OK)
	{
		if(OutputLength == Output.size())
		{
			if(Output.size() >= MAX_UNPACKED_DEMO_BYTES)
			{
				inflateEnd(&Stream);
				free(pData);
				return false;
			}
			Output.resize(std::min(Output.size() * 2, MAX_UNPACKED_DEMO_BYTES));
		}
		Stream.next_out = Output.data() + OutputLength;
		Stream.avail_out = (uInt)std::min<size_t>(Output.size() - OutputLength, 0x40000000);
		Result = inflate(&Stream, Z_NO_FLUSH);
		OutputLength = Output.size() - Stream.avail_out;
	}
	inflateEnd(&Stream);
	free(pData);

	if(Result != Z_STREAM_END || OutputLength > MAX_UNPACKED_DEMO_BYTES || !HasDemoMagic(Output.data(), OutputLength))
		return false;

	IOHANDLE File = pStorage->OpenFile(pStoragePath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;
	const bool WriteOk = io_write(File, Output.data(), OutputLength) == OutputLength;
	io_close(File);
	return WriteOk;
}
void CRankGhost::StartManifestFetch()
{
	if(Http() == nullptr)
	{
		Fail(Localize("HTTP is not available"), Localize("Rank ghost: HTTP is not available"));
		return;
	}

	m_pManifestRequest = HttpGet(MANIFEST_URL);
	m_pManifestRequest->MaxResponseSize(MAX_MANIFEST_BYTES);
	m_pManifestRequest->Timeout(CTimeout{5000, 30000, 100, 5});
	m_pManifestRequest->LogProgress(HTTPLOG::FAILURE);
	m_pManifestRequest->FailOnErrorStatus(false);
	m_pManifestRequest->HeaderString("User-Agent", USER_AGENT);
	Http()->Run(m_pManifestRequest);

	m_Stage = EStage::FETCH_MANIFEST;
	log_info("rank_ghost", "fetching manifest from '%s'", MANIFEST_URL);
	Echo(Localize("Rank ghost: fetching the replay list ..."));
}

void CRankGhost::UpdateManifestStage()
{
	if(!m_pManifestRequest || !m_pManifestRequest->Done())
		return;

	const bool Ok = m_pManifestRequest->State() == EHttpState::DONE && m_pManifestRequest->StatusCode() == 200;
	unsigned char *pResult = nullptr;
	size_t ResultLength = 0;
	if(Ok)
		m_pManifestRequest->Result(&pResult, &ResultLength);

	if(!Ok || pResult == nullptr || ResultLength == 0)
	{
		m_pManifestRequest = nullptr;
		m_ManifestFailed = true;
		m_ManifestFailedAt = time_get();
		Fail(Localize("Failed to fetch the replay list"), Localize("Rank ghost: failed to fetch the replay list"));
		return;
	}

	// 响应缓冲区由请求对象持有，必须等解析完成后再释放请求
	const bool Parsed = ParseManifest(pResult, ResultLength);
	m_pManifestRequest = nullptr;

	if(!Parsed)
	{
		m_ManifestFailed = true;
		m_ManifestFailedAt = time_get();
		Fail(Localize("The replay list is empty or malformed"), Localize("Rank ghost: the replay list is empty or malformed"));
		return;
	}

	m_ManifestLoaded = true;
	m_ManifestFailed = false;
	m_ManifestLoadedAt = time_get();
	m_Stage = EStage::IDLE;
	log_info("rank_ghost", "manifest loaded: %d entries", (int)m_vEntries.size());
	LookupInManifest();
}
void CRankGhost::StartDemoFetch()
{
	if(Http() == nullptr)
	{
		Fail(Localize("HTTP is not available"), Localize("Rank ghost: HTTP is not available"));
		return;
	}

	// QmClient：demo 浏览器目录已有同一场次回放时复用，避免重复下载。
	// 无论下载还是影子转换都复制到自己的缓存路径，页面状态才有可观察的文件。
	char aBrowserDemo[IO_MAX_PATH_LENGTH];
	if(FindBrowserDownloadedDemo(Storage(), g_Config, m_ActiveEntry, aBrowserDemo, sizeof(aBrowserDemo)))
	{
		if(CopyStorageFile(Storage(), aBrowserDemo, m_aDemoStoragePath))
		{
			log_info("rank_ghost", "reusing demo downloaded by demo browser: '%s'", aBrowserDemo);
			FinishDemoFetch();
			return;
		}
		log_info("rank_ghost", "failed to reuse demo '%s', falling back to download", aBrowserDemo);
	}

	char aUrl[1024];
	str_format(aUrl, sizeof(aUrl), "%s/%s", DEMO_URL_PREFIX, m_ActiveEntry.m_Demo.c_str());

	m_pDemoRequest = HttpGetFile(aUrl, Storage(), m_aDemoStoragePath, IStorage::TYPE_SAVE);
	m_pDemoRequest->MaxResponseSize(MAX_DEMO_BYTES);
	m_pDemoRequest->Timeout(CTimeout{5000, 120000, 200, 10});
	m_pDemoRequest->LogProgress(HTTPLOG::FAILURE);
	m_pDemoRequest->FailOnErrorStatus(false);
	m_pDemoRequest->HeaderString("User-Agent", USER_AGENT);
	Http()->Run(m_pDemoRequest);

	m_Stage = EStage::FETCH_DEMO;
	log_info("rank_ghost", "downloading demo from '%s'", aUrl);
	if(m_PendingMode == EPendingMode::DEMO_ONLY)
		qm_ui::Toast(EQmToastKind::INFO, Localize("Downloading rank 1 demo"));
	else
		Echo(Localize("Rank ghost: downloading the replay ..."));
}

void CRankGhost::UpdateDemoStage()
{
	if(!m_pDemoRequest || !m_pDemoRequest->Done())
		return;

	const bool Ok = m_pDemoRequest->State() == EHttpState::DONE && m_pDemoRequest->StatusCode() == 200;
	m_pDemoRequest = nullptr;

	if(!Ok)
	{
		Fail(Localize("Failed to download the rank 1 demo"), Localize("Rank ghost: failed to download the replay"));
		return;
	}

	FinishDemoFetch();
}

// demo 下载（或本地复用）后的公共收尾：校验并按请求模式处理
void CRankGhost::FinishDemoFetch()
{
	if(!ValidateOrUnpackDemo(Storage(), m_aDemoStoragePath))
	{
		Storage()->RemoveFile(m_aDemoStoragePath, IStorage::TYPE_SAVE);
		Fail(Localize("The downloaded file is not a valid demo"), Localize("Rank ghost: the downloaded replay is not a valid demo"));
		return;
	}

	// 仅下载请求到此结束（Rank 1 页面的“下载回放”），不解析成影子
	if(m_PendingMode == EPendingMode::DEMO_ONLY)
	{
		m_Stage = EStage::IDLE;
		ClearPendingRequest();
		char aBuf[IO_MAX_PATH_LENGTH + 64];
		str_format(aBuf, sizeof(aBuf), Localize("Rank 1 demo downloaded to '%s'"), m_aDemoStoragePath);
		Echo(aBuf);
		qm_ui::Toast(EQmToastKind::SUCCESS, aBuf);
		log_info("rank_ghost", "demo cached: '%s'", m_aDemoStoragePath);
		return;
	}

	StartParse();
}
