#ifndef ENGINE_SHARED_HTTP_URL_H
#define ENGINE_SHARED_HTTP_URL_H

#include <base/str.h>
#include <base/system.h>

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

// 复用原 HTTP 并发分组的 authority 提取规则；只提取主机，不替代 curl 的 URL 校验。
// 最终路由判断必须传入 CURLINFO_EFFECTIVE_URL，不能用最初请求地址代替。
inline std::string HttpUrlHost(std::string_view Url)
{
	const auto SchemeEnd = Url.find("://");
	if(SchemeEnd != std::string_view::npos && SchemeEnd < Url.find_first_of("/?#"))
		Url.remove_prefix(SchemeEnd + 3);
	const auto Authority = Url.substr(0, Url.find_first_of("/?#"));
	// 控制字符不能成为旁路证据，也不能经 C 字符串接口截断为另一个主机。
	for(const char RawCharacter : Authority)
	{
		const auto Character = static_cast<unsigned char>(RawCharacter);
		if(Character <= ' ' || Character == 127)
			return {};
	}
	// 先剥离 userinfo，再处理端口；密码中的冒号不属于主机。
	const auto UserEnd = Authority.find('@');
	auto Host = Authority.substr(UserEnd == std::string_view::npos ? 0 : UserEnd + 1);
	if(Host.empty())
		return {};

	std::string_view Port;
	const bool Ipv6 = Host.front() == '[';
	if(Ipv6)
	{
		const auto Close = Host.find(']');
		if(Close == std::string_view::npos || Close <= 1)
			return {};
		if(Close + 1 < Host.size())
		{
			if(Host[Close + 1] != ':')
				return {};
			Port = Host.substr(Close + 2);
		}
		Host = Host.substr(1, Close - 1);
		// curl 将 IPv6 zone id 单独保存；NO_PROXY 的地址匹配只使用地址部分。
		const auto Zone = Host.find('%');
		if(Zone != std::string_view::npos)
		{
			if(Zone + 1 == Host.size())
				return {};
			Host = Host.substr(0, Zone);
		}
		NETADDR Address;
		const std::string Literal = "[" + std::string(Host) + "]";
		if(net_addr_from_str(&Address, Literal.c_str()) != 0 || Address.type != NETTYPE_IPV6)
			return {};
	}
	else
	{
		const auto Colon = Host.find(':');
		if(Colon != std::string_view::npos)
		{
			Port = Host.substr(Colon + 1);
			Host = Host.substr(0, Colon);
		}
	}
	if(!Port.empty())
	{
		unsigned int Number;
		const auto Parsed = std::from_chars(Port.data(), Port.data() + Port.size(), Number);
		if(Parsed.ec != std::errc{} || Parsed.ptr != Port.data() + Port.size() || Number > 65535)
			return {};
	}
	if(Host.empty())
		return {};

	std::string Result;
	Result.reserve(Host.size());
	for(size_t Index = 0; Index < Host.size(); ++Index)
	{
		unsigned char Character = static_cast<unsigned char>(Host[Index]);
		if(!Ipv6 && Character == '%')
		{
			if(Index + 2 >= Host.size())
				return {};
			const char aHex[] = {Host[Index + 1], Host[Index + 2], '\0'};
			if(str_hex_decode(&Character, 1, aHex) != 0)
				return {};
			Index += 2;
		}
		static constexpr std::string_view FORBIDDEN_HOST_CHARACTERS = ":/?#@[]\\!{}$'\"^`*<>=;,+&()%";
		if(Character <= ' ' || Character == 127 ||
			(!Ipv6 && FORBIDDEN_HOST_CHARACTERS.find(static_cast<char>(Character)) != std::string_view::npos))
			return {};
		Result.push_back(static_cast<char>(Character >= 'A' && Character <= 'Z' ? Character + ('a' - 'A') : Character));
	}
	// curl 接受短写、八进制及十六进制 IPv4；保持与原 URL API 相同的主机键。
	// 仅在整个主机是合法数字地址时规范化，普通域名和非法数字仍保持原样。
	if(!Ipv6 && Result.front() >= '0' && Result.front() <= '9')
	{
		std::string_view Remaining(Result);
		uint32_t aParts[4] = {};
		size_t Count = 0;
		while(!Remaining.empty())
		{
			if(Count == 4)
				return Result;
			const auto Dot = Remaining.find('.');
			auto Part = Remaining.substr(0, Dot);
			int Base = 10;
			if(Part.size() > 1 && Part.front() == '0')
			{
				Base = Part[1] == 'x' || Part[1] == 'X' ? 16 : 8;
				Part.remove_prefix(Base == 16 ? 2 : 1);
			}
			const auto Parsed = std::from_chars(Part.data(), Part.data() + Part.size(), aParts[Count], Base);
			if(Part.empty() || Parsed.ec != std::errc{} || Parsed.ptr != Part.data() + Part.size())
				return Result;
			++Count;
			if(Dot == std::string_view::npos)
				break;
			Remaining.remove_prefix(Dot + 1);
			if(Remaining.empty())
				return Result;
		}
		uint32_t Address = aParts[Count - 1];
		if(static_cast<uint64_t>(Address) >= (uint64_t{1} << (8 * (5 - Count))))
			return Result;
		for(size_t Index = 0; Index + 1 < Count; ++Index)
		{
			if(aParts[Index] > 255)
				return Result;
			Address |= aParts[Index] << (24 - 8 * Index);
		}
		return std::to_string(Address >> 24) + "." + std::to_string((Address >> 16) & 255) + "." +
		       std::to_string((Address >> 8) & 255) + "." + std::to_string(Address & 255);
	}
	return Result;
}

inline std::string HttpUrlHost(const char *pUrl)
{
	return pUrl ? HttpUrlHost(std::string_view(pUrl)) : std::string();
}

#endif
