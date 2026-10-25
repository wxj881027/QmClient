#include "http.h"

#include <base/dbg.h>
#include <base/fs.h>
#include <base/log.h>
#include <base/mem.h>
#include <base/str.h>
#include <base/system.h>
#include <base/time.h>

#include <engine/external/json-parser/json.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/version.h>

#include <algorithm>
#include <limits>
#include <utility>

IHttpRequest::IHttpRequest(const char *pUrl)
{
	str_copy(m_aUrl, pUrl);
	sha256_init(&m_ActualSha256Ctx);
}

IHttpRequest::~IHttpRequest()
{
	dbg_assert(m_File == nullptr, "HTTP request file was not closed");
	free(m_pBody);
	free(m_pBuffer);
	if(m_State == EHttpState::DONE && m_ValidateBeforeOverwrite)
	{
		OnValidation(false);
	}
}

void IHttpRequest::WriteToMemory()
{
	m_WriteToMemory = true;
	m_WriteToFile = false;
}

void IHttpRequest::WriteToFile(IStorage *pStorage, const char *pDest, int StorageType)
{
	m_WriteToMemory = false;
	m_WriteToFile = true;
	str_copy(m_aDest, pDest);
	if(StorageType == -2)
	{
		pStorage->GetBinaryPath(m_aDest, m_aDestAbsolute, sizeof(m_aDestAbsolute));
	}
	else
	{
		pStorage->GetCompletePath(StorageType, m_aDest, m_aDestAbsolute, sizeof(m_aDestAbsolute));
	}
	IStorage::FormatTmpPath(m_aDestAbsoluteTmp, sizeof(m_aDestAbsoluteTmp), m_aDestAbsolute);
}

void IHttpRequest::WriteToFileAndMemory(IStorage *pStorage, const char *pDest, int StorageType)
{
	WriteToFile(pStorage, pDest, StorageType);
	m_WriteToMemory = true;
}

bool IHttpRequest::ServerListDnsFallback()
{
	for(int Index = 1; Index <= 4; ++Index)
	{
		char aUrl[128];
		str_format(aUrl, sizeof(aUrl), "https://master%d.ddnet.org/ddnet/15/servers.json", Index);
		if(str_comp(m_aUrl, aUrl) == 0)
		{
			m_ServerListDnsFallback = true;
			return true;
		}
	}
	return false;
}

void IHttpRequest::Head()
{
	m_Type = REQUEST::HEAD;
}

void IHttpRequest::Post(const unsigned char *pData, size_t DataLength)
{
	SetPostBody(REQUEST::POST, pData, DataLength);
}

void IHttpRequest::PostForm(const unsigned char *pData, size_t DataLength)
{
	SetPostBody(REQUEST::POST_FORM, pData, DataLength);
}

void IHttpRequest::PostJson(const char *pJson)
{
	SetPostBody(REQUEST::POST_JSON, reinterpret_cast<const unsigned char *>(pJson), str_length(pJson));
}

void *IHttpRequest::AllocateRequestBody(size_t Size) noexcept
{
	return malloc(Size);
}

void IHttpRequest::SetPostBody(REQUEST Type, const unsigned char *pData, size_t DataLength)
{
	m_Type = Type;
	m_BodyLength = DataLength;
	free(m_pBody);
	m_pBody = nullptr;
	// 空POST也持有非空正文，不能把malloc(0)的空返回交给传输后端。
	m_pBody = static_cast<unsigned char *>(AllocateRequestBody(std::max(size_t{1}, DataLength)));
	if(m_pBody && DataLength > 0)
		mem_copy(m_pBody, pData, DataLength);
}

void IHttpRequest::HeaderString(const char *pName, const char *pValue)
{
	char aHeader[256];
	str_format(aHeader, sizeof(aHeader), "%s: %s", pName, pValue);
	Header(aHeader);
}

void IHttpRequest::HeaderInt(const char *pName, int Value)
{
	char aHeader[256];
	str_format(aHeader, sizeof(aHeader), "%s: %d", pName, Value);
	Header(aHeader);
}

const char *IHttpRequest::Dest() const
{
	if(m_WriteToFile)
	{
		return m_aDest;
	}
	else
	{
		return nullptr;
	}
}

void IHttpRequest::Wait()
{
	std::unique_lock Lock(m_WaitMutex);
	m_WaitCondition.wait(Lock, [this]() {
		EHttpState State = m_State.load(std::memory_order_seq_cst);
		return State != EHttpState::QUEUED && State != EHttpState::RUNNING;
	});
}

void IHttpRequest::OnValidation(bool Success) noexcept
{
	dbg_assert(m_ValidateBeforeOverwrite, "this function is illegal to call without having set ValidateBeforeOverwrite");
	m_ValidateBeforeOverwrite = false;
	try
	{
		if(Success)
		{
			if(m_IfModifiedSince >= 0 && m_StatusCode == 304) // 304 Not Modified
			{
				(void)RemoveResponseFile();
				return;
			}
			if(RenameResponseFile(false))
			{
				m_State = EHttpState::ERROR;
				RemoveResponseFileBestEffort();
				log_error("http", "i/o error, cannot move file: %s", m_aDest);
			}
		}
		else
		{
			m_State = EHttpState::ERROR;
			RemoveResponseFileBestEffort();
		}
	}
	catch(...)
	{
		m_State = EHttpState::ERROR;
		RemoveResponseFileBestEffort();
	}
}

void IHttpRequest::Result(unsigned char **ppResult, size_t *pResultLength) const
{
	dbg_assert(State() == EHttpState::DONE, "Request not done");
	dbg_assert(m_WriteToMemory, "Result only usable when written to memory");
	*ppResult = m_pBuffer;
	*pResultLength = m_ResponseLength;
}

json_value *IHttpRequest::ResultJson() const
{
	unsigned char *pResult;
	size_t ResultLength;
	Result(&pResult, &ResultLength);
	return JsonParse((char *)pResult, ResultLength);
}

const SHA256_DIGEST &IHttpRequest::ResultSha256() const
{
	dbg_assert(State() == EHttpState::DONE, "Request not done");
	dbg_assert(m_ActualSha256.has_value(), "Result SHA256 missing");
	return m_ActualSha256.value();
}

int IHttpRequest::StatusCode() const
{
	dbg_assert(State() == EHttpState::DONE, "Request not done");
	return m_StatusCode;
}

int IHttpRequest::CompletedStatusCode() const
{
	dbg_assert(Done(), "Request not completed");
	return m_StatusCode;
}

std::optional<int64_t> IHttpRequest::ResultAgeSeconds() const
{
	dbg_assert(State() == EHttpState::DONE, "Request not done");
	const auto SaturatingAdd = [](int64_t Left, int64_t Right) {
		return Right > std::numeric_limits<int64_t>::max() - Left ? std::numeric_limits<int64_t>::max() : Left + Right;
	};
	const int64_t Resident = m_ResultHeadersReceivedAt ?
					 std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::seconds>(time_get_nanoseconds() - *m_ResultHeadersReceivedAt).count()) :
					 0;
	std::optional<int64_t> Age;
	if(m_ResultAge)
	{
		Age = SaturatingAdd(*m_ResultAge, Resident);
		// Date 的时钟偏差只能让数据更旧，不能让有效 Age 变小。
		if(m_ResultDate)
			Age = std::max(*Age, std::max<int64_t>(0, time_timestamp() - *m_ResultDate));
	}
	if(m_ResultDate && m_ResultLastModified)
	{
		const int64_t SourceAge = std::max<int64_t>(0, *m_ResultDate - *m_ResultLastModified);
		const int64_t Transit = std::max(Resident, std::max<int64_t>(0, time_timestamp() - *m_ResultDate));
		const int64_t ModifiedAge = SaturatingAdd(SourceAge, Transit);
		Age = std::max(Age.value_or(0), ModifiedAge);
	}
	return Age;
}

std::optional<int64_t> IHttpRequest::ResultLastModified() const
{
	dbg_assert(State() == EHttpState::DONE, "Request not done");
	return m_ResultLastModified;
}

static bool CalculateSha256(const char *pAbsoluteFilename, SHA256_DIGEST *pSha256)
{
	IOHANDLE File = io_open(pAbsoluteFilename, IOFLAG_READ);
	if(!File)
	{
		return false;
	}
	SHA256_CTX Sha256Ctxt;
	sha256_init(&Sha256Ctxt);
	unsigned char aBuffer[64 * 1024];
	while(true)
	{
		unsigned Bytes = io_read(File, aBuffer, sizeof(aBuffer));
		if(Bytes == 0)
			break;
		sha256_update(&Sha256Ctxt, aBuffer, Bytes);
	}
	io_close(File);
	*pSha256 = sha256_finish(&Sha256Ctxt);
	return true;
}

const char *const IHttpRequest::USER_AGENT_STRING = GAME_NAME " " GAME_RELEASE_VERSION " (" CONF_PLATFORM_STRING "; " CONF_ARCH_STRING ")";

const char *IHttpRequest::GetRequestType(REQUEST Type)
{
	switch(Type)
	{
	case REQUEST::GET:
		return "GET";
	case REQUEST::HEAD:
		return "HEAD";
	case REQUEST::POST:
	case REQUEST::POST_FORM:
	case REQUEST::POST_JSON:
		return "POST";
	default:
		dbg_assert_failed("Invalid Type: %d", (int)Type);
	}
}

bool IHttpRequest::ShouldSkipRequest()
{
	if(m_WriteToFile && m_ExpectedSha256.has_value())
	{
		SHA256_DIGEST Sha256;
		if(CalculateSha256(m_aDestAbsolute, &Sha256) && Sha256 == m_ExpectedSha256.value())
		{
			log_debug("http", "skipping download because expected file already exists: %s", m_aDest);
			return true;
		}
	}
	return false;
}

bool IHttpRequest::BeforeInit()
{
	if((m_Type == REQUEST::POST || m_Type == REQUEST::POST_FORM || m_Type == REQUEST::POST_JSON) && !m_pBody)
	{
		log_error("http", "failed to allocate HTTP request body");
		return false;
	}
	if(m_WriteToFile)
	{
		if(m_SkipByFileTime)
		{
			time_t FileCreatedTime, FileModifiedTime;
			if(fs_file_time(m_aDestAbsolute, &FileCreatedTime, &FileModifiedTime) == 0)
			{
				m_IfModifiedSince = FileModifiedTime;
			}
		}

		if(fs_makedir_rec_for(m_aDestAbsoluteTmp) < 0)
		{
			log_error("http", "i/o error, cannot create folder for: %s", m_aDest);
			return false;
		}

		m_File = io_open(m_aDestAbsoluteTmp, IOFLAG_WRITE);
		if(!m_File)
		{
			log_error("http", "i/o error, cannot open file: %s", m_aDest);
			return false;
		}
	}
	return true;
}

void IHttpRequest::ResponseSample(size_t Bytes)
{
	dbg_assert(Bytes > 0 && m_WriteToMemory && !m_WriteToFile, "HTTP samples must stay in memory");
	m_ResponseSampleSize = Bytes;
}

void IHttpRequest::ByteRange(int64_t First, int64_t Last)
{
	dbg_assert(First >= 0 && Last >= First && Last < INT64_MAX, "invalid HTTP byte range");
	m_ByteRange = CHttpByteRange{First, Last, 0};
	char aRange[96];
	str_format(aRange, sizeof(aRange), "bytes=%lld-%lld", static_cast<long long>(First), static_cast<long long>(Last));
	HeaderString("Range", aRange);
	SkipByFileTime(false);
	MaxResponseSize(Last - First + 1);
}

std::optional<CHttpByteRange> IHttpRequest::ResultContentRange() const
{
	dbg_assert(Done(), "HTTP range not completed");
	return m_ResultContentRange;
}

void IHttpRequest::ResultResponseSample(unsigned char **ppResult, size_t *pResultLength) const
{
	dbg_assert(Done() && m_ResponseSampleSize > 0, "HTTP sample not finished");
	*ppResult = m_pBuffer;
	*pResultLength = m_ResponseLength;
}

void *IHttpRequest::ReallocateResponseBuffer(size_t Size) noexcept
{
	return realloc(m_pBuffer, Size);
}

int IHttpRequest::CloseResponseFile(IOHANDLE File) noexcept
{
	return io_close(File);
}

int IHttpRequest::RenameResponseFile(bool AllowDestinationRemoval)
{
	return fs_rename(m_aDestAbsoluteTmp, m_aDestAbsolute, AllowDestinationRemoval);
}

int IHttpRequest::RemoveResponseFile()
{
	return fs_remove(m_aDestAbsoluteTmp);
}

bool IHttpRequest::ReadResponseFile(void **ppResult, unsigned *pLength)
{
	*ppResult = nullptr;
	*pLength = 0;
	const std::unique_ptr<void, decltype(&io_close)> File(io_open(m_aDestAbsolute, IOFLAG_READ), io_close);
	return File && io_read_all(File.get(), ppResult, pLength);
}

void IHttpRequest::RemoveResponseFileBestEffort() noexcept
{
	try
	{
		(void)RemoveResponseFile();
	}
	catch(...)
	{
		// 候选文件可能残留，清理失败不能中断终态发布或逃出析构。
	}
}

size_t IHttpRequest::OnData(const char *pData, size_t DataSize)
{
	if(m_ResponseFailed)
		return 0;
	const auto Reject = [this] {
		m_ResponseFailed = true;
		return size_t{0};
	};
	const size_t IncomingSize = DataSize;
	if(m_ResponseSampleSize > 0)
	{
		if(m_ResponseLength >= m_ResponseSampleSize)
			return 0;
		DataSize = std::min<uint64_t>(DataSize, m_ResponseSampleSize - m_ResponseLength);
	}
	if(DataSize > std::numeric_limits<uint64_t>::max() - m_ResponseLength)
		return Reject();
	const uint64_t NewResponseLength = m_ResponseLength + DataSize;
	// Need to check for the maximum response size here as implementation may not support it,
	// e.g. curl can only guarantee it if the server sets a Content-Length header.
	if(m_MaxResponseSize >= 0 && NewResponseLength > (uint64_t)m_MaxResponseSize)
	{
		return Reject();
	}

	if(DataSize == 0)
	{
		return DataSize;
	}

	size_t Result = DataSize;

	if(m_WriteToMemory)
	{
		if(NewResponseLength > std::numeric_limits<size_t>::max())
			return Reject();
		size_t NewBufferSize = std::max((size_t)1024, m_BufferSize);
		while(NewResponseLength > NewBufferSize)
		{
			if(NewBufferSize > std::numeric_limits<size_t>::max() / 2)
			{
				NewBufferSize = static_cast<size_t>(NewResponseLength);
				break;
			}
			NewBufferSize *= 2;
		}
		if(NewBufferSize != m_BufferSize)
		{
			auto *pNewBuffer = static_cast<unsigned char *>(ReallocateResponseBuffer(NewBufferSize));
			if(pNewBuffer == nullptr)
				return Reject();
			m_pBuffer = pNewBuffer;
			m_BufferSize = NewBufferSize;
		}
		mem_copy(m_pBuffer + m_ResponseLength, pData, DataSize);
	}
	// 分配失败的正文既不计入长度，也不能提前污染摘要。
	sha256_update(&m_ActualSha256Ctx, pData, DataSize);
	if(m_WriteToFile)
	{
		Result = io_write(m_File, pData, DataSize);
		if(Result != DataSize)
			m_ResponseFailed = true;
	}
	m_ResponseLength = NewResponseLength;
	if(m_ResponseSampleSize > 0 && m_ResponseLength == m_ResponseSampleSize)
	{
		m_ResponseSampleComplete = true;
		return 0;
	}
	return m_ResponseSampleSize > 0 && Result == DataSize ? IncomingSize : Result;
}

void IHttpRequest::OnCompletionInternal(EHttpState State)
{
	try
	{
		// 后端完成成功也不能覆盖正文接收失败，文件候选仍走原错误回收路径。
		if(State == EHttpState::DONE && (m_ResponseFailed || m_ProgressCallbackFailed))
			State = EHttpState::ERROR;
		if(State == EHttpState::DONE)
		{
			m_ActualSha256 = sha256_finish(&m_ActualSha256Ctx);
			if(m_ExpectedSha256.has_value() && m_ActualSha256.value() != m_ExpectedSha256.value())
			{
				if(g_Config.m_DbgHttp || m_LogProgress >= HTTPLOG::FAILURE)
				{
					char aActualSha256[SHA256_MAXSTRSIZE];
					sha256_str(m_ActualSha256.value(), aActualSha256, sizeof(aActualSha256));
					char aExpectedSha256[SHA256_MAXSTRSIZE];
					sha256_str(m_ExpectedSha256.value(), aExpectedSha256, sizeof(aExpectedSha256));
					log_error("http", "SHA256 mismatch: got=%s, expected=%s, url=%s", aActualSha256, aExpectedSha256, m_aUrl);
				}
				State = EHttpState::ERROR;
			}
		}

		if(m_WriteToFile)
		{
			// fclose 返回失败也已消费句柄；先解除成员，异常路径不能再次关闭。
			IOHANDLE File = std::exchange(m_File, nullptr);
			if(File && CloseResponseFile(File) != 0)
			{
				if(State != EHttpState::ABORTED)
					State = EHttpState::ERROR;
				log_error("http", "i/o error, cannot close file: %s", m_aDest);
			}

			if(State == EHttpState::ERROR || State == EHttpState::ABORTED)
			{
				RemoveResponseFileBestEffort();
			}
			else if(m_IfModifiedSince >= 0 && m_StatusCode == 304) // 304 Not Modified
			{
				(void)RemoveResponseFile();
				if(m_WriteToMemory)
				{
					void *pBuffer = nullptr;
					unsigned Length = 0;
					bool Success;
					try
					{
						Success = ReadResponseFile(&pBuffer, &Length);
					}
					catch(...)
					{
						free(pBuffer);
						throw;
					}
					std::unique_ptr<void, decltype(&free)> Buffer(pBuffer, free);
					if(Success)
					{
						free(m_pBuffer);
						m_pBuffer = static_cast<unsigned char *>(Buffer.release());
						m_ResponseLength = Length;
						m_BufferSize = static_cast<size_t>(Length) + 1;
					}
					else
					{
						State = EHttpState::ERROR;
						log_error("http", "i/o error, cannot read existing file: %s", m_aDest);
					}
				}
			}
			else if(!m_ValidateBeforeOverwrite)
			{
				if(RenameResponseFile(false))
				{
					State = EHttpState::ERROR;
					RemoveResponseFileBestEffort();
					log_error("http", "i/o error, cannot move file: %s", m_aDest);
				}
			}
		}
	}
	catch(...)
	{
		if(State != EHttpState::ABORTED)
			State = EHttpState::ERROR;
		if(m_File)
			(void)CloseResponseFile(std::exchange(m_File, nullptr));
		if(m_WriteToFile)
			RemoveResponseFileBestEffort();
	}

	PublishCompletion(State);
}

bool IHttpRequest::PublishProgress()
{
	if(m_ProgressCallbackFailed)
		return false;
	auto pOwnedCallback = m_pOwnedProgressCallback;
	auto *pCallback = m_pProgressCallback;
	try
	{
		if(pCallback)
			pCallback->OnProgress();
		return true;
	}
	catch(...)
	{
		m_ProgressCallbackFailed = true;
		return false;
	}
}

void IHttpRequest::PublishCompletion(EHttpState State)
{
	// 自有回调可以在执行时清除或替换注册，保活本次正在调用的对象。
	auto pOwnedCallback = m_pOwnedProgressCallback;
	auto *pCallback = m_pProgressCallback;
	// The globally visible state must be updated after OnCompletion has finished,
	// or other threads may try to access the result of a completed HTTP request,
	// before the result has been initialized/updated in OnCompletion.
	if(pCallback != nullptr)
	{
		try
		{
			pCallback->OnCompletion(State);
		}
		catch(...)
		{
			if(State != EHttpState::ABORTED)
				State = EHttpState::ERROR;
		}
	}
	{
		std::unique_lock WaitLock(m_WaitMutex);
		m_State = State;
	}
	m_WaitCondition.notify_all();
}
