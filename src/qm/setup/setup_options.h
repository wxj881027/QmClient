#ifndef QM_SETUP_SETUP_OPTIONS_H
#define QM_SETUP_SETUP_OPTIONS_H

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace QmSetup
{
	inline bool IsCancelled(unsigned ExitCode, bool CancellationRequested)
	{
		// Inno 的代码 5 同时表示取消和文件错误后的中止，不能一律显示为用户取消。
		return ExitCode == 2 || (ExitCode == 5 && CancellationRequested);
	}

	inline std::wstring Lower(std::wstring Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](wchar_t Character) { return static_cast<wchar_t>(std::towlower(Character)); });
		return Value;
	}

	struct SOptions
	{
		bool m_UseNativeUi = true;
		std::wstring m_Directory;
		std::vector<std::wstring> m_vArguments;
	};

	inline SOptions ParseOptions(const std::vector<std::wstring> &vArguments)
	{
		SOptions Options;
		Options.m_vArguments = vArguments;
		for(const auto &Argument : vArguments)
		{
			const auto Name = Lower(Argument);
			if(Name.rfind(L"/dir=", 0) == 0)
				Options.m_Directory = Argument.substr(5);
			else if(Name != L"/noicons" && Name != L"/norestart" && Name != L"/sp-" && Name != L"/lang=zh_cn" && Name.rfind(L"/log=", 0) != 0)
				// 静默更新、帮助与高级安装参数完整交给原有内核处理。
				Options.m_UseNativeUi = false;
		}
		return Options;
	}

	inline std::wstring QuoteArgument(const std::wstring &Argument)
	{
		std::wstring Result = L"\"";
		size_t Slashes = 0;
		for(const wchar_t Character : Argument)
		{
			if(Character == L'\\')
			{
				++Slashes;
				continue;
			}
			Result.append(Character == L'"' ? Slashes * 2 + 1 : Slashes, L'\\');
			Result += Character;
			Slashes = 0;
		}
		Result.append(Slashes * 2, L'\\');
		Result += L'"';
		return Result;
	}

	inline std::wstring CommandLine(const std::vector<std::wstring> &vArguments)
	{
		std::wstring Result;
		for(const auto &Argument : vArguments)
		{
			if(!Result.empty())
				Result += L' ';
			Result += QuoteArgument(Argument);
		}
		return Result;
	}

	inline std::vector<std::wstring> NativeArguments(const SOptions &Options, const std::wstring &Directory, bool DesktopIcon)
	{
		std::vector<std::wstring> vResult;
		for(const auto &Argument : Options.m_vArguments)
		{
			if(Lower(Argument).rfind(L"/dir=", 0) != 0)
				vResult.push_back(Argument);
		}
		vResult.insert(vResult.end(), {L"/VERYSILENT", L"/SUPPRESSMSGBOXES", L"/SP-", L"/NORESTART", L"/NORESTARTAPPLICATIONS", L"/DIR=" + Directory, DesktopIcon ? L"/TASKS=desktopicon" : L"/TASKS="});
		return vResult;
	}
}

#endif // QM_SETUP_SETUP_OPTIONS_H
