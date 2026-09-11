// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QM_CONSOLE_LOG_FILTER_H
#define GAME_CLIENT_COMPONENTS_QM_CONSOLE_LOG_FILTER_H

#include <base/math.h>
#include <base/str.h>

/**
 * 控制台日志分类与筛选（纯函数，便于单测）。
 *
 * 控制台顶栏的分类按钮是「风扇式多选」：每个类别独立点亮，点亮哪些就只显示哪些，全部点亮等价于不筛选。
 * 分类结果由每行日志里的 system 名决定（`<时间戳> <级别> <system>: <内容>`），而不是靠内容关键字猜。
 */

// 单行日志的归属类别（位标志，便于直接与筛选掩码做与运算）
enum
{
	QM_CONSOLE_LOG_CATEGORY_SYSTEM = 1 << 0, // 服务器与投票播报、网络、图形、存储、警告与报错
	QM_CONSOLE_LOG_CATEGORY_PLAYER = 1 << 1, // 玩家聊天、队伍聊天、私聊
	QM_CONSOLE_LOG_CATEGORY_COMMAND = 1 << 2, // 命令回复、exec/echo 回显、脚本与菜单输出
	QM_CONSOLE_LOG_CATEGORY_BINDS = 1 << 3, // bind / unbind 回显
};

// 全部分类
#define QM_CONSOLE_LOG_CATEGORY_ALL (QM_CONSOLE_LOG_CATEGORY_SYSTEM | QM_CONSOLE_LOG_CATEGORY_PLAYER | QM_CONSOLE_LOG_CATEGORY_COMMAND | QM_CONSOLE_LOG_CATEGORY_BINDS)

/**
 * 判断日志的 system 名是否等于给定名字或是其子分类。
 *
 * 例如 "binds" 与 "binds/foo" 匹配 "binds"，而 "bindsomething" 不匹配，
 * 避免前缀匹配把无关 system 吞进同一个分类。
 */
inline bool QmConsoleLogSystemIs(const char *pSystem, const char *pName)
{
	if(!pSystem || !pName || pName[0] == '\0')
		return false;
	const char *pRest = str_startswith_nocase(pSystem, pName);
	return pRest != nullptr && (*pRest == '\0' || *pRest == '/');
}

/**
 * 按日志的 system 名分类。
 *
 * 命令行 / 控制台直接打印的行没有独立的 system 参数，会带着 `"<system>: "` 前缀，
 * 这类行请先取出 system 名（见 QmClassifyConsoleLogLine）或退化为本函数。
 */
inline int QmClassifyConsoleLogSystem(const char *pSystem)
{
	if(!pSystem || pSystem[0] == '\0')
		return QM_CONSOLE_LOG_CATEGORY_SYSTEM;

	if(QmConsoleLogSystemIs(pSystem, "binds"))
		return QM_CONSOLE_LOG_CATEGORY_BINDS;

	// 玩家说话：公共、队伍、私聊
	if(QmConsoleLogSystemIs(pSystem, "chat/all") ||
		QmConsoleLogSystemIs(pSystem, "chat/team") ||
		QmConsoleLogSystemIs(pSystem, "chat/whisper"))
		return QM_CONSOLE_LOG_CATEGORY_PLAYER;

	// 服务器播报与投票、命令回复、exec/echo 回显、脚本与菜单输出
	if(QmConsoleLogSystemIs(pSystem, "chat/server") ||
		QmConsoleLogSystemIs(pSystem, "chatresp") ||
		QmConsoleLogSystemIs(pSystem, "console") ||
		QmConsoleLogSystemIs(pSystem, "chat/client"))
		return QM_CONSOLE_LOG_CATEGORY_COMMAND;

	return QM_CONSOLE_LOG_CATEGORY_SYSTEM;
}

/**
 * 从整行文本里取出 system 名写入 pBuf。
 *
 * 行格式为 `2026-09-11 20:45:42 I binds: bound w = +weapon1`；
 * 取 `": "` 之前、最后一个空格之后的片段即 system 名。取不到时返回 false。
 */
inline bool QmExtractConsoleLogSystem(const char *pLine, size_t Length, char *pBuf, size_t BufSize)
{
	if(!pLine || Length == 0 || !pBuf || BufSize == 0)
		return false;
	pBuf[0] = '\0';

	const char *pEnd = pLine + Length;
	const char *pIt = pLine;
	const char *pColon = nullptr;
	while(pIt + 1 < pEnd)
	{
		if(pIt[0] == ':' && pIt[1] == ' ')
		{
			pColon = pIt;
			break;
		}
		++pIt;
	}
	if(pColon == nullptr)
		return false;

	const char *pNameStart = pColon;
	while(pNameStart > pLine && pNameStart[-1] != ' ')
		--pNameStart;
	if(pNameStart == pColon)
		return false;

	const size_t NameLength = (size_t)(pColon - pNameStart);
	const size_t CopyLength = NameLength + 1 < BufSize ? NameLength + 1 : BufSize;
	str_copy(pBuf, pNameStart, CopyLength);
	return pBuf[0] != '\0';
}

/** 按整行文本分类；取不到 system 名时归入系统类。 */
inline int QmClassifyConsoleLogLine(const char *pLine, size_t Length)
{
	char aSystem[64];
	if(!QmExtractConsoleLogSystem(pLine, Length, aSystem, sizeof(aSystem)))
		return QM_CONSOLE_LOG_CATEGORY_SYSTEM;
	return QmClassifyConsoleLogSystem(aSystem);
}

/**
 * 把筛选掩码归一化到合法分类集合；空掩码退回全部分类。
 *
 * 空掩码会让控制台一行不剩，用户也没有可点的按钮能走出来，因此不保留这个状态。
 */
inline int QmNormalizeConsoleLogFilterMask(int Mask)
{
	const int Valid = Mask & QM_CONSOLE_LOG_CATEGORY_ALL;
	return Valid != 0 ? Valid : QM_CONSOLE_LOG_CATEGORY_ALL;
}

/** 该行是否应该显示在给定筛选掩码下。 */
inline bool QmConsoleLogCategoryPassesFilter(int Category, int FilterMask)
{
	return (Category & QmNormalizeConsoleLogFilterMask(FilterMask)) != 0;
}

#endif
