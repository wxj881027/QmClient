// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "update_version.h"

#include <base/str.h>

#include <climits>

namespace
{

	void NormalizeQmClientVersion(const char *pStr, char *pBuf, size_t BufSize)
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

	bool ParseQmClientVersion(const char *pVersion, int (&aParts)[4])
	{
		if(!pVersion || pVersion[0] == '\0')
			return false;

		int PartCount = 0;
		const char *pCursor = pVersion;
		while(*pCursor != '\0')
		{
			if(PartCount == 4)
				return false;
			int Value = 0;
			bool HasDigit = false;
			while(*pCursor >= '0' && *pCursor <= '9')
			{
				HasDigit = true;
				const int Digit = *pCursor - '0';
				if(Value > (INT_MAX - Digit) / 10)
					return false;
				Value = Value * 10 + Digit;
				pCursor++;
			}
			if(!HasDigit || (*pCursor != '\0' && *pCursor != '.'))
				return false;
			aParts[PartCount++] = Value;
			if(*pCursor == '.')
			{
				pCursor++;
				if(*pCursor == '\0')
					return false;
			}
		}
		return PartCount >= 1;
	}

} // namespace

bool IsQmClientRemoteVersionNewer(const char *pRemoteVersion, const char *pLocalVersion, bool LocalIsDevelopmentBuild)
{
	char aRemote[64];
	char aLocal[64];
	NormalizeQmClientVersion(pRemoteVersion, aRemote, sizeof(aRemote));
	NormalizeQmClientVersion(pLocalVersion, aLocal, sizeof(aLocal));

	if(aRemote[0] == '\0' || aLocal[0] == '\0')
		return false;

	int aRemoteParts[4] = {};
	int aLocalParts[4] = {};
	if(!ParseQmClientVersion(aRemote, aRemoteParts) || !ParseQmClientVersion(aLocal, aLocalParts))
		return false;

	for(int Index = 0; Index < 4; ++Index)
	{
		if(aRemoteParts[Index] != aLocalParts[Index])
			return aRemoteParts[Index] > aLocalParts[Index];
	}
	return LocalIsDevelopmentBuild;
}
