#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_EMOJI_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_EMOJI_H

#include <base/system.h>

#include <engine/graphics.h>

#include <game/client/component.h>

#include <algorithm>
#include <array>
#include <cstddef>

enum class EQmChatEmoji
{
	NONE = 0,
	LOVE,
	NO,
	OPPOSE,
	AWKWARD,
	KNEEL,
	HEHE,
	INSULT,
	CUTE,
	ANGRY,
	DEAD,
	AGREE,
	SURRENDER,
	SMELL,
	QUESTION,
	SHOCKED,
	SUPPORT,
	COUNT,
};

constexpr std::size_t QM_CHAT_EMOJI_COUNT = static_cast<std::size_t>(EQmChatEmoji::COUNT) - 1;

struct SQmChatEmojiDefinition
{
	const char *m_pText;
	EQmChatEmoji m_Emoji;
	const char *m_pTexturePath;
};

inline constexpr std::array<SQmChatEmojiDefinition, QM_CHAT_EMOJI_COUNT> QM_CHAT_EMOJI_DEFINITIONS = {{
	{":ax", EQmChatEmoji::LOVE, "qmclient/chat_emojis/love.png"},
	{":bx", EQmChatEmoji::NO, "qmclient/chat_emojis/no.png"},
	{":fd", EQmChatEmoji::OPPOSE, "qmclient/chat_emojis/oppose.png"},
	{":gg", EQmChatEmoji::AWKWARD, "qmclient/chat_emojis/awkward.png"},
	{":gx", EQmChatEmoji::KNEEL, "qmclient/chat_emojis/kneel.png"},
	{":hh", EQmChatEmoji::HEHE, "qmclient/chat_emojis/hehe.png"},
	{":mr", EQmChatEmoji::INSULT, "qmclient/chat_emojis/insult.png"},
	{":mm", EQmChatEmoji::CUTE, "qmclient/chat_emojis/cute.png"},
	{":sq", EQmChatEmoji::ANGRY, "qmclient/chat_emojis/angry.png"},
	{":sd", EQmChatEmoji::DEAD, "qmclient/chat_emojis/dead.png"},
	{":ty", EQmChatEmoji::AGREE, "qmclient/chat_emojis/agree.png"},
	{":tx", EQmChatEmoji::SURRENDER, "qmclient/chat_emojis/surrender.png"},
	{":wd", EQmChatEmoji::SMELL, "qmclient/chat_emojis/smell.png"},
	{":wh", EQmChatEmoji::QUESTION, "qmclient/chat_emojis/question.png"},
	{":zj", EQmChatEmoji::SHOCKED, "qmclient/chat_emojis/shocked.png"},
	{":zc", EQmChatEmoji::SUPPORT, "qmclient/chat_emojis/support.png"},
}};

// 半角 ':' 或全角 '：'（U+FF1A）开头时返回冒号 UTF-8 字节数，否则返回 0。
inline int QmChatEmojiColonUtf8Length(const char *pText)
{
	if(pText == nullptr || pText[0] == '\0')
		return 0;
	if(pText[0] == ':')
		return 1;
	// U+FF1A FULLWIDTH COLON: EF BC 9A
	if((unsigned char)pText[0] == 0xEF && (unsigned char)pText[1] == 0xBC && (unsigned char)pText[2] == 0x9A)
		return 3;
	return 0;
}

inline bool QmChatEmojiIsColonPrefixed(const char *pText)
{
	return QmChatEmojiColonUtf8Length(pText) > 0;
}

// pPrefix 为冒号之后的搜索串（可为空）。按官方码序收集所有前缀匹配项。
inline int QmChatEmojiCollectByPrefix(const char *pPrefix, const SQmChatEmojiDefinition **apMatches, int MaxMatches)
{
	if(apMatches == nullptr || MaxMatches <= 0)
		return 0;
	if(pPrefix == nullptr)
		pPrefix = "";
	int Count = 0;
	for(const SQmChatEmojiDefinition &Definition : QM_CHAT_EMOJI_DEFINITIONS)
	{
		if(str_startswith_nocase(Definition.m_pText + 1, pPrefix))
		{
			apMatches[Count++] = &Definition;
			if(Count >= MaxMatches)
				break;
		}
	}
	return Count;
}

// 将候选码（不含冒号）以逗号连接，便于输入框旁展示“可能出现的所有”。
inline bool QmChatEmojiFormatCandidates(const SQmChatEmojiDefinition *const *apMatches, int NumMatches, char *pOut, size_t OutSize)
{
	if(pOut == nullptr || OutSize == 0)
		return false;
	pOut[0] = '\0';
	if(apMatches == nullptr || NumMatches <= 0)
		return false;
	for(int i = 0; i < NumMatches; ++i)
	{
		if(apMatches[i] == nullptr || apMatches[i]->m_pText == nullptr)
			continue;
		const char *pCode = apMatches[i]->m_pText + 1;
		if(pOut[0] != '\0')
			str_append(pOut, ",", OutSize);
		str_append(pOut, pCode, OutSize);
	}
	return pOut[0] != '\0';
}

inline EQmChatEmoji QmChatEmojiFromText(const char *pText)
{
	if(pText == nullptr)
		return EQmChatEmoji::NONE;
	const int ColonLength = QmChatEmojiColonUtf8Length(pText);
	if(ColonLength <= 0)
		return EQmChatEmoji::NONE;
	// 归一化为半角冒号再比对，使 :ax 与 ：ax 都能识别为表情。
	char aNormalized[16];
	aNormalized[0] = ':';
	str_copy(aNormalized + 1, pText + ColonLength, sizeof(aNormalized) - 1);
	for(const SQmChatEmojiDefinition &Definition : QM_CHAT_EMOJI_DEFINITIONS)
	{
		if(str_comp(aNormalized, Definition.m_pText) == 0)
			return Definition.m_Emoji;
	}
	return EQmChatEmoji::NONE;
}

inline const char *QmChatEmojiTexturePath(EQmChatEmoji Emoji)
{
	for(const SQmChatEmojiDefinition &Definition : QM_CHAT_EMOJI_DEFINITIONS)
	{
		if(Definition.m_Emoji == Emoji)
			return Definition.m_pTexturePath;
	}
	return nullptr;
}

constexpr bool QmChatEmojiIsKnown(EQmChatEmoji Emoji)
{
	const std::size_t Value = static_cast<std::size_t>(Emoji);
	return Value > static_cast<std::size_t>(EQmChatEmoji::NONE) && Value < static_cast<std::size_t>(EQmChatEmoji::COUNT);
}

constexpr bool QmChatEmojiShouldRenderImage(EQmChatEmoji Emoji, bool TextureAvailable)
{
	return QmChatEmojiIsKnown(Emoji) && TextureAvailable;
}

constexpr bool QmChatEmojiShouldTranslate(EQmChatEmoji Emoji)
{
	return !QmChatEmojiIsKnown(Emoji);
}

constexpr bool QmChatEmojiShouldLoadTexture(EQmChatEmoji Emoji, bool LoadAttempted)
{
	return QmChatEmojiIsKnown(Emoji) && !LoadAttempted;
}

inline float QmChatEmojiChatDisplaySize(float FontSize)
{
	return std::clamp(FontSize * 3.0f, 18.0f, 30.0f);
}

inline float QmChatEmojiBubbleDisplaySize(float FontSize)
{
	return std::clamp(FontSize * 3.0f, 48.0f, 96.0f);
}

class CQmChatEmoji : public CComponent
{
	mutable std::array<IGraphics::CTextureHandle, QM_CHAT_EMOJI_COUNT> m_aTextures;
	mutable std::array<bool, QM_CHAT_EMOJI_COUNT> m_aLoadAttempted{};

	void EnsureTextureLoaded(EQmChatEmoji Emoji) const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnShutdown() override;

	bool CanRender(EQmChatEmoji Emoji) const;
	void Render(EQmChatEmoji Emoji, float X, float Y, float Width, float Height, float Alpha) const;
};

#endif
