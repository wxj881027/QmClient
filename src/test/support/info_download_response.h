#ifndef TEST_SUPPORT_INFO_DOWNLOAD_RESPONSE_H
#define TEST_SUPPORT_INFO_DOWNLOAD_RESPONSE_H

#include <base/system.h>

#include <engine/http.h>
#include <engine/storage.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

// 只替换传输；真实 HTTP 基类负责正文缓冲、摘要、临时文件和完成状态。
class CInfoDownloadTestRequest : public IHttpRequest
{
public:
	CInfoDownloadTestRequest(IStorage *pStorage, const char *pFilename) :
		IHttpRequest("https://info.test/communities.json")
	{
		WriteToFileAndMemory(pStorage, pFilename, IStorage::TYPE_SAVE);
		ValidateBeforeOverwrite(true);
		SkipByFileTime(false);
		LogProgress(HTTPLOG::NONE);
	}
	void Header(const char *) override {}
	void Finish(std::string_view Body, EHttpState State = EHttpState::DONE)
	{
		ASSERT_TRUE(BeforeInit());
		m_State = EHttpState::RUNNING;
		m_StatusCode = 200;
		if(!Body.empty())
			ASSERT_EQ(OnData(Body.data(), Body.size()), Body.size());
		OnCompletionInternal(State);
	}
	const char *TemporaryPath() const { return m_aDestAbsoluteTmp; }
};

inline void WriteInfoDownloadCache(IStorage *pStorage, const char *pFilename, std::string_view Body)
{
	IOHANDLE File = pStorage->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_write(File, Body.data(), Body.size()), Body.size());
	EXPECT_EQ(io_close(File), 0);
}

inline std::string ReadInfoDownloadCache(IStorage *pStorage, const char *pFilename)
{
	void *pData = nullptr;
	unsigned Length = 0;
	if(!pStorage->ReadFile(pFilename, IStorage::TYPE_SAVE, &pData, &Length))
	{
		ADD_FAILURE() << "cannot read info cache " << pFilename;
		return {};
	}
	std::unique_ptr<void, decltype(&free)> pBuffer(pData, free);
	return std::string(static_cast<const char *>(pData), Length);
}

#endif
