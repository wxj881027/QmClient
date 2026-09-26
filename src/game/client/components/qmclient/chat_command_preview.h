// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_COMMAND_PREVIEW_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_COMMAND_PREVIEW_H

#include <base/str.h>

#include <game/localization.h>

#include <cstddef>

// 斜杠指令用法提示：聊天输入以 /xxx 开头时，在输入行下方显示一行小字说明。
// 这里只做纯文本解析与格式化（便于单测）；服务端下发的指令说明由调用方按命令名查出后传入。
namespace QmChatCommandPreview
{
	// 命令名与首个参数的长度上限
	constexpr int TOKEN_LENGTH = 128;
	// 其余参数（可能含引号包裹的玩家名或地图名）的长度上限
	constexpr int REST_LENGTH = 256;

	// 服务端下发的指令说明；没有命中指令表时由调用方传 nullptr
	struct SCommandInfo
	{
		const char *m_pName = nullptr;
		const char *m_pParams = nullptr;
		const char *m_pHelpText = nullptr;
	};

	inline bool NameIs(const char *pName, const char *pCommand)
	{
		return str_comp_nocase(pName, pCommand) == 0;
	}

	// 读取一个 token（支持双引号包裹与反斜杠转义），返回 token 之后的位置
	inline const char *ReadToken(const char *pText, char *pToken, size_t TokenSize)
	{
		if(TokenSize == 0)
			return pText;

		pToken[0] = '\0';
		if(pText == nullptr)
			return "";

		const char *pCursor = str_skip_whitespaces_const(pText);
		char *pDst = pToken;
		char *pEnd = pToken + TokenSize;

		if(*pCursor == '"')
		{
			pCursor++;
			while(*pCursor != '\0' && *pCursor != '"')
			{
				if(*pCursor == '\\' && pCursor[1] != '\0')
					pCursor++;
				if(pDst + 1 < pEnd)
					*pDst++ = *pCursor;
				pCursor++;
			}
			if(*pCursor == '"')
				pCursor++;
		}
		else
		{
			while(*pCursor != '\0' && !str_isspace(*pCursor))
			{
				if(pDst + 1 < pEnd)
					*pDst++ = *pCursor;
				pCursor++;
			}
		}

		*pDst = '\0';
		return pCursor;
	}

	// 读取 /命令 后的命令名；输入不是斜杠指令时返回 false
	inline bool ReadCommandName(const char *pInput, char *pName, size_t NameSize)
	{
		if(pInput == nullptr || pInput[0] != '/' || pInput[1] == '\0')
			return false;

		ReadToken(pInput + 1, pName, NameSize);
		return pName[0] != '\0';
	}

	// 读取 token 之后的全部内容；整体被引号包裹时去掉外层引号
	inline void CopyRest(const char *pText, char *pBuf, size_t BufSize)
	{
		if(BufSize == 0)
			return;

		pBuf[0] = '\0';
		if(pText == nullptr)
			return;

		const char *pRest = str_skip_whitespaces_const(pText);
		if(pRest[0] == '\0')
			return;

		if(pRest[0] == '"')
		{
			char aQuoted[TOKEN_LENGTH];
			const char *pAfterQuoted = ReadToken(pRest, aQuoted, sizeof(aQuoted));
			if(*str_skip_whitespaces_const(pAfterQuoted) == '\0')
			{
				str_copy(pBuf, aQuoted, BufSize);
				return;
			}
		}

		str_copy(pBuf, pRest, BufSize);
		str_utf8_trim_right(pBuf);
	}

	// 指令说明是服务端下发的英文原文，按当前语言本地化
	inline const char *LocalizeText(const char *pText)
	{
		if(pText == nullptr || pText[0] == '\0')
			return pText;

		return Localize(pText);
	}

	// 生成 /xxx 的用法提示；pCommand 为命中的服务端指令说明，未命中传 nullptr
	inline bool Build(const char *pInput, const SCommandInfo *pCommand, char *pBuf, size_t BufSize)
	{
		if(BufSize == 0)
			return false;

		pBuf[0] = '\0';
		const int PreviewBufSize = (int)BufSize;

		if(pInput == nullptr || pInput[0] != '/' || pInput[1] == '\0')
			return false;

		char aCommand[TOKEN_LENGTH];
		const char *pAfterCommand = ReadToken(pInput + 1, aCommand, sizeof(aCommand));
		if(aCommand[0] == '\0')
			return false;

		char aFirstArg[TOKEN_LENGTH];
		const char *pAfterFirstArg = ReadToken(pAfterCommand, aFirstArg, sizeof(aFirstArg));

		char aRestArg[REST_LENGTH];
		CopyRest(pAfterCommand, aRestArg, sizeof(aRestArg));

		char aRestAfterFirstArg[REST_LENGTH];
		CopyRest(pAfterFirstArg, aRestAfterFirstArg, sizeof(aRestAfterFirstArg));

		if(NameIs(aCommand, "points"))
		{
			if(aRestArg[0] != '\0')
				str_format(pBuf, PreviewBufSize, Localize("Query points for %s"), aRestArg);
			else
				str_format(pBuf, PreviewBufSize, Localize("Query points for %s"), Localize("yourself"));
			return true;
		}

		if(NameIs(aCommand, "rank"))
		{
			if(aRestArg[0] != '\0')
				str_format(pBuf, PreviewBufSize, Localize("Query rank for %s"), aRestArg);
			else
				str_format(pBuf, PreviewBufSize, Localize("Query rank for %s"), Localize("yourself"));
			return true;
		}

		if(NameIs(aCommand, "teamrank") || NameIs(aCommand, "rankteam"))
		{
			if(aRestArg[0] != '\0')
				str_format(pBuf, PreviewBufSize, Localize("Query team rank for %s"), aRestArg);
			else
				str_format(pBuf, PreviewBufSize, Localize("Query team rank for %s"), Localize("yourself"));
			return true;
		}

		if(NameIs(aCommand, "r") || NameIs(aCommand, "rescue"))
		{
			str_copy(pBuf, Localize("Rescue: auto mode teleports out of freeze; manual mode records a rescue point on landing and teleports when frozen"), BufSize);
			return true;
		}

		if(NameIs(aCommand, "w") || NameIs(aCommand, "whisper"))
		{
			if(aFirstArg[0] != '\0' && aRestAfterFirstArg[0] != '\0')
				str_format(pBuf, PreviewBufSize, Localize("Whisper to %s: %s"), aFirstArg, aRestAfterFirstArg);
			else if(aFirstArg[0] != '\0')
				str_format(pBuf, PreviewBufSize, Localize("Whisper to %s"), aFirstArg);
			else
				str_copy(pBuf, Localize("Whisper: /w player message"), BufSize);
			return true;
		}

		if(NameIs(aCommand, "c") || NameIs(aCommand, "converse"))
		{
			if(aRestArg[0] != '\0')
				str_format(pBuf, PreviewBufSize, Localize("Reply to the last whisper target: %s"), aRestArg);
			else
				str_copy(pBuf, Localize("Reply to the last whisper target"), BufSize);
			return true;
		}

		if(NameIs(aCommand, "mapinfo"))
		{
			if(aRestArg[0] != '\0')
				str_format(pBuf, PreviewBufSize, Localize("Query map info for %s"), aRestArg);
			else
				str_copy(pBuf, Localize("Query current map info"), BufSize);
			return true;
		}

		if(NameIs(aCommand, "team"))
		{
			if(aFirstArg[0] != '\0')
				str_format(pBuf, PreviewBufSize, Localize("Join team %s"), aFirstArg);
			else
				str_copy(pBuf, Localize("Show your current team"), BufSize);
			return true;
		}

		if(NameIs(aCommand, "lock"))
		{
			if(str_comp(aFirstArg, "0") == 0)
				str_copy(pBuf, Localize("Unlock the team"), BufSize);
			else
				str_copy(pBuf, Localize("Lock the team so other players cannot join directly"), BufSize);
			return true;
		}

		if(NameIs(aCommand, "invite"))
		{
			if(aRestArg[0] != '\0')
				str_format(pBuf, PreviewBufSize, Localize("Invite %s to the locked team"), aRestArg);
			else
				str_format(pBuf, PreviewBufSize, Localize("Invite %s to the locked team"), Localize("a player"));
			return true;
		}

		if(NameIs(aCommand, "swap"))
		{
			if(aRestArg[0] != '\0')
				str_format(pBuf, PreviewBufSize, Localize("Request to swap positions with %s"), aRestArg);
			else
				str_copy(pBuf, Localize("Request a position swap"), BufSize);
			return true;
		}

		if(NameIs(aCommand, "save"))
		{
			if(aRestArg[0] != '\0')
				str_format(pBuf, PreviewBufSize, Localize("Save the team as %s"), aRestArg);
			else
				str_copy(pBuf, Localize("Save the current team"), BufSize);
			return true;
		}

		if(NameIs(aCommand, "load"))
		{
			if(aRestArg[0] != '\0')
				str_format(pBuf, PreviewBufSize, Localize("Load save %s"), aRestArg);
			else
				str_copy(pBuf, Localize("Show existing saves"), BufSize);
			return true;
		}

		if(NameIs(aCommand, "settings"))
		{
			if(aFirstArg[0] != '\0')
				str_format(pBuf, PreviewBufSize, Localize("Query server setting: %s"), aFirstArg);
			else
				str_copy(pBuf, Localize("Show server settings"), BufSize);
			return true;
		}

		if(NameIs(aCommand, "help"))
		{
			if(aRestArg[0] != '\0')
				str_format(pBuf, PreviewBufSize, Localize("%s (/%s %s)"), Localize("Usage"), aCommand, aRestArg);
			else
				str_format(pBuf, PreviewBufSize, Localize("Usage: /%s %s"), aCommand, "");
			return true;
		}

		if(pCommand == nullptr)
			return false;

		const char *pHelpText = LocalizeText(pCommand->m_pHelpText);
		if(pHelpText != nullptr && pHelpText[0] != '\0')
		{
			if(pCommand->m_pParams != nullptr && pCommand->m_pParams[0] != '\0')
				str_format(pBuf, PreviewBufSize, Localize("%s (/%s %s)"), pHelpText, pCommand->m_pName, pCommand->m_pParams);
			else
				str_copy(pBuf, pHelpText, BufSize);
			return true;
		}

		if(pCommand->m_pParams != nullptr && pCommand->m_pParams[0] != '\0')
		{
			str_format(pBuf, PreviewBufSize, Localize("Usage: /%s %s"), pCommand->m_pName, pCommand->m_pParams);
			return true;
		}

		return false;
	}
}

#endif
