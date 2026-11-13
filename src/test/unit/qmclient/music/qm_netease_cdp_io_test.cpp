#include <gtest/gtest.h>
#include <qm/hooks/netease/qm_netease_cdp_io.h>

#include <cstring>
#include <limits>
#include <vector>

using namespace QmNeteaseCdp;

namespace
{
	class CSocketScript : public ISocketIo
	{
		size_t m_Next = 0;

		SIoChunk Next() noexcept
		{
			return m_Next < m_vChunks.size() ? m_vChunks[m_Next++] : SIoChunk{EChunkStatus::DATA, 1};
		}

	public:
		uint64_t m_Now = 100;
		uint64_t m_TransferTime = 0;
		EIoReady m_Ready = EIoReady::READY;
		std::vector<SIoChunk> m_vChunks;
		int m_Waits = 0, m_Reads = 0, m_Writes = 0, m_LargestSlice = 0;
		std::atomic_bool *m_pCancelOnWait = nullptr;
		std::atomic_bool *m_pCancelOnTransfer = nullptr;
		std::string m_Written;

		CSocketScript() { m_Written.reserve(64); }
		uint64_t NowMs() const noexcept override { return m_Now; }
		EIoReady Wait(EIoDirection, int SliceMs) noexcept override
		{
			++m_Waits;
			m_LargestSlice = std::max(m_LargestSlice, SliceMs);
			m_Now += m_Ready == EIoReady::TIMEOUT ? SliceMs : 1;
			if(m_pCancelOnWait)
				m_pCancelOnWait->store(true);
			return m_Ready;
		}
		SIoChunk Read(void *pData, size_t Capacity) noexcept override
		{
			++m_Reads;
			const SIoChunk Chunk = Next();
			if(Chunk.m_Status == EChunkStatus::DATA)
				std::memset(pData, 'x', std::min(Capacity, Chunk.m_Size));
			AfterTransfer();
			return Chunk;
		}
		SIoChunk Write(const void *pData, size_t Size) noexcept override
		{
			++m_Writes;
			const SIoChunk Chunk = Next();
			if(Chunk.m_Status == EChunkStatus::DATA)
				m_Written.append(static_cast<const char *>(pData), std::min(Size, Chunk.m_Size));
			AfterTransfer();
			return Chunk;
		}
		void AfterTransfer() noexcept
		{
			m_Now += m_TransferTime;
			if(m_pCancelOnTransfer)
				m_pCancelOnTransfer->store(true);
		}
	};
}

TEST(QmNeteaseCdpIo, BudgetShrinksWithoutResettingItsOrigin)
{
	const CIoBudget Budget(100, 85);
	EXPECT_EQ(Budget.Remaining(100), 85);
	EXPECT_EQ(Budget.Slice(100), 20);
	EXPECT_EQ(Budget.Remaining(180), 5);
	EXPECT_EQ(Budget.Slice(180), 5);
	EXPECT_EQ(Budget.Remaining(185), 0);
}

TEST(QmNeteaseCdpIo, BudgetRejectsClockRollbackAndNonpositiveDurations)
{
	EXPECT_EQ(CIoBudget(100, 10).Remaining(99), 0);
	EXPECT_EQ(CIoBudget(100, 0).Remaining(100), 0);
	EXPECT_EQ(CIoBudget(100, -1).Remaining(100), 0);
	const uint64_t NearEnd = std::numeric_limits<uint64_t>::max() - 2;
	EXPECT_EQ(CIoBudget(NearEnd, 10).Remaining(NearEnd + 1), 9);
}

TEST(QmNeteaseCdpIo, StopFlagImmediatelyExhaustsBudget)
{
	std::atomic_bool Stop{false};
	const CIoBudget Budget(100, 100, &Stop);
	EXPECT_EQ(Budget.Remaining(101), 99);
	Stop.store(true);
	EXPECT_TRUE(Budget.Cancelled());
	EXPECT_EQ(Budget.Remaining(101), 0);
	EXPECT_EQ(Budget.Slice(101), 0);
}

TEST(QmNeteaseCdpIo, ReadinessTimeoutsConsumeOneSharedBudget)
{
	CSocketScript Io;
	Io.m_Ready = EIoReady::TIMEOUT;
	EXPECT_FALSE(WaitCdpSocket(Io, CIoBudget(100, 45), EIoDirection::READ));
	EXPECT_EQ(Io.m_Now, 145u);
	EXPECT_EQ(Io.m_Waits, 3);
	EXPECT_EQ(Io.m_LargestSlice, 20);
}

TEST(QmNeteaseCdpIo, ReadinessErrorFailsWithoutAnotherWait)
{
	CSocketScript Io;
	Io.m_Ready = EIoReady::FAILED;
	EXPECT_FALSE(WaitCdpSocket(Io, CIoBudget(100, 100), EIoDirection::WRITE));
	EXPECT_EQ(Io.m_Waits, 1);
}

TEST(QmNeteaseCdpIo, ExactReadCombinesPartialChunks)
{
	CSocketScript Io;
	Io.m_vChunks = {{EChunkStatus::DATA, 2}, {EChunkStatus::DATA, 1}};
	char aData[3]{};
	EXPECT_EQ(ReadCdpExact(Io, aData, sizeof(aData), CIoBudget(100, 100)), ETransferResult::COMPLETE);
	EXPECT_EQ(std::string(aData, sizeof(aData)), "xxx");
	EXPECT_EQ(Io.m_Reads, 2);
}

TEST(QmNeteaseCdpIo, WouldBlockReadRetriesWithinOriginalBudget)
{
	CSocketScript Io;
	Io.m_vChunks = {{EChunkStatus::RETRY, 0}, {EChunkStatus::DATA, 2}};
	char aData[2]{};
	EXPECT_EQ(ReadCdpExact(Io, aData, sizeof(aData), CIoBudget(100, 100)), ETransferResult::COMPLETE);
	EXPECT_EQ(Io.m_Reads, 2);
	EXPECT_EQ(std::string(aData, sizeof(aData)), "xx");
}

TEST(QmNeteaseCdpIo, NoDataBeforeDeadlineCanReturnIdle)
{
	CSocketScript Io;
	Io.m_Ready = EIoReady::TIMEOUT;
	char aData[2]{};
	EXPECT_EQ(ReadCdpExact(Io, aData, sizeof(aData), CIoBudget(100, 25), true), ETransferResult::IDLE);
	EXPECT_EQ(Io.m_Reads, 0);
}

TEST(QmNeteaseCdpIo, PartialReadCannotBeReportedAsIdle)
{
	CSocketScript Io;
	Io.m_TransferTime = 4;
	char aData[2]{};
	EXPECT_EQ(ReadCdpExact(Io, aData, sizeof(aData), CIoBudget(100, 6), true), ETransferResult::FAILED);
	EXPECT_EQ(Io.m_Reads, 1);
}

TEST(QmNeteaseCdpIo, DrippingBytesCannotExtendReadDeadline)
{
	CSocketScript Io;
	Io.m_TransferTime = 3;
	char aData[10]{};
	EXPECT_EQ(ReadCdpExact(Io, aData, sizeof(aData), CIoBudget(100, 10)), ETransferResult::FAILED);
	EXPECT_EQ(Io.m_Reads, 3);
	EXPECT_LT(Io.m_Reads, sizeof(aData));
}

TEST(QmNeteaseCdpIo, InvalidReadResultsNeverComplete)
{
	const SIoChunk aChunks[] = {{EChunkStatus::END, 0}, {EChunkStatus::FAILED, 0}, {EChunkStatus::DATA, 0}, {EChunkStatus::DATA, 3}};
	for(const SIoChunk Chunk : aChunks)
	{
		SCOPED_TRACE(static_cast<int>(Chunk.m_Status));
		CSocketScript Io;
		Io.m_vChunks = {Chunk};
		char aData[2]{};
		EXPECT_EQ(ReadCdpExact(Io, aData, sizeof(aData), CIoBudget(100, 100)), ETransferResult::FAILED);
		EXPECT_EQ(Io.m_Reads, 1);
	}
}

TEST(QmNeteaseCdpIo, CancellationDuringReadyWaitPreventsRead)
{
	std::atomic_bool Stop{false};
	CSocketScript Io;
	Io.m_pCancelOnWait = &Stop;
	char aData[2]{};
	EXPECT_EQ(ReadCdpExact(Io, aData, sizeof(aData), CIoBudget(100, 100, &Stop), true), ETransferResult::FAILED);
	EXPECT_EQ(Io.m_Reads, 0);
}

TEST(QmNeteaseCdpIo, CancellationAfterLastReadDoesNotPublishCompletion)
{
	std::atomic_bool Stop{false};
	CSocketScript Io;
	Io.m_pCancelOnTransfer = &Stop;
	char aData[1]{};
	EXPECT_EQ(ReadCdpExact(Io, aData, sizeof(aData), CIoBudget(100, 100, &Stop)), ETransferResult::FAILED);
	EXPECT_EQ(Io.m_Reads, 1);
}

TEST(QmNeteaseCdpIo, PartialWritesAndWouldBlockKeepByteOrder)
{
	CSocketScript Io;
	Io.m_vChunks = {{EChunkStatus::DATA, 2}, {EChunkStatus::RETRY, 0}, {EChunkStatus::DATA, 1}};
	EXPECT_TRUE(WriteCdpAll(Io, "abc", 3, CIoBudget(100, 100)));
	EXPECT_EQ(Io.m_Written, "abc");
	EXPECT_EQ(Io.m_Writes, 3);
}

TEST(QmNeteaseCdpIo, DrippingWritesCannotExtendDeadline)
{
	CSocketScript Io;
	Io.m_TransferTime = 3;
	EXPECT_FALSE(WriteCdpAll(Io, "abcdef", 6, CIoBudget(100, 10)));
	EXPECT_EQ(Io.m_Written, "abc");
}

TEST(QmNeteaseCdpIo, CancellationStopsPartialWriteBeforeNextChunk)
{
	std::atomic_bool Stop{false};
	CSocketScript Io;
	Io.m_pCancelOnTransfer = &Stop;
	EXPECT_FALSE(WriteCdpAll(Io, "abc", 3, CIoBudget(100, 100, &Stop)));
	EXPECT_EQ(Io.m_Written, "a");
	EXPECT_EQ(Io.m_Writes, 1);
}
