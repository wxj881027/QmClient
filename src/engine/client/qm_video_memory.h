#ifndef ENGINE_CLIENT_QM_VIDEO_MEMORY_H
#define ENGINE_CLIENT_QM_VIDEO_MEMORY_H

#include <cstddef>

// 字形图集是常驻资源。8192² 两张 R8 纹理约占 128 MiB 显存，另需同等
// CPU 镜像；只在独显预算足够时从 8192 起步，低显存/核显保持 4096。
inline constexpr size_t QmChooseInitialGlyphAtlasDimension(int VramBudgetMb)
{
	return VramBudgetMb >= 2048 ? 8192 : 4096;
}

// 返回 DXGI 适配器 0 的 LOCAL 显存预算（MiB）。非 Windows 或查询失败返回 -1。
// 这里只做一次轻量查询，不依赖游戏内监控采样，避免图集初始化晚于首帧。
int QmQueryDedicatedVideoMemoryBudgetMb();

#endif // ENGINE_CLIENT_QM_VIDEO_MEMORY_H
