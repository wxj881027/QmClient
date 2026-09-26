#include "qm_map_upload.h"

#include <engine/shared/json.h>

#include <algorithm>

namespace QmMapUpload
{
	class CUpload::CPrepareJob : public IJob
	{
		std::string m_Path;
		std::string m_Filename;
		std::string m_PlayerName;
		std::string m_Endpoint;
		std::atomic<bool> m_Cancelled{false};
		EStatus m_Status = EStatus::UPLOADING;
		std::shared_ptr<IHttpRequest> m_pRequest;

		bool StopIfCancelled()
		{
			if(!m_Cancelled.load(std::memory_order_relaxed))
				return false;
			m_Status = EStatus::CANCELLED;
			return true;
		}

		void Run() override
		{
			if(StopIfCancelled())
				return;
			IOHANDLE File = io_open(m_Path.c_str(), IOFLAG_READ);
			if(!File)
			{
				m_Status = EStatus::READ_FAILED;
				return;
			}
			const int64_t FileSize = io_length(File);
			if(FileSize <= 0 || FileSize > static_cast<int64_t>(MAX_MAP_SIZE))
			{
				io_close(File);
				m_Status = FileSize > static_cast<int64_t>(MAX_MAP_SIZE) ? EStatus::TOO_LARGE : EStatus::READ_FAILED;
				return;
			}
			std::vector<unsigned char> vData(static_cast<size_t>(FileSize));
			size_t Read = 0;
			while(Read < vData.size())
			{
				if(StopIfCancelled())
				{
					io_close(File);
					return;
				}
				const unsigned Chunk = static_cast<unsigned>(std::min<size_t>(vData.size() - Read, 1024 * 1024));
				const unsigned Received = io_read(File, vData.data() + Read, Chunk);
				Read += Received;
				if(Received != Chunk)
					break;
			}
			unsigned char Extra = 0;
			const bool Complete = Read == vData.size() && io_read(File, &Extra, 1) == 0;
			io_close(File);
			if(!Complete || StopIfCancelled())
			{
				if(m_Status != EStatus::CANCELLED)
					m_Status = EStatus::READ_FAILED;
				return;
			}

			unsigned char Random[16];
			secure_random_fill(Random, sizeof(Random));
			std::string Boundary = "QmClientMapUpload";
			for(const unsigned char Byte : Random)
			{
				Boundary += "0123456789abcdef"[Byte >> 4];
				Boundary += "0123456789abcdef"[Byte & 15];
			}
			std::string Body;
			if(!BuildMultipart(m_Filename.c_str(), m_PlayerName.c_str(), vData.data(), vData.size(), Boundary.c_str(), Body))
			{
				m_Status = EStatus::INVALID_FILE;
				return;
			}
			if(StopIfCancelled())
				return;

			std::unique_ptr<IHttpRequest> pRequest = CreateHttpRequest(m_Endpoint.c_str());
			m_pRequest = std::shared_ptr<IHttpRequest>(pRequest.release());
			m_pRequest->Post(reinterpret_cast<const unsigned char *>(Body.data()), Body.size());
			const std::string ContentType = "multipart/form-data; boundary=" + Boundary;
			m_pRequest->HeaderString("Content-Type", ContentType.c_str());
			m_pRequest->HeaderString("Accept", "application/json");
			m_pRequest->MaxResponseSize(MAX_RESPONSE_SIZE);
			m_pRequest->FailOnErrorStatus(false);
			m_pRequest->Timeout(CTimeout{10000, 300000, 1024, 30});
			m_pRequest->LogProgress(HTTPLOG::NONE);
			if(StopIfCancelled())
				m_pRequest.reset();
		}

	public:
		CPrepareJob(const char *pPath, const char *pFilename, const char *pPlayerName, const char *pEndpoint) :
			m_Path(pPath ? pPath : ""), m_Filename(pFilename ? pFilename : ""), m_PlayerName(pPlayerName ? pPlayerName : ""), m_Endpoint(pEndpoint ? pEndpoint : "") {}
		void Cancel() { m_Cancelled.store(true, std::memory_order_relaxed); }
		EStatus Status() const { return m_Status; }
		std::shared_ptr<IHttpRequest> Request() const { return m_pRequest; }
	};

	CUpload::~CUpload()
	{
		Cancel();
	}

	void CUpload::Reset()
	{
		if(Busy())
			return;
		m_Status = EStatus::IDLE;
		m_Detail.clear();
		m_StatusCode = 0;
	}

	void CUpload::Start(IStorage *pStorage, IHttp *pHttp, IEngine *pEngine, const char *pEndpoint, const char *pPath, int StorageType, const char *pPlayerName)
	{
		if(Busy())
			return;
		Reset();
		if(!pEndpoint || (str_startswith(pEndpoint, "https://") == nullptr && str_startswith(pEndpoint, "http://") == nullptr))
		{
			m_Status = EStatus::INVALID_ENDPOINT;
			return;
		}
		if(!pStorage || !pHttp || !pEngine || !pPath || !ValidateFilename(fs_filename(pPath)))
		{
			m_Status = EStatus::INVALID_FILE;
			return;
		}
		if(!pPlayerName || !str_utf8_check(pPlayerName) || *str_utf8_skip_whitespaces(pPlayerName) == '\0')
		{
			m_Status = EStatus::MISSING_PLAYER;
			return;
		}
		if(StorageType < IStorage::TYPE_SAVE || StorageType >= pStorage->NumPaths())
		{
			m_Status = EStatus::READ_FAILED;
			return;
		}
		char aAbsolutePath[IO_MAX_PATH_LENGTH];
		pStorage->GetCompletePath(StorageType, pPath, aAbsolutePath, sizeof(aAbsolutePath));
		m_pHttp = pHttp;
		m_pPrepareJob = std::make_shared<CPrepareJob>(aAbsolutePath, fs_filename(pPath), pPlayerName, pEndpoint);
		m_Status = EStatus::UPLOADING;
		pEngine->AddJob(m_pPrepareJob);
	}

	void CUpload::Poll()
	{
		if(m_pPrepareJob)
		{
			if(!m_pPrepareJob->Done())
				return;
			if(m_Status != EStatus::CANCELLED && m_pPrepareJob->State() == IJob::STATE_DONE)
			{
				m_Status = m_pPrepareJob->Status();
				m_pRequest = m_pPrepareJob->Request();
				if(m_pRequest)
					m_pHttp->Run(m_pRequest);
			}
			else
				m_Status = EStatus::CANCELLED;
			m_pPrepareJob.reset();
		}
		if(!m_pRequest || !m_pRequest->Done())
			return;
		if(m_Status == EStatus::CANCELLED)
		{
			m_pRequest.reset();
			return;
		}
		if(m_pRequest->State() != EHttpState::DONE)
		{
			m_Status = m_pRequest->State() == EHttpState::ABORTED ? EStatus::CANCELLED : EStatus::NETWORK_ERROR;
			m_pRequest.reset();
			return;
		}
		m_StatusCode = m_pRequest->StatusCode();
		unsigned char *pData = nullptr;
		size_t Length = 0;
		m_pRequest->Result(&pData, &Length);
		const SResponse Response = ParseResponse(m_StatusCode, reinterpret_cast<const char *>(pData), Length);
		m_Detail = Response.m_Message;
		if(Response.m_Success)
			m_Status = EStatus::SUCCESS;
		else if(m_StatusCode < 200 || m_StatusCode >= 300 || Response.m_Valid)
			m_Status = EStatus::SERVER_ERROR;
		else
			m_Status = EStatus::INVALID_RESPONSE;
		m_pRequest.reset();
	}

	void CUpload::Cancel()
	{
		if(!Busy())
			return;
		m_Status = EStatus::CANCELLED;
		if(m_pPrepareJob)
			m_pPrepareJob->Cancel();
		if(m_pRequest)
			m_pRequest->Abort();
	}
	bool ValidateFilename(const char *pFilename)
	{
		if(!IsMapFilename(pFilename) || !str_utf8_check(pFilename))
			return false;
		const std::string_view Filename(pFilename);
		const size_t StemLength = Filename.size() - 4;
		if(StemLength == 0 || Filename[StemLength - 1] == ' ')
			return false;
		for(size_t Index = 0; Index < Filename.size(); ++Index)
		{
			const unsigned char Character = Filename[Index];
			if(Character < 32 || Character == 127 || std::string_view("!@#$%^&*()+|\\/[]{};:'\",<>=").find(Character) != std::string_view::npos || (Character == '.' && Index != StemLength))
				return false;
		}
		return true;
	}

	bool BuildMultipart(const char *pFilename, const char *pPlayerName, const unsigned char *pData, size_t Size, const char *pBoundary, std::string &Body)
	{
		Body.clear();
		if(!ValidateFilename(pFilename) || !pPlayerName || !str_utf8_check(pPlayerName) || *str_utf8_skip_whitespaces(pPlayerName) == '\0' || !pData || Size == 0 || Size > MAX_MAP_SIZE || !pBoundary)
			return false;
		const std::string_view Boundary(pBoundary);
		if(Boundary.empty() || Boundary.size() > 70)
			return false;
		for(const char Character : Boundary)
			if(!((Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z') || (Character >= '0' && Character <= '9') || Character == '-'))
				return false;
		if(std::string_view(reinterpret_cast<const char *>(pData), Size).find(Boundary) != std::string_view::npos ||
			std::string_view(pFilename).find(Boundary) != std::string_view::npos ||
			std::string_view(pPlayerName).find(Boundary) != std::string_view::npos)
			return false;
		Body.reserve(Size + 256);
		Body = "--" + std::string(Boundary) + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" + pFilename + "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
		Body.append(reinterpret_cast<const char *>(pData), Size);
		Body += "\r\n--" + std::string(Boundary) + "\r\nContent-Disposition: form-data; name=\"player_id\"\r\n\r\n" + pPlayerName + "\r\n--" + std::string(Boundary) + "--\r\n";
		return true;
	}

	SResponse ParseResponse(int StatusCode, const char *pData, size_t Length)
	{
		SResponse Result;
		if(!pData || Length == 0 || Length > MAX_RESPONSE_SIZE)
			return Result;
		json_value *pRoot = JsonParse(pData, Length);
		if(!pRoot)
			return Result;
		if(pRoot->type == json_object)
		{
			const json_value *pSuccess = json_object_get(pRoot, "success");
			Result.m_Valid = pSuccess && pSuccess->type == json_boolean;
			Result.m_Success = Result.m_Valid && pSuccess->u.boolean && StatusCode >= 200 && StatusCode < 300;
			const json_value *pMessage = json_object_get(pRoot, "message");
			if(pMessage && pMessage->type == json_string)
			{
				Result.m_Message.assign(pMessage->u.string.ptr, pMessage->u.string.length);
				for(char &Character : Result.m_Message)
					if(static_cast<unsigned char>(Character) < 32 || Character == 127)
						Character = ' ';
			}
		}
		json_value_free(pRoot);
		return Result;
	}
}
