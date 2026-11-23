#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_INDEX_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_INDEX_H

#include <game/client/components/binds.h>

#include <cstdint>
#include <string>
#include <unordered_map>

// 命令 → 按键索引，设置页所有绑定行共用。
// 索引跟随 CBindStorage::Revision() 失效重建：cfg 载入、控制台 bind、其它页面
// 改键都会在下一帧被看到，不再依赖“进程内只扫描一次”的旧缓存。
class CCommandBindIndex
{
public:
	// 绑定表版本变化时重建整张索引；返回本次是否真的重建过。
	bool EnsureFresh(const CBindStorage &Storage);
	// 先按原文精确查找，找不到再按归一化命令查找（容纳 cfg 手写空白差异）。
	// pExactMatch 区分命中来源：只有精确命中才允许解绑旧键，否则会把另一条
	// 等价命令的按键一并删掉。
	CBindSlot Find(const char *pCommand, bool *pExactMatch = nullptr) const;
	void Clear();

private:
	std::unordered_map<std::string, CBindSlot> m_Exact;
	std::unordered_map<std::string, CBindSlot> m_Normalized;
	uint64_t m_Revision = 0;
	bool m_Valid = false;
};

inline char QmFoldAsciiLower(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// 去掉首尾空白，把引号外的连续空白压成一个空格，并把命令首词（控制台命令名）
// 转小写——控制台查找命令本就不区分大小写。引号内的聊天文本原样保留，避免
// "say \"Hello\"" 被归一化成另一条消息。
inline std::string QmNormalizeBindCommand(const char *pCommand)
{
	std::string Result;
	if(pCommand == nullptr)
		return Result;

	bool Quoted = false;
	bool FirstToken = true;
	bool PendingSpace = false;
	for(const char *pCursor = pCommand; *pCursor != '\0'; ++pCursor)
	{
		// 转义引号不是引号边界，原样保留。
		if(*pCursor == '\\' && pCursor[1] == '"')
		{
			Result += *pCursor;
			Result += *++pCursor;
			PendingSpace = false;
			continue;
		}
		if(*pCursor == '"')
		{
			Quoted = !Quoted;
			Result += *pCursor;
			PendingSpace = false;
			continue;
		}
		const bool IsSpace = !Quoted && (*pCursor == ' ' || *pCursor == '\t' || *pCursor == '\r' || *pCursor == '\n');
		if(IsSpace)
		{
			// 首部空白直接丢掉，词之间的空白留到下一个词真正出现时再补。
			if(!Result.empty())
				PendingSpace = true;
			continue;
		}
		if(PendingSpace)
		{
			Result += ' ';
			PendingSpace = false;
			FirstToken = false;
		}
		Result += (FirstToken && !Quoted) ? QmFoldAsciiLower(*pCursor) : *pCursor;
	}
	return Result;
}

inline void CCommandBindIndex::Clear()
{
	m_Exact.clear();
	m_Normalized.clear();
	m_Revision = 0;
	m_Valid = false;
}

inline bool CCommandBindIndex::EnsureFresh(const CBindStorage &Storage)
{
	const uint64_t Revision = Storage.Revision();
	if(m_Valid && Revision == m_Revision)
		return false;

	m_Exact.clear();
	m_Normalized.clear();
	m_Exact.reserve(64);
	m_Normalized.reserve(64);
	for(int Modifier = 0; Modifier < KeyModifier::COMBINATION_COUNT; ++Modifier)
	{
		for(int Key = KEY_FIRST; Key < KEY_LAST; ++Key)
		{
			const char *pBind = Storage.Get(Key, Modifier);
			if(!pBind[0])
				continue;
			// 同一命令绑了多个键时保留先扫到的那个，与旧缓存的 try_emplace 行为一致。
			m_Exact.try_emplace(pBind, CBindSlot(Key, Modifier));
			std::string Normalized = QmNormalizeBindCommand(pBind);
			if(!Normalized.empty())
				m_Normalized.try_emplace(std::move(Normalized), CBindSlot(Key, Modifier));
		}
	}
	m_Revision = Revision;
	m_Valid = true;
	return true;
}

inline CBindSlot CCommandBindIndex::Find(const char *pCommand, bool *pExactMatch) const
{
	if(pExactMatch != nullptr)
		*pExactMatch = false;
	if(pCommand == nullptr || pCommand[0] == '\0')
		return EMPTY_BIND_SLOT;

	const auto ExactIt = m_Exact.find(pCommand);
	if(ExactIt != m_Exact.end())
	{
		if(pExactMatch != nullptr)
			*pExactMatch = true;
		return ExactIt->second;
	}

	const std::string Normalized = QmNormalizeBindCommand(pCommand);
	if(Normalized.empty())
		return EMPTY_BIND_SLOT;
	const auto NormalizedIt = m_Normalized.find(Normalized);
	if(NormalizedIt != m_Normalized.end())
		return NormalizedIt->second;
	return EMPTY_BIND_SLOT;
}

// 全局索引：定义在 SettingsMenuWidgets.cpp，避免每张卡片各扫一遍绑定表。
CCommandBindIndex &QmCommandBindIndex();
// 用当前绑定表刷新全局索引（幂等，Revision 未变时直接返回）。
void QmRefreshCommandBindIndex(const CBinds &Binds);
// 查询命令当前绑定的按键；pExactMatch 语义见 CCommandBindIndex::Find。
CBindSlot QmLookupCommandBind(const CBinds &Binds, const char *pCommand, bool *pExactMatch = nullptr);

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_INDEX_H
