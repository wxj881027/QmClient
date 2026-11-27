#ifndef ENGINE_CLIENT_GLYPH_RENDER_RETRY_H
#define ENGINE_CLIENT_GLYPH_RENDER_RETRY_H

#include <chrono>
#include <new>

enum class EQmGlyphState
{
	UNINITIALIZED,
	// 只从 FreeType 取排版度量，未写入图集。测量/布局路径不得为了拿宽度
	// 触发位图光栅化；真正绘制时再升级为 RENDERED。
	MEASURED,
	// 后台 worker 已接收任务，结果尚未发布。可见文本命中该状态时仍由主线程
	// 同步完成，不能等待异步结果，否则切页会出现字形延迟。
	QUEUED,
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

// 测量（TextWidth / 纯布局）只允许补齐尚未渲染字形的度量。
// 已渲染字形必须保持 RENDERED：把它降级为 MEASURED 会让随后的绘制重新
// 光栅化并再占一块图集，预热成果被立刻作废。后台在飞字形同样必须保持
// QUEUED，否则结果发布时找不到任务归属。
constexpr bool QmMeasurementNeedsGlyphMetrics(EQmGlyphState State)
{
	return State != EQmGlyphState::MEASURED && State != EQmGlyphState::RENDERED &&
	       State != EQmGlyphState::ERROR;
}

constexpr EQmGlyphState QmGlyphStateAfterMeasurement(EQmGlyphState State)
{
	return State == EQmGlyphState::QUEUED || State == EQmGlyphState::RENDERED ? State : EQmGlyphState::MEASURED;
}

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
