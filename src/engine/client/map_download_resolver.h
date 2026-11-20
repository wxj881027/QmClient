#ifndef ENGINE_CLIENT_MAP_DOWNLOAD_RESOLVER_H
#define ENGINE_CLIENT_MAP_DOWNLOAD_RESOLVER_H

#include <engine/http.h>

#include <cstring>
#include <string>
#include <string_view>

enum class EQmMapDownloadSource
{
	// 服务器在 NETMSG_MAP_DETAILS 里直接给了地图 URL。
	SERVER_URL,
	// 有服务器下发的 SHA256，按官方 downloadedmaps/<名>_<sha>.map 规则走 CDN。
	CDN_HASH,
	// 只有地图名：先查 CDN 目录索引，再按索引里的哈希文件下载（仅预览/预下载）。
	CDN_NAME_INDEX,
	// 没有 SHA256 也没有 URL：官方会向服务器请求地图分块。
	SERVER_TRANSFER,
};

struct SQmMapDownloadRequest
{
	const char *m_pMapName = nullptr;
	// 64 位十六进制；为空表示服务器没给哈希。
	const char *m_pSha256Hex = nullptr;
	// 没有 SHA256 时官方文件名用 CRC；-1 表示未知。
	int m_Crc = -1;
	// 服务器提供的显式地图 URL，优先级最高。
	const char *m_pServerUrl = nullptr;
	const char *m_pBaseUrl = "https://maps.ddnet.org";
};

struct SQmMapDownloadPlan
{
	EQmMapDownloadSource m_Source = EQmMapDownloadSource::SERVER_TRANSFER;
	std::string m_Url;
	// 与官方 FormatMapDownloadFilename 一致：downloadedmaps/<名>_<sha|crc>.map
	std::string m_FileName;
	bool m_Valid = false;
};

inline bool QmMapNameIsSafe(const char *pMapName)
{
	if(pMapName == nullptr || pMapName[0] == '\0')
		return false;
	if(pMapName[0] == '/' || pMapName[0] == '\\')
		return false;
	if(std::string_view(pMapName).find("..") != std::string_view::npos)
		return false;
	if(std::strchr(pMapName, '/') != nullptr || std::strchr(pMapName, '\\') != nullptr)
		return false;
	return true;
}

inline bool QmMapDownloadHashIsHex(std::string_view Hash)
{
	return Hash.find_first_not_of("0123456789abcdefABCDEF") == std::string_view::npos;
}

inline bool QmMapSha256IsHex(const char *pSha256)
{
	const std::string_view Hash = pSha256 != nullptr ? pSha256 : "";
	return Hash.size() == 64 && QmMapDownloadHashIsHex(Hash);
}

// 地图名后的文件尾只允许 CRC、SHA256 或以 CRC 开头的双段哈希。
inline bool QmMapDownloadSuffixIsHashed(std::string_view Suffix)
{
	if(!Suffix.ends_with(".map"))
		return false;
	Suffix.remove_suffix(4);
	const size_t Separator = Suffix.find('_');
	if(Separator != std::string_view::npos)
	{
		if(Separator != 8 || !QmMapDownloadHashIsHex(Suffix.substr(0, Separator)))
			return false;
		Suffix.remove_prefix(Separator + 1);
	}
	return (Suffix.size() == 8 || Suffix.size() == 64) && QmMapDownloadHashIsHex(Suffix);
}

inline std::string QmMapDownloadUrlEncode(const char *pText)
{
	char aEscaped[2048];
	EscapeUrl(aEscaped, sizeof(aEscaped), pText);
	return aEscaped;
}

inline std::string QmMapDownloadBaseUrl(const char *pBaseUrl)
{
	std::string Base = pBaseUrl != nullptr && pBaseUrl[0] != '\0' ? pBaseUrl : "https://maps.ddnet.org";
	while(!Base.empty() && Base.back() == '/')
		Base.pop_back();
	return Base;
}

inline std::string QmMapDownloadFileName(const char *pMapName, const char *pSha256Hex, int Crc)
{
	if(!QmMapNameIsSafe(pMapName))
		return {};
	char aSuffix[96];
	if(QmMapSha256IsHex(pSha256Hex))
		str_copy(aSuffix, pSha256Hex);
	else if(Crc >= 0)
		str_format(aSuffix, sizeof(aSuffix), "%08x", Crc);
	else
		return {};
	return std::string("downloadedmaps/") + pMapName + "_" + aSuffix + ".map";
}

// 官方进服下载的解析规则。服务器 URL > SHA256 CDN > 服务器传输。
inline bool QmBuildMapDownloadPlan(const SQmMapDownloadRequest &Request, SQmMapDownloadPlan *pPlan)
{
	if(pPlan == nullptr)
		return false;
	const std::string FileName = QmMapDownloadFileName(Request.m_pMapName, Request.m_pSha256Hex, Request.m_Crc);
	if(FileName.empty())
		return false;

	*pPlan = SQmMapDownloadPlan{};
	pPlan->m_FileName = FileName;
	pPlan->m_Valid = true;
	if(Request.m_pServerUrl != nullptr && Request.m_pServerUrl[0] != '\0')
	{
		pPlan->m_Source = EQmMapDownloadSource::SERVER_URL;
		pPlan->m_Url = Request.m_pServerUrl;
		return true;
	}
	if(QmMapSha256IsHex(Request.m_pSha256Hex))
	{
		pPlan->m_Source = EQmMapDownloadSource::CDN_HASH;
		pPlan->m_Url = QmMapDownloadBaseUrl(Request.m_pBaseUrl) + "/" + QmMapDownloadUrlEncode(pPlan->m_FileName.c_str() + std::strlen("downloadedmaps/"));
		return true;
	}
	pPlan->m_Source = EQmMapDownloadSource::SERVER_TRANSFER;
	return true;
}

// 只有地图名时的回退：Href 必须是 CDN 索引中的 <URL 编码名>_<哈希>.map。
inline bool QmBuildIndexedMapDownloadPlan(const char *pMapName, const std::string_view Href, const char *pBaseUrl, SQmMapDownloadPlan *pPlan)
{
	if(pPlan == nullptr || !QmMapNameIsSafe(pMapName) || !str_valid_filename(pMapName))
		return false;
	const std::string Prefix = QmMapDownloadUrlEncode(pMapName) + "_";
	if(!Href.starts_with(Prefix))
		return false;
	const std::string_view Suffix = Href.substr(Prefix.size());
	if(!QmMapDownloadSuffixIsHashed(Suffix))
		return false;
	*pPlan = SQmMapDownloadPlan{};
	pPlan->m_Source = EQmMapDownloadSource::CDN_NAME_INDEX;
	pPlan->m_Url = QmMapDownloadBaseUrl(pBaseUrl) + "/" + std::string(Href);
	pPlan->m_FileName = std::string("downloadedmaps/") + pMapName + "_" + std::string(Suffix);
	pPlan->m_Valid = true;
	return true;
}

#endif
