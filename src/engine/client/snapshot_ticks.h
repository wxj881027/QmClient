#ifndef ENGINE_CLIENT_SNAPSHOT_TICKS_H
#define ENGINE_CLIENT_SNAPSHOT_TICKS_H

#include <engine/shared/packer.h>

#include <cstdint>
#include <limits>

namespace client_snapshot
{
	struct STicks
	{
		int m_GameTick;
		int m_DeltaTick;
	};

	inline bool UnpackTicks(CUnpacker &Unpacker, STicks &Output) noexcept
	{
		const int GameTick = Unpacker.GetInt();
		const int Distance = Unpacker.GetInt();
		if(Unpacker.Error())
			return false;
		const int64_t DeltaTick = static_cast<int64_t>(GameTick) - Distance;
		if(DeltaTick < std::numeric_limits<int>::min() || DeltaTick > std::numeric_limits<int>::max())
			return false;
		// 完整解析并确认快照键可表示后再发布。
		Output = {GameTick, static_cast<int>(DeltaTick)};
		return true;
	}
}

#endif
