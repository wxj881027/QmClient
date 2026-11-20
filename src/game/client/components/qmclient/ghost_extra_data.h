#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_EXTRA_DATA_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_EXTRA_DATA_H

#include <algorithm>

// 幽灵旧格式没有强弱钩字段。m_Direction 原始值只有 -1/0/1，高字节长期为 0，
// 因此把 StrongWeakId 打包进高字节，低字节继续保存方向。旧文件的高字节为 0，
// 读取时直接按原始方向处理，不需要升级文件格式。
inline int QmEncodeGhostDirection(int Direction, int StrongWeakId)
{
	const int PackedDirection = std::clamp(Direction, -1, 1) + 1;
	const int PackedStrongWeak = std::clamp(StrongWeakId, 0, 254) + 1;
	return (PackedStrongWeak << 8) | PackedDirection;
}

inline int QmDecodeGhostDirection(int Packed)
{
	if(Packed == -1)
		return -1;
	const int Low = Packed & 0xFF;
	const int High = (Packed >> 8) & 0xFF;
	if(High == 0)
		return Packed;
	return Low >= 0 && Low <= 2 ? Low - 1 : 0;
}

// -1 = 旧文件没有强弱钩数据。
inline int QmDecodeGhostStrongWeak(int Packed)
{
	const int Low = Packed & 0xFF;
	const int High = (Packed >> 8) & 0xFF;
	if(High == 0 || Low < 0 || Low > 2)
		return -1;
	return High - 1;
}

#endif
