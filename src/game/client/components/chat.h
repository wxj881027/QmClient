/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_CHAT_H
#define GAME_CLIENT_COMPONENTS_CHAT_H
#include <base/system.h>

#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/shared/ringbuffer.h>

#include <generated/protocol7.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/chat_emoji.h>
#include <game/client/components/qmclient/hud_notifications/hud_notifications.h>
#include <game/client/lineinput.h>
#include <game/client/render.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class CTranslateResponse
{
public:
	bool m_Error = false;
	char m_Text[1024] = "";
	char m_Language[16] = "";
};

constexpr auto SAVES_FILE = "ddnet-saves.txt";

class CChat : public CComponent
{
public:
	enum class EBlockWordsAction
	{
		REPLACE = 0,
		HIDE_MESSAGE = 1,
	};

	enum class EPresentationState
	{
		ENTERING,
		VISIBLE,
		EXITING,
		COLLAPSED,
	};

	struct SPresentationState
	{
		int64_t m_PresentationBirthTick = 0;
		float m_EntryProgress = 0.0f;
		float m_LayoutVisibility = 0.0f;
		float m_RenderY = 0.0f;
		float m_TargetY = 0.0f;
		float m_RenderOffsetX = 0.0f;
		float m_RenderOffsetY = 0.0f;
		float m_RenderAlpha = 0.0f;
		float m_RenderScale = 1.0f;
		bool m_RenderYInitialized = false;
		bool m_AnimationsSuppressed = false;
		EPresentationState m_State = EPresentationState::COLLAPSED;
	};

	static void ResetPresentationState(SPresentationState &Presentation);
	static void BeginLinePresentation(SPresentationState &Presentation, int64_t Now, bool GentleRefresh);
	static void UpdateLinePresentation(SPresentationState &Presentation, int64_t LineTime, int64_t Now, float DeltaSeconds, bool ShowLargeArea, bool ForceVisible, int64_t LargeAreaOpenTick, float RecallDelaySeconds, bool ExtraAnimations = true);
	static float SmoothPresentationY(float CurrentY, float TargetY, float DeltaSeconds);
	static bool CanMergePlayerMessages(int PreviousClientId, int PreviousTeam, const char *pPreviousText, int64_t PreviousTime, int ClientId, int Team, const char *pText, int64_t Now);

private:
	static constexpr float CHAT_HEIGHT_FULL = 200.0f;
	static constexpr float CHAT_HEIGHT_MIN = 50.0f;
	static constexpr float CHAT_FONTSIZE_WIDTH_RATIO = 2.5f;

	static constexpr float CHAT_ENTRY_DURATION = 0.22f;
	static constexpr float CHAT_ENTRY_OFFSET_X = -24.0f;
	static constexpr float CHAT_RECALL_DURATION = 0.12f;
	static constexpr float CHAT_RECALL_STAGGER_SECONDS = 0.022f;
	static constexpr float CHAT_EXIT_DURATION = 0.20f;
	static constexpr float CHAT_MESSAGE_VISIBLE_DURATION = 14.0f;
	static constexpr float CHAT_PRESENTATION_MAX_DELTA_SECONDS = 0.20f;
	static constexpr float CHAT_RENDER_Y_SMOOTHING = 24.0f;

	enum
	{
		MAX_LINES = 64,
		MAX_LINE_LENGTH = 256
	};

	CLineInputBuffered<MAX_LINE_LENGTH> m_Input;
	struct SMergedAuthor
	{
		int m_ClientId = -1;
		char m_aName[MAX_NAME_LENGTH] = "";
		char m_aPlayerName[MAX_NAME_LENGTH] = "";
		char m_aQmTitle[64] = "";
		ColorRGBA m_NameColor;
	};

	class CLine
	{
	public:
		CLine();
		void Reset(CChat &This);

		bool m_Initialized;
		int64_t m_Time;
		float m_aYOffset[2];
		int m_ClientId;
		int m_TeamNumber;
		bool m_Team;
		bool m_Whisper;
		int m_NameColor;
		char m_aName[MAX_CLIENTS * (MAX_NAME_LENGTH + 1)];
		char m_aQmTitle[64] = "";
		char m_aText[MAX_LINE_LENGTH];
		EQmChatEmoji m_ChatEmoji = EQmChatEmoji::NONE;
		CUIRect m_ChatEmojiRect = {};
		std::vector<SMergedAuthor> m_vMergedAuthors;
		bool m_Friend;
		bool m_Highlighted;
		bool m_ForceVisible;
		bool m_ConsoleSuppressed;
		QmHudNotifications::EServerMessageClass m_ServerMessageClass;
		std::optional<ColorRGBA> m_CustomColor;

		STextContainerIndex m_TextContainerIndex;
		int m_QuadContainerIndex;

		std::shared_ptr<CManagedTeeRenderInfo> m_pManagedTeeRenderInfo;

		float m_TextYOffset;
		// 当前消息实际占用的水平宽度，用于鼠标命中和选中高亮。
		float m_ContentWidth;
		float m_CutOffProgress;
		SPresentationState m_Presentation;

		int m_TimesRepeated;

		// 翻译标识符（每次内容变更时递增）
		unsigned int m_TranslationId = 0;

		std::shared_ptr<CTranslateResponse> m_pTranslateResponse;
	};

	bool m_PrevScoreBoardShowed;
	bool m_PrevShowChat;
	int64_t m_LastPresentationUpdateTime;
	int64_t m_LargeAreaOpenTick;
	bool m_LastPresentationShowLargeArea;
	int m_PendingConsoleLineIndex;

	CLine m_aLines[MAX_LINES];
	int m_CurrentLine;
	int m_BacklogCurLine;
	bool m_ScrollbarDragging;
	float m_ScrollbarDragOffset;
	std::optional<vec2> m_LastMousePos;
	bool m_MouseIsPress;
	vec2 m_MousePress;
	vec2 m_MouseRelease;

	enum
	{
		// client IDs for special messages
		CLIENT_MSG = -2,
		SERVER_MSG = -1,
	};

	enum
	{
		MODE_NONE = 0,
		MODE_ALL,
		MODE_TEAM,
	};

	enum
	{
		CHAT_SERVER = 0,
		CHAT_HIGHLIGHT,
		CHAT_CLIENT,
		CHAT_NUM,
	};

	int m_Mode;
	bool m_Show;
	bool m_CompletionUsed;
	int m_CompletionChosen;
	char m_aCompletionBuffer[MAX_LINE_LENGTH];
	int m_PlaceholderOffset;
	int m_PlaceholderLength;
	// 表情补全：候选列表与用户输入的冒号类型（半角/全角）
	const SQmChatEmojiDefinition *m_apEmojiCompletionList[QM_CHAT_EMOJI_COUNT];
	int m_EmojiCompletionListLength;
	char m_aEmojiCompletionColon[4];
	static char ms_aDisplayText[MAX_LINE_LENGTH];
	class CRateablePlayer
	{
	public:
		int m_ClientId;
		int m_Score;
	};
	CRateablePlayer m_aPlayerCompletionList[MAX_CLIENTS];
	int m_PlayerCompletionListLength;

public:
	struct CCommand
	{
		char m_aName[IConsole::TEMPCMD_NAME_LENGTH];
		char m_aParams[IConsole::TEMPCMD_PARAMS_LENGTH];
		char m_aHelpText[IConsole::TEMPCMD_HELP_LENGTH];

		CCommand() = default;
		CCommand(const char *pName, const char *pParams, const char *pHelpText)
		{
			str_copy(m_aName, pName);
			str_copy(m_aParams, pParams);
			str_copy(m_aHelpText, pHelpText);
		}

		bool operator<(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) < 0; }
		bool operator<=(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) <= 0; }
		bool operator==(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) == 0; }
	};

private:
	std::vector<CCommand> m_vServerCommands;
	bool m_ServerCommandsNeedSorting;

	struct CHistoryEntry
	{
		int m_Team;
		char m_aText[1];
	};
	CHistoryEntry *m_pHistoryEntry;
	CStaticRingBuffer<CHistoryEntry, 64 * 1024, CRingBufferBase::FLAG_RECYCLE> m_History;
	int m_PendingChatCounter;
	int64_t m_LastChatSend;
	int64_t m_aLastSoundPlayed[CHAT_NUM];
	bool m_IsInputCensored;
	char m_aCurrentInputText[MAX_LINE_LENGTH];
	bool m_EditingNewLine;
	char m_aSavedInputText[MAX_LINE_LENGTH];
	bool m_SavedInputPending;
	char m_aChatLogLastCleanupDate[11];

	bool m_ServerSupportsCommandInfo;
	static void ConSay(IConsole::IResult *pResult, void *pUserData);
	static void ConSayTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConChat(IConsole::IResult *pResult, void *pUserData);
	static void ConShowChat(IConsole::IResult *pResult, void *pUserData);
	static void ConEcho(IConsole::IResult *pResult, void *pUserData);
	static void ConClearChat(IConsole::IResult *pResult, void *pUserData);

	static void ConchainChatOld(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainChatFontSize(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainChatWidth(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	bool LineShouldHighlight(const char *pLine, const char *pName);
	void StoreSave(const char *pText);
	bool EnsureChatLogFolder() const;
	void CleanupOldChatLogs(const char *pToday);
	void SaveChatLogLine(int ClientId, int Team, const char *pLine);
	void PrintBlockedMessageToConsole(int ClientId, int Team, const char *pLine);
	void SendChatQueued(int Team, const char *pLine, bool AllowOutgoingTranslation);
	int CountInitializedLines() const;
	int CountVisibleLinesFrom(int BacklogLine) const;
	void UpdatePresentationStates(int64_t Now, float DeltaSeconds, bool ShowLargeArea, bool ExtraAnimations);

	friend class CBindChat;
	friend class CTranslate;
	friend class CTClient;
	friend class CPieMenu;

	// 翻译按钮状态
	struct STranslateButtonState
	{
		bool m_IsPressed = false;
		bool m_RectValid = false;
		bool m_IconUiElementInit = false;
		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_W = 0.0f;
		float m_H = 0.0f;
		bool m_AutoTranslateEnabled = false;
		CUIElement m_IconUiElement;
	};
	STranslateButtonState m_TranslateButton;

	// 翻译菜单下拉框展开状态
	enum class ETranslateDropdown : int
	{
		NONE = 0,
		INBOUND_LANG,
		OUTBOUND_LANG,
		BACKEND,
	};

	// 语言菜单
	class CLanguagePopupContext : public SPopupMenuId
	{
	public:
		CChat *m_pChat = nullptr;

		// DoDropDown 状态（使用游戏自带下拉框组件）
		CUi::SDropDownState m_InboundLangDropDownState;
		CUi::SDropDownState m_OutboundLangDropDownState;
		CUi::SDropDownState m_BackendDropDownState;

		enum
		{
			LABEL_TITLE = 0,
			LABEL_INBOUND_TOGGLE,
			LABEL_OUTBOUND_TOGGLE,
			LABEL_INBOUND_LANG,
			LABEL_OUTBOUND_LANG,
			LABEL_BACKEND,
			LABEL_WARNING,
			LABEL_COUNT,
		};
		CUIElement m_aLabelUiElements[LABEL_COUNT];
		bool m_LabelUiElementsInit = false;

		// 菜单动画状态
		int64_t m_OpenTime = 0;
		float m_AnimationProgress = 1.0f;

		void InitLabelUiElements(CUi *pUi)
		{
			if(m_LabelUiElementsInit)
				return;
			for(CUIElement &LabelUiElement : m_aLabelUiElements)
				LabelUiElement.Init(pUi, 1);
			m_LabelUiElementsInit = true;
		}
	};
	CLanguagePopupContext m_LanguagePopupContext;
	bool m_LanguageMenuOpen = false;

	class CChatLinePopupContext : public SPopupMenuId
	{
	public:
		CChat *m_pChat = nullptr;
		int m_ClientId = CLIENT_MSG;
		int m_TeamNumber = 0;
		int m_LineIndex = -1;
		char m_aName[64] = "";
		char m_aPlayerName[MAX_NAME_LENGTH] = "";
		char m_aText[MAX_LINE_LENGTH] = "";
		bool m_PlayerLine = false;
		bool m_LocalPlayer = false;

		CButtonContainer m_CopyButton;
		CButtonContainer m_AddOneButton;
		CButtonContainer m_ReplyButton;
		CButtonContainer m_SpectateButton;
		CButtonContainer m_MutePlayerButton;
		CButtonContainer m_AddBlockedWordButton;
		CButtonContainer m_CopyNameButton;
	};
	CChatLinePopupContext m_ChatLinePopupContext;

public:
	CChat();
	int Sizeof() const override { return sizeof(*this); }

	static constexpr float MESSAGE_TEE_PADDING_RIGHT = 0.5f;
	// 将变换后的 HUD 坐标反算回聊天默认坐标空间。
	static vec2 InverseHudTransformPoint(const vec2 &Point, const CUIRect &DefaultRect, const CUIRect &TargetRect)
	{
		if(DefaultRect.w <= 0.0f || TargetRect.w <= 0.0f)
			return Point;
		const float Scale = TargetRect.w / DefaultRect.w;
		return {
			DefaultRect.x + (Point.x - TargetRect.x) / Scale,
			DefaultRect.y + (Point.y - TargetRect.y) / Scale};
	}
	static bool IsChatLineHit(const CUIRect &Rect, const vec2 &Point)
	{
		return Rect.w > 0.0f && Rect.h > 0.0f && Rect.Inside(Point);
	}

	static int ClampBacklogLine(int Line, int TotalLines, int VisibleLines)
	{
		const int MaxScroll = std::max(0, TotalLines - VisibleLines);
		return std::clamp(Line, 0, MaxScroll);
	}
	static int ScrollbarValueToBacklogLine(float Value, int MaxScroll)
	{
		const float ClampedValue = std::clamp(Value, 0.0f, 1.0f);
		return std::clamp((int)std::round((1.0f - ClampedValue) * MaxScroll), 0, MaxScroll);
	}
	static float BacklogLineToScrollbarValue(int Line, int MaxScroll)
	{
		if(MaxScroll <= 0)
			return 1.0f;
		return 1.0f - std::clamp(Line, 0, MaxScroll) / (float)MaxScroll;
	}
	static bool IsCopyClickDrag(vec2 Press, vec2 Release)
	{
		return length(Release - Press) <= 5.0f;
	}
	static float CalculateCutOffOffsetX(float Progress);
	static bool AppendBlockWordToList(char *pList, size_t ListSize, const char *pWord)
	{
		if(pList == nullptr || pWord == nullptr || pWord[0] == '\0' || ListSize == 0)
			return false;

		const size_t ListLength = str_length(pList);
		const size_t SeparatorLength = ListLength > 0 ? 1 : 0;
		const size_t WordLength = str_length(pWord);
		if(ListLength + SeparatorLength + WordLength >= ListSize)
			return false;

		if(SeparatorLength > 0)
			str_append(pList, ";", ListSize);

		const size_t WordStart = str_length(pList);
		str_append(pList, pWord, ListSize);
		return (size_t)str_length(pList) > WordStart;
	}
	static bool BuildWhisperCommand(char *pBuf, size_t BufSize, const char *pName, const char *pMessage)
	{
		if(pBuf == nullptr || pName == nullptr || pName[0] == '\0' || pMessage == nullptr || BufSize == 0)
			return false;

		str_copy(pBuf, "/w \"", BufSize);
		char *pDst = pBuf + str_length(pBuf);
		str_escape(&pDst, pName, pBuf + BufSize);
		str_append(pBuf, "\" ", BufSize);
		str_append(pBuf, pMessage, BufSize);
		return true;
	}
	static bool BuildSpectateCommand(char *pBuf, size_t BufSize, const char *pName)
	{
		if(pBuf == nullptr || pName == nullptr || pName[0] == '\0' || BufSize == 0)
			return false;

		str_copy(pBuf, "say /spec \"", BufSize);
		char *pDst = pBuf + str_length(pBuf);
		str_escape(&pDst, pName, pBuf + BufSize);
		str_append(pBuf, "\"", BufSize);
		return true;
	}
	static const char *MessageNamePrefixForClientId(int ClientId, bool HideSystemPrefix = true)
	{
		if(ClientId == SERVER_MSG)
			return HideSystemPrefix ? "" : "*** ";
		if(ClientId == CLIENT_MSG)
			return "— ";
		return "";
	}
	static const char *SystemMessageNamePrefix(bool HideSystemPrefix = true)
	{
		return MessageNamePrefixForClientId(SERVER_MSG, HideSystemPrefix);
	}
	static bool ShouldHideBlockWordsMessage(EBlockWordsAction Action, bool Matched, int ClientId, bool IsLocalClient, int Team)
	{
		return Action == EBlockWordsAction::HIDE_MESSAGE && Matched &&
		       ClientId >= 0 && ClientId < MAX_CLIENTS && !IsLocalClient && Team != TEAM_WHISPER_SEND;
	}
	static bool ShouldSyncDummyCommand(const char *pLine)
	{
		if(pLine == nullptr)
			return false;

		const char *pTeamArgument = str_startswith_nocase(pLine, "/team ");
		if(pTeamArgument != nullptr)
			return *str_utf8_skip_whitespaces(pTeamArgument) != '\0';

		return str_comp_nocase(pLine, "/vote particle") == 0;
	}
	static QmHudNotifications::EServerMessageClass ResolveLineServerMessageClass(int ClientId, const char *pLine, std::optional<QmHudNotifications::EServerMessageClass> KnownServerMessageClass = std::nullopt)
	{
		if(ClientId != SERVER_MSG)
			return QmHudNotifications::EServerMessageClass::None;
		if(KnownServerMessageClass.has_value())
			return *KnownServerMessageClass;
		return QmHudNotifications::ServerMessageClass(pLine, QmHudNotifications::ESoloPrompt::None);
	}

	bool IsActive() const { return m_Mode != MODE_NONE; }
	const char *GetInputText() const { return m_Input.GetString(); }
	void AddLine(int ClientId, int Team, const char *pLine, bool ForceVisible = false);
	void AddLine(int ClientId, int Team, const char *pLine, bool ForceVisible, std::optional<QmHudNotifications::EServerMessageClass> KnownServerMessageClass);
	void EnableMode(int Team);
	void DisableMode();
	void SaveDraft();
	void RegisterCommand(const char *pName, const char *pParams, const char *pHelpText);
	void UnregisterCommand(const char *pName);
	void Echo(const char *pString);
	void Echo(const char *pString, bool ForceVisible);

	void OnWindowResize() override;
	void OnConsoleInit() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;
	void OnPrepareLines(float y);
	void Reset();
	void OnRelease() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;
	void OnInit() override;

	void RebuildChat();
	void ClearLines();

	void EnsureCoherentFontSize() const;
	void EnsureCoherentWidth() const;

	float FontSize() const { return g_Config.m_ClChatFontSize / 10.0f; }
	float MessagePaddingX() const { return FontSize() * (5 / 6.f); }
	float MessagePaddingY() const { return FontSize() * (1 / 6.f); }
	float MessageTeeSize() const { return FontSize() * (7 / 6.f); }
	float MessageRounding() const { return FontSize() * (1 / 2.f); }

	// 翻译按钮相关方法
	vec2 GetChatMousePos() const;
	vec2 GetUiMousePos() const;
	void RenderTranslateButton(const CUIRect &ButtonRect);
	void ToggleAutoTranslate();
	void OpenLanguageMenu();
	void CloseLanguageMenu();
	bool IsLanguageMenuOpen() const { return m_LanguageMenuOpen; }
	bool TranslateVisibleChatLines();
	static bool IsManualVisibleTranslateCandidate(int ClientId, bool HasText, bool HasTranslateResponse, const int *pLocalIds, size_t NumLocalIds)
	{
		if(!HasText || HasTranslateResponse)
			return false;
		if(ClientId < 0)
			return false;
		for(size_t i = 0; i < NumLocalIds; ++i)
		{
			if(pLocalIds[i] >= 0 && ClientId == pLocalIds[i])
				return false;
		}
		return true;
	}
	static CUi::EPopupMenuFunctionResult PopupLanguageMenu(void *pContext, CUIRect View, bool Active);
	void OpenChatLineMenu(const CLine &Line, vec2 UiMousePos);
	void CloseChatLineMenu();
	static CUi::EPopupMenuFunctionResult PopupChatLineMenu(void *pContext, CUIRect View, bool Active);
	void AddTextToBlockWords(const char *pText);
	void ReplyToChatLine(const CChatLinePopupContext &Context);
	void RepeatChatLine(const CChatLinePopupContext &Context);
	void SpectateChatLine(const CChatLinePopupContext &Context);

	// 聊天行索引辅助方法（用于翻译任务的内存安全）
	int GetLineIndex(const CLine *pLine) const;
	CLine *GetLineByIndex(int Index);
	void InvalidateLineTranslation(CLine &Line);
	ColorRGBA PlayerNameColor(int ClientId, int NameColor, bool TeamMessage) const;
	void AddMergedAuthor(CLine &Line, int ClientId);
	void RebuildMergedAuthorName(CLine &Line);
	void PrintLineToConsole(const CLine &Line) const;
	void FlushPendingConsoleLine(bool Force = false);

	// ----- send functions -----

	// Sends a chat message to the server.
	//
	// @param Team MODE_ALL=0 MODE_TEAM=1
	// @param pLine the chat message
	void SendChat(int Team, const char *pLine);
	// Sends a chat message using the specified connection (main/dummy).
	void SendChatOnConn(int Conn, int Team, const char *pLine, bool AllowWhitespaceOnly = false, bool HandleLocalSaveForLoadCommand = true);

	// Sends a chat message to the server.
	//
	// It uses a queue with a maximum of 3 entries
	// that ensures there is a minimum delay of one second
	// between sent messages.
	//
	// It uses team or public chat depending on m_Mode.
	void SendChatQueued(const char *pLine);
};

static inline float ChatPresentationClamp(float Value)
{
	return std::clamp(Value, 0.0f, 1.0f);
}

static inline float ChatPresentationCubicBezierSample(float A, float B, float T)
{
	const float InvT = 1.0f - T;
	return 3.0f * InvT * InvT * T * A + 3.0f * InvT * T * T * B + T * T * T;
}

static inline float ChatPresentationEaseOut(float Progress)
{
	const float X = ChatPresentationClamp(Progress);
	float Low = 0.0f;
	float High = 1.0f;
	float T = X;
	for(int i = 0; i < 8; ++i)
	{
		T = (Low + High) * 0.5f;
		const float SampleX = ChatPresentationCubicBezierSample(0.16f, 0.30f, T);
		if(SampleX < X)
			Low = T;
		else
			High = T;
	}
	return ChatPresentationClamp(ChatPresentationCubicBezierSample(1.0f, 1.0f, T));
}

static inline float ChatPresentationTicksToSeconds(int64_t Ticks)
{
	return Ticks <= 0 ? 0.0f : Ticks / (float)time_freq();
}

inline bool CChat::CanMergePlayerMessages(int PreviousClientId, int PreviousTeam, const char *pPreviousText, int64_t PreviousTime, int ClientId, int Team, const char *pText, int64_t Now)
{
	if(PreviousClientId < 0 || ClientId < 0 || PreviousTeam >= TEAM_WHISPER_SEND || Team >= TEAM_WHISPER_SEND)
		return false;
	if(pPreviousText == nullptr || pText == nullptr || str_comp(pPreviousText, pText) != 0)
		return false;
	if(Now < PreviousTime)
		return false;
	return Now - PreviousTime <= time_freq() * 2;
}

inline void CChat::ResetPresentationState(SPresentationState &Presentation)
{
	Presentation.m_PresentationBirthTick = 0;
	Presentation.m_EntryProgress = 0.0f;
	Presentation.m_LayoutVisibility = 0.0f;
	Presentation.m_RenderY = 0.0f;
	Presentation.m_TargetY = 0.0f;
	Presentation.m_RenderOffsetX = 0.0f;
	Presentation.m_RenderOffsetY = 0.0f;
	Presentation.m_RenderAlpha = 0.0f;
	Presentation.m_RenderScale = 1.0f;
	Presentation.m_RenderYInitialized = false;
	Presentation.m_AnimationsSuppressed = false;
	Presentation.m_State = EPresentationState::COLLAPSED;
}

inline void CChat::BeginLinePresentation(SPresentationState &Presentation, int64_t Now, bool GentleRefresh)
{
	const float StartProgress = GentleRefresh ? std::max(Presentation.m_EntryProgress, 0.72f) : 0.0f;
	Presentation.m_PresentationBirthTick = Now - (int64_t)(StartProgress * CHAT_ENTRY_DURATION * time_freq());
	Presentation.m_LayoutVisibility = 1.0f;
	Presentation.m_RenderOffsetY = 0.0f;
	Presentation.m_RenderYInitialized = false;
	Presentation.m_AnimationsSuppressed = false;
	Presentation.m_State = EPresentationState::ENTERING;
	Presentation.m_EntryProgress = StartProgress;
	const float EntryEase = ChatPresentationEaseOut(Presentation.m_EntryProgress);
	Presentation.m_RenderOffsetX = CHAT_ENTRY_OFFSET_X * (1.0f - EntryEase);
	Presentation.m_RenderAlpha = EntryEase;
	Presentation.m_RenderScale = 1.0f;
}

inline void CChat::UpdateLinePresentation(
	SPresentationState &Presentation,
	int64_t LineTime,
	int64_t Now,
	float DeltaSeconds,
	bool ShowLargeArea,
	bool ForceVisible,
	int64_t LargeAreaOpenTick,
	float RecallDelaySeconds,
	bool ExtraAnimations)
{
	DeltaSeconds = std::clamp(DeltaSeconds, 0.0f, CHAT_PRESENTATION_MAX_DELTA_SECONDS);
	(void)DeltaSeconds;

	// 系统时间回拨时，避免动画年龄变成异常值。
	if(Now < Presentation.m_PresentationBirthTick || Now < LineTime)
		Presentation.m_PresentationBirthTick = Now;

	const float LineAge = ChatPresentationTicksToSeconds(Now - LineTime);
	const bool VisibleWithoutAnimations = ForceVisible || ShowLargeArea || LineAge <= CHAT_MESSAGE_VISIBLE_DURATION;
	const auto ApplySettledState = [&] {
		Presentation.m_EntryProgress = 1.0f;
		Presentation.m_LayoutVisibility = VisibleWithoutAnimations ? 1.0f : 0.0f;
		Presentation.m_RenderOffsetX = 0.0f;
		Presentation.m_RenderOffsetY = 0.0f;
		Presentation.m_RenderAlpha = VisibleWithoutAnimations ? 1.0f : 0.0f;
		Presentation.m_RenderScale = 1.0f;
		Presentation.m_State = VisibleWithoutAnimations ? EPresentationState::VISIBLE : EPresentationState::COLLAPSED;
	};
	if(!ExtraAnimations)
	{
		Presentation.m_AnimationsSuppressed = true;
		ApplySettledState();
		return;
	}
	if(Presentation.m_AnimationsSuppressed)
	{
		Presentation.m_PresentationBirthTick = Now - (int64_t)(CHAT_ENTRY_DURATION * time_freq());
		const bool HistoricalRecallPending = ShowLargeArea && !ForceVisible && LargeAreaOpenTick > 0 && LineTime < LargeAreaOpenTick;
		ApplySettledState();
		if((VisibleWithoutAnimations && !HistoricalRecallPending) || (!VisibleWithoutAnimations && LineAge >= CHAT_MESSAGE_VISIBLE_DURATION + CHAT_EXIT_DURATION))
			Presentation.m_AnimationsSuppressed = false;
		return;
	}

	const float EntryAge = ChatPresentationTicksToSeconds(Now - Presentation.m_PresentationBirthTick);
	Presentation.m_EntryProgress = ChatPresentationClamp(EntryAge / CHAT_ENTRY_DURATION);

	const float EntryEase = ChatPresentationEaseOut(Presentation.m_EntryProgress);
	Presentation.m_RenderOffsetX = CHAT_ENTRY_OFFSET_X * (1.0f - EntryEase);
	Presentation.m_RenderOffsetY = 0.0f;
	Presentation.m_RenderScale = 1.0f;

	if(ShowLargeArea && !ForceVisible && LargeAreaOpenTick > 0 && LineTime < LargeAreaOpenTick)
	{
		const float RecallAge = ChatPresentationTicksToSeconds(Now - LargeAreaOpenTick) - std::max(0.0f, RecallDelaySeconds);
		const float RecallProgress = ChatPresentationClamp(RecallAge / CHAT_RECALL_DURATION);
		const float RecallEase = ChatPresentationEaseOut(RecallProgress);
		Presentation.m_LayoutVisibility = RecallProgress > 0.0f ? 1.0f : 0.0f;
		Presentation.m_RenderOffsetX = CHAT_ENTRY_OFFSET_X * (1.0f - RecallEase);
		Presentation.m_RenderOffsetY = 0.0f;
		Presentation.m_RenderAlpha = RecallEase;
		Presentation.m_RenderScale = 1.0f;
		Presentation.m_State = RecallProgress >= 1.0f ? EPresentationState::VISIBLE : EPresentationState::ENTERING;
		return;
	}

	// 入场阶段优先级最高，避免刚出现的消息直接进入退场状态。
	if(Presentation.m_EntryProgress < 1.0f)
	{
		Presentation.m_LayoutVisibility = 1.0f;
		Presentation.m_RenderOffsetX = CHAT_ENTRY_OFFSET_X * (1.0f - EntryEase);
		Presentation.m_RenderOffsetY = 0.0f;
		Presentation.m_RenderAlpha = EntryEase;
		Presentation.m_State = EPresentationState::ENTERING;
		return;
	}

	// 强制显示消息不参与普通聊天超时逻辑。
	if(ForceVisible)
	{
		Presentation.m_LayoutVisibility = 1.0f;
		Presentation.m_RenderOffsetX = 0.0f;
		Presentation.m_RenderOffsetY = 0.0f;
		Presentation.m_RenderAlpha = 1.0f;
		Presentation.m_RenderScale = 1.0f;
		Presentation.m_State = EPresentationState::VISIBLE;
		return;
	}

	// 展开聊天历史时，恢复所有历史消息，不让它们按普通 HUD 生命周期消失。
	if(ShowLargeArea)
	{
		Presentation.m_LayoutVisibility = 1.0f;
		Presentation.m_RenderOffsetX = 0.0f;
		Presentation.m_RenderOffsetY = 0.0f;
		Presentation.m_RenderAlpha = 1.0f;
		Presentation.m_RenderScale = 1.0f;
		Presentation.m_State = EPresentationState::VISIBLE;
		return;
	}

	// 对齐官方语义：
	// 0~14 秒：完全显示。
	// 14 秒后：整条消息横向滑出。
	// 滑出后：折叠，不再参与正常 HUD 绘制。
	const float FadeProgress = ChatPresentationClamp(
		(LineAge - CHAT_MESSAGE_VISIBLE_DURATION) / CHAT_EXIT_DURATION);

	if(FadeProgress > 0.0f)
	{
		const float FadeEase =
			FadeProgress * FadeProgress * (3.0f - 2.0f * FadeProgress);

		if(FadeProgress >= 1.0f)
		{
			Presentation.m_LayoutVisibility = 0.0f;
			Presentation.m_RenderOffsetX = CHAT_ENTRY_OFFSET_X;
			Presentation.m_RenderOffsetY = 0.0f;
			Presentation.m_RenderAlpha = 0.0f;
			Presentation.m_RenderScale = 1.0f;
			Presentation.m_State = EPresentationState::COLLAPSED;
			return;
		}

		Presentation.m_LayoutVisibility = 1.0f;
		Presentation.m_RenderOffsetX = CHAT_ENTRY_OFFSET_X * FadeEase;
		Presentation.m_RenderOffsetY = 0.0f;
		Presentation.m_RenderAlpha = 1.0f - FadeEase;
		Presentation.m_RenderScale = 1.0f;
		Presentation.m_State = EPresentationState::EXITING;
		return;
	}

	Presentation.m_LayoutVisibility = 1.0f;
	Presentation.m_RenderOffsetX = 0.0f;
	Presentation.m_RenderOffsetY = 0.0f;
	Presentation.m_RenderAlpha = 1.0f;
	Presentation.m_RenderScale = 1.0f;
	Presentation.m_State = EPresentationState::VISIBLE;
}

inline float CChat::SmoothPresentationY(float CurrentY, float TargetY, float DeltaSeconds)
{
	DeltaSeconds = std::clamp(DeltaSeconds, 0.0f, CHAT_PRESENTATION_MAX_DELTA_SECONDS);
	if(!std::isfinite(CurrentY) || !std::isfinite(TargetY))
		return TargetY;
	const float Blend = 1.0f - std::exp(-CHAT_RENDER_Y_SMOOTHING * DeltaSeconds);
	return CurrentY + (TargetY - CurrentY) * std::clamp(Blend, 0.0f, 1.0f);
}
#endif
