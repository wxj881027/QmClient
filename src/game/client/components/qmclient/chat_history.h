#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_HISTORY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_HISTORY_H

#include <base/str.h>

#include <engine/shared/ringbuffer.h>

#include <cstring>
#include <limits>
#include <string_view>

struct SQmChatHistoryEntry
{
	int m_Team;
	int m_OriginalOffset;
	char m_aText[1];
	const char *InputText() const { return m_aText + (m_OriginalOffset > 0 ? m_OriginalOffset : 0); }
};

// 输入借用须来自历史缓冲区之外；Allocate可能回收旧条目，不能在分配后继续读取旧条目。
inline bool QmAppendChatHistory(CTypedRingBuffer<SQmChatHistoryEntry> &History, int Team, std::string_view Text, std::string_view Original = {})
{
	constexpr size_t Limit = std::numeric_limits<int>::max();
	if(Text.size() > Limit - sizeof(SQmChatHistoryEntry))
		return false;
	size_t Size = sizeof(SQmChatHistoryEntry) + Text.size();
	if(!Original.empty())
	{
		if(Original.size() >= Limit - Size)
			return false;
		Size += Original.size() + 1;
	}
	auto *pEntry = History.Allocate(static_cast<int>(Size));
	if(pEntry == nullptr)
		return false;
	// 环形存储不会初始化复用的载荷，普通聊天和绑定都必须重写全部元数据。
	pEntry->m_Team = Team;
	pEntry->m_OriginalOffset = Original.empty() ? 0 : static_cast<int>(Text.size()) + 1;
	if(!Text.empty())
		std::memcpy(pEntry->m_aText, Text.data(), Text.size());
	pEntry->m_aText[Text.size()] = '\0';
	str_utf8_fix_truncation(pEntry->m_aText);
	if(!Original.empty())
	{
		std::memcpy(pEntry->m_aText + pEntry->m_OriginalOffset, Original.data(), Original.size());
		pEntry->m_aText[pEntry->m_OriginalOffset + Original.size()] = '\0';
		str_utf8_fix_truncation(pEntry->m_aText + pEntry->m_OriginalOffset);
	}
	return true;
}

#endif
