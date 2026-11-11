#ifndef GAME_CLIENT_QMUI_COLORPICKERPALETTE_H
#define GAME_CLIENT_QMUI_COLORPICKERPALETTE_H

#include <base/color.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>

#include <algorithm>
#include <cstdint>
#include <vector>

// 取色器「我的调色板」配置串编解码。
// 持久化格式（qm_color_picker_palette）：`名称|#rrggbb[aa]`，条目以分号分隔；
// 名称内的分隔符（';' '|'）在写入前统一替换为空格，保证可无损往返。
namespace qm_picker_palette
{
	// “我的颜色”上限。48 格在取色器里是可滚动的 12 行网格，
	// 配置串（4096 字节）按最坏条目 ~38 字节计仍有余量。
	inline constexpr int MAX_SWATCHES = 48;
	inline constexpr int MAX_NAME_BYTES = 28; // 含结尾 NUL；中文按 UTF-8 字节计

	struct SSwatch
	{
		char m_aName[MAX_NAME_BYTES];
		ColorRGBA m_Color;
	};

	inline bool IsNameSeparator(char c)
	{
		return c == ';' || c == '|';
	}

	inline void SanitizeName(char *pName, int Size)
	{
		for(int i = 0; i < Size && pName[i] != '\0'; ++i)
		{
			if(IsNameSeparator(pName[i]))
				pName[i] = ' ';
		}
	}

	// 自动命名：取「前缀 N」中第一个未被占用的编号。
	// pReserved 指向此刻不在列表里、但随时可能插回的条目（例如撤销缓冲）：
	// 取名时一并视为已占用，否则「删除自定义 1 → 重新改色 → 撤回」会出现两个同名声块。
	// SkipIndex 是正在改名的条目本身，扫描时跳过，避免回退名被它自己的旧名字挤到下一个编号。
	inline void NextUniqueName(char *pName, int Size, const char *pPrefix, const std::vector<SSwatch> &vSwatches, const SSwatch *pReserved, int SkipIndex)
	{
		for(int Number = 1;; Number++)
		{
			char aName[MAX_NAME_BYTES];
			str_format(aName, sizeof(aName), "%s %d", pPrefix, Number);
			bool Taken = pReserved != nullptr && str_comp(pReserved->m_aName, aName) == 0;
			for(int Index = 0; !Taken && Index < (int)vSwatches.size(); Index++)
			{
				if(Index != SkipIndex && str_comp(vSwatches[Index].m_aName, aName) == 0)
					Taken = true;
			}
			if(!Taken)
			{
				str_copy(pName, aName, Size);
				return;
			}
		}
	}

	inline int HexDigitValue(char c)
	{
		if(c >= '0' && c <= '9')
			return c - '0';
		if(c >= 'a' && c <= 'f')
			return 10 + (c - 'a');
		if(c >= 'A' && c <= 'F')
			return 10 + (c - 'A');
		return -1;
	}

	// 解析 6 或 8 位十六进制（rrggbb / rrggbbaa），以及 3 或 4 位简写（rgb / rgba），失败返回 false。
	inline bool ParseHexColor(const char *pHex, ColorRGBA &Out)
	{
		if(pHex == nullptr)
			return false;
		if(pHex[0] == '#')
			++pHex;
		const int Len = str_length(pHex);
		if(Len == 3 || Len == 4)
		{
			int Values[4];
			for(int i = 0; i < Len; ++i)
			{
				Values[i] = HexDigitValue(pHex[i]);
				if(Values[i] < 0)
					return false;
			}
			const float R = (Values[0] * 17) / 255.0f;
			const float G = (Values[1] * 17) / 255.0f;
			const float B = (Values[2] * 17) / 255.0f;
			const float A = Len == 4 ? (Values[3] * 17) / 255.0f : 1.0f;
			Out = ColorRGBA(R, G, B, A);
			return true;
		}
		if(Len != 6 && Len != 8)
			return false;
		int Values[8];
		for(int i = 0; i < Len; ++i)
		{
			Values[i] = HexDigitValue(pHex[i]);
			if(Values[i] < 0)
				return false;
		}
		const float R = (Values[0] * 16 + Values[1]) / 255.0f;
		const float G = (Values[2] * 16 + Values[3]) / 255.0f;
		const float B = (Values[4] * 16 + Values[5]) / 255.0f;
		const float A = Len == 8 ? (Values[6] * 16 + Values[7]) / 255.0f : 1.0f;
		Out = ColorRGBA(R, G, B, A);
		return true;
	}

	// 解析配置串；损坏条目直接跳过，不中断后续条目。
	inline std::vector<SSwatch> Parse(const char *pConfig)
	{
		std::vector<SSwatch> vSwatches;
		if(pConfig == nullptr)
			return vSwatches;
		const char *pEntry = pConfig;
		while(*pEntry != '\0' && static_cast<int>(vSwatches.size()) < MAX_SWATCHES)
		{
			const char *pEntryEnd = strchr(pEntry, ';');
			const int EntryLen = pEntryEnd != nullptr ? (int)(pEntryEnd - pEntry) : str_length(pEntry);
			if(EntryLen > 0)
			{
				const char *pSep = (const char *)memchr(pEntry, '|', EntryLen);
				if(pSep != nullptr)
				{
					const int NameLen = minimum((int)(pSep - pEntry), MAX_NAME_BYTES - 1);
					SSwatch Swatch;
					// 手工拷贝名称片段并消毒分隔符。
					for(int i = 0; i < NameLen && i < MAX_NAME_BYTES - 1; ++i)
						Swatch.m_aName[i] = IsNameSeparator(pEntry[i]) ? ' ' : pEntry[i];
					Swatch.m_aName[minimum(NameLen, MAX_NAME_BYTES - 1)] = '\0';
					// 颜色片段必须按条目长度截断：pSep+1 之后的 str_length 会跨过分号
					// 读到后续条目，导致第一条目被误判为非法颜色。
					const int ColorLen = EntryLen - (int)(pSep + 1 - pEntry);
					char aColor[16];
					const int CopyLen = minimum(ColorLen, 15);
					for(int i = 0; i < CopyLen; ++i)
						aColor[i] = pSep[1 + i];
					aColor[CopyLen] = '\0';
					if(ParseHexColor(aColor, Swatch.m_Color))
						vSwatches.push_back(Swatch);
				}
			}
			if(pEntryEnd == nullptr)
				break;
			pEntry = pEntryEnd + 1;
		}
		return vSwatches;
	}

	// 序列化到配置缓冲；超长条目自动截断，保证返回值始终 NUL 结尾。
	inline void Serialize(char *pBuf, int BufSize, const std::vector<SSwatch> &vSwatches)
	{
		dbg_assert(BufSize > 0, "palette buffer empty");
		pBuf[0] = '\0';
		int Written = 0;
		for(const SSwatch &Swatch : vSwatches)
		{
			// 名称里的分隔符先替换为空格，保证分号切分与竖线切分不被破坏。
			char aName[MAX_NAME_BYTES];
			str_copy(aName, Swatch.m_aName, MAX_NAME_BYTES);
			SanitizeName(aName, MAX_NAME_BYTES);
			char aEntry[96];
			const unsigned Packed =
				(unsigned)round_truncate(std::clamp(Swatch.m_Color.r, 0.0f, 1.0f) * 255.0f) << 24 |
				(unsigned)round_truncate(std::clamp(Swatch.m_Color.g, 0.0f, 1.0f) * 255.0f) << 16 |
				(unsigned)round_truncate(std::clamp(Swatch.m_Color.b, 0.0f, 1.0f) * 255.0f) << 8 |
				(unsigned)round_truncate(std::clamp(Swatch.m_Color.a, 0.0f, 1.0f) * 255.0f);
			str_format(aEntry, sizeof(aEntry), "%s|#%08x", aName, Packed);
			const int EntryLen = str_length(aEntry);
			if(Written > 0)
			{
				if(Written + 1 + EntryLen >= BufSize)
					break;
				str_append(pBuf, ";", BufSize);
				++Written;
			}
			else if(EntryLen >= BufSize)
			{
				break;
			}
			str_append(pBuf, aEntry, BufSize);
			Written += EntryLen;
		}
	}
} // namespace qm_picker_palette

#endif
