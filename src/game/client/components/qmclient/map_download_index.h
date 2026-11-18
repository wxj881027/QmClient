#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_DOWNLOAD_INDEX_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_DOWNLOAD_INDEX_H

#include <string>
#include <string_view>
#include <vector>

// 从 maps.ddnet.org 的目录索引里挑选某张地图的下载文件。
// EscapedPrefix 是 URL 编码后的地图名加下划线；同一张图可能保留多代文件，
// 取文件名最长的（通常带 crc + sha256），避免退回到旧的短哈希文件。
inline std::string QmSelectMapDownloadHref(const std::vector<std::string> &vHrefs, const std::string_view EscapedPrefix)
{
	std::string Best;
	for(const auto &Href : vHrefs)
	{
		if(Href.size() <= EscapedPrefix.size() || Href.compare(0, EscapedPrefix.size(), EscapedPrefix) != 0)
			continue;
		if(Href.size() < 4 || Href.compare(Href.size() - 4, 4, ".map") != 0)
			continue;
		if(Href.find('/') != std::string::npos)
			continue;
		if(Href.size() >= Best.size())
			Best = Href;
	}
	return Best;
}

#endif
