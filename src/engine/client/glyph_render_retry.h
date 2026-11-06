#ifndef ENGINE_CLIENT_GLYPH_RENDER_RETRY_H
#define ENGINE_CLIENT_GLYPH_RENDER_RETRY_H

#include <chrono>
#include <new>

enum class EQmGlyphState
{
	UNINITIALIZED,
	RENDERED,
	ERROR,
	RETRY,
};

enum class EQmGlyphRenderResult
{
	SUCCESS,
	UNSUPPORTED,
	RETRY,
};

// 内存不足时整条光栅化链路共用冷却时间；已生成的字形仍可使用。
// 替代字符只借给本次排版，不能覆盖原字形身份或变成永久成功缓存。
class CQmGlyphRenderRetry
{
	bool m_Pending = false;
	std::chrono::nanoseconds m_NextAttempt{};

public:
	bool Blocked() const { return m_Pending; }

	void Failed(std::chrono::nanoseconds Now)
	{
		if(!m_Pending)
			m_NextAttempt = Now + std::chrono::seconds(1);
		m_Pending = true;
	}

	// 由帧末调用；返回 true 时需使缓存的替代字符和旧排版失效。
	bool BeginRetry(std::chrono::nanoseconds Now)
	{
		if(!m_Pending || Now < m_NextAttempt)
			return false;
		m_Pending = false;
		return true;
	}

	void Reset() { m_Pending = false; }

	template<typename TGlyph, typename TRender, typename TFallback>
	const TGlyph *Resolve(TGlyph &Glyph, std::chrono::nanoseconds Now, TRender &&Render, TFallback &&Fallback)
	{
		if(Glyph.m_State == EQmGlyphState::RENDERED)
			return &Glyph;
		if(Glyph.m_State == EQmGlyphState::ERROR)
			return Fallback();
		if(!m_Pending)
		{
			EQmGlyphRenderResult Result;
			try
			{
				Result = Render();
			}
			catch(const std::bad_alloc &)
			{
				Result = EQmGlyphRenderResult::RETRY;
			}
			if(Result == EQmGlyphRenderResult::SUCCESS)
			{
				Glyph.m_State = EQmGlyphState::RENDERED;
				return &Glyph;
			}
			if(Result == EQmGlyphRenderResult::UNSUPPORTED)
			{
				Glyph.m_State = EQmGlyphState::ERROR;
				return Fallback();
			}
			Failed(Now);
		}
		Glyph.m_State = EQmGlyphState::RETRY;
		return Fallback();
	}
};

#endif
