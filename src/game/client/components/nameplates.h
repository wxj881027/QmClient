#ifndef GAME_CLIENT_COMPONENTS_NAMEPLATES_H
#define GAME_CLIENT_COMPONENTS_NAMEPLATES_H

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/component.h>

inline bool QmNameplateUsesPhysicalPixelAlignment(const float HiDpiScale, const bool IsMacos)
{
	return IsMacos && HiDpiScale > 1.0f;
}

// 名字牌文字在世界映射下栅格化：字形像素数 = FontSize * 该密度，绘制时又按同一映射缩放回去。
// 只有栅格化密度与绘制密度一致，字形才是 1:1 采样的（按相机缩放档位取整最多会偏 12%，表现为整条名字发虚）。
inline float QmNameplateTextRasterizationDensity(const float ScreenHeight, const float ScreenWorldHeight)
{
	if(!(ScreenHeight > 0.0f) || !(ScreenWorldHeight > 0.0f))
		return 1.0f;
	return ScreenHeight / ScreenWorldHeight;
}

// 相对容差：密度变化在此之内沿用已有字形。
inline constexpr float QM_NAMEPLATE_TEXT_RASTERIZATION_TOLERANCE = 0.01f;

// 缩放稳定判定的连续帧数：相机平滑缩放（含无级 zoom+/-）期间密度每帧都变，
// 此时不得重建文字容器，否则动画期间每帧都要重排所有玩家的文字（实测为严重卡顿）。
inline constexpr int QM_NAMEPLATE_TEXT_ZOOM_SETTLE_FRAMES = 2;

struct SQmNameplateTextRasterization
{
	float m_Density = 0.0f;
	bool m_Valid = false;
};

inline bool QmNameplateTextNeedsRebake(const SQmNameplateTextRasterization &Baked, const float Density)
{
	if(!(Density > 0.0f))
		return false;
	if(!Baked.m_Valid || !(Baked.m_Density > 0.0f))
		return true;
	return absolute(Baked.m_Density - Density) > Density * QM_NAMEPLATE_TEXT_RASTERIZATION_TOLERANCE;
}

inline SQmNameplateTextRasterization QmNameplateTextRasterizationAfterRebake(const float Density)
{
	SQmNameplateTextRasterization Rasterization;
	if(Density > 0.0f)
	{
		Rasterization.m_Density = Density;
		Rasterization.m_Valid = true;
	}
	return Rasterization;
}

// 缩放是否已稳定。相机在平滑缩放期间每帧都会改写 m_Zoom，这里按"密度连续多少帧没变"判定，
// 不依赖相机内部状态；动画一停就允许重建，此后不再重复触发。
struct SQmNameplateTextZoomStability
{
	float m_LastDensity = 0.0f;
	int m_StableFrames = 0;
	bool m_ZoomSettled = false;

	// 返回缩放是否已稳定。只有"刚稳定"那一帧返回 true，避免逐帧重复重建。
	bool RecordDensity(const float Density, const int SettleFrames = QM_NAMEPLATE_TEXT_ZOOM_SETTLE_FRAMES)
	{
		const bool DensityStable = m_StableFrames > 0 && Density == m_LastDensity;
		m_LastDensity = Density;
		m_StableFrames = DensityStable ? minimum(m_StableFrames + 1, SettleFrames + 1) : 1;
		// 密度一变就说明又进入动画：必须复位稳定标记，否则动画中仍会被判为"刚稳定"而放行重建。
		m_ZoomSettled = DensityStable && m_ZoomSettled;

		const bool Settled = m_StableFrames >= SettleFrames;
		const bool NewlySettled = Settled && !m_ZoomSettled;
		m_ZoomSettled = Settled;
		return NewlySettled;
	}
};

struct CNetObj_PlayerInfo;
class CUIRect;

class CNamePlates : public CComponent
{
private:
	class CNamePlatesData;
	CNamePlatesData *m_pData;
	void ResetChatBubbleAnimState(int ClientId, bool IsDestructing = false);
	void UpdateCoordXAlignFrameState();

public:
	void RenderNamePlateGame(vec2 Position, const CNetObj_PlayerInfo *pPlayerInfo, float Alpha, bool TrackCoordXAlign = true);
	// 预览框（设置页深色预览区域）所需高度：铭牌内容 + 与脚本体间距 + 脚本体 + 上下留白。
	// 玩家/分身取较大者，切换预览时框高不变。
	float MeasurePreviewAreaHeight() const;
	// PreviewArea 同时是自由移动的边界：铭牌与脚本体贴框底排布，拖动范围就是可见范围。
	void RenderNamePlatePreview(const CUIRect &PreviewArea, int Dummy);
	void RenderChatBubble(vec2 Position, int ClientId, float Alpha);
	void ResetNamePlates();
	int Sizeof() const override { return sizeof(*this); }
	void OnShutdown() override;
	void OnWindowResize() override;
	void OnRender() override;
	CNamePlates();
	~CNamePlates() override;
};

#endif
