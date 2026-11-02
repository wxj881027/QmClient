/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmImageView.h"

#include <algorithm>
#include <cmath>

float CQmImageView::FitScale() const
{
	if(!IsValid())
		return 1.0f;
	const float Fit = std::min((float)m_ViewWidth / (float)m_ImageWidth, (float)m_ViewHeight / (float)m_ImageHeight);
	// 默认「适应」不放大原图；卡片类视图可以要求铺满视口。
	return m_FitAllowsUpscale ? Fit : std::min(1.0f, Fit);
}

float CQmImageView::MinScale() const
{
	return std::min(m_MinScale, FitScale());
}

float CQmImageView::ClampScale(float Scale) const
{
	const float Lower = MinScale();
	const float Upper = std::max(Lower, m_MaxScale);
	return std::clamp(Scale, Lower, Upper);
}

float CQmImageView::RelativeZoom() const
{
	const float Fit = FitScale();
	return Fit > 0.0f ? m_Scale / Fit : 1.0f;
}

void CQmImageView::ClampOffsets()
{
	const float MaxX = std::max(0.0f, ((float)m_ImageWidth * m_Scale - (float)m_ViewWidth) * 0.5f);
	const float MaxY = std::max(0.0f, ((float)m_ImageHeight * m_Scale - (float)m_ViewHeight) * 0.5f);
	m_OffsetX = std::clamp(m_OffsetX, -MaxX, MaxX);
	m_OffsetY = std::clamp(m_OffsetY, -MaxY, MaxY);
}

void CQmImageView::SetImageSize(int Width, int Height)
{
	m_ImageWidth = std::max(0, Width);
	m_ImageHeight = std::max(0, Height);
	// 换图后旧的缩放和平移失去参照，回到适应。
	Fit();
}

void CQmImageView::SetViewportSize(int Width, int Height)
{
	m_ViewWidth = std::max(0, Width);
	m_ViewHeight = std::max(0, Height);
	if(m_Fit)
		Fit();
	else
		ClampOffsets();
}

void CQmImageView::SetFitAllowsUpscale(bool AllowUpscale)
{
	if(m_FitAllowsUpscale == AllowUpscale)
		return;
	m_FitAllowsUpscale = AllowUpscale;
	// 正处在适应模式时，立即按新规则重新贴合。
	if(m_Fit)
		Fit();
}

void CQmImageView::SetScaleLimits(float MinScale, float MaxScale)
{
	if(!std::isfinite(MinScale) || !std::isfinite(MaxScale) || MinScale <= 0.0f || MaxScale <= 0.0f || MinScale > MaxScale)
	{
		m_MinScale = DEFAULT_MIN_SCALE;
		m_MaxScale = DEFAULT_MAX_SCALE;
		return;
	}
	m_MinScale = MinScale;
	m_MaxScale = MaxScale;
	// 范围变小时把当前缩放一起夹回来，避免留下范围外的状态。
	const float Scale = ClampScale(m_Scale);
	if(Scale != m_Scale)
	{
		m_Scale = Scale;
		ClampOffsets();
	}
}

void CQmImageView::Fit()
{
	m_Fit = true;
	m_Scale = FitScale();
	m_OffsetX = m_OffsetY = 0.0f;
}

void CQmImageView::ActualSize()
{
	m_Fit = false;
	m_Scale = 1.0f;
	m_OffsetX = m_OffsetY = 0.0f;
}

void CQmImageView::ToggleFit()
{
	if(m_Fit)
		ActualSize();
	else
		Fit();
}

void CQmImageView::SetScale(float Scale)
{
	if(!std::isfinite(Scale) || Scale <= 0.0f)
		return;
	m_Scale = ClampScale(Scale);
	m_Fit = false;
	ClampOffsets();
}

void CQmImageView::ZoomAt(float Factor, float ViewX, float ViewY)
{
	if(!IsValid() || !std::isfinite(Factor) || Factor <= 0.0f || !std::isfinite(ViewX) || !std::isfinite(ViewY))
		return;
	const float Scale = ClampScale(m_Scale * Factor);
	if(Scale == m_Scale)
		return;
	// 锚点下的图片像素在缩放前后保持同一屏幕位置。
	const float Ratio = Scale / m_Scale;
	m_OffsetX = (ViewX - m_ViewWidth * 0.5f) * (1.0f - Ratio) + m_OffsetX * Ratio;
	m_OffsetY = (ViewY - m_ViewHeight * 0.5f) * (1.0f - Ratio) + m_OffsetY * Ratio;
	m_Scale = Scale;
	m_Fit = false;
	ClampOffsets();
}

void CQmImageView::Pan(float X, float Y)
{
	if(!IsValid() || !std::isfinite(X) || !std::isfinite(Y))
		return;
	m_OffsetX += X;
	m_OffsetY += Y;
	ClampOffsets();
}

CQmImageView::SRect CQmImageView::ImageRect() const
{
	if(!IsValid())
		return {};
	const float Width = m_ImageWidth * m_Scale;
	const float Height = m_ImageHeight * m_Scale;
	return {(m_ViewWidth - Width) * 0.5f + m_OffsetX, (m_ViewHeight - Height) * 0.5f + m_OffsetY, Width, Height};
}

CQmImageView::ESampling CQmImageView::Sampling() const
{
	return m_PixelMode || m_Scale >= PIXEL_SAMPLING_MIN_SCALE ? ESampling::PIXEL : ESampling::SMOOTH;
}
