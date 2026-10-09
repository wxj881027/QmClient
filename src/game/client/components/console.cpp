/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "console.h"

#include <base/lock.h>
#include <base/logger.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/ringbuffer.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/QmUi/QmConsoleUi.h>
#include <game/client/components/qmclient/colored_parts.h>
#include <game/client/components/qmclient/console_syntax.h>
#include <game/client/components/qmclient/console_text.h>
#include <game/client/components/qmclient/qm_chat_export.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

static constexpr float FONT_SIZE = 10.0f;
static constexpr float LINE_SPACING = 1.0f;
static constexpr float LINK_CLICK_DRAG_THRESHOLD = 3.0f;
static constexpr float LINK_UNDERLINE_HEIGHT = 0.12f;
static constexpr float CONSOLE_SCROLLBAR_WIDTH = 18.0f;
static constexpr float CONSOLE_SCROLLBAR_MARGIN = 5.0f;
static constexpr ColorRGBA LINK_TEXT_COLOR = ColorRGBA(0.2f, 0.65f, 1.0f, 1.0f);
static constexpr ColorRGBA LINK_UNDERLINE_COLOR = ColorRGBA(0.2f, 0.65f, 1.0f, 0.9f);

struct SConfigHelpLookup
{
	const char *m_pScriptName;
	const SConfigVariable *m_pVariable = nullptr;
};

static void FindConfigHelpVariable(const SConfigVariable *pVar, void *pUserData)
{
	auto *pLookup = static_cast<SConfigHelpLookup *>(pUserData);
	if(pLookup->m_pVariable == nullptr && str_comp(pVar->m_pScriptName, pLookup->m_pScriptName) == 0)
		pLookup->m_pVariable = pVar;
}

static bool BuildLocalizedConfigHelpText(const SConfigVariable *pVar, char *pBuffer, size_t BufferSize)
{
	if(pVar == nullptr || pVar->m_pHelpLocalizeKey == nullptr)
		return false;

	const char *pHelpText = Localize(pVar->m_pHelpLocalizeKey);
	if(pVar->m_Type == SConfigVariable::VAR_INT)
	{
		const auto *pInt = static_cast<const SIntConfigVariable *>(pVar);
		if(pInt->m_Min == pInt->m_Max)
			str_format(pBuffer, BufferSize, Localize("%s (default: %d)", "Config help"), pHelpText, pInt->m_Default);
		else if(pInt->m_Max == 0)
			str_format(pBuffer, BufferSize, Localize("%s (default: %d, min: %d)", "Config help"), pHelpText, pInt->m_Default, pInt->m_Min);
		else
			str_format(pBuffer, BufferSize, Localize("%s (default: %d, min: %d, max: %d)", "Config help"), pHelpText, pInt->m_Default, pInt->m_Min, pInt->m_Max);
	}
	else if(pVar->m_Type == SConfigVariable::VAR_COLOR)
	{
		const auto *pColor = static_cast<const SColorConfigVariable *>(pVar);
		str_format(pBuffer, BufferSize, Localize("%s (default: $%0*X)", "Config help"), pHelpText, pColor->m_Alpha ? 8 : 6, color_cast<ColorRGBA>(ColorHSLA(pColor->m_Default, pColor->m_Alpha)).Pack(pColor->m_Alpha));
	}
	else if(pVar->m_Type == SConfigVariable::VAR_STRING)
	{
		const auto *pString = static_cast<const SStringConfigVariable *>(pVar);
		str_format(pBuffer, BufferSize, Localize("%s (default: \"%s\", max length: %d)", "Config help"), pHelpText, pString->m_pDefault, (int)pString->m_MaxSize - 1);
	}
	else
	{
		str_copy(pBuffer, pHelpText, BufferSize);
	}
	return true;
}

bool CGameConsole::DoButton(const CUIRect &Rect, const char *pIcon, vec2 MousePosition, bool Released)
{
	const bool PressedInside = Rect.Inside(m_ButtonPressPosition);
	const bool MouseInside = Rect.Inside(MousePosition);
	const bool Active = CurrentConsole()->m_MouseIsPress && PressedInside;

	const float ColorMul = Active ? Ui()->ButtonColorMulActive() : (MouseInside ? Ui()->ButtonColorMulHot() : Ui()->ButtonColorMulDefault());
	Ui()->DrawButton_FontIcon(pIcon, &Rect, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * ColorMul), IGraphics::CORNER_B);
	return m_ConsoleState == CONSOLE_OPEN && Released && PressedInside && MouseInside;
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
static void TrimAsciiSpaces(std::string &Text)
{
	size_t Start = 0;
	while(Start < Text.size() && (Text[Start] == ' ' || Text[Start] == '\t' || Text[Start] == '\r' || Text[Start] == '\n'))
		++Start;
	size_t End = Text.size();
	while(End > Start && (Text[End - 1] == ' ' || Text[End - 1] == '\t' || Text[End - 1] == '\r' || Text[End - 1] == '\n'))
		--End;
	if(Start != 0 || End != Text.size())
		Text = Text.substr(Start, End - Start);
}

static bool TryParseChatExportLine(const char *pText, const char *pLocalName, QmChatExport::SLine &Line)
{
	struct SChatPrefix
	{
		const char *m_pNeedle;
	};
	static const SChatPrefix s_aPrefixes[] = {
		{" chat/all: "},
		{" chat/team: "},
		{" chat/whisper: "},
	};

	const char *pMessage = nullptr;
	for(const auto &Prefix : s_aPrefixes)
	{
		const char *pFound = str_find(pText, Prefix.m_pNeedle);
		if(pFound)
		{
			pMessage = pFound + str_length(Prefix.m_pNeedle);
			break;
		}
	}
	if(!pMessage)
		return false;

	Line.m_Raw = pText;
	Line.m_Time.clear();
	if(str_length(pText) >= 19 && pText[4] == '-' && pText[7] == '-' && pText[10] == ' ' && pText[13] == ':' && pText[16] == ':')
		Line.m_Time.assign(pText, 19);

	// 悄悄话日志形如 "→ 对方: 内容"（发出）或 "← 对方: 内容"（收到）。
	// 箭头方向就是气泡方向：不识别它会把自己发出的悄悄话渲染成对方发来的，
	// 且箭头会被当成玩家名的一部分参与本地作者比较。
	bool WhisperOutgoing = false;
	if(pMessage[0] == '\xe2' && pMessage[1] == '\x86' && (pMessage[2] == '\x92' || pMessage[2] == '\x90'))
		WhisperOutgoing = pMessage[2] == '\x92'; // U+2192 发出，U+2190 收到

	const char *pNameEnd = str_find(pMessage, ": ");
	if(pNameEnd && pNameEnd > pMessage)
	{
		Line.m_Sender.assign(pMessage, pNameEnd - pMessage);
		Line.m_Message = pNameEnd + 2;
	}
	else
	{
		Line.m_Sender.clear();
		Line.m_Message = pMessage;
	}
	// 发出的悄悄话里，名字位置是收件人；本地作者判定只能依据箭头方向。
	Line.m_Local = WhisperOutgoing || (pLocalName && pLocalName[0] != '\0' && !Line.m_Sender.empty() && str_comp(Line.m_Sender.c_str(), pLocalName) == 0);
	return true;
}

// 导出任务：字形由主线程分帧光栅化，条目排版、头像合成与 PNG 编码都在后台线程做。
class CQmChatExportJob : public IJob
{
	IStorage *m_pStorage;
	std::string m_BaseFilename;
	QmChatExport::SLabels m_Labels;
	std::vector<std::pair<int, int>> m_vGlyphKeys;
	QmChatExport::TGlyphs m_Glyphs;
	size_t m_NextGlyph = 0;

	void Run() override
	{
		if(m_Cancelled.load())
			return;
		for(const char *pDirectory : {"qmclient", "qmclient/chat_log"})
		{
			if(!m_pStorage->CreateFolder(pDirectory, IStorage::TYPE_SAVE) && !m_pStorage->FolderExists(pDirectory, IStorage::TYPE_SAVE))
				return;
		}
		m_Success = QmChatExport::Export(m_pStorage, m_BaseFilename, m_vLines, m_Glyphs, m_Labels, m_Cancelled, m_CompletedPages);
	}

public:
	const std::vector<QmChatExport::SLine> m_vLines;
	std::atomic<bool> m_Cancelled{false};
	std::atomic<int> m_CompletedPages{0};
	bool m_Queued = false;
	bool m_Success = false;

	CQmChatExportJob(IStorage *pStorage, std::string BaseFilename, std::vector<QmChatExport::SLine> vLines, QmChatExport::SLabels Labels) :
		m_pStorage(pStorage), m_BaseFilename(std::move(BaseFilename)), m_Labels(std::move(Labels)), m_vLines(std::move(vLines))
	{
		std::set<std::pair<int, int>> Keys;
		auto AddText = [&Keys](int FontSize, const std::string &Text) {
			Keys.emplace(FontSize, '?');
			Keys.emplace(FontSize, ' ');
			const char *pText = Text.c_str();
			while(*pText)
			{
				const int Codepoint = str_utf8_decode(&pText);
				if(Codepoint >= 32)
					Keys.emplace(FontSize, Codepoint);
			}
		};
		for(const auto &Line : m_vLines)
		{
			AddText(QmChatExport::FONT_MESSAGE, Line.m_Message);
			AddText(QmChatExport::FONT_NAME, Line.m_Sender);
			AddText(QmChatExport::FONT_TIME, Line.m_Time);
		}
		m_vGlyphKeys.assign(Keys.begin(), Keys.end());
	}

	int PreparationPercent() const
	{
		return m_vGlyphKeys.empty() ? 100 : (int)(100 * m_NextGlyph / m_vGlyphKeys.size());
	}

	// 每帧只做一小段：避免导出准备卡住主线程，同时不让后台任务碰到渲染资源。
	bool PrepareGlyphs(ITextRender *pTextRender, IGraphics *pGraphics)
	{
		const unsigned PreviousFlags = pTextRender->GetRenderFlags();
		const EFontPreset PreviousFont = pTextRender->GetFontPreset();
		float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
		pGraphics->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
		// 像素映射使导出尺寸不受控制台开关、UI 缩放和窗口大小影响。
		pGraphics->MapScreen(0, 0, pGraphics->ScreenWidth(), pGraphics->ScreenHeight());
		pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
		pTextRender->SetRenderFlags(TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
		const auto Deadline = time_get_nanoseconds() + std::chrono::milliseconds(2);
		int Prepared = 0;
		while(m_NextGlyph < m_vGlyphKeys.size() && Prepared < 32)
		{
			const auto Key = m_vGlyphKeys[m_NextGlyph++];
			const int FontSize = Key.first;
			char aCharacter[5] = {};
			const int Length = str_utf8_encode(aCharacter, Key.second);
			float VisualTop = 0.0f;
			STextSizeProperties Props;
			Props.m_pVisualTop = &VisualTop;
			QmChatExport::SGlyph Glyph;
			Glyph.m_Advance = maximum(1, round_to_int(pTextRender->TextWidth(FontSize, aCharacter, Length, -1.0f, 0, Props)));
			Glyph.m_OffsetY = maximum(0, round_to_int(VisualTop));
			const int MaskWidth = FontSize * 3;
			const int MaskHeight = FontSize * 2;
			std::vector<uint8_t> vMask((size_t)MaskWidth * MaskHeight * 4, 0);
			CImageInfo Mask;
			Mask.m_Width = MaskWidth;
			Mask.m_Height = MaskHeight;
			Mask.m_Format = CImageInfo::FORMAT_RGBA;
			Mask.m_pData = vMask.data();
			if(Key.second != ' ')
				pTextRender->UploadEntityLayerText(Mask, MaskWidth, MaskHeight, aCharacter, Length, 0, 0, FontSize);
			else
				Glyph.m_Advance = maximum(Glyph.m_Advance, FontSize / 3);
			for(int Y = 0; Y < MaskHeight; ++Y)
				for(int X = 0; X < MaskWidth; ++X)
					if(vMask[((size_t)Y * MaskWidth + X) * 4 + 3] != 0)
					{
						Glyph.m_Width = maximum(Glyph.m_Width, X + 1);
						Glyph.m_Height = maximum(Glyph.m_Height, Y + 1);
					}
			Glyph.m_Advance = maximum(Glyph.m_Advance, Glyph.m_Width + 1);
			Glyph.m_vAlpha.resize((size_t)Glyph.m_Width * Glyph.m_Height);
			for(int Y = 0; Y < Glyph.m_Height; ++Y)
				for(int X = 0; X < Glyph.m_Width; ++X)
					Glyph.m_vAlpha[(size_t)Y * Glyph.m_Width + X] = vMask[((size_t)Y * MaskWidth + X) * 4 + 3];
			m_Glyphs.emplace(Key, std::move(Glyph));
			++Prepared;
			if(time_get_nanoseconds() >= Deadline)
				break;
		}
		pTextRender->SetRenderFlags(PreviousFlags);
		pTextRender->SetFontPreset(PreviousFont);
		pGraphics->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
		return m_NextGlyph == m_vGlyphKeys.size();
	}
};

class CConsoleLogger : public ILogger
{
	CGameConsole *m_pConsole;
	CLock m_ConsoleMutex;
	CLock m_PendingColorSpansLock;
	std::string m_PendingColorSpansSystem GUARDED_BY(m_PendingColorSpansLock);
	std::string m_PendingColorSpansMessage GUARDED_BY(m_PendingColorSpansLock);
	std::vector<CGameConsole::SColorSpan> m_vPendingColorSpans GUARDED_BY(m_PendingColorSpansLock);
	std::shared_ptr<const QmChatExport::SMetadata> m_pPendingChatMetadata GUARDED_BY(m_PendingColorSpansLock);

public:
	CConsoleLogger(CGameConsole *pConsole) :
		m_pConsole(pConsole)
	{
		dbg_assert(pConsole != nullptr, "console pointer must not be null");
	}

	void Log(const CLogMessage *pMessage) override REQUIRES(!m_ConsoleMutex);
	void OnConsoleDeletion() REQUIRES(!m_ConsoleMutex);
	void SetPendingColorSpans(const char *pSystem, const char *pMessage, const CGameConsole::SColorSpan *pColorSpans, size_t NumColorSpans, std::shared_ptr<const QmChatExport::SMetadata> pChatMetadata) REQUIRES(!m_PendingColorSpansLock);
	void ClearPendingColorSpans() REQUIRES(!m_PendingColorSpansLock);
};

void CConsoleLogger::Log(const CLogMessage *pMessage)
{
	if(m_Filter.Filters(pMessage))
	{
		return;
	}
	ColorRGBA Color = gs_ConsoleDefaultColor;
	if(pMessage->m_HaveColor)
	{
		Color.r = pMessage->m_Color.r / 255.0;
		Color.g = pMessage->m_Color.g / 255.0;
		Color.b = pMessage->m_Color.b / 255.0;
	}
	const CLockScope LockScope(m_ConsoleMutex);
	if(m_pConsole)
	{
		std::vector<CGameConsole::SColorSpan> vColorSpans;
		std::shared_ptr<const QmChatExport::SMetadata> pChatMetadata;
		{
			const CLockScope ColorSpansLockScope(m_PendingColorSpansLock);
			if(m_PendingColorSpansSystem == pMessage->m_aSystem && m_PendingColorSpansMessage == pMessage->Message())
			{
				vColorSpans = std::move(m_vPendingColorSpans);
				pChatMetadata = std::move(m_pPendingChatMetadata);
				m_PendingColorSpansSystem.clear();
				m_PendingColorSpansMessage.clear();
				m_vPendingColorSpans.clear();
			}
		}
		for(CGameConsole::SColorSpan &Span : vColorSpans)
			Span.m_CharIndex += (int)str_utf8_offset_bytes_to_chars(pMessage->m_aLine, pMessage->m_LineMessageOffset);
		m_pConsole->m_LocalConsole.PrintLine(pMessage->m_aLine, pMessage->m_LineLength, Color, vColorSpans.data(), vColorSpans.size(), std::move(pChatMetadata));
	}
}

void CConsoleLogger::SetPendingColorSpans(const char *pSystem, const char *pMessage, const CGameConsole::SColorSpan *pColorSpans, size_t NumColorSpans, std::shared_ptr<const QmChatExport::SMetadata> pChatMetadata)
{
	const CLockScope LockScope(m_PendingColorSpansLock);
	m_PendingColorSpansSystem = pSystem;
	m_PendingColorSpansMessage = pMessage;
	m_pPendingChatMetadata = std::move(pChatMetadata);
	if(NumColorSpans == 0)
		m_vPendingColorSpans.clear();
	else
		m_vPendingColorSpans.assign(pColorSpans, pColorSpans + NumColorSpans);
}

void CConsoleLogger::ClearPendingColorSpans()
{
	const CLockScope LockScope(m_PendingColorSpansLock);
	m_PendingColorSpansSystem.clear();
	m_PendingColorSpansMessage.clear();
	m_vPendingColorSpans.clear();
	m_pPendingChatMetadata.reset();
}

void CConsoleLogger::OnConsoleDeletion()
{
	const CLockScope LockScope(m_ConsoleMutex);
	m_pConsole = nullptr;
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
enum class EArgumentCompletionType
{
	NONE,
	MAP,
	TUNE,
	SETTING,
	KEY,
};

// NOLINTNEXTLINE(misc-use-internal-linkage)
class CArgumentCompletionEntry
{
public:
	EArgumentCompletionType m_Type;
	const char *m_pCommandName;
	int m_ArgumentIndex;
};

static const CArgumentCompletionEntry gs_aArgumentCompletionEntries[] = {
	{EArgumentCompletionType::MAP, "sv_map", 0},
	{EArgumentCompletionType::MAP, "change_map", 0},
	{EArgumentCompletionType::TUNE, "tune", 0},
	{EArgumentCompletionType::TUNE, "tune_reset", 0},
	{EArgumentCompletionType::TUNE, "toggle_tune", 0},
	{EArgumentCompletionType::TUNE, "tune_zone", 1},
	{EArgumentCompletionType::SETTING, "reset", 0},
	{EArgumentCompletionType::SETTING, "toggle", 0},
	{EArgumentCompletionType::SETTING, "access_level", 0},
	{EArgumentCompletionType::SETTING, "+toggle", 0},
	{EArgumentCompletionType::KEY, "bind", 0},
	{EArgumentCompletionType::KEY, "binds", 0},
	{EArgumentCompletionType::KEY, "unbind", 0},
};

static std::pair<EArgumentCompletionType, int> ArgumentCompletion(const char *pStr)
{
	const char *pCommandStart = pStr;
	const char *pIt = pStr;
	pIt = str_skip_to_whitespace_const(pIt);
	int CommandLength = pIt - pCommandStart;
	const char *pCommandEnd = pIt;

	if(!CommandLength)
		return {EArgumentCompletionType::NONE, -1};

	pIt = str_skip_whitespaces_const(pIt);
	if(pIt == pCommandEnd)
		return {EArgumentCompletionType::NONE, -1};

	for(const auto &Entry : gs_aArgumentCompletionEntries)
	{
		int Length = maximum(str_length(Entry.m_pCommandName), CommandLength);
		if(str_comp_nocase_num(Entry.m_pCommandName, pCommandStart, Length) == 0)
		{
			int CurrentArg = 0;
			const char *pArgStart = nullptr, *pArgEnd = nullptr;
			while(CurrentArg < Entry.m_ArgumentIndex)
			{
				pArgStart = pIt;
				pIt = str_skip_to_whitespace_const(pIt); // Skip argument value
				pArgEnd = pIt;

				if(!pIt[0] || pArgStart == pIt) // Check that argument is not empty
					return {EArgumentCompletionType::NONE, -1};

				pIt = str_skip_whitespaces_const(pIt); // Go to next argument position
				CurrentArg++;
			}
			if(pIt == pArgEnd)
				return {EArgumentCompletionType::NONE, -1}; // Check that there is at least one space after
			return {Entry.m_Type, pIt - pStr};
		}
	}
	return {EArgumentCompletionType::NONE, -1};
}

static int PossibleTunings(const char *pStr, IConsole::FPossibleCallback pfnCallback = IConsole::EmptyPossibleCommandCallback, void *pUser = nullptr)
{
	int Index = 0;
	for(int i = 0; i < CTuningParams::Num(); i++)
	{
		if(str_find_nocase(CTuningParams::Name(i), pStr))
		{
			pfnCallback(Index, CTuningParams::Name(i), pUser);
			Index++;
		}
	}
	return Index;
}

static int PossibleKeys(const char *pStr, IInput *pInput, IConsole::FPossibleCallback pfnCallback = IConsole::EmptyPossibleCommandCallback, void *pUser = nullptr)
{
	int Index = 0;
	for(int Key = KEY_A; Key < KEY_JOY_AXIS_11_RIGHT; Key++)
	{
		if(Key == KEY_ESCAPE)
		{
			// Binding to Escape key is not supported
			continue;
		}
		// Ignore unnamed keys starting with '&'
		const char *pKeyName = pInput->KeyName(Key);
		if(pKeyName[0] != '&' && str_find_nocase(pKeyName, pStr))
		{
			pfnCallback(Index, pKeyName, pUser);
			Index++;
		}
	}
	return Index;
}

static void CollectPossibleCommandsCallback(int Index, const char *pStr, void *pUser)
{
	((std::vector<const char *> *)pUser)->push_back(pStr);
}

static void SortCompletions(std::vector<const char *> &vCompletions, const char *pSearch)
{
	if(pSearch[0] == '\0')
		return;

	std::sort(vCompletions.begin(), vCompletions.end(), [pSearch](const char *pA, const char *pB) {
		const char *pMatchA = str_find_nocase(pA, pSearch);
		const char *pMatchB = str_find_nocase(pB, pSearch);
		int MatchPosA = pMatchA ? (pMatchA - pA) : -1;
		int MatchPosB = pMatchB ? (pMatchB - pB) : -1;

		if(MatchPosA != MatchPosB)
			return MatchPosA < MatchPosB;

		int LenA = str_length(pA);
		int LenB = str_length(pB);
		if(LenA != LenB)
			return LenA < LenB;

		return str_comp_nocase(pA, pB) < 0;
	});
}

CGameConsole::CInstance::CInstance(int Type)
{
	m_pHistoryEntry = nullptr;

	m_Type = Type;

	if(Type == CGameConsole::CONSOLETYPE_LOCAL)
	{
		m_pName = "local_console";
		m_CompletionFlagmask = CFGFLAG_CLIENT;
	}
	else
	{
		m_pName = "remote_console";
		m_CompletionFlagmask = CFGFLAG_SERVER;
	}

	m_aCompletionBuffer[0] = 0;
	m_CompletionChosen = -1;
	m_aCompletionBufferArgument[0] = 0;
	m_CompletionChosenArgument = -1;
	m_CompletionArgumentPosition = 0;
	m_CompletionDirty = true;
	m_QueueResetAnimation = false;
	Reset();

	m_aUser[0] = '\0';
	m_UserGot = false;
	m_UsernameReq = false;

	m_IsCommand = false;

	m_Backlog.SetPopCallback([this](CBacklogEntry *pEntry) {
		m_Selection.OnEntryRemoved(pEntry->m_ExportId);
		m_ColorSpansByExportId.erase(pEntry->m_ExportId);
		m_ChatMetadataByExportId.erase(pEntry->m_ExportId);
		if(pEntry->m_LineCount != -1 && MatchesLogFilter(pEntry))
		{
			m_NewLineCounter -= pEntry->m_LineCount;
			InvalidateTotalBacklogLines();
			for(auto &SearchMatch : m_vSearchMatches)
			{
				SearchMatch.m_StartLine += pEntry->m_LineCount;
				SearchMatch.m_EndLine += pEntry->m_LineCount;
				SearchMatch.m_EntryLine += pEntry->m_LineCount;
			}
		}
		if(pEntry->m_ExportId == m_ChatExportAnchorId)
			m_ChatExportAnchorId = -1;
	});

	m_BacklogPending.SetPopCallback([this](CBacklogEntry *pEntry) REQUIRES(m_BacklogPendingLock) {
		m_PendingColorSpansByExportId.erase(pEntry->m_ExportId);
		m_PendingChatMetadataByExportId.erase(pEntry->m_ExportId);
	});

	m_Input.SetClipboardLineCallback([this](const char *pStr) { ExecuteLine(pStr); });

	m_CurrentMatchIndex = -1;
	m_aCurrentSearchString[0] = '\0';
}

void CGameConsole::CInstance::Init(CGameConsole *pGameConsole)
{
	m_pGameConsole = pGameConsole;
}

void CGameConsole::CInstance::ClearBacklog()
{
	{
		// We must ensure that no log messages are printed while owning
		// m_BacklogPendingLock or this will result in a dead lock.
		const CLockScope LockScope(m_BacklogPendingLock);
		m_BacklogPending.Init();
		m_PendingColorSpansByExportId.clear();
		m_PendingChatMetadataByExportId.clear();
		m_NextExportId = 1;
	}

	m_Selection.Clear();
	m_Backlog.Init();
	m_ColorSpansByExportId.clear();
	m_ChatMetadataByExportId.clear();
	m_BacklogCurLine = 0;
	m_BacklogLastActiveLine = -1;
	m_ScrollbarDragging = false;
	m_ScrollbarDragOffset = 0.0f;
	InvalidateTotalBacklogLines();
	m_ChatExportAnchorId = -1;
	m_ChatExportMode = false;
	ClearSearch();
}

void CGameConsole::CInstance::UpdateBacklogTextAttributes()
{
	// Pending backlog entries are not handled because they don't have text attributes yet.
	for(CBacklogEntry *pEntry = m_Backlog.First(); pEntry; pEntry = m_Backlog.Next(pEntry))
	{
		UpdateEntryTextAttributes(pEntry);
	}
	InvalidateTotalBacklogLines();
}

void CGameConsole::CInstance::PumpBacklogPending()
{
	{
		// We must ensure that no log messages are printed while owning
		// m_BacklogPendingLock or this will result in a dead lock.
		const CLockScope LockScopePending(m_BacklogPendingLock);
		for(CBacklogEntry *pPendingEntry = m_BacklogPending.First(); pPendingEntry; pPendingEntry = m_BacklogPending.Next(pPendingEntry))
		{
			const size_t EntrySize = sizeof(CBacklogEntry) + pPendingEntry->m_Length;
			CBacklogEntry *pEntry = m_Backlog.Allocate(EntrySize);
			mem_copy(pEntry, pPendingEntry, EntrySize);
			const auto MetadataIt = m_PendingChatMetadataByExportId.find(pPendingEntry->m_ExportId);
			if(MetadataIt != m_PendingChatMetadataByExportId.end())
				m_ChatMetadataByExportId[pEntry->m_ExportId] = std::move(MetadataIt->second);
			const auto ColorSpansIt = m_PendingColorSpansByExportId.find(pPendingEntry->m_ExportId);
			if(ColorSpansIt != m_PendingColorSpansByExportId.end())
				m_ColorSpansByExportId[pEntry->m_ExportId] = std::move(ColorSpansIt->second);
		}

		m_BacklogPending.Init();
		m_PendingColorSpansByExportId.clear();
		m_PendingChatMetadataByExportId.clear();
	}

	// Update text attributes and count number of added lines
	m_pGameConsole->Ui()->MapScreen();
	for(CBacklogEntry *pEntry = m_Backlog.First(); pEntry; pEntry = m_Backlog.Next(pEntry))
	{
		if(pEntry->m_LineCount == -1)
		{
			UpdateEntryTextAttributes(pEntry);
			if(MatchesLogFilter(pEntry))
			{
				m_NewLineCounter += pEntry->m_LineCount;
				InvalidateTotalBacklogLines();
			}
		}
	}
}

void CGameConsole::CInstance::ClearHistory()
{
	m_History.Init();
	m_pHistoryEntry = nullptr;
}

void CGameConsole::CInstance::Reset()
{
	m_CompletionRenderOffset = 0.0f;
	m_CompletionRenderOffsetChange = 0.0f;
	m_pCommandName = "";
	m_pCommandHelp = "";
	m_pCommandParams = "";
	m_CompletionArgumentPosition = 0;
	m_CompletionDirty = true;
}

void CGameConsole::ForceUpdateRemoteCompletionSuggestions()
{
	m_RemoteConsole.m_CompletionDirty = true;
	m_RemoteConsole.UpdateCompletionSuggestions();
}

void CGameConsole::CInstance::UpdateCompletionSuggestions()
{
	if(!m_CompletionDirty)
		return;

	// Store old selection
	char aOldCommand[IConsole::CMDLINE_LENGTH];
	aOldCommand[0] = '\0';
	if(m_CompletionChosen != -1 && (size_t)m_CompletionChosen < m_vpCommandSuggestions.size())
		str_copy(aOldCommand, m_vpCommandSuggestions[m_CompletionChosen], sizeof(aOldCommand));

	char aOldArgument[IConsole::CMDLINE_LENGTH];
	aOldArgument[0] = '\0';
	if(m_CompletionChosenArgument != -1 && (size_t)m_CompletionChosenArgument < m_vpArgumentSuggestions.size())
		str_copy(aOldArgument, m_vpArgumentSuggestions[m_CompletionChosenArgument], sizeof(aOldArgument));

	m_vpCommandSuggestions.clear();
	m_vpArgumentSuggestions.clear();

	// Command completion
	char aSearch[IConsole::CMDLINE_LENGTH];
	GetCommand(m_aCompletionBuffer, aSearch);
	const bool RemoteConsoleCompletion = m_Type == CGameConsole::CONSOLETYPE_REMOTE && m_pGameConsole->Client()->RconAuthed();
	const bool UseTempCommands = RemoteConsoleCompletion && m_pGameConsole->Client()->UseTempRconCommands();
	m_pGameConsole->m_pConsole->PossibleCommands(aSearch, m_CompletionFlagmask, UseTempCommands, CollectPossibleCommandsCallback, &m_vpCommandSuggestions);
	SortCompletions(m_vpCommandSuggestions, aSearch);

	// Argument completion
	const auto [CompletionType, CompletionPos] = ArgumentCompletion(GetString());
	if(CompletionType != EArgumentCompletionType::NONE)
	{
		if(CompletionType == EArgumentCompletionType::MAP)
			m_pGameConsole->PossibleMaps(m_aCompletionBufferArgument, CollectPossibleCommandsCallback, &m_vpArgumentSuggestions);
		else if(CompletionType == EArgumentCompletionType::TUNE)
			PossibleTunings(m_aCompletionBufferArgument, CollectPossibleCommandsCallback, &m_vpArgumentSuggestions);
		else if(CompletionType == EArgumentCompletionType::SETTING)
			m_pGameConsole->m_pConsole->PossibleCommands(m_aCompletionBufferArgument, m_CompletionFlagmask, UseTempCommands, CollectPossibleCommandsCallback, &m_vpArgumentSuggestions);
		else if(CompletionType == EArgumentCompletionType::KEY)
			PossibleKeys(m_aCompletionBufferArgument, m_pGameConsole->Input(), CollectPossibleCommandsCallback, &m_vpArgumentSuggestions);
		SortCompletions(m_vpArgumentSuggestions, m_aCompletionBufferArgument);
	}

	// Restore old selection if it changed
	if(m_CompletionChosen != -1 && (size_t)m_CompletionChosen < m_vpCommandSuggestions.size() &&
		aOldCommand[0] != '\0' && str_comp(m_vpCommandSuggestions[m_CompletionChosen], aOldCommand) != 0)
	{
		for(size_t SuggestedId = 0; SuggestedId < m_vpCommandSuggestions.size(); SuggestedId++)
		{
			if(str_comp(m_vpCommandSuggestions[SuggestedId], aOldCommand) == 0)
			{
				m_CompletionChosen = SuggestedId;
				m_QueueResetAnimation = true;
				break;
			}
		}
	}
	if(m_CompletionChosenArgument != -1 && (size_t)m_CompletionChosenArgument < m_vpArgumentSuggestions.size() &&
		aOldArgument[0] != '\0' && str_comp(m_vpArgumentSuggestions[m_CompletionChosenArgument], aOldArgument) != 0)
	{
		for(size_t SuggestedId = 0; SuggestedId < m_vpArgumentSuggestions.size(); SuggestedId++)
		{
			if(str_comp(m_vpArgumentSuggestions[SuggestedId], aOldArgument) == 0)
			{
				m_CompletionChosenArgument = SuggestedId;
				m_QueueResetAnimation = true;
				break;
			}
		}
	}

	m_CompletionDirty = false;
}

void CGameConsole::CInstance::ExecuteLine(const char *pLine)
{
	if(m_Type == CONSOLETYPE_LOCAL || m_pGameConsole->Client()->RconAuthed())
	{
		const char *pPrevEntry = m_History.Last();
		if(pPrevEntry == nullptr || str_comp(pPrevEntry, pLine) != 0)
		{
			const size_t Size = str_length(pLine) + 1;
			char *pEntry = m_History.Allocate(Size);
			str_copy(pEntry, pLine, Size);
		}
		// print out the user's commands before they get run
		char aBuf[IConsole::CMDLINE_LENGTH + 3];
		str_format(aBuf, sizeof(aBuf), "> %s", pLine);
		if(m_Type == CONSOLETYPE_LOCAL)
			PrintLine(aBuf, str_length(aBuf), gs_ConsoleDefaultColor, nullptr, 0, nullptr, true);
		else
			m_pGameConsole->PrintLine(m_Type, aBuf);
	}

	if(m_Type == CGameConsole::CONSOLETYPE_LOCAL)
	{
		m_pGameConsole->m_pConsole->ExecuteLine(pLine, IConsole::CLIENT_ID_UNSPECIFIED);
	}
	else
	{
		if(m_pGameConsole->Client()->RconAuthed())
		{
			m_pGameConsole->Client()->Rcon(pLine);
		}
		else
		{
			if(!m_UserGot && m_UsernameReq)
			{
				m_UserGot = true;
				str_copy(m_aUser, pLine);
			}
			else
			{
				m_pGameConsole->Client()->RconAuth(m_aUser, pLine, g_Config.m_ClDummy);
				m_UserGot = false;
			}
		}
	}
}

void CGameConsole::CInstance::GetCommand(const char *pInput, char (&aCmd)[IConsole::CMDLINE_LENGTH])
{
	char aInput[IConsole::CMDLINE_LENGTH];
	str_copy(aInput, pInput);
	m_CompletionCommandStart = 0;
	m_CompletionCommandEnd = 0;

	char aaSeparators[][2] = {";", "\""};
	for(auto *pSeparator : aaSeparators)
	{
		int Start, End;
		str_delimiters_around_offset(aInput + m_CompletionCommandStart, pSeparator, m_Input.GetCursorOffset() - m_CompletionCommandStart, &Start, &End);
		m_CompletionCommandStart += Start;
		m_CompletionCommandEnd = m_CompletionCommandStart + (End - Start);
		aInput[m_CompletionCommandEnd] = '\0';
	}
	m_CompletionCommandStart = str_skip_whitespaces_const(aInput + m_CompletionCommandStart) - aInput;

	str_copy(aCmd, aInput + m_CompletionCommandStart, sizeof(aCmd));
}

static void StrCopyUntilSpace(char *pDest, size_t DestSize, const char *pSrc)
{
	const char *pSpace = str_find(pSrc, " ");
	str_copy(pDest, pSrc, minimum<size_t>(pSpace ? pSpace - pSrc + 1 : 1, DestSize));
}

bool CGameConsole::CInstance::OnInput(const IInput::CEvent &Event)
{
	bool Handled = false;

	// Don't allow input while the console is opening/closing
	if(m_pGameConsole->m_ConsoleState == CONSOLE_OPENING || m_pGameConsole->m_ConsoleState == CONSOLE_CLOSING)
		return Handled;

	auto &&SelectNextSearchMatch = [&](int Direction) {
		if(!m_vSearchMatches.empty())
		{
			m_CurrentMatchIndex += Direction;
			if(m_CurrentMatchIndex >= (int)m_vSearchMatches.size())
				m_CurrentMatchIndex = 0;
			if(m_CurrentMatchIndex < 0)
				m_CurrentMatchIndex = (int)m_vSearchMatches.size() - 1;
			m_Selection.Clear();
			// Also scroll to the correct line
			ScrollToCenter(m_vSearchMatches[m_CurrentMatchIndex].m_StartLine, m_vSearchMatches[m_CurrentMatchIndex].m_EndLine);
		}
	};

	if(Event.m_Flags & IInput::FLAG_PRESS)
	{
		if(m_Type == CONSOLETYPE_LOCAL && !m_ChatExportMode && m_pGameConsole->Input()->AltIsPressed() &&
			Event.m_Key >= KEY_1 && Event.m_Key <= KEY_5 && !m_pGameConsole->Input()->HasComposition())
		{
			SetLogFilterMask(QmToggleConsoleLogFilterCategory(m_LogFilterMask, LogFilterCategoryForButton(Event.m_Key - KEY_1)));
			Handled = true;
		}
		else if(Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER)
		{
			if(m_pGameConsole->GameClient()->Input()->HasComposition())
			{
				return true;
			}

			if(!m_SearchInput.IsSearching())
			{
				if(!m_Input.IsEmpty() || (m_UsernameReq && !m_pGameConsole->Client()->RconAuthed() && !m_UserGot))
				{
					ExecuteLine(m_Input.GetString());
					m_Input.Clear();
					m_pHistoryEntry = nullptr;
				}
			}
			else
			{
				SelectNextSearchMatch(m_pGameConsole->GameClient()->Input()->ShiftIsPressed() ? -1 : 1);
			}

			Handled = true;
		}
		else if(Event.m_Key == KEY_UP)
		{
			if(m_SearchInput.IsSearching())
			{
				SelectNextSearchMatch(-1);
			}
			else if(m_Type == CONSOLETYPE_LOCAL || m_pGameConsole->Client()->RconAuthed())
			{
				if(m_pHistoryEntry)
				{
					char *pTest = m_History.Prev(m_pHistoryEntry);

					if(pTest)
						m_pHistoryEntry = pTest;
				}
				else
				{
					m_pHistoryEntry = m_History.Last();
				}

				if(m_pHistoryEntry)
					m_Input.Set(m_pHistoryEntry);
			}
			Handled = true;
		}
		else if(Event.m_Key == KEY_DOWN)
		{
			if(m_SearchInput.IsSearching())
			{
				SelectNextSearchMatch(1);
			}
			else if(m_Type == CONSOLETYPE_LOCAL || m_pGameConsole->Client()->RconAuthed())
			{
				if(m_pHistoryEntry)
					m_pHistoryEntry = m_History.Next(m_pHistoryEntry);

				if(m_pHistoryEntry)
					m_Input.Set(m_pHistoryEntry);
				else
					m_Input.Clear();
			}
			Handled = true;
		}
		else if(Event.m_Key == KEY_TAB)
		{
			const int Direction = m_pGameConsole->GameClient()->Input()->ShiftIsPressed() ? -1 : 1;

			if(!m_SearchInput.IsSearching())
			{
				UpdateCompletionSuggestions();

				// Command completion
				int CompletionEnumerationCount = m_vpCommandSuggestions.size();

				if(m_Type == CGameConsole::CONSOLETYPE_LOCAL || m_pGameConsole->Client()->RconAuthed())
				{
					if(CompletionEnumerationCount)
					{
						if(m_CompletionChosen == -1 && Direction < 0)
							m_CompletionChosen = 0;
						m_CompletionChosen = (m_CompletionChosen + Direction + CompletionEnumerationCount) % CompletionEnumerationCount;
						m_CompletionArgumentPosition = 0;

						char aBefore[IConsole::CMDLINE_LENGTH];
						str_truncate(aBefore, sizeof(aBefore), m_aCompletionBuffer, m_CompletionCommandStart);
						char aBuf[IConsole::CMDLINE_LENGTH];
						str_format(aBuf, sizeof(aBuf), "%s%s%s", aBefore, m_vpCommandSuggestions[m_CompletionChosen], m_aCompletionBuffer + m_CompletionCommandEnd);
						m_Input.Set(aBuf);
						m_Input.SetCursorOffset(str_length(m_vpCommandSuggestions[m_CompletionChosen]) + m_CompletionCommandStart);
					}
					else if(m_CompletionChosen != -1)
					{
						m_CompletionChosen = -1;
						Reset();
					}
				}

				// Argument completion
				const auto [CompletionType, CompletionPos] = ArgumentCompletion(GetString());
				int CompletionEnumerationCountArgs = m_vpArgumentSuggestions.size();
				if(CompletionEnumerationCountArgs)
				{
					if(m_CompletionChosenArgument == -1 && Direction < 0)
						m_CompletionChosenArgument = 0;
					m_CompletionChosenArgument = (m_CompletionChosenArgument + Direction + CompletionEnumerationCountArgs) % CompletionEnumerationCountArgs;
					m_CompletionArgumentPosition = CompletionPos;

					// get command
					char aBuf[IConsole::CMDLINE_LENGTH];
					str_copy(aBuf, GetString(), m_CompletionArgumentPosition);
					str_append(aBuf, " ");

					// append argument
					str_append(aBuf, m_vpArgumentSuggestions[m_CompletionChosenArgument]);
					m_Input.Set(aBuf);
				}
				else if(m_CompletionChosenArgument != -1)
				{
					m_CompletionChosenArgument = -1;
					Reset();
				}
			}
			else
			{
				// Use Tab / Shift-Tab to cycle through search matches
				SelectNextSearchMatch(Direction);
			}
			Handled = true;
		}
		else if(Event.m_Key == KEY_PAGEUP)
		{
			m_BacklogCurLine += GetLinesToScroll(-1, m_LinesRendered);
			Handled = true;
		}
		else if(Event.m_Key == KEY_PAGEDOWN)
		{
			m_BacklogCurLine -= GetLinesToScroll(1, m_LinesRendered);
			if(m_BacklogCurLine < 0)
			{
				m_BacklogCurLine = 0;
			}
			Handled = true;
		}
		else if(Event.m_Key == KEY_MOUSE_WHEEL_UP)
		{
			m_BacklogCurLine += GetLinesToScroll(-1, 1);
			Handled = true;
		}
		else if(Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
		{
			--m_BacklogCurLine;
			if(m_BacklogCurLine < 0)
			{
				m_BacklogCurLine = 0;
			}
			Handled = true;
		}
		// in order not to conflict with CLineInput's handling of Home/End only
		// react to it when the input is empty
		else if(Event.m_Key == KEY_HOME && m_Input.IsEmpty())
		{
			m_BacklogCurLine += GetLinesToScroll(-1, -1);
			m_BacklogLastActiveLine = m_BacklogCurLine;
			Handled = true;
		}
		else if(Event.m_Key == KEY_END && m_Input.IsEmpty())
		{
			m_BacklogCurLine = 0;
			Handled = true;
		}
		else if(Event.m_Key == KEY_ESCAPE && m_SearchInput.IsSearching())
		{
			SetSearching(false);
			Handled = true;
		}
		else if(Event.m_Key == KEY_F && m_pGameConsole->Input()->ModifierIsPressed())
		{
			SetSearching(true);
			Handled = true;
		}
	}

	if(!Handled)
	{
		Handled = m_Input.ProcessInput(Event);
		if(Handled)
			UpdateSearch();
	}

	if(Event.m_Flags & (IInput::FLAG_PRESS | IInput::FLAG_TEXT))
		UpdateInputState(Event.m_Key != KEY_TAB && Event.m_Key != KEY_LSHIFT && Event.m_Key != KEY_RSHIFT);

	return Handled;
}

void CGameConsole::CInstance::UpdateInputState(bool ResetCompletion)
{
	if(ResetCompletion)
	{
		const char *pInputStr = m_Input.GetString();

		m_CompletionChosen = -1;
		str_copy(m_aCompletionBuffer, pInputStr);

		const auto [CompletionType, CompletionPos] = ArgumentCompletion(GetString());
		if(CompletionType != EArgumentCompletionType::NONE)
		{
			for(const auto &Entry : gs_aArgumentCompletionEntries)
			{
				if(Entry.m_Type != CompletionType)
					continue;
				const int Len = str_length(Entry.m_pCommandName);
				if(str_comp_nocase_num(pInputStr, Entry.m_pCommandName, Len) == 0 && str_isspace(pInputStr[Len]))
				{
					m_CompletionChosenArgument = -1;
					str_copy(m_aCompletionBufferArgument, &pInputStr[CompletionPos]);
				}
			}
		}

		Reset();
	}

	// 更新当前命令信息；鼠标切换搜索也走同一入口。
	{
		char aCmd[IConsole::CMDLINE_LENGTH];
		GetCommand(GetString(), aCmd);
		char aBuf[IConsole::CMDLINE_LENGTH];
		StrCopyUntilSpace(aBuf, sizeof(aBuf), aCmd);

		const IConsole::ICommandInfo *pCommand = m_pGameConsole->m_pConsole->GetCommandInfo(aBuf, m_CompletionFlagmask,
			m_Type != CGameConsole::CONSOLETYPE_LOCAL && m_pGameConsole->Client()->RconAuthed() && m_pGameConsole->Client()->UseTempRconCommands());
		if(pCommand)
		{
			m_IsCommand = true;
			m_pCommandName = pCommand->Name();
			m_pCommandHelp = pCommand->Help();
			m_pCommandParams = pCommand->Params();
		}
		else
		{
			m_IsCommand = false;
		}
	}
}

void CGameConsole::CInstance::PrintLine(const char *pLine, int Len, ColorRGBA PrintColor, const SColorSpan *pColorSpans, size_t NumColorSpans, std::shared_ptr<const QmChatExport::SMetadata> pChatMetadata, bool CommandEcho)
{
	// We must ensure that no log messages are printed while owning
	// m_BacklogPendingLock or this will result in a dead lock.
	const CLockScope LockScope(m_BacklogPendingLock);
	CBacklogEntry *pEntry = m_BacklogPending.Allocate(sizeof(CBacklogEntry) + Len);
	pEntry->m_YOffset = -1.0f;
	pEntry->m_PrintColor = PrintColor;
	pEntry->m_Length = Len;
	pEntry->m_LogCategory = CommandEcho ? QM_CONSOLE_LOG_CATEGORY_COMMAND : QmClassifyConsoleLogLine(pLine, (size_t)Len);
	pEntry->m_ExportId = m_NextExportId++;
	pEntry->m_ExportSelected = false;
	pEntry->m_CommandEcho = CommandEcho;
	if(NumColorSpans > 0)
		m_PendingColorSpansByExportId[pEntry->m_ExportId].assign(pColorSpans, pColorSpans + NumColorSpans);
	if(pChatMetadata)
		m_PendingChatMetadataByExportId[pEntry->m_ExportId] = std::move(pChatMetadata);
	pEntry->m_LineCount = -1;
	str_copy(pEntry->m_aText, pLine, Len + 1);
}

int CGameConsole::CInstance::LogFilterCategoryForButton(int ButtonIndex)
{
	switch(ButtonIndex)
	{
	case 0: return QM_CONSOLE_LOG_CATEGORY_ALL;
	case 1: return QM_CONSOLE_LOG_CATEGORY_PLAYER;
	case 2: return QM_CONSOLE_LOG_CATEGORY_SYSTEM;
	case 3: return QM_CONSOLE_LOG_CATEGORY_COMMAND;
	case 4: return QM_CONSOLE_LOG_CATEGORY_BINDS;
	default: return QM_CONSOLE_LOG_CATEGORY_ALL;
	}
}

bool CGameConsole::CInstance::MatchesLogFilter(const CBacklogEntry *pEntry) const
{
	// 分类在 PrintLine 时已存进条目，这里不再按行文本重新分类。
	return pEntry != nullptr && QmConsoleLogCategoryPassesFilter(pEntry->m_LogCategory, m_LogFilterMask);
}

void CGameConsole::CInstance::SetLogFilterMask(int Mask)
{
	// 空掩码会让控制台一行不剩，用户也没有可点的按钮能走出来，因此不保留这个状态。
	const int Normalized = QmNormalizeConsoleLogFilterMask(Mask);
	if(m_LogFilterMask == Normalized)
		return;
	m_LogFilterMask = Normalized;
	m_BacklogCurLine = 0;
	m_BacklogLastActiveLine = -1;
	m_NewLineCounter = 0;
	InvalidateTotalBacklogLines();
	m_Selection.Clear();
	m_MouseIsPress = false;
	m_ScrollbarDragging = false;
	m_ScrollbarDragOffset = 0.0f;
	if(m_SearchInput.IsSearching())
		UpdateSearch();

	// 顶栏分类选择跨启动保留；只写变量，落盘交给常规配置保存
	if(m_Type == CONSOLETYPE_LOCAL)
		g_Config.m_QmConsoleFilterMask = Normalized;
}

int CGameConsole::CInstance::TotalBacklogLines()
{
	if(m_TotalBacklogLinesValid)
		return m_TotalBacklogLines;

	int TotalLines = 0;
	for(CBacklogEntry *pEntry = m_Backlog.First(); pEntry; pEntry = m_Backlog.Next(pEntry))
	{
		if(pEntry->m_LineCount > 0 && MatchesLogFilter(pEntry))
			TotalLines += pEntry->m_LineCount;
	}
	m_TotalBacklogLines = TotalLines;
	m_TotalBacklogLinesValid = true;
	return m_TotalBacklogLines;
}

void CGameConsole::CInstance::InvalidateTotalBacklogLines()
{
	m_TotalBacklogLines = 0;
	m_TotalBacklogLinesValid = false;
}

bool CGameConsole::CInstance::IsChatExportableEntry(const CBacklogEntry *pEntry) const
{
	// 导出只针对玩家聊天行；分类已存进条目，直接按位判断。
	return pEntry != nullptr && (pEntry->m_LogCategory & QM_CONSOLE_LOG_CATEGORY_PLAYER) != 0;
}

void CGameConsole::CInstance::ClearChatExportSelection()
{
	for(CBacklogEntry *pEntry = m_Backlog.First(); pEntry; pEntry = m_Backlog.Next(pEntry))
		pEntry->m_ExportSelected = false;
	m_ChatExportAnchorId = -1;
}

void CGameConsole::CInstance::SelectAllChatExportable() REQUIRES(!m_BacklogPendingLock)
{
	PumpBacklogPending();
	for(CBacklogEntry *pEntry = m_Backlog.First(); pEntry; pEntry = m_Backlog.Next(pEntry))
	{
		if(IsChatExportableEntry(pEntry))
			pEntry->m_ExportSelected = true;
	}
}

int CGameConsole::CInstance::SelectedChatExportCount()
{
	int Count = 0;
	for(CBacklogEntry *pEntry = m_Backlog.First(); pEntry; pEntry = m_Backlog.Next(pEntry))
	{
		if(IsChatExportableEntry(pEntry) && pEntry->m_ExportSelected)
			++Count;
	}
	return Count;
}

void CGameConsole::CInstance::ToggleChatExportEntry(CBacklogEntry *pEntry, bool RangeSelect)
{
	if(!IsChatExportableEntry(pEntry))
		return;

	const bool Select = !pEntry->m_ExportSelected;
	if(RangeSelect && m_ChatExportAnchorId >= 0)
	{
		const int RangeStart = minimum(m_ChatExportAnchorId, pEntry->m_ExportId);
		const int RangeEnd = maximum(m_ChatExportAnchorId, pEntry->m_ExportId);
		for(CBacklogEntry *pRangeEntry = m_Backlog.First(); pRangeEntry; pRangeEntry = m_Backlog.Next(pRangeEntry))
		{
			if(IsChatExportableEntry(pRangeEntry) && pRangeEntry->m_ExportId >= RangeStart && pRangeEntry->m_ExportId <= RangeEnd)
				pRangeEntry->m_ExportSelected = Select;
		}
	}
	else
	{
		pEntry->m_ExportSelected = Select;
	}
	m_ChatExportAnchorId = pEntry->m_ExportId;
	m_Selection.Clear();
}

void CGameConsole::CInstance::SetChatExportMode(bool Enable)
{
	if(!Enable)
		CancelChatExport();
	if(m_ChatExportMode == Enable)
		return;

	if(Enable)
	{
		PumpBacklogPending();
		if(m_SearchInput.IsSearching())
			SetSearching(false);
		ClearChatExportSelection();
		m_ChatExportPreviousFilterMask = m_LogFilterMask;
		SetLogFilterMask(QM_CONSOLE_LOG_CATEGORY_PLAYER);
		m_ChatExportMode = true;
		m_Selection.Clear();
		m_MouseIsPress = false;
	}
	else
	{
		m_ChatExportMode = false;
		ClearChatExportSelection();
		SetLogFilterMask(m_ChatExportPreviousFilterMask);
		m_Selection.Clear();
		m_MouseIsPress = false;
	}
	UpdateBacklogTextAttributes();
}

int CGameConsole::CInstance::GetLinesToScroll(int Direction, int LinesToScroll)
{
	auto *pEntry = m_Backlog.Last();
	int Line = 0;
	int LinesToSkip = (Direction == -1 ? m_BacklogCurLine + m_LinesRendered : m_BacklogCurLine - 1);
	while(Line < LinesToSkip && pEntry)
	{
		if(pEntry->m_LineCount == -1)
			UpdateEntryTextAttributes(pEntry);
		if(MatchesLogFilter(pEntry))
			Line += pEntry->m_LineCount;
		pEntry = m_Backlog.Prev(pEntry);
	}

	int Amount = maximum(0, Line - LinesToSkip);
	while(pEntry && (LinesToScroll > 0 ? Amount < LinesToScroll : true))
	{
		if(pEntry->m_LineCount == -1)
			UpdateEntryTextAttributes(pEntry);
		if(MatchesLogFilter(pEntry))
			Amount += pEntry->m_LineCount;
		pEntry = Direction == -1 ? m_Backlog.Prev(pEntry) : m_Backlog.Next(pEntry);
	}

	return LinesToScroll > 0 ? minimum(Amount, LinesToScroll) : Amount;
}

void CGameConsole::CInstance::ScrollToCenter(int StartLine, int EndLine)
{
	// This method is used to scroll lines from `StartLine` to `EndLine` to the center of the screen, if possible.

	// Find target line
	int Target = maximum(0, (int)ceil(StartLine - minimum(StartLine - EndLine, m_LinesRendered) / 2) - m_LinesRendered / 2);
	if(m_BacklogCurLine == Target)
		return;

	// Compute actual amount of lines to scroll to make sure lines fit in viewport and we don't have empty space
	int Direction = m_BacklogCurLine - Target < 0 ? -1 : 1;
	int LinesToScroll = absolute(Target - m_BacklogCurLine);
	int ComputedLines = GetLinesToScroll(Direction, LinesToScroll);

	if(Direction == -1)
		m_BacklogCurLine += ComputedLines;
	else
		m_BacklogCurLine -= ComputedLines;
}

float CGameConsole::CInstance::FontSize() const
{
	return m_Type == CONSOLETYPE_LOCAL ? QmConsoleAppearance::FontSize(g_Config.m_QmConsoleFontSize) : FONT_SIZE;
}

float CGameConsole::CInstance::BacklogLineWidth() const
{
	// 始终预留滚动条宽度，确保测量、命中和绘制使用同一套换行位置。
	return maximum(1.0f, m_pGameConsole->Ui()->Screen()->w - 10.0f - CONSOLE_SCROLLBAR_WIDTH - CONSOLE_SCROLLBAR_MARGIN - (m_ChatExportMode ? 22.0f : 0.0f));
}

bool CGameConsole::CInstance::ParseEntryColors(const CBacklogEntry *pEntry) const
{
	return pEntry->m_Length > (size_t)str_length("xxxx-xx-xx xx:xx:xx x ") && str_startswith(pEntry->m_aText + str_length("xxxx-xx-xx xx:xx:xx x "), "chat/client");
}

CQmConsoleSelection::CPosition CGameConsole::CInstance::SelectionPositionAt(vec2 Position, float LogBottom, float LineHeight)
{
	float EntryBottom = LogBottom;
	int SkippedLines = 0;
	bool First = true;
	CBacklogEntry *pOldestEntry = nullptr;
	for(CBacklogEntry *pEntry = m_Backlog.Last(); pEntry; pEntry = m_Backlog.Prev(pEntry))
	{
		if(!MatchesLogFilter(pEntry))
			continue;
		if(SkippedLines + pEntry->m_LineCount <= m_BacklogCurLine)
		{
			SkippedLines += pEntry->m_LineCount;
			continue;
		}
		if(First)
		{
			EntryBottom += (m_BacklogCurLine - SkippedLines) * LineHeight;
			First = false;
		}
		pOldestEntry = pEntry;
		const float EntryTop = EntryBottom - pEntry->m_YOffset;
		if(Position.y >= EntryTop)
		{
			CColoredParts Text(pEntry->m_aText, ParseEntryColors(pEntry));
			CTextCursor Cursor;
			Cursor.SetPosition(vec2(0.0f, EntryTop));
			Cursor.m_FontSize = FontSize();
			Cursor.m_LineWidth = BacklogLineWidth();
			Cursor.m_MaxLines = pEntry->m_LineCount;
			Cursor.m_LineSpacing = LINE_SPACING;
			Cursor.m_Flags = 0;
			Cursor.m_CursorMode = TEXT_CURSOR_CURSOR_MODE_CALCULATE;
			Cursor.m_RenderCursor = false;
			Cursor.m_ReleaseMouse = Position;
			m_pGameConsole->TextRender()->TextEx(&Cursor, Text.Text());
			return {pEntry->m_ExportId, maximum(0, Cursor.m_CursorCharacter)};
		}
		EntryBottom = EntryTop;
	}
	return pOldestEntry ? CQmConsoleSelection::CPosition{pOldestEntry->m_ExportId, 0} : CQmConsoleSelection::CPosition{};
}

std::string CGameConsole::CInstance::SelectionText()
{
	std::string Result;
	for(CBacklogEntry *pEntry = m_Backlog.First(); pEntry; pEntry = m_Backlog.Next(pEntry))
	{
		if(!MatchesLogFilter(pEntry) || !m_Selection.ContainsEntry(pEntry->m_ExportId))
			continue;
		CColoredParts Text(pEntry->m_aText, ParseEntryColors(pEntry));
		m_Selection.AppendText(pEntry->m_ExportId, Text.Text(), Result);
	}
	return Result;
}

void CGameConsole::CInstance::UpdateEntryTextAttributes(CBacklogEntry *pEntry) const
{
	CTextCursor Cursor;
	Cursor.m_FontSize = FontSize();
	Cursor.m_Flags = 0;
	Cursor.m_LineWidth = BacklogLineWidth();
	Cursor.m_MaxLines = 10;
	Cursor.m_LineSpacing = LINE_SPACING;
	const CColoredParts Text(pEntry->m_aText, ParseEntryColors(pEntry));
	m_pGameConsole->TextRender()->TextEx(&Cursor, Text.Text(), -1);
	pEntry->m_YOffset = Cursor.Height();
	pEntry->m_LineCount = Cursor.m_LineCount;
}

bool CGameConsole::CInstance::IsInputHidden() const
{
	if(m_Type != CONSOLETYPE_REMOTE)
		return false;
	if(m_pGameConsole->Client()->State() != IClient::STATE_ONLINE || m_SearchInput.IsSearching())
		return false;
	if(m_pGameConsole->Client()->RconAuthed())
		return false;
	return m_UserGot || !m_UsernameReq;
}

void CGameConsole::CInstance::SetSearching(bool Searching)
{
	if(Searching)
	{
		if(!m_SearchInput.Begin(m_Input.GetString()))
		{
			m_Input.SelectAll();
			return;
		}
		m_Selection.Clear();
		m_Input.SetClipboardLineCallback(nullptr); // 搜索粘贴按普通文本处理，不执行命令。
		m_Input.Set(m_aCurrentSearchString);
		m_Input.SelectAll();
		UpdateSearch();
	}
	else if(const auto Draft = m_SearchInput.End())
	{
		m_Selection.Clear();
		m_Input.SetClipboardLineCallback([this](const char *pLine) { ExecuteLine(pLine); });
		m_Input.Set(Draft->c_str());
		UpdateInputState(true);
	}
}

void CGameConsole::CInstance::ClearSearch()
{
	m_vSearchMatches.clear();
	m_CurrentMatchIndex = -1;
	m_Input.Clear();
	m_aCurrentSearchString[0] = '\0';
}

void CGameConsole::CInstance::UpdateSearch()
{
	if(!m_SearchInput.IsSearching())
		return;

	const char *pSearchText = m_Input.GetString();
	bool SearchChanged = str_utf8_comp_nocase(pSearchText, m_aCurrentSearchString) != 0;

	str_copy(m_aCurrentSearchString, pSearchText);

	m_vSearchMatches.clear();
	if(pSearchText[0] == '\0')
	{
		m_CurrentMatchIndex = -1;
		return;
	}

	if(SearchChanged)
	{
		m_CurrentMatchIndex = -1;
		m_Selection.Clear();
	}

	ITextRender *pTextRender = m_pGameConsole->Ui()->TextRender();
	const float LineWidth = BacklogLineWidth();

	CBacklogEntry *pEntry = m_Backlog.Last();
	int EntryLine = 0;
	std::vector<QmConsoleText::SRange> vMatches;

	for(; pEntry; pEntry = m_Backlog.Prev(pEntry))
	{
		if(pEntry->m_LineCount == -1)
			UpdateEntryTextAttributes(pEntry);
		if(!MatchesLogFilter(pEntry))
			continue;

		CColoredParts ColoredParts(pEntry->m_aText, ParseEntryColors(pEntry));
		const char *pText = ColoredParts.Text();
		QmConsoleText::CollectSearchMatches(pText, pSearchText, vMatches);
		int EntryLineCount = pEntry->m_LineCount;

		// Find all occurrences of the search string and save their positions
		for(const auto &Match : vMatches)
		{
			const int Pos = Match.m_StartByte;
			const int Length = Match.m_EndByte - Pos;

			if(EntryLineCount == 1)
			{
				m_vSearchMatches.emplace_back(Pos, Length, EntryLine, EntryLine, EntryLine);
			}
			else
			{
				// A match can span multiple lines in case of a multiline entry, so we need to know which line the match starts at
				// and which line it ends at in order to put it in viewport properly
				STextSizeProperties Props;
				int LineCount;
				Props.m_pLineCount = &LineCount;

				// Compute line of end match
				pTextRender->TextWidth(FontSize(), pText, Match.m_EndByte, LineWidth, 0, Props);
				int EndLine = (EntryLineCount - LineCount);
				int MatchEndLine = EntryLine + EndLine;

				// Compute line of start of match
				int MatchStartLine = MatchEndLine;
				if(LineCount > 1)
				{
					pTextRender->TextWidth(FontSize(), pText, Pos, LineWidth, 0, Props);
					int StartLine = (EntryLineCount - LineCount);
					MatchStartLine = EntryLine + StartLine;
				}

				m_vSearchMatches.emplace_back(Pos, Length, MatchStartLine, MatchEndLine, EntryLine);
			}
		}

		EntryLine += pEntry->m_LineCount;
	}

	if(!m_vSearchMatches.empty() && SearchChanged)
		m_CurrentMatchIndex = 0;
	else
		m_CurrentMatchIndex = m_vSearchMatches.empty() ? -1 : std::clamp(m_CurrentMatchIndex, 0, (int)m_vSearchMatches.size() - 1);

	// Reverse order of lines by sorting so we have matches from top to bottom instead of bottom to top
	std::sort(m_vSearchMatches.begin(), m_vSearchMatches.end(), [](const SSearchMatch &MatchA, const SSearchMatch &MatchB) {
		if(MatchA.m_StartLine == MatchB.m_StartLine)
			return MatchA.m_Pos < MatchB.m_Pos; // Make sure to keep position order
		return MatchA.m_StartLine > MatchB.m_StartLine;
	});

	if(!m_vSearchMatches.empty() && SearchChanged)
	{
		ScrollToCenter(m_vSearchMatches[0].m_StartLine, m_vSearchMatches[0].m_EndLine);
	}
}

void CGameConsole::CInstance::Dump()
{
	char aTimestamp[20];
	str_timestamp(aTimestamp, sizeof(aTimestamp));
	char aFilename[IO_MAX_PATH_LENGTH];
	str_format(aFilename, sizeof(aFilename), "dumps/%s_dump_%s.txt", m_pName, aTimestamp);
	IOHANDLE File = m_pGameConsole->Storage()->OpenFile(aFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(File)
	{
		PumpBacklogPending();
		for(CInstance::CBacklogEntry *pEntry = m_Backlog.First(); pEntry; pEntry = m_Backlog.Next(pEntry))
		{
			io_write(File, pEntry->m_aText, pEntry->m_Length);
			io_write_newline(File);
		}
		io_close(File);
		log_info("console", "%s contents were written to '%s'", m_pName, aFilename);
	}
	else
	{
		log_error("console", "Failed to open '%s'", aFilename);
	}
}

bool CGameConsole::CInstance::ExportSelectedChat()
{
	if(m_pChatExportJob)
		return false;
	PumpBacklogPending();

	const char *pLocalName = "";
	const int LocalClientId = m_pGameConsole->GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId >= 0 && LocalClientId < MAX_CLIENTS)
		pLocalName = m_pGameConsole->GameClient()->m_aClients[LocalClientId].m_aName;

	std::vector<QmChatExport::SLine> vLines;
	for(CBacklogEntry *pEntry = m_Backlog.First(); pEntry; pEntry = m_Backlog.Next(pEntry))
	{
		if(!IsChatExportableEntry(pEntry) || !pEntry->m_ExportSelected)
			continue;
		QmChatExport::SLine Line;
		if(!TryParseChatExportLine(pEntry->m_aText, pLocalName, Line))
			continue;
		// 打印时记下的身份与头像优先于事后解析：改名或换皮肤不影响历史消息。
		const auto Metadata = m_ChatMetadataByExportId.find(pEntry->m_ExportId);
		if(Metadata != m_ChatMetadataByExportId.end())
		{
			Line.m_Sender = Metadata->second->m_Sender;
			Line.m_Message = Metadata->second->m_Message;
			Line.m_Local = Metadata->second->m_Local;
			Line.m_pAvatar = Metadata->second->m_pAvatar;
		}
		vLines.push_back(std::move(Line));
	}

	if(vLines.empty())
	{
		m_pGameConsole->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", Localize("No chat log selected"));
		return false;
	}

	char aTimestamp[20];
	str_timestamp(aTimestamp, sizeof(aTimestamp));
	char aBaseFilename[IO_MAX_PATH_LENGTH];
	str_format(aBaseFilename, sizeof(aBaseFilename), "qmclient/chat_log/local_chat_export_%s_%lld", aTimestamp, (long long)time_get_nanoseconds().count());
	QmChatExport::SLabels Labels{Localize("QmClient chat log"), Localize("Total"), Localize("Messages")};
	m_pChatExportJob = std::make_shared<CQmChatExportJob>(m_pGameConsole->Storage(), aBaseFilename, std::move(vLines), std::move(Labels));
	return true;
}

void CGameConsole::CInstance::CancelChatExport()
{
	if(!m_pChatExportJob)
		return;
	if(m_pChatExportJob->m_Queued)
	{
		// 已经交给后台：只发取消请求，等它自己收尾，避免拆掉正在使用的数据。
		if(m_pChatExportJob->State() != IJob::STATE_DONE)
			m_pChatExportJob->m_Cancelled.store(true);
	}
	else
	{
		m_pChatExportJob.reset();
		m_pGameConsole->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", Localize("Chat export cancelled"));
	}
}

void CGameConsole::CInstance::UpdateChatExport()
{
	if(!m_pChatExportJob)
		return;
	if(!m_pChatExportJob->m_Queued)
	{
		if(m_pChatExportJob->PrepareGlyphs(m_pGameConsole->TextRender(), m_pGameConsole->Graphics()))
		{
			m_pChatExportJob->m_Queued = true;
			m_pGameConsole->Engine()->AddJob(m_pChatExportJob);
		}
		return;
	}
	// 取消仅发请求；STATE_DONE 后才能读取后台结果并释放任务。
	if(m_pChatExportJob->State() != IJob::STATE_DONE)
		return;
	const auto pJob = std::move(m_pChatExportJob);
	if(pJob->m_Success)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), Localize("Exported %d chat messages"), (int)pJob->m_vLines.size());
		m_pGameConsole->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", aBuf);
		SetChatExportMode(false);
	}
	else if(pJob->m_Cancelled.load())
		m_pGameConsole->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", Localize("Chat export cancelled"));
	else
		m_pGameConsole->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", Localize("Chat export failed"));
}

CGameConsole::CGameConsole() :
	m_LocalConsole(CONSOLETYPE_LOCAL), m_RemoteConsole(CONSOLETYPE_REMOTE)
{
	m_ConsoleType = CONSOLETYPE_LOCAL;
	m_ConsoleState = CONSOLE_CLOSED;
	m_StateChangeEnd = 0.0f;
	m_StateChangeDuration = 0.1f;

	m_pConsoleLogger = new CConsoleLogger(this);
}

CGameConsole::~CGameConsole()
{
	if(m_pConsoleLogger)
		m_pConsoleLogger->OnConsoleDeletion();
}

CGameConsole::CInstance *CGameConsole::ConsoleForType(int ConsoleType)
{
	if(ConsoleType == CONSOLETYPE_REMOTE)
		return &m_RemoteConsole;
	return &m_LocalConsole;
}

CGameConsole::CInstance *CGameConsole::CurrentConsole()
{
	return ConsoleForType(m_ConsoleType);
}

void CGameConsole::OnReset()
{
	m_RemoteConsole.Reset();
	m_RemoteConsole.m_Selection.Finish();
	m_RemoteConsole.m_ScrollbarDragging = false;
	m_RemoteConsole.m_ScrollbarDragOffset = 0.0f;
}

int CGameConsole::PossibleMaps(const char *pStr, IConsole::FPossibleCallback pfnCallback, void *pUser)
{
	int Index = 0;
	for(const std::string &Entry : Client()->MaplistEntries())
	{
		if(str_find_nocase(Entry.c_str(), pStr))
		{
			pfnCallback(Index, Entry.c_str(), pUser);
			Index++;
		}
	}
	return Index;
}

// only defined for 0<=t<=1
static float ConsoleScaleFunc(float t)
{
	return std::sin(std::acos(1.0f - t));
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct CCompletionOptionRenderInfo
{
	CGameConsole *m_pSelf;
	CTextCursor m_Cursor;
	const char *m_pCurrentCmd;
	int m_WantedCompletion;
	float m_Offset;
	float *m_pOffsetChange;
	float m_Width;
	float m_TotalWidth;
};

void CGameConsole::PossibleCommandsRenderCallback(int Index, const char *pStr, void *pUser)
{
	CCompletionOptionRenderInfo *pInfo = static_cast<CCompletionOptionRenderInfo *>(pUser);

	ColorRGBA TextColor;
	if(Index == pInfo->m_WantedCompletion)
	{
		TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		const float TextWidth = pInfo->m_pSelf->TextRender()->TextWidth(pInfo->m_Cursor.m_FontSize, pStr);
		const CUIRect Rect = {pInfo->m_Cursor.m_X - 2.0f, pInfo->m_Cursor.m_Y - 2.0f, TextWidth + 4.0f, pInfo->m_Cursor.m_FontSize + 4.0f};
		Rect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.85f), IGraphics::CORNER_ALL, 2.0f);

		// scroll when out of sight
		const bool MoveLeft = Rect.x - *pInfo->m_pOffsetChange < 0.0f;
		const bool MoveRight = Rect.x + Rect.w - *pInfo->m_pOffsetChange > pInfo->m_Width;
		if(MoveLeft && !MoveRight)
		{
			*pInfo->m_pOffsetChange -= -Rect.x + pInfo->m_Width / 4.0f;
		}
		else if(!MoveLeft && MoveRight)
		{
			*pInfo->m_pOffsetChange += Rect.x + Rect.w - pInfo->m_Width + pInfo->m_Width / 4.0f;
		}
	}
	else
	{
		TextColor = ColorRGBA(0.75f, 0.75f, 0.75f, 1.0f);
	}

	const char *pMatchStart = str_find_nocase(pStr, pInfo->m_pCurrentCmd);
	if(pMatchStart)
	{
		pInfo->m_pSelf->TextRender()->TextColor(TextColor);
		pInfo->m_pSelf->TextRender()->TextEx(&pInfo->m_Cursor, pStr, pMatchStart - pStr);
		pInfo->m_pSelf->TextRender()->TextColor(1.0f, 0.75f, 0.0f, 1.0f);
		pInfo->m_pSelf->TextRender()->TextEx(&pInfo->m_Cursor, pMatchStart, str_length(pInfo->m_pCurrentCmd));
		pInfo->m_pSelf->TextRender()->TextColor(TextColor);
		pInfo->m_pSelf->TextRender()->TextEx(&pInfo->m_Cursor, pMatchStart + str_length(pInfo->m_pCurrentCmd));
	}
	else
	{
		pInfo->m_pSelf->TextRender()->TextColor(TextColor);
		pInfo->m_pSelf->TextRender()->TextEx(&pInfo->m_Cursor, pStr);
	}

	pInfo->m_Cursor.m_X += 7.0f;
	pInfo->m_TotalWidth = pInfo->m_Cursor.m_X + pInfo->m_Offset;
}

void CGameConsole::Prompt(char (&aPrompt)[32])
{
	CInstance *pConsole = CurrentConsole();
	if(pConsole->m_SearchInput.IsSearching())
	{
		str_format(aPrompt, sizeof(aPrompt), "%s: ", Localize("Searching"));
	}
	else if(m_ConsoleType == CONSOLETYPE_REMOTE)
	{
		if(Client()->State() == IClient::STATE_LOADING || Client()->State() == IClient::STATE_ONLINE)
		{
			if(Client()->RconAuthed())
				str_copy(aPrompt, "rcon> ");
			else if(pConsole->m_UsernameReq && !pConsole->m_UserGot)
				str_format(aPrompt, sizeof(aPrompt), "%s> ", Localize("Enter Username"));
			else
				str_format(aPrompt, sizeof(aPrompt), "%s> ", Localize("Enter Password"));
		}
		else
		{
			str_format(aPrompt, sizeof(aPrompt), "%s> ", Localize("NOT CONNECTED"));
		}
	}
	else
	{
		str_copy(aPrompt, "> ");
	}
}

void CGameConsole::OnRender()
{
	// 导出准备每帧推进一步，完成后任务才交给后台线程。
	m_LocalConsole.UpdateChatExport();
	CUIRect Screen = *Ui()->Screen();
	CInstance *pConsole = CurrentConsole();

	const float MaxConsoleHeight = m_ConsoleType == CONSOLETYPE_LOCAL && m_LocalConsoleFullscreen ? Screen.h : Screen.h * 3 / 5.0f;
	float Progress = (Client()->GlobalTime() - (m_StateChangeEnd - m_StateChangeDuration)) / m_StateChangeDuration;

	if(Progress >= 1.0f)
	{
		if(m_ConsoleState == CONSOLE_CLOSING)
		{
			m_ConsoleState = CONSOLE_CLOSED;
			pConsole->m_BacklogLastActiveLine = -1;
		}
		else if(m_ConsoleState == CONSOLE_OPENING)
		{
			m_ConsoleState = CONSOLE_OPEN;
			pConsole->m_Input.Activate(EInputPriority::CONSOLE);
		}

		Progress = 1.0f;
	}

	if(m_ConsoleState == CONSOLE_OPEN && g_Config.m_ClEditor)
		Toggle(CONSOLETYPE_LOCAL);

	if(m_ConsoleState == CONSOLE_CLOSED)
		return;

	Ui()->MapScreen();
	const bool LocalConsole = m_ConsoleType == CONSOLETYPE_LOCAL;
	const float FontSize = pConsole->FontSize();
	const auto Palette = QmConsoleAppearance::Palette(g_Config);
	const bool SettingsOpen = ConsoleSettingsOpen();
	const bool FontChanged = LocalConsole && m_LastLocalFontSize != FontSize;
	if(FontChanged)
	{
		m_LastLocalFontSize = FontSize;
		pConsole->UpdateBacklogTextAttributes();
		pConsole->m_Selection.Finish();
		pConsole->m_ScrollbarDragging = false;
		pConsole->m_MouseIsPress = false;
		pConsole->m_Input.GetMouseSelection()->m_Selecting = false;
		if(pConsole->m_SearchInput.IsSearching())
			pConsole->UpdateSearch();
	}
	const char *apFilterLabels[CInstance::LOG_FILTER_BUTTON_COUNT] = {
		Localize("All"), Localize("Players"), Localize("System"), Localize("Commands"), Localize("Binds")};
	constexpr float FilterSpacing = 4.0f;
	constexpr float FilterPadding = 6.0f;
	const float FilterIndicatorWidth = FontSize + 4.0f;
	float aFilterWidths[CInstance::LOG_FILTER_BUTTON_COUNT];
	float TotalFilterWidth = FilterSpacing * (CInstance::LOG_FILTER_BUTTON_COUNT - 1);
	for(int i = 0; i < CInstance::LOG_FILTER_BUTTON_COUNT; ++i)
	{
		aFilterWidths[i] = TextRender()->TextWidth(FontSize, apFilterLabels[i]) + FilterPadding * 2.0f + (LocalConsole && i != 0 ? FilterIndicatorWidth : 0.0f);
		TotalFilterWidth += aFilterWidths[i];
	}
	enum class EToolbarAction
	{
		SEARCH,
		FOLLOW,
		EXPORT,
		CANCEL_EXPORT,
		SAVE_EXPORT,
		CLEAR_EXPORT,
		SELECT_CHAT,
		EXPAND,
		SETTINGS,
	};
	struct SToolbarAction
	{
		EToolbarAction m_Action;
		const char *m_pLabel;
		float m_Width;
		bool m_Selected;
	};
	std::array<SToolbarAction, 6> aActions;
	int NumActions = 0;
	const auto AddAction = [&](EToolbarAction Action, const char *pLabel, bool Selected = false) {
		dbg_assert(NumActions < static_cast<int>(aActions.size()), "Too many console toolbar actions");
		aActions[NumActions++] = {Action, pLabel, TextRender()->TextWidth(FontSize, pLabel) + FilterPadding * 2.0f, Selected};
	};
	if(LocalConsole)
	{
		if(pConsole->m_ChatExportMode)
		{
			AddAction(EToolbarAction::CANCEL_EXPORT, Localize("Cancel"));
			if(!pConsole->m_pChatExportJob)
			{
				AddAction(EToolbarAction::SAVE_EXPORT, Localize("Export selected"));
				AddAction(EToolbarAction::CLEAR_EXPORT, Localize("Clear"));
				AddAction(EToolbarAction::SELECT_CHAT, Localize("Select all chat"));
			}
		}
		else
		{
			AddAction(EToolbarAction::SEARCH, pConsole->m_SearchInput.IsSearching() ? Localize("Cancel") : Localize("Search"), pConsole->m_SearchInput.IsSearching());
			AddAction(EToolbarAction::FOLLOW, Localize("Following"), pConsole->m_BacklogCurLine == 0);
			AddAction(EToolbarAction::EXPORT, Localize("Select export"));
		}
		AddAction(EToolbarAction::SETTINGS, Localize("Console settings"), SettingsOpen);
		AddAction(EToolbarAction::EXPAND, m_LocalConsoleFullscreen ? Localize("Collapse console") : Localize("Expand console"), m_LocalConsoleFullscreen);
	}
	float TotalActionWidth = 0.0f;
	for(int i = 0; i < NumActions; ++i)
		TotalActionWidth += aActions[i].m_Width + FilterSpacing;
	if(NumActions > 0)
		TotalActionWidth -= FilterSpacing;
	float ToolbarRightMargin = 0.0f;
#if defined(CONF_PLATFORM_IOS)
	ToolbarRightMargin = FontSize * 2.0f + 10.0f;
#endif
	const auto Toolbar = QmConsoleUi::LayoutToolbar(Screen.w - ToolbarRightMargin, pConsole->m_ChatExportMode ? 0.0f : TotalFilterWidth, TotalActionWidth, FontSize + 16.0f);
	const float RowHeight = LocalConsole ? Toolbar.m_Height : FontSize * 2.0f;
	const float FooterHeight = LocalConsole ? FontSize + 6.0f : 0.0f;

	const ColorRGBA PreviousTextColor = TextRender()->GetTextColor();
	const ColorRGBA PreviousTextOutlineColor = TextRender()->GetTextOutlineColor();
	const ColorRGBA PreviousTextSelectionColor = TextRender()->GetTextSelectionColor();
	const unsigned PreviousRenderFlags = TextRender()->GetRenderFlags();
	const EFontPreset PreviousFontPreset = TextRender()->GetFontPreset();
	if(LocalConsole)
		TextRender()->TextOutlineColor(ResolveUiSurfaceForeground(Palette.m_aColors[QmConsoleAppearance::TEXT]).WithAlpha(PreviousTextOutlineColor.a));

	float ConsoleHeightScale;
	if(m_ConsoleState == CONSOLE_OPENING)
		ConsoleHeightScale = ConsoleScaleFunc(Progress);
	else if(m_ConsoleState == CONSOLE_CLOSING)
		ConsoleHeightScale = ConsoleScaleFunc(1.0f - Progress);
	else // CONSOLE_OPEN
		ConsoleHeightScale = ConsoleScaleFunc(1.0f);

	const float ConsoleHeight = ConsoleHeightScale * MaxConsoleHeight;

	const ColorRGBA ShadowColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f);
	const ColorRGBA TransparentColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	const ColorRGBA aBackgroundColors[NUM_CONSOLETYPES] = {ColorRGBA(0.2f, 0.2f, 0.2f, 0.9f), ColorRGBA(0.4f, 0.2f, 0.2f, 0.9f)};
	const ColorRGBA aBorderColors[NUM_CONSOLETYPES] = {ColorRGBA(0.1f, 0.1f, 0.1f, 0.9f), ColorRGBA(0.2f, 0.1f, 0.1f, 0.9f)};

	const bool UpdateConsoleUi = !Ui()->Enabled();
	if(UpdateConsoleUi)
	{
		Ui()->SetEnabled(true);
		Ui()->StartCheck();
		if(CLineInput *pActiveInput = SettingsOpen ? nullptr : CLineInput::GetActiveInput())
		{
			Ui()->SetActiveItem(pActiveInput);
			Ui()->SetActiveItem(nullptr);
		}
		Ui()->Update();
	}

	// 本地控制台使用终端式纯色面板，工具栏与状态栏独立分区。
	if(LocalConsole)
	{
		QmConsoleUi::DrawPanel(Ui(), {0.0f, 0.0f, Screen.w, ConsoleHeight}, Palette.m_aColors[QmConsoleAppearance::BACKGROUND]);
		QmConsoleUi::DrawPanel(Ui(), {0.0f, 0.0f, Screen.w, RowHeight}, Palette.m_Panel);
		QmConsoleUi::DrawPanel(Ui(), {0.0f, ConsoleHeight - FooterHeight, Screen.w, FooterHeight}, Palette.m_Panel);
	}
	else
	{
		Graphics()->TextureSet(g_pData->m_aImages[IMAGE_BACKGROUND_NOISE].m_Id);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(aBackgroundColors[m_ConsoleType]);
		Graphics()->QuadsSetSubset(0, 0, Screen.w / 80.0f, ConsoleHeight / 80.0f);
		IGraphics::CQuadItem QuadItemBackground(0.0f, 0.0f, Screen.w, ConsoleHeight);
		Graphics()->QuadsDrawTL(&QuadItemBackground, 1);
		Graphics()->QuadsEnd();
	}
	// bottom border
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(aBorderColors[m_ConsoleType]);
	IGraphics::CQuadItem QuadItemBorder(0.0f, ConsoleHeight, Screen.w, 1.0f);
	Graphics()->QuadsDrawTL(&QuadItemBorder, 1);
	Graphics()->QuadsEnd();

	// bottom shadow
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor4(ShadowColor, ShadowColor, TransparentColor, TransparentColor);
	IGraphics::CQuadItem QuadItemShadow(0.0f, ConsoleHeight + 1.0f, Screen.w, 10.0f);
	Graphics()->QuadsDrawTL(&QuadItemShadow, 1);
	Graphics()->QuadsEnd();

	{
		// Get height of 1 line
		const float LineHeight = TextRender()->TextBoundingBox(FontSize, " ", -1, -1.0f, LINE_SPACING).m_H;

		float x = 3;
		float y = ConsoleHeight - FontSize * 2.0f - 18.0f - FooterHeight;

		const float InitialX = x;
		const float InitialY = y;

		// render prompt
		CTextCursor PromptCursor;
		PromptCursor.SetPosition(vec2(x, y + FontSize / 2.0f));
		PromptCursor.m_FontSize = FontSize;

		char aPrompt[32];
		TextRender()->TextColor(LocalConsole ? Palette.m_aColors[QmConsoleAppearance::TEXT] : TextRender()->DefaultTextColor());
		Prompt(aPrompt);
		TextRender()->TextEx(&PromptCursor, aPrompt);

		// check if mouse is pressed
		const vec2 ScreenSize = vec2(Screen.w, Screen.h);
		bool LinkClickPending = false;
		vec2 LinkClickPos = vec2(0.0f, 0.0f);
		vec2 LinkClickPress = vec2(0.0f, 0.0f);
		bool ChatExportClickPending = false;
		vec2 ChatExportClickPos = vec2(0.0f, 0.0f);
		const bool CtrlPressed = Input()->ModifierIsPressed();
		const bool WasMousePressed = pConsole->m_MouseIsPress;
		Ui()->UpdateTouchState(m_TouchState);
		if(SettingsOpen)
			m_TouchState.m_ScrollAmount = vec2(0.0f, 0.0f);
		const auto &&GetMousePosition = [&]() -> vec2 {
			if(m_TouchState.m_PrimaryPressed)
			{
				return m_TouchState.m_PrimaryPosition * ScreenSize;
			}
			else
			{
				return Ui()->MousePos();
			}
		};
		if(!SettingsOpen && !pConsole->m_MouseIsPress && (m_TouchState.m_PrimaryPressed || Input()->NativeMousePressed(1)))
		{
			pConsole->m_MouseIsPress = true;
			pConsole->m_MousePress = GetMousePosition();
			m_ButtonPressPosition = pConsole->m_MousePress;
		}
		if(pConsole->m_MouseIsPress && !m_TouchState.m_PrimaryPressed && !Input()->NativeMousePressed(1))
		{
			const vec2 ReleasePos = GetMousePosition();
			pConsole->m_MouseRelease = ReleasePos;
			pConsole->m_MouseIsPress = false;
			if(WasMousePressed && !pConsole->m_ScrollbarDragging && pConsole->m_ChatExportMode && pConsole->m_MousePress.y < pConsole->m_BoundingBox.m_Y && length(ReleasePos - pConsole->m_MousePress) <= LINK_CLICK_DRAG_THRESHOLD)
			{
				ChatExportClickPending = true;
				ChatExportClickPos = ReleasePos;
			}
			else if(WasMousePressed && !pConsole->m_ScrollbarDragging && CtrlPressed && !m_TouchState.m_PrimaryPressed)
			{
				LinkClickPending = true;
				LinkClickPos = ReleasePos;
				LinkClickPress = pConsole->m_MousePress;
			}
		}
		const bool ButtonReleased = WasMousePressed && !pConsole->m_MouseIsPress;
		const bool MousePressedThisFrame = !WasMousePressed && pConsole->m_MouseIsPress;
		const vec2 ButtonMousePosition = ButtonReleased ? pConsole->m_MouseRelease : GetMousePosition();
#if defined(CONF_PLATFORM_IOS)
		CUIRect CloseButtonBar, CloseButton;
		Screen.HSplitTop(RowHeight, &CloseButtonBar, nullptr);
		CloseButtonBar.VSplitRight(10.0f, &CloseButtonBar, nullptr);
		CloseButtonBar.VSplitRight(RowHeight, &CloseButtonBar, &CloseButton);
		if(DoButton(CloseButton, FontIcon::XMARK, ButtonMousePosition, ButtonReleased))
			Toggle(m_ConsoleType);
#endif
		if(pConsole->m_MouseIsPress)
		{
			pConsole->m_MouseRelease = GetMousePosition();
		}
		const float ScaledLineHeight = LineHeight / ScreenSize.y;
		if(!SettingsOpen && absolute(m_TouchState.m_ScrollAmount.y) >= ScaledLineHeight)
		{
			if(m_TouchState.m_ScrollAmount.y > 0.0f)
			{
				pConsole->m_BacklogCurLine += pConsole->GetLinesToScroll(-1, 1);
				m_TouchState.m_ScrollAmount.y -= ScaledLineHeight;
			}
			else
			{
				--pConsole->m_BacklogCurLine;
				if(pConsole->m_BacklogCurLine < 0)
					pConsole->m_BacklogCurLine = 0;
				m_TouchState.m_ScrollAmount.y += ScaledLineHeight;
			}
		}

		x = PromptCursor.m_X;

		if(!SettingsOpen && m_ConsoleState == CONSOLE_OPEN && !pConsole->m_Selection.IsDragging() && !pConsole->m_ScrollbarDragging)
		{
			if(pConsole->m_MousePress.y >= pConsole->m_BoundingBox.m_Y && pConsole->m_MousePress.y < pConsole->m_BoundingBox.m_Y + pConsole->m_BoundingBox.m_H)
			{
				if(MousePressedThisFrame)
					pConsole->m_Selection.Clear();
				CLineInput::SMouseSelection *pMouseSelection = pConsole->m_Input.GetMouseSelection();
				if(pMouseSelection->m_Selecting && !pConsole->m_MouseIsPress && pConsole->m_Input.IsActive())
				{
					Input()->EnsureScreenKeyboardShown();
				}
				pMouseSelection->m_Selecting = pConsole->m_MouseIsPress;
				pMouseSelection->m_PressMouse = pConsole->m_MousePress;
				pMouseSelection->m_ReleaseMouse = pConsole->m_MouseRelease;
			}
			else if(pConsole->m_MouseIsPress)
			{
				pConsole->m_Input.SelectNothing();
			}
		}

		// render console input (wrap line)
		pConsole->m_Input.SetHidden(pConsole->IsInputHidden());
		if(!SettingsOpen && m_ConsoleState == CONSOLE_OPEN)
		{
			pConsole->m_Input.Activate(EInputPriority::CONSOLE); // Ensure that the input is active
		}
		const CUIRect InputCursorRect = {x, y + FontSize * 1.5f, 0.0f, 0.0f};
		const bool WasChanged = pConsole->m_Input.WasChanged();
		const bool WasCursorChanged = pConsole->m_Input.WasCursorChanged();
		const bool Changed = WasChanged || WasCursorChanged || FontChanged;
		std::vector<STextColorSplit> vInputColors;
		if(LocalConsole && !pConsole->m_SearchInput.IsSearching() && g_Config.m_QmConsoleHighlightCommands)
		{
			std::string DisplayText = pConsole->m_Input.GetString();
			if(pConsole->m_Input.IsActive() && Input()->HasComposition())
				DisplayText.insert(pConsole->m_Input.GetCursorOffset(), Input()->GetComposition());
			QmConsoleSyntax::AppendColors(DisplayText.c_str(), Palette, vInputColors);
		}
		pConsole->m_BoundingBox = pConsole->m_Input.Render(&InputCursorRect, FontSize, TEXTALIGN_BL, Changed, Screen.w - 10.0f - x, LINE_SPACING, vInputColors);
		if(pConsole->m_Input.HasSelection())
			pConsole->m_Selection.Clear();

		y -= pConsole->m_BoundingBox.m_H - FontSize;
		if(LocalConsole)
			QmConsoleUi::DrawPanel(Ui(), {0.0f, y - 2.0f, Screen.w, 1.0f}, Palette.m_MutedText.WithAlpha(0.35f));

		bool HandleLinkClick = false;
		if(LinkClickPending)
		{
			const bool InLogArea = LinkClickPress.y >= RowHeight && LinkClickPress.y < pConsole->m_BoundingBox.m_Y && LinkClickPress.x < Screen.w - CONSOLE_SCROLLBAR_WIDTH - CONSOLE_SCROLLBAR_MARGIN;
			const float DragDistance = length(LinkClickPos - LinkClickPress);
			HandleLinkClick = InLogArea && DragDistance <= LINK_CLICK_DRAG_THRESHOLD;
		}

		// render possible commands
		if(!pConsole->m_SearchInput.IsSearching() && (m_ConsoleType == CONSOLETYPE_LOCAL || Client()->RconAuthed()) && !pConsole->m_Input.IsEmpty())
		{
			pConsole->UpdateCompletionSuggestions();

			CCompletionOptionRenderInfo Info;
			Info.m_pSelf = this;
			Info.m_WantedCompletion = pConsole->m_CompletionChosen;
			Info.m_Offset = pConsole->m_CompletionRenderOffset;
			Info.m_pOffsetChange = &pConsole->m_CompletionRenderOffsetChange;
			Info.m_Width = Screen.w;
			Info.m_TotalWidth = 0.0f;
			char aCmd[IConsole::CMDLINE_LENGTH];
			pConsole->GetCommand(pConsole->m_aCompletionBuffer, aCmd);
			Info.m_pCurrentCmd = aCmd;

			Info.m_Cursor.SetPosition(vec2(InitialX - Info.m_Offset, InitialY + FontSize * 2.0f + 2.0f));
			Info.m_Cursor.m_FontSize = FontSize;

			for(size_t SuggestionId = 0; SuggestionId < pConsole->m_vpCommandSuggestions.size(); ++SuggestionId)
			{
				PossibleCommandsRenderCallback(SuggestionId, pConsole->m_vpCommandSuggestions[SuggestionId], &Info);
			}
			const int NumCommands = pConsole->m_vpCommandSuggestions.size();
			Info.m_TotalWidth = Info.m_Cursor.m_X + Info.m_Offset;
			pConsole->m_CompletionRenderOffset = Info.m_Offset;

			if(NumCommands <= 0 && pConsole->m_IsCommand)
			{
				int NumArguments = 0;
				if(!pConsole->m_vpArgumentSuggestions.empty())
				{
					Info.m_WantedCompletion = pConsole->m_CompletionChosenArgument;
					Info.m_TotalWidth = 0.0f;
					Info.m_pCurrentCmd = pConsole->m_aCompletionBufferArgument;

					for(size_t SuggestionId = 0; SuggestionId < pConsole->m_vpArgumentSuggestions.size(); ++SuggestionId)
					{
						PossibleCommandsRenderCallback(SuggestionId, pConsole->m_vpArgumentSuggestions[SuggestionId], &Info);
					}
					NumArguments = pConsole->m_vpArgumentSuggestions.size();
					Info.m_TotalWidth = Info.m_Cursor.m_X + Info.m_Offset;
					pConsole->m_CompletionRenderOffset = Info.m_Offset;
				}

				if(NumArguments <= 0 && pConsole->m_IsCommand)
				{
					char aBuf[1024];
					const char *pCommandHelp = pConsole->m_pCommandHelp != nullptr ? pConsole->m_pCommandHelp : "";
					const char *pCommandParams = pConsole->m_pCommandParams != nullptr ? pConsole->m_pCommandParams : "";
					char aLocalizedConfigHelp[1024];
					const char *pLocalizedHelp = nullptr;
					SConfigHelpLookup Lookup{pConsole->m_pCommandName};
					ConfigManager()->PossibleConfigVariables(pConsole->m_pCommandName, pConsole->m_CompletionFlagmask, FindConfigHelpVariable, &Lookup);
					if(BuildLocalizedConfigHelpText(Lookup.m_pVariable, aLocalizedConfigHelp, sizeof(aLocalizedConfigHelp)))
						pLocalizedHelp = aLocalizedConfigHelp;
					if(pLocalizedHelp == nullptr)
						pLocalizedHelp = Localize(pCommandHelp);
					const char *pLocalizedParams = Localize(pCommandParams);
					str_format(aBuf, sizeof(aBuf), Localize("Help: %s"), pLocalizedHelp);
					TextRender()->TextEx(&Info.m_Cursor, aBuf, -1);
					TextRender()->TextColor(0.75f, 0.75f, 0.75f, 1);
					str_format(aBuf, sizeof(aBuf), Localize("Usage: %s %s"), pConsole->m_pCommandName, pLocalizedParams);
					TextRender()->TextEx(&Info.m_Cursor, aBuf, -1);
				}
			}

			// Reset animation offset in case our chosen completion index changed due to new commands being added/removed
			if(pConsole->m_QueueResetAnimation)
			{
				pConsole->m_CompletionRenderOffset += pConsole->m_CompletionRenderOffsetChange;
				pConsole->m_CompletionRenderOffsetChange = 0.0f;
				pConsole->m_QueueResetAnimation = false;
			}
			Ui()->DoSmoothScrollLogic(&pConsole->m_CompletionRenderOffset, &pConsole->m_CompletionRenderOffsetChange, Info.m_Width, Info.m_TotalWidth);
		}
		else if(pConsole->m_SearchInput.IsSearching() && !pConsole->m_Input.IsEmpty())
		{ // Render current match and match count
			CTextCursor MatchInfoCursor;
			MatchInfoCursor.SetPosition(vec2(InitialX, InitialY + FontSize * 2.0f + 2.0f));
			MatchInfoCursor.m_FontSize = FontSize;
			TextRender()->TextColor(0.8f, 0.8f, 0.8f, 1.0f);
			if(!pConsole->m_vSearchMatches.empty())
			{
				char aBuf[64];
				str_format(aBuf, sizeof(aBuf), Localize("Match %d of %d"), pConsole->m_CurrentMatchIndex + 1, (int)pConsole->m_vSearchMatches.size());
				TextRender()->TextEx(&MatchInfoCursor, aBuf, -1);
			}
			else
			{
				TextRender()->TextEx(&MatchInfoCursor, Localize("No results"), -1);
			}
		}

		pConsole->PumpBacklogPending();
		if(pConsole->m_NewLineCounter != 0)
		{
			pConsole->UpdateSearch();

			// keep scroll position when new entries are printed.
			if(pConsole->m_BacklogCurLine != 0 || pConsole->m_Selection.HasSelection() || pConsole->m_Selection.IsDragging())
			{
				pConsole->m_BacklogCurLine += pConsole->m_NewLineCounter;
				pConsole->m_BacklogLastActiveLine += pConsole->m_NewLineCounter;
			}
			if(pConsole->m_NewLineCounter < 0)
				pConsole->m_NewLineCounter = 0;
		}

		const float LogTop = RowHeight;
		const float LogBottom = y;
		const float LogHeight = maximum(0.0f, LogBottom - LogTop);
		const int VisibleBacklogLines = maximum(1, (int)std::floor(LogHeight / LineHeight));
		const int TotalBacklogLines = pConsole->TotalBacklogLines();
		const int MaxScroll = maximum(0, TotalBacklogLines - VisibleBacklogLines);
		pConsole->m_BacklogCurLine = std::clamp(pConsole->m_BacklogCurLine, 0, MaxScroll);
		if(pConsole->m_BacklogLastActiveLine >= 0)
			pConsole->m_BacklogLastActiveLine = std::clamp(pConsole->m_BacklogLastActiveLine, 0, MaxScroll);
		const bool ShowConsoleScrollbar = MaxScroll > 0 && LogHeight > 0.0f;
		const float LogTextRightInset = CONSOLE_SCROLLBAR_WIDTH + CONSOLE_SCROLLBAR_MARGIN;
		const bool CanSelectLog = !SettingsOpen && !pConsole->m_ChatExportMode && m_ConsoleState == CONSOLE_OPEN;
		const CUIRect LogRect = {0.0f, LogTop, Screen.w - LogTextRightInset, LogHeight};
		HandleLinkClick = !SettingsOpen && HandleLinkClick && LogRect.Inside(LinkClickPress) && LogRect.Inside(LinkClickPos);
		ChatExportClickPending = !SettingsOpen && ChatExportClickPending && LogRect.Inside(pConsole->m_MousePress) && LogRect.Inside(ChatExportClickPos);
		if(CanSelectLog && MousePressedThisFrame && LogRect.Inside(pConsole->m_MousePress))
			pConsole->m_Selection.Begin(pConsole->SelectionPositionAt(pConsole->m_MousePress, LogBottom, LineHeight));
		if(CanSelectLog && pConsole->m_MouseIsPress)
		{
			const int ScrollLines = pConsole->m_Selection.AutoScroll(pConsole->m_MouseRelease.y, LogTop, LogBottom, LineHeight, Client()->RenderFrameTime());
			pConsole->m_BacklogCurLine = std::clamp(pConsole->m_BacklogCurLine + ScrollLines, 0, MaxScroll);
		}
		if(ShowConsoleScrollbar)
		{
			CUIRect ScrollbarRect = {Screen.w - CONSOLE_SCROLLBAR_WIDTH - CONSOLE_SCROLLBAR_MARGIN, LogTop, CONSOLE_SCROLLBAR_WIDTH, LogHeight};
			const float RailMargin = 5.0f;
			CUIRect Rail;
			ScrollbarRect.Margin(RailMargin, &Rail);
			if(Rail.w > 0.0f && Rail.h > Rail.w)
			{
				const float HandleHeight = std::clamp(Rail.h * (VisibleBacklogLines / (float)maximum(TotalBacklogLines, 1)), maximum(Rail.w, 14.0f), Rail.h);
				const float TrackRange = maximum(1.0f, Rail.h - HandleHeight);
				const float Current = 1.0f - (pConsole->m_BacklogCurLine / (float)maximum(MaxScroll, 1));
				CUIRect Handle = {Rail.x, Rail.y + TrackRange * Current, Rail.w, HandleHeight};
				const vec2 MousePos = GetMousePosition();
				const bool MouseDown = !SettingsOpen && (m_TouchState.m_PrimaryPressed || Input()->NativeMousePressed(1));
				const auto InsideRect = [&](const CUIRect &Rect) {
					return MousePos.x >= Rect.x && MousePos.x <= Rect.x + Rect.w && MousePos.y >= Rect.y && MousePos.y <= Rect.y + Rect.h;
				};

				if(!MouseDown)
				{
					pConsole->m_ScrollbarDragging = false;
				}
				else if(!pConsole->m_ScrollbarDragging && MousePressedThisFrame && InsideRect(Rail))
				{
					pConsole->m_ScrollbarDragOffset = InsideRect(Handle) ? std::clamp(MousePos.y - Handle.y, 0.0f, Handle.h) : Handle.h * 0.5f;
					pConsole->m_ScrollbarDragging = true;
				}

				if(pConsole->m_ScrollbarDragging)
				{
					const float HandleTop = std::clamp(MousePos.y - pConsole->m_ScrollbarDragOffset, Rail.y, Rail.y + TrackRange);
					const float Relative = (HandleTop - Rail.y) / TrackRange;
					const int NewLine = std::clamp((int)std::round((1.0f - Relative) * MaxScroll), 0, MaxScroll);
					if(NewLine != pConsole->m_BacklogCurLine)
					{
						pConsole->m_BacklogCurLine = NewLine;
						pConsole->m_BacklogLastActiveLine = NewLine;
					}
					Handle.y = Rail.y + TrackRange * (1.0f - (pConsole->m_BacklogCurLine / (float)maximum(MaxScroll, 1)));
				}

				Rail.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.22f), IGraphics::CORNER_ALL, Rail.w * 0.5f);
				const ColorRGBA HandleColor = pConsole->m_ScrollbarDragging ? ColorRGBA(0.85f, 0.85f, 0.85f, 0.95f) : ColorRGBA(0.62f, 0.62f, 0.62f, 0.82f);
				Handle.Draw(HandleColor, IGraphics::CORNER_ALL, Handle.w * 0.5f);
			}
		}
		else
		{
			pConsole->m_ScrollbarDragging = false;
		}
		if(pConsole->m_Selection.IsDragging())
		{
			if(CanSelectLog && LogHeight > 0.0f)
			{
				vec2 SelectionMouse = pConsole->m_MouseRelease;
				SelectionMouse.y = std::clamp(SelectionMouse.y, LogTop, LogBottom);
				pConsole->m_Selection.Extend(pConsole->SelectionPositionAt(SelectionMouse, LogBottom, LineHeight));
			}
			if(!CanSelectLog || !pConsole->m_MouseIsPress)
				pConsole->m_Selection.Finish();
		}

		// render console log (current entry, status, wrap lines)
		CInstance::CBacklogEntry *pEntry = pConsole->m_Backlog.Last();
		float OffsetY = 0.0f;

		std::vector<QmConsoleText::SRange> vLinkRanges;
		std::vector<STextColorSplit> vColorLayers;

		pConsole->m_BacklogLastActiveLine = pConsole->m_BacklogCurLine;

		int LineNum = -1;
		pConsole->m_LinesRendered = minimum(VisibleBacklogLines, TotalBacklogLines - pConsole->m_BacklogCurLine);

		int SkippedLines = 0;
		bool First = true;

		const float XScale = Graphics()->ScreenWidth() / Screen.w;
		const float YScale = Graphics()->ScreenHeight() / Screen.h;
		const float CalcOffsetY = LineHeight * std::floor((y - RowHeight) / LineHeight);
		const float ClipStartY = (y - CalcOffsetY) * YScale;
		HandleLinkClick = !SettingsOpen && HandleLinkClick && LinkClickPress.y >= y - CalcOffsetY && LinkClickPos.y >= y - CalcOffsetY;
		Graphics()->ClipEnable(0, ClipStartY, Screen.w * XScale, (y + 2.0f) * YScale - ClipStartY);

		while(pEntry)
		{
			if(pEntry->m_LineCount == -1)
				pConsole->UpdateEntryTextAttributes(pEntry);

			if(!pConsole->MatchesLogFilter(pEntry))
			{
				pEntry = pConsole->m_Backlog.Prev(pEntry);
				continue;
			}

			LineNum += pEntry->m_LineCount;
			if(LineNum < pConsole->m_BacklogLastActiveLine)
			{
				SkippedLines += pEntry->m_LineCount;
				pEntry = pConsole->m_Backlog.Prev(pEntry);
				continue;
			}
			const ColorRGBA PrintColor = LocalConsole && pEntry->m_PrintColor == gs_ConsoleDefaultColor ? Palette.m_aColors[QmConsoleAppearance::TEXT] : pEntry->m_PrintColor;
			TextRender()->TextColor(PrintColor);

			if(First)
			{
				OffsetY -= (pConsole->m_BacklogLastActiveLine - SkippedLines) * LineHeight;
			}

			const float LocalOffsetY = OffsetY + pEntry->m_YOffset / (float)pEntry->m_LineCount;
			OffsetY += pEntry->m_YOffset;
			const float EntryTop = y - OffsetY;
			const float EntryBottom = EntryTop + pEntry->m_YOffset;

			// stop rendering when lines reach the top
			const bool Outside = y - OffsetY <= RowHeight;
			const bool CanRenderOneLine = y - LocalOffsetY > RowHeight;
			if(Outside && !CanRenderOneLine)
				break;

			const bool ChatExportable = pConsole->IsChatExportableEntry(pEntry);
			if(pConsole->m_ChatExportMode && ChatExportable)
			{
				CUIRect EntryRect = {0.0f, EntryTop, Screen.w, EntryBottom - EntryTop};
				if(pEntry->m_ExportSelected)
					EntryRect.Draw(ColorRGBA(0.20f, 0.48f, 1.0f, 0.18f), IGraphics::CORNER_NONE, 0.0f);
				CUIRect CheckBox = {5.0f, EntryTop + maximum(2.0f, (EntryBottom - EntryTop - 11.0f) / 2.0f), 11.0f, 11.0f};
				CheckBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, 2.0f);
				CheckBox.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.65f));
				if(pEntry->m_ExportSelected)
				{
					CUIRect Inner = {CheckBox.x + 3.0f, CheckBox.y + 3.0f, CheckBox.w - 6.0f, CheckBox.h - 6.0f};
					Inner.Draw(ColorRGBA(0.35f, 0.65f, 1.0f, 0.95f), IGraphics::CORNER_ALL, 1.0f);
				}
				if(ChatExportClickPending && ChatExportClickPos.y >= EntryTop && ChatExportClickPos.y <= EntryBottom)
				{
					pConsole->ToggleChatExportEntry(pEntry, GameClient()->Input()->ShiftIsPressed());
					ChatExportClickPending = false;
				}
			}

			const float EntryTextX = pConsole->m_ChatExportMode ? 22.0f : 0.0f;
			const float EntryLineWidth = pConsole->BacklogLineWidth();
			CTextCursor EntryCursor;
			EntryCursor.SetPosition(vec2(EntryTextX, y - OffsetY));
			EntryCursor.m_FontSize = FontSize;
			EntryCursor.m_LineWidth = EntryLineWidth;
			EntryCursor.m_MaxLines = pEntry->m_LineCount;
			EntryCursor.m_LineSpacing = LINE_SPACING;
			const bool ParseColors = pConsole->ParseEntryColors(pEntry);

			CColoredParts ColoredParts(pEntry->m_aText, ParseColors);
			ColoredParts.AddSplitsToCursor(EntryCursor);
			if(!ColoredParts.Colors().empty() && ColoredParts.Colors()[0].m_Index == str_length("xxxx-xx-xx xx:xx:xx x chat/client: — "))
			{
				EntryCursor.m_vColorSplits[0].m_CharIndex -= str_length("— ");
				EntryCursor.m_vColorSplits[0].m_Length += str_length("— ");
			}
			const char *pText = ColoredParts.Text();
			const int TextLength = str_length(pText);
			vColorLayers.clear();
			vColorLayers.emplace_back(0, TextLength, PrintColor);
			if(LocalConsole && pEntry->m_CommandEcho && g_Config.m_QmConsoleHighlightCommands)
				QmConsoleSyntax::AppendColors(pText + 2, Palette, vColorLayers, 2);
			vColorLayers.insert(vColorLayers.end(), EntryCursor.m_vColorSplits.begin(), EntryCursor.m_vColorSplits.end());
			const auto StoredColorSpans = pConsole->m_ColorSpansByExportId.find(pEntry->m_ExportId);
			if(StoredColorSpans != pConsole->m_ColorSpansByExportId.end())
			{
				for(const SColorSpan &Span : StoredColorSpans->second)
					vColorLayers.push_back(QmConsoleText::ColorSplitForCharacters(pText, Span.m_CharIndex, Span.m_Length, Span.m_Color));
			}
			if(pConsole->m_Selection.ContainsEntry(pEntry->m_ExportId))
			{
				const int Characters = static_cast<int>(str_utf8_offset_bytes_to_chars(pText, str_length(pText)));
				const auto Range = pConsole->m_Selection.RangeForEntry(pEntry->m_ExportId, Characters);
				EntryCursor.m_CalculateSelectionMode = TEXT_CURSOR_SELECTION_MODE_SET;
				EntryCursor.m_SelectionStart = Range->m_Start;
				EntryCursor.m_SelectionEnd = Range->m_End;
			}
			QmConsoleText::CollectLinks(pText, vLinkRanges);
			for(const auto &Range : vLinkRanges)
			{
				vColorLayers.emplace_back(Range.m_StartByte, Range.m_EndByte - Range.m_StartByte, LocalConsole ? Palette.m_aColors[QmConsoleAppearance::LINK] : LINK_TEXT_COLOR);
				CTextCursor LinkCursor;
				LinkCursor.SetPosition(vec2(EntryTextX, EntryTop));
				LinkCursor.m_FontSize = FontSize;
				LinkCursor.m_LineWidth = EntryLineWidth;
				LinkCursor.m_MaxLines = pEntry->m_LineCount;
				LinkCursor.m_LineSpacing = LINE_SPACING;
				LinkCursor.m_Flags = 0;
				LinkCursor.m_CalculateSelectionMode = TEXT_CURSOR_SELECTION_MODE_SET;
				LinkCursor.m_RenderSelection = false;
				LinkCursor.m_SelectionStart = Range.m_StartChar;
				LinkCursor.m_SelectionEnd = Range.m_EndChar;
				TextRender()->TextEx(&LinkCursor, pText);
				// 使用实际换行后的链接矩形，空白处不会被最近字符的光标吸附误判。
				if(HandleLinkClick && QmConsoleText::ContainsPoint(LinkCursor.m_vSelectionQuads, LinkClickPress) &&
					QmConsoleText::ContainsPoint(LinkCursor.m_vSelectionQuads, LinkClickPos))
				{
					HandleLinkClick = false;
					const std::string Url = QmConsoleText::LinkUrl(pText, Range);
					bool Opened;
#if defined(CONF_PLATFORM_ANDROID)
					Opened = Client()->ViewLink(Url.c_str());
#else
					Opened = str_startswith(Url.c_str(), "http://") ? open_link(Url.c_str()) != 0 : Client()->ViewLink(Url.c_str());
#endif
					if(Opened)
						pConsole->m_Selection.Clear();
				}
				for(const auto &Quad : LinkCursor.m_vSelectionQuads)
				{
					const float Height = Quad.m_Height * LINK_UNDERLINE_HEIGHT;
					const CUIRect Underline = {Quad.m_X, Quad.m_Y + Quad.m_Height - Height, Quad.m_Width, Height};
					QmConsoleUi::DrawPanel(Ui(), Underline, LocalConsole ? Palette.m_aColors[QmConsoleAppearance::LINK].WithAlpha(0.9f) : LINK_UNDERLINE_COLOR);
				}
			}
			// 搜索最后合入颜色层，链接仍保留下划线，但不会盖住搜索命中颜色。
			if(pConsole->m_SearchInput.IsSearching() && pConsole->m_CurrentMatchIndex >= 0)
			{
				const auto &Selected = pConsole->m_vSearchMatches[pConsole->m_CurrentMatchIndex];
				for(const auto &Match : pConsole->m_vSearchMatches)
				{
					if(Match.m_EntryLine != LineNum + 1 - pEntry->m_LineCount)
						continue;
					const bool IsSelected = Selected.m_EntryLine == Match.m_EntryLine && Selected.m_Pos == Match.m_Pos;
					vColorLayers.emplace_back(Match.m_Pos, Match.m_Length, LocalConsole ? Palette.m_aColors[IsSelected ? QmConsoleAppearance::SEARCH_SELECTED : QmConsoleAppearance::SEARCH] : (IsSelected ? ms_SearchSelectedColor : ms_SearchHighlightColor));
				}
			}
			QmConsoleText::ComposeColorSplits(pText, vColorLayers, EntryCursor.m_vColorSplits);
			TextRender()->TextEx(&EntryCursor, pText);
			pEntry = pConsole->m_Backlog.Prev(pEntry);

			// reset color
			TextRender()->TextColor(LocalConsole ? Palette.m_aColors[QmConsoleAppearance::TEXT] : TextRender()->DefaultTextColor());
			First = false;

			if(!pEntry)
				break;
		}

		// 本帧新增日志的滚动补偿已在绘制前统一应用。
		pConsole->m_NewLineCounter = 0;

		Graphics()->ClipDisable();

		pConsole->m_BacklogLastActiveLine = pConsole->m_BacklogCurLine;

		if(m_WantsSelectionCopy)
		{
			const std::string SelectionString = pConsole->SelectionText();
			if(!SelectionString.empty())
				Input()->SetClipboardText(SelectionString.c_str());
			m_WantsSelectionCopy = false;
		}

		TextRender()->TextColor(LocalConsole ? Palette.m_aColors[QmConsoleAppearance::TEXT] : TextRender()->DefaultTextColor());

		char aLinesBuf[128];
		const int LineStart = pConsole->m_LinesRendered > 0 ? pConsole->m_BacklogCurLine + 1 : 0;
		const int LineEnd = pConsole->m_LinesRendered > 0 ? pConsole->m_BacklogCurLine + pConsole->m_LinesRendered : 0;
		str_format(aLinesBuf, sizeof(aLinesBuf), Localize("Lines %d - %d (%s)"), LineStart, LineEnd,
			pConsole->m_BacklogCurLine != 0 ? Localize("Locked") : Localize("Following"));
		const auto ToolbarButton = [&](const CUIRect &Rect, const char *pLabel, float Scale, bool Selected = false, bool CategoryButton = false) {
			return QmConsoleUi::Button(Ui(), Rect, pLabel, maximum(7.0f, FontSize * Scale), Selected,
				m_ButtonPressPosition, ButtonMousePosition, pConsole->m_MouseIsPress, ButtonReleased, !SettingsOpen && m_ConsoleState == CONSOLE_OPEN,
				CategoryButton ? FilterIndicatorWidth * Scale : 0.0f, LocalConsole ? &Palette : nullptr);
		};
		const float LinesWidth = LocalConsole ? 0.0f : TextRender()->TextWidth(FontSize, aLinesBuf);
		float FilterX = LocalConsole ? 10.0f : LinesWidth + 20.0f;
		const float FilterScale = LocalConsole ? Toolbar.m_FilterScale : minimum(1.0f, maximum(0.0f, (Screen.w - ToolbarRightMargin - FilterX - 10.0f) / TotalFilterWidth));
		if(!pConsole->m_ChatExportMode)
		{
			for(int i = 0; i < CInstance::LOG_FILTER_BUTTON_COUNT; ++i)
			{
				const int Category = CInstance::LogFilterCategoryForButton(i);
				const CUIRect Button = {FilterX, 3.0f, aFilterWidths[i] * FilterScale, LocalConsole ? FontSize + 10.0f : 14.0f};
				if(ToolbarButton(Button, apFilterLabels[i], FilterScale, QmConsoleLogFilterButtonActive(pConsole->m_LogFilterMask, Category),
					   LocalConsole && Category != QM_CONSOLE_LOG_CATEGORY_ALL))
					pConsole->SetLogFilterMask(QmToggleConsoleLogFilterCategory(pConsole->m_LogFilterMask, Category));
				FilterX += (aFilterWidths[i] + FilterSpacing) * FilterScale;
			}
		}
		if(LocalConsole)
		{
			float ActionX = Toolbar.m_ActionX;
			for(int i = 0; i < NumActions; ++i)
			{
				const auto &Action = aActions[i];
				const CUIRect Button = {ActionX, Toolbar.m_SplitRows ? FontSize + 19.0f : 3.0f, Action.m_Width * Toolbar.m_ActionScale, FontSize + 10.0f};
				if(ToolbarButton(Button, Action.m_pLabel, Toolbar.m_ActionScale, Action.m_Selected))
				{
					switch(Action.m_Action)
					{
					case EToolbarAction::SEARCH: pConsole->SetSearching(!pConsole->m_SearchInput.IsSearching()); break;
					case EToolbarAction::FOLLOW:
						pConsole->m_BacklogCurLine = 0;
						pConsole->m_Selection.Clear();
						break;
					case EToolbarAction::EXPORT: pConsole->SetChatExportMode(true); break;
					case EToolbarAction::CANCEL_EXPORT: pConsole->SetChatExportMode(false); break;
					case EToolbarAction::SAVE_EXPORT: pConsole->ExportSelectedChat(); break;
					case EToolbarAction::CLEAR_EXPORT: pConsole->ClearChatExportSelection(); break;
					case EToolbarAction::SELECT_CHAT: pConsole->SelectAllChatExportable(); break;
					case EToolbarAction::SETTINGS: OpenSettings(); break;
					case EToolbarAction::EXPAND: m_LocalConsoleFullscreen = !m_LocalConsoleFullscreen; break;
					}
				}
				ActionX += (Action.m_Width + FilterSpacing) * Toolbar.m_ActionScale;
			}
			char aStatus[256];
			str_copy(aStatus, aLinesBuf);
			if(pConsole->m_ChatExportMode)
			{
				char aExportStatus[128];
				if(pConsole->m_pChatExportJob)
				{
					const auto &Job = *pConsole->m_pChatExportJob;
					if(Job.m_Queued)
						str_format(aExportStatus, sizeof(aExportStatus), Localize("Exporting chat images: %d"), Job.m_CompletedPages.load());
					else
						str_format(aExportStatus, sizeof(aExportStatus), Localize("Preparing chat export: %d%%"), Job.PreparationPercent());
				}
				else
					str_format(aExportStatus, sizeof(aExportStatus), Localize("Selected %d"), pConsole->SelectedChatExportCount());
				str_append(aStatus, "  |  ");
				str_append(aStatus, aExportStatus);
			}
			const char *pHints = pConsole->m_ChatExportMode ? "Shift+Mouse1  |  Esc" :
				(pConsole->m_SearchInput.IsSearching() ? "Enter / Shift+Enter  |  Esc  |  Ctrl+Mouse1" : "Ctrl+F  |  Alt+1-5  |  PgUp/PgDn  |  End  |  Ctrl+Mouse1");
			const float HintWidth = TextRender()->TextWidth(FontSize * 0.8f, pHints);
			const float StatusWidth = TextRender()->TextWidth(FontSize * 0.8f, aStatus);
			const float HintX = Screen.w - HintWidth - 10.0f;
			const bool ShowHints = StatusWidth + HintWidth + 30.0f <= Screen.w;
			const CUIRect StatusRect = {10.0f, ConsoleHeight - FooterHeight, ShowHints ? HintX - 20.0f : maximum(0.0f, Screen.w - 20.0f), FooterHeight};
			SLabelProperties Props;
			Props.m_MaxWidth = StatusRect.w;
			Props.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&StatusRect, aStatus, FontSize * 0.8f, TEXTALIGN_ML, Props);
			if(ShowHints)
			{
				const CUIRect HintRect = {HintX, ConsoleHeight - FooterHeight, HintWidth, FooterHeight};
				Ui()->DoLabel(&HintRect, pHints, FontSize * 0.8f, TEXTALIGN_ML);
			}
		}
		else
		{
			TextRender()->Text(10.0f, FontSize / 2.0f, FontSize, aLinesBuf);
			if(Client()->ReceivingRconCommands() || Client()->ReceivingMaplist())
			{
				const float Percentage = Client()->ReceivingRconCommands() ? Client()->GotRconCommandsPercentage() : Client()->GotMaplistPercentage();
				SProgressSpinnerProperties ProgressProps;
				ProgressProps.m_Progress = Percentage;
				Ui()->RenderProgressSpinner(vec2(Screen.w / 4.0f + FontSize / 2.f, FontSize), FontSize / 2.f, ProgressProps);
				char aLoading[128];
				str_copy(aLoading, Client()->ReceivingRconCommands() ? Localize("Loading commands…") : Localize("Loading maps…"));
				if(Percentage > 0)
				{
					char aPercentage[8];
					str_format(aPercentage, sizeof(aPercentage), " %d%%", (int)(Percentage * 100));
					str_append(aLoading, aPercentage);
				}
				TextRender()->Text(Screen.w / 4.0f + FontSize + 2.0f, FontSize / 2.f, FontSize, aLoading);
			}
			char aVersion[128];
			str_copy(aVersion, "v" GAME_VERSION " on " CONF_PLATFORM_STRING " " CONF_ARCH_STRING);
			const float VersionWidth = TextRender()->TextWidth(FontSize, aVersion);
			if(FilterX + VersionWidth + 10.0f <= Screen.w - ToolbarRightMargin)
			{
				TextRender()->Text(Screen.w - ToolbarRightMargin - VersionWidth - 10.0f, FontSize / 2.0f, FontSize, aVersion);
				const char *pClientVersion = CLIENT_NAME " " CLIENT_RELEASE_VERSION;
				TextRender()->Text(Screen.w - ToolbarRightMargin - TextRender()->TextWidth(FontSize, pClientVersion) - 10.0f, FontSize * 2.0f, FontSize, pClientVersion);
			}
		}
	}

	if(ConsoleSettingsOpen())
	{
		QmConsoleUi::DrawPanel(Ui(), Screen, ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f));
		Ui()->RenderPopupMenus();
		if(!ConsoleSettingsOpen() && m_ConsoleState == CONSOLE_OPEN)
			pConsole->m_Input.Activate(EInputPriority::CONSOLE);
	}
	RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);

	if(UpdateConsoleUi)
	{
		Ui()->FinishCheck();
		Ui()->SetEnabled(false);
	}

	TextRender()->SetRenderFlags(PreviousRenderFlags);
	TextRender()->SetFontPreset(PreviousFontPreset);
	TextRender()->TextOutlineColor(PreviousTextOutlineColor);
	TextRender()->TextSelectionColor(PreviousTextSelectionColor);
	TextRender()->TextColor(PreviousTextColor);
}

void CGameConsole::OnMessage(int MsgType, void *pRawMsg)
{
}

bool CGameConsole::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!IsActive())
		return false;

	// 控制台沿用客户端光标，避免释放鼠标捕获后出现系统光标。
	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);
	return true;
}

bool CGameConsole::OnInput(const IInput::CEvent &Event)
{
	// accept input when opening, but not at first frame to discard the input that caused the console to open
	if(m_ConsoleState != CONSOLE_OPEN && (m_ConsoleState != CONSOLE_OPENING || m_StateChangeEnd == Client()->GlobalTime() + m_StateChangeDuration))
		return false;
	if((Event.m_Key >= KEY_F1 && Event.m_Key <= KEY_F12) || (Event.m_Key >= KEY_F13 && Event.m_Key <= KEY_F24))
		return false;

	if(ConsoleSettingsOpen())
	{
		const bool PreviouslyEnabled = Ui()->Enabled();
		Ui()->SetEnabled(true);
		Ui()->OnInput(Event);
		Ui()->SetEnabled(PreviouslyEnabled);
		return true;
	}

	if(Event.m_Key == KEY_ESCAPE && (Event.m_Flags & IInput::FLAG_PRESS) && CurrentConsole()->m_ChatExportMode)
	{
		CurrentConsole()->SetChatExportMode(false);
	}
	else if(Event.m_Key == KEY_ESCAPE && (Event.m_Flags & IInput::FLAG_PRESS) && !CurrentConsole()->m_SearchInput.IsSearching())
	{
		Toggle(m_ConsoleType);
	}
	else if(!CurrentConsole()->OnInput(Event))
	{
		if(GameClient()->Input()->ModifierIsPressed() && Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_C)
			m_WantsSelectionCopy = CurrentConsole()->m_Selection.HasSelection();
	}

	return true;
}

void CGameConsole::Toggle(int Type)
{
	Ui()->ClosePopupMenu(&m_SettingsPopupId, true);
	CurrentConsole()->m_Selection.Finish();
	CurrentConsole()->m_ScrollbarDragging = false;
	CurrentConsole()->m_MouseIsPress = false;
	CurrentConsole()->m_Input.GetMouseSelection()->m_Selecting = false;
	m_WantsSelectionCopy = false;
	if(m_ConsoleType != Type && (m_ConsoleState == CONSOLE_OPEN || m_ConsoleState == CONSOLE_OPENING))
	{
		// don't toggle console, just switch what console to use
	}
	else
	{
		if(m_ConsoleState == CONSOLE_CLOSED || m_ConsoleState == CONSOLE_OPEN)
		{
			m_StateChangeEnd = Client()->GlobalTime() + m_StateChangeDuration;
		}
		else
		{
			float Progress = m_StateChangeEnd - Client()->GlobalTime();
			float ReversedProgress = m_StateChangeDuration - Progress;

			m_StateChangeEnd = Client()->GlobalTime() + ReversedProgress;
		}

		if(m_ConsoleState == CONSOLE_CLOSED || m_ConsoleState == CONSOLE_CLOSING)
		{
			Ui()->SetEnabled(false);
			m_ConsoleState = CONSOLE_OPENING;
		}
		else
		{
			ConsoleForType(Type)->m_Input.Deactivate();
			Ui()->SetEnabled(true);
			GameClient()->OnRelease();
			m_ConsoleState = CONSOLE_CLOSING;
		}
	}
	m_ConsoleType = Type;
}

void CGameConsole::ConToggleLocalConsole(IConsole::IResult *pResult, void *pUserData)
{
	((CGameConsole *)pUserData)->Toggle(CONSOLETYPE_LOCAL);
}

void CGameConsole::ConToggleRemoteConsole(IConsole::IResult *pResult, void *pUserData)
{
	((CGameConsole *)pUserData)->Toggle(CONSOLETYPE_REMOTE);
}

void CGameConsole::ConClearLocalConsole(IConsole::IResult *pResult, void *pUserData)
{
	((CGameConsole *)pUserData)->m_LocalConsole.ClearBacklog();
}

void CGameConsole::ConClearRemoteConsole(IConsole::IResult *pResult, void *pUserData)
{
	((CGameConsole *)pUserData)->m_RemoteConsole.ClearBacklog();
}

void CGameConsole::ConDumpLocalConsole(IConsole::IResult *pResult, void *pUserData)
{
	((CGameConsole *)pUserData)->m_LocalConsole.Dump();
}

void CGameConsole::ConDumpRemoteConsole(IConsole::IResult *pResult, void *pUserData)
{
	((CGameConsole *)pUserData)->m_RemoteConsole.Dump();
}

void CGameConsole::ConConsolePageUp(IConsole::IResult *pResult, void *pUserData)
{
	CInstance *pConsole = ((CGameConsole *)pUserData)->CurrentConsole();
	pConsole->m_BacklogCurLine += pConsole->GetLinesToScroll(-1, pConsole->m_LinesRendered);
}

void CGameConsole::ConConsolePageDown(IConsole::IResult *pResult, void *pUserData)
{
	CInstance *pConsole = ((CGameConsole *)pUserData)->CurrentConsole();
	pConsole->m_BacklogCurLine -= pConsole->GetLinesToScroll(1, pConsole->m_LinesRendered);
	if(pConsole->m_BacklogCurLine < 0)
		pConsole->m_BacklogCurLine = 0;
}

void CGameConsole::ConConsolePageTop(IConsole::IResult *pResult, void *pUserData)
{
	CInstance *pConsole = ((CGameConsole *)pUserData)->CurrentConsole();
	pConsole->m_BacklogCurLine += pConsole->GetLinesToScroll(-1, pConsole->m_LinesRendered);
}

void CGameConsole::ConConsolePageBottom(IConsole::IResult *pResult, void *pUserData)
{
	CInstance *pConsole = ((CGameConsole *)pUserData)->CurrentConsole();
	pConsole->m_BacklogCurLine = 0;
}

void CGameConsole::ConchainConsoleOutputLevel(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CGameConsole *pSelf = (CGameConsole *)pUserData;
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		pSelf->m_pConsoleLogger->SetFilter(CLogFilter{IConsole::ToLogLevelFilter(g_Config.m_ConsoleOutputLevel)});
	}
}

void CGameConsole::RequireUsername(bool UsernameReq)
{
	if((m_RemoteConsole.m_UsernameReq = UsernameReq))
	{
		m_RemoteConsole.m_aUser[0] = '\0';
		m_RemoteConsole.m_UserGot = false;
	}
}

void CGameConsole::PrintLine(int Type, const char *pLine)
{
	if(Type == CONSOLETYPE_LOCAL)
		m_LocalConsole.PrintLine(pLine, str_length(pLine), gs_ConsoleDefaultColor);
	else if(Type == CONSOLETYPE_REMOTE)
		m_RemoteConsole.PrintLine(pLine, str_length(pLine), TextRender()->DefaultTextColor());
}

void CGameConsole::PrintLineWithColorSpans(int Level, const char *pFrom, const char *pLine, ColorRGBA PrintColor, const SColorSpan *pColorSpans, size_t NumColorSpans, std::shared_ptr<const QmChatExport::SMetadata> pChatMetadata)
{
	m_pConsoleLogger->SetPendingColorSpans(pFrom, pLine, pColorSpans, NumColorSpans, std::move(pChatMetadata));
	Console()->Print(Level, pFrom, pLine, PrintColor);
	m_pConsoleLogger->ClearPendingColorSpans();
}

void CGameConsole::OnConsoleInit()
{
	// init console instances
	m_LocalConsole.Init(this);
	m_RemoteConsole.Init(this);

	// 本地控制台的分类选择跨启动保留（远程控制台保持独立，不受该配置影响）
	m_LocalConsole.m_LogFilterMask = QmNormalizeConsoleLogFilterMask(g_Config.m_QmConsoleFilterMask);

	m_pConsole = Kernel()->RequestInterface<IConsole>();

	// TClient
	Console()->Register("clear", "", CFGFLAG_CLIENT, ConClearLocalConsole, this, "Clear local console");

	Console()->Register("toggle_local_console", "", CFGFLAG_CLIENT, ConToggleLocalConsole, this, "Toggle local console");
	Console()->Register("toggle_remote_console", "", CFGFLAG_CLIENT, ConToggleRemoteConsole, this, "Toggle remote console");
	Console()->Register("clear_local_console", "", CFGFLAG_CLIENT, ConClearLocalConsole, this, "Clear local console");
	Console()->Register("clear_remote_console", "", CFGFLAG_CLIENT, ConClearRemoteConsole, this, "Clear remote console");
	Console()->Register("dump_local_console", "", CFGFLAG_CLIENT, ConDumpLocalConsole, this, "Write local console contents to a text file");
	Console()->Register("dump_remote_console", "", CFGFLAG_CLIENT, ConDumpRemoteConsole, this, "Write remote console contents to a text file");

	Console()->Register("console_page_up", "", CFGFLAG_CLIENT, ConConsolePageUp, this, "Previous page in console");
	Console()->Register("console_page_down", "", CFGFLAG_CLIENT, ConConsolePageDown, this, "Next page in console");
	Console()->Register("console_page_top", "", CFGFLAG_CLIENT, ConConsolePageTop, this, "Last page in console");
	Console()->Register("console_page_bottom", "", CFGFLAG_CLIENT, ConConsolePageBottom, this, "First page in console");
	Console()->Chain("console_output_level", ConchainConsoleOutputLevel, this);
}

void CGameConsole::OnInit()
{
	Engine()->SetAdditionalLogger(std::unique_ptr<ILogger>(m_pConsoleLogger));
	// add resize event
	Graphics()->AddWindowResizeListener([this]() {
		m_LocalConsole.UpdateBacklogTextAttributes();
		m_RemoteConsole.UpdateBacklogTextAttributes();
	});
}

void CGameConsole::OnStateChange(int NewState, int OldState)
{
	if(OldState <= IClient::STATE_ONLINE && NewState == IClient::STATE_OFFLINE)
	{
		m_RemoteConsole.m_UserGot = false;
		m_RemoteConsole.m_aUser[0] = '\0';
		m_RemoteConsole.m_Input.Clear();
		m_RemoteConsole.m_UsernameReq = false;
	}
}
