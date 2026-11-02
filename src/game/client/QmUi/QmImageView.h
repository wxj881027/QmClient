/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMIMAGEVIEW_H
#define GAME_CLIENT_QMUI_QMIMAGEVIEW_H

// 共享看图视口：只负责几何与采样策略，不依赖 SDL、GPU 或界面组件。
//
// 坐标使用渲染像素：Scale 表示「一个图片像素占多少屏幕像素」，100% 即 1:1。
// 图片尺寸和视口尺寸可以按任意顺序设置，两者都有效之前不会给出图片矩形。
// 独立看图窗口、游戏内照片卡等需要缩放/平移/裁剪的看图面板共用这一套数学。
class CQmImageView
{
public:
	struct SRect
	{
		float m_X;
		float m_Y;
		float m_Width;
		float m_Height;
	};

	// 贴图过滤方式：缩小看全图时平滑，1:1 附近逐像素检查时用最近邻。
	enum class ESampling
	{
		SMOOTH,
		PIXEL,
	};

private:
	// 1:1 附近（含适配窗口的 60%-100%）用最近邻：截图本身就是最终像素，
	// 线性降采样会把它磨糊；大幅缩小时仍用平滑，避免整图锯齿。
	static constexpr float PIXEL_SAMPLING_MIN_SCALE = 0.6f;
	static constexpr float DEFAULT_MIN_SCALE = 0.01f;
	static constexpr float DEFAULT_MAX_SCALE = 32.0f;

	int m_ImageWidth = 0;
	int m_ImageHeight = 0;
	int m_ViewWidth = 0;
	int m_ViewHeight = 0;
	float m_Scale = 1.0f;
	float m_OffsetX = 0.0f;
	float m_OffsetY = 0.0f;
	float m_MinScale = DEFAULT_MIN_SCALE;
	float m_MaxScale = DEFAULT_MAX_SCALE;
	bool m_Fit = true;
	// 适应时允许放大：照片卡这类以「铺满容器」为 1x 的视图使用。
	bool m_FitAllowsUpscale = false;
	bool m_PixelMode = false;

	float MinScale() const;
	float ClampScale(float Scale) const;
	void ClampOffsets();

public:
	// 尺寸。换图会回到「适应」；视口尺寸变化保持当前缩放模式。
	void SetImageSize(int Width, int Height);
	void SetViewportSize(int Width, int Height);
	int ImageWidth() const { return m_ImageWidth; }
	int ImageHeight() const { return m_ImageHeight; }
	int ViewportWidth() const { return m_ViewWidth; }
	int ViewportHeight() const { return m_ViewHeight; }
	bool IsValid() const { return m_ImageWidth > 0 && m_ImageHeight > 0 && m_ViewWidth > 0 && m_ViewHeight > 0; }

	// 缩放模式。
	void SetFitAllowsUpscale(bool AllowUpscale);
	// 滚轮缩放的绝对比例范围；参数非正或倒置时恢复默认范围。
	void SetScaleLimits(float MinScale, float MaxScale);
	void Fit();
	void ActualSize();
	void ToggleFit();
	// 直接设定缩放比例（会夹到范围内），用于恢复相对缩放的视图。
	void SetScale(float Scale);
	// 以视口内坐标 (ViewX, ViewY) 为锚点缩放：锚点下的图片像素保持不动。
	void ZoomAt(float Factor, float ViewX, float ViewY);
	void Pan(float X, float Y);

	float Scale() const { return m_Scale; }
	float FitScale() const;
	// 相对「适应」的倍数：1 表示正好贴合视口。
	float RelativeZoom() const;
	bool IsFit() const { return m_Fit; }
	// 图片在视口中的矩形，原点是视口左上角。
	SRect ImageRect() const;

	// 采样策略。按住 Ctrl 等临时检查场景直接强制像素采样。
	void SetPixelMode(bool PixelMode) { m_PixelMode = PixelMode; }
	bool PixelMode() const { return m_PixelMode; }
	ESampling Sampling() const;
};

#endif
