#ifndef ENGINE_CLIENT_SERVERBROWSER_ADDRESSES_H
#define ENGINE_CLIENT_SERVERBROWSER_ADDRESSES_H

#include <base/system.h>

// HTTP 原始条目和界面副本使用同一连接字符串，保留地址顺序及 0.7 协议。
inline void ServerBrowserFormatAddresses(char *pBuffer, int BufferSize, const NETADDR *pAddrs, int NumAddrs)
{
	if(BufferSize <= 0)
		return;
	pBuffer[0] = '\0';
	int Length = 0;
	for(int Index = 0; Index < NumAddrs; ++Index)
	{
		char aAddress[sizeof("tw-0.7+udp://") - 1 + NETADDR_MAXSTRSIZE];
		const char *pPrefix = pAddrs[Index].type & NETTYPE_TW7 ? "tw-0.7+udp://" : "";
		str_copy(aAddress, pPrefix);
		const int PrefixLength = str_length(pPrefix);
		net_addr_str(&pAddrs[Index], aAddress + PrefixLength, sizeof(aAddress) - PrefixLength, true);
		const int AddressLength = str_length(aAddress);
		const int SeparatorLength = Length > 0 ? 1 : 0;
		// 容量不足时保留完整候选，不能把截断的 IP 或端口交给连接入口。
		if(AddressLength + SeparatorLength >= BufferSize - Length)
			break;
		if(SeparatorLength)
			str_append(pBuffer, ",", BufferSize);
		str_append(pBuffer, aAddress, BufferSize);
		Length += AddressLength + SeparatorLength;
	}
}

#endif
