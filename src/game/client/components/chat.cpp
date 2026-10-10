/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "chat.h"

#include <base/log.h>
#include <base/log_color.h>

#include <engine/editor.h>
#include <engine/external/regex.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/csv.h>
#include <engine/textrender.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/QmUi/UiTokens.h>
#include <game/client/animstate.h>
#include <game/client/components/censor.h>
#include <game/client/components/console.h>
#include <game/client/components/qmclient/chat_command_hud_render.h>
#include <game/client/components/qmclient/chat_command_preview.h>
#include <game/client/components/qmclient/chat_gradient.h>
#include <game/client/components/qmclient/chat_input_layout.h>
#include <game/client/components/qmclient/chat_scrollbar.h>
#include <game/client/components/qmclient/colored_parts.h>
#include <game/client/components/qmclient/demo_display.h>
#include <game/client/components/qmclient/friend_heart_icon.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/qm_chat_avatar.h>
#include <game/client/components/qmclient/qm_title_color.h>
#include <game/client/components/qmclient/qm_title_style.h>
#include <game/client/components/scoreboard.h>
#include <game/client/components/skins.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <string>

char CChat::ms_aDisplayText[MAX_LINE_LENGTH] = "";

enum
{
	BLOCK_WORDS_MODE_REGEX = 0,
	BLOCK_WORDS_MODE_FULL,
	BLOCK_WORDS_MODE_BOTH
};

struct SQmChatEmojiCursorLayout
{
	CUIRect m_Rect;
	float m_RequiredHeight;
};

static void MeasureSolidFriendHeart(ITextRender *pTextRender, CTextCursor *pCursor)
{
	const EFontPreset PreviousPreset = pTextRender->GetFontPreset();
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
	pTextRender->TextEx(pCursor, QM_FRIEND_HEART_ICON_WITH_SPACE);
	pTextRender->SetFontPreset(PreviousPreset);
}

static void AppendSolidFriendHeart(ITextRender *pTextRender, STextContainerIndex &TextContainerIndex, CTextCursor *pCursor, const ColorRGBA &Color)
{
	const ColorRGBA PreviousColor = pTextRender->GetTextColor();
	const EFontPreset PreviousPreset = pTextRender->GetFontPreset();
	pTextRender->TextColor(Color);
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
	pTextRender->CreateOrAppendTextContainer(TextContainerIndex, pCursor, QM_FRIEND_HEART_ICON_WITH_SPACE);
	pTextRender->SetFontPreset(PreviousPreset);
	pTextRender->TextColor(PreviousColor);
}

static SQmChatEmojiCursorLayout LayoutQmChatEmoji(CTextCursor &Cursor, float Size)
{
	// 图片作为一个不可拆分的字符参与聊天行的换行和高度计算。
	const float TextLineHeight = Cursor.m_AlignedFontSize + Cursor.m_AlignedLineSpacing > 0.0f ?
					     Cursor.m_AlignedFontSize + Cursor.m_AlignedLineSpacing :
					     Cursor.m_FontSize;
	const float LineRight = Cursor.m_StartX + Cursor.m_LineWidth;
	float EmojiSize = QmChatEmojiFitSize(Size, LineRight - Cursor.m_X);
	if(EmojiSize <= 0.0f)
	{
		if(Cursor.m_LineWidth > 0.0f && Cursor.m_X > Cursor.m_StartX)
		{
			Cursor.m_X = Cursor.m_StartX;
			Cursor.m_Y += TextLineHeight;
			++Cursor.m_LineCount;
			EmojiSize = QmChatEmojiFitSize(Size, Cursor.m_LineWidth);
		}
		if(EmojiSize <= 0.0f)
			EmojiSize = Size;
	}

	const float AlignedFontSize = Cursor.m_AlignedFontSize > 0.0f ? Cursor.m_AlignedFontSize : Cursor.m_FontSize;
	// 表情按文字基线对齐时会向光标顶部伸出；把光标的图片位置下移同样的
	// 伸出量，使图片完整落在本行的垂直预算内，而不侵入上一行。
	Cursor.m_Y += maximum(0.0f, EmojiSize - AlignedFontSize);
	const CUIRect Rect = {Cursor.m_X, Cursor.m_Y + QmChatEmojiBaselineOffset(AlignedFontSize, EmojiSize), EmojiSize, EmojiSize};
	Cursor.m_X += EmojiSize;
	Cursor.m_LongestLineWidth = maximum(Cursor.m_LongestLineWidth, Cursor.m_X - Cursor.m_StartX);
	Cursor.m_MaxCharacterHeight = maximum(Cursor.m_MaxCharacterHeight, EmojiSize);
	return {Rect, maximum(Cursor.Height(), Rect.y - Cursor.m_StartY + EmojiSize)};
}

static int BlockWordsSeparatorLength(const char *pStr)
{
	const unsigned char C0 = (unsigned char)pStr[0];
	if(C0 == ',' || C0 == ';' || C0 == '|' || C0 == '\n' || C0 == '\r')
		return 1;
	if(C0 == 0xEF && (unsigned char)pStr[1] == 0xBC)
	{
		const unsigned char C2 = (unsigned char)pStr[2];
		if(C2 == 0x8C || C2 == 0x9B)
			return 3;
	}
	return 0;
}

static void ParseBlockWordsList(const char *pList, std::vector<std::string> &OutWords)
{
	OutWords.clear();
	if(!pList || pList[0] == '\0')
		return;

	char aBuf[1024];
	str_copy(aBuf, pList, sizeof(aBuf));
	char *pCursor = aBuf;

	while(*pCursor)
	{
		int SepLen = BlockWordsSeparatorLength(pCursor);
		while(*pCursor && SepLen > 0)
		{
			pCursor += SepLen;
			SepLen = BlockWordsSeparatorLength(pCursor);
		}

		char *pStart = pCursor;
		while(*pCursor && BlockWordsSeparatorLength(pCursor) == 0)
			pCursor++;

		if(pStart == pCursor)
			break;

		if(*pCursor)
		{
			const int CutLen = BlockWordsSeparatorLength(pCursor);
			*pCursor = '\0';
			pCursor += CutLen;
		}

		char *pToken = (char *)str_utf8_skip_whitespaces(pStart);
		str_utf8_trim_right(pToken);
		if(pToken[0] != '\0')
			OutWords.emplace_back(pToken);
	}
}

static void PushUniqueWord(std::vector<std::string> &OutWords, const std::string &Word)
{
	if(std::find(OutWords.begin(), OutWords.end(), Word) == OutWords.end())
		OutWords.push_back(Word);
}

namespace
{
	struct CBlockWordsCache
	{
		std::string m_List;
		int m_Mode = -1;
		std::vector<std::string> m_Words;
		std::vector<Regex> m_Regexes;
	};

	static constexpr const char *QM_CHAT_LOG_DIR = "qmclient/chat_log";
	static constexpr const char *QM_CHAT_LOG_PREFIX = "auto_chat_";
	static constexpr const char *QM_CHAT_LOG_EXTENSION = ".txt";

	struct SChatLogCleanupData
	{
		IStorage *m_pStorage = nullptr;
		time_t m_CutoffDate = 0;
	};

	static bool ExtractChatLogDate(const char *pFilename, time_t *pTimestamp)
	{
		const char *pDate = str_startswith(pFilename, QM_CHAT_LOG_PREFIX);
		if(pDate == nullptr || !str_endswith(pFilename, QM_CHAT_LOG_EXTENSION))
			return false;

		if(str_length(pFilename) != str_length(QM_CHAT_LOG_PREFIX) + 10 + str_length(QM_CHAT_LOG_EXTENSION))
			return false;

		char aDate[11];
		str_truncate(aDate, sizeof(aDate), pDate, 10);
		return timestamp_from_str(aDate, "%Y-%m-%d", pTimestamp);
	}

	static int ChatLogCleanupCallback(const char *pName, int IsDir, int DirType, void *pUser)
	{
		if(IsDir)
			return 0;

		SChatLogCleanupData *pData = (SChatLogCleanupData *)pUser;
		time_t FileDate = 0;
		if(!ExtractChatLogDate(pName, &FileDate) || FileDate >= pData->m_CutoffDate)
			return 0;

		char aFilename[IO_MAX_PATH_LENGTH];
		str_format(aFilename, sizeof(aFilename), "%s/%s", QM_CHAT_LOG_DIR, pName);
		if(!pData->m_pStorage->RemoveFile(aFilename, DirType))
			log_error("chat", "Failed to remove old chat log '%s'", aFilename);
		return 0;
	}

	static const char *ChatLogKind(int ClientId, int Team)
	{
		if(ClientId == -1)
			return "system";
		if(ClientId == -2)
			return "client";
		if(Team == TEAM_WHISPER_SEND)
			return "whisper-send";
		if(Team == TEAM_WHISPER_RECV)
			return "whisper-recv";
		if(Team == 1)
			return "team";
		return "public";
	}
} // namespace

static void UpdateBlockWordsCache(CBlockWordsCache &Cache)
{
	if(Cache.m_List == g_Config.m_QmBlockWordsList && Cache.m_Mode == g_Config.m_QmBlockWordsMode)
		return;

	Cache.m_List = g_Config.m_QmBlockWordsList;
	Cache.m_Mode = g_Config.m_QmBlockWordsMode;

	ParseBlockWordsList(Cache.m_List.c_str(), Cache.m_Words);
	Cache.m_Regexes.clear();

	if(Cache.m_Mode == BLOCK_WORDS_MODE_REGEX || Cache.m_Mode == BLOCK_WORDS_MODE_BOTH)
	{
		Cache.m_Regexes.reserve(Cache.m_Words.size());
		for(const auto &Pattern : Cache.m_Words)
		{
			Regex Re(Pattern);
			if(!Re.error().empty())
			{
				log_error("blocklist", "Invalid regex: %s", Pattern.c_str());
				continue;
			}
			Cache.m_Regexes.push_back(std::move(Re));
		}
	}
}

static bool ReplaceLiteralWords(std::string &Text, const std::vector<std::string> &Words, char Replacement, bool MultiReplace, std::vector<std::string> *pMatched)
{
	bool AnyReplaced = false;

	for(const auto &Word : Words)
	{
		if(Word.empty())
			continue;

		std::string Result;
		const char *pCursor = Text.c_str();
		const char *pMatchEnd = nullptr;
		bool WordReplaced = false;

		while(const char *pMatch = str_utf8_find_nocase(pCursor, Word.c_str(), &pMatchEnd))
		{
			Result.append(pCursor, pMatch - pCursor);
			if(MultiReplace)
				Result.append(pMatchEnd - pMatch, Replacement);
			else
				Result.push_back(Replacement);
			pCursor = pMatchEnd;
			WordReplaced = true;
		}

		if(WordReplaced)
		{
			Result.append(pCursor);
			Text.swap(Result);
			AnyReplaced = true;
			if(pMatched)
				PushUniqueWord(*pMatched, Word);
		}
	}

	return AnyReplaced;
}

static bool ReplaceRegexWords(std::string &Text, std::vector<Regex> &Regexes, char Replacement, bool MultiReplace, std::vector<std::string> *pMatched)
{
	bool AnyReplaced = false;

	for(Regex &Re : Regexes)
	{
		bool RegexMatched = false;
		std::string Result = Re.replace(Text, true, [&](const std::string &Str, int, int Group) {
			if(Group != 0)
				return std::string();
			RegexMatched = true;
			if(pMatched)
				PushUniqueWord(*pMatched, Str);
			if(MultiReplace)
				return std::string(Str.size(), Replacement);
			return std::string(1, Replacement);
		});

		if(RegexMatched)
		{
			Text.swap(Result);
			AnyReplaced = true;
		}
	}

	return AnyReplaced;
}

static bool ApplyBlockWords(std::string &Text, std::vector<std::string> *pMatched)
{
	if(!g_Config.m_QmBlockWordsEnabled || g_Config.m_QmBlockWordsList[0] == '\0')
		return false;

	static CBlockWordsCache s_Cache;
	UpdateBlockWordsCache(s_Cache);
	if(s_Cache.m_Words.empty())
		return false;

	const char Replacement = g_Config.m_QmBlockWordsReplacementChar[0] != '\0' ? g_Config.m_QmBlockWordsReplacementChar[0] : '*';
	const bool MultiReplace = g_Config.m_QmBlockWordsMultiReplace != 0;
	const int Mode = g_Config.m_QmBlockWordsMode;

	bool Replaced = false;
	if(Mode == BLOCK_WORDS_MODE_REGEX || Mode == BLOCK_WORDS_MODE_BOTH)
		Replaced |= ReplaceRegexWords(Text, s_Cache.m_Regexes, Replacement, MultiReplace, pMatched);
	if(Mode == BLOCK_WORDS_MODE_FULL || Mode == BLOCK_WORDS_MODE_BOTH)
		Replaced |= ReplaceLiteralWords(Text, s_Cache.m_Words, Replacement, MultiReplace, pMatched);

	return Replaced;
}

CChat::CLine::CLine()
{
	m_TextContainerIndex.Reset();
	m_BodyTextContainerIndex.Reset();
	m_QuadContainerIndex = -1;
	m_aYOffset[0] = -1.0f;
	m_aYOffset[1] = -1.0f;
	m_TextYOffset = 0.0f;
	m_ContentWidth = 0.0f;
	m_CutOffProgress = 0.0f;
	CChat::ResetPresentationState(m_Presentation);
	m_DiagnosticPresentationState = -1;
	m_DiagnosticCollapsedSkipLogged = false;
	m_DiagnosticInvalidTextLogged = false;
	m_ForceVisible = false;
	m_ConsoleSuppressed = false;
	m_ServerMessageClass = QmHudNotifications::EServerMessageClass::None;
}

void CChat::CLine::DeleteTextContainers(ITextRender *pTextRender)
{
	pTextRender->DeleteTextContainer(m_TextContainerIndex);
	pTextRender->DeleteTextContainer(m_BodyTextContainerIndex);
}

void CChat::CLine::Reset(CChat &This)
{
	DeleteTextContainers(This.TextRender());
	This.Graphics()->DeleteQuadContainer(m_QuadContainerIndex);
	m_QuadContainerIndex = -1;
	m_Initialized = false;
	m_Time = 0;
	m_aText[0] = '\0';
	m_aName[0] = '\0';
	m_aQmTitle[0] = '\0';
	m_ChatEmoji = EQmChatEmoji::NONE;
	m_ChatEmojiImageLayout = false;
	m_ChatEmojiRect = {};
	m_SponsorChatStyle = EQmSponsorChatStyle::NONE;
	m_RenderSponsorChatStyle = EQmSponsorChatStyle::NONE;
	m_SponsorTextBounds = {};
	m_QmTitleBobPadding = 0.0f;
	m_vTitleTextMetrics.clear();
	m_aYOffset[0] = -1.0f;
	m_aYOffset[1] = -1.0f;
	m_TextYOffset = 0.0f;
	m_ContentWidth = 0.0f;
	m_CutOffProgress = 0.0f;
	CChat::ResetPresentationState(m_Presentation);
	m_DiagnosticPresentationState = -1;
	m_DiagnosticCollapsedSkipLogged = false;
	m_DiagnosticInvalidTextLogged = false;
	m_Friend = false;
	m_ForceVisible = false;
	m_ConsoleSuppressed = false;
	m_ServerMessageClass = QmHudNotifications::EServerMessageClass::None;
	m_TimesRepeated = 0;
	m_vMergedAuthors.clear();
	m_pManagedTeeRenderInfo = nullptr;
	m_pExportMetadata.reset();
	m_pTranslateResponse = nullptr;

	// 递增翻译 ID，标记内容已变更
	// 溢出保护：跳过 0，避免与默认值冲突
	if(m_TranslationId < std::numeric_limits<unsigned int>::max())
		m_TranslationId++;
	else
		m_TranslationId = 1;
}

static float ClampPresentationProgress(float Value)
{
	return std::clamp(Value, 0.0f, 1.0f);
}

CChat::CChat()
{
	m_Mode = MODE_NONE;
	m_LastPresentationUpdateTime = 0;
	m_LargeAreaOpenTick = 0;
	m_LastPresentationShowLargeArea = false;
	m_PendingConsoleLineIndex = -1;

	m_Input.SetCalculateOffsetCallback([this]() { return m_IsInputCensored; });
	m_Input.SetDisplayTextCallback([this](char *pStr, size_t NumChars) {
		m_IsInputCensored = false;
		if(
			g_Config.m_ClStreamerMode &&
			(str_startswith(pStr, "/login ") ||
				str_startswith(pStr, "/register ") ||
				str_startswith(pStr, "/code ") ||
				str_startswith(pStr, "/timeout ") ||
				str_startswith(pStr, "/save ") ||
				str_startswith(pStr, "/load ")))
		{
			bool Censor = false;
			const size_t NumLetters = minimum(NumChars, sizeof(ms_aDisplayText) - 1);
			for(size_t i = 0; i < NumLetters; ++i)
			{
				if(Censor)
					ms_aDisplayText[i] = '*';
				else
					ms_aDisplayText[i] = pStr[i];
				if(pStr[i] == ' ')
				{
					Censor = true;
					m_IsInputCensored = true;
				}
			}
			ms_aDisplayText[NumLetters] = '\0';
			return ms_aDisplayText;
		}
		return pStr;
	});

	m_EmojiCompletionListLength = 0;
	m_aEmojiCompletionColon[0] = '\0';
}

float CChat::CalculateCutOffOffsetX(float Progress)
{
	if(!g_Config.m_QmChatAnimSlideOut)
		return 0.0f;
	return -24.0f * std::clamp(Progress, 0.0f, 1.0f);
}

void CChat::RegisterCommand(const char *pName, const char *pParams, const char *pHelpText)
{
	// Don't allow duplicate commands.
	for(const auto &Command : m_vServerCommands)
		if(str_comp(Command.m_aName, pName) == 0)
			return;

	m_vServerCommands.emplace_back(pName, pParams, pHelpText);
	m_ServerCommandsNeedSorting = true;
	++m_ServerCommandsRevision;
}

void CChat::UnregisterCommand(const char *pName)
{
	m_vServerCommands.erase(std::remove_if(m_vServerCommands.begin(), m_vServerCommands.end(), [pName](const CCommand &Command) { return str_comp(Command.m_aName, pName) == 0; }), m_vServerCommands.end());
	++m_ServerCommandsRevision;
}

const CChat::CCommand *CChat::FindServerCommand(const char *pName) const
{
	for(const CCommand &Command : m_vServerCommands)
	{
		if(str_comp_nocase(Command.m_aName, pName) == 0)
			return &Command;
	}
	return nullptr;
}

bool CChat::BuildCommandUsagePreview(const char *pInput, char *pBuf, size_t BufSize) const
{
	// 先按命令名查服务端下发的指令说明，再交给纯格式化模块生成提示文本
	char aCommand[QmChatCommandPreview::TOKEN_LENGTH];
	const QmChatCommandPreview::SCommandInfo *pCommandInfo = nullptr;
	QmChatCommandPreview::SCommandInfo CommandInfo;
	if(QmChatCommandPreview::ReadCommandName(pInput, aCommand, sizeof(aCommand)))
	{
		if(const CCommand *pCommand = FindServerCommand(aCommand))
		{
			CommandInfo.m_pName = pCommand->m_aName;
			CommandInfo.m_pParams = pCommand->m_aParams;
			CommandInfo.m_pHelpText = pCommand->m_aHelpText;
			pCommandInfo = &CommandInfo;
		}
	}
	return QmChatCommandPreview::Build(pInput, pCommandInfo, pBuf, BufSize);
}

bool CChat::HandleCommandHudInput(const IInput::CEvent &Event)
{
	if(!m_CommandHud.MatchesSource(m_ServerCommandsRevision))
	{
		m_CommandHud.Hide();
		return false;
	}
	const vec2 MousePos = GetChatMousePos();
	if((Event.m_Flags & IInput::FLAG_PRESS) && (Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN))
		return m_CommandHud.Scroll(MousePos.x, MousePos.y, Event.m_Key == KEY_MOUSE_WHEEL_UP ? -1 : 1, m_Input.GetString(), m_Input.GetCursorOffset());
	if(Event.m_Key != KEY_MOUSE_1)
		return false;

	if(Event.m_Flags & IInput::FLAG_PRESS)
	{
		if(!m_CommandHud.Press(MousePos.x, MousePos.y, m_Input.GetString(), m_Input.GetCursorOffset()))
			return false;
		// 列表按下不能同时开始输入框选字或聊天记录拖选。
		m_Input.GetMouseSelection()->m_Selecting = false;
		m_MouseIsPress = false;
		return true;
	}
	if(Event.m_Flags & IInput::FLAG_RELEASE)
	{
		const bool WasPressed = m_CommandHud.IsPressed();
		std::string Completion;
		size_t Cursor = 0;
		if(m_CommandHud.Release(MousePos.x, MousePos.y, m_Input.GetString(), m_Input.GetCursorOffset(), Completion, Cursor))
		{
			m_Input.Set(Completion.c_str());
			m_Input.SetCursorOffset(Cursor);
			m_Input.SelectNothing();
			m_CompletionChosen = -1;
			m_CompletionUsed = false;
			m_EmojiCompletionListLength = 0;
			m_aEmojiCompletionColon[0] = '\0';
		}
		return WasPressed;
	}
	return false;
}

void CChat::RenderCommandHud(float X, float Bottom, float Width, float FontSize, const CUIRect &ChatRect, const CUIRect &TargetRect)
{
	if(m_CommandHud.BeginUpdate(m_Input.GetString(), m_Input.GetCursorOffset(), g_Config.m_QmChatCommandCompletion != 0, m_ServerCommandsRevision))
	{
		for(const auto &Command : m_vServerCommands)
			m_CommandHud.AddServerCommand(Command.m_aName, Command.m_aParams, Localize(Command.m_aHelpText));
		m_CommandHud.EndUpdate();
	}

	const float Scale = ChatRect.w > 0.0f ? TargetRect.w / ChatRect.w : 1.0f;
	const float OffsetX = TargetRect.x - ChatRect.x * Scale;
	const float OffsetY = TargetRect.y - ChatRect.y * Scale;
	m_CommandHud.SetInputTransform(OffsetX, OffsetY, Scale);
	float ScreenLeft, ScreenTop, ScreenRight, ScreenBottom;
	Graphics()->GetScreen(&ScreenLeft, &ScreenTop, &ScreenRight, &ScreenBottom);
	const float ScreenMargin = 4.0f / maximum(Scale, 0.01f);
	Bottom = minimum(Bottom, ScreenBottom - ScreenMargin);
	m_CommandHud.SetLayout(X, Bottom, Width, maximum(0.0f, Bottom - ScreenTop - ScreenMargin), FontSize, ScreenLeft + ScreenMargin, ScreenRight - ScreenMargin);
	QmRenderChatCommandHud(m_CommandHud, Ui(), TextRender(), GetChatMousePos());
}

void CChat::RebuildChat()
{
	for(auto &Line : m_aLines)
	{
		if(!Line.m_Initialized)
			continue;
		RefreshMessageNamePrefix(Line.m_ClientId, Line.m_aName, sizeof(Line.m_aName), g_Config.m_QmChatHideSystemPrefix != 0);
		Line.DeleteTextContainers(TextRender());
		Graphics()->DeleteQuadContainer(Line.m_QuadContainerIndex);
		// recalculate sizes
		Line.m_aYOffset[0] = -1.0f;
		Line.m_aYOffset[1] = -1.0f;
		Line.m_ChatEmojiRect = {};
		Line.m_ContentWidth = 0.0f;
		Line.m_CutOffProgress = 0.0f;
		Line.m_Presentation.m_RenderYInitialized = false;
	}
}

void CChat::ClearLines()
{
	FlushPendingConsoleLine(true);
	for(auto &Line : m_aLines)
		Line.Reset(*this);
	m_SponsorChatRenderer.Reset(Graphics());
	m_BacklogCurLine = 0;
	m_ScrollbarDragging = false;
	m_ScrollbarDragOffset = 0.0f;
	m_ChatInputMapRect = {};
	m_TranslateButton.m_RectValid = false;
	m_TranslateButton.m_Input.Reset();
	m_LastMousePos.reset();
	m_MouseIsPress = false;
	m_MousePress = vec2(0.0f, 0.0f);
	m_MouseRelease = vec2(0.0f, 0.0f);
	m_PrevScoreBoardShowed = false;
	m_PrevShowChat = false;
	m_LastPresentationUpdateTime = 0;
	m_LargeAreaOpenTick = 0;
	m_LastPresentationShowLargeArea = false;
	// 清屏时直接丢弃 echo 重复计数，不清算：补出来的统计行会立刻被清掉。
	ResetPendingEchoRepeat();
}

int CChat::GetLineIndex(const CLine *pLine) const
{
	if(pLine == nullptr)
		return -1;

	// 计算指针在数组中的偏移量
	const CLine *pBegin = m_aLines;
	const CLine *pEnd = pBegin + MAX_LINES;

	if(pLine < pBegin || pLine >= pEnd)
		return -1; // 指针不在数组范围内

	return static_cast<int>(pLine - pBegin);
}

CChat::CLine *CChat::GetLineByIndex(int Index)
{
	if(Index < 0 || Index >= MAX_LINES)
		return nullptr;

	return &m_aLines[Index];
}

int CChat::CountInitializedLines() const
{
	int Count = 0;
	for(const CLine &Line : m_aLines)
	{
		if(Line.m_Initialized)
			++Count;
	}
	return Count;
}

int CChat::CountVisibleLinesFrom(int BacklogLine) const
{
	const SQmFocusModeDecisions Focus = GetQmFocusModeDecisions();
	const bool FocusHideChat = Focus.m_HidePlayerMessages;
	const bool FocusHideSystemInfoMessages = Focus.m_HideSystemInfoMessages;
	const bool FocusHideSystemPromptMessages = Focus.m_HideSystemPromptMessages;
	const bool FocusHideEcho = Focus.m_HideEchoMessages;

	int Count = 0;
	for(int i = BacklogLine; i < MAX_LINES; ++i)
	{
		const CLine &Line = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
		if(!Line.m_Initialized)
			break;
		const bool ServerMessageIsBasicInfo = Line.m_ServerMessageClass == QmHudNotifications::EServerMessageClass::BasicInfo;
		if(ShouldRenderFocusFilteredChatLine(FocusHideChat, FocusHideSystemInfoMessages, FocusHideSystemPromptMessages, FocusHideEcho, Line.m_ClientId, Line.m_ForceVisible, ServerMessageIsBasicInfo))
			++Count;
	}
	return Count;
}

void CChat::UpdatePresentationStates(int64_t Now, float DeltaSeconds, bool ShowLargeArea, bool ExtraAnimations)
{
	if(ShowLargeArea && !m_LastPresentationShowLargeArea)
		m_LargeAreaOpenTick = Now;
	else if(!ShowLargeArea)
		m_LargeAreaOpenTick = 0;
	m_LastPresentationShowLargeArea = ShowLargeArea;

	int RecallIndex = 0;
	for(int i = 0; i < MAX_LINES; ++i)
	{
		CLine &Line = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
		if(!Line.m_Initialized)
			break;

		const float RecallDelaySeconds = ShowLargeArea ? RecallIndex++ * CHAT_RECALL_STAGGER_SECONDS : 0.0f;
		UpdateLinePresentation(
			Line.m_Presentation,
			Line.m_Time,
			Now,
			DeltaSeconds,
			ShowLargeArea,
			Line.m_ForceVisible,
			m_LargeAreaOpenTick,
			RecallDelaySeconds,
			ExtraAnimations);

		if(Line.m_ClientId == SERVER_MSG && QmMacosGraphicsDiagnosticsEnabled())
		{
			const int PresentationState = static_cast<int>(Line.m_Presentation.m_State);
			if(Line.m_DiagnosticPresentationState != PresentationState)
			{
				char aPayload[384];
				str_format(aPayload, sizeof(aPayload), "event=server_message_presentation line_age_ms=%.0f class=%d state=%d force_visible=%d show_large=%d text_container=%d text_container_valid=%d render_alpha=%.3f layout_visibility=%.3f",
					ChatPresentationTicksToSeconds(Now - Line.m_Time) * 1000.0f,
					static_cast<int>(Line.m_ServerMessageClass), PresentationState, Line.m_ForceVisible ? 1 : 0, ShowLargeArea ? 1 : 0,
					Line.m_TextContainerIndex.m_Index, Line.m_TextContainerIndex.Valid() ? 1 : 0,
					Line.m_Presentation.m_RenderAlpha, Line.m_Presentation.m_LayoutVisibility);
				QmMacosGraphicsDiagnosticsLogPayload("perf/autodiag_chat", aPayload, Client());
				Line.m_DiagnosticPresentationState = PresentationState;
				Line.m_DiagnosticCollapsedSkipLogged = false;
			}
		}
	}
}

void CChat::InvalidateLineTranslation(CLine &Line)
{
	++Line.m_TranslationId;
}

void CChat::OnWindowResize()
{
	m_CommandHud.Hide();
	RebuildChat();
	m_SponsorChatRenderer.Reset(Graphics());
}

void CChat::OnShutdown()
{
	for(auto &Line : m_aLines)
		Line.DeleteTextContainers(TextRender());
	m_SponsorChatRenderer.Reset(Graphics());
}

void CChat::Reset()
{
	ClearLines();

	m_Show = false;
	m_CompletionUsed = false;
	m_CompletionChosen = -1;
	m_aCompletionBuffer[0] = 0;
	m_PlaceholderOffset = 0;
	m_PlaceholderLength = 0;
	m_EmojiCompletionListLength = 0;
	m_aEmojiCompletionColon[0] = '\0';
	m_pHistoryEntry = nullptr;
	m_PendingChatCounter = 0;
	m_LastChatSend = 0;
	m_CurrentLine = 0;
	m_IsInputCensored = false;
	m_EditingNewLine = true;
	m_aSavedInputText[0] = '\0';
	m_SavedInputPending = false;
	m_ServerSupportsCommandInfo = false;
	m_ServerCommandsNeedSorting = false;
	m_aCurrentInputText[0] = '\0';
	DisableMode();
	m_vServerCommands.clear();
	++m_ServerCommandsRevision;

	for(int64_t &LastSoundPlayed : m_aLastSoundPlayed)
		LastSoundPlayed = 0;
}

void CChat::OnRelease()
{
	m_CommandHud.Hide();
	FlushPendingConsoleLine(true);
	m_Show = false;
}

void CChat::OnStateChange(int NewState, int OldState)
{
	FlushPendingConsoleLine(true);
	if(OldState <= IClient::STATE_CONNECTING)
		Reset();
}

void CChat::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->SendChat(0, pResult->GetString(0));
}

void CChat::ConSayTeam(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->SendChat(1, pResult->GetString(0));
}

void CChat::ConChat(IConsole::IResult *pResult, void *pUserData)
{
	CChat *pChat = (CChat *)pUserData;
	const char *pMode = pResult->GetString(0);
	if(str_comp(pMode, "all") == 0)
		pChat->EnableMode(0);
	else if(str_comp(pMode, "team") == 0)
		pChat->EnableMode(1);
	else
		log_error("chat", "expected all or team as mode");

	if(pResult->GetString(1)[0])
	{
		pChat->m_Input.Set(pResult->GetString(1));
	}
	else if(g_Config.m_ClChatReset)
	{
		if(g_Config.m_QmChatSaveDraft && pChat->m_SavedInputPending)
		{
			pChat->m_Input.Set(pChat->m_aSavedInputText);
		}
		else
		{
			pChat->m_Input.Clear();
		}
	}

	if(!g_Config.m_QmChatSaveDraft)
	{
		pChat->m_SavedInputPending = false;
		pChat->m_aSavedInputText[0] = '\0';
	}
}

void CChat::ConShowChat(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->m_Show = pResult->GetInteger(0) != 0;
}

void CChat::SaveDraft()
{
	if(!g_Config.m_QmChatSaveDraft)
	{
		m_SavedInputPending = false;
		m_aSavedInputText[0] = '\0';
		return;
	}

	if(m_Input.GetString()[0] != '\0')
	{
		str_copy(m_aSavedInputText, m_Input.GetString(), sizeof(m_aSavedInputText));
		m_SavedInputPending = true;
	}
	else
	{
		m_SavedInputPending = false;
		m_aSavedInputText[0] = '\0';
	}
}

void CChat::ConEcho(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->Echo(pResult->GetString(0));
}

void CChat::ConClearChat(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->ClearLines();
}

void CChat::ConchainChatOld(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	((CChat *)pUserData)->RebuildChat();
}

void CChat::ConchainChatFontSize(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CChat *pChat = (CChat *)pUserData;
	pChat->EnsureCoherentWidth();
	pChat->RebuildChat();
}

void CChat::ConchainChatWidth(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CChat *pChat = (CChat *)pUserData;
	pChat->EnsureCoherentFontSize();
	pChat->RebuildChat();
}

void CChat::EchoLine(const char *pString, bool ForceVisible)
{
	AddLine(CLIENT_MSG, 0, pString, ForceVisible);
	if(pString != nullptr && pString[0] != '\0')
	{
		str_copy(m_aPendingEchoRepeat, pString);
		m_PendingEchoRepeatCount = 1;
		m_PendingEchoRepeatTime = time();
	}
}

bool CChat::GateEchoRepeat(const char *pString)
{
	// echo 合并始终生效（不受 qm_message_merge 影响）：相同文本在窗口内连续出现时返回 true，
	// 调用方什么都不做——控制台聊天栏与通知栏都不会被刷屏。
	const int WindowMs = std::clamp(g_Config.m_QmEchoMergeWindowMs, 0, 60000);
	const int64_t Now = time();
	if(WindowMs > 0 && pString != nullptr && pString[0] != '\0' && str_comp(m_aPendingEchoRepeat, pString) == 0 &&
		EchoRepeatWithinWindow(Now, m_PendingEchoRepeatTime, WindowMs))
	{
		++m_PendingEchoRepeatCount;
		m_PendingEchoRepeatTime = Now;
		return true;
	}

	// 换了一条 echo 或被关闭时，先把上一段的 [N] 统计收口。
	if(!HasPendingEchoRepeat())
		return false;

	// 用滑动窗口而不是「不同文本才收口」：否则重复段过长时末尾统计永远不落地。
	// 计数交给聊天渲染已有的 [N] 显示（CLIENT_MSG 用的是 "[N] 文本" 前缀，与玩家消息合并
	// 共用同一套计数），这里不再拼第二份后缀。
	char aText[sizeof(m_aPendingEchoRepeat)];
	str_copy(aText, m_aPendingEchoRepeat);
	const int RepeatCount = m_PendingEchoRepeatCount;
	const int64_t LastTime = m_PendingEchoRepeatTime;
	ResetPendingEchoRepeat();
	if(EchoRepeatWithinWindow(Now, LastTime, WindowMs))
		AddLine(CLIENT_MSG, 0, aText, false, std::nullopt, -1, RepeatCount);
	return false;
}

void CChat::Echo(const char *pString)
{
	// 合并判定放在最外层：被抑制的重复 echo 连 Console()->Print 都不会走到。
	if(GateEchoRepeat(pString))
		return;
	const bool FocusHideEcho = GetQmFocusModeDecisions().m_HideEchoMessages;
	const unsigned EchoColor = g_Config.m_ClMessageClientColor;
	if(!FocusHideEcho && GameClient()->m_QmHudNotifications.QueueEcho(pString, EchoColor))
	{
		char aBuf[1024];
		str_format(aBuf, sizeof(aBuf), "— %s", pString);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat/client", aBuf, color_cast<ColorRGBA>(ColorHSLA(EchoColor)));
		return;
	}
	EchoLine(pString, false);
}

void CChat::Echo(const char *pString, bool ForceVisible)
{
	if(GateEchoRepeat(pString))
		return;
	const bool FocusHideEcho = GetQmFocusModeDecisions().m_HideEchoMessages && !ForceVisible;
	const unsigned EchoColor = g_Config.m_ClMessageClientColor;
	if(!FocusHideEcho && GameClient()->m_QmHudNotifications.QueueEcho(pString, EchoColor))
	{
		char aBuf[1024];
		str_format(aBuf, sizeof(aBuf), "— %s", pString);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat/client", aBuf, color_cast<ColorRGBA>(ColorHSLA(EchoColor)));
		return;
	}
	EchoLine(pString, ForceVisible);
}

void CChat::OnConsoleInit()
{
	Console()->Register("say", "r[message]", CFGFLAG_CLIENT, ConSay, this, "Say in chat");
	Console()->Register("say_team", "r[message]", CFGFLAG_CLIENT, ConSayTeam, this, "Say in team chat");
	Console()->Register("chat", "s['team'|'all'] ?r[message]", CFGFLAG_CLIENT, ConChat, this, "Enable chat with all/team mode");
	Console()->Register("+show_chat", "", CFGFLAG_CLIENT, ConShowChat, this, "Show chat");
	Console()->Register("echo", "r[message]", CFGFLAG_CLIENT | CFGFLAG_STORE, ConEcho, this, "Echo the text in chat window");
	Console()->Register("clear_chat", "", CFGFLAG_CLIENT | CFGFLAG_STORE, ConClearChat, this, "Clear chat messages");
}

void CChat::OnInit()
{
	Reset();
	Console()->Chain("cl_chat_old", ConchainChatOld, this);
	Console()->Chain("cl_chat_size", ConchainChatFontSize, this);
	Console()->Chain("cl_chat_width", ConchainChatWidth, this);
	const char *apRoles[] = {"system", "client", "highlight", "team", "friend", "normal"};
	const char *apProperties[] = {"type", "angle", "center_x", "center_y", "range", "reverse"};
	for(const char *pRole : apRoles)
	{
		for(const char *pProperty : apProperties)
		{
			char aCommand[64];
			str_format(aCommand, sizeof(aCommand), "qm_chat_%s_gradient_%s", pRole, pProperty);
			Console()->Chain(aCommand, ConchainChatOld, this);
		}
	}
}

bool CChat::OnInput(const IInput::CEvent &Event)
{
	const bool ChatInputActive = m_Mode != MODE_NONE;
	if(!ChatInputActive)
	{
		m_TranslateButton.m_Input.Reset();
		return false;
	}

	const bool LanguageMenuOpen = m_LanguageMenuOpen || Ui()->IsPopupOpen(&m_LanguagePopupContext);
	const bool ChatLineMenuOpen = Ui()->IsPopupOpen(&m_ChatLinePopupContext);
	const bool AnyChatPopupOpen = LanguageMenuOpen || ChatLineMenuOpen;
	const bool CommandHudEnabled = m_CommandHud.Source() == CQmChatCommandHud::ESource::SERVER && g_Config.m_QmChatCommandCompletion;
	if(AnyChatPopupOpen || Input()->HasComposition() || GameClient()->m_Menus.IsActive() || GameClient()->m_HudEditor.IsActive() || !CommandHudEnabled)
		m_CommandHud.Hide();
	else if(HandleCommandHudInput(Event))
		return true;
	const bool IsWheelEvent = Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN;
	if(!AnyChatPopupOpen && (Event.m_Flags & IInput::FLAG_PRESS) && IsWheelEvent)
	{
		const float Height = 300.0f;
		const float Width = Height * Graphics()->ScreenAspect();
		const CUIRect ChatRect = {0.0f, 50.0f, std::min(Width, std::max(190.0f, g_Config.m_ClChatWidth + 32.0f)), 250.0f};
		float HistoryBottom = Height - (20.0f * FontSize() / 6.0f + (g_Config.m_QmStatusBar ? g_Config.m_QmStatusBarHeight : 0.0f));
		HistoryBottom -= FontSize() * (8.0f / 6.0f);
		const float HeightLimit = GameClient()->m_Scoreboard.IsActive() ? 180.0f : (m_PrevShowChat ? 50.0f : 200.0f);
		const vec2 MousePos = GetChatLocalMousePos();
		const bool InsideHistory = MousePos.x >= ChatRect.x && MousePos.x <= ChatRect.x + ChatRect.w && MousePos.y >= HeightLimit && MousePos.y <= HistoryBottom;
		const bool InsideTranslateButton =
			m_TranslateButton.m_RectValid &&
			MousePos.x >= m_TranslateButton.m_X &&
			MousePos.x <= m_TranslateButton.m_X + m_TranslateButton.m_W &&
			MousePos.y >= m_TranslateButton.m_Y &&
			MousePos.y <= m_TranslateButton.m_Y + m_TranslateButton.m_H;
		if(InsideHistory && !InsideTranslateButton && !m_ScrollbarDragging)
		{
			const int TotalLines = CountInitializedLines();
			const int Direction = Event.m_Key == KEY_MOUSE_WHEEL_UP ? 1 : -1;
			m_BacklogCurLine = ClampBacklogLine(m_BacklogCurLine + Direction, TotalLines, 1);
			RebuildChat();
			return true;
		}
	}

	// 翻译按钮只按鼠标按钮分工，不依据聊天内容切换左键的动作。
	const vec2 ButtonMousePos = GetChatLocalMousePos();
	const bool InsideButton = m_TranslateButton.m_RectValid &&
				  ButtonMousePos.x >= m_TranslateButton.m_X && ButtonMousePos.x <= m_TranslateButton.m_X + m_TranslateButton.m_W &&
				  ButtonMousePos.y >= m_TranslateButton.m_Y && ButtonMousePos.y <= m_TranslateButton.m_Y + m_TranslateButton.m_H;
	const auto ButtonAction = m_TranslateButton.m_Input.Update(Event.m_Key, Event.m_Flags, InsideButton, !AnyChatPopupOpen && m_TranslateButton.m_RectValid);
	if(ButtonAction != CQmChatTranslateButton::EAction::NONE)
	{
		if(ButtonAction == CQmChatTranslateButton::EAction::OPEN_SETTINGS)
			OpenLanguageMenu();
		else if(ButtonAction == CQmChatTranslateButton::EAction::TOGGLE_AUTO)
			ToggleAutoTranslate();
		else if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			if(CLineInput::SMouseSelection *pMouseSel = m_Input.GetMouseSelection())
			{
				pMouseSel->m_Selecting = false;
				pMouseSel->m_PressMouse = vec2(0, 0);
				pMouseSel->m_ReleaseMouse = vec2(0, 0);
			}
		}
		return true;
	}

	// 聊天弹窗打开时，键盘确认/取消只作用于弹窗，不能穿透到聊天提交/关闭。
	if(AnyChatPopupOpen && (Event.m_Flags & IInput::FLAG_PRESS) && (Event.m_Key == KEY_ESCAPE || Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER))
	{
		if(LanguageMenuOpen)
			CloseLanguageMenu();
		if(ChatLineMenuOpen)
			CloseChatLineMenu();
		return true;
	}

	// ESC 键处理：优先关闭弹出菜单
	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		if(m_LanguageMenuOpen || Ui()->IsPopupOpen(&m_LanguagePopupContext))
		{
			CloseLanguageMenu();
			return true;
		}
		if(Ui()->IsPopupOpen(&m_ChatLinePopupContext))
		{
			CloseChatLineMenu();
			return true;
		}

		DisableMode();
		GameClient()->OnRelease();
		if(g_Config.m_ClChatReset)
		{
			SaveDraft();
			m_Input.Clear();
			m_pHistoryEntry = nullptr;
			m_EditingNewLine = true;
		}
		else if(!g_Config.m_QmChatSaveDraft)
		{
			m_SavedInputPending = false;
			m_aSavedInputText[0] = '\0';
		}
	}
	else if(Event.m_Flags & IInput::FLAG_PRESS && (Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER))
	{
		if(Input()->HasComposition())
		{
			return true;
		}

		if(m_ServerCommandsNeedSorting)
		{
			std::sort(m_vServerCommands.begin(), m_vServerCommands.end());
			m_ServerCommandsNeedSorting = false;
		}

		if(GameClient()->m_BindChat.ChatDoBinds(m_Input.GetString()))
			; // Do nothing as bindchat was executed
		else if(GameClient()->m_TClient.ChatDoSpecId(m_Input.GetString()))
			; // Do nothing as specid was executed
		else
			SendChatQueued(m_Input.GetString());
		m_SavedInputPending = false;
		m_aSavedInputText[0] = '\0';
		m_pHistoryEntry = nullptr;
		m_EditingNewLine = true;
		DisableMode();
		GameClient()->OnRelease();
		m_Input.Clear();
	}
	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_TAB)
	{
		const bool ShiftPressed = Input()->ShiftIsPressed();

		// fill the completion buffer
		if(!m_CompletionUsed)
		{
			const char *pCursor = m_Input.GetString() + m_Input.GetCursorOffset();
			for(size_t Count = 0; Count < m_Input.GetCursorOffset() && *(pCursor - 1) != ' '; --pCursor, ++Count)
				;
			m_PlaceholderOffset = pCursor - m_Input.GetString();

			for(m_PlaceholderLength = 0; *pCursor && *pCursor != ' '; ++pCursor)
				++m_PlaceholderLength;

			str_truncate(m_aCompletionBuffer, sizeof(m_aCompletionBuffer), m_Input.GetString() + m_PlaceholderOffset, m_PlaceholderLength);
		}

		// 表情补全：半角/全角冒号前缀，行为对齐官方 Tab 循环补全
		const int EmojiColonLength = QmChatEmojiColonUtf8Length(m_aCompletionBuffer);
		const bool EmojiCompletionCandidate = EmojiColonLength > 0;
		if(EmojiCompletionCandidate && !m_CompletionUsed)
		{
			const char *pEmojiPrefix = m_aCompletionBuffer + EmojiColonLength;
			m_EmojiCompletionListLength = QmChatEmojiCollectByPrefix(pEmojiPrefix, m_apEmojiCompletionList, (int)QM_CHAT_EMOJI_COUNT);
			str_truncate(m_aEmojiCompletionColon, sizeof(m_aEmojiCompletionColon), m_aCompletionBuffer, EmojiColonLength);
		}

		// 无命令前缀时构建玩家名候选；表情有匹配时优先走表情补全，否则仍可回退到玩家名
		if(!m_CompletionUsed && m_aCompletionBuffer[0] != '/' && !(EmojiCompletionCandidate && m_EmojiCompletionListLength > 0))
		{
			// Create the completion list of player names through which the player can iterate
			const char *PlayerName, *FoundInput;
			m_PlayerCompletionListLength = 0;
			for(auto &PlayerInfo : GameClient()->m_Snap.m_apInfoByName)
			{
				if(PlayerInfo)
				{
					PlayerName = GameClient()->m_aClients[PlayerInfo->m_ClientId].m_aName;
					FoundInput = str_utf8_find_nocase(PlayerName, m_aCompletionBuffer);
					if(FoundInput != nullptr)
					{
						m_aPlayerCompletionList[m_PlayerCompletionListLength].m_ClientId = PlayerInfo->m_ClientId;
						// The score for suggesting a player name is determined by the distance of the search input to the beginning of the player name
						m_aPlayerCompletionList[m_PlayerCompletionListLength].m_Score = (int)(FoundInput - PlayerName);
						m_PlayerCompletionListLength++;
					}
				}
			}
			std::stable_sort(m_aPlayerCompletionList, m_aPlayerCompletionList + m_PlayerCompletionListLength,
				[](const CRateablePlayer &Player1, const CRateablePlayer &Player2) -> bool {
					return Player1.m_Score < Player2.m_Score;
				});
		}

		if(m_aCompletionBuffer[0] == '/' && !m_vServerCommands.empty())
		{
			CCommand *pCompletionCommand = nullptr;

			const size_t NumCommands = m_vServerCommands.size();

			if(ShiftPressed && m_CompletionUsed)
				m_CompletionChosen--;
			else if(!ShiftPressed)
				m_CompletionChosen++;
			m_CompletionChosen = (m_CompletionChosen + 2 * NumCommands) % (2 * NumCommands);

			m_CompletionUsed = true;

			const char *pCommandStart = m_aCompletionBuffer + 1;
			for(size_t i = 0; i < 2 * NumCommands; ++i)
			{
				int SearchType;
				int Index;

				if(ShiftPressed)
				{
					SearchType = ((m_CompletionChosen - i + 2 * NumCommands) % (2 * NumCommands)) / NumCommands;
					Index = (m_CompletionChosen - i + NumCommands) % NumCommands;
				}
				else
				{
					SearchType = ((m_CompletionChosen + i) % (2 * NumCommands)) / NumCommands;
					Index = (m_CompletionChosen + i) % NumCommands;
				}

				auto &Command = m_vServerCommands[Index];

				if(str_startswith_nocase(Command.m_aName, pCommandStart))
				{
					pCompletionCommand = &Command;
					m_CompletionChosen = Index + SearchType * NumCommands;
					break;
				}
			}

			// 插入命令
			if(pCompletionCommand)
			{
				char aBuf[MAX_LINE_LENGTH];
				// 添加补全项之前的内容
				str_truncate(aBuf, sizeof(aBuf), m_Input.GetString(), m_PlaceholderOffset);

				// 添加命令
				str_append(aBuf, "/");
				str_append(aBuf, pCompletionCommand->m_aName);

				// 添加分隔符
				const char *pSeparator = pCompletionCommand->m_aParams[0] == '\0' ? "" : " ";
				str_append(aBuf, pSeparator);

				// 添加补全项之后的内容
				str_append(aBuf, m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength);

				m_PlaceholderLength = str_length(pSeparator) + str_length(pCompletionCommand->m_aName) + 1;
				m_Input.Set(aBuf);
				m_Input.SetCursorOffset(m_PlaceholderOffset + m_PlaceholderLength);
			}
		}
		else if(EmojiCompletionCandidate && m_EmojiCompletionListLength > 0)
		{
			const int NumEmojis = m_EmojiCompletionListLength;
			if(ShiftPressed && m_CompletionUsed)
				m_CompletionChosen--;
			else if(!ShiftPressed)
				m_CompletionChosen++;
			if(m_CompletionChosen < 0)
				m_CompletionChosen += NumEmojis;
			m_CompletionChosen %= NumEmojis;
			m_CompletionUsed = true;

			const SQmChatEmojiDefinition *pCompletionEmoji = m_apEmojiCompletionList[m_CompletionChosen];
			if(pCompletionEmoji != nullptr && pCompletionEmoji->m_pText != nullptr)
			{
				char aBuf[MAX_LINE_LENGTH];
				str_truncate(aBuf, sizeof(aBuf), m_Input.GetString(), m_PlaceholderOffset);

				// 保留用户输入的半角/全角冒号类型
				str_append(aBuf, m_aEmojiCompletionColon);
				str_append(aBuf, pCompletionEmoji->m_pText + 1);

				str_append(aBuf, m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength);

				// m_aEmojiCompletionColon 可能为 3 字节全角冒号，不能用 str_length 假定 1
				m_PlaceholderLength = str_length(m_aEmojiCompletionColon) + str_length(pCompletionEmoji->m_pText + 1);
				m_Input.Set(aBuf);
				m_Input.SetCursorOffset(m_PlaceholderOffset + m_PlaceholderLength);
			}
		}
		else
		{
			// find next possible name
			const char *pCompletionString = nullptr;
			if(m_PlayerCompletionListLength > 0)
			{
				// We do this in a loop, if a player left the game during the repeated pressing of Tab, they are skipped
				CGameClient::CClientData *pCompletionClientData;
				for(int i = 0; i < m_PlayerCompletionListLength; ++i)
				{
					if(ShiftPressed && m_CompletionUsed)
					{
						m_CompletionChosen--;
					}
					else if(!ShiftPressed)
					{
						m_CompletionChosen++;
					}
					if(m_CompletionChosen < 0)
					{
						m_CompletionChosen += m_PlayerCompletionListLength;
					}
					m_CompletionChosen %= m_PlayerCompletionListLength;
					m_CompletionUsed = true;

					pCompletionClientData = &GameClient()->m_aClients[m_aPlayerCompletionList[m_CompletionChosen].m_ClientId];
					if(!pCompletionClientData->m_Active)
					{
						continue;
					}

					pCompletionString = pCompletionClientData->m_aName;
					break;
				}
			}

			// insert the name
			if(pCompletionString)
			{
				char aBuf[MAX_LINE_LENGTH];
				// add part before the name
				str_truncate(aBuf, sizeof(aBuf), m_Input.GetString(), m_PlaceholderOffset);

				// 必要时为命令参数中的玩家名加引号
				char aQuoted[128];
				if(m_Input.GetString()[0] == '/' && (str_find(pCompletionString, " ") || str_find(pCompletionString, "\"")))
				{
					// 转义玩家名
					str_copy(aQuoted, "\"");
					char *pDst = aQuoted + str_length(aQuoted);
					str_escape(&pDst, pCompletionString, aQuoted + sizeof(aQuoted));
					str_append(aQuoted, "\"");

					pCompletionString = aQuoted;
				}

				// add the name
				str_append(aBuf, pCompletionString);

				// add separator
				const char *pSeparator = "";
				if(*(m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength) != ' ')
					pSeparator = m_PlaceholderOffset == 0 ? ": " : " ";
				else if(m_PlaceholderOffset == 0)
					pSeparator = ":";
				if(*pSeparator)
					str_append(aBuf, pSeparator);

				// add part after the name
				str_append(aBuf, m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength);

				m_PlaceholderLength = str_length(pSeparator) + str_length(pCompletionString);
				m_Input.Set(aBuf);
				m_Input.SetCursorOffset(m_PlaceholderOffset + m_PlaceholderLength);
			}
		}
	}
	else
	{
		// reset name completion process
		if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key != KEY_TAB && Event.m_Key != KEY_LSHIFT && Event.m_Key != KEY_RSHIFT)
		{
			m_CompletionChosen = -1;
			m_CompletionUsed = false;
			m_EmojiCompletionListLength = 0;
			m_aEmojiCompletionColon[0] = '\0';
		}

		m_Input.ProcessInput(Event);
	}

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_UP)
	{
		if(m_EditingNewLine)
		{
			str_copy(m_aCurrentInputText, m_Input.GetString());
			m_EditingNewLine = false;
		}

		if(m_pHistoryEntry)
		{
			CHistoryEntry *pTest = m_History.Prev(m_pHistoryEntry);

			if(pTest)
			{
				m_pHistoryEntry = pTest;
			}
		}
		else
		{
			m_pHistoryEntry = m_History.Last();
		}

		if(m_pHistoryEntry)
		{
			m_Input.Set(m_pHistoryEntry->m_aText);
		}
	}
	else if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_DOWN)
	{
		if(m_pHistoryEntry)
			m_pHistoryEntry = m_History.Next(m_pHistoryEntry);

		if(m_pHistoryEntry)
		{
			m_Input.Set(m_pHistoryEntry->m_aText);
		}
		else if(!m_EditingNewLine)
		{
			m_Input.Set(m_aCurrentInputText);
			m_EditingNewLine = true;
		}
	}

	return true;
}

void CChat::EnableMode(int Team)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	if(m_Mode == MODE_NONE)
	{
		if(Team)
			m_Mode = MODE_TEAM;
		else
			m_Mode = MODE_ALL;

		Input()->Clear();
		m_CompletionChosen = -1;
		m_CompletionUsed = false;
		m_EmojiCompletionListLength = 0;
		m_aEmojiCompletionColon[0] = '\0';
		m_Input.Activate(EInputPriority::CHAT);
	}
}

void CChat::DisableMode()
{
	m_CommandHud.Hide();
	CloseLanguageMenu();
	CloseChatLineMenu();
	if(m_Mode != MODE_NONE)
	{
		m_Mode = MODE_NONE;
		m_Input.Deactivate();
	}
}

void CChat::OnMessage(int MsgType, void *pRawMsg)
{
	OnMessage(MsgType, pRawMsg, -1);
}

void CChat::OnMessage(int MsgType, void *pRawMsg, int SourceConnection)
{
	if(GameClient()->m_SuppressEvents)
		return;

	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		if(Client()->State() != IClient::STATE_DEMOPLAYBACK &&
			GameClient()->IsLocalClientId(pMsg->m_ClientId) &&
			IsSensitiveChatCommand(pMsg->m_pMessage))
			return;

		auto &Re = GameClient()->m_TClient.m_RegexChatIgnore;
		if(Re.error().empty() && Re.test(pMsg->m_pMessage))
			return;

		if(pMsg->m_ClientId == SERVER_MSG && g_Config.m_ClShowChatSystem)
		{
			const auto PrintSuppressedServerMessage = [this, pMsg]() {
				if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
					StoreSave(pMsg->m_pMessage);
				char aBuf[1024];
				str_copy(aBuf, pMsg->m_pMessage);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat/server", aBuf, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor)));
			};
			const SQmFocusModeDecisions Focus = GetQmFocusModeDecisions();
			const bool FocusHideSystemInfoMessages = Focus.m_HideSystemInfoMessages;
			const bool FocusHideSystemPromptMessages = Focus.m_HideSystemPromptMessages;
			QmHudNotifications::SServerMessageAnalysis ServerMessageAnalysis;
			const bool ServerMessageHandled = GameClient()->m_QmHudNotifications.HandleServerChat(pMsg->m_pMessage, g_Config.m_QmHudNotificationsSystem != 0, &ServerMessageAnalysis);
			char aLocalizedServerMessage[1024];
			const bool ServerMessageLocalized = QmHudNotifications::TryFormatLocalizedServerChatMessage(pMsg->m_pMessage, aLocalizedServerMessage, sizeof(aLocalizedServerMessage));
			const char *pDisplayMessage = ServerMessageLocalized ? aLocalizedServerMessage : pMsg->m_pMessage;
			if(QmMacosGraphicsDiagnosticsEnabled())
			{
				char aPayload[256];
				str_format(aPayload, sizeof(aPayload), "event=server_message_received handled=%d show_chat_system=%d notifications=%d focus_hide_info=%d focus_hide_prompt=%d route=%d class=%d text_len=%d",
					ServerMessageHandled ? 1 : 0, g_Config.m_ClShowChatSystem, g_Config.m_QmHudNotificationsSystem,
					FocusHideSystemInfoMessages ? 1 : 0, FocusHideSystemPromptMessages ? 1 : 0,
					static_cast<int>(ServerMessageAnalysis.m_Route), static_cast<int>(ServerMessageAnalysis.m_Class), str_length(pMsg->m_pMessage));
				QmMacosGraphicsDiagnosticsLogPayload("perf/autodiag_chat", aPayload, Client());
			}
			if(g_Config.m_QmAutoAcceptTeamInvite &&
				Client()->State() != IClient::STATE_DEMOPLAYBACK &&
				ServerMessageAnalysis.m_Domain == QmHudNotifications::EServerMessageDomain::Team &&
				ServerMessageAnalysis.m_Class == QmHudNotifications::EServerMessageClass::Prompt)
			{
				const char *pRaw = pMsg->m_pMessage;
				char aTeamNum[32] = {};
				if(pRaw[0] == '\'' && str_find(pRaw + 1, "' invited you to team ") != nullptr)
				{
					const char *pTeamPos = str_find(pRaw, "' invited you to team ");
					const char *pUsePos = pTeamPos ? str_find(pTeamPos + str_length("' invited you to team "), ". Use /team ") : nullptr;
					if(pTeamPos && pUsePos)
						str_truncate(aTeamNum, sizeof(aTeamNum), pTeamPos + str_length("' invited you to team "), pUsePos - (pTeamPos + str_length("' invited you to team ")));
				}
				else if(pRaw[0] == '\'' && str_find(pRaw + 1, "' 邀请你加入 ") != nullptr && str_find(pRaw, "。输入 /team ") != nullptr)
				{
					const char *pTeamPos = str_find(pRaw, "' 邀请你加入 ");
					const char *pUsePos = pTeamPos ? str_find(pTeamPos + str_length("' 邀请你加入 "), "。输入 /team ") : nullptr;
					if(pTeamPos && pUsePos)
					{
						str_truncate(aTeamNum, sizeof(aTeamNum), pTeamPos + str_length("' 邀请你加入 "), pUsePos - (pTeamPos + str_length("' 邀请你加入 ")));
						if(str_endswith(aTeamNum, " 队"))
							aTeamNum[str_length(aTeamNum) - str_length(" 队")] = '\0';
					}
				}
				if(aTeamNum[0] != '\0')
				{
					char aCmd[64];
					str_format(aCmd, sizeof(aCmd), "/team %s", aTeamNum);
					SendChat(0, aCmd);
				}
			}
			// 通知栏成功接管的服务端消息不再重复进入聊天框；未入队的消息继续显示。
			if(ServerMessageHandled && QmHudNotifications::ShouldSuppressServerMessageChat(ServerMessageAnalysis))
			{
				PrintSuppressedServerMessage();
				return;
			}
			AddLine(pMsg->m_ClientId, pMsg->m_Team, pDisplayMessage, false, ServerMessageAnalysis.m_Class, SourceConnection);
		}
		else
		{
			AddLine(pMsg->m_ClientId, pMsg->m_Team, pMsg->m_pMessage, false, std::nullopt, SourceConnection);
		}

		SaveChatLogLine(pMsg->m_ClientId, pMsg->m_Team, pMsg->m_pMessage);

		if(Client()->State() != IClient::STATE_DEMOPLAYBACK &&
			pMsg->m_ClientId == SERVER_MSG)
		{
			StoreSave(pMsg->m_pMessage);
		}
	}
	else if(MsgType == NETMSGTYPE_SV_COMMANDINFO)
	{
		CNetMsg_Sv_CommandInfo *pMsg = (CNetMsg_Sv_CommandInfo *)pRawMsg;
		if(!m_ServerSupportsCommandInfo)
		{
			m_vServerCommands.clear();
			++m_ServerCommandsRevision;
			m_ServerSupportsCommandInfo = true;
		}
		RegisterCommand(pMsg->m_pName, pMsg->m_pArgsFormat, pMsg->m_pHelpText);
	}
	else if(MsgType == NETMSGTYPE_SV_COMMANDINFOREMOVE)
	{
		CNetMsg_Sv_CommandInfoRemove *pMsg = (CNetMsg_Sv_CommandInfoRemove *)pRawMsg;
		UnregisterCommand(pMsg->m_pName);
	}
}

bool CChat::LineShouldHighlight(const char *pLine, const char *pName)
{
	const char *pHit = str_utf8_find_nocase(pLine, pName);

	while(pHit)
	{
		int Length = str_length(pName);

		if(Length > 0 && (pLine == pHit || pHit[-1] == ' ') && (pHit[Length] == 0 || pHit[Length] == ' ' || pHit[Length] == '.' || pHit[Length] == '!' || pHit[Length] == ',' || pHit[Length] == '?' || pHit[Length] == ':'))
			return true;

		pHit = str_utf8_find_nocase(pHit + 1, pName);
	}

	return false;
}

static constexpr const char *SAVES_HEADER[] = {
	"Time",
	"Player",
	"Map",
	"Code",
};

// TODO: remove this in a few releases (in 2027 or later)
//       it got deprecated by CGameClient::StoreSave
void CChat::StoreSave(const char *pText)
{
	const char *pStart = str_find(pText, "Team successfully saved by ");
	const char *pMid = str_find(pText, ". Use '/load ");
	const char *pOn = str_find(pText, "' on ");
	const char *pEnd = str_find(pText, pOn ? " to continue" : "' to continue");

	if(!pStart || !pMid || !pEnd || pMid < pStart || pEnd < pMid || (pOn && (pOn < pMid || pEnd < pOn)))
		return;

	char aName[16];
	str_truncate(aName, sizeof(aName), pStart + 27, pMid - pStart - 27);

	char aSaveCode[64];

	str_truncate(aSaveCode, sizeof(aSaveCode), pMid + 13, (pOn ? pOn : pEnd) - pMid - 13);

	char aTimestamp[20];
	str_timestamp_format(aTimestamp, sizeof(aTimestamp), FORMAT_SPACE);

	const bool SavesFileExists = Storage()->FileExists(SAVES_FILE, IStorage::TYPE_SAVE);
	IOHANDLE File = Storage()->OpenFile(SAVES_FILE, IOFLAG_APPEND, IStorage::TYPE_SAVE);
	if(!File)
		return;

	const char *apColumns[4] = {
		aTimestamp,
		aName,
		Client()->GetCurrentMap(),
		aSaveCode,
	};

	if(!SavesFileExists)
	{
		CsvWrite(File, 4, SAVES_HEADER);
	}
	CsvWrite(File, 4, apColumns);
	io_close(File);
}

static bool EnsureChatLogFolder(IStorage *pStorage)
{
	if(!pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE) && !pStorage->FolderExists("qmclient", IStorage::TYPE_SAVE))
	{
		log_error("chat", "Failed to create chat log root folder");
		return false;
	}
	if(!pStorage->CreateFolder(QM_CHAT_LOG_DIR, IStorage::TYPE_SAVE) && !pStorage->FolderExists(QM_CHAT_LOG_DIR, IStorage::TYPE_SAVE))
	{
		log_error("chat", "Failed to create chat log folder '%s'", QM_CHAT_LOG_DIR);
		return false;
	}
	return true;
}

static void CleanupOldChatLogs(IStorage *pStorage, const char *pToday, int KeepDays, std::string &LastCleanupDate)
{
	if(KeepDays <= 0 || LastCleanupDate == pToday)
		return;

	time_t TodayDate = 0;
	if(!timestamp_from_str(pToday, "%Y-%m-%d", &TodayDate))
		return;

	SChatLogCleanupData Data;
	Data.m_pStorage = pStorage;
	Data.m_CutoffDate = TodayDate - (time_t)maximum(KeepDays - 1, 0) * 24 * 60 * 60;
	pStorage->ListDirectory(IStorage::TYPE_SAVE, QM_CHAT_LOG_DIR, ChatLogCleanupCallback, &Data);
	LastCleanupDate = pToday;
}

void CChat::SaveChatLogLine(int ClientId, int Team, const char *pLine)
{
	if(!g_Config.m_QmChatLogAutoSave || Client()->State() == IClient::STATE_DEMOPLAYBACK || pLine == nullptr || pLine[0] == '\0')
		return;

	char aDate[11];
	str_timestamp_format(aDate, sizeof(aDate), "%Y-%m-%d");

	char aTimestamp[20];
	str_timestamp_format(aTimestamp, sizeof(aTimestamp), FORMAT_SPACE);

	char aName[MAX_NAME_LENGTH];
	if(ClientId == SERVER_MSG)
	{
		str_copy(aName, "server");
	}
	else if(ClientId == CLIENT_MSG)
	{
		str_copy(aName, "client");
	}
	else if(ClientId >= 0 && ClientId < MAX_CLIENTS && GameClient()->m_aClients[ClientId].m_aName[0] != '\0')
	{
		GameClient()->FormatStreamerName(ClientId, aName, sizeof(aName));
	}
	else
	{
		str_format(aName, sizeof(aName), "client %d", ClientId);
	}
	str_sanitize_cc(aName);

	char aText[MAX_LINE_LENGTH];
	str_copy(aText, pLine);
	str_sanitize_cc(aText);

	char aFilename[IO_MAX_PATH_LENGTH];
	str_format(aFilename, sizeof(aFilename), "%s/%s%s%s", QM_CHAT_LOG_DIR, QM_CHAT_LOG_PREFIX, aDate, QM_CHAT_LOG_EXTENSION);
	char aLine[512];
	if(ClientId == SERVER_MSG || ClientId == CLIENT_MSG)
		str_format(aLine, sizeof(aLine), "[%s] [%s] %s", aTimestamp, ChatLogKind(ClientId, Team), aText);
	else
		str_format(aLine, sizeof(aLine), "[%s] [%s] %s: %s", aTimestamp, ChatLogKind(ClientId, Team), aName, aText);

	// 收消息时快照所有输入，worker 不访问组件、配置或玩家状态。
	auto pJob = m_ChatLogWrites.Enqueue([pStorage = Storage(), pLastCleanupDate = m_pChatLogLastCleanupDate,
						    Date = std::string(aDate), Filename = std::string(aFilename), Line = std::string(aLine), KeepDays = g_Config.m_QmChatLogKeepDays] {
		if(!EnsureChatLogFolder(pStorage))
			return;
		CleanupOldChatLogs(pStorage, Date.c_str(), KeepDays, *pLastCleanupDate);
		IOHANDLE File = pStorage->OpenFile(Filename.c_str(), IOFLAG_APPEND, IStorage::TYPE_SAVE);
		if(!File)
		{
			log_error("chat", "Failed to open chat log '%s'", Filename.c_str());
			return;
		}
		io_write(File, Line.data(), Line.size());
		io_write_newline(File);
		io_close(File);
	});
	if(pJob)
		Engine()->AddJob(pJob);
}

static std::shared_ptr<const QmChatExport::SMetadata> CaptureChatExportMetadata(CGameClient *pGameClient, int ClientId, int Team, const char *pName, const char *pMessage, int SourceConnection);

void CChat::PrintBlockedMessageToConsole(int ClientId, int Team, const char *pLine, int SourceConnection)
{
	char aName[64] = "";
	bool Highlighted = false;
	const char *pFrom = "chat/all";
	ColorRGBA ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));

	if(ClientId == SERVER_MSG)
	{
		str_copy(aName, "*** ");
		pFrom = "chat/server";
		ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
	}
	else if(ClientId == CLIENT_MSG)
	{
		str_copy(aName, "— ");
		pFrom = "chat/client";
		ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
	}
	else
	{
		const CGameClient::CClientData &LineAuthor = GameClient()->m_aClients[ClientId];
		char aDisplayName[MAX_NAME_LENGTH];
		GameClient()->FormatStreamerName(ClientId, aDisplayName, sizeof(aDisplayName));

		if(Client()->State() != IClient::STATE_DEMOPLAYBACK && !GameClient()->IsLocalClientId(ClientId))
		{
			for(int LocalId : GameClient()->m_aLocalIds)
				Highlighted |= LocalId >= 0 && LineShouldHighlight(pLine, GameClient()->m_aClients[LocalId].m_aName);
		}
		else if(Client()->State() == IClient::STATE_DEMOPLAYBACK && ClientId != GameClient()->m_Snap.m_LocalClientId)
		{
			const int LocalId = GameClient()->m_Snap.m_LocalClientId;
			Highlighted = LocalId >= 0 && LineShouldHighlight(pLine, GameClient()->m_aClients[LocalId].m_aName);
		}

		if(Team == TEAM_WHISPER_SEND || Team == TEAM_WHISPER_RECV)
		{
			str_copy(aName, Team == TEAM_WHISPER_SEND ? "→" : "←");
			if(LineAuthor.m_Active)
			{
				str_append(aName, " ");
				str_append(aName, aDisplayName);
			}
			Highlighted = Team == TEAM_WHISPER_RECV;
			pFrom = "chat/whisper";
		}
		else
		{
			str_copy(aName, aDisplayName);
			if(Team == 1)
				pFrom = "chat/team";
		}

		const bool Friend = LineAuthor.m_Active && LineAuthor.m_Friend;
		if(Highlighted)
			ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
		else if(Friend && g_Config.m_ClMessageFriend)
			ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor));
		else if(Team == 1)
			ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
	}

	char aBuf[1024];
	str_format(aBuf, sizeof(aBuf), "%s%s%s", aName, ClientId >= 0 ? ": " : "", pLine);
	GameClient()->m_GameConsole.PrintLineWithColorSpans(IConsole::OUTPUT_LEVEL_STANDARD, pFrom, aBuf, ChatLogColor, nullptr, 0,
		CaptureChatExportMetadata(GameClient(), ClientId, Team, aName, pLine, SourceConnection));
}

ColorRGBA CChat::PlayerNameColor(int ClientId, int NameColor, bool TeamMessage) const
{
	if(ClientId >= 0 && g_Config.m_QmWarList && g_Config.m_QmWarListChat && GameClient()->m_WarList.GetAnyWar(ClientId))
		return GameClient()->m_WarList.GetPriorityColor(ClientId);
	if(TeamMessage)
		return CalculateNameColor(ColorHSLA(g_Config.m_ClMessageTeamColor));
	if(NameColor == TEAM_RED)
		return ColorRGBA(1.0f, 0.5f, 0.5f, 1.0f);
	if(NameColor == TEAM_BLUE)
		return ColorRGBA(0.7f, 0.7f, 1.0f, 1.0f);
	if(NameColor == TEAM_SPECTATORS)
		return ColorRGBA(0.75f, 0.5f, 0.75f, 1.0f);
	if(ClientId >= 0 && g_Config.m_ClChatTeamColors && GameClient()->m_Teams.Team(ClientId))
		return GameClient()->GetDDTeamColor(GameClient()->m_Teams.Team(ClientId), 0.75f);
	return ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f);
}

void CChat::AddMergedAuthor(CLine &Line, int ClientId)
{
	for(const SMergedAuthor &Author : Line.m_vMergedAuthors)
	{
		if(Author.m_ClientId == ClientId)
			return;
	}

	SMergedAuthor Author;
	Author.m_ClientId = ClientId;
	GameClient()->FormatStreamerName(ClientId, Author.m_aName, sizeof(Author.m_aName));
	str_copy(Author.m_aPlayerName, GameClient()->m_aClients[ClientId].m_aName);
	str_copy(Author.m_aQmTitle, GameClient()->m_QmClient.PlayerTitle(ClientId));

	int NameColor = -2;
	const CGameClient::CClientData &LineAuthor = GameClient()->m_aClients[ClientId];
	if(LineAuthor.m_Active)
	{
		if(LineAuthor.m_Team == TEAM_SPECTATORS)
			NameColor = TEAM_SPECTATORS;
		if(GameClient()->IsTeamPlay())
		{
			if(LineAuthor.m_Team == TEAM_RED)
				NameColor = TEAM_RED;
			else if(LineAuthor.m_Team == TEAM_BLUE)
				NameColor = TEAM_BLUE;
		}
	}
	Author.m_NameColor = PlayerNameColor(ClientId, NameColor, false);
	Line.m_vMergedAuthors.push_back(Author);
	RebuildMergedAuthorName(Line);
}

void CChat::RebuildMergedAuthorName(CLine &Line)
{
	Line.m_aName[0] = '\0';
	for(size_t i = 0; i < Line.m_vMergedAuthors.size(); ++i)
	{
		if(i > 0)
			str_append(Line.m_aName, ",", sizeof(Line.m_aName));
		str_append(Line.m_aName, Line.m_vMergedAuthors[i].m_aName, sizeof(Line.m_aName));
	}
}

void CChat::PrintLineToConsole(const CLine &Line) const
{
	if(Line.m_ConsoleSuppressed)
		return;

	ColorRGBA ChatLogColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	if(Line.m_Highlighted)
		ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
	else if(Line.m_Friend && g_Config.m_ClMessageFriend)
		ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor));
	else if(Line.m_Team)
		ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
	else if(Line.m_ClientId == SERVER_MSG)
		ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
	else if(Line.m_ClientId == CLIENT_MSG)
		ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
	else
		ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));

	const char *pFrom;
	if(Line.m_Whisper)
		pFrom = "chat/whisper";
	else if(Line.m_Team)
		pFrom = "chat/team";
	else if(Line.m_ClientId == SERVER_MSG)
		pFrom = "chat/server";
	else if(Line.m_ClientId == CLIENT_MSG)
		pFrom = "chat/client";
	else
		pFrom = "chat/all";

	char aBuf[4096] = "";
	const bool Merged = Line.m_TimesRepeated > 0 && !Line.m_vMergedAuthors.empty();
	if(!Merged)
	{
		str_format(aBuf, sizeof(aBuf), "%s%s%s", Line.m_aName, Line.m_ClientId >= 0 ? ": " : "", Line.m_aText);
		GameClient()->m_GameConsole.PrintLineWithColorSpans(IConsole::OUTPUT_LEVEL_STANDARD, pFrom, aBuf, ChatLogColor, nullptr, 0, Line.m_pExportMetadata);
		return;
	}

	std::vector<CGameConsole::SColorSpan> vColorSpans;
	vColorSpans.reserve(Line.m_vMergedAuthors.size());
	for(size_t i = 0; i < Line.m_vMergedAuthors.size(); ++i)
	{
		if(i > 0)
			str_append(aBuf, ",", sizeof(aBuf));
		const size_t StartByte = str_length(aBuf);
		str_append(aBuf, Line.m_vMergedAuthors[i].m_aName, sizeof(aBuf));
		const size_t EndByte = str_length(aBuf);
		const int StartChar = (int)str_utf8_offset_bytes_to_chars(aBuf, StartByte);
		const int EndChar = (int)str_utf8_offset_bytes_to_chars(aBuf, EndByte);
		vColorSpans.push_back({StartChar, EndChar - StartChar, Line.m_vMergedAuthors[i].m_NameColor});
	}
	char aCount[16];
	str_format(aCount, sizeof(aCount), " [%d]: ", Line.m_TimesRepeated + 1);
	str_append(aBuf, aCount, sizeof(aBuf));
	str_append(aBuf, Line.m_aText, sizeof(aBuf));
	// 导出记录里的发送者带合并次数，导出的聊天记录才能看出这是一条被合并过的重复消息。
	auto pMetadata = std::make_shared<QmChatExport::SMetadata>();
	if(Line.m_pExportMetadata)
		*pMetadata = *Line.m_pExportMetadata;
	char aRepeated[16];
	str_format(aRepeated, sizeof(aRepeated), " [%d]", Line.m_TimesRepeated + 1);
	pMetadata->m_Sender = std::string(Line.m_aName) + aRepeated;
	pMetadata->m_Message = Line.m_aText;
	GameClient()->m_GameConsole.PrintLineWithColorSpans(IConsole::OUTPUT_LEVEL_STANDARD, pFrom, aBuf, ChatLogColor, vColorSpans.data(), vColorSpans.size(), std::move(pMetadata));
}

void CChat::FlushPendingConsoleLine(bool Force)
{
	if(m_PendingConsoleLineIndex < 0 || m_PendingConsoleLineIndex >= MAX_LINES)
		return;
	CLine &Line = m_aLines[m_PendingConsoleLineIndex];
	if(!Line.m_Initialized)
	{
		m_PendingConsoleLineIndex = -1;
		return;
	}
	const int64_t Now = time();
	if(!Force && g_Config.m_QmMessageMerge && Now >= Line.m_Time && Now - Line.m_Time <= time_freq() * 2)
		return;
	PrintLineToConsole(Line);
	m_PendingConsoleLineIndex = -1;
}

// 收到消息时固化身份与头像：之后改名、换皮肤或断线都不影响已导出/待导出的历史消息。
static std::shared_ptr<const QmChatExport::SMetadata> CaptureChatExportMetadata(CGameClient *pGameClient, int ClientId, int Team, const char *pName, const char *pMessage, int SourceConnection)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return nullptr;
	auto pMetadata = std::make_shared<QmChatExport::SMetadata>();
	pMetadata->m_Sender = pName;
	pMetadata->m_Message = pMessage;
	const int LocalId = pGameClient->m_Snap.m_LocalClientId;
	const bool DemoPlayback = pGameClient->Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const int Connection = SourceConnection >= 0 && SourceConnection < NUM_DUMMIES ? SourceConnection : g_Config.m_ClDummy;
	pMetadata->m_Local = Team == TEAM_WHISPER_SEND || pGameClient->IsLocalClientId(ClientId) ||
			     (DemoPlayback && ClientId == LocalId);
	// 发出私聊时协议中的 ClientId 指向收件人，头像应取实际发件人。
	const int SenderId = QmChatExport::ResolveSenderId(ClientId, Team == TEAM_WHISPER_SEND, Connection, pGameClient->m_aLocalIds, std::size(pGameClient->m_aLocalIds), LocalId, DemoPlayback);
	if(SenderId >= 0 && SenderId < MAX_CLIENTS && pGameClient->m_aClients[SenderId].m_Active)
	{
		pMetadata->m_pAvatar = QmChatAvatar::Capture(pGameClient->m_aClients[SenderId].m_RenderInfo, Connection);
		if(Team == TEAM_WHISPER_SEND)
		{
			char aSenderName[MAX_NAME_LENGTH];
			pGameClient->FormatStreamerName(SenderId, aSenderName, sizeof(aSenderName));
			pMetadata->m_Sender = std::string(aSenderName) + " " + pName;
		}
	}
	return pMetadata;
}

void CChat::AddLine(int ClientId, int Team, const char *pLine, bool ForceVisible)
{
	AddLine(ClientId, Team, pLine, ForceVisible, std::nullopt, -1);
}

void CChat::AddLine(int ClientId, int Team, const char *pLine, bool ForceVisible, std::optional<QmHudNotifications::EServerMessageClass> KnownServerMessageClass, int SourceConnection, int TimesRepeated)
{
	if(*pLine == 0 ||
		(ClientId == SERVER_MSG && !g_Config.m_ClShowChatSystem) ||
		(ClientId >= 0 && (GameClient()->m_aClients[ClientId].m_aName[0] == '\0' || // unknown client
					  GameClient()->m_aClients[ClientId].m_ChatIgnore ||
					  (GameClient()->m_Snap.m_LocalClientId != ClientId && g_Config.m_QmWarListBlockEnemyChat && GameClient()->m_WarList.IsEnemy(ClientId)) ||
					  (GameClient()->m_Snap.m_LocalClientId != ClientId && g_Config.m_ClShowChatFriends && !GameClient()->m_aClients[ClientId].m_Friend) ||
					  (GameClient()->m_Snap.m_LocalClientId != ClientId && g_Config.m_ClShowChatTeamMembersOnly && GameClient()->IsOtherTeam(ClientId) && GameClient()->m_Teams.Team(GameClient()->m_Snap.m_LocalClientId) != TEAM_FLOCK) ||
					  (GameClient()->m_Snap.m_LocalClientId != ClientId && GameClient()->m_aClients[ClientId].m_Foe))))
		return;

	// 其它消息出现时先给上一段 echo 重复计数收口，避免统计被后面的消息挤掉。
	// 必须放在下面「不显示客户端消息」的提前返回之前：被过滤掉的 echo 也要能触发收口。
	if(HasPendingEchoRepeat())
		GateEchoRepeat(nullptr);

	// TClient
	if(ClientId == CLIENT_MSG && !g_Config.m_QmShowChatClient)
		return;

	char aFilteredLine[MAX_LINE_LENGTH];
	const char *pFilteredLine = pLine;
	std::vector<std::string> BlockedWords;
	bool BlockWordsConsolePrinted = false;
	const EBlockWordsAction BlockWordsAction = static_cast<EBlockWordsAction>(g_Config.m_QmBlockWordsAction);
	bool IsLocalBlockWordsClient = GameClient()->IsLocalClientId(ClientId);
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		IsLocalBlockWordsClient = ClientId == GameClient()->m_Snap.m_LocalClientId;
	const bool CanHideBlockWordsMessage = ShouldHideBlockWordsMessage(BlockWordsAction, true, ClientId, IsLocalBlockWordsClient, Team);
	if(g_Config.m_QmBlockWordsEnabled && g_Config.m_QmBlockWordsList[0] != '\0' &&
		(BlockWordsAction == EBlockWordsAction::REPLACE || CanHideBlockWordsMessage))
	{
		std::string Text = pLine;
		std::vector<std::string> *pMatched = g_Config.m_QmBlockWordsShowConsole ? &BlockedWords : nullptr;
		if(ApplyBlockWords(Text, pMatched))
		{
			FlushPendingConsoleLine(true);
			if(g_Config.m_QmBlockWordsShowConsole && !BlockedWords.empty())
			{
				std::string Joined;
				for(size_t i = 0; i < BlockedWords.size(); ++i)
				{
					if(i > 0)
						Joined.append(", ");
					Joined.append(BlockedWords[i]);
				}
				char aBuf[512];
				str_format(aBuf, sizeof(aBuf), "屏蔽词: %s", Joined.c_str());
				const ColorRGBA LogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmBlockWordsConsoleColor));
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat/blocklist", aBuf, LogColor);
			}
			PrintBlockedMessageToConsole(ClientId, Team, pLine, SourceConnection);
			BlockWordsConsolePrinted = true;
			if(CanHideBlockWordsMessage)
			{
				return;
			}
			str_copy(aFilteredLine, Text.c_str(), sizeof(aFilteredLine));
			pFilteredLine = aFilteredLine;
		}
	}
	pLine = pFilteredLine;
	const EQmChatEmoji ChatEmoji = QmChatEmojiFromText(pLine);
	char aChatBubbleText[256];
	str_copy(aChatBubbleText, pLine);

	// trim right and set maximum length to 256 utf8-characters
	int Length = 0;
	const char *pStr = pLine;
	const char *pEnd = nullptr;
	while(*pStr)
	{
		const char *pStrOld = pStr;
		int Code = str_utf8_decode(&pStr);

		// check if unicode is not empty
		if(!str_utf8_isspace(Code))
		{
			pEnd = nullptr;
		}
		else if(pEnd == nullptr)
		{
			pEnd = pStrOld;
		}

		if(++Length >= MAX_LINE_LENGTH)
		{
			*(const_cast<char *>(pStr)) = '\0';
			break;
		}
	}
	if(pEnd != nullptr)
		*(const_cast<char *>(pEnd)) = '\0';

	if(*pLine == 0)
		return;

	bool Highlighted = false;

	// Custom color for new line
	std::optional<ColorRGBA> CustomColor = std::nullopt;
	if(ClientId == CLIENT_MSG)
		CustomColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));

	CLine &PreviousLine = m_aLines[m_CurrentLine];
	const int64_t Now = time();
	FlushPendingConsoleLine(false);

	// Team Number:
	// 0 = global; 1 = team; 2 = sending whisper; 3 = receiving whisper
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(ClientId >= 0 && ClientId != GameClient()->m_aLocalIds[0] && ClientId != GameClient()->m_aLocalIds[1])
		{
			for(int LocalId : GameClient()->m_aLocalIds)
			{
				Highlighted |= LocalId >= 0 && LineShouldHighlight(pLine, GameClient()->m_aClients[LocalId].m_aName);
			}
		}
	}
	else
	{
		// on demo playback use local id from snap directly,
		// since m_aLocalIds isn't valid there
		Highlighted |= GameClient()->m_Snap.m_LocalClientId >= 0 && LineShouldHighlight(pLine, GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_aName);
	}

	// 以收到消息时的认证身份为准，之后不按可能复用的玩家编号改变历史消息。
	const EQmSponsorChatStyle SponsorChatStyle = QmSponsorChatMessageStyle(
		GameClient()->m_QmClient.PlayerChatStyle(ClientId), Highlighted, Team == 1, Team >= 2,
		CustomColor.has_value(), ChatEmoji != EQmChatEmoji::NONE);

	if(g_Config.m_QmMessageMerge && !Highlighted &&
		PreviousLine.m_Initialized &&
		(PreviousLine.m_ConsoleSuppressed || m_PendingConsoleLineIndex == m_CurrentLine) &&
		PreviousLine.m_CustomColor == CustomColor &&
		PreviousLine.m_ForceVisible == ForceVisible &&
		PreviousLine.m_ConsoleSuppressed == BlockWordsConsolePrinted &&
		PreviousLine.m_ChatEmoji == ChatEmoji &&
		CanMergePlayerMessages(PreviousLine.m_ClientId, PreviousLine.m_TeamNumber, PreviousLine.m_aText, PreviousLine.m_Time, ClientId, Team, pLine, Now))
	{
		PreviousLine.m_TimesRepeated++;
		PreviousLine.m_SponsorChatStyle = QmSponsorChatMergedStyle(PreviousLine.m_SponsorChatStyle, SponsorChatStyle);
		AddMergedAuthor(PreviousLine, ClientId);
		if(PreviousLine.m_vMergedAuthors.size() > 1)
		{
			PreviousLine.m_Friend = false;
			PreviousLine.m_pManagedTeeRenderInfo = nullptr;
			if(PreviousLine.m_pExportMetadata)
			{
				auto pMetadata = std::make_shared<QmChatExport::SMetadata>(*PreviousLine.m_pExportMetadata);
				pMetadata->m_pAvatar.reset();
				pMetadata->m_Local = std::all_of(PreviousLine.m_vMergedAuthors.begin(), PreviousLine.m_vMergedAuthors.end(), [this](const SMergedAuthor &Author) {
					return GameClient()->IsLocalClientId(Author.m_ClientId);
				});
				PreviousLine.m_pExportMetadata = std::move(pMetadata);
			}
		}
		PreviousLine.m_ConsoleSuppressed |= BlockWordsConsolePrinted;
		PreviousLine.DeleteTextContainers(TextRender());
		Graphics()->DeleteQuadContainer(PreviousLine.m_QuadContainerIndex);
		PreviousLine.m_Time = Now;
		PreviousLine.m_aYOffset[0] = -1.0f;
		PreviousLine.m_aYOffset[1] = -1.0f;
		PreviousLine.m_ChatEmojiRect = {};
		PreviousLine.m_ContentWidth = 0.0f;
		PreviousLine.m_CutOffProgress = 0.0f;
		BeginLinePresentation(PreviousLine.m_Presentation, PreviousLine.m_Time, true);

		CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
		str_copy(ClientData.m_aChatBubbleText, aChatBubbleText, sizeof(ClientData.m_aChatBubbleText));
		ClientData.m_ChatBubbleStartTick = Now;
		ClientData.m_ChatBubbleExpireTick = Now + time_freq() * g_Config.m_QmChatBubbleDuration;
		return;
	}

	FlushPendingConsoleLine(true);

	m_CurrentLine = (m_CurrentLine + 1) % MAX_LINES;
	if(m_BacklogCurLine > 0)
		m_BacklogCurLine = ClampBacklogLine(m_BacklogCurLine + 1, CountInitializedLines() + 1, 1);

	CLine &CurrentLine = m_aLines[m_CurrentLine];
	CurrentLine.Reset(*this);
	CurrentLine.m_Initialized = true;
	CurrentLine.m_Time = Now;
	BeginLinePresentation(CurrentLine.m_Presentation, CurrentLine.m_Time, false);
	CurrentLine.m_aYOffset[0] = -1.0f;
	CurrentLine.m_aYOffset[1] = -1.0f;
	CurrentLine.m_ClientId = ClientId;
	CurrentLine.m_TeamNumber = Team;
	CurrentLine.m_Team = Team == 1;
	CurrentLine.m_Whisper = Team >= 2;
	CurrentLine.m_NameColor = -2;
	CurrentLine.m_CustomColor = CustomColor;
	CurrentLine.m_SponsorChatStyle = SponsorChatStyle;
	CurrentLine.m_ForceVisible = ForceVisible;
	CurrentLine.m_ConsoleSuppressed = BlockWordsConsolePrinted;
	// echo 重复段的计数直接落到行上，聊天渲染已有的 [N] 计数显示会负责呈现它。
	CurrentLine.m_TimesRepeated = TimesRepeated;

	CurrentLine.m_Highlighted = Highlighted;

	str_copy(CurrentLine.m_aText, pLine);
	CurrentLine.m_ChatEmoji = ChatEmoji;
	CurrentLine.m_ServerMessageClass = ResolveLineServerMessageClass(ClientId, CurrentLine.m_aText, KnownServerMessageClass);

	if(CurrentLine.m_ClientId == SERVER_MSG || CurrentLine.m_ClientId == CLIENT_MSG)
	{
		RefreshMessageNamePrefix(CurrentLine.m_ClientId, CurrentLine.m_aName, sizeof(CurrentLine.m_aName), g_Config.m_QmChatHideSystemPrefix != 0);
	}
	else
	{
		const auto &LineAuthor = GameClient()->m_aClients[CurrentLine.m_ClientId];
		str_copy(CurrentLine.m_aQmTitle, GameClient()->m_QmClient.PlayerTitle(CurrentLine.m_ClientId));
		char aDisplayName[MAX_NAME_LENGTH];
		GameClient()->FormatStreamerName(CurrentLine.m_ClientId, aDisplayName, sizeof(aDisplayName));

		if(LineAuthor.m_Active)
		{
			if(LineAuthor.m_Team == TEAM_SPECTATORS)
				CurrentLine.m_NameColor = TEAM_SPECTATORS;

			if(GameClient()->IsTeamPlay())
			{
				if(LineAuthor.m_Team == TEAM_RED)
					CurrentLine.m_NameColor = TEAM_RED;
				else if(LineAuthor.m_Team == TEAM_BLUE)
					CurrentLine.m_NameColor = TEAM_BLUE;
			}
		}

		if(Team == TEAM_WHISPER_SEND)
		{
			str_copy(CurrentLine.m_aName, "→");
			if(LineAuthor.m_Active)
			{
				str_append(CurrentLine.m_aName, " ");
				str_append(CurrentLine.m_aName, aDisplayName);
			}
			CurrentLine.m_NameColor = TEAM_BLUE;
			CurrentLine.m_Highlighted = false;
			Highlighted = false;
		}
		else if(Team == TEAM_WHISPER_RECV)
		{
			str_copy(CurrentLine.m_aName, "←");
			if(LineAuthor.m_Active)
			{
				str_append(CurrentLine.m_aName, " ");
				str_append(CurrentLine.m_aName, aDisplayName);
			}
			CurrentLine.m_NameColor = TEAM_RED;
			CurrentLine.m_Highlighted = true;
			Highlighted = true;
		}
		else
		{
			str_copy(CurrentLine.m_aName, aDisplayName);
		}

		if(LineAuthor.m_Active)
		{
			CurrentLine.m_Friend = LineAuthor.m_Friend;
			CurrentLine.m_pManagedTeeRenderInfo = GameClient()->CreateManagedTeeRenderInfo(LineAuthor);
		}

		if(Team < TEAM_WHISPER_SEND)
			AddMergedAuthor(CurrentLine, ClientId);
	}

	CurrentLine.m_pExportMetadata = CaptureChatExportMetadata(GameClient(), ClientId, Team, CurrentLine.m_aName, CurrentLine.m_aText, SourceConnection);
	if(g_Config.m_QmMessageMerge && ClientId >= 0 && Team < TEAM_WHISPER_SEND && !CurrentLine.m_ConsoleSuppressed)
		m_PendingConsoleLineIndex = m_CurrentLine;
	else
		PrintLineToConsole(CurrentLine);

	// play sound
	if(ClientId == SERVER_MSG)
	{
		if(Now - m_aLastSoundPlayed[CHAT_SERVER] >= time_freq() * 3 / 10)
		{
			if(g_Config.m_SndServerMessage)
			{
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_SERVER, 1.0f);
				m_aLastSoundPlayed[CHAT_SERVER] = Now;
			}
		}
	}
	else if(ClientId == CLIENT_MSG)
	{
		// No sound yet
	}
	else if(Highlighted && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(Now - m_aLastSoundPlayed[CHAT_HIGHLIGHT] >= time_freq() * 3 / 10)
		{
			char aBuf[1024];
			str_format(aBuf, sizeof(aBuf), "%s: %s", CurrentLine.m_aName, CurrentLine.m_aText);
			Client()->Notify("DDNet Chat", aBuf);
			if(g_Config.m_SndHighlight)
			{
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_HIGHLIGHT, 1.0f);
				m_aLastSoundPlayed[CHAT_HIGHLIGHT] = Now;
			}

			if(g_Config.m_ClEditor)
			{
				GameClient()->Editor()->UpdateMentions();
			}
		}
	}
	else if(Team != TEAM_WHISPER_SEND)
	{
		if(Now - m_aLastSoundPlayed[CHAT_CLIENT] >= time_freq() * 3 / 10)
		{
			bool PlaySound = CurrentLine.m_Team ? g_Config.m_SndTeamChat : g_Config.m_SndChat;
#if defined(CONF_VIDEORECORDER)
			if(IVideo::Current())
			{
				PlaySound &= qm_demo_display::Resolve(g_Config, Client()->State() == IClient::STATE_DEMOPLAYBACK, true).m_Chat;
			}
#endif
			if(PlaySound)
			{
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_CLIENT, 1.0f);
				m_aLastSoundPlayed[CHAT_CLIENT] = Now;
			}
		}
	}

	// Set chat bubble for player
	if(ClientId >= 0 && ClientId < MAX_CLIENTS)
	{
		CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
		str_copy(ClientData.m_aChatBubbleText, aChatBubbleText, sizeof(ClientData.m_aChatBubbleText));
		const int64_t BubbleStartTick = time();
		ClientData.m_ChatBubbleStartTick = BubbleStartTick;
		ClientData.m_ChatBubbleExpireTick = BubbleStartTick + time_freq() * g_Config.m_QmChatBubbleDuration;
	}

	// 表情码保留原文，但不应进入翻译任务。
	if(QmChatEmojiShouldTranslate(CurrentLine.m_ChatEmoji))
		GameClient()->m_Translate.AutoTranslate(CurrentLine);
}

bool CChat::OnPrepareLines(float y)
{
	bool EmojiLayoutChanged = false;
	float x = QmChatHistoryStartX(g_Config.m_QmChatScrollbarRight != 0);
	float FontSize = this->FontSize();
	const SQmFocusModeDecisions Focus = GetQmFocusModeDecisions();
	const bool FocusHideChat = Focus.m_HidePlayerMessages;
	const bool FocusHideSystemInfoMessages = Focus.m_HideSystemInfoMessages;
	const bool FocusHideSystemPromptMessages = Focus.m_HideSystemPromptMessages;
	const bool FocusHideEcho = Focus.m_HideEchoMessages;

	// 图集或配置字体变化时，正文、头衔及表情行的布局缓存一起失效。
	const uint64_t GlyphAtlasRevision = TextRender()->GlyphAtlasRevision();
	const std::array<int, 5> aChatTextSettings = {g_Config.m_ClChatFontSize, g_Config.m_ClChatWidth, g_Config.m_QmChatTranslationSize, g_Config.m_QmChatScrollbarRight, g_Config.m_QmChatHideSystemPrefix};
	if(m_PreparedGlyphAtlasRevision != GlyphAtlasRevision || m_aPreparedChatTextSettings != aChatTextSettings)
	{
		m_PreparedGlyphAtlasRevision = GlyphAtlasRevision;
		m_aPreparedChatTextSettings = aChatTextSettings;
		RebuildChat();
	}

	const bool IsScoreBoardOpen = GameClient()->m_Scoreboard.IsActive();
	const bool ShowLargeArea = m_Show || (m_Mode != MODE_NONE && g_Config.m_ClShowChat == 1) || g_Config.m_ClShowChat == 2;
	const std::array<int, 4> aTitleVisibility = {g_Config.m_QmShowMainTitle, g_Config.m_QmShowDummyTitle,
		GameClient()->m_aLocalIds[IClient::CONN_MAIN], GameClient()->m_aLocalIds[IClient::CONN_DUMMY]};
	const bool ForceRecreate = IsScoreBoardOpen != m_PrevScoreBoardShowed || ShowLargeArea != m_PrevShowChat;
	m_PrevScoreBoardShowed = IsScoreBoardOpen;
	m_PrevShowChat = ShowLargeArea;
	if(aTitleVisibility != m_aPrevTitleVisibility)
	{
		m_aPrevTitleVisibility = aTitleVisibility;
		RebuildChat();
	}
	if(m_PrevSponsorChatEffects != (g_Config.m_QmSponsorChatEffects != 0))
	{
		m_PrevSponsorChatEffects = g_Config.m_QmSponsorChatEffects != 0;
		RebuildChat();
		if(!m_PrevSponsorChatEffects)
			m_SponsorChatRenderer.Reset(Graphics());
	}
	// 保留消息原始头衔，重新开启本地显示后可以恢复历史消息。
	const auto VisibleTitle = [this](const char *pTitle, int ClientId) {
		return GameClient()->m_QmClient.ShouldShowPlayerTitle(ClientId) ? pTitle : "";
	};

	const int TeeSize = MessageTeeSize();
	float RealMsgPaddingX = MessagePaddingX();
	float RealMsgPaddingY = MessagePaddingY();
	float RealMsgPaddingTee = TeeSize + MESSAGE_TEE_PADDING_RIGHT;

	if(g_Config.m_ClChatOld)
	{
		RealMsgPaddingX = 0;
		RealMsgPaddingY = 0;
		RealMsgPaddingTee = 0;
	}

	float LineWidth = (IsScoreBoardOpen ? maximum(85.0f, (FontSize * 85.0f / 6.0f)) : g_Config.m_ClChatWidth) - (RealMsgPaddingX * 1.5f) - RealMsgPaddingTee;

	const float HeightLimit =
		IsScoreBoardOpen ?
			CHAT_HEIGHT_MIN + 130.0f :
			(ShowLargeArea ? CHAT_HEIGHT_MIN : CHAT_HEIGHT_FULL);

	float Begin = x;
	float TextBegin = Begin + RealMsgPaddingX / 2.0f;
	int OffsetType = IsScoreBoardOpen ? 1 : 0;
	const SQmTitleColorStyle TitleColorStyle = ResolveQmTitleColorStyle(g_Config.m_QmTitleColorMode, g_Config.m_QmTitleColor, g_Config.m_QmTitleOpacity, false);
	const float TitleAnimationTime = (float)GameClient()->m_QmClient.TitleAnimationTime();
	const SQmTitleShimmer TitleShimmer = QmTitleShimmerFromConfig();
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float PixelSize = (ScreenY1 - ScreenY0) / Graphics()->ScreenHeight();

	for(int i = m_BacklogCurLine; i < MAX_LINES; i++)
	{
		CLine &Line = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
		if(!Line.m_Initialized)
			break;
		const bool ServerMessageIsBasicInfo = Line.m_ServerMessageClass == QmHudNotifications::EServerMessageClass::BasicInfo;
		if(!ShouldRenderFocusFilteredChatLine(FocusHideChat, FocusHideSystemInfoMessages, FocusHideSystemPromptMessages, FocusHideEcho, Line.m_ClientId, Line.m_ForceVisible, ServerMessageIsBasicInfo))
		{
			continue;
		}
		if(!ShowLargeArea && !Line.m_ForceVisible && Line.m_Presentation.m_State == EPresentationState::COLLAPSED)
		{
			if(Line.m_ClientId == SERVER_MSG && QmMacosGraphicsDiagnosticsEnabled() && !Line.m_DiagnosticCollapsedSkipLogged)
			{
				char aPayload[256];
				str_format(aPayload, sizeof(aPayload), "event=server_message_prepare_skip reason=collapsed class=%d text_container=%d text_container_valid=%d",
					static_cast<int>(Line.m_ServerMessageClass), Line.m_TextContainerIndex.m_Index, Line.m_TextContainerIndex.Valid() ? 1 : 0);
				QmMacosGraphicsDiagnosticsLogPayload("perf/autodiag_chat", aPayload, Client());
				Line.m_DiagnosticCollapsedSkipLogged = true;
			}
			continue;
		}

		const bool RenderChatEmoji = GameClient()->m_QmChatEmoji.CanRender(Line.m_ChatEmoji);
		const bool LineEmojiLayoutChanged = QmChatEmojiInvalidateChangedLayout(RenderChatEmoji, Line.m_ChatEmojiImageLayout, Line.m_aYOffset);
		EmojiLayoutChanged |= LineEmojiLayoutChanged;
		// 隐藏身份后清除旧消息的头衔，并重新计算包含头衔的布局缓存。
		bool TitleHidden = false;
		if(Line.m_SponsorChatStyle != EQmSponsorChatStyle::NONE && GameClient()->ShouldHideStreamerIdentity(Line.m_ClientId))
		{
			Line.m_SponsorChatStyle = EQmSponsorChatStyle::NONE;
			TitleHidden = true;
		}
		if(Line.m_aQmTitle[0] != '\0' && GameClient()->ShouldHideStreamerIdentity(Line.m_ClientId))
		{
			Line.m_aQmTitle[0] = '\0';
			TitleHidden = true;
		}
		for(auto &Author : Line.m_vMergedAuthors)
		{
			if(Line.m_SponsorChatStyle != EQmSponsorChatStyle::NONE && GameClient()->ShouldHideStreamerIdentity(Author.m_ClientId))
			{
				Line.m_SponsorChatStyle = EQmSponsorChatStyle::NONE;
				TitleHidden = true;
			}
			if(Author.m_aQmTitle[0] != '\0' && GameClient()->ShouldHideStreamerIdentity(Author.m_ClientId))
			{
				Author.m_aQmTitle[0] = '\0';
				TitleHidden = true;
			}
		}
		const bool MergedPlayerMessages = Line.m_TimesRepeated > 0 && !Line.m_vMergedAuthors.empty();
		bool LineHasDynamicTitle = false;
		float TitleBobPadding = 0.0f;
		const auto IncludeTitleLayout = [&](const char *pTitle, int AuthorId) {
			pTitle = VisibleTitle(pTitle, AuthorId);
			if(pTitle[0] == '\0')
				return;
			const SQmTitleRenderStyle Style = QmTitleResolveRenderStyle(GameClient()->m_QmClient.PlayerTitleStyle(AuthorId));
			if(Style.m_pStyle != nullptr)
			{
				LineHasDynamicTitle = true;
				TitleBobPadding = maximum(TitleBobPadding, QmTitleStyleBobPadding(Style.m_Bob, PixelSize, FontSize));
			}
		};
		if(MergedPlayerMessages)
		{
			for(const auto &Author : Line.m_vMergedAuthors)
				IncludeTitleLayout(Author.m_aQmTitle, Author.m_ClientId);
		}
		else
			IncludeTitleLayout(Line.m_aQmTitle, Line.m_ClientId);
		if(LineEmojiLayoutChanged || TitleHidden || TitleBobPadding != Line.m_QmTitleBobPadding)
		{
			Line.DeleteTextContainers(TextRender());
			Line.m_ChatEmojiRect = {};
			Line.m_aYOffset[0] = -1.0f;
			Line.m_aYOffset[1] = -1.0f;
			Line.m_QmTitleBobPadding = TitleBobPadding;
		}
		const bool LinePrepared = (RenderChatEmoji ? Line.m_ChatEmojiRect.w > 0.0f : Line.m_TextContainerIndex.Valid() && Line.m_ChatEmojiRect.w <= 0.0f) && !LineHasDynamicTitle;
		if(LinePrepared && !ForceRecreate)
		{
			// 已有容器也必须消耗相同的垂直预算，
			// 否则只有“首次创建”时受高度限制，后续帧又会溢出。
			if(Line.m_aYOffset[OffsetType] >= 0.0f)
			{
				y -= Line.m_aYOffset[OffsetType] * ClampPresentationProgress(Line.m_Presentation.m_LayoutVisibility);

				if(y < HeightLimit)
					break;
			}
			continue;
		}

		if(ForceRecreate || !LineHasDynamicTitle)
			Line.DeleteTextContainers(TextRender());
		if(!Line.m_TextContainerIndex.Valid())
		{
			for(auto &Metrics : Line.m_vTitleTextMetrics)
				Metrics.Reset();
		}
		Line.m_ChatEmojiRect = {};
		const bool MultipleAuthors = Line.m_vMergedAuthors.size() > 1;
		// 多人合并仍将正文另起一行，只保留第一个人的前缀占位。
		const bool IndentMessage = MultipleAuthors || (!IsScoreBoardOpen && !g_Config.m_ClChatOld);
		float FirstAuthorPrefixWidth = 0.0f;
		if(MultipleAuthors)
		{
			const SMergedAuthor &FirstAuthor = Line.m_vMergedAuthors.front();
			FirstAuthorPrefixWidth = TextRender()->TextWidth(FontSize, VisibleTitle(FirstAuthor.m_aQmTitle, FirstAuthor.m_ClientId)) +
						 TextRender()->TextWidth(FontSize, FirstAuthor.m_aName) + TextRender()->TextWidth(FontSize, ": ");
		}

		char aClientId[16] = "";
		if(!MultipleAuthors && g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0' && !GameClient()->ShouldHideStreamerIdentity(Line.m_ClientId))
		{
			GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
		}

		char aCount[12];
		if(Line.m_ClientId < 0)
			str_format(aCount, sizeof(aCount), "[%d] ", Line.m_TimesRepeated + 1);
		else
			str_format(aCount, sizeof(aCount), " [%d]", Line.m_TimesRepeated + 1);

		const char *pText = Line.m_aText;
		if(Config()->m_ClStreamerMode && Line.m_ClientId == SERVER_MSG)
		{
			if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load ") && str_endswith(Line.m_aText, "'"))
			{
				pText = "Team save in progress. You'll be able to load with '/load *** *** ***'";
			}
			else if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load") && str_endswith(Line.m_aText, "if it fails"))
			{
				pText = "Team save in progress. You'll be able to load with '/load *** *** ***' if save is successful or with '/load *** *** ***' if it fails";
			}
			else if(str_startswith(Line.m_aText, "Team successfully saved by ") && str_endswith(Line.m_aText, " to continue"))
			{
				pText = "Team successfully saved by ***. Use '/load *** *** ***' to continue";
			}
		}

		const CColoredParts ColoredParts(pText, Line.m_ClientId == CLIENT_MSG);
		if(!ColoredParts.Colors().empty() && ColoredParts.Colors()[0].m_Index == 0)
			Line.m_CustomColor = ColoredParts.Colors()[0].m_Color;
		pText = ColoredParts.Text();
		Line.m_RenderSponsorChatStyle = QmSponsorChatMessageStyle(
			g_Config.m_QmSponsorChatEffects ? Line.m_SponsorChatStyle : EQmSponsorChatStyle::NONE,
			Line.m_Highlighted, Line.m_Team, Line.m_Whisper,
			Line.m_CustomColor.has_value() || !ColoredParts.Colors().empty(), Line.m_ChatEmoji != EQmChatEmoji::NONE);

		const char *pTranslatedError = nullptr;
		const char *pTranslatedText = nullptr;
		const char *pTranslatedLanguage = nullptr;
		if(Line.m_pTranslateResponse != nullptr && Line.m_pTranslateResponse->m_Text[0])
		{
			// If hidden and there is translated text
			if(pText != Line.m_aText)
			{
				pTranslatedError = Localize("Translated text hidden due to streamer mode");
			}
			else if(Line.m_pTranslateResponse->m_Error)
			{
				pTranslatedError = Line.m_pTranslateResponse->m_Text;
			}
			else
			{
				pTranslatedText = Line.m_pTranslateResponse->m_Text;
				if(Line.m_pTranslateResponse->m_Language[0] != '\0')
					pTranslatedLanguage = Line.m_pTranslateResponse->m_Language;
			}
		}

		// get the y offset (calculate it if we haven't done that yet)
		if(Line.m_aYOffset[OffsetType] < 0.0f)
		{
			CTextCursor MeasureCursor;
			MeasureCursor.SetPosition(vec2(TextBegin, 0.0f));
			MeasureCursor.m_FontSize = FontSize;
			MeasureCursor.m_Flags = 0;
			MeasureCursor.m_LineWidth = LineWidth;
			MeasureCursor.m_LineSpacing = 2.0f * TitleBobPadding;

			if(!MultipleAuthors && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
			{
				MeasureCursor.m_X += RealMsgPaddingTee;

				if(Line.m_Friend && g_Config.m_ClMessageFriend)
				{
					MeasureSolidFriendHeart(TextRender(), &MeasureCursor);
				}
			}

			TextRender()->TextEx(&MeasureCursor, aClientId);
			if(MergedPlayerMessages)
			{
				for(size_t i = 0; i < Line.m_vMergedAuthors.size(); ++i)
				{
					if(i > 0)
						TextRender()->TextEx(&MeasureCursor, ",");
					TextRender()->TextEx(&MeasureCursor, VisibleTitle(Line.m_vMergedAuthors[i].m_aQmTitle, Line.m_vMergedAuthors[i].m_ClientId));
					TextRender()->TextEx(&MeasureCursor, Line.m_vMergedAuthors[i].m_aName);
				}
			}
			else
			{
				TextRender()->TextEx(&MeasureCursor, VisibleTitle(Line.m_aQmTitle, Line.m_ClientId));
				TextRender()->TextEx(&MeasureCursor, Line.m_aName);
			}
			if(Line.m_TimesRepeated > 0)
				TextRender()->TextEx(&MeasureCursor, aCount);

			if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
			{
				TextRender()->TextEx(&MeasureCursor, MultipleAuthors ? ":\n" : ": ");
			}

			CTextCursor AppendCursor = MeasureCursor;
			AppendCursor.m_LongestLineWidth = 0.0f;
			if(MultipleAuthors)
			{
				QmChatApplyMergedMessageIndent(AppendCursor, FirstAuthorPrefixWidth);
			}
			else if(IndentMessage)
			{
				QmChatApplyMessageIndent(AppendCursor);
			}

			if(RenderChatEmoji)
			{
				const SQmChatEmojiCursorLayout EmojiLayout = LayoutQmChatEmoji(AppendCursor, QmChatEmojiChatDisplaySize(FontSize));
				Line.m_aYOffset[OffsetType] = maximum(AppendCursor.Height(), EmojiLayout.m_RequiredHeight + 2.0f * TitleBobPadding) + RealMsgPaddingY;
			}
			else if(pTranslatedText || pTranslatedError)
			{
				Line.m_aTextBlockLayout[OffsetType] = QmChatMeasureTextBlocks(AppendCursor, QmChatTranslationFontSize(FontSize, g_Config.m_QmChatTranslationSize), [&](CTextCursor &Cursor) { TextRender()->TextEx(&Cursor, pText); }, [&](CTextCursor &Cursor) {
						TextRender()->TextEx(&Cursor, pTranslatedText ? pTranslatedText : pTranslatedError);
						if(pTranslatedLanguage)
						{
							TextRender()->TextEx(&Cursor, " [");
							TextRender()->TextEx(&Cursor, pTranslatedLanguage);
							TextRender()->TextEx(&Cursor, "]");
						} });
				Line.m_aYOffset[OffsetType] = Line.m_aTextBlockLayout[OffsetType].m_Height + RealMsgPaddingY;
			}
			else
			{
				TextRender()->TextEx(&AppendCursor, pText);
			}

			if(!RenderChatEmoji && !pTranslatedText && !pTranslatedError)
				Line.m_aYOffset[OffsetType] = AppendCursor.Height() + RealMsgPaddingY;
		}

		const float LineHeight = Line.m_aYOffset[OffsetType];
		const float LayoutVisibility = ClampPresentationProgress(Line.m_Presentation.m_LayoutVisibility);
		const float LayoutBottom = y;
		y -= LineHeight * LayoutVisibility;
		// 超出 HUD 的聊天高度预算：停止准备更旧消息。
		if(y < HeightLimit)
			break;
		const float TargetY = LayoutBottom - LineHeight;

		const float TextYOffset = TargetY + RealMsgPaddingY / 2.0f;
		if(Line.m_TextYOffset != TextYOffset)
			Line.DeleteTextContainers(TextRender());
		Line.m_TextYOffset = TextYOffset;

		int CurRenderFlags = TextRender()->GetRenderFlags();
		TextRender()->SetRenderFlags(CurRenderFlags | ETextRenderFlags::TEXT_RENDER_FLAG_NO_AUTOMATIC_QUAD_UPLOAD);

		// reset the cursor
		CTextCursor LineCursor;
		const float PrimaryVisualOffset = pTranslatedText || pTranslatedError ? Line.m_aTextBlockLayout[OffsetType].m_PrimaryOffset : 0.0f;
		LineCursor.SetPosition(vec2(TextBegin, Line.m_TextYOffset + TitleBobPadding + PrimaryVisualOffset));
		LineCursor.m_FontSize = FontSize;
		LineCursor.m_LineWidth = LineWidth;
		LineCursor.m_LineSpacing = 2.0f * TitleBobPadding;
		if(Line.m_TextContainerIndex.Valid())
		{
			CTextCursor ClearCursor = LineCursor;
			TextRender()->RecreateTextContainerSoft(Line.m_TextContainerIndex, &ClearCursor, "");
		}
		Line.m_vTitleTextMetrics.resize(maximum(size_t(1), Line.m_vMergedAuthors.size()));
		size_t TitleMetricsIndex = 0;
		CQmTitleTextMetrics::SContext TitleMetricsContext;
		TitleMetricsContext.m_FontSize = FontSize;
		TitleMetricsContext.m_ScreenScale = vec2(Graphics()->ScreenWidth() / (ScreenX1 - ScreenX0), Graphics()->ScreenHeight() / (ScreenY1 - ScreenY0));
		TitleMetricsContext.m_RenderFlags = TextRender()->GetRenderFlags();
		TitleMetricsContext.m_FontPreset = (int)TextRender()->GetFontPreset();

		// Message is from valid player
		if(!MultipleAuthors && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		{
			LineCursor.m_X += RealMsgPaddingTee;

			if(Line.m_Friend && g_Config.m_ClMessageFriend)
			{
				AppendSolidFriendHeart(
					TextRender(),
					Line.m_TextContainerIndex,
					&LineCursor,
					color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendHeartColor)));
			}
		}

		// render name
		ColorRGBA NameColor;
		if(Line.m_CustomColor)
			NameColor = *Line.m_CustomColor;
		else if(Line.m_ClientId == SERVER_MSG)
			NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
		else if(Line.m_ClientId == CLIENT_MSG)
			NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
		else
			NameColor = PlayerNameColor(Line.m_ClientId, Line.m_NameColor, Line.m_Team);

		const auto AppendQmTitle = [&](const char *pTitle, const ColorRGBA &FallbackColor, int AuthorId) {
			pTitle = VisibleTitle(pTitle, AuthorId);
			CQmTitleTextMetrics &Metrics = Line.m_vTitleTextMetrics[TitleMetricsIndex++];
			const bool CustomColor = pTitle[0] != '\0' && TitleColorStyle.m_Mode != EQmTitleColorMode::FOLLOW_SERVER;
			const SQmTitleRenderStyle Style = QmTitleResolveRenderStyle(GameClient()->m_QmClient.PlayerTitleStyle(AuthorId));
			if(pTitle[0] != '\0' && Style.m_pStyle != nullptr)
			{
				Metrics.Update(pTitle, TitleMetricsContext, [&](const char *pPrefix) { return TextRender()->TextWidth(FontSize, pPrefix); });
				if(Style.m_ColorOverride)
					QmTitleRenderFillMotionOffsets(TextRender(), LineCursor, pTitle, LineCursor.m_FontSize, Style, TitleAnimationTime, TitleShimmer, &Metrics);
				else
					QmTitleRenderFillCursor(TextRender(), LineCursor, pTitle, LineCursor.m_FontSize, Style, TitleAnimationTime, 1.0f, TitleShimmer, &Metrics);
				if(CustomColor && TitleColorStyle.m_Rainbow)
					QmAddTitleRainbowSplits(LineCursor, pTitle, TitleColorStyle.m_Alpha);
				else if(CustomColor)
					TextRender()->TextColor(TitleColorStyle.m_Color);
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, pTitle);
				LineCursor.m_vColorSplits.clear();
				LineCursor.m_vCharOffsets.clear();
				if(CustomColor)
					TextRender()->TextColor(FallbackColor);
				return;
			}
			if(!CustomColor)
			{
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, pTitle);
				return;
			}
			if(TitleColorStyle.m_Rainbow)
				QmAddTitleRainbowSplits(LineCursor, pTitle, TitleColorStyle.m_Alpha);
			else
				TextRender()->TextColor(TitleColorStyle.m_Color);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, pTitle);
			LineCursor.m_vColorSplits.clear();
			TextRender()->TextColor(FallbackColor);
		};

		if(MergedPlayerMessages)
		{
			TextRender()->TextColor(Line.m_vMergedAuthors.front().m_NameColor);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, aClientId);
			for(size_t i = 0; i < Line.m_vMergedAuthors.size(); ++i)
			{
				TextRender()->TextColor(Line.m_vMergedAuthors[i].m_NameColor);
				if(i > 0)
					TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, ",");
				AppendQmTitle(Line.m_vMergedAuthors[i].m_aQmTitle, Line.m_vMergedAuthors[i].m_NameColor, Line.m_vMergedAuthors[i].m_ClientId);
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, Line.m_vMergedAuthors[i].m_aName);
			}
			NameColor = Line.m_vMergedAuthors.back().m_NameColor;
		}
		else
		{
			TextRender()->TextColor(NameColor);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, aClientId);
			AppendQmTitle(Line.m_aQmTitle, NameColor, Line.m_ClientId);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, Line.m_aName);
		}

		if(Line.m_TimesRepeated > 0)
		{
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.3f);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, aCount);
		}

		if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		{
			TextRender()->TextColor(NameColor);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, MultipleAuthors ? ":\n" : ": ");
		}

		ColorRGBA Color;
		EQmChatGradientRole GradientRole = EQmChatGradientRole::NORMAL;
		if(Line.m_CustomColor)
		{
			Color = *Line.m_CustomColor;
		}
		else if(Line.m_ClientId == SERVER_MSG)
		{
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
			GradientRole = EQmChatGradientRole::SYSTEM;
		}
		else if(Line.m_ClientId == CLIENT_MSG)
		{
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
			GradientRole = EQmChatGradientRole::CLIENT;
		}
		else if(Line.m_Highlighted)
		{
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
			GradientRole = EQmChatGradientRole::HIGHLIGHT;
		}
		else if(Line.m_Friend && g_Config.m_ClMessageFriend)
		{
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor));
			GradientRole = EQmChatGradientRole::FRIEND;
		}
		else if(Line.m_Team)
		{
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
			GradientRole = EQmChatGradientRole::TEAM;
		}
		else // regular message
		{
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));
			GradientRole = EQmChatGradientRole::NORMAL;
		}
		if(Line.m_RenderSponsorChatStyle == EQmSponsorChatStyle::PLATINUM)
			Color = QmSponsorChatPlatinumColor(0.0f, Color.a);
		TextRender()->TextColor(Color);

		CTextCursor AppendCursor = LineCursor;
		AppendCursor.m_TrackLineRanges = Line.m_RenderSponsorChatStyle == EQmSponsorChatStyle::PLATINUM;
		AppendCursor.m_LongestLineWidth = 0.0f;
		if(MultipleAuthors)
		{
			QmChatApplyMergedMessageIndent(AppendCursor, FirstAuthorPrefixWidth);
		}
		else if(IndentMessage)
		{
			QmChatApplyMessageIndent(AppendCursor);
		}

		STextContainerIndex &BodyContainer = Line.m_RenderSponsorChatStyle != EQmSponsorChatStyle::NONE ? Line.m_BodyTextContainerIndex : Line.m_TextContainerIndex;
		if(Line.m_BodyTextContainerIndex.Valid())
		{
			if(Line.m_RenderSponsorChatStyle == EQmSponsorChatStyle::NONE)
				TextRender()->DeleteTextContainer(Line.m_BodyTextContainerIndex);
			else
			{
				CTextCursor ClearCursor = AppendCursor;
				TextRender()->RecreateTextContainerSoft(Line.m_BodyTextContainerIndex, &ClearCursor, "");
			}
		}
		const auto AppendMessage = [&](CTextCursor &Cursor, const char *pMessage, bool UseGradient, bool UseColoredParts) {
			const bool Platinum = Line.m_RenderSponsorChatStyle == EQmSponsorChatStyle::PLATINUM;
			if(UseGradient && Platinum)
				QmSponsorChatAddPlatinumSplits(Cursor, pMessage, Color.a);
			if(UseColoredParts)
				ColoredParts.AddSplitsToCursor(Cursor);
			const bool SpatialGradient = UseGradient && !Platinum && Line.m_CustomColor == std::nullopt && ColoredParts.Colors().empty();
			const auto Gradient = QmChatGradientStyle(g_Config, GradientRole, Color);
			const CQmChatGradientPaint Paint(TextRender(), Cursor, pMessage, SpatialGradient ? &Gradient : nullptr);
			TextRender()->CreateOrAppendTextContainer(BodyContainer, &Cursor, pMessage);
			Cursor.m_vColorSplits.clear();
		};

		if(RenderChatEmoji)
		{
			const SQmChatEmojiCursorLayout EmojiLayout = LayoutQmChatEmoji(AppendCursor, QmChatEmojiChatDisplaySize(FontSize));
			Line.m_ChatEmojiRect = EmojiLayout.m_Rect;
		}
		else if(pTranslatedText || pTranslatedError)
		{
			AppendCursor.m_CalculateVisualBoundingBox = true;
			AppendMessage(AppendCursor, pText, true, true);
			const float OriginalWidth = AppendCursor.m_LongestLineWidth;
			CTextCursor SecondaryCursor = QmChatSecondaryCursor(AppendCursor,
				QmChatTranslationFontSize(FontSize, g_Config.m_QmChatTranslationSize), Line.m_aTextBlockLayout[OffsetType].m_SecondaryOffset);
			ColorRGBA ColorSub = Color;
			if(pTranslatedError)
			{
				ColorSub.r = 0.7f;
				ColorSub.g = 0.6f;
				ColorSub.b = 0.6f;
			}
			else
			{
				ColorSub.r *= 0.7f;
				ColorSub.g *= 0.7f;
				ColorSub.b *= 0.7f;
			}
			TextRender()->TextColor(ColorSub);
			AppendMessage(SecondaryCursor, pTranslatedText ? pTranslatedText : pTranslatedError, pTranslatedError == nullptr, false);
			if(pTranslatedLanguage)
			{
				ColorRGBA ColorLang = ColorSub;
				ColorLang.r *= 0.8f;
				ColorLang.g *= 0.8f;
				ColorLang.b *= 0.8f;
				TextRender()->TextColor(ColorLang);
				TextRender()->CreateOrAppendTextContainer(BodyContainer, &SecondaryCursor, " [");
				TextRender()->CreateOrAppendTextContainer(BodyContainer, &SecondaryCursor, pTranslatedLanguage);
				TextRender()->CreateOrAppendTextContainer(BodyContainer, &SecondaryCursor, "]");
			}
			AppendCursor = std::move(SecondaryCursor);
			AppendCursor.m_LongestLineWidth = maximum(OriginalWidth, AppendCursor.m_LongestLineWidth);
			TextRender()->TextColor(Color);
		}
		else
		{
			AppendMessage(AppendCursor, pText, true, true);
		}

		if(Line.m_aText[0] != '\0' || Line.m_aName[0] != '\0')
		{
			float FullWidth = RealMsgPaddingX * 1.5f;
			if(IndentMessage)
			{
				FullWidth += QmChatIndentedContentWidth(LineCursor, AppendCursor);
			}
			else
			{
				FullWidth += maximum(LineCursor.m_LongestLineWidth, AppendCursor.m_LongestLineWidth);
			}
			Line.m_ContentWidth = maximum(0.0f, FullWidth);
			Line.m_SponsorTextBounds = {Begin, TargetY, FullWidth, LineHeight};
			if(!g_Config.m_ClChatOld)
			{
				const CUIRect Rect = {Begin, TargetY, FullWidth, LineHeight};
				const float Rounding = MessageRounding();
				if(Line.m_QuadContainerIndex == -1 || Line.m_BackgroundRect.x != Rect.x || Line.m_BackgroundRect.y != Rect.y ||
					Line.m_BackgroundRect.w != Rect.w || Line.m_BackgroundRect.h != Rect.h || Line.m_BackgroundRounding != Rounding)
				{
					Graphics()->DeleteQuadContainer(Line.m_QuadContainerIndex);
					Graphics()->SetColor(1, 1, 1, 1);
					Line.m_QuadContainerIndex = Graphics()->CreateRectQuadContainer(Begin, TargetY, FullWidth, LineHeight, Rounding, IGraphics::CORNER_ALL);
					Line.m_BackgroundRect = Rect;
					Line.m_BackgroundRounding = Rounding;
				}
			}
		}

		TextRender()->SetRenderFlags(CurRenderFlags);
		if(Line.m_TextContainerIndex.Valid())
			TextRender()->UploadTextContainer(Line.m_TextContainerIndex);
		if(Line.m_BodyTextContainerIndex.Valid())
			TextRender()->UploadTextContainer(Line.m_BodyTextContainerIndex);
		if(Line.m_ClientId == SERVER_MSG && Line.m_TextContainerIndex.Valid() && QmMacosGraphicsDiagnosticsEnabled())
		{
			char aPayload[192];
			str_format(aPayload, sizeof(aPayload), "event=server_message_upload_requested class=%d text_container=%d text_buffering=%d",
				static_cast<int>(Line.m_ServerMessageClass), Line.m_TextContainerIndex.m_Index, Graphics()->IsTextBufferingEnabled() ? 1 : 0);
			QmMacosGraphicsDiagnosticsLogPayload("perf/autodiag_chat", aPayload, Client());
		}
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
	return EmojiLayoutChanged;
}

void CChat::OnRender()
{
	m_TranslateButton.m_RectValid = false;
	FlushPendingConsoleLine(false);
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		m_CommandHud.Hide();
		return;
	}
	const SQmFocusModeDecisions Focus = GetQmFocusModeDecisions();
	const bool FocusHideChat = Focus.m_HidePlayerMessages;
	const bool FocusHideSystemInfoMessages = Focus.m_HideSystemInfoMessages;
	const bool FocusHideSystemPromptMessages = Focus.m_HideSystemPromptMessages;
	const bool FocusHideEcho = Focus.m_HideEchoMessages;
	const bool HasForceVisibleLine = std::any_of(std::begin(m_aLines), std::end(m_aLines), [](const CLine &Line) { return Line.m_Initialized && Line.m_ForceVisible; });
	if(!ShouldRenderAnyFocusFilteredChat(FocusHideChat, FocusHideSystemInfoMessages, FocusHideSystemPromptMessages, FocusHideEcho, HasForceVisibleLine))
	{
		m_CommandHud.Hide();
		return;
	}

	const bool HudEditorPreview = GameClient()->m_HudEditor.IsActive();
	const bool InputActive = m_Mode != MODE_NONE;
	const bool ShowLargeArea =
		m_Show ||
		(InputActive && g_Config.m_ClShowChat == 1) ||
		g_Config.m_ClShowChat == 2;
	const bool ExtraAnimations = g_Config.m_QmExtraAnimations != 0 && GameClient()->UiRuntimeV2()->Enabled();
	int64_t Now = time();
	if(m_LastPresentationUpdateTime == 0 || Now < m_LastPresentationUpdateTime)
		m_LastPresentationUpdateTime = Now;
	float DeltaSeconds = std::clamp((Now - m_LastPresentationUpdateTime) / (float)time_freq(), 0.0f, CHAT_PRESENTATION_MAX_DELTA_SECONDS);
	const float ChatFadeDurationSeconds = g_Config.m_QmChatAnimFadeDurationMs / 1000.0f;
	(void)ChatFadeDurationSeconds;
	m_LastPresentationUpdateTime = Now;
	if(HudEditorPreview)
		DeltaSeconds = 0.0f;
	else
		UpdatePresentationStates(Now, DeltaSeconds, ShowLargeArea, ExtraAnimations);

	// send pending chat messages
	if(m_PendingChatCounter > 0 && m_LastChatSend + time_freq() < time())
	{
		CHistoryEntry *pEntry = m_History.Last();
		for(int i = m_PendingChatCounter - 1; pEntry; --i, pEntry = m_History.Prev(pEntry))
		{
			if(i == 0)
			{
				SendChat(pEntry->m_Team, pEntry->m_aText);
				break;
			}
		}
		--m_PendingChatCounter;
	}

	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);
	const bool ChatScrollbarOnRight = g_Config.m_QmChatScrollbarRight != 0;
	const CUIRect ChatRect = {0.0f, 50.0f, std::min(Width, std::max(190.0f, g_Config.m_ClChatWidth + 32.0f)), 250.0f};
	const auto HudEditorScope = GameClient()->m_HudEditor.BeginTransform(EHudEditorElement::Chat, ChatRect);
	float MapX0, MapY0, MapX1, MapY1;
	Graphics()->GetScreen(&MapX0, &MapY0, &MapX1, &MapY1);
	m_ChatInputMapRect = {MapX0, MapY0, MapX1 - MapX0, MapY1 - MapY0};

	float x = QmChatHistoryStartX(g_Config.m_QmChatScrollbarRight != 0);
	CQmChatVisibleBounds VisibleBounds(ChatRect);
	auto ExtendBounds = [&](float X, float Y, float W, float H) {
		VisibleBounds.Extend({X, Y, W, H});
	};

	// TClient
	float y = 300.0f - (20.0f * FontSize() / 6.0f + (g_Config.m_QmStatusBar ? g_Config.m_QmStatusBarHeight : 0.0f));
	// float y = 300.0f - 20.0f * FontSize() / 6.0f;

	float ScaledFontSize = FontSize() * (8.0f / 6.0f);
	const float CommandPreviewFontSize = ScaledFontSize * 0.5f;
	const float InputLineWidth = std::max(Width - 190.0f, 190.0f);
	const char *pInputModeLabel = m_Mode == MODE_ALL ? Localize("All") : (m_Mode == MODE_TEAM ? Localize("Team") : Localize("Chat"));
	const float InputPrefixWidth = TextRender()->TextWidth(ScaledFontSize, pInputModeLabel) + TextRender()->TextWidth(ScaledFontSize, ": ");
	// 翻译按钮在输入行最左侧，行宽先扣除按钮区，再扣除「全体」等前缀。
	const float CommandPreviewMaxWidth = QmChatInputMessageWidth(InputLineWidth, ScaledFontSize, InputPrefixWidth);
	char aCommandPreview[MAX_LINE_LENGTH];
	const bool HasCommandPreview = m_Mode != MODE_NONE && BuildCommandUsagePreview(m_Input.GetString(), aCommandPreview, sizeof(aCommandPreview));
	CUIRect InputBlockRect = {};
	bool InputBlockRectValid = false;
	if(HasCommandPreview)
	{
		// 提示显示在输入行下方，先把输入行整体上移，避免与底部历史消息重叠
		const STextBoundingBox PreviewBoundingBox = TextRender()->TextBoundingBox(CommandPreviewFontSize, aCommandPreview, -1, CommandPreviewMaxWidth);
		y -= PreviewBoundingBox.m_H + 4.0f;
	}

	if(InputActive)
	{
		const SQmChatInputLayout InputLayout = QmChatResolveInputLayout(x, y, InputLineWidth, ScaledFontSize, InputPrefixWidth);

		// render chat input
		CTextCursor InputCursor;
		InputCursor.SetPosition(vec2(InputLayout.m_TextStartX, y));
		InputCursor.m_FontSize = ScaledFontSize;
		InputCursor.m_LineWidth = InputLayout.m_CursorLineWidth;

		TextRender()->TextEx(&InputCursor, pInputModeLabel);

		TextRender()->TextEx(&InputCursor, ": ");

		const float MessageMaxWidth = InputLayout.m_MessageMaxWidth;
		const float InputContentHeight = 2.25f * InputCursor.m_FontSize;
		const float InputClipPaddingTop = maximum(1.0f, InputCursor.m_FontSize * 0.18f);
		const float InputClipPaddingBottom = maximum(1.0f, InputCursor.m_FontSize * 0.10f);
		const float InputClipPaddingX = maximum(1.0f, InputCursor.m_FontSize * 0.18f);
		const float InputClipPaddingRight = minimum(InputClipPaddingX, QM_CHAT_TRANSLATE_BUTTON_GAP - 1.0f);
		const CUIRect InputContentRect = {InputCursor.m_X, InputCursor.m_Y, MessageMaxWidth, InputContentHeight};
		const CUIRect InputClippingRect = {InputContentRect.x - InputClipPaddingX, InputContentRect.y - InputClipPaddingTop, InputContentRect.w + InputClipPaddingX + InputClipPaddingRight, InputContentRect.h + InputClipPaddingTop + InputClipPaddingBottom};
		InputBlockRect = {x, InputContentRect.y, InputLineWidth, InputContentRect.h};
		InputBlockRectValid = true;
		ExtendBounds(x, InputClippingRect.y, InputClippingRect.x + InputClippingRect.w - x, InputClippingRect.h);
		const SQmChatInputPixelClip InputPixelClip = QmChatInputPixelClip(InputClippingRect,
			m_ChatInputMapRect, Graphics()->ScreenWidth(), Graphics()->ScreenHeight());
		Graphics()->ClipEnable(InputPixelClip.m_X, InputPixelClip.m_Y, InputPixelClip.m_W, InputPixelClip.m_H);

		float ScrollOffset = m_Input.GetScrollOffset();
		float ScrollOffsetChange = m_Input.GetScrollOffsetChange();

		m_Input.Activate(EInputPriority::CHAT); // Ensure that the input is active
		// 输入内容与前缀标签（「全体」/「队伍」/「聊天」，见上方 pInputModeLabel 的 TextEx）
		// 共用 InputContentRect.y 作为首行起点。防裁切只需把 InputClippingRect 向上扩
		// InputClipPaddingTop，不能在文本这里再加一次，否则输入内容会比前缀低一个 padding。
		const CUIRect InputCursorRect = {InputContentRect.x, InputContentRect.y - ScrollOffset, 0.0f, 0.0f};
		const bool WasChanged = m_Input.WasChanged();
		const bool WasCursorChanged = m_Input.WasCursorChanged();
		const bool Changed = WasChanged || WasCursorChanged;
		const STextBoundingBox BoundingBox = m_Input.Render(&InputCursorRect, InputCursor.m_FontSize, TEXTALIGN_TL, Changed, MessageMaxWidth, 0.0f);

		Graphics()->ClipDisable();

		// Scroll up or down to keep the caret inside the content rect.
		// 光标与文本同基准（首行起点即 InputContentRect.y），无需再扣除顶部 padding。
		const float CaretPositionY = m_Input.GetCaretPosition().y - ScrollOffsetChange;
		if(CaretPositionY < InputContentRect.y)
			ScrollOffsetChange -= InputContentRect.y - CaretPositionY;
		else if(CaretPositionY + InputCursor.m_FontSize > InputContentRect.y + InputContentRect.h)
			ScrollOffsetChange += CaretPositionY + InputCursor.m_FontSize - (InputContentRect.y + InputContentRect.h);

		Ui()->DoSmoothScrollLogic(&ScrollOffset, &ScrollOffsetChange, InputContentRect.h, BoundingBox.m_H);

		m_Input.SetScrollOffset(ScrollOffset);
		m_Input.SetScrollOffsetChange(ScrollOffsetChange);

		// 补全提示也属于正文区域，不能跨过独立的翻译按钮操作区。
		Graphics()->ClipEnable(InputPixelClip.m_X, InputPixelClip.m_Y, InputPixelClip.m_W, InputPixelClip.m_H);
		// 自动补全提示：以半透明文字显示当前补全命令的剩余部分（与官方 DDNet 一致）
		if(m_Input.GetString()[0] == '/' && m_Input.GetString()[1] != '\0' && !m_vServerCommands.empty())
		{
			for(const auto &Command : m_vServerCommands)
			{
				if(str_startswith_nocase(Command.m_aName, m_Input.GetString() + 1))
				{
					InputCursor.m_X = InputCursor.m_X + TextRender()->TextWidth(InputCursor.m_FontSize, m_Input.GetString(), -1, InputCursor.m_LineWidth);
					InputCursor.m_Y = m_Input.GetCaretPosition().y;
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.5f);
					TextRender()->TextEx(&InputCursor, Command.m_aName + str_length(m_Input.GetString() + 1));
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					break;
				}
			}
		}
		else
		{
			// 表情补全提示：唯一匹配时显示剩余字符；多候选时以逗号分隔展示所有可能码
			const int HintColonLength = QmChatEmojiColonUtf8Length(m_Input.GetString());
			if(HintColonLength > 0)
			{
				const SQmChatEmojiDefinition *apHintMatches[QM_CHAT_EMOJI_COUNT];
				const int NumHintMatches = QmChatEmojiCollectByPrefix(m_Input.GetString() + HintColonLength, apHintMatches, (int)QM_CHAT_EMOJI_COUNT);
				if(NumHintMatches > 0)
				{
					char aHint[128];
					if(NumHintMatches == 1)
					{
						const char *pTyped = m_Input.GetString() + HintColonLength;
						const char *pMatch = apHintMatches[0]->m_pText + 1;
						str_copy(aHint, pMatch + str_length(pTyped));
					}
					else
					{
						QmChatEmojiFormatCandidates(apHintMatches, NumHintMatches, aHint, sizeof(aHint));
					}
					if(aHint[0] != '\0')
					{
						InputCursor.m_X = InputCursor.m_X + TextRender()->TextWidth(InputCursor.m_FontSize, m_Input.GetString(), -1, InputCursor.m_LineWidth);
						InputCursor.m_Y = m_Input.GetCaretPosition().y;
						TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.5f);
						TextRender()->TextEx(&InputCursor, aHint);
						TextRender()->TextColor(TextRender()->DefaultTextColor());
					}
				}
			}
		}

		Graphics()->ClipDisable();

		// 斜杠指令用法提示：在输入行下方显示一行小字说明当前指令的作用
		if(HasCommandPreview)
		{
			CTextCursor PreviewCursor;
			const float PreviewY = InputContentRect.y + minimum(BoundingBox.m_H, InputContentRect.h) + 2.0f;
			PreviewCursor.SetPosition(vec2(InputContentRect.x, PreviewY));
			PreviewCursor.m_FontSize = CommandPreviewFontSize;
			PreviewCursor.m_LineWidth = MessageMaxWidth;
			PreviewCursor.m_Flags = TEXTFLAG_RENDER;
			TextRender()->TextColor(0.72f, 0.88f, 1.0f, 0.78f);
			TextRender()->TextEx(&PreviewCursor, aCommandPreview);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			ExtendBounds(PreviewCursor.m_StartX, PreviewCursor.m_StartY, MessageMaxWidth, PreviewCursor.Height());
		}

		// 渲染翻译按钮：固定在输入行最左侧，「全体: 」等前缀从按钮右侧开始。
		const CUIRect TranslateButtonRect = {InputLayout.m_ButtonX, InputLayout.m_ButtonY, InputLayout.m_ButtonW, InputLayout.m_ButtonH};
		ExtendBounds(TranslateButtonRect.x, TranslateButtonRect.y, TranslateButtonRect.w, TranslateButtonRect.h);
		RenderTranslateButton(TranslateButtonRect);
		if(!Input()->HasComposition() && !HudEditorPreview && !GameClient()->m_Menus.IsActive() && !m_LanguageMenuOpen && !Ui()->IsPopupOpen(&m_LanguagePopupContext) && !Ui()->IsPopupOpen(&m_ChatLinePopupContext))
		{
			const vec2 CaretPosition = m_Input.GetCaretPosition();
			RenderCommandHud(CaretPosition.x, CaretPosition.y - 4.0f * CQmChatCommandHud::UI_SCALE, minimum(InputLineWidth, ChatRect.w - x), ScaledFontSize, ChatRect, HudEditorScope.m_Applied ? HudEditorScope.m_TargetRect : ChatRect);
			const auto &CommandLayout = m_CommandHud.Layout();
			if(CommandLayout.m_VisibleRows > 0)
			{
				ExtendBounds(CommandLayout.m_X, CommandLayout.m_Y, CommandLayout.m_W, CommandLayout.m_H);
				y = minimum(y, CommandLayout.m_Y - 2.0f);
			}
		}
		else
			m_CommandHud.Hide();
	}
	else
	{
		m_TranslateButton.m_RectValid = false;
		m_CommandHud.Hide();
	}

#if defined(CONF_VIDEORECORDER)
	const bool VideoRendering = IVideo::Current() != nullptr;
#else
	const bool VideoRendering = false;
#endif
	// 回放/导出走独立显示选项；其余情况沿用 cl_showchat / cl_video_showchat。
	if(!qm_demo_display::Resolve(g_Config, Client()->State() == IClient::STATE_DEMOPLAYBACK, VideoRendering).m_Chat)
	{
		GameClient()->m_HudEditor.EndTransform(HudEditorScope);
		return;
	}

	y -= ScaledFontSize;

	const bool IsScoreBoardOpen = GameClient()->m_Scoreboard.IsActive();
	const float HeightLimit = IsScoreBoardOpen ? CHAT_HEIGHT_MIN + 130.0f : (ShowLargeArea ? CHAT_HEIGHT_MIN : CHAT_HEIGHT_FULL);
	int OffsetType = IsScoreBoardOpen ? 1 : 0;
	const ColorRGBA ConfigBackgroundColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClChatBackgroundColor, true));
	const ColorRGBA BackgroundBaseColor = g_Config.m_ClChatOld ? ConfigBackgroundColor : ConfigBackgroundColor.Multiply(ColorRGBA(0.78f, 0.86f, 1.0f, 1.0f));
	const ColorRGBA DefaultTextColor = TextRender()->DefaultTextColor();
	const ColorRGBA DefaultTextOutlineColor = TextRender()->DefaultTextOutlineColor();
	const CAnimState *pIdleState = CAnimState::GetIdle();
	const int TeeSize = MessageTeeSize();

	float RealMsgPaddingX = MessagePaddingX();
	float RealMsgPaddingY = MessagePaddingY();
	const float RowHeight = FontSize() + RealMsgPaddingY;

	if(g_Config.m_ClChatOld)
	{
		RealMsgPaddingX = 0;
		RealMsgPaddingY = 0;
	}

	const float HistoryBottom = y;
	const float HistoryHeight = maximum(0.0f, HistoryBottom - HeightLimit);
	const int TotalVisibleLines = CountVisibleLinesFrom(0);
	const int VisibleLineCapacity = maximum(1, (int)std::floor(HistoryHeight / maximum(RowHeight, 1.0f)));
	const int MaxScroll = maximum(0, TotalVisibleLines - VisibleLineCapacity);
	if(!InputActive)
		m_BacklogCurLine = 0;
	m_BacklogCurLine = ClampBacklogLine(m_BacklogCurLine, TotalVisibleLines, VisibleLineCapacity);

	const bool ShowChatScrollbar = InputActive && MaxScroll > 0 && HistoryHeight > 0.0f;
	const CUIRect ScrollbarRect = QmChatScrollbarRail(ChatRect, HeightLimit, HistoryHeight, ChatScrollbarOnRight);
	float ScrollbarHandleY = ScrollbarRect.y;
	float ScrollbarHandleH = ScrollbarRect.h;
	if(ShowChatScrollbar)
	{
		ScrollbarHandleH = QmChatScrollbarHandleHeight(ScrollbarRect.h, VisibleLineCapacity, TotalVisibleLines);
		const float TrackRange = maximum(0.0f, ScrollbarRect.h - ScrollbarHandleH);
		ScrollbarHandleY = QmChatScrollbarHandle(ScrollbarRect, ScrollbarHandleH, BacklogLineToScrollbarValue(m_BacklogCurLine, MaxScroll)).y;
		const vec2 MousePos = GetChatLocalMousePos();
		const bool InsideRail =
			MousePos.x >= ScrollbarRect.x &&
			MousePos.x <= ScrollbarRect.x + ScrollbarRect.w &&
			MousePos.y >= ScrollbarRect.y &&
			MousePos.y <= ScrollbarRect.y + ScrollbarRect.h;
		const bool InsideHandle =
			MousePos.x >= ScrollbarRect.x &&
			MousePos.x <= ScrollbarRect.x + ScrollbarRect.w &&
			MousePos.y >= ScrollbarHandleY &&
			MousePos.y <= ScrollbarHandleY + ScrollbarHandleH;

		if(Input()->KeyPress(KEY_MOUSE_1) && InsideRail)
		{
			m_ScrollbarDragging = true;
			m_ScrollbarDragOffset = InsideHandle ? std::clamp(MousePos.y - ScrollbarHandleY, 0.0f, ScrollbarHandleH) : ScrollbarHandleH * 0.5f;
		}
		if(!Input()->KeyIsPressed(KEY_MOUSE_1))
			m_ScrollbarDragging = false;
		if(m_ScrollbarDragging)
		{
			const float HandleTop = std::clamp(MousePos.y - m_ScrollbarDragOffset, ScrollbarRect.y, ScrollbarRect.y + TrackRange);
			const float RelativeTop = TrackRange > 0.0f ? (HandleTop - ScrollbarRect.y) / TrackRange : 1.0f;
			const int NewBacklogCurLine = ScrollbarValueToBacklogLine(RelativeTop, MaxScroll);
			if(NewBacklogCurLine != m_BacklogCurLine)
			{
				m_BacklogCurLine = NewBacklogCurLine;
				RebuildChat();
			}
			ScrollbarHandleY = QmChatScrollbarHandle(ScrollbarRect, ScrollbarHandleH, BacklogLineToScrollbarValue(m_BacklogCurLine, MaxScroll)).y;
		}
	}
	else
	{
		m_ScrollbarDragging = false;
	}

	const vec2 MousePos = GetChatLocalMousePos();
	const bool LanguageMenuOpen = m_LanguageMenuOpen || Ui()->IsPopupOpen(&m_LanguagePopupContext);
	const bool ChatLineMenuOpen = Ui()->IsPopupOpen(&m_ChatLinePopupContext);
	const bool MouseDown = Input()->KeyIsPressed(KEY_MOUSE_1);
	const bool InsideInputBlock =
		InputBlockRectValid &&
		MousePos.x >= InputBlockRect.x &&
		MousePos.x <= InputBlockRect.x + InputBlockRect.w &&
		MousePos.y >= InputBlockRect.y &&
		MousePos.y <= InputBlockRect.y + InputBlockRect.h;
	const bool InsideTranslateButton =
		m_TranslateButton.m_RectValid &&
		MousePos.x >= m_TranslateButton.m_X &&
		MousePos.x <= m_TranslateButton.m_X + m_TranslateButton.m_W &&
		MousePos.y >= m_TranslateButton.m_Y &&
		MousePos.y <= m_TranslateButton.m_Y + m_TranslateButton.m_H;
	const bool InsideScrollbar =
		ShowChatScrollbar &&
		MousePos.x >= ScrollbarRect.x &&
		MousePos.x <= ScrollbarRect.x + ScrollbarRect.w &&
		MousePos.y >= ScrollbarRect.y &&
		MousePos.y <= ScrollbarRect.y + ScrollbarRect.h;
	const vec2 CommandHudMousePos = GetChatMousePos();
	const bool InsideCommandHud = m_CommandHud.Contains(CommandHudMousePos.x, CommandHudMousePos.y);
	const bool ChatCopyActive = m_Mode != MODE_NONE && !LanguageMenuOpen && !ChatLineMenuOpen && !InsideInputBlock && !InsideTranslateButton && !InsideScrollbar && !InsideCommandHud && !m_CommandHud.IsPressed() && !m_ScrollbarDragging;
	const bool CopyClickReleased = m_MouseIsPress && !MouseDown && IsCopyClickDrag(m_MousePress, MousePos);
	// 菜单打开时也允许右键：在其它消息行重新定位菜单，在空白处关闭菜单；
	// 菜单自身区域内的右键不处理，避免与菜单按钮交互冲突。
	const CUIRect *pChatLineMenuRect = ChatLineMenuOpen ? Ui()->GetPopupMenuRect(&m_ChatLinePopupContext) : nullptr;
	const bool InsideChatLineMenu = pChatLineMenuRect != nullptr && pChatLineMenuRect->Inside(GetUiMousePos());
	const bool ChatLineMenuRequested = m_Mode != MODE_NONE && !LanguageMenuOpen && !InsideInputBlock && !InsideTranslateButton && !InsideScrollbar && !InsideCommandHud && !m_ScrollbarDragging && !InsideChatLineMenu && Input()->KeyPress(KEY_MOUSE_2);

	// 菜单打开时，左键按下非菜单区域立即关闭菜单。
	if(ChatLineMenuOpen && !InsideChatLineMenu && Input()->KeyPress(KEY_MOUSE_1))
		CloseChatLineMenu();
	if(ChatCopyActive)
	{
		if(!m_MouseIsPress && MouseDown)
		{
			m_MouseIsPress = true;
			m_MousePress = MousePos;
			m_MouseRelease = MousePos;
		}
		else if(m_MouseIsPress && MouseDown)
		{
			m_MouseRelease = MousePos;
		}
		else if(m_MouseIsPress)
		{
			m_MouseIsPress = false;
			m_MouseRelease = MousePos;
		}
	}
	else if(!MouseDown)
	{
		m_MouseIsPress = false;
	}

	const bool EmojiLayoutChanged = OnPrepareLines(y);

	bool RenderedAnyLines = false;
	bool HasVisibleSponsorMessage = false;
	const CLine *pClickedLine = nullptr;
	const CLine *pMenuLine = nullptr;

	for(int i = m_BacklogCurLine; i < MAX_LINES; i++)
	{
		const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
		CLine &Line = m_aLines[LineIndex];
		if(!Line.m_Initialized)
			break;
		const bool ServerMessageIsBasicInfo = Line.m_ServerMessageClass == QmHudNotifications::EServerMessageClass::BasicInfo;
		if(!ShouldRenderFocusFilteredChatLine(FocusHideChat, FocusHideSystemInfoMessages, FocusHideSystemPromptMessages, FocusHideEcho, Line.m_ClientId, Line.m_ForceVisible, ServerMessageIsBasicInfo))
		{
			continue;
		}
		if(!ShowLargeArea && !Line.m_ForceVisible && Line.m_Presentation.m_State == EPresentationState::COLLAPSED)
		{
			continue;
		}

		const bool LineHeightValid = Line.m_aYOffset[OffsetType] >= 0.0f;
		const float LineHeight = LineHeightValid ? Line.m_aYOffset[OffsetType] : FontSize() + RealMsgPaddingY;
		const float LayoutVisibility = ClampPresentationProgress(Line.m_Presentation.m_LayoutVisibility);
		const float LayoutBottom = y;
		y -= LineHeight * LayoutVisibility;
		if(y < HeightLimit)
			break;
		Line.m_Presentation.m_TargetY = LayoutBottom - LineHeight;

		// Don't abort the full render pass on a single malformed line.
		if(!LineHeightValid)
		{
			Line.m_CutOffProgress = 0.0f;
			continue;
		}

		if(!Line.m_Presentation.m_RenderYInitialized || HudEditorPreview || !ExtraAnimations || EmojiLayoutChanged)
		{
			Line.m_Presentation.m_RenderY = Line.m_Presentation.m_TargetY;
			Line.m_Presentation.m_RenderYInitialized = true;
		}
		else
			Line.m_Presentation.m_RenderY = SmoothPresentationY(Line.m_Presentation.m_RenderY, Line.m_Presentation.m_TargetY, DeltaSeconds);

		const float RenderY = Line.m_Presentation.m_RenderY + Line.m_Presentation.m_RenderOffsetY;
		Line.m_CutOffProgress = 0.0f;
		const float AnimAlpha = ClampPresentationProgress(Line.m_Presentation.m_RenderAlpha);
		const float AnimOffsetX = Line.m_Presentation.m_RenderOffsetX + CalculateCutOffOffsetX(Line.m_CutOffProgress);
		const float AnimOffsetY = (RenderY + RealMsgPaddingY / 2.0f) - Line.m_TextYOffset;

		if(AnimAlpha <= 0.001f)
			continue;

		// Fully transparent lines must not receive mouse interaction.
		if(AnimAlpha <= 0.001f)
			continue;

		const float RenderScale = maximum(0.0f, Line.m_Presentation.m_RenderScale);
		const float RenderedContentWidth = Line.m_ContentWidth * RenderScale;
		const CUIRect RenderedTextRect = {x + AnimOffsetX, RenderY, RenderedContentWidth, LineHeight * RenderScale};
		const bool MouseInsideLine = IsChatLineHit(RenderedTextRect, MousePos);
		if(CopyClickReleased && MouseInsideLine)
		{
			pClickedLine = &Line;
		}

		if(ChatLineMenuRequested && MouseInsideLine)
		{
			pMenuLine = &Line;
		}
		const bool IsPopupTarget =
			(ChatLineMenuOpen && m_ChatLinePopupContext.m_LineIndex == LineIndex &&
				m_ChatLinePopupContext.m_ClientId == Line.m_ClientId &&
				m_ChatLinePopupContext.m_TeamNumber == Line.m_TeamNumber &&
				str_comp(m_ChatLinePopupContext.m_aText, Line.m_aText) == 0) ||
			(ChatLineMenuRequested && pMenuLine == &Line);

		// Draw backgrounds for messages in one batch
		if(!g_Config.m_ClChatOld)
		{
			Graphics()->TextureClear();
			if(Line.m_QuadContainerIndex != -1)
			{
				const float QuadScale = RenderScale;
				const float QuadY = Line.m_TextYOffset - RealMsgPaddingY / 2.0f;
				const float QuadOffsetX = AnimOffsetX + x * (1.0f - QuadScale);
				const float QuadOffsetY = AnimOffsetY + QuadY * (1.0f - QuadScale);
				Graphics()->SetColor(BackgroundBaseColor.WithMultipliedAlpha(AnimAlpha));
				Graphics()->RenderQuadContainerEx(Line.m_QuadContainerIndex, 0, -1, QuadOffsetX, QuadOffsetY, QuadScale, QuadScale);
			}
		}
		if(IsPopupTarget && RenderedTextRect.w > 0.0f && RenderedTextRect.h > 0.0f)
		{
			Graphics()->TextureClear();
			const ColorRGBA SelectionColor = ColorRGBA(0.20f, 0.55f, 0.88f, 0.35f * AnimAlpha);
			const float Rounding = g_Config.m_ClChatOld ? 0.0f : MessageRounding();
			Graphics()->DrawRect(RenderedTextRect.x, RenderedTextRect.y, RenderedTextRect.w, RenderedTextRect.h, SelectionColor, IGraphics::CORNER_ALL, Rounding);
		}

		const bool RenderChatEmoji = Line.m_ChatEmojiRect.w > 0.0f && GameClient()->m_QmChatEmoji.CanRender(Line.m_ChatEmoji);
		if(Line.m_ClientId == SERVER_MSG && !RenderChatEmoji && !Line.m_TextContainerIndex.Valid() && QmMacosGraphicsDiagnosticsEnabled() && !Line.m_DiagnosticInvalidTextLogged)
		{
			char aPayload[192];
			str_format(aPayload, sizeof(aPayload), "event=server_message_render_skip reason=invalid_text_container class=%d text_container=%d", static_cast<int>(Line.m_ServerMessageClass), Line.m_TextContainerIndex.m_Index);
			QmMacosGraphicsDiagnosticsLogPayload("perf/autodiag_chat", aPayload, Client());
			Line.m_DiagnosticInvalidTextLogged = true;
		}
		if(Line.m_TextContainerIndex.Valid() || RenderChatEmoji)
		{
			RenderedAnyLines = true;
			// 横向入场动画不改变 HUD 锚点，避免贴边时边界反馈推动整个聊天区域。
			ExtendBounds(x, RenderY, ChatRect.w - x, LineHeight);
			if(Line.m_vMergedAuthors.size() <= 1 && !g_Config.m_ClChatOld && Line.m_pManagedTeeRenderInfo != nullptr)
			{
				CTeeRenderInfo &TeeRenderInfo = Line.m_pManagedTeeRenderInfo->TeeRenderInfo();
				TeeRenderInfo.m_Size = TeeSize * Line.m_Presentation.m_RenderScale;

				float OffsetTeeY = TeeSize / 2.0f;
				float FullHeightMinusTee = RowHeight - TeeSize;

				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeRenderInfo, OffsetToMid);
				vec2 TeeRenderPos(x + AnimOffsetX + (RealMsgPaddingX + TeeSize) / 2.0f, RenderY + Line.m_QmTitleBobPadding + OffsetTeeY + FullHeightMinusTee / 2.0f + OffsetToMid.y);
				RenderTools()->RenderTee(pIdleState, &TeeRenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), TeeRenderPos, AnimAlpha);
			}

			if(RenderChatEmoji)
			{
				GameClient()->m_QmChatEmoji.Render(
					Line.m_ChatEmoji,
					Line.m_ChatEmojiRect.x + AnimOffsetX,
					Line.m_ChatEmojiRect.y + AnimOffsetY,
					Line.m_ChatEmojiRect.w,
					Line.m_ChatEmojiRect.h,
					AnimAlpha);
			}

			if(Line.m_BodyTextContainerIndex.Valid())
			{
				const bool LatestVisibleSponsor = !HasVisibleSponsorMessage;
				if(Line.m_RenderSponsorChatStyle != EQmSponsorChatStyle::NONE)
					HasVisibleSponsorMessage = true;
				const double AgeSeconds = static_cast<double>(Now - Line.m_Time) / time_freq();
				SQmSponsorChatSweep SweepState;
				if(!HudEditorPreview && Line.m_RenderSponsorChatStyle == EQmSponsorChatStyle::PLATINUM)
					SweepState = QmSponsorChatSweepState(AgeSeconds, TextRender()->GetTextContainerRenderedLineCount(Line.m_BodyTextContainerIndex), LatestVisibleSponsor);
				m_SponsorChatRenderer.Render(Graphics(), TextRender(), Line.m_BodyTextContainerIndex,
					Line.m_RenderSponsorChatStyle, Line.m_SponsorTextBounds, FontSize(), AnimAlpha, AnimOffsetX, AnimOffsetY, SweepState);
			}

			if(Line.m_TextContainerIndex.Valid())
			{
				const ColorRGBA TextColor = DefaultTextColor.WithMultipliedAlpha(AnimAlpha);
				const ColorRGBA TextOutlineColor = DefaultTextOutlineColor.WithMultipliedAlpha(AnimAlpha);
				TextRender()->RenderTextContainer(Line.m_TextContainerIndex, TextColor, TextOutlineColor, AnimOffsetX, AnimOffsetY);
			}
		}
	}

	if(CopyClickReleased && pClickedLine != nullptr && pClickedLine->m_aText[0] != '\0')
	{
		Input()->SetClipboardText(pClickedLine->m_aText);
	}
	if(ChatLineMenuRequested)
	{
		if(pMenuLine != nullptr && pMenuLine->m_aText[0] != '\0')
			OpenChatLineMenu(*pMenuLine, GetUiMousePos());
		else if(ChatLineMenuOpen)
			CloseChatLineMenu();
	}

	if(ShowChatScrollbar)
	{
		QmDrawChatScrollbar(Ui(), ScrollbarRect, ScrollbarHandleY, ScrollbarHandleH, m_ScrollbarDragging || InsideScrollbar);
		ExtendBounds(ScrollbarRect.x, ScrollbarRect.y, ScrollbarRect.w, ScrollbarRect.h);
	}

	if(HudEditorPreview && !RenderedAnyLines)
	{
		struct SPreviewLine
		{
			const char *m_pPrefix;
			const char *m_pMessage;
			ColorRGBA m_TextColor;
		};

		static const SPreviewLine s_aPreviewLines[] = {
			{"Server", "Welcome to QmClient", ColorRGBA(0.72f, 0.82f, 1.0f, 0.92f)},
			{"Teammate", "Ready?", ColorRGBA(0.72f, 1.0f, 0.72f, 0.92f)},
			{"Friend", "Let's go!", ColorRGBA(1.0f, 0.92f, 0.72f, 0.92f)},
		};

		float PreviewY = 300.0f - (20.0f * FontSize() / 6.0f + (g_Config.m_QmStatusBar ? g_Config.m_QmStatusBarHeight : 0.0f)) - ScaledFontSize;
		PreviewY -= RowHeight * (float)std::size(s_aPreviewLines);

		for(const SPreviewLine &Line : s_aPreviewLines)
		{
			char aPreviewText[256];
			str_format(aPreviewText, sizeof(aPreviewText), "%s: %s", Line.m_pPrefix, Line.m_pMessage);
			const float TextWidth = TextRender()->TextWidth(FontSize(), aPreviewText, -1, -1.0f);
			const float PreviewWidth = minimum(ChatRect.w - x, TextWidth + RealMsgPaddingX * 1.5f + (g_Config.m_ClChatOld ? 0.0f : MessageTeeSize() + 2.0f));

			if(!g_Config.m_ClChatOld)
				Graphics()->DrawRect(x, PreviewY, PreviewWidth, RowHeight, BackgroundBaseColor, IGraphics::CORNER_ALL, MessageRounding());

			TextRender()->TextColor(Line.m_TextColor);
			TextRender()->Text(x + (g_Config.m_ClChatOld ? 0.0f : RealMsgPaddingX), PreviewY + RealMsgPaddingY * 0.5f, FontSize(), aPreviewText, -1.0f);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			ExtendBounds(x, PreviewY, PreviewWidth, RowHeight);
			PreviewY += RowHeight;
		}
	}

	if(VisibleBounds.Valid())
		GameClient()->m_HudEditor.UpdateVisibleRect(EHudEditorElement::Chat, VisibleBounds.Rect());

	GameClient()->m_HudEditor.EndTransform(HudEditorScope);

	// 渲染聊天相关弹窗。
	if(m_Mode != MODE_NONE && (Ui()->IsPopupOpen(&m_LanguagePopupContext) || Ui()->IsPopupOpen(&m_ChatLinePopupContext)))
	{
		Ui()->StartCheck();
		Ui()->Update();
		Ui()->MapScreen();
		Ui()->RenderPopupMenus();
		Ui()->FinishCheck();
		Ui()->ClearHotkeys();
		m_LanguageMenuOpen = Ui()->IsPopupOpen(&m_LanguagePopupContext);
		Graphics()->MapScreen(0.0f, 0.0f, Width, Height);
	}
	else
	{
		m_LanguageMenuOpen = false;
	}

	// 渲染鼠标光标（当聊天框激活时）
	if(m_Mode != MODE_NONE)
	{
		const vec2 UiMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
		const vec2 UiToChatScale(Width / Ui()->Screen()->w, Height / Ui()->Screen()->h);
		RenderTools()->RenderCursor(UiMousePos * UiToChatScale, 12.0f);
	}
}

void CChat::EnsureCoherentFontSize() const
{
	// Adjust font size based on width
	if(g_Config.m_ClChatWidth / (float)g_Config.m_ClChatFontSize >= CHAT_FONTSIZE_WIDTH_RATIO)
		return;

	// We want to keep a ration between font size and font width so that we don't have a weird rendering
	g_Config.m_ClChatFontSize = g_Config.m_ClChatWidth / CHAT_FONTSIZE_WIDTH_RATIO;
}

void CChat::EnsureCoherentWidth() const
{
	// Adjust width based on font size
	if(g_Config.m_ClChatWidth / (float)g_Config.m_ClChatFontSize >= CHAT_FONTSIZE_WIDTH_RATIO)
		return;

	// We want to keep a ration between font size and font width so that we don't have a weird rendering
	g_Config.m_ClChatWidth = CHAT_FONTSIZE_WIDTH_RATIO * g_Config.m_ClChatFontSize;
}

// ----- send functions -----

static bool ShouldSyncDummyCommandToOther(const char *pLine)
{
	return g_Config.m_ClDummyCopyMoves && CChat::ShouldSyncDummyCommand(pLine);
}

void CChat::SendChat(int Team, const char *pLine)
{
	if(pLine && GameClient()->TClientComponent().TryHandleLocalSaveReply(pLine))
		return;

	// don't send empty messages
	if(*str_utf8_skip_whitespaces(pLine) == '\0')
		return;
	if(GameClient()->m_FastPractice.ConsumePracticeChatCommand(Team, pLine))
		return;

	m_LastChatSend = time();

	if(GameClient()->Client()->IsSixup())
	{
		protocol7::CNetMsg_Cl_Say Msg7;
		Msg7.m_Mode = Team == 1 ? protocol7::CHAT_TEAM : protocol7::CHAT_ALL;
		Msg7.m_Target = -1;
		Msg7.m_pMessage = pLine;
		if(Client()->SendPackMsgActive(&Msg7, MSGFLAG_VITAL, true) == 0)
			GameClient()->TClientComponent().TrackLocalSaveLoadCommand(g_Config.m_ClDummy, pLine);

		if(Client()->DummyConnected() && ShouldSyncDummyCommandToOther(pLine))
			SendChatOnConn(!g_Config.m_ClDummy, Team, pLine);

		return;
	}

	// send chat message
	CNetMsg_Cl_Say Msg;
	Msg.m_Team = Team;
	Msg.m_pMessage = pLine;
	if(Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL) == 0)
		GameClient()->TClientComponent().TrackLocalSaveLoadCommand(g_Config.m_ClDummy, pLine);

	if(Client()->DummyConnected() && ShouldSyncDummyCommandToOther(pLine))
		SendChatOnConn(!g_Config.m_ClDummy, Team, pLine);
}

void CChat::SendChatOnConn(int Conn, int Team, const char *pLine, bool AllowWhitespaceOnly, bool HandleLocalSaveForLoadCommand)
{
	if(HandleLocalSaveForLoadCommand && pLine && GameClient()->TClientComponent().TryHandleLocalSaveReply(pLine))
		return;

	if(pLine == nullptr || pLine[0] == '\0')
		return;

	// don't send empty messages
	if(!AllowWhitespaceOnly && *str_utf8_skip_whitespaces(pLine) == '\0')
		return;

	if(Conn != IClient::CONN_DUMMY)
		Conn = IClient::CONN_MAIN;

	m_LastChatSend = time();
	int SendResult;

	if(GameClient()->Client()->IsSixup())
	{
		protocol7::CNetMsg_Cl_Say Msg7;
		Msg7.m_Mode = Team == 1 ? protocol7::CHAT_TEAM : protocol7::CHAT_ALL;
		Msg7.m_Target = -1;
		Msg7.m_pMessage = pLine;
		SendResult = Client()->SendPackMsg(Conn, &Msg7, MSGFLAG_VITAL, true);
	}
	else
	{
		// send chat message
		CNetMsg_Cl_Say Msg;
		Msg.m_Team = Team;
		Msg.m_pMessage = pLine;
		SendResult = Client()->SendPackMsg(Conn, &Msg, MSGFLAG_VITAL);
	}

	if(HandleLocalSaveForLoadCommand && SendResult == 0)
		GameClient()->TClientComponent().TrackLocalSaveLoadCommand(Conn, pLine);
}

CChat::EChatSendResult CChat::SendChatQueued(int Team, const char *pLine, bool AllowOutgoingTranslation)
{
	if(pLine && GameClient()->TClientComponent().TryHandleLocalSaveReply(pLine))
		return EChatSendResult::HANDLED;

	const auto SendSensitive = [&] { SendChat(Team, pLine); };
	const auto SendOrdinary = [&] {
		// 自动出站翻译
		if(AllowOutgoingTranslation && QmChatEmojiShouldTranslate(QmChatEmojiFromText(pLine)) && GameClient()->m_Translate.ShouldAutoTranslateOutgoing(pLine))
		{
			GameClient()->m_Translate.StartAutoOutgoingTranslate(Team, pLine);
			return EChatSendResult::HANDLED;
		}

		const EChatSendResult Result = ReserveChatMessage(m_LastChatSend, time(), time_freq(), m_PendingChatCounter);
		if(Result == EChatSendResult::SENT)
			SendChat(Team, pLine);

		if(Result != EChatSendResult::REJECTED)
		{
			const int Length = str_length(pLine);
			CHistoryEntry *pEntry = m_History.Allocate(sizeof(CHistoryEntry) + Length);
			pEntry->m_Team = Team;
			str_copy(pEntry->m_aText, pLine, Length + 1);
		}
		return Result;
	};
	return DispatchChatMessage(pLine, SendSensitive, SendOrdinary);
}

CChat::EChatSendResult CChat::SendChatQueued(const char *pLine)
{
	return SendChatQueued(m_Mode == MODE_ALL ? 0 : 1, pLine, true);
}

// ===== 翻译按钮相关方法 =====

vec2 CChat::GetChatMousePos() const
{
	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
	const vec2 UiMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
	const vec2 UiToChatScale(Width / Ui()->Screen()->w, Height / Ui()->Screen()->h);
	return UiMousePos * UiToChatScale;
}

vec2 CChat::GetChatLocalMousePos() const
{
	if(m_ChatInputMapRect.w <= 0.0f || m_ChatInputMapRect.h <= 0.0f)
		return GetChatMousePos();
	const SQmChatViewport Viewport{m_ChatInputMapRect, vec2(maximum(1, Graphics()->WindowWidth()), maximum(1, Graphics()->WindowHeight()))};
	return Viewport.ToLocal(Ui()->UpdatedMousePos());
}

vec2 CChat::GetUiMousePos() const
{
	const CUIRect *pScreen = Ui()->Screen();
	const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
	return vec2(pScreen->x, pScreen->y) + Ui()->UpdatedMousePos() * vec2(pScreen->w, pScreen->h) / WindowSize;
}

void CChat::RenderTranslateButton(const CUIRect &ButtonRect)
{
	using namespace FontIcons;
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());

	m_TranslateButton.m_X = ButtonRect.x;
	m_TranslateButton.m_Y = ButtonRect.y;
	m_TranslateButton.m_W = ButtonRect.w;
	m_TranslateButton.m_H = ButtonRect.h;
	m_TranslateButton.m_RectValid = true;

	const vec2 MousePos = GetChatLocalMousePos();
	const bool Hovered = !Ui()->IsPopupOpen() && ButtonRect.Inside(MousePos);

	const bool IsOpen = m_LanguageMenuOpen;
	m_TranslateButton.m_AutoTranslateEnabled = g_Config.m_QmTranslateAutoOutgoing != 0;
	const bool IsEnabled = m_TranslateButton.m_AutoTranslateEnabled;

	ColorRGBA ButtonColor;
	if(IsEnabled)
	{
		ButtonColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTranslateBtnColorEnabled, true));
	}
	else if(IsOpen)
	{
		ButtonColor = ColorRGBA(0.35f, 0.45f, 0.70f, 0.90f);
	}
	else if(Hovered)
	{
		ButtonColor = ColorRGBA(0.28f, 0.28f, 0.28f, 0.90f);
	}
	else
	{
		ButtonColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTranslateBtnColorDisabled, true));
	}
	const float ButtonRounding = maximum(6.0f, ButtonRect.h * 0.28f);

	// 自动翻译已启用时也保留悬浮/按下反馈，不被启用颜色分支遮住。
	const SUiTheme &Theme = Ui()->QmControlTheme();
	const bool Pressed = Hovered && Input()->KeyIsPressed(KEY_MOUSE_1);
	DrawRoundedSurface(Ui(), ButtonRect, ButtonColor, Hovered ? Theme.m_Accent : ColorRGBA(), ButtonRounding, Hovered ? (Pressed ? 1.0f : 0.65f) : 0.0f);

	CUIRect IconRect;
	ButtonRect.Margin(1.0f, &IconRect);
	const float IconSize = IconRect.h * CUi::ms_FontmodHeight;

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH |
				     ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING |
				     ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING |
				     ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT |
				     ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);
	// 图标走图集（失败回退 FONT_ICON_LANGUAGE）；单字形无需 streamed 文本缓存。
	Ui()->DoLabel_QmIcon(&IconRect, EQmIcon::LANGUAGE, FONT_ICON_LANGUAGE, IconSize, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	if(Hovered)
	{
		const char *pTooltip = IsEnabled ? Localize("Right-click to disable auto-translate") : Localize("Right-click to enable auto-translate");
		const SQmChatViewport Viewport{m_ChatInputMapRect, vec2(maximum(1, Graphics()->WindowWidth()), maximum(1, Graphics()->WindowHeight()))};
		const CUIRect TooltipRect = Viewport.ToUi(ButtonRect, *Ui()->Screen());
		GameClient()->m_Tooltips.DoToolTipForRect(&m_TranslateButton, &TooltipRect, pTooltip);
	}
}

bool CChat::TranslateVisibleChatLines()
{
	const SQmFocusModeDecisions Focus = GetQmFocusModeDecisions();
	const bool FocusHideChat = Focus.m_HidePlayerMessages;
	const bool FocusHideSystemInfoMessages = Focus.m_HideSystemInfoMessages;
	const bool FocusHideSystemPromptMessages = Focus.m_HideSystemPromptMessages;
	const bool FocusHideEcho = Focus.m_HideEchoMessages;
	const bool IsScoreBoardOpen = GameClient()->m_Scoreboard.IsActive();
	const bool ShowLargeArea = m_Show || (m_Mode != MODE_NONE && g_Config.m_ClShowChat == 1) || g_Config.m_ClShowChat == 2;
	const int OffsetType = IsScoreBoardOpen ? 1 : 0;

	int aLineIndices[MAX_LINES];
	int NumLineIndices = 0;
	for(int i = m_BacklogCurLine; i < MAX_LINES; i++)
	{
		const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
		CLine &Line = m_aLines[LineIndex];
		if(!Line.m_Initialized)
			break;
		const bool ServerMessageIsBasicInfo = Line.m_ServerMessageClass == QmHudNotifications::EServerMessageClass::BasicInfo;
		if(!ShouldRenderFocusFilteredChatLine(FocusHideChat, FocusHideSystemInfoMessages, FocusHideSystemPromptMessages, FocusHideEcho, Line.m_ClientId, Line.m_ForceVisible, ServerMessageIsBasicInfo))
			continue;
		if(!ShowLargeArea && !Line.m_ForceVisible && Line.m_Presentation.m_State == EPresentationState::COLLAPSED)
			continue;
		if(!ShowLargeArea && Line.m_Presentation.m_LayoutVisibility <= 0.001f && Line.m_Presentation.m_RenderAlpha <= 0.001f)
			continue;
		if(Line.m_aYOffset[OffsetType] < 0.0f)
			continue;
		if(Line.m_CutOffProgress >= 1.0f)
			continue;
		if(!QmChatEmojiShouldTranslate(Line.m_ChatEmoji))
			continue;
		if(!IsManualVisibleTranslateCandidate(Line.m_ClientId, Line.m_aText[0] != '\0', Line.m_pTranslateResponse != nullptr, GameClient()->m_aLocalIds, std::size(GameClient()->m_aLocalIds)))
			continue;

		aLineIndices[NumLineIndices++] = LineIndex;
	}
	for(int i = 0; i < NumLineIndices; i++)
		GameClient()->m_Translate.Translate(m_aLines[aLineIndices[i]]);
	return NumLineIndices > 0;
}

void CChat::ToggleAutoTranslate()
{
	m_TranslateButton.m_AutoTranslateEnabled = !m_TranslateButton.m_AutoTranslateEnabled;
	g_Config.m_QmTranslateAutoOutgoing = m_TranslateButton.m_AutoTranslateEnabled ? 1 : 0;
}

void CChat::OpenLanguageMenu()
{
	if(m_LanguageMenuOpen || Ui()->IsPopupOpen(&m_LanguagePopupContext))
	{
		CloseLanguageMenu();
		return;
	}
	m_LanguageMenuOpen = true;
	const SQmChatViewport Viewport{m_ChatInputMapRect, vec2(maximum(1, Graphics()->WindowWidth()), maximum(1, Graphics()->WindowHeight()))};
	const CUIRect Button = Viewport.ToUi({m_TranslateButton.m_X, m_TranslateButton.m_Y, m_TranslateButton.m_W, m_TranslateButton.m_H}, *Ui()->Screen());
	m_LanguagePopupContext.Open(Ui(), vec2(Button.x + Button.w, Button.y), GameClient());
}

void CChat::CloseLanguageMenu()
{
	if(Ui()->IsPopupOpen(&m_LanguagePopupContext))
		Ui()->ClosePopupMenu(&m_LanguagePopupContext, true);
	m_LanguageMenuOpen = false;
	m_TranslateButton.m_Input.Reset();
}

void CChat::OpenChatLineMenu(const CLine &Line, vec2 UiMousePos)
{
	CloseLanguageMenu();

	m_ChatLinePopupContext.m_pChat = this;
	m_ChatLinePopupContext.m_ClientId = Line.m_ClientId;
	m_ChatLinePopupContext.m_TeamNumber = Line.m_TeamNumber;
	m_ChatLinePopupContext.m_LineIndex = GetLineIndex(&Line);
	str_copy(m_ChatLinePopupContext.m_aText, Line.m_aText);
	m_ChatLinePopupContext.m_PlayerLine = Line.m_vMergedAuthors.size() <= 1 && Line.m_ClientId >= 0 && Line.m_ClientId < MAX_CLIENTS;
	m_ChatLinePopupContext.m_LocalPlayer = m_ChatLinePopupContext.m_PlayerLine && GameClient()->IsLocalClientId(Line.m_ClientId);
	if(m_ChatLinePopupContext.m_PlayerLine)
	{
		str_copy(m_ChatLinePopupContext.m_aPlayerName, GameClient()->m_aClients[Line.m_ClientId].m_aName);
		GameClient()->FormatStreamerName(Line.m_ClientId, m_ChatLinePopupContext.m_aName, sizeof(m_ChatLinePopupContext.m_aName));
	}
	else
	{
		m_ChatLinePopupContext.m_aPlayerName[0] = '\0';
		str_copy(m_ChatLinePopupContext.m_aName, Line.m_aName);
	}

	constexpr float MenuWidth = 188.0f;
	constexpr float MenuHeight = 266.0f;
	Ui()->DoPopupMenu(&m_ChatLinePopupContext, UiMousePos.x, UiMousePos.y, MenuWidth, MenuHeight, &m_ChatLinePopupContext, PopupChatLineMenu);
}

void CChat::CloseChatLineMenu()
{
	if(Ui()->IsPopupOpen(&m_ChatLinePopupContext))
		Ui()->ClosePopupMenu(&m_ChatLinePopupContext, true);
	m_ChatLinePopupContext.m_LineIndex = -1;
}

void CChat::AddTextToBlockWords(const char *pText)
{
	if(AppendBlockWordToList(g_Config.m_QmBlockWordsList, sizeof(g_Config.m_QmBlockWordsList), pText))
		g_Config.m_QmBlockWordsEnabled = 1;
}

void CChat::ReplyToChatLine(const CChatLinePopupContext &Context)
{
	if(!Context.m_PlayerLine || Context.m_aName[0] == '\0')
		return;

	if(Context.m_TeamNumber == TEAM_WHISPER_SEND || Context.m_TeamNumber == TEAM_WHISPER_RECV)
	{
		EnableMode(0);
		char aReply[MAX_LINE_LENGTH];
		BuildWhisperCommand(aReply, sizeof(aReply), Context.m_aName, "");
		m_Input.Set(aReply);
		m_Input.SetCursorOffset(m_Input.GetLength());
		m_Input.SelectNothing();
		return;
	}

	EnableMode(Context.m_TeamNumber == 1 ? 1 : 0);
	char aReply[MAX_LINE_LENGTH];
	str_format(aReply, sizeof(aReply), "%s: ", Context.m_aName);
	m_Input.Set(aReply);
	m_Input.SetCursorOffset(m_Input.GetLength());
	m_Input.SelectNothing();
}

void CChat::RepeatChatLine(const CChatLinePopupContext &Context)
{
	if(Context.m_aText[0] == '\0')
		return;

	if(Context.m_TeamNumber == TEAM_WHISPER_SEND || Context.m_TeamNumber == TEAM_WHISPER_RECV)
	{
		if(!Context.m_PlayerLine || Context.m_aName[0] == '\0')
			return;

		char aLine[MAX_LINE_LENGTH];
		if(BuildWhisperCommand(aLine, sizeof(aLine), Context.m_aName, Context.m_aText))
			SendChat(0, aLine);
		return;
	}

	SendChat(Context.m_TeamNumber == 1 ? 1 : 0, Context.m_aText);
}

void CChat::SpectateChatLine(const CChatLinePopupContext &Context)
{
	if(!Context.m_PlayerLine || Context.m_ClientId < 0 || Context.m_ClientId >= MAX_CLIENTS || Context.m_aPlayerName[0] == '\0')
		return;

	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		GameClient()->m_Spectator.Spectate(Context.m_ClientId);
		return;
	}

	char aCommand[2 * MAX_NAME_LENGTH + 32];
	if(BuildSpectateCommand(aCommand, sizeof(aCommand), Context.m_aPlayerName))
		Console()->ExecuteLine(aCommand);
}

CUi::EPopupMenuFunctionResult CChat::PopupChatLineMenu(void *pContext, CUIRect View, bool Active)
{
	CChatLinePopupContext *pPopupContext = static_cast<CChatLinePopupContext *>(pContext);
	CChat *pChat = pPopupContext->m_pChat;
	CUi *pUi = pChat->Ui();

	View.Margin(5.0f, &View);

	CUIRect Header, Divider;
	View.HSplitTop(40.0f, &Header, &View);
	View.HSplitTop(4.0f, &Divider, &View);

	Header.Draw(ColorRGBA(0.08f, 0.11f, 0.14f, 0.82f), IGraphics::CORNER_ALL, 5.0f);
	Header.Margin(5.0f, &Header);

	CUIRect NameRow, PreviewRow;
	Header.HSplitTop(14.0f, &NameRow, &PreviewRow);
	char aName[96];
	if(pPopupContext->m_aName[0] != '\0')
		str_copy(aName, pPopupContext->m_aName);
	else
		str_copy(aName, Localize("Chat"));

	SLabelProperties LabelProps;
	LabelProps.m_MaxWidth = NameRow.w;
	LabelProps.m_EllipsisAtEnd = true;
	LabelProps.SetColor(ColorRGBA(0.92f, 0.98f, 1.0f, 0.95f));
	pUi->DoLabel(&NameRow, aName, 8.0f, TEXTALIGN_ML, LabelProps);

	LabelProps.m_MaxWidth = PreviewRow.w;
	LabelProps.SetColor(ColorRGBA(0.72f, 0.82f, 0.88f, 0.78f));
	pUi->DoLabel(&PreviewRow, pPopupContext->m_aText, 7.0f, TEXTALIGN_ML, LabelProps);

	Divider.HMargin(1.5f, &Divider);
	Divider.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.13f), IGraphics::CORNER_ALL, 1.0f);
	View.HSplitTop(4.0f, nullptr, &View);

	constexpr float ButtonHeight = 24.0f;
	constexpr float ButtonSpacing = 3.5f;
	constexpr float FontSize = 9.5f;
	constexpr float IconSize = 9.5f;
	constexpr float IconWidth = 21.0f;

	auto DoEntry = [&](CButtonContainer *pButton, EQmIcon Icon, const char *pIcon, const char *pText, bool Enabled, ColorRGBA AccentColor) {
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(pUi);
		CUIRect Button, IconRect, LabelRect;
		View.HSplitTop(ButtonHeight, &Button, &View);
		View.HSplitTop(ButtonSpacing, nullptr, &View);
		const CUIRect ButtonHitRect = Button;

		const bool Hovered = Active && Enabled && pUi->MouseHovered(&Button);
		const ColorRGBA BackgroundColor = Enabled ?
							  (Hovered ? ColorRGBA(0.18f, 0.25f, 0.30f, 0.96f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.07f)) :
							  ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f);
		Button.Draw(BackgroundColor, IGraphics::CORNER_ALL, 5.0f);

		Button.VMargin(6.0f, &Button);
		Button.VSplitLeft(IconWidth, &IconRect, &LabelRect);

		pChat->TextRender()->TextColor(Enabled ? AccentColor : ColorRGBA(0.55f, 0.60f, 0.64f, 0.45f));
		pUi->DoLabel_QmIcon(&IconRect, Icon, pIcon, IconSize, TEXTALIGN_MC);
		pChat->TextRender()->TextColor(Enabled ? ColorRGBA(0.93f, 0.96f, 0.98f, 0.96f) : ColorRGBA(0.62f, 0.67f, 0.70f, 0.45f));
		pUi->DoLabel(&LabelRect, pText, FontSize, TEXTALIGN_ML);
		pChat->TextRender()->TextColor(pChat->TextRender()->DefaultTextColor());

		return Active && Enabled && pUi->DoButtonLogic(pButton, 0, &ButtonHitRect, BUTTONFLAG_LEFT);
	};

	if(DoEntry(&pPopupContext->m_CopyButton, EQmIcon::COPY, FontIcons::FONT_ICON_COPY, Localize("Copy"), pPopupContext->m_aText[0] != '\0', ColorRGBA(0.74f, 0.88f, 1.0f, 1.0f)))
	{
		pChat->Input()->SetClipboardText(pPopupContext->m_aText);
		return CUi::POPUP_CLOSE_CURRENT;
	}
	if(DoEntry(&pPopupContext->m_AddOneButton, EQmIcon::ARROWS_ROTATE, FontIcons::FONT_ICON_ARROWS_ROTATE, Localize("Add one"), pPopupContext->m_aText[0] != '\0', ColorRGBA(0.70f, 0.95f, 0.78f, 1.0f)))
	{
		pChat->RepeatChatLine(*pPopupContext);
		return CUi::POPUP_CLOSE_CURRENT;
	}
	if(DoEntry(&pPopupContext->m_ReplyButton, EQmIcon::COMMENT, FontIcons::FONT_ICON_COMMENT, Localize("Reply"), pPopupContext->m_PlayerLine && pPopupContext->m_aName[0] != '\0', ColorRGBA(0.88f, 0.78f, 1.0f, 1.0f)))
	{
		pChat->ReplyToChatLine(*pPopupContext);
		return CUi::POPUP_CLOSE_CURRENT;
	}
	if(DoEntry(&pPopupContext->m_SpectateButton, EQmIcon::EYE, FontIcons::FONT_ICON_EYE, Localize("Spectate"), pPopupContext->m_PlayerLine && pPopupContext->m_aPlayerName[0] != '\0', ColorRGBA(0.72f, 0.86f, 1.0f, 1.0f)))
	{
		pChat->SpectateChatLine(*pPopupContext);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(2.0f, &Divider, &View);
	Divider.HMargin(0.75f, &Divider);
	Divider.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.09f), IGraphics::CORNER_ALL, 1.0f);
	View.HSplitTop(3.0f, nullptr, &View);

	if(DoEntry(&pPopupContext->m_MutePlayerButton, EQmIcon::BAN, FontIcons::FONT_ICON_BAN, Localize("Mute player"), pPopupContext->m_PlayerLine && !pPopupContext->m_LocalPlayer, ColorRGBA(1.0f, 0.50f, 0.52f, 1.0f)))
	{
		pChat->GameClient()->m_aClients[pPopupContext->m_ClientId].m_ChatIgnore = true;
		return CUi::POPUP_CLOSE_CURRENT;
	}
	if(DoEntry(&pPopupContext->m_AddBlockedWordButton, EQmIcon::COMMENT_SLASH, FontIcons::FONT_ICON_COMMENT_SLASH, Localize("Add to blocked words"), pPopupContext->m_aText[0] != '\0', ColorRGBA(1.0f, 0.67f, 0.45f, 1.0f)))
	{
		pChat->AddTextToBlockWords(pPopupContext->m_aText);
		return CUi::POPUP_CLOSE_CURRENT;
	}
	if(DoEntry(&pPopupContext->m_CopyNameButton, EQmIcon::USER, FontIcons::FONT_ICON_USER, Localize("Copy name"), pPopupContext->m_PlayerLine && pPopupContext->m_aName[0] != '\0', ColorRGBA(0.78f, 0.88f, 0.95f, 1.0f)))
	{
		pChat->Input()->SetClipboardText(pPopupContext->m_aName);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

bool CChat::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(m_Mode == MODE_NONE)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);
	return true;
}
