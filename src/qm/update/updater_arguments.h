#ifndef QM_UPDATE_UPDATER_ARGUMENTS_H
#define QM_UPDATE_UPDATER_ARGUMENTS_H

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace QmUpdate
{
	constexpr char PERMISSION_DENIED_MARKER[] = "QM_UPDATE_PERMISSION_DENIED:";

	struct SArguments
	{
		uint32_t m_ParentPid = 0;
		std::wstring m_Package;
		std::wstring m_PackageSignature;
		std::wstring m_Manifest;
		std::wstring m_ManifestSignature;
		std::wstring m_Install;
		bool m_Elevated = false;
	};

	inline bool ParseArguments(const std::vector<std::wstring> &ArgumentsList, SArguments &Arguments)
	{
		Arguments = {};
		bool Valid = !ArgumentsList.empty();
		bool ParentPidSet = false;
		bool PackageSet = false;
		bool PackageSignatureSet = false;
		bool ManifestSet = false;
		bool ManifestSignatureSet = false;
		bool InstallSet = false;
		bool ElevatedSet = false;
		for(size_t Index = 1; Index < ArgumentsList.size() && Valid; ++Index)
		{
			const std::wstring &Name = ArgumentsList[Index];
			const auto ReadValue = [&](std::wstring &Value) {
				if(Index + 1 >= ArgumentsList.size())
					return false;
				Value = ArgumentsList[++Index];
				return !Value.empty();
			};
			if(Name == L"--parent-pid")
			{
				if(ParentPidSet)
				{
					Valid = false;
					continue;
				}
				ParentPidSet = true;
				std::wstring Value;
				if(!ReadValue(Value))
				{
					Valid = false;
					continue;
				}
				try
				{
					if(Value.find_first_not_of(L"0123456789") != std::wstring::npos)
					{
						Valid = false;
						continue;
					}
					size_t ParsedLength = 0;
					const unsigned long long Pid = std::stoull(Value, &ParsedLength);
					if(ParsedLength != Value.size() || Pid > std::numeric_limits<uint32_t>::max())
						Valid = false;
					else
						Arguments.m_ParentPid = static_cast<uint32_t>(Pid);
				}
				catch(...)
				{
					Valid = false;
				}
			}
			else if(Name == L"--package")
			{
				if(PackageSet || !ReadValue(Arguments.m_Package))
					Valid = false;
				PackageSet = true;
			}
			else if(Name == L"--package-signature")
			{
				if(PackageSignatureSet || !ReadValue(Arguments.m_PackageSignature))
					Valid = false;
				PackageSignatureSet = true;
			}
			else if(Name == L"--manifest")
			{
				if(ManifestSet || !ReadValue(Arguments.m_Manifest))
					Valid = false;
				ManifestSet = true;
			}
			else if(Name == L"--manifest-signature")
			{
				if(ManifestSignatureSet || !ReadValue(Arguments.m_ManifestSignature))
					Valid = false;
				ManifestSignatureSet = true;
			}
			else if(Name == L"--install")
			{
				if(InstallSet || !ReadValue(Arguments.m_Install))
					Valid = false;
				InstallSet = true;
			}
			else if(Name == L"--qm-elevated")
			{
				if(ElevatedSet)
					Valid = false;
				ElevatedSet = true;
				Arguments.m_Elevated = true;
			}
			else
				Valid = false;
		}
		return Valid && ParentPidSet && Arguments.m_Elevated == (Arguments.m_ParentPid == 0) && !Arguments.m_Package.empty() && !Arguments.m_PackageSignature.empty() &&
		       !Arguments.m_Manifest.empty() && !Arguments.m_ManifestSignature.empty() && !Arguments.m_Install.empty();
	}

	inline bool PathEquals(const std::filesystem::path &Left, const std::filesystem::path &Right)
	{
		const std::wstring LeftText = Left.lexically_normal().generic_wstring();
		const std::wstring RightText = Right.lexically_normal().generic_wstring();
		return LeftText.size() == RightText.size() && std::equal(LeftText.begin(), LeftText.end(), RightText.begin(), [](wchar_t LeftCharacter, wchar_t RightCharacter) {
			return std::towlower(LeftCharacter) == std::towlower(RightCharacter);
		});
	}

	inline bool ParseUpdaterSession(const std::filesystem::path &UpdaterPath, uint32_t &SessionPid)
	{
		constexpr wchar_t PREFIX[] = L"QmClient-Updater-";
		constexpr wchar_t SUFFIX[] = L".exe";
		const std::wstring Filename = UpdaterPath.filename().generic_wstring();
		if(Filename.size() <= std::size(PREFIX) - 1 + std::size(SUFFIX) - 1 || Filename.compare(0, std::size(PREFIX) - 1, PREFIX) != 0 ||
			Filename.compare(Filename.size() - (std::size(SUFFIX) - 1), std::size(SUFFIX) - 1, SUFFIX) != 0)
			return false;
		const std::wstring PidText = Filename.substr(std::size(PREFIX) - 1, Filename.size() - (std::size(PREFIX) - 1) - (std::size(SUFFIX) - 1));
		if(PidText.empty() || PidText.find_first_not_of(L"0123456789") != std::wstring::npos)
			return false;
		try
		{
			size_t ParsedLength = 0;
			const unsigned long long Pid = std::stoull(PidText, &ParsedLength);
			if(ParsedLength != PidText.size() || Pid == 0 || Pid > std::numeric_limits<uint32_t>::max())
				return false;
			SessionPid = static_cast<uint32_t>(Pid);
			return true;
		}
		catch(...)
		{
			return false;
		}
	}

	inline bool ValidateSessionPaths(const SArguments &Arguments, const std::filesystem::path &UpdaterPath)
	{
		uint32_t SessionPid = 0;
		if(!ParseUpdaterSession(UpdaterPath, SessionPid) || (!Arguments.m_Elevated && Arguments.m_ParentPid != SessionPid))
			return false;
		const std::filesystem::path Package(Arguments.m_Package);
		const std::filesystem::path PackageSignature(Arguments.m_PackageSignature);
		const std::filesystem::path Manifest(Arguments.m_Manifest);
		const std::filesystem::path ManifestSignature(Arguments.m_ManifestSignature);
		const std::filesystem::path Install(Arguments.m_Install);
		if(!UpdaterPath.is_absolute() || !Package.is_absolute() || !PackageSignature.is_absolute() || !Manifest.is_absolute() || !ManifestSignature.is_absolute() || !Install.is_absolute())
			return false;
		const std::wstring Session = std::to_wstring(SessionPid);
		if(Package.filename() != L"QmClient-windows.zip." + Session + L".tmp" ||
			PackageSignature.filename() != L"QmClient-windows.zip.sig." + Session + L".tmp" ||
			Manifest.filename() != L"QmClient-windows-update.json." + Session + L".tmp" ||
			ManifestSignature.filename() != L"QmClient-windows-update.json.sig." + Session + L".tmp")
			return false;
		const std::filesystem::path AssetDirectory = Package.parent_path();
		return PathEquals(PackageSignature.parent_path(), AssetDirectory) && PathEquals(Manifest.parent_path(), AssetDirectory) &&
		       PathEquals(ManifestSignature.parent_path(), AssetDirectory) && PathEquals(UpdaterPath.parent_path(), AssetDirectory / L"qmclient");
	}

	inline bool IsPermissionError(const std::string &Error)
	{
		return Error.rfind(PERMISSION_DENIED_MARKER, 0) == 0;
	}
}

#endif
