#include "qm_video_memory.h"

#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dxgi1_4.h>
#pragma comment(lib, "dxgi.lib")
#endif

int QmQueryDedicatedVideoMemoryBudgetMb()
{
#if defined(CONF_FAMILY_WINDOWS)
	// 这个实现故意放在独立翻译单元：windows.h 的 COM IStorage 会与
	// DDNet 的 engine/storage.h 冲突，不能从 text.cpp 传递包含。
	IDXGIFactory1 *pFactory = nullptr;
	if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&pFactory))) || pFactory == nullptr)
		return -1;

	int BudgetMb = -1;
	IDXGIAdapter1 *pAdapter = nullptr;
	if(SUCCEEDED(pFactory->EnumAdapters1(0, &pAdapter)) && pAdapter != nullptr)
	{
		DXGI_ADAPTER_DESC1 Desc = {};
		if(SUCCEEDED(pAdapter->GetDesc1(&Desc)) && (Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
		{
			IDXGIAdapter3 *pAdapter3 = nullptr;
			if(SUCCEEDED(pAdapter->QueryInterface(IID_PPV_ARGS(&pAdapter3))) && pAdapter3 != nullptr)
			{
				DXGI_QUERY_VIDEO_MEMORY_INFO Info = {};
				if(SUCCEEDED(pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &Info)) && Info.Budget > 0)
					BudgetMb = static_cast<int>(Info.Budget / (1024 * 1024));
				pAdapter3->Release();
			}
			if(BudgetMb < 0 && Desc.DedicatedVideoMemory > 0)
				BudgetMb = static_cast<int>(Desc.DedicatedVideoMemory / (1024 * 1024));
		}
		pAdapter->Release();
	}
	pFactory->Release();
	return BudgetMb;
#else
	return -1;
#endif
}
