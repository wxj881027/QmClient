#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_DOWNLOAD_INDEX_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_DOWNLOAD_INDEX_H

#include <engine/client/map_download_resolver.h>

#include <string>
#include <string_view>
#include <vector>

// 从 maps.ddnet.org 的目录索引里挑选某张地图的下载文件。
// EscapedPrefix 是 URL 编码后的地图名加下划线；同一张图可能保留多代文件，
// 取文件名最长的（通常带 crc + sha256），避免退回到旧的短哈希文件。
inline std::string QmSelectMapDownloadHref(const std::vector<std::string> &vHrefs, const std::string_view EscapedPrefix)
{
	std::string_view Best;
	for(const std::string_view Href : vHrefs)
	{
		if(!Href.starts_with(EscapedPrefix) || Href.find('/') != std::string_view::npos)
			continue;
		if(!QmMapDownloadSuffixIsHashed(Href.substr(EscapedPrefix.size())))
			continue;
		if(Href.size() >= Best.size())
			Best = Href;
	}
	return std::string(Best);
}

#endif
