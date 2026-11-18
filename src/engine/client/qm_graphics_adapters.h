#ifndef ENGINE_CLIENT_QM_GRAPHICS_ADAPTERS_H
#define ENGINE_CLIENT_QM_GRAPHICS_ADAPTERS_H

#include <base/detect.h>
#include <base/str.h>

#include <engine/graphics.h>

#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <devguid.h>
#include <setupapi.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "uuid.lib")
#endif

// 虚拟显示驱动可能复制实体显卡名称且不设置 DXGI 软件标记，不能只按名称或 Flags 计数。
inline bool QmIsSystemGraphicsHardwareAdapter(std::wstring_view DevicePath)
{
	if(DevicePath.substr(0, 4) == L"\\\\?\\")
		DevicePath.remove_prefix(4);
	constexpr std::wstring_view VirtualDisplayPrefix = L"ROOT\\DISPLAY\\";
	if(DevicePath.size() < VirtualDisplayPrefix.size())
		return true;
	for(size_t i = 0; i < VirtualDisplayPrefix.size(); ++i)
	{
		wchar_t Character = DevicePath[i];
		if(Character >= L'a' && Character <= L'z')
			Character -= L'a' - L'A';
		if(Character == L'#')
			Character = L'\\';
		if(Character != VirtualDisplayPrefix[i])
			return true;
	}
	return false;
}

#if defined(CONF_FAMILY_WINDOWS)
template<size_t N>
std::string QmDecodeSystemGraphicsAdapterName(const wchar_t (&aDescription)[N], size_t DescriptionBytes)
{
	if(DescriptionBytes > sizeof(aDescription) || DescriptionBytes % sizeof(wchar_t) != 0)
		return {};

	// 注册表字符串可能缺少终止符，转换时只使用属性返回的字节。
	const std::wstring_view Property(aDescription, DescriptionBytes / sizeof(wchar_t));
	const std::wstring_view Description = Property.substr(0, Property.find(L'\0'));
	if(Description.empty())
		return {};

	char aName[sizeof(aDescription) * 2];
	const int NameBytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Description.data(), static_cast<int>(Description.size()), aName, sizeof(aName), nullptr, nullptr);
	if(NameBytes == 0)
		return {};
	return std::string(aName, NameBytes);
}
#endif

inline std::vector<std::string> QmReadSystemGraphicsAdapters()
{
	std::vector<std::string> vNames;
#if defined(CONF_FAMILY_WINDOWS)
	// PnP 清单提供真实设备实例身份，避免虚拟显示驱动伪装成同名 DXGI GPU。
	const HDEVINFO Devices = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
	if(Devices == INVALID_HANDLE_VALUE)
		return vNames;
	const auto DestroyDevices = [](void *pDevices) { SetupDiDestroyDeviceInfoList(pDevices); };
	const std::unique_ptr<void, decltype(DestroyDevices)> DeviceOwner(Devices, DestroyDevices);
	for(DWORD Index = 0;; ++Index)
	{
		SP_DEVINFO_DATA Device{};
		Device.cbSize = sizeof(Device);
		if(!SetupDiEnumDeviceInfo(Devices, Index, &Device))
			break;
		wchar_t aInstanceId[512] = {};
		if(!SetupDiGetDeviceInstanceIdW(Devices, &Device, aInstanceId, std::size(aInstanceId), nullptr))
			continue;
		if(!QmIsSystemGraphicsHardwareAdapter(aInstanceId))
			continue;
		wchar_t aDescription[512] = {};
		DWORD DescriptionBytes = 0;
		if(!SetupDiGetDeviceRegistryPropertyW(Devices, &Device, SPDRP_FRIENDLYNAME, nullptr, reinterpret_cast<BYTE *>(aDescription), sizeof(aDescription), &DescriptionBytes) &&
			!SetupDiGetDeviceRegistryPropertyW(Devices, &Device, SPDRP_DEVICEDESC, nullptr, reinterpret_cast<BYTE *>(aDescription), sizeof(aDescription), &DescriptionBytes))
			continue;
		std::string Name = QmDecodeSystemGraphicsAdapterName(aDescription, DescriptionBytes);
		if(!Name.empty())
			vNames.push_back(std::move(Name));
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
	try
	{
		for(const std::string &Name : ReadAdapters())
		{
			STWGraphicGpu::STWGraphicGpuItem Item{};
			str_copy(Item.m_aName, Name.c_str());
			Item.m_GpuType = STWGraphicGpu::GRAPHICS_GPU_TYPE_INVALID;
			List.m_vGpus.push_back(Item);
		}
	}
	catch(const std::bad_alloc &)
	{
		List.m_vGpus.clear();
	}
}

#endif
