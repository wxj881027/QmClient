#include <engine/client/input_sdl3.h>

#include <gtest/gtest.h>

#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{
	struct SFailure
	{
		bool m_FailAllocation = false;
		size_t m_MaxElements = 64;
		int m_Allocations = 0, m_Constructs = 0, m_FailConstructAt = -1;
	};
	template<typename T>
	struct CAllocator
	{
		using value_type = T;
		SFailure *m_pFailure;
		explicit CAllocator(SFailure *pFailure) :
			m_pFailure(pFailure) {}
		template<typename U>
		CAllocator(const CAllocator<U> &Other) :
			m_pFailure(Other.m_pFailure)
		{
		}
		T *allocate(size_t Count)
		{
			++m_pFailure->m_Allocations;
			if(m_pFailure->m_FailAllocation)
				throw std::bad_alloc();
			return std::allocator<T>{}.allocate(Count);
		}
		void deallocate(T *pData, size_t Count) { std::allocator<T>{}.deallocate(pData, Count); }
		size_t max_size() const { return m_pFailure->m_MaxElements; }
		template<typename U, typename... TArgs>
		void construct(U *pData, TArgs &&...Args)
		{
			if(m_pFailure->m_Constructs++ == m_pFailure->m_FailConstructAt)
				throw std::runtime_error("controlled event construction failure");
			::new(static_cast<void *>(pData)) U(std::forward<TArgs>(Args)...);
		}
		template<typename U>
		bool operator==(const CAllocator<U> &Other) const
		{
			return m_pFailure == Other.m_pFailure;
		}
	};
	class CInputAdmission : public ::testing::Test
	{
	protected:
		SFailure m_Failure;
		std::vector<IInput::CEvent, CAllocator<IInput::CEvent>> m_vEvents{CAllocator<IInput::CEvent>(&m_Failure)};
		bool m_aCurrent[KEY_LAST]{}, m_aFrame[KEY_LAST]{};
		void ExistingPrefix(size_t Capacity = 2)
		{
			m_vEvents.reserve(Capacity);
			IInput::CEvent Event{};
			Event.m_InputCount = 3;
			Event.m_Flags = IInput::FLAG_TEXT;
			str_copy(Event.m_aText, "already accepted");
			m_vEvents.push_back(Event);
		}
		void ExpectPrefixOnly()
		{
			ASSERT_EQ(m_vEvents.size(), 1u);
			EXPECT_STREQ(m_vEvents.front().m_aText, "already accepted");
			EXPECT_EQ(m_vEvents.front().m_InputCount, 3u);
		}
		void AppendKeys(const SSdlKeyEvent *pKeys, size_t Count)
		{
			AppendSdlKeyEvents(m_vEvents, pKeys, Count, 7, m_aCurrent, m_aFrame);
		}
	};
}

TEST_F(CInputAdmission, LongTextReservationFailureCannotPublishItsFirstChunk)
{
	ExistingPrefix();
	m_Failure.m_FailAllocation = true;
	EXPECT_THROW(AppendSdlTextEvents(m_vEvents, std::string(100, 'x').c_str(), 7), std::bad_alloc);
	ExpectPrefixOnly();
}

TEST_F(CInputAdmission, LongTextCanBeRetriedOnceAfterReservationFailure)
{
	ExistingPrefix();
	const std::string Value(100, 'x');
	m_Failure.m_FailAllocation = true;
	EXPECT_THROW(AppendSdlTextEvents(m_vEvents, Value.c_str(), 7), std::bad_alloc);
	m_Failure.m_FailAllocation = false;
	AppendSdlTextEvents(m_vEvents, Value.c_str(), 7);
	ASSERT_EQ(m_vEvents.size(), 5u);
	std::string Committed;
	for(size_t i = 1; i < m_vEvents.size(); ++i)
	{
		Committed += m_vEvents[i].m_aText;
		EXPECT_EQ(m_vEvents[i].m_InputCount, 7u);
	}
	EXPECT_EQ(Committed, Value);
	EXPECT_STREQ(m_vEvents.front().m_aText, "already accepted");
}

TEST_F(CInputAdmission, ShortTextUsesExistingCapacityWithoutAnotherAllocation)
{
	ExistingPrefix(8);
	const int Allocations = m_Failure.m_Allocations;
	m_Failure.m_FailAllocation = true;
	EXPECT_NO_THROW(AppendSdlTextEvents(m_vEvents, "中文", 7));
	ASSERT_EQ(m_vEvents.size(), 2u);
	EXPECT_STREQ(m_vEvents.back().m_aText, "中文");
	EXPECT_EQ(m_Failure.m_Allocations, Allocations);
}

TEST_F(CInputAdmission, EmptyCompositionNotificationIsNotPublishedWhenReservationFails)
{
	ExistingPrefix(1);
	m_Failure.m_FailAllocation = true;
	EXPECT_THROW(AppendSdlTextEvents(m_vEvents, "", 7), std::bad_alloc);
	ExpectPrefixOnly();
}

TEST_F(CInputAdmission, TextCapacityLimitRejectsTheWholeCommit)
{
	m_Failure.m_MaxElements = 4;
	ExistingPrefix(2);
	EXPECT_THROW(AppendSdlTextEvents(m_vEvents, std::string(100, 'x').c_str(), 7), std::length_error);
	ExpectPrefixOnly();
}

TEST_F(CInputAdmission, LaterTextConstructionFailureRollsBackOnlyTheNewChunks)
{
	ExistingPrefix(8);
	m_Failure.m_Constructs = 0;
	m_Failure.m_FailConstructAt = 1;
	EXPECT_THROW(AppendSdlTextEvents(m_vEvents, std::string(100, 'x').c_str(), 7), std::runtime_error);
	ExpectPrefixOnly();
}

TEST_F(CInputAdmission, AFailedLaterCommitCannotRemovePreviouslyAcceptedText)
{
	ExistingPrefix(4);
	AppendSdlTextEvents(m_vEvents, "accepted second", 7);
	m_Failure.m_FailAllocation = true;
	EXPECT_THROW(AppendSdlTextEvents(m_vEvents, std::string(200, 'x').c_str(), 7), std::bad_alloc);
	ASSERT_EQ(m_vEvents.size(), 2u);
	EXPECT_STREQ(m_vEvents[0].m_aText, "already accepted");
	EXPECT_STREQ(m_vEvents[1].m_aText, "accepted second");
}

TEST_F(CInputAdmission, SingleTextConstructionFailureDoesNotLeaveANotification)
{
	ExistingPrefix(4);
	m_Failure.m_Constructs = 0;
	m_Failure.m_FailConstructAt = 0;
	EXPECT_THROW(AppendSdlTextEvents(m_vEvents, "", 7), std::runtime_error);
	ExpectPrefixOnly();
}

TEST_F(CInputAdmission, AliasReleaseReservationFailureKeepsBothKeysAndTheQueueUnchanged)
{
	ExistingPrefix(2);
	m_aCurrent[KEY_MUTE] = m_aCurrent[KEY_AUDIOMUTE] = true;
	const SSdlKeyEvent aKeys[] = {{KEY_MUTE, IInput::FLAG_RELEASE}, {KEY_AUDIOMUTE, IInput::FLAG_RELEASE}};
	m_Failure.m_FailAllocation = true;
	EXPECT_THROW(AppendKeys(aKeys, 2), std::bad_alloc);
	ExpectPrefixOnly();
	EXPECT_TRUE(m_aCurrent[KEY_MUTE]);
	EXPECT_TRUE(m_aCurrent[KEY_AUDIOMUTE]);
	EXPECT_FALSE(m_aFrame[KEY_MUTE]);
}

TEST_F(CInputAdmission, RetriedAliasReleasesKeepTheirOrderWithoutDuplicatePublication)
{
	ExistingPrefix(2);
	m_aCurrent[KEY_MUTE] = m_aCurrent[KEY_AUDIOMUTE] = true;
	const SSdlKeyEvent aKeys[] = {{KEY_MUTE, IInput::FLAG_RELEASE}, {KEY_AUDIOMUTE, IInput::FLAG_RELEASE}};
	m_Failure.m_FailAllocation = true;
	EXPECT_THROW(AppendKeys(aKeys, 2), std::bad_alloc);
	m_Failure.m_FailAllocation = false;
	AppendKeys(aKeys, 2);
	ASSERT_EQ(m_vEvents.size(), 3u);
	EXPECT_EQ(m_vEvents[1].m_Key, KEY_MUTE);
	EXPECT_EQ(m_vEvents[2].m_Key, KEY_AUDIOMUTE);
	EXPECT_EQ(m_vEvents[1].m_InputCount, 7u);
	EXPECT_FALSE(m_aCurrent[KEY_MUTE]);
	EXPECT_FALSE(m_aCurrent[KEY_AUDIOMUTE]);
}

TEST_F(CInputAdmission, ReleasePressTransitionCommitsBothStatesAfterTheWholeQueueBatch)
{
	ExistingPrefix(2);
	m_aCurrent[KEY_A] = true;
	const SSdlKeyEvent aKeys[] = {{KEY_A, IInput::FLAG_RELEASE}, {KEY_B, IInput::FLAG_PRESS}};
	m_Failure.m_FailAllocation = true;
	EXPECT_THROW(AppendKeys(aKeys, 2), std::bad_alloc);
	EXPECT_TRUE(m_aCurrent[KEY_A]);
	EXPECT_FALSE(m_aCurrent[KEY_B]);
	m_Failure.m_FailAllocation = false;
	AppendKeys(aKeys, 2);
	EXPECT_FALSE(m_aCurrent[KEY_A]);
	EXPECT_TRUE(m_aCurrent[KEY_B]);
	EXPECT_FALSE(m_aFrame[KEY_A]);
	EXPECT_TRUE(m_aFrame[KEY_B]);
}

TEST_F(CInputAdmission, LaterKeyConstructionFailureCannotPartiallyChangeHeldState)
{
	ExistingPrefix(8);
	const SSdlKeyEvent aKeys[] = {{KEY_A, IInput::FLAG_PRESS}, {KEY_B, IInput::FLAG_PRESS}};
	m_Failure.m_Constructs = 0;
	m_Failure.m_FailConstructAt = 1;
	EXPECT_THROW(AppendKeys(aKeys, 2), std::runtime_error);
	ExpectPrefixOnly();
	EXPECT_FALSE(m_aCurrent[KEY_A]);
	EXPECT_FALSE(m_aCurrent[KEY_B]);
	EXPECT_FALSE(m_aFrame[KEY_A]);
	EXPECT_FALSE(m_aFrame[KEY_B]);
}

TEST_F(CInputAdmission, PressReleasePulseKeepsTheOriginalFrameAndHeldSemantics)
{
	const SSdlKeyEvent Key{KEY_MOUSE_WHEEL_UP, IInput::FLAG_PRESS | IInput::FLAG_RELEASE};
	AppendKeys(&Key, 1);
	ASSERT_EQ(m_vEvents.size(), 1u);
	EXPECT_EQ(m_vEvents[0].m_Flags, IInput::FLAG_PRESS | IInput::FLAG_RELEASE);
	EXPECT_EQ(m_vEvents[0].m_aText[0], '\0');
	EXPECT_FALSE(m_aCurrent[KEY_MOUSE_WHEEL_UP]);
	EXPECT_TRUE(m_aFrame[KEY_MOUSE_WHEEL_UP]);
}

TEST_F(CInputAdmission, RepeatedPressKeepsItsEventFlagAndUpdatesTheFrameState)
{
	const SSdlKeyEvent Key{KEY_A, IInput::FLAG_PRESS | IInput::FLAG_REPEAT};
	AppendKeys(&Key, 1);
	ASSERT_EQ(m_vEvents.size(), 1u);
	EXPECT_EQ(m_vEvents[0].m_Flags, IInput::FLAG_PRESS | IInput::FLAG_REPEAT);
	EXPECT_TRUE(m_aCurrent[KEY_A]);
	EXPECT_TRUE(m_aFrame[KEY_A]);
}

TEST_F(CInputAdmission, KeyBatchCapacityLimitLeavesTheOriginalState)
{
	m_Failure.m_MaxElements = 2;
	ExistingPrefix(2);
	const SSdlKeyEvent aKeys[] = {{KEY_A, IInput::FLAG_PRESS}, {KEY_B, IInput::FLAG_PRESS}};
	EXPECT_THROW(AppendKeys(aKeys, 2), std::length_error);
	ExpectPrefixOnly();
	EXPECT_FALSE(m_aCurrent[KEY_A]);
	EXPECT_FALSE(m_aCurrent[KEY_B]);
}

TEST_F(CInputAdmission, EmptyKeyBatchDoesNotReserveOrChangeState)
{
	ExistingPrefix(1);
	m_aCurrent[KEY_A] = true;
	m_Failure.m_FailAllocation = true;
	EXPECT_NO_THROW(AppendKeys(nullptr, 0));
	ExpectPrefixOnly();
	EXPECT_TRUE(m_aCurrent[KEY_A]);
}

TEST_F(CInputAdmission, KeyBatchUsesExistingCapacityWhenAllocationIsUnavailable)
{
	ExistingPrefix(8);
	const int Allocations = m_Failure.m_Allocations;
	m_Failure.m_FailAllocation = true;
	const SSdlKeyEvent aKeys[] = {{KEY_A, IInput::FLAG_PRESS}, {KEY_B, IInput::FLAG_PRESS}};
	EXPECT_NO_THROW(AppendKeys(aKeys, 2));
	EXPECT_EQ(m_vEvents.size(), 3u);
	EXPECT_EQ(m_Failure.m_Allocations, Allocations);
	EXPECT_TRUE(m_aCurrent[KEY_A]);
	EXPECT_TRUE(m_aCurrent[KEY_B]);
}
