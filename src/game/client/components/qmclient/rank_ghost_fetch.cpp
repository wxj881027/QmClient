/* Copyright © 2026 BestProject Team */
// Rank 1 回放的拉取阶段：清单请求、demo 下载与下载后校验。
// 与集合查询、缓存加载、查看模式分开，网络与文件校验只在这里维护。

#include "rank_ghost_fetch.h"

#include "bounded_file_reader.h"
#include "rank_demo_cache.h"
#include "rank_demo_manifest.h"
#include "rank_ghost.h"

#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/http_response_validation.h>
#include <engine/shared/config.h>
#include <engine/shared/demo_output.h>
#include <engine/storage.h>

#include <game/client/QmUi/QmToast.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
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

	bool ValidateDemoTimeline(IStorage *pStorage, const char *pPath, int StorageType)
	{
		auto pMetadata = std::make_unique<CDemoPlayer>(nullptr, nullptr, false);
		if(pMetadata->Load(pStorage, pPath, StorageType, false) != 0)
			return false;
		const auto &Info = *pMetadata->BaseInfo();
		const bool Valid = Info.m_FirstTick >= 0 && Info.m_LastTick >= Info.m_FirstTick;
		pMetadata->Stop();
		return Valid;
	}

	template<typename TWrite, typename TValidate>
	bool WriteRankCache(IStorage &Storage, const char *pPath, const TWrite &Write, const TValidate &Validate)
	try
	{
		demo_output::CTransaction Transaction;
		if(!Transaction.Prepare(Storage, pPath))
			return false;
		std::unique_ptr<void, decltype(&io_close)> pFile(io_open(Transaction.Filename(), IOFLAG_WRITE), io_close);
		if(!pFile)
			return false;
		const bool Written = Write(pFile.get()) && io_flush(pFile.get()) == 0 && io_error(pFile.get()) == 0;
		const bool Closed = io_close(pFile.release()) == 0;
		return Written && Closed && Validate(Transaction.Filename()) && Transaction.Publish();
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}
	catch(const std::filesystem::filesystem_error &)
	{
		return false;
	}

	// 流式复制浏览器里的回放，保留源文件；失败时上层继续走下载。
	bool CopyStorageFile(IStorage *pStorage, const char *pFrom, const char *pTo)
	try
	{
		if(str_comp(pFrom, pTo) == 0)
			return false;
		std::string From;
		if(!demo_output::Resolve(*pStorage, pFrom, From))
			return false;
		std::unique_ptr<void, decltype(&io_close)> pInput(io_open_read_regular_checked(From.c_str(), nullptr), io_close);
		if(!pInput)
			return false;
		const int64_t Length = io_length(pInput.get());
		if(Length <= 0 || Length > static_cast<int64_t>(MAX_UNPACKED_DEMO_BYTES))
			return false;

		constexpr unsigned BUFFER_SIZE = 4 * 1024 * 1024;
		const std::unique_ptr<unsigned char[]> pBuffer(new(std::nothrow) unsigned char[BUFFER_SIZE]);
		if(!pBuffer)
			return false;
		return WriteRankCache(*pStorage, pTo, [&](IOHANDLE Output) {
			for(int64_t Remaining = Length; Remaining > 0;)
			{
				const auto Bytes = static_cast<unsigned>(std::min<int64_t>(Remaining, BUFFER_SIZE));
				if(io_read(pInput.get(), pBuffer.get(), Bytes) != Bytes || io_write(Output, pBuffer.get(), Bytes) != Bytes)
					return false;
				Remaining -= Bytes;
			}
			unsigned char Extra;
			if(io_read(pInput.get(), &Extra, 1) != 0 || io_error(pInput.get()) != 0)
				return false;
			return io_close(pInput.release()) == 0; }, [](const char *) { return true; });
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}
	catch(const std::filesystem::filesystem_error &)
	{
		return false;
	}

} // namespace

bool rank_ghost_fetch::ValidateOrUnpackDemo(IStorage *pStorage, const char *pStoragePath)
try
{
	CQmCheckedFileData Input;
	if(!Input.Read(*pStorage, pStoragePath, IStorage::TYPE_SAVE, MAX_UNPACKED_DEMO_BYTES))
		return false;
	const auto *pData = static_cast<const unsigned char *>(Input.Data());
	if(!IsGzipData(pData, Input.Size()))
	{
		if(!HasDemoMagic(pData, Input.Size()))
			return false;
		Input = {};
		return ValidateDemoTimeline(pStorage, pStoragePath, IStorage::TYPE_SAVE);
	}

	z_stream Stream = {};
	Stream.next_in = const_cast<Bytef *>(pData);
	Stream.avail_in = Input.Size();
	if(inflateInit2(&Stream, 15 + 32) != Z_OK)
		return false;
	std::unique_ptr<z_stream, decltype(&inflateEnd)> pInflate(&Stream, inflateEnd);

	std::vector<unsigned char> vOutput(std::min(std::max<size_t>(static_cast<size_t>(Input.Size()) * 4, 1024 * 1024), MAX_UNPACKED_DEMO_BYTES));
	size_t OutputLength = 0;
	int Result = Z_OK;
	while(Result == Z_OK)
	{
		if(OutputLength == vOutput.size())
		{
			if(vOutput.size() >= MAX_UNPACKED_DEMO_BYTES)
				return false;
			vOutput.resize(std::min(vOutput.size() * 2, MAX_UNPACKED_DEMO_BYTES));
		}
		Stream.next_out = vOutput.data() + OutputLength;
		Stream.avail_out = static_cast<uInt>(vOutput.size() - OutputLength);
		Result = inflate(&Stream, Z_NO_FLUSH);
		OutputLength = vOutput.size() - Stream.avail_out;
	}
	if(Result != Z_STREAM_END || Stream.avail_in != 0 || !HasDemoMagic(vOutput.data(), OutputLength))
		return false;
	pInflate.reset();
	Input = {};

	return WriteRankCache(*pStorage, pStoragePath, [&](IOHANDLE File) { return io_write(File, vOutput.data(), static_cast<unsigned>(OutputLength)) == OutputLength; }, [&](const char *pFilename) { return ValidateDemoTimeline(pStorage, pFilename, IStorage::TYPE_ABSOLUTE); });
}
catch(const std::bad_alloc &)
{
	return false;
}
catch(const std::length_error &)
{
	return false;
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
	static std::atomic<uint64_t> s_NextCandidate{0};
	char aCandidate[IO_MAX_PATH_LENGTH];
	const int CandidateLength = std::snprintf(aCandidate, sizeof(aCandidate), "%s/.r%x-%llx",
		DEMO_CACHE_DIR, static_cast<unsigned>(pid()), static_cast<unsigned long long>(s_NextCandidate.fetch_add(1, std::memory_order_relaxed)));
	if(CandidateLength < 0 || static_cast<size_t>(CandidateLength) >= sizeof(aCandidate))
	{
		Fail(Localize("Failed to download the rank 1 demo"), Localize("Rank ghost: failed to download the replay"));
		return;
	}

	// QmClient：demo 浏览器目录已有同一场次回放时复用，避免重复下载。
	// 无论下载还是影子转换都复制到自己的缓存路径，页面状态才有可观察的文件。
	char aBrowserDemo[IO_MAX_PATH_LENGTH];
	if(FindBrowserDownloadedDemo(Storage(), g_Config, m_ActiveEntry, aBrowserDemo, sizeof(aBrowserDemo)))
	{
		if(CopyStorageFile(Storage(), aBrowserDemo, aCandidate))
		{
			log_info("rank_ghost", "reusing demo downloaded by demo browser: '%s'", aBrowserDemo);
			FinishDemoFetch(aCandidate);
			return;
		}
		log_info("rank_ghost", "failed to reuse demo '%s', falling back to download", aBrowserDemo);
	}

	char aUrl[1024];
	str_format(aUrl, sizeof(aUrl), "%s/%s", DEMO_URL_PREFIX, m_ActiveEntry.m_Demo.c_str());

	std::string AbsoluteCandidate;
	if(!demo_output::Resolve(*Storage(), aCandidate, AbsoluteCandidate) ||
		AbsoluteCandidate.size() + 16 >= IO_MAX_PATH_LENGTH)
	{
		Fail(Localize("Failed to download the rank 1 demo"), Localize("Rank ghost: failed to download the replay"));
		return;
	}
	m_pDemoRequest = HttpGetFile(aUrl, Storage(), aCandidate, IStorage::TYPE_SAVE);
	m_pDemoRequest->SkipByFileTime(false);
	m_pDemoRequest->ValidateBeforeOverwrite(true);
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

	const auto pRequest = std::move(m_pDemoRequest);
	if(pRequest->State() != EHttpState::DONE)
	{
		Fail(Localize("Failed to download the rank 1 demo"), Localize("Rank ghost: failed to download the replay"));
		return;
	}
	CHttpResponseValidation Validation(*pRequest);
	if(pRequest->StatusCode() != 200 || !Validation.Commit())
	{
		Fail(Localize("Failed to download the rank 1 demo"), Localize("Rank ghost: failed to download the replay"));
		return;
	}

	FinishDemoFetch(pRequest->Dest());
}

// demo 下载（或本地复用）后的公共收尾：校验并按请求模式处理
void CRankGhost::FinishDemoFetch(const char *pCandidate)
{
	const auto RemoveCandidate = [this](const char *pPath) noexcept {
		try
		{
			Storage()->RemoveFile(pPath, IStorage::TYPE_SAVE);
		}
		catch(...)
		{
		}
	};
	std::unique_ptr<const char, decltype(RemoveCandidate)> pCleanup(pCandidate, RemoveCandidate);
	if(!pCandidate || !ValidateOrUnpackDemo(Storage(), pCandidate))
	{
		Fail(Localize("The downloaded file is not a valid demo"), Localize("Rank ghost: the downloaded replay is not a valid demo"));
		return;
	}
	if(!Storage()->RenameFile(pCandidate, m_aDemoStoragePath, IStorage::TYPE_SAVE, false))
	{
		Fail(Localize("Failed to download the rank 1 demo"), Localize("Rank ghost: failed to download the replay"));
		return;
	}
	pCleanup.release();

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
