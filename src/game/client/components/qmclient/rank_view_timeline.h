#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_VIEW_TIMELINE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_VIEW_TIMELINE_H

#include <base/system.h>

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace qmclient::rank_ghost
{
	inline constexpr char VIEW_TIMELINE_MAGIC[8] = {'Q', 'M', 'G', 'H', 'E', 'V', 'T', '2'};
	inline constexpr int64_t MAX_TIMELINE_BYTES = 128 * 1024 * 1024;

	struct SViewEvent
	{
		int m_RelTick;
		int m_Type;
		int m_aData[3];
	};
	struct SViewSwitchState
	{
		int m_RelTick;
		int m_HighestSwitchNumber;
		unsigned m_aStatus[8];
	};
	enum class EViewMessageType
	{
		RACE_FINISH,
		RACE_TIME,
		SOUND_GLOBAL,
		MAP_SOUND_GLOBAL,
	};
	struct SViewMessage
	{
		int m_RelTick;
		EViewMessageType m_Type;
		int m_aData[5];
	};
	struct SViewTimeline
	{
		std::vector<SViewEvent> m_vEvents;
		std::vector<SViewSwitchState> m_vSwitchStates;
		std::vector<SViewMessage> m_vMessages;
	};

	inline bool ValidViewEvent(const SViewEvent &Event)
	{
		switch(Event.m_Type)
		{
		case NETEVENTTYPE_HAMMERHIT:
		case NETEVENTTYPE_EXPLOSION:
		case NETEVENTTYPE_BIRTHDAY:
		case NETEVENTTYPE_FINISH:
		case NETEVENTTYPE_SPAWN:
		case NETEVENTTYPE_DAMAGEIND:
			return true;
		case NETEVENTTYPE_DEATH:
			return Event.m_aData[2] >= 0 && Event.m_aData[2] < MAX_CLIENTS;
		case NETEVENTTYPE_SOUNDWORLD:
		case NETEVENTTYPE_MAPSOUNDWORLD:
			return Event.m_aData[2] >= 0;
		default:
			return false;
		}
	}

	// 借用句柄，保持现有分段落盘布局；所有数量校验必须先于 reserve。
	inline bool ReadViewTimeline(IOHANDLE File, SViewTimeline &Output)
	{
		Output = {};
		if(File == nullptr)
			return false;
		int64_t Remaining = io_length(File) - io_tell(File);
		if(Remaining < 0 || Remaining > MAX_TIMELINE_BYTES)
			return false;
		const auto Read = [&](void *pData, size_t Size) {
			if(Size > (uint64_t)Remaining || io_read(File, pData, (unsigned)Size) != Size)
				return false;
			Remaining -= Size;
			return true;
		};
		char aMagic[8];
		if(!Read(aMagic, sizeof(aMagic)) || mem_comp(aMagic, VIEW_TIMELINE_MAGIC, sizeof(aMagic)) != 0)
			return false;
		SViewTimeline Candidate;
		size_t MemoryRemaining = MAX_TIMELINE_BYTES;
		const auto ReadSection = [&](auto &vRecords, size_t RecordBytes, auto ReadRecord) {
			uint32_t Count = 0;
			using TRecord = typename std::decay_t<decltype(vRecords)>::value_type;
			if(!Read(&Count, sizeof(Count)) || Count > (uint64_t)Remaining / RecordBytes || Count > MemoryRemaining / sizeof(TRecord))
				return false;
			MemoryRemaining -= (size_t)Count * sizeof(TRecord);
			vRecords.reserve(Count);
			int PreviousTick = -1;
			for(uint32_t Index = 0; Index < Count; ++Index)
			{
				TRecord Record{};
				if(!ReadRecord(Record) || Record.m_RelTick < 0 || Record.m_RelTick < PreviousTick)
					return false;
				PreviousTick = Record.m_RelTick;
				vRecords.push_back(Record);
			}
			return true;
		};
		try
		{
			if(!ReadSection(Candidate.m_vEvents, 5 * sizeof(int), [&](SViewEvent &Event) {
				   return Read(&Event.m_RelTick, sizeof(Event.m_RelTick)) && Read(&Event.m_Type, sizeof(Event.m_Type)) &&
					  Read(Event.m_aData, sizeof(Event.m_aData)) && ValidViewEvent(Event);
			   }) || !ReadSection(Candidate.m_vSwitchStates, 2 * sizeof(int) + 8 * sizeof(unsigned), [&](SViewSwitchState &State) {
				   return Read(&State.m_RelTick, sizeof(State.m_RelTick)) && Read(&State.m_HighestSwitchNumber, sizeof(State.m_HighestSwitchNumber)) &&
					  Read(State.m_aStatus, sizeof(State.m_aStatus)) && State.m_HighestSwitchNumber >= 0 && State.m_HighestSwitchNumber < 256;
			   }) || !ReadSection(Candidate.m_vMessages, 7 * sizeof(int), [&](SViewMessage &Message) {
				   int Type = 0;
				   if(!Read(&Message.m_RelTick, sizeof(Message.m_RelTick)) || !Read(&Type, sizeof(Type)) ||
					   !Read(Message.m_aData, sizeof(Message.m_aData)) || Type < 0 || Type > (int)EViewMessageType::MAP_SOUND_GLOBAL)
					   return false;
				   Message.m_Type = (EViewMessageType)Type;
				   if(Message.m_Type == EViewMessageType::RACE_FINISH)
					   return Message.m_aData[0] >= 0 && Message.m_aData[0] < MAX_CLIENTS;
				   return Message.m_Type == EViewMessageType::RACE_TIME || Message.m_aData[0] >= 0;
			   }))
				return false;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		if(Remaining != 0)
			return false;
		Output = std::move(Candidate);
		return true;
	}
}

#endif
