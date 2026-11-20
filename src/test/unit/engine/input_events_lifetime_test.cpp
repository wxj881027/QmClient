#include <engine/client/input_sdl3.h>

#include <gtest/gtest.h>

#include <new>

namespace
{
	IInput::CEvent MakeEvent(int Key, const char *pText, uint32_t Count)
	{
		IInput::CEvent Event{};
		Event.m_Key = Key;
		Event.m_Flags = IInput::FLAG_PRESS;
		Event.m_InputCount = Count;
		str_copy(Event.m_aText, pText);
		return Event;
	}
}

TEST(SdlInputConsumption, CurrentGenerationKeepsOriginalOrderAndMetadata)
{
	uint32_t Count = 7;
	std::vector<IInput::CEvent> vEvents{MakeEvent(KEY_A, "first", 7), MakeEvent(KEY_B, "old", 6), MakeEvent(KEY_C, "last", 7)};
	std::vector<int> vKeys;
	ConsumeSdlInputEvents(vEvents, Count, [&](const auto &Event) {
		vKeys.push_back(Event.m_Key);
		EXPECT_EQ(Event.m_InputCount, 7u);
		EXPECT_EQ(Event.m_Flags, IInput::FLAG_PRESS);
	});
	EXPECT_EQ(vKeys, (std::vector<int>{KEY_A, KEY_C}));
}

TEST(SdlInputConsumption, ClearDuringCallbackKeepsTheCurrentValueAndDiscardsTheRest)
{
	uint32_t Count = 7;
	std::vector<IInput::CEvent> vEvents{MakeEvent(KEY_A, "opening chat", 7), MakeEvent(KEY_B, "discarded", 7)};
	int Calls = 0;
	ConsumeSdlInputEvents(vEvents, Count, [&](const auto &Event) {
		++Calls;
		vEvents.clear();
		vEvents.shrink_to_fit();
		EXPECT_EQ(Event.m_Key, KEY_A);
		EXPECT_STREQ(Event.m_aText, "opening chat");
	});
	EXPECT_EQ(Calls, 1);
}

TEST(SdlInputConsumption, CallbackGrowthDoesNotInvalidateTheCurrentEventOrConsumeNewTail)
{
	uint32_t Count = 7;
	std::vector<IInput::CEvent> vEvents{MakeEvent(KEY_A, "first", 7), MakeEvent(KEY_B, "second", 7)};
	std::vector<int> vKeys;
	ConsumeSdlInputEvents(vEvents, Count, [&](const auto &Event) {
		vKeys.push_back(Event.m_Key);
		if(Event.m_Key == KEY_A)
		{
			vEvents.reserve(vEvents.capacity() + 10);
			vEvents.push_back(MakeEvent(KEY_C, "new tail", 7));
			EXPECT_STREQ(Event.m_aText, "first");
		}
	});
	EXPECT_EQ(vKeys, (std::vector<int>{KEY_A, KEY_B}));
}

TEST(SdlInputConsumption, NewUpdateGenerationStopsTheOldConsumer)
{
	uint32_t Count = 7;
	std::vector<IInput::CEvent> vEvents{MakeEvent(KEY_A, "old", 7), MakeEvent(KEY_B, "old tail", 7)};
	int Calls = 0;
	ConsumeSdlInputEvents(vEvents, Count, [&](const auto &Event) {
		++Calls;
		Count = 8;
		vEvents = {MakeEvent(KEY_C, "new generation", 8)};
		EXPECT_STREQ(Event.m_aText, "old");
	});
	EXPECT_EQ(Calls, 1);
}

TEST(SdlInputConsumption, ConsumerFailurePropagatesWithoutReplayingAnEvent)
{
	uint32_t Count = 7;
	std::vector<IInput::CEvent> vEvents{MakeEvent(KEY_A, "first", 7), MakeEvent(KEY_B, "second", 7)};
	int Calls = 0;
	EXPECT_THROW(ConsumeSdlInputEvents(vEvents, Count, [&](const auto &) {
		++Calls;
		throw std::bad_alloc();
	}),
		std::bad_alloc);
	EXPECT_EQ(Calls, 1);
	EXPECT_EQ(vEvents.size(), 2u);
}

TEST(SdlInputConsumption, EmptyQueueDoesNotInvokeTheConsumer)
{
	uint32_t Count = 7;
	std::vector<IInput::CEvent> vEvents;
	int Calls = 0;
	ConsumeSdlInputEvents(vEvents, Count, [&](const auto &) { ++Calls; });
	EXPECT_EQ(Calls, 0);
}
