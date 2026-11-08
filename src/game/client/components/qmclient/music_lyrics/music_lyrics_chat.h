#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_CHAT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_CHAT_H

#include <game/client/components/qmclient/netease/netease_lyric_parser.h>

#include <string>
#include <string_view>

namespace QmMusicLyrics
{
	inline std::string CleanLyricChatText(std::string_view Text)
	{
		if(!NeteaseLyrics::IsValidUtf8(Text))
			return {};
		std::string Clean(Text);
		for(char &Ch : Clean)
			if(static_cast<unsigned char>(Ch) < 0x20 || Ch == 0x7f)
				Ch = ' ';
		const size_t Start = Clean.find_first_not_of(' ');
		if(Start == std::string::npos)
			return {};
		return Clean.substr(Start, Clean.find_last_not_of(' ') - Start + 1);
	}

	inline std::string LyricChatMessage(std::string_view Text)
	{
		const std::string Clean = CleanLyricChatText(Text);
		// 音符前缀避免以斜杠开头的歌词被当作聊天指令执行。
		return Clean.empty() ? std::string() : "♪ " + NeteaseLyrics::TruncateUtf8(Clean, 251);
	}
}

#endif
