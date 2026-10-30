#include <engine/shared/datafile.h>

#include <game/mapitems_ex.h>

#include <gtest/gtest.h>
#include <test/support/datafile_writer_operations.h>
#include <test/test.h>

#include <array>
#include <memory>
#include <utility>

namespace
{
	class DatafileWriterFailure : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CTestDatafileWriterOperations m_Operations;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}
		void Prepare(CDataFileWriter &Writer, const char *pName = "candidate.map")
		{
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), pName));
			const int Value = 123;
			ASSERT_EQ(Writer.AddItem(1, 7, sizeof(Value), &Value), 0);
			ASSERT_EQ(Writer.AddDataString("payload"), 0);
		}
		void ExpectItem(const char *pName, int Type, int Id, int Value)
		{
			CDataFileReader Reader;
			ASSERT_TRUE(Reader.Open(m_pStorage.get(), pName, IStorage::TYPE_SAVE));
			const int Index = Reader.FindItemIndex(Type, Id);
			ASSERT_GE(Index, 0);
			ASSERT_EQ(Reader.GetItemSize(Index), sizeof(int));
			EXPECT_EQ(*static_cast<int *>(Reader.GetItem(Index)), Value);
		}
	};
}

TEST_F(DatafileWriterFailure, SuccessfulOutputKeepsSortedItemsExtendedTypesAndCompressedData)
{
	CDataFileWriter Writer(&m_Operations);
	ASSERT_TRUE(Writer.Open(m_pStorage.get(), "candidate.map"));
	const int Value = 123;
	ASSERT_EQ(Writer.AddItem(4, 7, sizeof(Value), &Value), 0);
	ASSERT_EQ(Writer.AddItem(1, 8, 0, nullptr), 1);
	const int SecondValue = 456;
	ASSERT_EQ(Writer.AddItem(4, 10, sizeof(SecondValue), &SecondValue), 2);
	ASSERT_GE(Writer.AddItem(MAPITEMTYPE_TEST, 9, sizeof(Value), &Value), 0);
	ASSERT_EQ(Writer.AddDataString(""), -1);
	ASSERT_EQ(Writer.AddDataString("payload"), 0);
	const std::array<int, 2> aData{1, 0x10203040};
	ASSERT_EQ(Writer.AddDataSwapped(sizeof(aData), aData.data()), 1);
	ASSERT_EQ(Writer.AddData(4, "best", CDataFileWriter::COMPRESSION_BEST), 2);
	ASSERT_TRUE(Writer.Finish());
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(m_pStorage.get(), "candidate.map", IStorage::TYPE_SAVE));
	EXPECT_EQ(Reader.GetItemSize(Reader.FindItemIndex(1, 8)), 0);
	ASSERT_NE(Reader.FindItem(4, 7), nullptr);
	EXPECT_EQ(*static_cast<int *>(Reader.FindItem(4, 7)), Value);
	ASSERT_NE(Reader.FindItem(4, 10), nullptr);
	EXPECT_EQ(*static_cast<int *>(Reader.FindItem(4, 10)), SecondValue);
	ASSERT_NE(Reader.FindItem(MAPITEMTYPE_TEST, 9), nullptr);
	EXPECT_EQ(*static_cast<int *>(Reader.FindItem(MAPITEMTYPE_TEST, 9)), Value);
	EXPECT_STREQ(Reader.GetDataString(0), "payload");
	ASSERT_EQ(Reader.GetDataSize(1), sizeof(aData));
	ASSERT_NE(Reader.GetDataSwapped(1), nullptr);
	EXPECT_EQ(mem_comp(Reader.GetDataSwapped(1), aData.data(), sizeof(aData)), 0);
	ASSERT_NE(Reader.GetData(2), nullptr);
	EXPECT_EQ(mem_comp(Reader.GetData(2), "best", 4), 0);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(DatafileWriterFailure, ItemAllocationFailureLatchesFailureAndClosesWithoutWriting)
{
	CDataFileWriter Writer(&m_Operations);
	ASSERT_TRUE(Writer.Open(m_pStorage.get(), "candidate.map"));
	m_Operations.m_FailAllocationAt = 1;
	const int Value = 1;
	EXPECT_EQ(Writer.AddItem(1, 0, sizeof(Value), &Value), -1);
	EXPECT_EQ(Writer.AddDataString("later"), -1);
	EXPECT_EQ(Writer.AddItem(1, 1, 0, nullptr), -1);
	EXPECT_FALSE(Writer.Finish());
	EXPECT_FALSE(Writer.Finish());
	EXPECT_EQ(m_Operations.m_Writes, 0);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(DatafileWriterFailure, DataAllocationFailureRejectsOtherwiseValidPartialMap)
{
	CDataFileWriter Writer(&m_Operations);
	ASSERT_TRUE(Writer.Open(m_pStorage.get(), "candidate.map"));
	ASSERT_EQ(Writer.AddItem(1, 0, 0, nullptr), 0);
	m_Operations.m_FailAllocationAt = 1;
	EXPECT_EQ(Writer.AddDataString("payload"), -1);
	EXPECT_FALSE(Writer.Finish());
	EXPECT_EQ(m_Operations.m_Writes, 0);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(DatafileWriterFailure, ExtendedTypeDeclarationAllocationFailurePropagates)
{
	CDataFileWriter Writer(&m_Operations);
	ASSERT_TRUE(Writer.Open(m_pStorage.get(), "candidate.map"));
	m_Operations.m_FailAllocationAt = 1;
	const int Value = 1;
	EXPECT_EQ(Writer.AddItem(-1, 0, sizeof(Value), &Value, &UUID_ZEROED), -1);
	EXPECT_FALSE(Writer.Finish());
	EXPECT_EQ(m_Operations.m_Writes, 0);
}

TEST_F(DatafileWriterFailure, SwappedDataAllocationFailureRejectsFile)
{
	CDataFileWriter Writer(&m_Operations);
	ASSERT_TRUE(Writer.Open(m_pStorage.get(), "candidate.map"));
	m_Operations.m_FailAllocationAt = 1;
	const std::array<int, 2> aData{1, 2};
	EXPECT_EQ(Writer.AddDataSwapped(sizeof(aData), aData.data()), -1);
	EXPECT_FALSE(Writer.Finish());
	EXPECT_EQ(m_Operations.m_Writes, 0);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(DatafileWriterFailure, ExtendedItemPayloadFailureRejectsPreparedDeclaration)
{
	CDataFileWriter Writer(&m_Operations);
	ASSERT_TRUE(Writer.Open(m_pStorage.get(), "candidate.map"));
	m_Operations.m_FailAllocationAt = 2;
	const int Value = 1;
	EXPECT_EQ(Writer.AddItem(-1, 0, sizeof(Value), &Value, &UUID_ZEROED), -1);
	EXPECT_FALSE(Writer.Finish());
	EXPECT_EQ(m_Operations.m_Writes, 0);
}

TEST_F(DatafileWriterFailure, CompressionBufferAllocationFailureReturnsFailure)
{
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	m_Operations.m_FailAllocationAt = m_Operations.m_Allocations + 1;
	EXPECT_FALSE(Writer.Finish());
	EXPECT_EQ(m_Operations.m_Compressions, 0);
	EXPECT_EQ(m_Operations.m_Writes, 0);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(DatafileWriterFailure, CompressionFailureClosesWithoutPublishingBytes)
{
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	m_Operations.m_CompressionFails = true;
	EXPECT_FALSE(Writer.Finish());
	EXPECT_EQ(m_Operations.m_Writes, 0);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(DatafileWriterFailure, CompressionExceptionDoesNotEscapeFinish)
{
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	m_Operations.m_CompressionThrows = true;
	EXPECT_FALSE(Writer.Finish());
	EXPECT_EQ(m_Operations.m_Writes, 0);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(DatafileWriterFailure, InvalidCompressedLengthsFailBeforeAnyWrite)
{
	for(int InvalidSize : {0, 1})
	{
		SCOPED_TRACE(InvalidSize);
		m_Operations.m_InvalidCompressedSize = InvalidSize;
		CDataFileWriter Writer(&m_Operations);
		Prepare(Writer);
		EXPECT_FALSE(Writer.Finish());
		EXPECT_EQ(m_Operations.m_Writes, 0);
	}
	EXPECT_EQ(m_Operations.m_Closes, 2);
}

TEST_F(DatafileWriterFailure, EachShortWriteStopsTheFileAndReturnsFailure)
{
	// 一个 item 和一个 data 依次写入八段：表头、类型、三个索引、item 头/内容、压缩内容。
	for(int FailedWrite = 1; FailedWrite <= 8; ++FailedWrite)
	{
		SCOPED_TRACE(FailedWrite);
		m_Operations.m_Writes = 0;
		m_Operations.m_ShortWriteAt = FailedWrite;
		CDataFileWriter Writer(&m_Operations);
		Prepare(Writer);
		EXPECT_FALSE(Writer.Finish());
		EXPECT_EQ(m_Operations.m_Writes, FailedWrite);
	}
	EXPECT_EQ(m_Operations.m_Closes, 8);
}

TEST_F(DatafileWriterFailure, WriteExceptionClosesAndStopsFurtherWrites)
{
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	m_Operations.m_ThrowWriteAt = 4;
	EXPECT_FALSE(Writer.Finish());
	EXPECT_EQ(m_Operations.m_Writes, 4);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(DatafileWriterFailure, ErrorAfterCompleteWriteStillFailsImmediately)
{
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	m_Operations.m_ErrorAt = 1;
	EXPECT_FALSE(Writer.Finish());
	EXPECT_EQ(m_Operations.m_Writes, 1);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(DatafileWriterFailure, LateStreamErrorIsCheckedBeforeClosing)
{
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	m_Operations.m_ErrorAt = 9;
	EXPECT_FALSE(Writer.Finish());
	EXPECT_EQ(m_Operations.m_Writes, 8);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(DatafileWriterFailure, LateErrorExceptionStillClosesOnce)
{
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	m_Operations.m_ThrowErrorAt = 9;
	EXPECT_FALSE(Writer.Finish());
	EXPECT_FALSE(Writer.Finish());
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(DatafileWriterFailure, CloseFailureIsReportedAndNotRetriedByDestructor)
{
	{
		CDataFileWriter Writer(&m_Operations);
		Prepare(Writer);
		m_Operations.m_CloseFails = true;
		EXPECT_FALSE(Writer.Finish());
		EXPECT_FALSE(Writer.Finish());
	}
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(DatafileWriterFailure, SuccessfulFinishIsIdempotentAndRejectsLateAdditions)
{
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	ASSERT_TRUE(Writer.Finish());
	const int Writes = m_Operations.m_Writes;
	EXPECT_EQ(Writer.AddDataString("late"), -1);
	EXPECT_EQ(Writer.AddItem(1, 8, 0, nullptr), -1);
	EXPECT_TRUE(Writer.Finish());
	EXPECT_EQ(m_Operations.m_Writes, Writes);
	EXPECT_EQ(m_Operations.m_Closes, 1);
	ExpectItem("candidate.map", 1, 7, 123);
}

TEST_F(DatafileWriterFailure, ReopenAfterFailureStartsAnIndependentFile)
{
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	m_Operations.m_CloseFails = true;
	ASSERT_FALSE(Writer.Finish());
	m_Operations.m_CloseFails = false;
	ASSERT_TRUE(Writer.Open(m_pStorage.get(), "retry.map"));
	const int Value = 456;
	ASSERT_EQ(Writer.AddItem(2, 9, sizeof(Value), &Value), 0);
	ASSERT_TRUE(Writer.Finish());
	ExpectItem("retry.map", 2, 9, Value);
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(m_pStorage.get(), "retry.map", IStorage::TYPE_SAVE));
	EXPECT_EQ(Reader.NumItems(), 1);
	EXPECT_EQ(Reader.NumData(), 0);
}

TEST_F(DatafileWriterFailure, DataPreparedBeforeOpenIsPreserved)
{
	CDataFileWriter Writer(&m_Operations);
	const int Value = 123;
	ASSERT_EQ(Writer.AddItem(1, 7, sizeof(Value), &Value), 0);
	ASSERT_TRUE(Writer.Open(m_pStorage.get(), "candidate.map"));
	ASSERT_TRUE(Writer.Finish());
	ExpectItem("candidate.map", 1, 7, Value);
}

TEST_F(DatafileWriterFailure, OpenFailureCanRetryWithoutLosingPreparedItems)
{
	CDataFileWriter Writer;
	const int Value = 123;
	ASSERT_EQ(Writer.AddItem(1, 7, sizeof(Value), &Value), 0);
	ASSERT_FALSE(Writer.Open(m_pStorage.get(), "missing-directory/candidate.map"));
	ASSERT_TRUE(Writer.Open(m_pStorage.get(), "candidate.map"));
	ASSERT_TRUE(Writer.Finish());
	ExpectItem("candidate.map", 1, 7, Value);
}

TEST_F(DatafileWriterFailure, MoveKeepsInjectedOperationsAndTransfersTheOnlyHandle)
{
	std::unique_ptr<CDataFileWriter> pMoved;
	{
		CDataFileWriter Writer(&m_Operations);
		Prepare(Writer);
		pMoved = std::make_unique<CDataFileWriter>(std::move(Writer));
	}
	EXPECT_EQ(m_Operations.m_Closes, 0);
	m_Operations.m_CloseFails = true;
	EXPECT_FALSE(pMoved->Finish());
	pMoved.reset();
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(DatafileWriterFailure, MoveRebasesDefaultOperationsAfterSourceDestruction)
{
	std::unique_ptr<CDataFileWriter> pMoved;
	{
		CDataFileWriter Writer;
		Prepare(Writer);
		pMoved = std::make_unique<CDataFileWriter>(std::move(Writer));
	}
	ASSERT_TRUE(pMoved->Finish());
	ExpectItem("candidate.map", 1, 7, 123);
}

TEST_F(DatafileWriterFailure, MovePreservesLatchedPreparationFailure)
{
	CDataFileWriter Writer(&m_Operations);
	ASSERT_TRUE(Writer.Open(m_pStorage.get(), "candidate.map"));
	m_Operations.m_FailAllocationAt = 1;
	ASSERT_EQ(Writer.AddDataString("payload"), -1);
	CDataFileWriter Moved(std::move(Writer));
	EXPECT_FALSE(Moved.Finish());
	EXPECT_EQ(m_Operations.m_Writes, 0);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(DatafileWriterFailure, AbandonedWriterClosesWithoutFinishing)
{
	{
		CDataFileWriter Writer(&m_Operations);
		Prepare(Writer);
	}
	EXPECT_EQ(m_Operations.m_Compressions, 0);
	EXPECT_EQ(m_Operations.m_Writes, 0);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(DatafileWriterFailure, FinishWithoutOpenReturnsFailure)
{
	CDataFileWriter Writer(&m_Operations);
	EXPECT_FALSE(Writer.Finish());
	EXPECT_FALSE(Writer.Finish());
	EXPECT_EQ(m_Operations.m_Closes, 0);
}
