#include <engine/client/screenshot_save.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>
#include <test/test.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using CFile = std::unique_ptr<void, decltype(&io_close)>;

	bool WriteText(IStorage &Storage, const char *pName, const char *pBytes)
	{
		CFile File(Storage.OpenFile(pName, IOFLAG_WRITE, IStorage::TYPE_SAVE_OR_ABSOLUTE), io_close);
		if(!File)
			return false;
		const unsigned Size = str_length(pBytes);
		const bool Written = io_write(File.get(), pBytes, Size) == Size;
		return io_close(File.release()) == 0 && Written;
	}

	std::string Read(IStorage &Storage, const char *pName)
	{
		void *pBytes = nullptr;
		unsigned Size = 0;
		if(!Storage.ReadFile(pName, IStorage::TYPE_SAVE_OR_ABSOLUTE, &pBytes, &Size))
			return {};
		std::unique_ptr<void, decltype(&std::free)> Bytes(pBytes, std::free);
		return {static_cast<const char *>(pBytes), Size};
	}

	CImageInfo MakeImage(uint8_t Red = 12)
	{
		CImageInfo Image;
		Image.m_Width = Image.m_Height = 1;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = static_cast<uint8_t *>(std::malloc(4));
		if(Image.m_pData)
		{
			Image.m_pData[0] = Red;
			Image.m_pData[1] = 34;
			Image.m_pData[2] = 56;
			Image.m_pData[3] = 255;
		}
		return Image;
	}

	class CObservedFiles : public screenshot_save::CFileOperations
	{
	public:
		struct SAttempt
		{
			std::string m_Name;
			bool m_Created = false;
		};
		std::vector<SAttempt> m_vAttempts;
		bool m_FailOpen = false, m_ShortWrite = false, m_FailClose = false;
		bool m_FailReplace = false, m_ThrowReplace = false;
		bool m_CollideFirst = false, m_CollideAlways = false, m_Seeded = true;
		int m_Writes = 0, m_Closes = 0, m_Replaces = 0, m_Removes = 0;
		std::function<void()> m_BeforeReplace;

		IOHANDLE Open(IStorage &Storage, const char *pName, char *pWholePath, int WholePathSize) override
		{
			// 记录和分配都先于原生打开，避免观测代码在拿到句柄后抛异常。
			m_vAttempts.push_back({pName});
			if(m_FailOpen)
				return nullptr;
			if(m_CollideAlways || (m_CollideFirst && m_vAttempts.size() == 1))
			{
				m_Seeded = m_Seeded && WriteText(Storage, pName, "occupied");
				if(!m_Seeded)
					return nullptr;
			}
			IOHANDLE File = CFileOperations::Open(Storage, pName, pWholePath, WholePathSize);
			m_vAttempts.back().m_Created = File != nullptr;
			return File;
		}
		unsigned Write(IOHANDLE File, const void *pData, unsigned Size) noexcept override
		{
			++m_Writes;
			return CImageFileOperations::Write(File, pData, m_ShortWrite && Size > 0 ? Size - 1 : Size);
		}
		int Close(IOHANDLE File) noexcept override
		{
			++m_Closes;
			const int Result = CImageFileOperations::Close(File);
			return m_FailClose ? 1 : Result;
		}
		bool Replace(IStorage &Storage, const char *pCandidate, const char *pTarget) override
		{
			++m_Replaces;
			if(m_BeforeReplace)
			{
				auto Callback = std::move(m_BeforeReplace);
				Callback();
			}
			if(m_ThrowReplace)
				throw std::runtime_error("replace failure");
			return !m_FailReplace && CFileOperations::Replace(Storage, pCandidate, pTarget);
		}
		void Remove(IStorage &Storage, const char *pCandidate) noexcept override
		{
			++m_Removes;
			CFileOperations::Remove(Storage, pCandidate);
		}
	};

	class CInlineSaveJob : public screenshot_save::CSaveJob
	{
	public:
		using CSaveJob::CSaveJob;
		using CSaveJob::Run;
	};

	struct SConcurrentSaves
	{
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		std::string m_Name;
		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		int m_Arrived = 0;
		bool m_Aborted = false;
		bool m_aResults[2]{};
		char m_aaCandidates[2][IO_MAX_PATH_LENGTH]{};
		bool Meet()
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			++m_Arrived;
			m_Condition.notify_all();
			return m_Condition.wait_for(Lock, std::chrono::seconds(2), [&] { return m_Arrived == 2 || m_Aborted; }) && !m_Aborted;
		}
		void Abort()
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Aborted = true;
			m_Condition.notify_all();
		}
	};

	class CConcurrentFiles : public screenshot_save::CFileOperations
	{
		SConcurrentSaves &m_State;
		int m_Index;

	public:
		CConcurrentFiles(SConcurrentSaves &State, int Index) :
			m_State(State), m_Index(Index) {}
		IOHANDLE Open(IStorage &Storage, const char *pName, char *pWholePath, int WholePathSize) override
		{
			CFile File(CFileOperations::Open(Storage, pName, pWholePath, WholePathSize), io_close);
			if(!File)
				return nullptr;
			str_copy(m_State.m_aaCandidates[m_Index], pWholePath);
			// 两份真实文件同时打开后才允许写入；等待有界，异常也不遗留本次候选。
			try
			{
				if(m_State.Meet())
					return File.release();
			}
			catch(...)
			{
			}
			io_close(File.release());
			Remove(Storage, pWholePath);
			return nullptr;
		}
	};

	void RunConcurrentSave(const std::shared_ptr<SConcurrentSaves> &pState, int Index)
	{
		try
		{
			screenshot_save::COwnedImage Image(MakeImage(Index == 0 ? 12 : 77));
			if(!Image.Image().m_pData)
				return;
			CConcurrentFiles Files(*pState, Index);
			char aPath[IO_MAX_PATH_LENGTH] = {};
			pState->m_aResults[Index] = screenshot_save::Save(pState->m_pStorage.get(), pState->m_Name.c_str(), Image.Image(), nullptr, aPath, sizeof(aPath), Files);
		}
		catch(...)
		{
			pState->Abort();
		}
	}

	class ScreenshotAtomicSave : public ::testing::TestWithParam<bool>
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		screenshot_save::COwnedImage m_Image{MakeImage()};
		CObservedFiles m_Files;
		std::string m_Name;
		char m_aResult[IO_MAX_PATH_LENGTH] = "unpublished";
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_NE(m_Image.Image().m_pData, nullptr);
			m_Name = GetParam() ? "shot.WEBP" : "shot.png";
			ASSERT_TRUE(WriteText(*m_pStorage, m_Name.c_str(), "original"));
		}
		bool Save(const char *pName = nullptr)
		{
			return screenshot_save::Save(m_pStorage.get(), pName ? pName : m_Name.c_str(), m_Image.Image(), nullptr, m_aResult, sizeof(m_aResult), m_Files);
		}
		void ExpectOriginal()
		{
			EXPECT_EQ(Read(*m_pStorage, m_Name.c_str()), "original");
			EXPECT_STREQ(m_aResult, "unpublished");
		}
		void ExpectCandidatesRemoved()
		{
			for(const auto &Attempt : m_Files.m_vAttempts)
				if(Attempt.m_Created)
					EXPECT_FALSE(m_pStorage->FileExists(Attempt.m_Name.c_str(), IStorage::TYPE_SAVE_OR_ABSOLUTE)) << Attempt.m_Name;
		}
		void ExpectPixel(const char *pPath, uint8_t Red = 12)
		{
			const std::string Bytes = Read(*m_pStorage, pPath);
			screenshot_save::COwnedImage Decoded(CImageInfo{});
			ASSERT_TRUE(GetParam() ? CImageLoader::LoadWebP(Bytes.data(), Bytes.size(), pPath, Decoded.Image()) : CImageLoader::LoadPng(Bytes.data(), Bytes.size(), pPath, Decoded.Image()));
			ASSERT_EQ(Decoded.Image().m_Width, 1u);
			ASSERT_EQ(Decoded.Image().m_Height, 1u);
			ASSERT_EQ(Decoded.Image().m_Format, CImageInfo::FORMAT_RGBA);
			ASSERT_NE(Decoded.Image().m_pData, nullptr);
			EXPECT_EQ(Decoded.Image().m_pData[0], Red);
			EXPECT_EQ(Decoded.Image().m_pData[1], 34);
			EXPECT_EQ(Decoded.Image().m_pData[2], 56);
			EXPECT_EQ(Decoded.Image().m_pData[3], 255);
		}
	};
}

TEST_P(ScreenshotAtomicSave, TargetChangesOnlyAfterTheCandidateHasBeenClosed)
{
	m_Files.m_BeforeReplace = [&] {
		EXPECT_EQ(m_Files.m_Closes, 1);
		EXPECT_EQ(Read(*m_pStorage, m_Name.c_str()), "original");
		ASSERT_EQ(m_Files.m_vAttempts.size(), 1u);
		ExpectPixel(m_Files.m_vAttempts[0].m_Name.c_str());
	};
	ASSERT_TRUE(Save());
	char aExpected[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, m_Name.c_str(), aExpected, sizeof(aExpected));
	EXPECT_STREQ(m_aResult, aExpected);
	ExpectPixel(m_aResult);
	ExpectCandidatesRemoved();
	EXPECT_EQ(m_Files.m_Removes, 0);
}

TEST_P(ScreenshotAtomicSave, EncoderFailurePreservesExistingFileAndRemovesOnlyItsCandidate)
{
	m_Image.Image().m_Width = 0;
	EXPECT_FALSE(Save());
	ExpectOriginal();
	ExpectCandidatesRemoved();
	EXPECT_EQ(m_Files.m_Closes, 1);
	EXPECT_EQ(m_Files.m_Removes, 1);
	EXPECT_EQ(m_Files.m_Replaces, 0);
}

TEST_P(ScreenshotAtomicSave, ShortWritePreservesExistingFileAndRemovesTheCandidate)
{
	m_Files.m_ShortWrite = true;
	EXPECT_FALSE(Save());
	ExpectOriginal();
	ExpectCandidatesRemoved();
	EXPECT_EQ(m_Files.m_Closes, 1);
	EXPECT_EQ(m_Files.m_Replaces, 0);
}

TEST_P(ScreenshotAtomicSave, CloseFailurePreservesExistingFileAndRemovesTheCandidate)
{
	m_Files.m_FailClose = true;
	EXPECT_FALSE(Save());
	ExpectOriginal();
	ExpectCandidatesRemoved();
	EXPECT_EQ(m_Files.m_Closes, 1);
	EXPECT_EQ(m_Files.m_Replaces, 0);
}

TEST_P(ScreenshotAtomicSave, ReplacementFailurePreservesExistingFileAndRemovesTheCandidate)
{
	m_Files.m_FailReplace = true;
	EXPECT_FALSE(Save());
	ExpectOriginal();
	ExpectCandidatesRemoved();
	EXPECT_EQ(m_Files.m_Replaces, 1);
	EXPECT_EQ(m_Files.m_Removes, 1);
}

TEST_P(ScreenshotAtomicSave, FailedPublicationDoesNotPreventTheNextSave)
{
	m_Files.m_FailReplace = true;
	EXPECT_FALSE(Save());
	ExpectOriginal();
	m_Files.m_FailReplace = false;
	ASSERT_TRUE(Save());
	ASSERT_EQ(m_Files.m_vAttempts.size(), 2u);
	EXPECT_NE(m_Files.m_vAttempts[0].m_Name, m_Files.m_vAttempts[1].m_Name);
	ExpectPixel(m_aResult);
	ExpectCandidatesRemoved();
}

TEST_P(ScreenshotAtomicSave, FailureWithoutAnExistingTargetLeavesItMissing)
{
	ASSERT_TRUE(m_pStorage->RemoveFile(m_Name.c_str(), IStorage::TYPE_SAVE));
	m_Files.m_FailClose = true;
	EXPECT_FALSE(Save());
	EXPECT_FALSE(m_pStorage->FileExists(m_Name.c_str(), IStorage::TYPE_SAVE));
	EXPECT_STREQ(m_aResult, "unpublished");
	ExpectCandidatesRemoved();
}

TEST_P(ScreenshotAtomicSave, ReplacementExceptionStillRemovesTheCandidateAndPreservesTheTarget)
{
	m_Files.m_ThrowReplace = true;
	EXPECT_THROW(Save(), std::runtime_error);
	ExpectOriginal();
	ExpectCandidatesRemoved();
	EXPECT_EQ(m_Files.m_Closes, 1);
	EXPECT_EQ(m_Files.m_Removes, 1);
}

TEST_P(ScreenshotAtomicSave, OpenFailureDoesNotRemoveUnownedPathsOrChangeTheTarget)
{
	m_Files.m_FailOpen = true;
	EXPECT_FALSE(Save());
	ExpectOriginal();
	EXPECT_EQ(m_Files.m_Closes, 0);
	EXPECT_EQ(m_Files.m_Removes, 0);
	EXPECT_EQ(m_Files.m_Replaces, 0);
}

TEST_P(ScreenshotAtomicSave, OccupiedCandidateIsRetriedWithoutChangingItsContents)
{
	m_Files.m_CollideFirst = true;
	ASSERT_TRUE(Save());
	ASSERT_TRUE(m_Files.m_Seeded);
	ASSERT_EQ(m_Files.m_vAttempts.size(), 2u);
	EXPECT_NE(m_Files.m_vAttempts[0].m_Name, m_Files.m_vAttempts[1].m_Name);
	EXPECT_FALSE(m_Files.m_vAttempts[0].m_Created);
	EXPECT_EQ(Read(*m_pStorage, m_Files.m_vAttempts[0].m_Name.c_str()), "occupied");
	ExpectCandidatesRemoved();
	ExpectPixel(m_aResult);
}

TEST_P(ScreenshotAtomicSave, ExhaustedCandidateRetriesPreserveEveryOccupiedFile)
{
	m_Files.m_CollideAlways = true;
	EXPECT_FALSE(Save());
	ExpectOriginal();
	ASSERT_TRUE(m_Files.m_Seeded);
	ASSERT_EQ(m_Files.m_vAttempts.size(), 32u);
	for(const auto &Attempt : m_Files.m_vAttempts)
	{
		EXPECT_FALSE(Attempt.m_Created);
		EXPECT_EQ(Read(*m_pStorage, Attempt.m_Name.c_str()), "occupied");
	}
	EXPECT_EQ(m_Files.m_Removes, 0);
}

TEST_P(ScreenshotAtomicSave, ReentrantSameNameSavesPublishSeparateCompleteCandidates)
{
	screenshot_save::COwnedImage Second(MakeImage(77));
	ASSERT_NE(Second.Image().m_pData, nullptr);
	CObservedFiles OtherFiles;
	bool OtherSaved = false;
	m_Files.m_BeforeReplace = [&] {
		char aOtherPath[IO_MAX_PATH_LENGTH] = {};
		OtherSaved = screenshot_save::Save(m_pStorage.get(), m_Name.c_str(), Second.Image(), nullptr, aOtherPath, sizeof(aOtherPath), OtherFiles);
		EXPECT_TRUE(OtherSaved);
		ExpectPixel(aOtherPath, 77);
	};
	ASSERT_TRUE(Save());
	ASSERT_TRUE(OtherSaved);
	ASSERT_EQ(m_Files.m_vAttempts.size(), 1u);
	ASSERT_EQ(OtherFiles.m_vAttempts.size(), 1u);
	EXPECT_NE(m_Files.m_vAttempts[0].m_Name, OtherFiles.m_vAttempts[0].m_Name);
	EXPECT_FALSE(m_pStorage->FileExists(OtherFiles.m_vAttempts[0].m_Name.c_str(), IStorage::TYPE_SAVE));
	ExpectPixel(m_aResult);
	ExpectCandidatesRemoved();
}

TEST_P(ScreenshotAtomicSave, JobOwnsIoUntilFailureAndDoesNotPublishACompletion)
{
	auto pFiles = std::make_shared<CObservedFiles>();
	pFiles->m_FailClose = true;
	std::weak_ptr<CObservedFiles> Files = pFiles;
	int Failures = 0, Completions = 0;
	{
		CInlineSaveJob Job(m_pStorage.get(), m_Name.c_str(), std::move(m_Image.Image()), nullptr, [&] { ++Failures; }, [&](CImageInfo &&, const char *) { ++Completions; }, pFiles);
		pFiles.reset();
		ASSERT_FALSE(Files.expired());
		EXPECT_NO_THROW(Job.Run());
		EXPECT_EQ(Failures, 1);
		EXPECT_EQ(Completions, 0);
		EXPECT_EQ(Read(*m_pStorage, m_Name.c_str()), "original");
		const auto pObserved = Files.lock();
		ASSERT_NE(pObserved, nullptr);
		ASSERT_EQ(pObserved->m_vAttempts.size(), 1u);
		EXPECT_FALSE(m_pStorage->FileExists(pObserved->m_vAttempts[0].m_Name.c_str(), IStorage::TYPE_SAVE));
	}
	EXPECT_TRUE(Files.expired());
}

TEST_P(ScreenshotAtomicSave, JobCompletionSeesTheFinalFileAfterReplacement)
{
	int Failures = 0, Completions = 0;
	CInlineSaveJob Job(m_pStorage.get(), m_Name.c_str(), std::move(m_Image.Image()), nullptr, [&] { ++Failures; }, [&](CImageInfo &&Image, const char *pPath) {
		++Completions;
		EXPECT_NE(Image.m_pData, nullptr);
		char aExpected[IO_MAX_PATH_LENGTH];
		m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, m_Name.c_str(), aExpected, sizeof(aExpected));
		EXPECT_STREQ(pPath, aExpected);
		ExpectPixel(pPath); });
	Job.Run();
	EXPECT_EQ(Failures, 0);
	EXPECT_EQ(Completions, 1);
}

TEST_P(ScreenshotAtomicSave, AbsoluteDestinationReportsTheSameFinalPath)
{
	char aAbsolute[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, m_Name.c_str(), aAbsolute, sizeof(aAbsolute));
	ASSERT_TRUE(Save(aAbsolute));
	EXPECT_STREQ(m_aResult, aAbsolute);
	ExpectPixel(aAbsolute);
	ExpectCandidatesRemoved();
}

TEST_P(ScreenshotAtomicSave, UnicodeDirectoryKeepsItsFinalPathAndPixels)
{
	const std::string Name = std::string("照片/合影") + (GetParam() ? ".webp" : ".png");
	ASSERT_TRUE(Save(Name.c_str()));
	char aExpected[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, Name.c_str(), aExpected, sizeof(aExpected));
	EXPECT_STREQ(m_aResult, aExpected);
	ExpectPixel(m_aResult);
	ExpectCandidatesRemoved();
}

TEST_P(ScreenshotAtomicSave, OverlongResolvedPathIsRejectedBeforeOpeningAFile)
{
	const std::string LongName(IO_MAX_PATH_LENGTH, 'x');
	EXPECT_FALSE(Save(LongName.c_str()));
	ExpectOriginal();
	EXPECT_TRUE(m_Files.m_vAttempts.empty());
	EXPECT_EQ(m_Files.m_Removes, 0);
}

TEST_P(ScreenshotAtomicSave, RelativeTraversalIsStillRejectedByStorage)
{
	const std::string Name = "unused/../" + m_Name;
	EXPECT_FALSE(Save(Name.c_str()));
	ExpectOriginal();
	EXPECT_EQ(m_Files.m_Writes, 0);
	EXPECT_EQ(m_Files.m_Replaces, 0);
}

TEST_P(ScreenshotAtomicSave, NativeReplacementFailureKeepsTheBlockingDirectoryAndItsContents)
{
	ASSERT_TRUE(m_pStorage->RemoveFile(m_Name.c_str(), IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_pStorage->CreateFolder(m_Name.c_str(), IStorage::TYPE_SAVE));
	const std::string Child = m_Name + "/keep.txt";
	ASSERT_TRUE(WriteText(*m_pStorage, Child.c_str(), "keep"));
	EXPECT_FALSE(Save());
	EXPECT_EQ(Read(*m_pStorage, Child.c_str()), "keep");
	EXPECT_TRUE(m_pStorage->FolderExists(m_Name.c_str(), IStorage::TYPE_SAVE));
	ExpectCandidatesRemoved();
}

TEST_P(ScreenshotAtomicSave, ConcurrentSameNameSavesLeaveOneCompleteImageAndNoOwnedCandidates)
{
	auto pState = std::make_shared<SConcurrentSaves>();
	ASSERT_NE(pState->m_pStorage, nullptr);
	pState->m_Name = m_Name;
	// 超时后工作线程仍持有整份隔离状态，不借用 fixture 或测试栈。
	ASSERT_TRUE(WaitForAsyncIo([pState] {
		std::thread Workers[2];
		std::exception_ptr Failure;
		try
		{
			Workers[0] = std::thread([pState] { RunConcurrentSave(pState, 0); });
			Workers[1] = std::thread([pState] { RunConcurrentSave(pState, 1); });
		}
		catch(...)
		{
			Failure = std::current_exception();
			pState->Abort();
		}
		for(auto &Worker : Workers)
			if(Worker.joinable())
				Worker.join();
		if(Failure)
			std::rethrow_exception(Failure);
	}));
	ASSERT_TRUE(pState->m_aResults[0]);
	ASSERT_TRUE(pState->m_aResults[1]);
	EXPECT_STRNE(pState->m_aaCandidates[0], pState->m_aaCandidates[1]);
	for(const auto &Candidate : pState->m_aaCandidates)
	{
		EXPECT_NE(Candidate[0], '\0');
		EXPECT_FALSE(pState->m_pStorage->FileExists(Candidate, IStorage::TYPE_ABSOLUTE));
	}
	const std::string Bytes = Read(*pState->m_pStorage, m_Name.c_str());
	screenshot_save::COwnedImage Decoded(CImageInfo{});
	ASSERT_TRUE(GetParam() ? CImageLoader::LoadWebP(Bytes.data(), Bytes.size(), m_Name.c_str(), Decoded.Image()) : CImageLoader::LoadPng(Bytes.data(), Bytes.size(), m_Name.c_str(), Decoded.Image()));
	ASSERT_EQ(Decoded.Image().m_Format, CImageInfo::FORMAT_RGBA);
	ASSERT_EQ(Decoded.Image().m_Width, 1u);
	ASSERT_EQ(Decoded.Image().m_Height, 1u);
	ASSERT_NE(Decoded.Image().m_pData, nullptr);
	EXPECT_TRUE(Decoded.Image().m_pData[0] == 12 || Decoded.Image().m_pData[0] == 77);
	EXPECT_EQ(Decoded.Image().m_pData[1], 34);
	EXPECT_EQ(Decoded.Image().m_pData[2], 56);
	EXPECT_EQ(Decoded.Image().m_pData[3], 255);
}

INSTANTIATE_TEST_SUITE_P(Png, ScreenshotAtomicSave, ::testing::Values(false));
#if defined(CONF_WEBP)
INSTANTIATE_TEST_SUITE_P(Webp, ScreenshotAtomicSave, ::testing::Values(true));
#endif

TEST(ScreenshotCandidateFile, ExclusiveCreateWritesANewFile)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CFile File(pStorage->OpenFile("new", IOFLAG_WRITE | IOFLAG_EXCLUSIVE, IStorage::TYPE_SAVE), io_close);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_write(File.get(), "new", 3), 3u);
	EXPECT_EQ(io_close(File.release()), 0);
	EXPECT_EQ(Read(*pStorage, "new"), "new");
}

TEST(ScreenshotCandidateFile, ExclusiveCreatePreservesAnExistingFile)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(WriteText(*pStorage, "occupied", "original"));
	CFile File(pStorage->OpenFile("occupied", IOFLAG_WRITE | IOFLAG_EXCLUSIVE, IStorage::TYPE_SAVE), io_close);
	EXPECT_EQ(File, nullptr);
	EXPECT_EQ(Read(*pStorage, "occupied"), "original");
}

TEST(ScreenshotCandidateFile, AnotherCreatorCannotTruncateAnOpenCandidate)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CFile First(pStorage->OpenFile("busy", IOFLAG_WRITE | IOFLAG_EXCLUSIVE, IStorage::TYPE_SAVE), io_close);
	ASSERT_NE(First, nullptr);
	ASSERT_EQ(io_write(First.get(), "first", 5), 5u);
	ASSERT_EQ(io_flush(First.get()), 0);
	CFile Second(pStorage->OpenFile("busy", IOFLAG_WRITE | IOFLAG_EXCLUSIVE, IStorage::TYPE_SAVE), io_close);
	EXPECT_EQ(Second, nullptr);
	ASSERT_EQ(io_close(First.release()), 0);
	EXPECT_EQ(Read(*pStorage, "busy"), "first");
}
