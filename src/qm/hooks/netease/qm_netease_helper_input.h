#ifndef QM_HOOKS_NETEASE_QM_NETEASE_HELPER_INPUT_H
#define QM_HOOKS_NETEASE_QM_NETEASE_HELPER_INPUT_H

#include <algorithm>
#include <cstddef>
#include <string>

namespace QmNeteaseHelper
{
	inline bool ValidWideByteCount(size_t Bytes, size_t CapacityBytes)
	{
		return Bytes <= CapacityBytes && Bytes % sizeof(wchar_t) == 0;
	}

	// 原生 API 不保证字符串以 NUL 结尾，复制仅使用实际返回的完整字符范围。
	inline std::wstring CopyWideBytes(const wchar_t *pData, size_t Bytes, size_t CapacityChars)
	{
		if(!pData || Bytes % sizeof(wchar_t) != 0 || Bytes / sizeof(wchar_t) > CapacityChars)
			return {};
		const size_t Count = Bytes / sizeof(wchar_t);
		return std::wstring(pData, std::find(pData, pData + Count, L'\0'));
	}

	// 调用方已排除内嵌双引号；末尾反斜杠在 Windows 引号内必须成对转义。
	inline std::wstring QuoteDirectoryArgument(std::wstring Value)
	{
		while(Value.size() > 1 && (Value.back() == L'\\' || Value.back() == L'/'))
		{
			if(Value.size() == 3 && Value[1] == L':')
				break;
			Value.pop_back();
		}
		if(!Value.empty() && Value.back() == L'\\')
			Value.push_back(L'\\');
		return L"\"" + Value + L"\"";
	}
}

#endif
