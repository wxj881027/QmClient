#ifndef ENGINE_CLIENT_SERVERBROWSER_HTTP_PARSE_H
#define ENGINE_CLIENT_SERVERBROWSER_HTTP_PARSE_H

#include "serverbrowser_addresses.h"

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
			const json_value &Address = Addresses[AddressIndex];
			if(Address.type != json_string)
				return true;
			NETADDR ParsedAddress;
			if(net_addr_from_url(&ParsedAddress, Address.u.string.ptr, nullptr, 0) || ParsedAddress.port == 0)
				continue;
			const bool IsSix = (ParsedAddress.type & NETTYPE_TW7) == 0;
			if(HasSix && !IsSix)
				continue;
			if(IsSix && !HasSix)
			{
				// 找到可连接的 0.6 地址后，丢弃先前收集的 0.7 地址。
				Parsed.m_NumAddresses = 0;
				HasSix = true;
			}
			if(Parsed.m_NumAddresses < (int)std::size(Parsed.m_aAddresses))
				Parsed.m_aAddresses[Parsed.m_NumAddresses++] = ParsedAddress;
		}
		if(Parsed.m_NumAddresses > 0)
		{
			ServerBrowserFormatAddresses(Parsed.m_aAddress, sizeof(Parsed.m_aAddress), Parsed.m_aAddresses, Parsed.m_NumAddresses);
			vServers.push_back(Parsed);
		}
	}
	*pvServers = std::move(vServers);
	return false;
}

#endif
