#include "qm_netease_cdp.h"

#include <engine/external/json-parser/json.h>

#include <algorithm>
#include <charconv>
#include <cwctype>
#include <limits>

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

		bool EqualNoCase(std::wstring_view A, std::wstring_view B)
		{
			if(A.size() != B.size())
				return false;
			for(size_t Index = 0; Index < A.size(); ++Index)
				if(std::towlower(A[Index]) != std::towlower(B[Index]))
					return false;
			return true;
		}

		bool IsOptionBoundary(wchar_t Character)
		{
			return std::iswspace(Character) != 0 || Character == L'"' || Character == L'\'';
		}

		size_t FindOption(std::wstring_view CommandLine, std::wstring_view Option, size_t SearchFrom)
		{
			for(size_t Position = CommandLine.find(Option, SearchFrom); Position != std::wstring_view::npos; Position = CommandLine.find(Option, Position + 1))
			{
				if((Position == 0 || IsOptionBoundary(CommandLine[Position - 1])) &&
					(Position + Option.size() == CommandLine.size() || CommandLine[Position + Option.size()] == L'=' || IsOptionBoundary(CommandLine[Position + Option.size()])))
					return Position;
			}
			return std::wstring_view::npos;
		}

		bool ReadOptionValue(std::wstring_view CommandLine, std::wstring_view Option, size_t SearchFrom, std::wstring_view *pValue, size_t *pNext)
		{
			if(pValue == nullptr)
				return false;
			const size_t Position = FindOption(CommandLine, Option, SearchFrom);
			if(Position == std::wstring_view::npos)
				return false;
			size_t ValueStart = Position + Option.size();
			if(ValueStart < CommandLine.size() && CommandLine[ValueStart] == L'=')
				++ValueStart;
			else
			{
				while(ValueStart < CommandLine.size() && std::iswspace(CommandLine[ValueStart]) != 0)
					++ValueStart;
			}
			if(ValueStart >= CommandLine.size())
				return false;
			const bool Quoted = CommandLine[ValueStart] == L'"';
			if(Quoted)
				++ValueStart;
			else if(CommandLine.substr(ValueStart, 2) == L"--")
				return false;
			const size_t ValueEnd = Quoted ? CommandLine.find(L'"', ValueStart) : [&] {
				size_t End = ValueStart;
				while(End < CommandLine.size() && !std::iswspace(CommandLine[End]) && CommandLine[End] != L'"')
					++End;
				return End;
			}();
			if(ValueEnd == std::wstring_view::npos || ValueEnd == ValueStart)
				return false;
			*pValue = CommandLine.substr(ValueStart, ValueEnd - ValueStart);
			if(pNext != nullptr)
				*pNext = ValueEnd + (Quoted && ValueEnd < CommandLine.size() ? 1 : 0);
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

		bool HasAnyOption(std::wstring_view CommandLine, std::wstring_view Option)
		{
			return FindOption(CommandLine, Option, 0) != std::wstring_view::npos;
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
			if(Character < 0x20 || Character == 0x7F)
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
		json_value *pRoot = json_parse(Json.data(), Json.size());
		if(pRoot == nullptr)
		{
			if(pError)
				*pError = "invalid JSON";
			return false;
		}
		const bool IsArray = pRoot->type == json_array;
		if(!IsArray)
		{
			json_value_free(pRoot);
			if(pError)
				*pError = "target list is not an array";
			return false;
		}
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
			pTargets->push_back(std::move(Target));
		}
		json_value_free(pRoot);
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
		const std::wstring LowerCommandLine = [&] {
			std::wstring Value(CommandLine);
			std::transform(Value.begin(), Value.end(), Value.begin(), [](wchar_t C) { return (wchar_t)std::towlower(C); });
			return Value;
		}();
		// Chromium 默认只绑定 loopback；一旦命令行显式指定其它地址，
		// 不把该调试端口当作可信来源，也不尝试连接它。
		for(size_t SearchFrom = 0;;)
		{
			const size_t Position = FindOption(LowerCommandLine, L"--remote-debugging-address", SearchFrom);
			if(Position == std::wstring_view::npos)
				break;
			std::wstring_view Address;
			size_t Next = 0;
			if(ReadOptionValue(LowerCommandLine, L"--remote-debugging-address", Position, &Address, &Next))
			{
				if(!EqualNoCase(Address, L"127.0.0.1") && !EqualNoCase(Address, L"localhost"))
					return false;
				SearchFrom = std::max(Next, Position + 1);
			}
			else
				return false;
		}

		for(size_t SearchFrom = 0;;)
		{
			const size_t Position = FindOption(LowerCommandLine, L"--remote-debugging-port", SearchFrom);
			if(Position == std::wstring_view::npos)
				break;
			std::wstring_view Value;
			size_t Next = 0;
			if(ReadOptionValue(LowerCommandLine, L"--remote-debugging-port", Position, &Value, &Next))
			{
				uint16_t Port = 0;
				if(ParsePortValue(Value, &Port))
				{
					*pPort = Port;
					return true;
				}
				SearchFrom = std::max(Next, Position + 1);
			}
			else
				SearchFrom = Position + 1;
		}
		return false;
	}

	std::wstring AddLoopbackDebuggingPort(std::wstring_view CommandLine, uint16_t Port)
	{
		if(Port == 0)
			return std::wstring(CommandLine);
		uint16_t ExistingPort = 0;
		const std::wstring LowerCommandLine = [&] {
			std::wstring Value(CommandLine);
			std::transform(Value.begin(), Value.end(), Value.begin(), [](wchar_t C) { return (wchar_t)std::towlower(C); });
			return Value;
		}();
		if(ParseDebuggingPort(CommandLine, &ExistingPort) || HasAnyOption(LowerCommandLine, L"--remote-debugging-port"))
			return std::wstring(CommandLine);
		std::wstring Result(CommandLine);
		if(!Result.empty())
			Result.push_back(L' ');
		Result.append(L"--remote-debugging-address=127.0.0.1 --remote-debugging-port=");
		Result.append(std::to_wstring(Port));
		return Result;
	}
}
