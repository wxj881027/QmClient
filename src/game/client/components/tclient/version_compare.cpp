#include "version_compare.h"

#include <base/str.h>

#include <string_view>

namespace
{

	void NormalizeVersion(const char *pStr, char *pBuf, size_t BufSize)
	{
		if(!pBuf || BufSize == 0)
			return;
		pBuf[0] = '\0';
		if(!pStr)
			return;

		pStr = str_skip_whitespaces_const(pStr);
		if(pStr[0] == 'v' || pStr[0] == 'V')
			pStr++;

		str_copy(pBuf, pStr, BufSize);
		int End = str_length(pBuf);
		while(End > 0 && str_isspace(pBuf[End - 1]))
		{
			pBuf[End - 1] = '\0';
			End--;
		}
	}

	const char *SkipDot(const char *pStr)
	{
		return pStr && pStr[0] == '.' ? pStr + 1 : pStr;
	}

	bool ParseNextComponent(const char *&pVersion, std::string_view &OutValue)
	{
		OutValue = {};
		if(!pVersion || pVersion[0] == '\0')
			return false;

		const char *pCursor = pVersion;
		pCursor = SkipDot(pCursor);
		if(!pCursor || pCursor[0] == '\0')
		{
			pVersion = pCursor;
			return false;
		}

		const char *pDigits = pCursor;
		while(*pCursor >= '0' && *pCursor <= '9')
		{
			pCursor++;
		}

		if(pDigits == pCursor)
			return false;

		if(*pCursor != '\0' && *pCursor != '.')
			return false;

		// 去掉前导零后按位数和字典序比较，避免长数字段的整数转换溢出。
		while(pDigits != pCursor && *pDigits == '0')
			pDigits++;
		OutValue = std::string_view(pDigits, pCursor - pDigits);
		pVersion = pCursor;
		return true;
	}

	int CompareNormalizedVersions(const char *pLeft, const char *pRight)
	{
		const char *pLeftCursor = pLeft;
		const char *pRightCursor = pRight;

		for(int Index = 0; Index < 4; ++Index)
		{
			std::string_view LeftValue;
			std::string_view RightValue;
			const bool LeftHasValue = ParseNextComponent(pLeftCursor, LeftValue);
			const bool RightHasValue = ParseNextComponent(pRightCursor, RightValue);

			if(!LeftHasValue && !RightHasValue)
				return 0;
			if(LeftValue.size() != RightValue.size())
				return LeftValue.size() < RightValue.size() ? -1 : 1;
			if(LeftValue != RightValue)
				return LeftValue < RightValue ? -1 : 1;
		}

		return 0;
	}

} // namespace

bool IsRemoteVersionNewer(const char *pRemoteVersion, const char *pLocalVersion)
{
	char aRemote[64];
	char aLocal[64];
	NormalizeVersion(pRemoteVersion, aRemote, sizeof(aRemote));
	NormalizeVersion(pLocalVersion, aLocal, sizeof(aLocal));

	if(aRemote[0] == '\0' || aLocal[0] == '\0')
		return false;

	return CompareNormalizedVersions(aRemote, aLocal) > 0;
}
