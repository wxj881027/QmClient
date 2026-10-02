#include "player_volume.h"

#include "media_volume_logic.h"

#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS) && defined(_MSC_VER)
#pragma push_macro("NOGDI")
#undef NOGDI
#include <windows.h>
#include <wingdi.h>
#include <appmodel.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <winrt/base.h>
#pragma pop_macro("NOGDI")

#include <chrono>
#include <vector>

struct CQmPlayerVolume::SImpl
{
	struct SSession
	{
		std::wstring m_Id;
		winrt::com_ptr<ISimpleAudioVolume> m_pVolume;
	};
	std::vector<SSession> m_vSessions;
	std::string m_Source;
	uint64_t m_Generation = 1;
	std::chrono::steady_clock::time_point m_LastScan{};

	static bool MatchesProcess(DWORD ProcessId, const std::string &Source)
	{
		if(ProcessId == 0 || ProcessId == GetCurrentProcessId())
			return false;
		winrt::handle Process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ProcessId));
		if(!Process)
			return false;
		std::string AppId;
		UINT32 Length = 0;
		if(GetApplicationUserModelId(Process.get(), &Length, nullptr) == ERROR_INSUFFICIENT_BUFFER && Length > 0)
		{
			std::wstring Buffer(Length, L'\0');
			if(GetApplicationUserModelId(Process.get(), &Length, Buffer.data()) == ERROR_SUCCESS)
				AppId = winrt::to_string(std::wstring_view(Buffer.c_str()));
		}
		wchar_t aPath[32768];
		DWORD PathLength = static_cast<DWORD>(std::size(aPath));
		const std::string Executable = QueryFullProcessImageNameW(Process.get(), 0, aPath, &PathLength) ? winrt::to_string(std::wstring_view(aPath, PathLength)) : "";
		return QmMediaVolume::MatchesPlayer(Source, AppId, Executable);
	}

	void Scan()
	{
		std::vector<SSession> vSessions;
		winrt::com_ptr<IMMDeviceEnumerator> pEnumerator;
		if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), pEnumerator.put_void())))
		{
			winrt::com_ptr<IMMDeviceCollection> pDevices;
			if(SUCCEEDED(pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, pDevices.put())))
			{
				UINT DeviceCount = 0;
				pDevices->GetCount(&DeviceCount);
				for(UINT DeviceIndex = 0; DeviceIndex < DeviceCount; ++DeviceIndex)
				{
					winrt::com_ptr<IMMDevice> pDevice;
					winrt::com_ptr<IAudioSessionManager2> pManager;
					winrt::com_ptr<IAudioSessionEnumerator> pSessions;
					if(FAILED(pDevices->Item(DeviceIndex, pDevice.put())) ||
						FAILED(pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, pManager.put_void())) ||
						FAILED(pManager->GetSessionEnumerator(pSessions.put())))
						continue;
					int Count = 0;
					pSessions->GetCount(&Count);
					for(int Index = 0; Index < Count; ++Index)
					{
						winrt::com_ptr<IAudioSessionControl> pControl;
						if(FAILED(pSessions->GetSession(Index, pControl.put())))
							continue;
						auto pControl2 = pControl.try_as<IAudioSessionControl2>();
						AudioSessionState State;
						DWORD ProcessId = 0;
						if(!pControl2 || pControl2->IsSystemSoundsSession() == S_OK ||
							FAILED(pControl->GetState(&State)) || State == AudioSessionStateExpired ||
							pControl2->GetProcessId(&ProcessId) != S_OK || !MatchesProcess(ProcessId, m_Source))
							continue;
						auto pVolume = pControl.try_as<ISimpleAudioVolume>();
						LPWSTR pId = nullptr;
						if(pVolume && SUCCEEDED(pControl2->GetSessionInstanceIdentifier(&pId)))
						{
							vSessions.push_back({pId, std::move(pVolume)});
							CoTaskMemFree(pId);
						}
					}
				}
			}
		}
		const auto ById = [](const SSession &Left, const SSession &Right) { return Left.m_Id < Right.m_Id; };
		std::sort(vSessions.begin(), vSessions.end(), ById);
		if(vSessions.size() != m_vSessions.size() || !std::equal(vSessions.begin(), vSessions.end(), m_vSessions.begin(), [](const SSession &Left, const SSession &Right) { return Left.m_Id == Right.m_Id; }))
			++m_Generation;
		m_vSessions = std::move(vSessions);
	}
};

CQmPlayerVolume::CQmPlayerVolume() : m_pImpl(std::make_unique<SImpl>()) {}
CQmPlayerVolume::~CQmPlayerVolume() = default;

void CQmPlayerVolume::Reset()
{
	m_pImpl->m_vSessions.clear();
	m_pImpl->m_Source.clear();
	++m_pImpl->m_Generation;
	m_pImpl->m_LastScan = {};
}

CQmPlayerVolume::SSnapshot CQmPlayerVolume::Refresh(const std::string &SourceAppId)
{
	if(m_pImpl->m_Source != SourceAppId)
	{
		Reset();
		m_pImpl->m_Source = SourceAppId;
	}
	if(SourceAppId.empty())
		return {};
	const auto Now = std::chrono::steady_clock::now();
	if(Now - m_pImpl->m_LastScan >= std::chrono::seconds(1))
	{
		m_pImpl->Scan();
		m_pImpl->m_LastScan = Now;
	}
	SSnapshot Snapshot;
	Snapshot.m_Generation = m_pImpl->m_Generation;
	for(const auto &Session : m_pImpl->m_vSessions)
	{
		float Level;
		BOOL Muted;
		if(FAILED(Session.m_pVolume->GetMasterVolume(&Level)) || FAILED(Session.m_pVolume->GetMute(&Muted)))
		{
			Reset();
			return {};
		}
		// 同一播放器可有多个输出会话，显示最大可听音量，拖动时统一设置。
		Snapshot.m_Level = std::max(Snapshot.m_Level, Muted ? 0.0f : Level);
		Snapshot.m_Muted = !Snapshot.m_Available ? Muted != FALSE : Snapshot.m_Muted && Muted != FALSE;
		Snapshot.m_Available = true;
	}
	return Snapshot;
}

bool CQmPlayerVolume::SetVolume(uint64_t Generation, float Level)
{
	if(Generation != m_pImpl->m_Generation || m_pImpl->m_vSessions.empty() || !std::isfinite(Level))
		return false;
	const float Volume = std::clamp(Level, 0.0f, 1.0f);
	for(const auto &Session : m_pImpl->m_vSessions)
	{
		if(FAILED(Session.m_pVolume->SetMasterVolume(Volume, nullptr)) || FAILED(Session.m_pVolume->SetMute(Volume == 0.0f, nullptr)))
		{
			Reset();
			return false;
		}
	}
	return true;
}
#else
struct CQmPlayerVolume::SImpl {};
CQmPlayerVolume::CQmPlayerVolume() = default;
CQmPlayerVolume::~CQmPlayerVolume() = default;
CQmPlayerVolume::SSnapshot CQmPlayerVolume::Refresh(const std::string &) { return {}; }
bool CQmPlayerVolume::SetVolume(uint64_t, float) { return false; }
void CQmPlayerVolume::Reset() {}
#endif
