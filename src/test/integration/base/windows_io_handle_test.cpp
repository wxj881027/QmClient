#include <base/system.h>
#include <base/windows.h>
#include <base/windows_io.h>

#include <gtest/gtest.h>
#include <test/test.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <engine/storage.h>

#include <windows.h>

#include <io.h>

#include <cerrno>
#include <memory>

namespace
{
	using CFile = std::unique_ptr<void, decltype(&io_close)>;
	using CHandle = std::unique_ptr<void, decltype(&CloseHandle)>;

	class CErrnoRestore
	{
		int m_Errno = errno;

	public:
		~CErrnoRestore() { errno = m_Errno; }
	};

	class CObservedOperations : public CWindowsFileOperations
	{
	public:
		bool m_FailDescriptor = false;
		bool m_FailStream = false;
		int m_DescriptorCalls = 0;
		int m_StreamCalls = 0;
		int m_NativeCloses = 0;
		int m_DescriptorCloses = 0;
		int m_ClosedDescriptor = -1;
		int m_DescriptorCloseResult = -1;
		bool m_NativeCloseSucceeded = false;

		int OpenDescriptor(intptr_t Handle, int OpenFlags) noexcept override
		{
			++m_DescriptorCalls;
			if(m_FailDescriptor)
			{
				errno = EMFILE;
				return -1;
			}
			return CWindowsFileOperations::OpenDescriptor(Handle, OpenFlags);
		}

		FILE *OpenStream(int Descriptor, const char *pMode) noexcept override
		{
			++m_StreamCalls;
			if(m_FailStream)
			{
				errno = EMFILE;
				return nullptr;
			}
			return CWindowsFileOperations::OpenStream(Descriptor, pMode);
		}

		void CloseNativeHandle(intptr_t Handle) noexcept override
		{
			++m_NativeCloses;
			m_NativeCloseSucceeded = CloseHandle(reinterpret_cast<HANDLE>(Handle)) != 0;
			// 清理可能改写 errno，返回时仍须保留触发失败的原因。
			errno = EACCES;
		}

		void CloseDescriptor(int Descriptor) noexcept override
		{
			++m_DescriptorCloses;
			m_ClosedDescriptor = Descriptor;
			m_DescriptorCloseResult = _close(Descriptor);
			errno = EACCES;
		}
	};

	bool NativeHandleIsClosed(HANDLE Handle)
	{
		DWORD Flags;
		const bool Valid = GetHandleInformation(Handle, &Flags) != 0;
		return !Valid && GetLastError() == ERROR_INVALID_HANDLE;
	}

	class CWindowsIoHandleTest : public ::testing::Test
	{
		CErrnoRestore m_ErrnoRestore;
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

	protected:
		char m_aPath[IO_MAX_PATH_LENGTH];

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			str_format(m_aPath, sizeof(m_aPath), "%s/windows-file.tmp", m_Info.StoragePath());
		}

		CHandle OpenNative()
		{
			const std::wstring Path = windows_utf8_to_wide(m_aPath);
			const HANDLE Handle = CreateFileW(Path.c_str(), FILE_WRITE_DATA,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if(Handle == INVALID_HANDLE_VALUE)
			{
				ADD_FAILURE() << "CreateFileW failed: " << GetLastError();
				return {nullptr, &CloseHandle};
			}
			return {Handle, &CloseHandle};
		}
	};
}

TEST_F(CWindowsIoHandleTest, DescriptorFailureClosesOnlyTheNativeHandleAndKeepsTheError)
{
	CObservedOperations Operations;
	Operations.m_FailDescriptor = true;
	auto Handle = OpenNative();
	ASSERT_TRUE(Handle);
	const HANDLE Raw = Handle.get();
	CFile File(windows_io_from_handle(reinterpret_cast<intptr_t>(Handle.release()), "wb", &Operations), &io_close);
	const int Error = errno;
	const bool Closed = NativeHandleIsClosed(Raw);
	EXPECT_EQ(File, nullptr);
	EXPECT_EQ(Error, EMFILE);
	EXPECT_TRUE(Closed);
	EXPECT_EQ(Operations.m_DescriptorCalls, 1);
	EXPECT_EQ(Operations.m_StreamCalls, 0);
	EXPECT_EQ(Operations.m_NativeCloses, 1);
	EXPECT_TRUE(Operations.m_NativeCloseSucceeded);
	EXPECT_EQ(Operations.m_DescriptorCloses, 0);
}

TEST_F(CWindowsIoHandleTest, StreamFailureClosesTheDescriptorAndItsNativeHandleAndKeepsTheError)
{
	CObservedOperations Operations;
	Operations.m_FailStream = true;
	auto Handle = OpenNative();
	ASSERT_TRUE(Handle);
	const HANDLE Raw = Handle.get();
	CFile File(windows_io_from_handle(reinterpret_cast<intptr_t>(Handle.release()), "wb", &Operations), &io_close);
	const int Error = errno;
	const bool Closed = NativeHandleIsClosed(Raw);
	EXPECT_EQ(File, nullptr);
	EXPECT_EQ(Error, EMFILE);
	EXPECT_TRUE(Closed);
	EXPECT_EQ(Operations.m_DescriptorCalls, 1);
	EXPECT_EQ(Operations.m_StreamCalls, 1);
	EXPECT_EQ(Operations.m_NativeCloses, 0);
	EXPECT_EQ(Operations.m_DescriptorCloses, 1);
	EXPECT_GE(Operations.m_ClosedDescriptor, 0);
	EXPECT_EQ(Operations.m_DescriptorCloseResult, 0);
}

TEST_F(CWindowsIoHandleTest, SuccessLeavesTheHandleOwnedByTheReturnedStream)
{
	CObservedOperations Operations;
	auto Handle = OpenNative();
	ASSERT_TRUE(Handle);
	const HANDLE Raw = Handle.get();
	CFile File(windows_io_from_handle(reinterpret_cast<intptr_t>(Handle.release()), "wb", &Operations), &io_close);
	ASSERT_NE(File, nullptr);
	DWORD Flags;
	EXPECT_NE(GetHandleInformation(Raw, &Flags), 0);
	EXPECT_EQ(io_write(File.get(), "ok", 2), 2U);
	EXPECT_EQ(Operations.m_NativeCloses, 0);
	EXPECT_EQ(Operations.m_DescriptorCloses, 0);
	const int Result = io_close(File.release());
	const bool Closed = NativeHandleIsClosed(Raw);
	EXPECT_EQ(Result, 0);
	EXPECT_TRUE(Closed);
}

TEST_F(CWindowsIoHandleTest, ADescriptorFailureDoesNotPreventTheNextOpen)
{
	CObservedOperations Operations;
	Operations.m_FailDescriptor = true;
	auto First = OpenNative();
	ASSERT_TRUE(First);
	CFile Failed(windows_io_from_handle(reinterpret_cast<intptr_t>(First.release()), "wb", &Operations), &io_close);
	ASSERT_EQ(Failed, nullptr);
	Operations.m_FailDescriptor = false;
	auto Next = OpenNative();
	ASSERT_TRUE(Next);
	CFile File(windows_io_from_handle(reinterpret_cast<intptr_t>(Next.release()), "wb", &Operations), &io_close);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_write(File.get(), "ok", 2), 2U);
	EXPECT_EQ(Operations.m_NativeCloses, 1);
	EXPECT_EQ(Operations.m_DescriptorCloses, 0);
}

TEST_F(CWindowsIoHandleTest, AStreamFailureDoesNotPreventTheNextOpen)
{
	CObservedOperations Operations;
	Operations.m_FailStream = true;
	auto First = OpenNative();
	ASSERT_TRUE(First);
	CFile Failed(windows_io_from_handle(reinterpret_cast<intptr_t>(First.release()), "wb", &Operations), &io_close);
	ASSERT_EQ(Failed, nullptr);
	Operations.m_FailStream = false;
	auto Next = OpenNative();
	ASSERT_TRUE(Next);
	CFile File(windows_io_from_handle(reinterpret_cast<intptr_t>(Next.release()), "wb", &Operations), &io_close);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_write(File.get(), "ok", 2), 2U);
	EXPECT_EQ(Operations.m_NativeCloses, 0);
	EXPECT_EQ(Operations.m_DescriptorCloses, 1);
}

TEST_F(CWindowsIoHandleTest, RepeatedDescriptorFailuresReleaseEachHandle)
{
	CObservedOperations Operations;
	Operations.m_FailDescriptor = true;
	for(int Attempt = 0; Attempt < 3; ++Attempt)
	{
		SCOPED_TRACE(Attempt);
		auto Handle = OpenNative();
		ASSERT_TRUE(Handle);
		const HANDLE Raw = Handle.get();
		CFile File(windows_io_from_handle(reinterpret_cast<intptr_t>(Handle.release()), "wb", &Operations), &io_close);
		const bool Closed = NativeHandleIsClosed(Raw);
		EXPECT_EQ(File, nullptr);
		EXPECT_TRUE(Closed);
	}
	EXPECT_EQ(Operations.m_NativeCloses, 3);
	EXPECT_EQ(Operations.m_DescriptorCloses, 0);
	EXPECT_EQ(Operations.m_StreamCalls, 0);
}

TEST_F(CWindowsIoHandleTest, AFailingOperationsObjectDoesNotAffectAnotherOpen)
{
	CObservedOperations FailedOperations;
	FailedOperations.m_FailStream = true;
	auto First = OpenNative();
	ASSERT_TRUE(First);
	CFile Failed(windows_io_from_handle(reinterpret_cast<intptr_t>(First.release()), "wb", &FailedOperations), &io_close);
	ASSERT_EQ(Failed, nullptr);
	CObservedOperations HealthyOperations;
	auto Next = OpenNative();
	ASSERT_TRUE(Next);
	CFile File(windows_io_from_handle(reinterpret_cast<intptr_t>(Next.release()), "wb", &HealthyOperations), &io_close);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(HealthyOperations.m_DescriptorCalls, 1);
	EXPECT_EQ(HealthyOperations.m_StreamCalls, 1);
	EXPECT_EQ(HealthyOperations.m_DescriptorCloses, 0);
	EXPECT_EQ(FailedOperations.m_DescriptorCloses, 1);
}

TEST_F(CWindowsIoHandleTest, PublicOpenStillReadsAndAppendsBinaryBytes)
{
	CFile File(io_open(m_aPath, IOFLAG_WRITE), &io_close);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_write(File.get(), "a\n\x1a", 3), 3U);
	ASSERT_EQ(io_close(File.release()), 0);
	File.reset(io_open(m_aPath, IOFLAG_APPEND));
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_write(File.get(), "z\n", 2), 2U);
	ASSERT_EQ(io_close(File.release()), 0);
	File.reset(io_open(m_aPath, IOFLAG_READ));
	ASSERT_NE(File, nullptr);
	char aBytes[8] = {};
	EXPECT_EQ(io_read(File.get(), aBytes, sizeof(aBytes)), 5U);
	EXPECT_EQ(mem_comp(aBytes, "a\n\x1az\n", 5), 0);
}
#endif
