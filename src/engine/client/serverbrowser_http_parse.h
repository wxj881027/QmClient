#ifndef ENGINE_CLIENT_SERVERBROWSER_HTTP_PARSE_H
#define ENGINE_CLIENT_SERVERBROWSER_HTTP_PARSE_H

#include <base/system.h>

#include <engine/shared/json.h>
#include <engine/shared/serverinfo.h>

#include <vector>

// 纯解析层：先构建完整的新列表，输入无效时保留调用方旧列表。
inline bool ServerBrowserParseHttpList(json_value *pJson, std::vector<CServerInfo> *pvServers)
{
	if(pJson == nullptr)
		return true;
	const json_value &Servers = (*pJson)["servers"];
	if(Servers.type != json_array)
		return true;
	std::vector<CServerInfo> vServers;
	for(unsigned i = 0; i < Servers.u.array.length; ++i)
	{
		const json_value &Server = Servers[i];
		const json_value &Addresses = Server["addresses"];
		const json_value &Info = Server["info"];
		const json_value &Location = Server["location"];
		if(Addresses.type != json_array || (Location.type != json_string && Location.type != json_none))
			return true;
		int ParsedLocation = CServerInfo::LOC_UNKNOWN;
		if(Location.type == json_string && CServerInfo::ParseLocation(&ParsedLocation, Location))
			return true;
		CServerInfo2 ParsedInfo;
		if(CServerInfo2::FromJson(&ParsedInfo, &Info))
			continue;
		CServerInfo Parsed = ParsedInfo;
		Parsed.m_Location = ParsedLocation;
		Parsed.m_NumAddresses = 0;
		bool HasSix = false;
		for(unsigned AddressIndex = 0; AddressIndex < Addresses.u.array.length; ++AddressIndex)
		{
			if(Addresses[AddressIndex].type != json_string)
				return true;
			if(str_startswith(Addresses[AddressIndex], "tw-0.6+udp://"))
				HasSix = true;
		}
		for(unsigned AddressIndex = 0; AddressIndex < Addresses.u.array.length; ++AddressIndex)
		{
			const char *pAddress = Addresses[AddressIndex].u.string.ptr;
			if(HasSix && str_startswith(pAddress, "tw-0.7+udp://"))
				continue;
			NETADDR ParsedAddress;
			if(net_addr_from_url(&ParsedAddress, pAddress, nullptr, 0) || ParsedAddress.port == 0)
				continue;
			if(Parsed.m_NumAddresses < (int)std::size(Parsed.m_aAddresses))
				Parsed.m_aAddresses[Parsed.m_NumAddresses++] = ParsedAddress;
		}
		if(Parsed.m_NumAddresses > 0)
			vServers.push_back(Parsed);
	}
	*pvServers = std::move(vServers);
	return false;
}

#endif
