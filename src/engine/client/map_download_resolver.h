#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_DOWNLOAD_RESOLVER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_DOWNLOAD_RESOLVER_H

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

inline bool QmMapSha256IsHex(const char *pSha256)
{
	if(pSha256 == nullptr || std::strlen(pSha256) != 64)
		return false;
	for(const char *p = pSha256; *p != '\0'; ++p)
	{
		const bool Hex = (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F');
		if(!Hex)
			return false;
	}
	return true;
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
	if(pPlan == nullptr || !QmMapNameIsSafe(pMapName) || Href.empty())
		return false;
	const std::string EscapedName = QmMapDownloadUrlEncode(pMapName);
	const std::string Prefix = EscapedName + "_";
	if(Href.size() <= Prefix.size() || Href.compare(0, Prefix.size(), Prefix) != 0)
		return false;
	if(Href.size() < 4 || Href.compare(Href.size() - 4, 4, ".map") != 0)
		return false;
	*pPlan = SQmMapDownloadPlan{};
	pPlan->m_Source = EQmMapDownloadSource::CDN_NAME_INDEX;
	pPlan->m_Url = QmMapDownloadBaseUrl(pBaseUrl) + "/" + std::string(Href);
	pPlan->m_FileName = std::string("downloadedmaps/") + pMapName + "_" + std::string(Href.substr(Prefix.size()));
	pPlan->m_Valid = true;
	return true;
}

#endif
