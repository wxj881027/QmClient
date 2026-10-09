#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DUMMY_MINIVIEW_RENDER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DUMMY_MINIVIEW_RENDER_H

#include <algorithm>
#include <cstdint>

struct SQmDummyMiniViewTargetSize
{
	int m_W = 0;
	int m_H = 0;
};

// 容量按块增长，缩小时复用；窗口尺寸变化不必逐帧等待 GPU 销毁旧目标。
inline SQmDummyMiniViewTargetSize ResolveQmDummyMiniViewTargetSize(int RequiredW, int RequiredH, int PreviousW, int PreviousH, int ScreenW, int ScreenH)
{
	if(RequiredW <= 0 || RequiredH <= 0 || ScreenW <= 0 || ScreenH <= 0)
		return {};
	RequiredW = std::min(RequiredW, ScreenW);
	RequiredH = std::min(RequiredH, ScreenH);
	if(PreviousW >= RequiredW && PreviousH >= RequiredH)
		return {PreviousW, PreviousH};
	const auto Grow = [](int Required, int Previous, int Limit) {
		constexpr int Block = 64;
		const int Rounded = (int)std::min<int64_t>(((int64_t)Required + Block - 1) / Block * Block, Limit);
		return std::max(Previous, Rounded);
	};
	return {Grow(RequiredW, PreviousW, ScreenW), Grow(RequiredH, PreviousH, ScreenH)};
}

#endif
