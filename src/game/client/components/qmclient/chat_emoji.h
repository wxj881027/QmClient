#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_EMOJI_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_EMOJI_H

#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/jobs.h>

#include <game/client/component.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

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
	{":ax", EQmChatEmoji::LOVE, "qmclient/chat_emojis/love.webp"},
	{":bx", EQmChatEmoji::NO, "qmclient/chat_emojis/no.webp"},
	{":fd", EQmChatEmoji::OPPOSE, "qmclient/chat_emojis/oppose.webp"},
	{":gg", EQmChatEmoji::AWKWARD, "qmclient/chat_emojis/awkward.webp"},
	{":gx", EQmChatEmoji::KNEEL, "qmclient/chat_emojis/kneel.webp"},
	{":hh", EQmChatEmoji::HEHE, "qmclient/chat_emojis/hehe.webp"},
	{":mr", EQmChatEmoji::INSULT, "qmclient/chat_emojis/insult.webp"},
	{":mm", EQmChatEmoji::CUTE, "qmclient/chat_emojis/cute.webp"},
	{":sq", EQmChatEmoji::ANGRY, "qmclient/chat_emojis/angry.webp"},
	{":sd", EQmChatEmoji::DEAD, "qmclient/chat_emojis/dead.webp"},
	{":ty", EQmChatEmoji::AGREE, "qmclient/chat_emojis/agree.webp"},
	{":tx", EQmChatEmoji::SURRENDER, "qmclient/chat_emojis/surrender.webp"},
	{":wd", EQmChatEmoji::SMELL, "qmclient/chat_emojis/smell.webp"},
	{":wh", EQmChatEmoji::QUESTION, "qmclient/chat_emojis/question.webp"},
	{":zj", EQmChatEmoji::SHOCKED, "qmclient/chat_emojis/shocked.webp"},
	{":zc", EQmChatEmoji::SUPPORT, "qmclient/chat_emojis/support.webp"},
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

// 图片表情的最小可读尺寸：小于这个宽度就不再压缩，交给调用方（换行）处理。
constexpr float QM_CHAT_EMOJI_MIN_SIZE = 10.0f;

// 表情在给定宽度内的可用尺寸：放得下就保持原尺寸，放不下才等比缩小。
// 返回 0 表示这行已经放不下可读的表情，应换到下一行。
// 聊天宽度可调（cl_chat_width 最小 140）且玩家名可能很长，表情框直接整块摆在
// 文字后面时会被聊天区右边缘裁掉，所以这里按剩余宽度收一下。
inline float QmChatEmojiFitSize(float EmojiSize, float MaximumWidth)
{
	if(EmojiSize <= 0.0f)
		return 0.0f;
	if(EmojiSize <= MaximumWidth)
		return EmojiSize;
	if(MaximumWidth <= 0.0f)
		return 0.0f;
	const float MinimumSize = std::max(QM_CHAT_EMOJI_MIN_SIZE, EmojiSize * 0.5f);
	return MaximumWidth >= MinimumSize ? MaximumWidth : 0.0f;
}

// 图片表情按文字基线对齐时的纵向下移量（恒为非正值：表情只向上收）。
// 文字的基线在光标 Y 起算的第 AlignedFontSize 个像素，而表情框此前直接落在光标 Y 上，
// 于是整块表情都挂在基线之下（默认字体尤为明显：字形视觉高度远小于 em 框，本身下沉就多，
// 再加一个表情的高度就会压到下一行文字上）。这里把表情框底边压回基线，让表情坐在基线上、
// 保持与文字同一行，且不再侵入相邻行。
inline float QmChatEmojiBaselineOffset(float AlignedFontSize, float EmojiSize)
{
	if(EmojiSize <= 0.0f)
		return 0.0f;
	return std::min(0.0f, AlignedFontSize - EmojiSize);
}

inline bool QmChatEmojiInvalidateChangedLayout(bool RenderImage, bool &ImageLayout, float (&aCachedHeights)[2])
{
	if(RenderImage == ImageLayout)
		return false;

	// 异步图片就绪或回退为文字时，两个聊天宽度下的行高都必须重新测量。
	ImageLayout = RenderImage;
	for(float &Height : aCachedHeights)
		Height = -1.0f;
	return true;
}

// 解码任务独立持有图像，渲染线程只在完成后接管像素所有权。
class CQmChatEmojiLoadJob : public IJob
{
	std::function<void(CImageInfo &)> m_Load;
	CImageInfo m_Image;
	bool m_Retry = false;
	void Run() override
	{
		try
		{
			m_Load(m_Image);
		}
		catch(const std::bad_alloc &)
		{
			m_Image.Free();
			m_Retry = true;
		}
		catch(const std::length_error &)
		{
			m_Image.Free();
			m_Retry = true;
		}
		catch(...)
		{
			// 未知解码失败沿原文字回退，不把部分图像发布给主线程。
			m_Image.Free();
		}
	}

public:
	explicit CQmChatEmojiLoadJob(std::function<void(CImageInfo &)> Load) :
		m_Load(std::move(Load)) {}
	~CQmChatEmojiLoadJob() override { m_Image.Free(); }
	CImageInfo *Image() { return State() == STATE_DONE ? &m_Image : nullptr; }
	bool RetryableFailure() const { return State() == STATE_DONE && m_Retry; }
};

class IStorage;
void QmLoadChatEmojiImage(IStorage *pStorage, const char *pPath, CImageInfo &Image);
std::shared_ptr<CQmChatEmojiLoadJob> QmCreateChatEmojiLoadJob(IStorage *pStorage, EQmChatEmoji Emoji);

// 每个已知表情最多一个请求，固定队列容量由枚举域推导，保留原串行加载顺序。
// 主线程串行使用，factory/submit/upload回调不重入本状态；submit的false/异常表示未准入。
class CQmChatEmojiLoadState
{
	std::array<bool, QM_CHAT_EMOJI_COUNT> m_aRequested{};
	std::array<EQmChatEmoji, QM_CHAT_EMOJI_COUNT> m_aQueue{};
	size_t m_Begin = 0, m_Count = 0;
	std::shared_ptr<CQmChatEmojiLoadJob> m_pJob;
	EQmChatEmoji m_Loading = EQmChatEmoji::NONE;

	void Finish(bool Retry)
	{
		if(Retry)
		{
			m_aQueue[(m_Begin + m_Count) % QM_CHAT_EMOJI_COUNT] = m_Loading;
			++m_Count;
		}
		m_pJob.reset();
		m_Loading = EQmChatEmoji::NONE;
	}

public:
	CQmChatEmojiLoadState() = default;
	CQmChatEmojiLoadState(const CQmChatEmojiLoadState &) = delete;
	CQmChatEmojiLoadState &operator=(const CQmChatEmojiLoadState &) = delete;
	size_t PendingCount() const { return m_Count; }
	EQmChatEmoji Loading() const { return m_Loading; }
	const std::shared_ptr<CQmChatEmojiLoadJob> &Job() const { return m_pJob; }
	bool Request(EQmChatEmoji Emoji)
	{
		if(!QmChatEmojiIsKnown(Emoji))
			return false;
		const size_t Index = static_cast<size_t>(Emoji) - 1;
		if(m_aRequested[Index])
			return false;
		m_aQueue[(m_Begin + m_Count) % QM_CHAT_EMOJI_COUNT] = Emoji;
		++m_Count;
		m_aRequested[Index] = true;
		return true;
	}
	template<typename TCreate, typename TSubmit>
	bool TryStart(TCreate &&Create, TSubmit &&Submit)
	{
		if(m_pJob || m_Count == 0)
			return false;
		const EQmChatEmoji Emoji = m_aQueue[m_Begin];
		try
		{
			std::shared_ptr<CQmChatEmojiLoadJob> pCandidate = std::forward<TCreate>(Create)(Emoji);
			if(!pCandidate || !std::forward<TSubmit>(Submit)(pCandidate))
				return false;
			// shared_ptr移动和队列推进不分配，准入后不再经过可失败的准备。
			m_pJob = std::move(pCandidate);
			m_Loading = Emoji;
			m_Begin = (m_Begin + 1) % QM_CHAT_EMOJI_COUNT;
			--m_Count;
			return true;
		}
		catch(...)
		{
			return false;
		}
	}
	template<typename TCanUpload, typename TUpload>
	void Poll(TCanUpload &&CanUpload, TUpload &&Upload)
	{
		if(!m_pJob || !m_pJob->Finished())
			return;
		if(m_pJob->State() != IJob::STATE_DONE || m_pJob->RetryableFailure())
		{
			Finish(true);
			return;
		}
		CImageInfo &Image = *m_pJob->Image();
		if(!Image.m_pData)
		{
			Finish(false);
			return;
		}
		try
		{
			if(!std::forward<TCanUpload>(CanUpload)())
				return;
			if(std::forward<TUpload>(Upload)(Image, m_Loading))
			{
				Finish(false);
				return;
			}
		}
		catch(const std::bad_alloc &)
		{
		}
		catch(const std::length_error &)
		{
		}
		// 未移动的图像留在已完成任务里重试上传；已消费则排回队尾重新加载。
		if(!Image.m_pData)
			Finish(true);
	}
	void Clear()
	{
		m_pJob.reset();
		m_Loading = EQmChatEmoji::NONE;
		m_Begin = m_Count = 0;
		m_aRequested.fill(false);
	}
};

class CQmChatEmoji : public CComponent
{
	mutable std::array<IGraphics::CTextureHandle, QM_CHAT_EMOJI_COUNT> m_aTextures;
	mutable CQmChatEmojiLoadState m_LoadState;

	void EnsureTextureLoaded(EQmChatEmoji Emoji) const;
	void StartNextLoad() const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnUpdate() override;
	void OnShutdown() override;

	bool CanRender(EQmChatEmoji Emoji) const;
	void Render(EQmChatEmoji Emoji, float X, float Y, float Width, float Height, float Alpha) const;
};

#endif
