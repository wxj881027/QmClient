#include "qm_netease_cdp.h"

#include <engine/external/json-parser/json.h>

#include <charconv>
#include <cwctype>
#include <limits>
#include <memory>
#include <utility>

namespace QmNeteaseCdp
{
	namespace
	{
		const json_value *ObjectField(const json_value *pObject, const char *pName)
		{
			if(pObject == nullptr || pObject->type != json_object || pName == nullptr)
				return nullptr;
			for(unsigned int Index = 0; Index < pObject->u.object.length; ++Index)
			{
				const auto &Entry = pObject->u.object.values[Index];
				if(Entry.name != nullptr && std::string_view(Entry.name, Entry.name_length) == pName)
					return Entry.value;
			}
			return nullptr;
		}

		std::string JsonString(const json_value *pValue)
		{
			if(pValue == nullptr || pValue->type != json_string || pValue->u.string.ptr == nullptr)
				return {};
			return std::string(pValue->u.string.ptr, pValue->u.string.length);
		}

		bool ParsePort(std::string_view Value, uint16_t *pPort)
		{
			if(pPort == nullptr || Value.empty())
				return false;
			unsigned int Port = 0;
			const auto Result = std::from_chars(Value.data(), Value.data() + Value.size(), Port);
			if(Result.ec != std::errc{} || Result.ptr != Value.data() + Value.size() || Port == 0 || Port > 65535)
				return false;
			*pPort = (uint16_t)Port;
			return true;
		}

		constexpr std::wstring_view DEBUGGING_PORT_OPTION = L"--remote-debugging-port";

		bool EqualNoCase(std::wstring_view A, std::wstring_view B)
		{
			if(A.size() != B.size())
				return false;
			for(size_t Index = 0; Index < A.size(); ++Index)
				if(std::towlower(A[Index]) != std::towlower(B[Index]))
					return false;
			return true;
		}

		bool ReadCommandLineArgument(std::wstring_view &CommandLine, std::wstring &Argument)
		{
			size_t Position = 0;
			while(Position < CommandLine.size() && std::iswspace(CommandLine[Position]) != 0)
				++Position;
			if(Position == CommandLine.size())
				return false;

			Argument.clear();
			bool Quoted = false;
			while(Position < CommandLine.size())
			{
				const size_t Start = Position;
				while(Position < CommandLine.size() && CommandLine[Position] == L'\\')
					++Position;
				const size_t Backslashes = Position - Start;
				if(Position == CommandLine.size() || CommandLine[Position] != L'"')
				{
					Argument.append(Backslashes, L'\\');
					if(Position == CommandLine.size() || (!Quoted && std::iswspace(CommandLine[Position]) != 0))
						break;
					Argument.push_back(CommandLine[Position++]);
					continue;
				}

				// 引号前成对的反斜杠保留一半，剩余的一个反斜杠转义引号。
				Argument.append(Backslashes / 2, L'\\');
				++Position;
				if(Backslashes % 2 != 0)
					Argument.push_back(L'"');
				else if(Quoted && Position < CommandLine.size() && CommandLine[Position] == L'"')
				{
					Argument.push_back(L'"');
					++Position;
				}
				else
					Quoted = !Quoted;
			}
			CommandLine.remove_prefix(Position);
			return true;
		}

		bool ParsePortValue(std::wstring_view Value, uint16_t *pPort)
		{
			if(pPort == nullptr || Value.empty())
				return false;
			unsigned int Port = 0;
			for(const wchar_t Character : Value)
			{
				if(Character < L'0' || Character > L'9' || Port > 65535 / 10)
					return false;
				Port = Port * 10 + (unsigned int)(Character - L'0');
				if(Port > 65535)
					return false;
			}
			if(Port == 0)
				return false;
			*pPort = (uint16_t)Port;
			return true;
		}

		std::wstring_view OptionName(std::wstring_view Argument)
		{
			return Argument.substr(0, Argument.find(L'='));
		}
	}

	bool IsOrpheusTarget(std::string_view Url)
	{
		constexpr std::string_view Prefix = "orpheus://";
		return Url.size() >= Prefix.size() && Url.substr(0, Prefix.size()) == Prefix;
	}

	bool ParseLoopbackWebSocketUrl(std::string_view Url, uint16_t *pPort)
	{
		if(Url.size() > 4096 || Url.substr(0, 5) != "ws://")
			return false;
		for(const unsigned char Character : Url)
			if(Character <= 0x20 || Character == 0x7F)
				return false;
		Url.remove_prefix(5);
		constexpr std::string_view Host = "127.0.0.1";
		if(Url.size() <= Host.size() || Url.substr(0, Host.size()) != Host)
			return false;
		Url.remove_prefix(Host.size());
		if(Url.front() != ':')
			return false;
		Url.remove_prefix(1);
		const size_t Slash = Url.find('/');
		const std::string_view PortText = Slash == std::string_view::npos ? Url : Url.substr(0, Slash);
		uint16_t Port = 0;
		if(!ParsePort(PortText, &Port))
			return false;
		if(Slash == std::string_view::npos || Url.substr(Slash).empty() || Url[Slash] != '/')
			return false;
		if(pPort != nullptr)
			*pPort = Port;
		return true;
	}

	bool ParseTargetList(std::string_view Json, std::vector<SCdpTarget> *pTargets, std::string *pError)
	{
		if(pTargets == nullptr)
			return false;
		pTargets->clear();
		if(Json.size() > MAX_TARGET_JSON_BYTES)
		{
			if(pError)
				*pError = "target list too large";
			return false;
		}
		// 解析器会把原始 NUL 当作输入结束，先检查整个响应。
		const std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(
			Json.empty() || Json.find('\0') != std::string_view::npos ? nullptr : json_parse(Json.data(), Json.size()), json_value_free);
		if(!pRoot)
		{
			if(pError)
				*pError = "invalid JSON";
			return false;
		}
		if(pRoot->type != json_array)
		{
			if(pError)
				*pError = "target list is not an array";
			return false;
		}
		std::vector<SCdpTarget> vTargets;
		for(unsigned int Index = 0; Index < pRoot->u.array.length; ++Index)
		{
			const json_value *pEntry = pRoot->u.array.values[Index];
			if(pEntry == nullptr || pEntry->type != json_object)
				continue;
			const std::string Url = JsonString(ObjectField(pEntry, "url"));
			const std::string WebSocketUrl = JsonString(ObjectField(pEntry, "webSocketDebuggerUrl"));
			if(!IsOrpheusTarget(Url) || WebSocketUrl.empty())
				continue;
			uint16_t Port = 0;
			if(!ParseLoopbackWebSocketUrl(WebSocketUrl, &Port))
				continue;
			SCdpTarget Target;
			Target.m_Url = Url;
			Target.m_WebSocketDebuggerUrl = WebSocketUrl;
			Target.m_Port = Port;
			const json_value *pProcessId = ObjectField(pEntry, "processId");
			if(pProcessId != nullptr && pProcessId->type == json_integer && pProcessId->u.integer > 0 && pProcessId->u.integer <= std::numeric_limits<uint32_t>::max())
				Target.m_ProcessId = (uint32_t)pProcessId->u.integer;
			vTargets.push_back(std::move(Target));
		}
		*pTargets = std::move(vTargets);
		if(pTargets->empty() && pError)
			*pError = "no trusted Orpheus target";
		return !pTargets->empty();
	}

	bool IsTrustedTargetForProcess(const SCdpTarget &Target, uint16_t DiscoveryPort, uint32_t ListenerProcessId, uint32_t ExpectedProcessId)
	{
		if(DiscoveryPort == 0 || ListenerProcessId == 0 || ExpectedProcessId == 0 || ListenerProcessId != ExpectedProcessId)
			return false;
		if(!IsOrpheusTarget(Target.m_Url))
			return false;
		uint16_t WebSocketPort = 0;
		if(!ParseLoopbackWebSocketUrl(Target.m_WebSocketDebuggerUrl, &WebSocketPort) || WebSocketPort != DiscoveryPort || Target.m_Port != WebSocketPort)
			return false;
		return Target.m_ProcessId == 0 || Target.m_ProcessId == ExpectedProcessId;
	}

	bool ParseDebuggingPort(std::wstring_view CommandLine, uint16_t *pPort)
	{
		if(pPort == nullptr)
			return false;
		uint16_t Port = 0;
		std::wstring Argument;
		std::wstring ValueArgument;
		while(ReadCommandLineArgument(CommandLine, Argument))
		{
			const std::wstring_view Name = OptionName(Argument);
			const bool IsAddress = EqualNoCase(Name, L"--remote-debugging-address");
			if(!IsAddress && (Port != 0 || !EqualNoCase(Name, DEBUGGING_PORT_OPTION)))
				continue;

			std::wstring_view Value;
			if(Name.size() < Argument.size())
				Value = std::wstring_view(Argument).substr(Name.size() + 1);
			else
			{
				std::wstring_view Remaining = CommandLine;
				if(ReadCommandLineArgument(Remaining, ValueArgument) && !std::wstring_view(ValueArgument).starts_with(L"--"))
				{
					CommandLine = Remaining;
					Value = ValueArgument;
				}
			}

			// 任一显式绑定地址无效时，都不能采用命令行端口。
			if(IsAddress)
			{
				if(!EqualNoCase(Value, L"127.0.0.1") && !EqualNoCase(Value, L"localhost"))
					return false;
			}
			else
				ParsePortValue(Value, &Port);
		}
		if(Port == 0)
			return false;
		*pPort = Port;
		return true;
	}

	std::wstring AddLoopbackDebuggingPort(std::wstring_view CommandLine, uint16_t Port)
	{
		if(Port == 0)
			return std::wstring(CommandLine);
		std::wstring Argument;
		for(std::wstring_view Remaining = CommandLine; ReadCommandLineArgument(Remaining, Argument);)
			if(EqualNoCase(OptionName(Argument), DEBUGGING_PORT_OPTION))
				return std::wstring(CommandLine);
		std::wstring Result(CommandLine);
		if(!Result.empty())
			Result.push_back(L' ');
		Result.append(L"--remote-debugging-address=127.0.0.1 --remote-debugging-port=");
		Result.append(std::to_wstring(Port));
		return Result;
	}
}
