#include "update_proxy.h"

#include <base/detect.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>

#include <windows.h>

#include <winhttp.h>
#endif

namespace qm_update
{
	namespace
	{
		std::string Lower(std::string_view Value)
		{
			std::string Result(Value);
			for(auto &Character : Result)
				Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
			return Result;
		}
		std::string_view Trim(std::string_view Value)
		{
			const auto Start = Value.find_first_not_of(" \t");
			if(Start == std::string_view::npos)
				return {};
			return Value.substr(Start, Value.find_last_not_of(" \t") - Start + 1);
		}
		bool Glob(std::string_view Host, std::string_view Pattern)
		{
			size_t HostIndex = 0, PatternIndex = 0, Star = std::string_view::npos, Retry = 0;
			while(HostIndex < Host.size())
			{
				if(PatternIndex < Pattern.size() && (Pattern[PatternIndex] == '?' || Pattern[PatternIndex] == Host[HostIndex]))
				{
					++HostIndex;
					++PatternIndex;
				}
				else if(PatternIndex < Pattern.size() && Pattern[PatternIndex] == '*')
				{
					Star = PatternIndex++;
					Retry = HostIndex;
				}
				else if(Star != std::string_view::npos)
				{
					PatternIndex = Star + 1;
					HostIndex = ++Retry;
				}
				else
					return false;
			}
			while(PatternIndex < Pattern.size() && Pattern[PatternIndex] == '*')
				++PatternIndex;
			return PatternIndex == Pattern.size();
		}
	}

	std::string SelectSystemProxy(std::string_view ProxyList)
	{
		std::string Generic, Https, Socks;
		while(!ProxyList.empty())
		{
			const auto End = ProxyList.find(';');
			const auto Entry = Trim(ProxyList.substr(0, End));
			const auto Equals = Entry.find('=');
			if(Equals == std::string_view::npos)
				Generic = std::string(Entry);
			else
			{
				const auto Scheme = Lower(Trim(Entry.substr(0, Equals)));
				if(Scheme == "https")
					Https = std::string(Trim(Entry.substr(Equals + 1)));
				if(Scheme == "socks")
					Socks = std::string(Trim(Entry.substr(Equals + 1)));
			}
			if(End == std::string_view::npos)
				break;
			ProxyList.remove_prefix(End + 1);
		}
		std::string Result = !Https.empty() ? Https : !Generic.empty() ? Generic :
										 Socks;
		if(Result.empty() || Result.find_first_of("@\r\n\t #?") != std::string::npos)
			return {};
		if(Result.find("://") == std::string::npos)
			Result = (Https.empty() && Generic.empty() ? "socks5h://" : "http://") + Result;
		if(Result.compare(0, 7, "http://") != 0 && Result.compare(0, 8, "https://") != 0 && Result.compare(0, 10, "socks5h://") != 0 && Result.compare(0, 9, "socks5://") != 0)
			return {};
		return Result.size() < 256 ? Result : std::string();
	}

	bool ProxyBypassed(std::string_view Host, std::string_view BypassList)
	{
		const auto NormalHost = Lower(Host);
		while(!BypassList.empty())
		{
			const auto End = BypassList.find(';');
			const auto Pattern = Lower(Trim(BypassList.substr(0, End)));
			if((Pattern == "<local>" && NormalHost.find('.') == std::string::npos) || (!Pattern.empty() && Glob(NormalHost, Pattern)))
				return true;
			if(End == std::string_view::npos)
				break;
			BypassList.remove_prefix(End + 1);
		}
		return false;
	}

	bool HasEnvironmentProxy()
	{
		for(const char *pName : {"https_proxy", "HTTPS_PROXY", "all_proxy", "ALL_PROXY"})
		{
			const char *pValue = std::getenv(pName);
			if(pValue && *pValue)
				return true;
		}
		return false;
	}

	void CSystemProxyJob::Run()
	{
#if defined(CONF_FAMILY_WINDOWS)
		WINHTTP_CURRENT_USER_IE_PROXY_CONFIG Config{};
		if(!WinHttpGetIEProxyConfigForCurrentUser(&Config))
			return;
		const auto Free = [](wchar_t *pValue) { if(pValue) GlobalFree(pValue); };
		const auto Read = [](const wchar_t *pText) { return pText ? windows_wide_to_utf8(pText).value_or("") : std::string(); };
		const auto HostStart = m_Url.find("://") + 3;
		const auto Host = m_Url.substr(HostStart, m_Url.find('/', HostStart) - HostStart);
		m_Proxy = ProxyBypassed(Host, Read(Config.lpszProxyBypass)) ? "" : SelectSystemProxy(Read(Config.lpszProxy));
		m_HasDecision = Config.lpszProxy != nullptr;
		if(Config.lpszAutoConfigUrl || Config.fAutoDetect)
		{
			HINTERNET Session = WinHttpOpen(L"QmClient update", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
			if(Session)
			{
				WinHttpSetTimeouts(Session, 3000, 3000, 3000, 3000);
				WINHTTP_AUTOPROXY_OPTIONS Options{};
				Options.fAutoLogonIfChallenged = FALSE;
				if(Config.lpszAutoConfigUrl)
				{
					Options.dwFlags |= WINHTTP_AUTOPROXY_CONFIG_URL;
					Options.lpszAutoConfigUrl = Config.lpszAutoConfigUrl;
				}
				if(Config.fAutoDetect)
				{
					Options.dwFlags |= WINHTTP_AUTOPROXY_AUTO_DETECT;
					Options.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DNS_A;
				}
				WINHTTP_PROXY_INFO Info{};
				const auto WideUrl = windows_utf8_to_wide(m_Url.c_str());
				if(WinHttpGetProxyForUrl(Session, WideUrl.c_str(), &Options, &Info))
				{
					m_HasDecision = true;
					m_Proxy = Info.dwAccessType == WINHTTP_ACCESS_TYPE_NO_PROXY || ProxyBypassed(Host, Read(Info.lpszProxyBypass)) ? "" : SelectSystemProxy(Read(Info.lpszProxy));
				}
				Free(Info.lpszProxy);
				Free(Info.lpszProxyBypass);
				WinHttpCloseHandle(Session);
			}
		}
		Free(Config.lpszProxy);
		Free(Config.lpszProxyBypass);
		Free(Config.lpszAutoConfigUrl);
#endif
	}
}
