// FreeType SFNT 名称显式解码为 UTF-8，避免 Windows 本地代码页污染字体选择。
#ifndef ENGINE_CLIENT_QM_FONT_NAMES_H
#define ENGINE_CLIENT_QM_FONT_NAMES_H

#include <base/system.h>

#include <engine/client/qm_font_name_match.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
	std::string m_LegacyFamily;
	std::string m_LegacyStyle;
};

// 只折叠已知的思源集合变体；不按任意后缀猜测用户字体的族名。
inline SQmFontFaceNames QmOrganizeFontFaceNames(std::string Family, std::string Style, const std::string &TypographicFamily, const std::string &TypographicStyle, bool Collection)
{
	SQmFontFaceNames Result{std::move(Family), std::move(Style), {}, {}};
	Result.m_LegacyFamily = Result.m_Family;
	Result.m_LegacyStyle = Result.m_Style;
	// OpenType 16/17 表示排版字体族/样式，1/2 是旧版四样式兼容组。
	if(!TypographicFamily.empty())
		Result.m_Family = TypographicFamily;
	if(!TypographicStyle.empty())
		Result.m_Style = TypographicStyle;
	if(Result.m_Style.empty())
		Result.m_Style = "Regular";
	if(Collection)
	{
		for(const char *pVariant : {"K", "SC", "TC", "HC", "HW", "HW K", "HW SC", "HW TC", "HW HC"})
		{
			if(Result.m_Family == std::string("Source Han Sans ") + pVariant ||
				(Result.m_Family == "Source Han Sans" && Result.m_LegacyFamily == std::string("Source Han Sans ") + pVariant))
			{
				Result.m_Family = "Source Han Sans";
				const std::string Prefix = std::string(pVariant) + " ";
				// 新版集合可能用 16 统一族、17 已包含区域，避免丢失或重复区域标签。
				if(Result.m_Style.compare(0, Prefix.size(), Prefix) != 0)
					Result.m_Style = Prefix + Result.m_Style;
				break;
			}
		}
	}
	return Result;
}

// 旧配置只作为同一真实 face 的别名；不增加候选族或伪造字体面。
inline bool QmFontFaceMatchesLegacyName(const SQmFontFaceNames &Names, const char *pRequested)
{
	if(Names.m_LegacyFamily.empty())
		return false;
	const std::string FullName = Names.m_LegacyFamily + " " + Names.m_LegacyStyle;
	return QmFontNamesEqual(pRequested, FullName.c_str()) ||
	       (Names.m_LegacyFamily != Names.m_Family && QmFontNamesEqual(pRequested, Names.m_LegacyFamily.c_str()));
}

inline int QmFontRegularStyleRank(const std::string &Style)
{
	return QmFontNamesEqual(Style.c_str(), "Regular") ? 3 : QmFontNamesEqual(Style.c_str(), "Book") ? 2 :
							QmFontNamesEqual(Style.c_str(), "Normal")       ? 1 :
													  0;
}

// 显式字体族查询不接受完整 face 或旧别名；族列表与配置 face 查询不能混用。
inline FT_Face QmResolveFontFamilyName(const char *pFamily, const std::vector<FT_Face> &vFaces, const std::unordered_map<FT_Face, SQmFontFaceNames> &Names)
{
	if(pFamily == nullptr || pFamily[0] == '\0')
		return nullptr;
	FT_Face Result = nullptr;
	int BestScore = -1;
	for(FT_Face Face : vFaces)
	{
		const auto &Name = Names.at(Face);
		if(!QmFontNamesEqual(pFamily, Name.m_Family.c_str()))
			continue;
		const int Score = QmFontRegularStyleRank(Name.m_Style);
		if(Score > BestScore)
		{
			BestScore = Score;
			Result = Face;
		}
	}
	return Result;
}

// 所有候选共享优先级：排版完整名、排版族（普通样式优先）、旧名别名、PostScript。
// 不能逐 face 遇到旧名就返回，否则较早加载的旧兼容组会遮住真实排版名称。
inline FT_Face QmResolveFontFaceName(const char *pRequested, const std::vector<FT_Face> &vFaces, const std::unordered_map<FT_Face, SQmFontFaceNames> &Names)
{
	if(pRequested == nullptr || pRequested[0] == '\0')
		return nullptr;
	FT_Face FamilyMatch = nullptr, LegacyMatch = nullptr, PostscriptMatch = nullptr;
	int FamilyScore = -1;
	for(FT_Face Face : vFaces)
	{
		const auto &Name = Names.at(Face);
		const std::string FullName = Name.m_Family + " " + Name.m_Style;
		if(QmFontNamesEqual(pRequested, FullName.c_str()))
			return Face;
		if(QmFontNamesEqual(pRequested, Name.m_Family.c_str()))
		{
			const int Score = QmFontNamesEqual(Name.m_Style.c_str(), "Regular") ? 3 : QmFontNamesEqual(Name.m_Style.c_str(), "Book") ? 2 :
											  QmFontNamesEqual(Name.m_Style.c_str(), "Normal")       ? 1 :
																		   0;
			if(Score > FamilyScore)
			{
				FamilyMatch = Face;
				FamilyScore = Score;
			}
		}
		if(LegacyMatch == nullptr && QmFontFaceMatchesLegacyName(Name, pRequested))
			LegacyMatch = Face;
		if(PostscriptMatch == nullptr && QmFontNamesEqual(pRequested, FT_Get_Postscript_Name(Face)))
			PostscriptMatch = Face;
	}
	return FamilyMatch != nullptr ? FamilyMatch : LegacyMatch != nullptr ? LegacyMatch :
									       PostscriptMatch;
}

// 族默认面必须能用配置名往返；无冲突时保留族名（可变字重），冲突时附加真实样式。
template<typename TResolver>
inline bool QmFontFamilySelectionConfig(FT_Face FamilyFace, const std::unordered_map<FT_Face, SQmFontFaceNames> &Names, TResolver &&ResolveConfig, std::string &Config)
{
	Config.clear();
	if(FamilyFace == nullptr)
		return false;
	const auto &Name = Names.at(FamilyFace);
	Config = ResolveConfig(Name.m_Family.c_str()) == FamilyFace ? Name.m_Family : Name.m_Family + " " + Name.m_Style;
	if(ResolveConfig(Config.c_str()) != FamilyFace)
	{
		Config.clear();
		return false;
	}
	return true;
}

// 缓存命中与未命中；新增 face 后统一失效，避免菜单每帧遍历整个字体池。
class CQmFontFaceLookupCache
{
	std::unordered_map<std::string, FT_Face> m_Faces;

public:
	void Reset() { m_Faces.clear(); }
	template<typename TResolver>
	FT_Face Resolve(const char *pName, TResolver &&Resolver)
	{
		if(pName == nullptr || pName[0] == '\0')
			return nullptr;
		const auto Found = m_Faces.find(pName);
		if(Found != m_Faces.end())
			return Found->second;
		FT_Face Face = Resolver(pName);
		// 商店预览与诊断名称可以变化，缓存必须有界。
		if(m_Faces.size() >= 64)
			m_Faces.clear();
		m_Faces.emplace(pName, Face);
		return Face;
	}
};

inline bool QmFontSelectionNames(const SQmFontFaceNames *pNames, std::string &Family, std::string &Style)
{
	Family.clear();
	Style.clear();
	if(pNames == nullptr || pNames->m_Family.empty())
		return false;
	Family = pNames->m_Family;
	Style = pNames->m_Style;
	return true;
}

inline SQmFontFaceNames QmFontFaceNames(FT_Face Face)
{
	if(Face == nullptr)
		return {};
	std::string Family = QmFontSfntName(Face, 1, Face->family_name);
	if(Family.empty())
		Family = QmFontSfntName(Face, 16, FT_Get_Postscript_Name(Face));
	return QmOrganizeFontFaceNames(
		std::move(Family), QmFontSfntName(Face, 2, Face->style_name),
		QmFontSfntName(Face, 16, nullptr), QmFontSfntName(Face, 17, nullptr), Face->num_faces > 1);
}

#endif
