#ifndef ENGINE_CLIENT_QM_GRAPHICS_ADAPTERS_H
#define ENGINE_CLIENT_QM_GRAPHICS_ADAPTERS_H

#include <base/detect.h>
#include <base/str.h>

#include <engine/graphics.h>

#include <memory>
#include <string>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

// Windows COM 的 IStorage 名称不能与引擎存储接口冲突。
#define IStorage IStorageCOM
#include <dxgi1_2.h>
#undef IStorage

#pragma comment(lib, "dxgi.lib")
#endif

inline std::vector<std::string> QmReadSystemGraphicsAdapters()
{
	std::vector<std::string> vNames;
#if defined(CONF_FAMILY_WINDOWS)
	IDXGIFactory1 *pFactory = nullptr;
	const HRESULT FactoryResult = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&pFactory));
	const auto ReleaseFactory = [](IDXGIFactory1 *pValue) { pValue->Release(); };
	const std::unique_ptr<IDXGIFactory1, decltype(ReleaseFactory)> Factory(pFactory, ReleaseFactory);
	if(FAILED(FactoryResult) || !Factory)
		return vNames;
	for(UINT Index = 0;; ++Index)
	{
		IDXGIAdapter1 *pAdapter = nullptr;
		const HRESULT AdapterResult = Factory->EnumAdapters1(Index, &pAdapter);
		const auto ReleaseAdapter = [](IDXGIAdapter1 *pValue) { pValue->Release(); };
		const std::unique_ptr<IDXGIAdapter1, decltype(ReleaseAdapter)> Adapter(pAdapter, ReleaseAdapter);
		if(FAILED(AdapterResult) || !Adapter)
			break;
		DXGI_ADAPTER_DESC1 Description{};
		if(FAILED(Adapter->GetDesc1(&Description)) || (Description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
			continue;
		char aName[sizeof(Description.Description) * 2];
		if(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Description.Description, -1, aName, sizeof(aName), nullptr, nullptr) > 1)
			vNames.emplace_back(aName);
	}
#endif
	return vNames;
}

// 系统硬件清单只提供信息；当前渲染器单独显示，不据名称猜测正在使用哪张卡。
template<typename TReadAdapters>
void QmPopulateOpenGLGpuList(STWGraphicGpu &List, const char *pRenderer, TReadAdapters ReadAdapters)
{
	List.m_vGpus.clear();
	List.m_CanSelect = false;
	List.m_AutoGpu = {};
	List.m_AutoGpu.m_GpuType = STWGraphicGpu::GRAPHICS_GPU_TYPE_INVALID;
	str_copy(List.m_AutoGpu.m_aName, pRenderer ? pRenderer : "");
	for(const std::string &Name : ReadAdapters())
	{
		STWGraphicGpu::STWGraphicGpuItem Item{};
		str_copy(Item.m_aName, Name.c_str());
		Item.m_GpuType = STWGraphicGpu::GRAPHICS_GPU_TYPE_INVALID;
		List.m_vGpus.push_back(Item);
	}
}

#endif
