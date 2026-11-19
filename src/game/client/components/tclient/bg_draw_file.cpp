#include "bg_draw_file.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

#define MAX_LINE_LENGTH 256
#define MAX_PARTS_PER_ITEM 4096

namespace
{
	bool ParseFloat(char *&pCursor, float &Value, bool Required)
	{
		char *pEnd = nullptr;
		const float Parsed = std::strtof(pCursor, &pEnd);
		if(pEnd == pCursor)
			return !Required;
		Value = Parsed;
		pCursor = pEnd;
		if(*pCursor == ',')
		{
			++pCursor;
		}
		else if(*pCursor != '\0')
		{
			return false;
		}
		return true;
	}
}

bool BgDrawFile::Write(const std::function<bool(const char *)> &WriteLine, const CBgDrawItemData &Data)
{
	char aBuf[MAX_LINE_LENGTH];
	for(const CBgDrawItemDataPoint &Point : Data)
	{
		// 预留换行空间，保证读取时一行对应一个完整的点。
		const int MaxLength = sizeof(aBuf) - 3;
		int Length = std::snprintf(aBuf, sizeof(aBuf), "%f,%f,%f,%f,%f,%f,%f",
			Point.x, Point.y, Point.w, Point.r, Point.g, Point.b, Point.a);
		if(Length > MaxLength)
		{
			constexpr int Precision = std::numeric_limits<float>::max_digits10;
			Length = std::snprintf(aBuf, sizeof(aBuf), "%.*g,%.*g,%.*g,%.*g,%.*g,%.*g,%.*g",
				Precision, Point.x, Precision, Point.y, Precision, Point.w,
				Precision, Point.r, Precision, Point.g, Precision, Point.b, Precision, Point.a);
		}
		if(Length < 0 || Length > MaxLength || !WriteLine(aBuf))
			return false;
	}
	if(!WriteLine(","))
		return false;
	return true;
}
bool BgDrawFile::Read(const std::function<bool(char *pBuf, int Length)> &ReadLine, CBgDrawItemData &Data)
{
	char aBuf[MAX_LINE_LENGTH];
	Data.clear();
	for(int Count = 0; Count < MAX_PARTS_PER_ITEM; ++Count)
	{
		if(!ReadLine(aBuf, sizeof(aBuf)))
			return !Data.empty();
		if(aBuf[0] == '\0' || aBuf[0] == EOF || aBuf[0] == ',')
			return !Data.empty();
		if(aBuf[0] == '#')
			continue;
		float x = 0.0f, y = 0.0f;
		float w = 5.0f;
		float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
		char *pCursor = aBuf;
		if(!ParseFloat(pCursor, x, true) || !ParseFloat(pCursor, y, true))
			continue; // Need x and y
		if(ParseFloat(pCursor, w, false) &&
			ParseFloat(pCursor, r, false) &&
			ParseFloat(pCursor, g, false) &&
			ParseFloat(pCursor, b, false))
		{
			ParseFloat(pCursor, a, false);
		}
		if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) ||
			!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(a))
			continue;
		Data.emplace_back(x, y, w, r, g, b, a);
	}
	return !Data.empty();
}
bool BgDrawFile::Write(FILE *pFile, const CBgDrawItemData &Data)
{
	auto WriteLine = [pFile](const char *pLine) -> bool {
		if(fputs(pLine, pFile) == EOF)
			return false;
		if(fputc('\n', pFile) == EOF)
			return false;
		return true;
	};
	return Write(WriteLine, Data);
}
bool BgDrawFile::Read(FILE *pFile, CBgDrawItemData &Data)
{
	auto ReadLine = [pFile](char *pBuf, int Length) -> bool {
		if(!std::fgets(pBuf, Length, pFile))
			return false;
		size_t Len = std::strlen(pBuf);
		while(Len > 0 && (pBuf[Len - 1] == '\n' || pBuf[Len - 1] == '\r'))
			pBuf[--Len] = '\0';
		return true;
	};
	return Read(ReadLine, Data);
}
