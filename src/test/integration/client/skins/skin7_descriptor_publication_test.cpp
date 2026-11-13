#include <base/borrowed_allocator.h>

#include <game/client/components/qmclient/skin7_descriptor_publication.h>
#include <game/client/components/qmclient/skin7_save.h>
#include <game/client/components/skins7.h>

#include <gtest/gtest.h>
#include <test/support/job_wait.h>
#include <test/test.h>

#include <array>
#include <new>

namespace
{
	class CDescriptorMemory : public IAllocationResource
	{
		void *Allocate(size_t Size, size_t Alignment) override
		{
			if(m_Fail)
				throw std::bad_alloc();
			return IAllocationResource::AllocateDefault(Size, Alignment);
		}
		void Deallocate(void *pData, size_t Size, size_t Alignment) noexcept override
		{
			IAllocationResource::DeallocateDefault(pData, Size, Alignment);
		}

	public:
		bool m_Fail = false;
	};

	class CQmSkin7DescriptorPublicationTest : public ::testing::Test
	{
	protected:
		std::array<CSkins7::CSkinPart, protocol7::NUM_SKINPARTS> m_aDefaults;
		std::array<CSkins7::CSkinPart, protocol7::NUM_SKINPARTS> m_aNamed;
		std::vector<CSkins7::CSkin> m_Skins;
		qm_skin7_descriptor::SStatus m_Status;
		bool m_ThrowLookup = false;
		bool m_ThrowDiagnostic = false;
		bool m_SawSpecial = false;
		int m_DefaultLookups = 0;

		void SetUp() override
		{
			for(int i = 0; i < protocol7::NUM_SKINPARTS; ++i)
			{
				m_aDefaults[i].m_Type = m_aNamed[i].m_Type = i;
				str_copy(m_aDefaults[i].m_aName, "default");
				str_copy(m_aNamed[i].m_aName, "named");
			}
		}

		template<typename TAllocator>
		bool PublishTo(std::vector<CSkins7::CSkin, TAllocator> &Skins, const json_value &Info, const char *pName = "skin", int Flags = 0, bool Special = false)
		{
			return qm_skin7_descriptor::Publish(Skins, pName, Flags, Special, Info, CSkins7::ms_apSkinPartNames, CSkins7::ms_apColorComponents, [&](int Part, const char *pFilename, bool AllowSpecial) -> const CSkins7::CSkinPart * {
					if(m_ThrowLookup)
						throw std::bad_alloc();
					m_SawSpecial = m_SawSpecial || AllowSpecial;
					if(!pFilename)
					{
						++m_DefaultLookups;
						return &m_aDefaults[Part];
					}
					return &m_aNamed[Part]; }, [&](qm_skin7_descriptor::SStatus Status) {
					m_Status = Status;
					if(m_ThrowDiagnostic)
						throw std::bad_alloc(); });
		}

		bool Publish(const json_value &Info, const char *pName = "skin", int Flags = 0, bool Special = false)
		{
			return PublishTo(m_Skins, Info, pName, Flags, Special);
		}

		TQmSkin7Info Parse(const char *pJson) { return QmParseSkin7Info(pJson, str_length(pJson)); }
	};

	class CQmSkin7DescriptorBindingJobTest : public CQmSkin7DescriptorPublicationTest
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CJobPool m_Pool;
		bool m_PoolActive = false;
		std::shared_ptr<CQmSkin7InfoJob> m_pJob;
		size_t m_Next = 0;

		void SetUp() override
		{
			CQmSkin7DescriptorPublicationTest::SetUp();
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("skins7", IStorage::TYPE_SAVE));
			m_PoolActive = m_Pool.TryInit(1);
			ASSERT_TRUE(m_PoolActive);
		}

		void QueueTarget(const char *pJson)
		{
			IOHANDLE File = m_pStorage->OpenFile("skins7/qm_desc_retry.json", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			ASSERT_EQ(io_write(File, pJson, str_length(pJson)), static_cast<unsigned>(str_length(pJson)));
			ASSERT_EQ(io_close(File), 0);
			m_pJob = std::make_shared<CQmSkin7InfoJob>(m_pStorage.get());
			ASSERT_TRUE(m_Pool.TryAdd(m_pJob));
			ASSERT_TRUE(WaitForJobDone(*m_pJob));
			const auto &Entries = m_pJob->Entries();
			const auto Found = std::find_if(Entries.begin(), Entries.end(), [](const auto &Entry) { return Entry.m_Name == "qm_desc_retry"; });
			ASSERT_NE(Found, Entries.end());
			m_Next = std::distance(Entries.begin(), Found);
		}

		template<typename TAllocator>
		bool FinalizeTo(std::vector<CSkins7::CSkin, TAllocator> &Skins)
		{
			return QmTryFinalizeNextSkin7(*m_pJob, m_Next, [&](const auto &Entry) {
				return PublishTo(Skins, *Entry.m_pInfo, Entry.m_Name.c_str());
			});
		}

		void TearDown() override
		{
			if(m_PoolActive)
				m_Pool.Shutdown();
		}
	};
}

TEST_F(CQmSkin7DescriptorPublicationTest, OmittedPartsUseDefaultsAndExistingColorDefaults)
{
	auto Info = Parse(R"({"skin":{}})");
	ASSERT_NE(Info, nullptr);
	ASSERT_TRUE(Publish(*Info));
	ASSERT_EQ(m_Skins.size(), 1u);
	EXPECT_EQ(m_DefaultLookups, protocol7::NUM_SKINPARTS);
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; ++Part)
	{
		EXPECT_EQ(m_Skins[0].m_apParts[Part], &m_aDefaults[Part]);
		EXPECT_EQ(m_Skins[0].m_aUseCustomColors[Part], 0);
		EXPECT_EQ(m_Skins[0].m_aPartColors[Part], Part == protocol7::SKINPART_MARKING ? 0xff00ff80u : 0x0000ff80u);
	}
}

TEST_F(CQmSkin7DescriptorPublicationTest, ValidByteComponentsAndMarkingAlphaKeepPackedColors)
{
	auto Info = Parse(R"({"skin":{"body":{"filename":"standard","custom_colors":true,"hue":18,"sat":52,"lgt":86},"marking":{"filename":"mark","custom_colors":true,"hue":101,"sat":67,"lgt":33,"alp":135}}})");
	ASSERT_NE(Info, nullptr);
	ASSERT_TRUE(Publish(*Info));
	EXPECT_EQ(m_Skins[0].m_aPartColors[protocol7::SKINPART_BODY], 0x00123456u);
	EXPECT_EQ(m_Skins[0].m_aPartColors[protocol7::SKINPART_MARKING], 0x87654321u);
	EXPECT_EQ(m_Skins[0].m_apParts[protocol7::SKINPART_BODY], &m_aNamed[protocol7::SKINPART_BODY]);
}

TEST_F(CQmSkin7DescriptorPublicationTest, NegativeAndOversizedComponentsKeepTheirDefaultBytes)
{
	auto Info = Parse(R"({"skin":{"body":{"filename":"standard","custom_colors":true,"hue":-1,"sat":17,"lgt":18},"marking":{"filename":"mark","custom_colors":true,"hue":10,"sat":256,"lgt":-2,"alp":255}}})");
	ASSERT_NE(Info, nullptr);
	ASSERT_TRUE(Publish(*Info));
	EXPECT_EQ(m_Skins[0].m_aPartColors[protocol7::SKINPART_BODY], 0x00001112u);
	EXPECT_EQ(m_Skins[0].m_aPartColors[protocol7::SKINPART_MARKING], 0xff0aff80u);
}

TEST_F(CQmSkin7DescriptorPublicationTest, ExtremeJsonNumbersKeepDefaultBytes)
{
	auto Info = Parse(R"({"skin":{"marking":{"filename":"mark","custom_colors":true,"hue":9223372036854775807,"sat":-9223372036854775808,"lgt":42,"alp":9223372036854775807}}})");
	ASSERT_NE(Info, nullptr);
	ASSERT_TRUE(Publish(*Info));
	EXPECT_EQ(m_Skins[0].m_aPartColors[protocol7::SKINPART_MARKING], 0xff00ff2au);
}

TEST_F(CQmSkin7DescriptorPublicationTest, ZeroAndMaximumByteAreAcceptedIncludingUnsignedAlpha)
{
	auto Info = Parse(R"({"skin":{"body":{"filename":"standard","custom_colors":true,"hue":0,"sat":0,"lgt":0},"marking":{"filename":"mark","custom_colors":true,"hue":255,"sat":255,"lgt":255,"alp":255}}})");
	ASSERT_NE(Info, nullptr);
	ASSERT_TRUE(Publish(*Info));
	EXPECT_EQ(m_Skins[0].m_aPartColors[protocol7::SKINPART_BODY], 0u);
	EXPECT_EQ(m_Skins[0].m_aPartColors[protocol7::SKINPART_MARKING], 0xffffffffu);
}

TEST_F(CQmSkin7DescriptorPublicationTest, NonIntegerComponentsKeepDefaults)
{
	auto Info = Parse(R"({"skin":{"marking":{"filename":"mark","custom_colors":true,"hue":1.5,"sat":"42","lgt":true,"alp":null}}})");
	ASSERT_NE(Info, nullptr);
	ASSERT_TRUE(Publish(*Info));
	EXPECT_EQ(m_Skins[0].m_aPartColors[protocol7::SKINPART_MARKING], 0xff00ff80u);
}

TEST_F(CQmSkin7DescriptorPublicationTest, ExistingBooleanAndExactTrueStringRulesArePreserved)
{
	auto Info = Parse(R"({"skin":{"body":{"filename":"standard","custom_colors":"true","hue":1},"marking":{"filename":"mark","custom_colors":"false","hue":1},"hands":{"filename":"hand","custom_colors":true,"hue":1},"feet":{"filename":"foot","custom_colors":1,"hue":1},"eyes":{"filename":"eye","custom_colors":"TRUE","hue":1}}})");
	ASSERT_NE(Info, nullptr);
	ASSERT_TRUE(Publish(*Info));
	EXPECT_EQ(m_Skins[0].m_aUseCustomColors[protocol7::SKINPART_BODY], 1);
	EXPECT_EQ(m_Skins[0].m_aUseCustomColors[protocol7::SKINPART_MARKING], 0);
	EXPECT_EQ(m_Skins[0].m_aUseCustomColors[protocol7::SKINPART_HANDS], 1);
	EXPECT_EQ(m_Skins[0].m_aUseCustomColors[protocol7::SKINPART_FEET], 0);
	EXPECT_EQ(m_Skins[0].m_aUseCustomColors[protocol7::SKINPART_EYES], 0);
}

TEST_F(CQmSkin7DescriptorPublicationTest, DisabledCustomColorsIgnoreProvidedComponents)
{
	auto Info = Parse(R"({"skin":{"body":{"filename":"standard","custom_colors":false,"hue":42,"sat":17,"lgt":18}}})");
	ASSERT_NE(Info, nullptr);
	ASSERT_TRUE(Publish(*Info));
	EXPECT_EQ(m_Skins[0].m_aPartColors[protocol7::SKINPART_BODY], 0x00ff80u);
}

TEST_F(CQmSkin7DescriptorPublicationTest, NonMarkingAlphaNeverChangesPackedColor)
{
	auto Info = Parse(R"({"skin":{"body":{"filename":"standard","custom_colors":true,"alp":255}}})");
	ASSERT_NE(Info, nullptr);
	ASSERT_TRUE(Publish(*Info));
	EXPECT_EQ(m_Skins[0].m_aPartColors[protocol7::SKINPART_BODY], 0x00ff80u);
}

TEST_F(CQmSkin7DescriptorPublicationTest, InvalidPartDoesNotPublishPartiallyPreparedDescriptor)
{
	auto Good = Parse(R"({"skin":{}})");
	auto Bad = Parse(R"({"skin":{"body":false}})");
	ASSERT_NE(Good, nullptr);
	ASSERT_NE(Bad, nullptr);
	ASSERT_TRUE(Publish(*Good, "keep"));
	EXPECT_FALSE(Publish(*Bad, "bad"));
	ASSERT_EQ(m_Skins.size(), 1u);
	EXPECT_STREQ(m_Skins[0].m_aName, "keep");
	EXPECT_EQ(m_Status.m_Error, qm_skin7_descriptor::EError::PART_OBJECT);
	EXPECT_EQ(m_Status.m_Part, protocol7::SKINPART_BODY);
}

TEST_F(CQmSkin7DescriptorPublicationTest, MissingOrWrongFilenameRejectsWholeDescriptor)
{
	for(const char *pJson : {R"({"skin":{"body":{}}})", R"({"skin":{"body":{"filename":7}}})"})
	{
		SCOPED_TRACE(pJson);
		auto Info = Parse(pJson);
		ASSERT_NE(Info, nullptr);
		EXPECT_FALSE(Publish(*Info));
		EXPECT_TRUE(m_Skins.empty());
		EXPECT_EQ(m_Status.m_Error, qm_skin7_descriptor::EError::FILENAME_STRING);
	}
}

TEST_F(CQmSkin7DescriptorPublicationTest, CaseInsensitiveDescriptorOrderingUsesNativeRecordComparator)
{
	auto Info = Parse(R"({"skin":{}})");
	ASSERT_NE(Info, nullptr);
	ASSERT_TRUE(Publish(*Info, "zulu"));
	ASSERT_TRUE(Publish(*Info, "middle"));
	ASSERT_TRUE(Publish(*Info, "Alpha"));
	EXPECT_STREQ(m_Skins[0].m_aName, "Alpha");
	EXPECT_STREQ(m_Skins[1].m_aName, "middle");
	EXPECT_STREQ(m_Skins[2].m_aName, "zulu");
}

TEST_F(CQmSkin7DescriptorPublicationTest, DiagnosticExceptionDoesNotBlockValidPublication)
{
	auto Info = Parse(R"({"skin":{}})");
	ASSERT_NE(Info, nullptr);
	m_ThrowDiagnostic = true;
	EXPECT_TRUE(Publish(*Info));
	EXPECT_EQ(m_Skins.size(), 1u);
}

TEST_F(CQmSkin7DescriptorPublicationTest, DiagnosticExceptionDoesNotTurnInvalidDescriptorIntoPublishedSkin)
{
	auto Info = Parse(R"({"skin":{"body":false}})");
	ASSERT_NE(Info, nullptr);
	m_ThrowDiagnostic = true;
	EXPECT_FALSE(Publish(*Info));
	EXPECT_TRUE(m_Skins.empty());
}

TEST_F(CQmSkin7DescriptorPublicationTest, LookupExceptionKeepsPublishedListUntilRetry)
{
	auto Info = Parse(R"({"skin":{}})");
	ASSERT_NE(Info, nullptr);
	ASSERT_TRUE(Publish(*Info, "keep"));
	m_ThrowLookup = true;
	EXPECT_THROW(Publish(*Info, "new"), std::bad_alloc);
	ASSERT_EQ(m_Skins.size(), 1u);
	EXPECT_STREQ(m_Skins[0].m_aName, "keep");
	m_ThrowLookup = false;
	EXPECT_TRUE(Publish(*Info, "new"));
}

TEST_F(CQmSkin7DescriptorPublicationTest, VectorGrowthFailurePreservesExistingDescriptors)
{
	auto Info = Parse(R"({"skin":{}})");
	ASSERT_NE(Info, nullptr);
	CDescriptorMemory Memory;
	std::vector<CSkins7::CSkin, CBorrowedAllocator<CSkins7::CSkin>> Skins(&Memory);
	Skins.reserve(1);
	ASSERT_TRUE(PublishTo(Skins, *Info, "keep"));
	Memory.m_Fail = true;
	EXPECT_THROW(PublishTo(Skins, *Info, "new"), std::bad_alloc);
	ASSERT_EQ(Skins.size(), 1u);
	EXPECT_STREQ(Skins[0].m_aName, "keep");
	Memory.m_Fail = false;
	EXPECT_TRUE(PublishTo(Skins, *Info, "new"));
}

TEST_F(CQmSkin7DescriptorPublicationTest, DescriptorFlagsAndSpecialLookupContextAreForwarded)
{
	auto Info = Parse(R"({"skin":{"body":{"filename":"x_special"}}})");
	ASSERT_NE(Info, nullptr);
	ASSERT_TRUE(Publish(*Info, "x_skin", CSkins7::SKINFLAG_SPECIAL | CSkins7::SKINFLAG_STANDARD, true));
	EXPECT_EQ(m_Skins[0].m_Flags, CSkins7::SKINFLAG_SPECIAL | CSkins7::SKINFLAG_STANDARD);
	EXPECT_TRUE(m_SawSpecial);
}

TEST_F(CQmSkin7DescriptorPublicationTest, NativeSaveEncodingRoundTripsThroughActualDescriptorPublisher)
{
	qm_skin7_save::TParts Parts{};
	Parts[protocol7::SKINPART_BODY] = {"standard", true, 0x00123456u};
	Parts[protocol7::SKINPART_MARKING] = {"mark", true, 0x87654321u};
	const auto Json = qm_skin7_save::Encode(Parts);
	auto Info = QmParseSkin7Info(Json.data(), Json.size());
	ASSERT_NE(Info, nullptr);
	ASSERT_TRUE(Publish(*Info));
	EXPECT_EQ(m_Skins[0].m_aPartColors[protocol7::SKINPART_BODY], Parts[protocol7::SKINPART_BODY].m_Color);
	EXPECT_EQ(m_Skins[0].m_aPartColors[protocol7::SKINPART_MARKING], Parts[protocol7::SKINPART_MARKING].m_Color);
}

TEST_F(CQmSkin7DescriptorBindingJobTest, NativeEntryIsReleasedAndCursorAdvancedOnlyAfterPublication)
{
	QueueTarget(R"({"skin":{}})");
	ASSERT_FALSE(HasFatalFailure());
	const size_t Before = m_Next;
	ASSERT_TRUE(FinalizeTo(m_Skins));
	EXPECT_EQ(m_Next, Before + 1);
	EXPECT_EQ(m_pJob->Entries()[Before].m_pInfo, nullptr);
	ASSERT_EQ(m_Skins.size(), 1u);
	EXPECT_STREQ(m_Skins[0].m_aName, "qm_desc_retry");
}

TEST_F(CQmSkin7DescriptorBindingJobTest, NativeEntryAndCursorSurviveVectorAllocationFailure)
{
	QueueTarget(R"({"skin":{}})");
	ASSERT_FALSE(HasFatalFailure());
	const size_t Before = m_Next;
	const auto *pJson = m_pJob->Entries()[Before].m_pInfo.get();
	CDescriptorMemory Memory;
	std::vector<CSkins7::CSkin, CBorrowedAllocator<CSkins7::CSkin>> Skins(&Memory);
	Memory.m_Fail = true;
	EXPECT_FALSE(FinalizeTo(Skins));
	EXPECT_EQ(m_Next, Before);
	EXPECT_EQ(m_pJob->Entries()[Before].m_pInfo.get(), pJson);
	EXPECT_TRUE(Skins.empty());
	Memory.m_Fail = false;
	EXPECT_TRUE(FinalizeTo(Skins));
	EXPECT_EQ(m_Next, Before + 1);
	EXPECT_EQ(m_pJob->Entries()[Before].m_pInfo, nullptr);
	EXPECT_EQ(Skins.size(), 1u);
}

TEST_F(CQmSkin7DescriptorBindingJobTest, NativeEntrySurvivesLookupFailureAndCanBeRetried)
{
	QueueTarget(R"({"skin":{}})");
	ASSERT_FALSE(HasFatalFailure());
	const size_t Before = m_Next;
	m_ThrowLookup = true;
	EXPECT_FALSE(FinalizeTo(m_Skins));
	EXPECT_EQ(m_Next, Before);
	EXPECT_NE(m_pJob->Entries()[Before].m_pInfo, nullptr);
	m_ThrowLookup = false;
	EXPECT_TRUE(FinalizeTo(m_Skins));
	EXPECT_EQ(m_Next, Before + 1);
}

TEST_F(CQmSkin7DescriptorBindingJobTest, InvalidNativeDescriptorIsConsumedWithoutPublishingPartialSkin)
{
	QueueTarget(R"({"skin":{"body":false}})");
	ASSERT_FALSE(HasFatalFailure());
	const size_t Before = m_Next;
	EXPECT_TRUE(FinalizeTo(m_Skins));
	EXPECT_EQ(m_Next, Before + 1);
	EXPECT_EQ(m_pJob->Entries()[Before].m_pInfo, nullptr);
	EXPECT_TRUE(m_Skins.empty());
}

TEST_F(CQmSkin7DescriptorBindingJobTest, FinishedCursorDoesNotCallPublisherOrReleaseAnotherEntry)
{
	QueueTarget(R"({"skin":{}})");
	ASSERT_FALSE(HasFatalFailure());
	m_Next = m_pJob->Entries().size();
	EXPECT_FALSE(QmTryFinalizeNextSkin7(*m_pJob, m_Next, [](const auto &) {
		ADD_FAILURE() << "finished descriptor cursor reached publisher";
		return true;
	}));
	EXPECT_TRUE(m_Skins.empty());
}
