#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_BUBBLE_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_BUBBLE_STATE_H

#include "chat_bubble_geometry.h"
#include "nameplate_text_build.h"

#include <base/str.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

struct SQmChatBubbleMetadata
{
	bool m_Initialized = false;
	bool m_LastVisible = false;
	bool m_LastTyping = false;
	int64_t m_LastChatBubbleStartTick = 0;
	float m_LastTargetAlpha = std::numeric_limits<float>::quiet_NaN();
	float m_LastTargetScale = std::numeric_limits<float>::quiet_NaN();
	float m_LastTargetSlide = std::numeric_limits<float>::quiet_NaN();
	float m_LastTargetFillChars = std::numeric_limits<float>::quiet_NaN();
	float m_CachedTextWidth = 0.0f;
	float m_CachedTextHeight = 0.0f;
	float m_CachedFontSize = -INFINITY;
	float m_CachedLineWidth = -INFINITY;
	char m_aCachedText[256] = "";
	char m_aLayoutText[256] = "";
};

// 元数据和原生资源分开重置，避免默认构造新usage；只在主线程串行使用。
class CQmChatBubbleState : public SQmChatBubbleMetadata
{
	CQmNameplateTextBuild m_Build;
	CQmNameplateTextCache m_Cache;

public:
	STextContainerIndex m_TextContainerIndex;
	bool HasTextResources() const { return m_TextContainerIndex.m_Index >= 0 || m_Build.HasPendingCleanup(); }

	template<typename TText>
	void Cleanup(TText &Text)
	{
		m_Build.Cleanup(Text);
	}

	template<typename TText>
	void ClearLayout(TText &Text)
	{
		m_Cache.Reset();
		m_aLayoutText[0] = '\0';
		m_Build.Reset(Text, m_TextContainerIndex);
	}

	template<typename TText>
	void Reset(TText &Text)
	{
		static_cast<SQmChatBubbleMetadata &>(*this) = SQmChatBubbleMetadata{};
		ClearLayout(Text);
	}

	template<typename TText, typename TSeed>
	void Initialize(TText &Text, TSeed &&Seed)
	{
		Reset(Text);
		std::forward<TSeed>(Seed)();
		m_Initialized = true;
	}

	template<typename TText>
	void PrepareLayout(TText &Text, float FontSize, float LineWidth, const char *pText)
	{
		Cleanup(Text);
		const bool Dirty = !m_TextContainerIndex.Valid() || str_comp(m_aLayoutText, pText) != 0 ||
				   m_CachedFontSize != FontSize || m_CachedLineWidth != LineWidth;
		if(!m_Cache.NeedsUpdate(true, Dirty))
			return;
		bool Reuse = false;
		m_Build.Run(Text, m_TextContainerIndex, Reuse, false, m_Cache, [&] {
			CTextCursor Cursor;
			Cursor.m_FontSize = FontSize;
			Cursor.m_LineWidth = LineWidth;
			const bool Created = Text.CreateTextContainer(m_TextContainerIndex, &Cursor, pText);
			STextBoundingBox Bounds{};
			if(Created)
				Bounds = Text.GetBoundingBoxTextContainer(m_TextContainerIndex);
			if(Created && !QmChatBubbleValidTextSize(vec2(Bounds.m_W, Bounds.m_H)))
				throw std::length_error("invalid chat bubble text metrics");
			m_CachedTextWidth = Created ? Bounds.m_W : 0.0f;
			m_CachedTextHeight = Created ? Bounds.m_H : 0.0f;
			m_CachedFontSize = FontSize;
			m_CachedLineWidth = LineWidth;
			if(Created)
				str_copy(m_aLayoutText, pText);
			else
				m_aLayoutText[0] = '\0';
		});
		m_Cache.OnUpdate(Text.GlyphAtlasRevision());
	}
};

// 正常及异常出口均恢复调用前的颜色、映射和 flags。
template<typename TText, typename TGraphics>
class CQmChatBubbleRenderContext
{
	TText &m_Text;
	ColorRGBA m_PreviousColor;
	ColorRGBA m_PreviousOutline;
	CQmNameplateTextContext<TText, TGraphics> m_Context;
	bool m_Active = false;

public:
	CQmChatBubbleRenderContext(TText &Text, TGraphics &Graphics) :
		m_Text(Text), m_PreviousColor(Text.GetTextColor()), m_PreviousOutline(Text.GetTextOutlineColor()), m_Context(Text, Graphics, true) {}
	CQmChatBubbleRenderContext(const CQmChatBubbleRenderContext &) = delete;
	CQmChatBubbleRenderContext &operator=(const CQmChatBubbleRenderContext &) = delete;
	~CQmChatBubbleRenderContext()
	{
		try
		{
			Finish();
		}
		catch(...)
		{
		}
	}
	template<typename TMap>
	void Apply(TMap &&Map)
	{
		m_Active = true;
		m_Context.Apply(TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT, std::forward<TMap>(Map));
	}
	void Finish()
	{
		if(std::exchange(m_Active, false))
		{
			m_Text.TextColor(m_PreviousColor);
			m_Text.TextOutlineColor(m_PreviousOutline);
			m_Context.Finish();
		}
	}
};

// 只接住准备阶段的资源异常；绘制批次开始后不吞异常，不假定设备状态可继续使用。
class CQmChatBubbleFrame
{
	bool m_Drawing = false;

public:
	void BeginDrawing() { m_Drawing = true; }
	template<typename TWork>
	bool Run(TWork &&Work)
	{
		m_Drawing = false;
		try
		{
			std::forward<TWork>(Work)();
		}
		catch(const std::bad_alloc &)
		{
			if(m_Drawing)
				throw;
			return false;
		}
		catch(const std::length_error &)
		{
			if(m_Drawing)
				throw;
			return false;
		}
		return true;
	}
};

#endif
