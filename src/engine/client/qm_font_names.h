// FreeType SFNT 名称显式解码为 UTF-8，避免 Windows 本地代码页污染字体选择。
#ifndef ENGINE_CLIENT_QM_FONT_NAMES_H
#define ENGINE_CLIENT_QM_FONT_NAMES_H

#include <base/system.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include <string>
#include <utility>

inline bool QmFontNameUsable(const std::string &Name)
{
	if(Name.empty() || !str_utf8_check(Name.c_str()) || Name.find("�") != std::string::npos)
		return false;
	// 代码页转换留下的纯问号/空白占位不能遮住同 face 的有效 Unicode 名称。
	return Name.find_first_not_of("? \t\r\n") != std::string::npos;
}

inline std::string QmDecodeSfntName(const FT_SfntName &Name)
{
	if(Name.string == nullptr || Name.string_len == 0 || Name.string_len > 65536)
		return {};
	std::string Result;
	if(Name.platform_id == 0 || (Name.platform_id == 3 && (Name.encoding_id == 1 || Name.encoding_id == 10)))
	{
		if(Name.string_len % 2 != 0)
			return {};
		for(FT_UInt Index = 0; Index < Name.string_len; Index += 2)
		{
			unsigned Codepoint = (Name.string[Index] << 8) | Name.string[Index + 1];
			if(Codepoint >= 0xD800 && Codepoint <= 0xDBFF)
			{
				if(Index + 3 >= Name.string_len)
					return {};
				const unsigned Low = (Name.string[Index + 2] << 8) | Name.string[Index + 3];
				if(Low < 0xDC00 || Low > 0xDFFF)
					return {};
				Codepoint = 0x10000 + ((Codepoint - 0xD800) << 10) + Low - 0xDC00;
				Index += 2;
			}
			else if(Codepoint >= 0xDC00 && Codepoint <= 0xDFFF)
				return {};
			if(Codepoint == 0 || Codepoint < 0x20)
				return {};
			char aUtf8[4];
			const int Length = str_utf8_encode(aUtf8, Codepoint);
			Result.append(aUtf8, Length);
		}
	}
	else if(Name.platform_id == 1 && Name.encoding_id == 0)
	{
		// Mac Roman 只安全接收 ASCII；非 ASCII 留给 Unicode 名称记录。
		for(FT_UInt Index = 0; Index < Name.string_len; ++Index)
		{
			if(Name.string[Index] < 0x20 || Name.string[Index] >= 0x7F)
				return {};
			Result += static_cast<char>(Name.string[Index]);
		}
	}
	return QmFontNameUsable(Result) ? Result : std::string{};
}

inline std::string QmFontSfntName(FT_Face Face, unsigned NameId, const char *pFallback)
{
	if(Face == nullptr)
		return {};
	std::string Best;
	int BestScore = -1;
	for(FT_UInt Index = 0; Index < FT_Get_Sfnt_Name_Count(Face); ++Index)
	{
		FT_SfntName Name;
		if(FT_Get_Sfnt_Name(Face, Index, &Name) != 0 || Name.name_id != NameId)
			continue;
		std::string Decoded = QmDecodeSfntName(Name);
		if(Decoded.empty())
			continue;
		const int Score = (Name.platform_id == 3 && Name.language_id == 0x409 ? 100 : 0) + (Name.platform_id == 0 ? 20 : Name.platform_id == 3 ? 10 :
																			 0);
		if(Score > BestScore)
		{
			Best = std::move(Decoded);
			BestScore = Score;
		}
	}
	if(Best.empty() && pFallback != nullptr && QmFontNameUsable(pFallback))
		Best = pFallback;
	return Best;
}

struct SQmFontFaceNames
{
	std::string m_Family;
	std::string m_Style;
};

inline SQmFontFaceNames QmFontFaceNames(FT_Face Face)
{
	if(Face == nullptr)
		return {};
	SQmFontFaceNames Result{QmFontSfntName(Face, 1, Face->family_name), QmFontSfntName(Face, 2, Face->style_name)};
	if(Result.m_Family.empty())
		Result.m_Family = QmFontSfntName(Face, 16, FT_Get_Postscript_Name(Face));
	if(Result.m_Style.empty())
		Result.m_Style = "Regular";
	return Result;
}

#endif
