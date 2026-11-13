#include <base/os_locale.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace
{
	void ReadText(char *pOutput, size_t Size, const char *pText)
	{
		os_locale::Read(pOutput, Size, [pText](char *pBuffer, size_t Capacity) {
			str_copy(pBuffer, pText, Capacity);
			return true;
		});
	}
}

TEST(OsLocale, UnderscoreAndEncodingSuffixKeepTheirExistingMeaning)
{
	char aLocale[64];
	ReadText(aLocale, sizeof(aLocale), "zh_Hant_TW.UTF-8");
	EXPECT_STREQ(aLocale, "zh-Hant-TW");
	ReadText(aLocale, sizeof(aLocale), "de_DE@euro");
	EXPECT_STREQ(aLocale, "de-DE");
}

TEST(OsLocale, LetterCaseAndNumericTagsArePreserved)
{
	char aLocale[64];
	ReadText(aLocale, sizeof(aLocale), "es-419-x-Private");
	EXPECT_STREQ(aLocale, "es-419-x-Private");
}

TEST(OsLocale, EmptyAndUnavailablePosixLocalesUseDefault)
{
	for(const char *pText : {"", "C", "C.UTF-8", "POSIX", "POSIX@variant"})
	{
		SCOPED_TRACE(pText);
		char aLocale[64];
		ReadText(aLocale, sizeof(aLocale), pText);
		EXPECT_STREQ(aLocale, "en-US");
	}
}

TEST(OsLocale, InvalidInitialCharacterUsesDefault)
{
	char aLocale[64];
	ReadText(aLocale, sizeof(aLocale), "@broken");
	EXPECT_STREQ(aLocale, "en-US");
}

TEST(OsLocale, FailedQueryDiscardsPartialOutput)
{
	char aLocale[64];
	os_locale::Read(aLocale, sizeof(aLocale), [](char *pBuffer, size_t Size) {
		str_copy(pBuffer, "partial", Size);
		return false;
	});
	EXPECT_STREQ(aLocale, "en-US");
}

TEST(OsLocale, AllocationExceptionAfterPartialWriteUsesDefault)
{
	char aLocale[64];
	os_locale::Read(aLocale, sizeof(aLocale), [](char *pBuffer, size_t Size) -> bool {
		str_copy(pBuffer, "partial", Size);
		throw std::bad_alloc();
	});
	EXPECT_STREQ(aLocale, "en-US");
}

TEST(OsLocale, UnknownExceptionAfterPartialWriteUsesDefault)
{
	char aLocale[64];
	os_locale::Read(aLocale, sizeof(aLocale), [](char *pBuffer, size_t Size) -> bool {
		str_copy(pBuffer, "partial", Size);
		throw 7;
	});
	EXPECT_STREQ(aLocale, "en-US");
}

TEST(OsLocale, UnterminatedQueryResultUsesDefault)
{
	char aLocale[8];
	os_locale::Read(aLocale, sizeof(aLocale), [](char *pBuffer, size_t Size) {
		std::memset(pBuffer, 'x', Size);
		return true;
	});
	EXPECT_STREQ(aLocale, "en-US");
}

TEST(OsLocale, ZeroCapacityDoesNotQueryOrTouchOutput)
{
	char Output = '!';
	bool Called = false;
	os_locale::Read(&Output, 0, [&](char *, size_t) { Called = true; return true; });
	EXPECT_FALSE(Called);
	EXPECT_EQ(Output, '!');
}

TEST(OsLocale, MissingOutputDoesNotQuery)
{
	bool Called = false;
	os_locale::Read(nullptr, 16, [&](char *, size_t) { Called = true; return true; });
	EXPECT_FALSE(Called);
}

TEST(OsLocale, OneByteOutputIsTerminatedWithoutTouchingGuard)
{
	char aOutput[] = {'!', '?'};
	ReadText(aOutput, 1, "de_DE");
	EXPECT_EQ(aOutput[0], '\0');
	EXPECT_EQ(aOutput[1], '?');
}

TEST(OsLocale, ShortSuccessfulOutputKeepsTruncationBeforeNormalization)
{
	char aOutput[] = {'!', '!', '!', '!', '?'};
	ReadText(aOutput, 4, "de_DE.UTF-8");
	EXPECT_STREQ(aOutput, "de-");
	EXPECT_EQ(aOutput[4], '?');
}

TEST(OsLocale, ShortFailureOutputKeepsTerminatorAndGuard)
{
	char aOutput[] = {'!', '!', '?'};
	os_locale::Read(aOutput, 2, [](char *, size_t) { return false; });
	EXPECT_STREQ(aOutput, "e");
	EXPECT_EQ(aOutput[2], '?');
}

TEST(OsLocale, LaterSuccessfulReadDoesNotKeepEarlierFallback)
{
	char aLocale[64];
	os_locale::Read(aLocale, sizeof(aLocale), [](char *, size_t) { return false; });
	ReadText(aLocale, sizeof(aLocale), "fr_CA");
	EXPECT_STREQ(aLocale, "fr-CA");
}

namespace
{
	// 只替换原生资源操作；所有权、容量判断和回退均执行生产实现。
	struct CLocaleOperations
	{
		enum class EFailure
		{
			NONE,
			ACQUIRE_NULL,
			ACQUIRE_THROW,
			IDENTIFIER_NULL,
			IDENTIFIER_THROW,
			LENGTH_THROW,
			ALLOCATE_NULL,
			ALLOCATE_THROW,
			CONVERT_FALSE,
			CONVERT_THROW,
			CONVERT_UNTERMINATED
		};
		EFailure m_Failure = EFailure::NONE;
		int m_Locale = 0, m_Identifier = 0;
		bool m_LocaleLive = false;
		char *m_pLiveBuffer = nullptr;
		int m_Acquires = 0, m_Releases = 0, m_Allocations = 0, m_Frees = 0, m_Conversions = 0;
		int64_t m_Units = 11;
		std::array<char, 4> m_aCleanup{};
		size_t m_CleanupCount = 0;
		bool m_WrongRelease = false;
		~CLocaleOperations() { std::free(m_pLiveBuffer); }
		int *Acquire()
		{
			++m_Acquires;
			if(m_Failure == EFailure::ACQUIRE_THROW)
				throw std::bad_alloc();
			if(m_Failure == EFailure::ACQUIRE_NULL)
				return nullptr;
			m_LocaleLive = true;
			return &m_Locale;
		}
		void Release(int *pLocale) noexcept
		{
			m_WrongRelease |= pLocale != &m_Locale || !m_LocaleLive;
			m_LocaleLive = false;
			++m_Releases;
			if(m_CleanupCount < m_aCleanup.size())
				m_aCleanup[m_CleanupCount++] = 'L';
		}
		int *Identifier(int *)
		{
			if(m_Failure == EFailure::IDENTIFIER_THROW)
				throw 7;
			return m_Failure == EFailure::IDENTIFIER_NULL ? nullptr : &m_Identifier;
		}
		int64_t Length(int *)
		{
			if(m_Failure == EFailure::LENGTH_THROW)
				throw 7;
			return m_Units;
		}
		char *Allocate(size_t Bytes)
		{
			++m_Allocations;
			if(m_Failure == EFailure::ALLOCATE_THROW)
				throw std::bad_alloc();
			if(m_Failure == EFailure::ALLOCATE_NULL)
				return nullptr;
			m_pLiveBuffer = static_cast<char *>(std::malloc(Bytes));
			return m_pLiveBuffer;
		}
		void Free(char *pBuffer) noexcept
		{
			m_WrongRelease |= pBuffer != m_pLiveBuffer;
			std::free(pBuffer);
			m_pLiveBuffer = nullptr;
			++m_Frees;
			if(m_CleanupCount < m_aCleanup.size())
				m_aCleanup[m_CleanupCount++] = 'F';
		}
		bool Convert(int *, char *pBuffer, int64_t Capacity)
		{
			++m_Conversions;
			if(m_Failure == EFailure::CONVERT_THROW)
				throw 7;
			if(m_Failure == EFailure::CONVERT_FALSE)
				return false;
			if(m_Failure == EFailure::CONVERT_UNTERMINATED)
				std::memset(pBuffer, 'x', static_cast<size_t>(Capacity));
			else
				str_copy(pBuffer, "de_DE.UTF-8", static_cast<size_t>(Capacity));
			return true;
		}
	};
	class OsLocaleOwnedIdentifier : public testing::Test
	{
	protected:
		CLocaleOperations m_Operations;
		char m_aLocale[64]{};
		void Read()
		{
			os_locale::Read(m_aLocale, sizeof(m_aLocale), [&](char *pBuffer, size_t Length) { return os_locale::ReadOwnedIdentifier(pBuffer, Length, m_Operations); });
		}
		void TearDown() override
		{
			EXPECT_FALSE(m_Operations.m_LocaleLive);
			EXPECT_EQ(m_Operations.m_pLiveBuffer, nullptr);
			EXPECT_FALSE(m_Operations.m_WrongRelease);
		}
		void ExpectBothReleased()
		{
			EXPECT_EQ(m_Operations.m_Releases, 1);
			EXPECT_EQ(m_Operations.m_Frees, 1);
			EXPECT_EQ(m_Operations.m_aCleanup[0], 'F');
			EXPECT_EQ(m_Operations.m_aCleanup[1], 'L');
		}
	};
}

TEST_F(OsLocaleOwnedIdentifier, MissingLocaleDoesNotReleaseOrAllocate)
{
	m_Operations.m_Failure = CLocaleOperations::EFailure::ACQUIRE_NULL;
	Read();
	EXPECT_STREQ(m_aLocale, "en-US");
	EXPECT_EQ(m_Operations.m_Releases, 0);
	EXPECT_EQ(m_Operations.m_Allocations, 0);
}

TEST_F(OsLocaleOwnedIdentifier, AcquireExceptionDoesNotReleaseAnUnownedLocale)
{
	m_Operations.m_Failure = CLocaleOperations::EFailure::ACQUIRE_THROW;
	Read();
	EXPECT_STREQ(m_aLocale, "en-US");
	EXPECT_EQ(m_Operations.m_Releases, 0);
}

TEST_F(OsLocaleOwnedIdentifier, MissingIdentifierReleasesLocaleOnly)
{
	m_Operations.m_Failure = CLocaleOperations::EFailure::IDENTIFIER_NULL;
	Read();
	EXPECT_STREQ(m_aLocale, "en-US");
	EXPECT_EQ(m_Operations.m_Releases, 1);
	EXPECT_EQ(m_Operations.m_Allocations, 0);
}

TEST_F(OsLocaleOwnedIdentifier, IdentifierExceptionReleasesLocale)
{
	m_Operations.m_Failure = CLocaleOperations::EFailure::IDENTIFIER_THROW;
	Read();
	EXPECT_STREQ(m_aLocale, "en-US");
	EXPECT_EQ(m_Operations.m_Releases, 1);
}

TEST_F(OsLocaleOwnedIdentifier, NegativeLengthDoesNotReachAllocator)
{
	m_Operations.m_Units = -1;
	Read();
	EXPECT_STREQ(m_aLocale, "en-US");
	EXPECT_EQ(m_Operations.m_Releases, 1);
	EXPECT_EQ(m_Operations.m_Allocations, 0);
}

TEST_F(OsLocaleOwnedIdentifier, CapacityOverflowDoesNotReachAllocator)
{
	m_Operations.m_Units = std::numeric_limits<int64_t>::max();
	Read();
	EXPECT_STREQ(m_aLocale, "en-US");
	EXPECT_EQ(m_Operations.m_Releases, 1);
	EXPECT_EQ(m_Operations.m_Allocations, 0);
}

TEST_F(OsLocaleOwnedIdentifier, LengthExceptionReleasesLocale)
{
	m_Operations.m_Failure = CLocaleOperations::EFailure::LENGTH_THROW;
	Read();
	EXPECT_STREQ(m_aLocale, "en-US");
	EXPECT_EQ(m_Operations.m_Releases, 1);
}

TEST_F(OsLocaleOwnedIdentifier, AllocationRefusalReleasesLocaleWithoutFreeingNull)
{
	m_Operations.m_Failure = CLocaleOperations::EFailure::ALLOCATE_NULL;
	Read();
	EXPECT_STREQ(m_aLocale, "en-US");
	EXPECT_EQ(m_Operations.m_Releases, 1);
	EXPECT_EQ(m_Operations.m_Frees, 0);
}

TEST_F(OsLocaleOwnedIdentifier, AllocationExceptionReleasesLocale)
{
	m_Operations.m_Failure = CLocaleOperations::EFailure::ALLOCATE_THROW;
	Read();
	EXPECT_STREQ(m_aLocale, "en-US");
	EXPECT_EQ(m_Operations.m_Releases, 1);
	EXPECT_EQ(m_Operations.m_Frees, 0);
}

TEST_F(OsLocaleOwnedIdentifier, ConversionFailureReleasesBufferBeforeLocale)
{
	m_Operations.m_Failure = CLocaleOperations::EFailure::CONVERT_FALSE;
	Read();
	EXPECT_STREQ(m_aLocale, "en-US");
	ExpectBothReleased();
}

TEST_F(OsLocaleOwnedIdentifier, ConversionExceptionReleasesBufferBeforeLocale)
{
	m_Operations.m_Failure = CLocaleOperations::EFailure::CONVERT_THROW;
	Read();
	EXPECT_STREQ(m_aLocale, "en-US");
	ExpectBothReleased();
}

TEST_F(OsLocaleOwnedIdentifier, UnterminatedConversionIsRejectedAndReleased)
{
	m_Operations.m_Failure = CLocaleOperations::EFailure::CONVERT_UNTERMINATED;
	Read();
	EXPECT_STREQ(m_aLocale, "en-US");
	ExpectBothReleased();
}

TEST_F(OsLocaleOwnedIdentifier, SuccessfulConversionNormalizesAndReleasesOwnedResources)
{
	Read();
	EXPECT_STREQ(m_aLocale, "de-DE");
	EXPECT_EQ(m_Operations.m_Conversions, 1);
	ExpectBothReleased();
}

TEST_F(OsLocaleOwnedIdentifier, LaterReadRecoversAfterAllocationFailure)
{
	m_Operations.m_Failure = CLocaleOperations::EFailure::ALLOCATE_NULL;
	Read();
	m_Operations.m_Failure = CLocaleOperations::EFailure::NONE;
	Read();
	EXPECT_STREQ(m_aLocale, "de-DE");
	EXPECT_EQ(m_Operations.m_Acquires, 2);
	EXPECT_EQ(m_Operations.m_Releases, 2);
	EXPECT_EQ(m_Operations.m_Frees, 1);
}
