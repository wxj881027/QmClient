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
		if(static_cast<size_t>(str_length(pStr)) >= BufSize)
			return;

		str_copy(pBuf, pStr, BufSize);
		int End = str_length(pBuf);
		while(End > 0 && str_isspace(pBuf[End - 1]))
		{
			pBuf[End - 1] = '\0';
			End--;
		}
	}

	bool ParseNumber(const char *&pCursor, int &Value)
	{
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
		return HasDigit;
	}

} // namespace

bool ParseQmClientVersion(const char *pVersion, SQmClientVersion &Version)
{
	Version = {};
	char aNormalized[64];
	NormalizeQmClientVersion(pVersion, aNormalized, sizeof(aNormalized));
	const char *pCursor = aNormalized;
	int PartCount = 0;
	while(true)
	{
		if(PartCount == 4 || !ParseNumber(pCursor, Version.m_aParts[PartCount++]))
			return false;
		if(*pCursor != '.')
			break;
		++pCursor;
	}
	// 旧纯数字版本仅用于识别已有安装；新预览版本固定为两段基础版本。
	if(*pCursor == '\0')
		return true;
	const char *pPreview = str_startswith(pCursor, "-preview.");
	if(PartCount != 2 || !pPreview || *pPreview == '0')
		return false;
	return ParseNumber(pPreview, Version.m_Preview) && *pPreview == '\0';
}

bool IsQmClientRemoteVersionNewer(const char *pRemoteVersion, const char *pLocalVersion, bool LocalIsDevelopmentBuild)
{
	SQmClientVersion Remote;
	SQmClientVersion Local;
	if(!ParseQmClientVersion(pRemoteVersion, Remote) || !ParseQmClientVersion(pLocalVersion, Local))
		return false;
	if(Remote.m_Preview && !Local.m_Preview && !LocalIsDevelopmentBuild)
		return false;
	for(int Index = 0; Index < 4; ++Index)
	{
		if(Remote.m_aParts[Index] != Local.m_aParts[Index])
			return Remote.m_aParts[Index] > Local.m_aParts[Index];
	}
	if(Remote.m_Preview != Local.m_Preview)
	{
		if(Remote.m_Preview == 0)
			return true;
		return Local.m_Preview != 0 && Remote.m_Preview > Local.m_Preview;
	}
	return LocalIsDevelopmentBuild && Local.m_Preview == 0;
}
