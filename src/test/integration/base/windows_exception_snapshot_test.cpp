#include <base/crashdump_windows.h>
#include <base/detect.h>

#include <gtest/gtest.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <array>

namespace
{
	using crashdump_windows::CExceptionSnapshot;

	struct SSource
	{
		EXCEPTION_RECORD m_Record{};
		CONTEXT m_Context{};
		EXCEPTION_POINTERS m_Pointers{&m_Record, &m_Context};

		SSource()
		{
			m_Record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
			m_Record.ExceptionAddress = reinterpret_cast<void *>(0x1234);
			m_Record.NumberParameters = 2;
			m_Record.ExceptionInformation[0] = 1;
			m_Record.ExceptionInformation[1] = 0x5678;
			m_Context.ContextFlags = CONTEXT_FULL;
		}
	};

	// 真实相邻可读/不可读页，不模拟ReadProcessMemory或异常快照算法。
	class CNativePages
	{
		unsigned char *m_pMemory = nullptr;
		size_t m_PageSize = 0;
		bool m_Ready = false;

	public:
		CNativePages()
		{
			SYSTEM_INFO Info{};
			GetSystemInfo(&Info);
			m_PageSize = Info.dwPageSize;
			m_pMemory = static_cast<unsigned char *>(VirtualAlloc(nullptr, m_PageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
			DWORD OldProtection;
			m_Ready = m_pMemory && VirtualProtect(m_pMemory + m_PageSize, m_PageSize, PAGE_NOACCESS, &OldProtection);
		}
		~CNativePages()
		{
			if(m_pMemory)
				VirtualFree(m_pMemory, 0, MEM_RELEASE);
		}
		CNativePages(const CNativePages &) = delete;
		CNativePages &operator=(const CNativePages &) = delete;
		bool Ready() const { return m_Ready; }
		template<typename T>
		T *Inaccessible() const
		{
			return reinterpret_cast<T *>(m_pMemory + m_PageSize);
		}
		template<typename T>
		T *Crossing() const
		{
			return reinterpret_cast<T *>(m_pMemory + m_PageSize - sizeof(T) / 2);
		}
	};
}

TEST(WindowsExceptionSnapshot, CompleteInputUsesOwnedRecordsAndContext)
{
	SSource Source;
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	ASSERT_NE(Snapshot.Record(), nullptr);
	ASSERT_NE(Snapshot.Context(), nullptr);
	ASSERT_NE(Snapshot.PointersForDump(), nullptr);
	EXPECT_NE(Snapshot.Record(), &Source.m_Record);
	EXPECT_NE(Snapshot.Context(), &Source.m_Context);
	EXPECT_EQ(Snapshot.Record()->ExceptionCode, EXCEPTION_ACCESS_VIOLATION);
	EXPECT_EQ(Snapshot.Record()->ExceptionAddress, reinterpret_cast<void *>(0x1234));
	EXPECT_EQ(Snapshot.Record()->NumberParameters, 2U);
	EXPECT_EQ(Snapshot.Record()->ExceptionInformation[1], 0x5678U);
	EXPECT_EQ(Snapshot.Context()->ContextFlags, CONTEXT_FULL);
	EXPECT_EQ(Snapshot.PointersForDump()->ExceptionRecord, Snapshot.Record());
	EXPECT_EQ(Snapshot.PointersForDump()->ContextRecord, Snapshot.Context());
}

TEST(WindowsExceptionSnapshot, LaterSourceChangesDoNotChangeCapturedInput)
{
	SSource Source;
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	Source.m_Record.ExceptionCode = EXCEPTION_ILLEGAL_INSTRUCTION;
	Source.m_Record.ExceptionInformation[1] = 0;
	Source.m_Context.ContextFlags = 0;
	Source.m_Pointers = {};
	ASSERT_NE(Snapshot.PointersForDump(), nullptr);
	EXPECT_EQ(Snapshot.Record()->ExceptionCode, EXCEPTION_ACCESS_VIOLATION);
	EXPECT_EQ(Snapshot.Record()->ExceptionInformation[1], 0x5678U);
	EXPECT_EQ(Snapshot.Context()->ContextFlags, CONTEXT_FULL);
}

TEST(WindowsExceptionSnapshot, NullInputClearsPreviousCapture)
{
	SSource Source;
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	ASSERT_NE(Snapshot.PointersForDump(), nullptr);
	Snapshot.Capture(nullptr);
	EXPECT_EQ(Snapshot.Record(), nullptr);
	EXPECT_EQ(Snapshot.Context(), nullptr);
	EXPECT_EQ(Snapshot.PointersForDump(), nullptr);
}

TEST(WindowsExceptionSnapshot, UnreadableTopLevelHasNoPublishedInput)
{
	CNativePages Pages;
	ASSERT_TRUE(Pages.Ready());
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(Pages.Inaccessible<EXCEPTION_POINTERS>());
	EXPECT_EQ(Snapshot.Record(), nullptr);
	EXPECT_EQ(Snapshot.Context(), nullptr);
	EXPECT_EQ(Snapshot.PointersForDump(), nullptr);
}

TEST(WindowsExceptionSnapshot, PartiallyReadableTopLevelDoesNotPublishPartialPointers)
{
	CNativePages Pages;
	ASSERT_TRUE(Pages.Ready());
	SSource Source;
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	Snapshot.Capture(Pages.Crossing<EXCEPTION_POINTERS>());
	EXPECT_EQ(Snapshot.Record(), nullptr);
	EXPECT_EQ(Snapshot.Context(), nullptr);
	EXPECT_EQ(Snapshot.PointersForDump(), nullptr);
}

TEST(WindowsExceptionSnapshot, UnreadableRecordKeepsContextButOmitsDumpExceptionStream)
{
	CNativePages Pages;
	ASSERT_TRUE(Pages.Ready());
	SSource Source;
	Source.m_Pointers.ExceptionRecord = Pages.Inaccessible<EXCEPTION_RECORD>();
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	EXPECT_EQ(Snapshot.Record(), nullptr);
	ASSERT_NE(Snapshot.Context(), nullptr);
	EXPECT_EQ(Snapshot.Context()->ContextFlags, CONTEXT_FULL);
	EXPECT_EQ(Snapshot.PointersForDump(), nullptr);
}

TEST(WindowsExceptionSnapshot, UnreadableContextKeepsRecordButOmitsDumpExceptionStream)
{
	CNativePages Pages;
	ASSERT_TRUE(Pages.Ready());
	SSource Source;
	Source.m_Pointers.ContextRecord = Pages.Inaccessible<CONTEXT>();
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	ASSERT_NE(Snapshot.Record(), nullptr);
	EXPECT_EQ(Snapshot.Record()->ExceptionCode, EXCEPTION_ACCESS_VIOLATION);
	EXPECT_EQ(Snapshot.Context(), nullptr);
	EXPECT_EQ(Snapshot.PointersForDump(), nullptr);
}

TEST(WindowsExceptionSnapshot, PartiallyReadableRecordIsNotPublished)
{
	CNativePages Pages;
	ASSERT_TRUE(Pages.Ready());
	SSource Source;
	Source.m_Pointers.ExceptionRecord = Pages.Crossing<EXCEPTION_RECORD>();
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	EXPECT_EQ(Snapshot.Record(), nullptr);
	EXPECT_NE(Snapshot.Context(), nullptr);
	EXPECT_EQ(Snapshot.PointersForDump(), nullptr);
}

TEST(WindowsExceptionSnapshot, PartiallyReadableContextIsNotPublished)
{
	CNativePages Pages;
	ASSERT_TRUE(Pages.Ready());
	SSource Source;
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	Source.m_Pointers.ContextRecord = Pages.Crossing<CONTEXT>();
	Snapshot.Capture(&Source.m_Pointers);
	EXPECT_NE(Snapshot.Record(), nullptr);
	EXPECT_EQ(Snapshot.Context(), nullptr);
	EXPECT_EQ(Snapshot.PointersForDump(), nullptr);
}

TEST(WindowsExceptionSnapshot, UnreadableNestedRecordDoesNotInvalidateThePrimaryRecord)
{
	CNativePages Pages;
	ASSERT_TRUE(Pages.Ready());
	SSource Source;
	Source.m_Record.ExceptionRecord = Pages.Inaccessible<EXCEPTION_RECORD>();
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	ASSERT_NE(Snapshot.PointersForDump(), nullptr);
	EXPECT_EQ(Snapshot.Record()->ExceptionCode, EXCEPTION_ACCESS_VIOLATION);
	EXPECT_EQ(Snapshot.Record()->ExceptionRecord, nullptr);
	EXPECT_EQ(Source.m_Record.ExceptionRecord, Pages.Inaccessible<EXCEPTION_RECORD>());
}

TEST(WindowsExceptionSnapshot, NestedChainHasOwnedLinksAndRetainsNestedDetails)
{
	SSource Source;
	EXCEPTION_RECORD Nested{}, Last{};
	Source.m_Record.ExceptionRecord = &Nested;
	Nested.ExceptionCode = EXCEPTION_IN_PAGE_ERROR;
	Nested.ExceptionRecord = &Last;
	Last.ExceptionCode = EXCEPTION_INT_DIVIDE_BY_ZERO;
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	Nested = {};
	Last = {};
	ASSERT_NE(Snapshot.PointersForDump(), nullptr);
	const auto *pNested = Snapshot.Record()->ExceptionRecord;
	ASSERT_NE(pNested, nullptr);
	EXPECT_NE(pNested, &Nested);
	EXPECT_EQ(pNested->ExceptionCode, EXCEPTION_IN_PAGE_ERROR);
	ASSERT_NE(pNested->ExceptionRecord, nullptr);
	EXPECT_NE(pNested->ExceptionRecord, &Last);
	EXPECT_EQ(pNested->ExceptionRecord->ExceptionCode, EXCEPTION_INT_DIVIDE_BY_ZERO);
	EXPECT_EQ(pNested->ExceptionRecord->ExceptionRecord, nullptr);
}

TEST(WindowsExceptionSnapshot, SelfReferencingRecordProducesOneTerminatedRecord)
{
	SSource Source;
	Source.m_Record.ExceptionRecord = &Source.m_Record;
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	ASSERT_NE(Snapshot.PointersForDump(), nullptr);
	EXPECT_EQ(Snapshot.Record()->ExceptionCode, EXCEPTION_ACCESS_VIOLATION);
	EXPECT_EQ(Snapshot.Record()->ExceptionRecord, nullptr);
}

TEST(WindowsExceptionSnapshot, NestedCycleStopsBeforeRepeatingThePrimaryRecord)
{
	SSource Source;
	EXCEPTION_RECORD Nested{};
	Nested.ExceptionCode = EXCEPTION_IN_PAGE_ERROR;
	Nested.ExceptionRecord = &Source.m_Record;
	Source.m_Record.ExceptionRecord = &Nested;
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	ASSERT_NE(Snapshot.PointersForDump(), nullptr);
	const auto *pNested = Snapshot.Record()->ExceptionRecord;
	ASSERT_NE(pNested, nullptr);
	EXPECT_EQ(pNested->ExceptionCode, EXCEPTION_IN_PAGE_ERROR);
	EXPECT_EQ(pNested->ExceptionRecord, nullptr);
}

TEST(WindowsExceptionSnapshot, LongChainEndsAtTheFixedRecordBudget)
{
	SSource Source;
	std::array<EXCEPTION_RECORD, CExceptionSnapshot::MAX_RECORDS + 1> aChain{};
	for(size_t Index = 0; Index < aChain.size(); ++Index)
	{
		aChain[Index].ExceptionCode = static_cast<DWORD>(100 + Index);
		if(Index + 1 < aChain.size())
			aChain[Index].ExceptionRecord = &aChain[Index + 1];
	}
	Source.m_Pointers.ExceptionRecord = aChain.data();
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	const auto *pRecord = Snapshot.Record();
	for(size_t Index = 0; Index < CExceptionSnapshot::MAX_RECORDS; ++Index)
	{
		ASSERT_NE(pRecord, nullptr);
		EXPECT_EQ(pRecord->ExceptionCode, 100U + Index);
		pRecord = pRecord->ExceptionRecord;
	}
	EXPECT_EQ(pRecord, nullptr);
	EXPECT_EQ(aChain[CExceptionSnapshot::MAX_RECORDS - 1].ExceptionRecord, &aChain[CExceptionSnapshot::MAX_RECORDS]);
}

TEST(WindowsExceptionSnapshot, ParameterCountsAreBoundedForPrimaryAndNestedRecords)
{
	SSource Source;
	EXCEPTION_RECORD Nested{};
	Source.m_Record.NumberParameters = MAXDWORD;
	Source.m_Record.ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS - 1] = 0x9abc;
	Nested.NumberParameters = MAXDWORD;
	Source.m_Record.ExceptionRecord = &Nested;
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	ASSERT_NE(Snapshot.PointersForDump(), nullptr);
	EXPECT_EQ(Snapshot.Record()->NumberParameters, static_cast<DWORD>(EXCEPTION_MAXIMUM_PARAMETERS));
	EXPECT_EQ(Snapshot.Record()->ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS - 1], 0x9abcU);
	ASSERT_NE(Snapshot.Record()->ExceptionRecord, nullptr);
	EXPECT_EQ(Snapshot.Record()->ExceptionRecord->NumberParameters, static_cast<DWORD>(EXCEPTION_MAXIMUM_PARAMETERS));
	EXPECT_EQ(Source.m_Record.NumberParameters, MAXDWORD);
	EXPECT_EQ(Nested.NumberParameters, MAXDWORD);
}

TEST(WindowsExceptionSnapshot, AShorterRecaptureDoesNotRetainPreviousNestedLinks)
{
	SSource Source;
	EXCEPTION_RECORD Nested{};
	Source.m_Record.ExceptionRecord = &Nested;
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	ASSERT_NE(Snapshot.Record()->ExceptionRecord, nullptr);
	Source.m_Record.ExceptionRecord = nullptr;
	Source.m_Record.ExceptionCode = EXCEPTION_ILLEGAL_INSTRUCTION;
	Snapshot.Capture(&Source.m_Pointers);
	ASSERT_NE(Snapshot.PointersForDump(), nullptr);
	EXPECT_EQ(Snapshot.Record()->ExceptionCode, EXCEPTION_ILLEGAL_INSTRUCTION);
	EXPECT_EQ(Snapshot.Record()->ExceptionRecord, nullptr);
}

TEST(WindowsExceptionSnapshot, EmptyMembersClearBothPreviouslyCapturedParts)
{
	SSource Source;
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	Source.m_Pointers = {};
	Snapshot.Capture(&Source.m_Pointers);
	EXPECT_EQ(Snapshot.Record(), nullptr);
	EXPECT_EQ(Snapshot.Context(), nullptr);
	EXPECT_EQ(Snapshot.PointersForDump(), nullptr);
}

#if defined(CONTEXT_XSTATE)
TEST(WindowsExceptionSnapshot, FixedContextOmitsVariableLengthXstateFlags)
{
	SSource Source;
	const DWORD Flags = CONTEXT_FULL | CONTEXT_XSTATE;
	Source.m_Context.ContextFlags = Flags;
	CExceptionSnapshot Snapshot;
	Snapshot.Capture(&Source.m_Pointers);
	ASSERT_NE(Snapshot.PointersForDump(), nullptr);
	EXPECT_EQ(Snapshot.Context()->ContextFlags, CONTEXT_FULL);
	EXPECT_EQ(Source.m_Context.ContextFlags, Flags);
}
#endif
#endif
