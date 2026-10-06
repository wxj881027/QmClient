// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QM_ICON_MANAGER_H
#define GAME_CLIENT_QM_ICON_MANAGER_H

#include <base/color.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/QmUi/QmTheme.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

class IConsole;
class IStorage;

enum class EQmIcon
{
#define QM_ICON_ENTRY(Id, Name, Codepoint) Id,
#define QM_ICON_ALIAS QM_ICON_ENTRY
#include "qm_icon_registry.inc"
#undef QM_ICON_ENTRY
#undef QM_ICON_ALIAS
	COUNT,
};

enum class EQmIconState
{
	NORMAL = 0,
	HOVER,
	ACTIVE,
	DISABLED,
};

inline bool QmIconAtlasNeedsReload(const bool IsReady, const int LoadedWeight, const int DesiredWeight)
{
	return !IsReady || LoadedWeight != DesiredWeight;
}

inline bool QmIconAtlasRetryCooldownActive(const int64_t Now, const int64_t RetryDeadline)
{
	return Now < RetryDeadline;
}

inline bool QmIconReloadCooldownActive(const int64_t Now, const int64_t RetryDeadline, const bool HasFailedTarget, const int FailedWeight, const bool FailedMsdfSupported, const int DesiredWeight, const bool MsdfSupported)
{
	return HasFailedTarget && Now < RetryDeadline && FailedWeight == DesiredWeight && FailedMsdfSupported == MsdfSupported;
}

inline size_t QmIconMsdfRunBucket(const uint64_t RunLength)
{
	if(RunLength <= 1)
		return 0;
	if(RunLength <= 2)
		return 1;
	if(RunLength <= 4)
		return 2;
	if(RunLength <= 8)
		return 3;
	if(RunLength <= 16)
		return 4;
	if(RunLength <= 32)
		return 5;
	if(RunLength <= 64)
		return 6;
	return 7;
}

struct SQmIconDiagnostics
{
	static constexpr size_t MSDF_RUN_BUCKET_COUNT = 8;
	uint64_t m_MsdfIconDraws = 0;
	uint64_t m_FontFallbackDraws = 0;
	uint64_t m_MaxMsdfManagerCallRun = 0;
	std::array<uint64_t, MSDF_RUN_BUCKET_COUNT> m_MsdfManagerCallRunBuckets{};
	uint64_t m_ReloadAttempts = 0;
	uint64_t m_ReloadSuccesses = 0;
	uint64_t m_AtlasSwaps = 0;
	uint64_t m_TextureLoads = 0;
	uint64_t m_TextureLoadFailures = 0;
	uint64_t m_TextureUnloads = 0;
};

// 高频绘制计数按时间窗口汇总，资源变化立即触发汇总。
struct SQmIconDiagnosticsWindow
{
	SQmIconDiagnostics m_Total;
	uint64_t m_Frames = 0;
	uint64_t m_MaxMsdfDraws = 0;
	int64_t m_LastLog = 0;

	bool Add(const SQmIconDiagnostics &Frame, const int64_t Now, const int64_t Interval)
	{
		if(m_Frames == 0 && m_LastLog == 0)
			m_LastLog = Now;
		++m_Frames;
		m_Total.m_MsdfIconDraws += Frame.m_MsdfIconDraws;
		m_Total.m_FontFallbackDraws += Frame.m_FontFallbackDraws;
		m_Total.m_MaxMsdfManagerCallRun = std::max(m_Total.m_MaxMsdfManagerCallRun, Frame.m_MaxMsdfManagerCallRun);
		for(size_t i = 0; i < SQmIconDiagnostics::MSDF_RUN_BUCKET_COUNT; ++i)
			m_Total.m_MsdfManagerCallRunBuckets[i] += Frame.m_MsdfManagerCallRunBuckets[i];
		m_MaxMsdfDraws = std::max(m_MaxMsdfDraws, Frame.m_MsdfIconDraws);
		m_Total.m_ReloadAttempts += Frame.m_ReloadAttempts;
		m_Total.m_ReloadSuccesses += Frame.m_ReloadSuccesses;
		m_Total.m_AtlasSwaps += Frame.m_AtlasSwaps;
		m_Total.m_TextureLoads += Frame.m_TextureLoads;
		m_Total.m_TextureLoadFailures += Frame.m_TextureLoadFailures;
		m_Total.m_TextureUnloads += Frame.m_TextureUnloads;
		const bool ResourceEvent = Frame.m_ReloadAttempts || Frame.m_ReloadSuccesses || Frame.m_AtlasSwaps ||
					   Frame.m_TextureLoads || Frame.m_TextureLoadFailures || Frame.m_TextureUnloads;
		return ResourceEvent || Now - m_LastLog >= Interval;
	}

	void Clear(const int64_t Now)
	{
		m_Total = {};
		m_Frames = 0;
		m_MaxMsdfDraws = 0;
		m_LastLog = Now;
	}
};

inline bool QmIconTextureCanCommit(const bool IsValid, const bool IsNullTexture)
{
	return IsValid && !IsNullTexture;
}

inline int QmIconPreferredAtlasScale(const float HiDpiScale)
{
	const float HiDpi = std::max(1.0f, HiDpiScale);
	return HiDpi >= 3.0f ? 4 : (HiDpi >= 1.5f ? 2 : 1);
}

inline std::array<int, 3> QmIconAtlasScaleFallbackOrder(const int PreferredScale)
{
	return {
		PreferredScale,
		PreferredScale == 4 ? 2 : (PreferredScale == 2 ? 4 : 2),
		PreferredScale == 1 ? 4 : 1,
	};
}

inline int NormalizeQmIconWeight(const int Weight)
{
	return Weight >= 0 && Weight <= 5 ? Weight : 1;
}

// FontIcon 回退使用与 MSDF 图标相同的目标方框边长，避免两条路径出现尺寸漂移。
inline float QmIconFallbackFontSize(const CUIRect &Rect)
{
	return std::min(Rect.w, Rect.h) * 0.8f;
}

// 图标必须 1:1 绘制：字形位图（BitmapW:BitmapH）等比放进调用方方框并居中。
// 图集 manifest 存的是每个字形自己的紧贴框，宽高比各不相同；直接把它映射到方框上
// 会让每个图标按各自比例被拉伸（历史实现的症状：图标"不是 1:1"）。
inline CUIRect QmIconAspectFittedRect(const CUIRect &Rect, const int BitmapW, const int BitmapH)
{
	if(BitmapW <= 0 || BitmapH <= 0 || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return Rect;
	const float BoxAspect = static_cast<float>(BitmapW) / static_cast<float>(BitmapH);
	const float RectAspect = Rect.w / Rect.h;
	CUIRect Out = Rect;
	if(BoxAspect > RectAspect)
	{
		Out.h = Rect.w / BoxAspect;
		Out.y = Rect.y + (Rect.h - Out.h) * 0.5f;
	}
	else
	{
		Out.w = Rect.h * BoxAspect;
		Out.x = Rect.x + (Rect.w - Out.w) * 0.5f;
	}
	return Out;
}

inline float QmIconPixelScale(const int DrawableExtent, const float LogicalExtent)
{
	return DrawableExtent > 0 && LogicalExtent > 0.0f ? DrawableExtent / LogicalExtent : 0.0f;
}

// 眼睛 morph 关键帧插值：把弹簧进度映射到相邻两帧与各自的 alpha。
// 端点（进度 0 / 1）精确落在首末帧上，因此与静态图标之间没有尺寸/形状跳变。
struct SQmIconMorphFrameBlend
{
	int m_Index0 = 0;
	int m_Index1 = 0;
	float m_Alpha0 = 1.0f;
	float m_Alpha1 = 0.0f;
};

inline SQmIconMorphFrameBlend QmIconMorphFrameBlend(const float Progress, const int FrameCount)
{
	if(FrameCount <= 1)
		return {};
	const float Clamped = std::clamp(Progress, 0.0f, 1.0f);
	const float Scaled = Clamped * static_cast<float>(FrameCount - 1);
	const int Index0 = std::clamp(static_cast<int>(Scaled), 0, FrameCount - 1);
	const int Index1 = std::min(Index0 + 1, FrameCount - 1);
	const float Alpha1 = Scaled - static_cast<float>(Index0);
	return {Index0, Index1, 1.0f - Alpha1, Alpha1};
}

inline ColorRGBA QmUiIconColor(const ColorRGBA &Color, const int ConfiguredColor, const unsigned int CustomColor = 0xFFFFFFFF, const float RainbowTime = 0.0f)
{
	ColorRGBA Result;
	switch(ConfiguredColor)
	{
	case 2:
		Result = ColorRGBA(0.0f, 0.0f, 0.0f, Color.a);
		break;
	case 3:
		Result = color_cast<ColorRGBA>(ColorHSLA(CustomColor));
		Result.a = Color.a;
		break;
	case 4:
		Result = color_cast<ColorRGBA>(ColorHSLA(std::fmod(RainbowTime * 0.2f, 1.0f), 0.75f, 0.6f, Color.a));
		break;
	case 1:
	default:
		Result = ColorRGBA(1.0f, 1.0f, 1.0f, Color.a);
		break;
	}
	return Result;
}

// 配置驱动的图标颜色（供 UI 各处与契约测试共用）。定义留在头文件内联：testrunner 不链接
// 任何客户端源文件，若把定义放在 qm_icon_manager.cpp，测试调用它就会链接失败
// （LNK2001: 无法解析的外部符号 ConfiguredQmUiIconSecondaryColor）。
inline ColorRGBA ConfiguredQmUiIconColor(const ColorRGBA &Color)
{
	if(g_Config.m_QmUiIconColor != 4)
		return QmUiIconColor(Color, g_Config.m_QmUiIconColor, g_Config.m_QmUiIconCustomColor);

	const float Time = static_cast<float>(time_get()) / static_cast<float>(time_freq());
	return QmUiIconColor(Color, g_Config.m_QmUiIconColor, g_Config.m_QmUiIconCustomColor, Time);
}

inline ColorRGBA ConfiguredQmUiIconSecondaryColor(const ColorRGBA &Color)
{
	ColorRGBA Result = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiIconDuotoneSecondaryColor, true));
	Result.a *= Color.a;
	return Result;
}

// CFGFLAG_COLALPHA 将旧的六位 RGB 图标颜色扩展为带 Alpha 的 packed 颜色。
// 显式 Alpha 和已有非零 packed Alpha 保留，避免覆盖用户选择的透明度。
constexpr bool MigrateLegacyQmUiIconDuotoneSecondaryColor(unsigned &Color, const unsigned DefaultColor, const EColorInputAlphaMode InputAlphaMode = EColorInputAlphaMode::PACKED)
{
	if(InputAlphaMode == EColorInputAlphaMode::EXPLICIT || (InputAlphaMode == EColorInputAlphaMode::PACKED && (Color & 0xFF000000u) != 0))
		return false;
	Color = (Color & 0x00FFFFFFu) | (DefaultColor & 0xFF000000u);
	return true;
}

struct SQmIconStyle
{
	ColorRGBA m_Normal{qm_theme::ICON.m_Normal};
	ColorRGBA m_Hover{qm_theme::ICON.m_Hover};
	ColorRGBA m_Active{qm_theme::ICON.m_Active};
	ColorRGBA m_Disabled{qm_theme::ICON.m_Disabled};

	ColorRGBA Color(EQmIconState State) const;
};

class CQmIconAtlas
{
public:
	struct SEntry
	{
		bool m_Valid = false;
		float m_U0 = 0.0f;
		float m_V0 = 0.0f;
		float m_U1 = 1.0f;
		float m_V1 = 1.0f;
		// 字形位图尺寸（manifest 里的紧贴框）。绘制时按它等比适配调用方方框，
		// 否则每个图标都会按自己的宽高比被拉伸（不是 1:1）。
		int m_BoxW = 0;
		int m_BoxH = 0;
	};

	// 眼睛 morph 的预烘焙 MSDF 关键帧容量（与烘焙脚本的 FRAME_COUNT 对应）。
	static constexpr int MORPH_FRAME_CAPACITY = 8;

	void Clear(IGraphics *pGraphics);
	// 图形设备重建后使用：纹理已随设备消失，只清本地状态，绝不对旧句柄发删除命令。
	void ResetForDeviceRecreate();
	void Swap(CQmIconAtlas &Other)
	{
		std::swap(m_Texture, Other.m_Texture);
		std::swap(m_aEntries, Other.m_aEntries);
		std::swap(m_aMorphFrames, Other.m_aMorphFrames);
		std::swap(m_MorphFrameCount, Other.m_MorphFrameCount);
		std::swap(m_LoadedIconCount, Other.m_LoadedIconCount);
		std::swap(m_Width, Other.m_Width);
		std::swap(m_Height, Other.m_Height);
		std::swap(m_PxRange, Other.m_PxRange);
		std::swap(m_UseTrueSdf, Other.m_UseTrueSdf);
		std::swap(m_SecondaryMask, Other.m_SecondaryMask);
	}
	bool IsReady() const { return m_Texture.IsValid() && m_LoadedIconCount == static_cast<int>(EQmIcon::COUNT); }
	int LoadedIconCount() const { return m_LoadedIconCount; }
	int Width() const { return m_Width; }
	int Height() const { return m_Height; }
	bool HasSecondaryMask() const { return m_SecondaryMask; }

private:
	friend class CQmIconManager;

	IGraphics::CTextureHandle m_Texture;
	std::array<SEntry, static_cast<size_t>(EQmIcon::COUNT)> m_aEntries{};
	// 眼睛 morph 的预烘焙 MSDF 关键帧（仅 Bold 图集提供；缺失时为 0，运行时会回退）。
	std::array<SEntry, static_cast<size_t>(MORPH_FRAME_CAPACITY)> m_aMorphFrames{};
	int m_MorphFrameCount = 0;
	int m_LoadedIconCount = 0;
	int m_Width = 0;
	int m_Height = 0;
	float m_PxRange = 0.0f;
	bool m_UseTrueSdf = false;
	bool m_SecondaryMask = false;
};

class CQmIconManager
{
public:
	void Init(IGraphics *pGraphics, IStorage *pStorage, IConsole *pConsole);
	void Shutdown();
	/**
	 * 图形设备重建后调用：图集纹理已随设备消失，清掉图集状态并立刻重建。
	 * 旧的纹理句柄因为设备纪元自增已经失效，不需要再发删除命令。
	 */
	void OnGraphicsResourcesReset();
	bool Reload();
	void RefreshForCurrentDpi();
	bool IsReady() const { return m_Atlas.IsReady(); }
	// 图标只有 MTSDF 一条图集路径；图集不可用（后端无 MSDF 或资源缺失）时
	// 调用方应直接走 TTF 字形兜底，不再有位图图集中间层。
	bool PreferFontFallback() const { return !IsReady(); }
	int LoadedIconCount() const { return m_Atlas.LoadedIconCount(); }
	SQmIconDiagnostics TakeDiagnostics() const;
	// 仅计数实际提交的字体兜底绘制，关闭诊断时不维护每帧计数。
	void RecordFontFallback(int Count = 1) const
	{
		if(m_DiagnosticsEnabled && Count > 0)
			m_Diagnostics.m_FontFallbackDraws += static_cast<uint64_t>(Count);
	}

	// PreserveAspect=false 仅供刻意的各向异性动画使用（例如观战眼睛的纵向压扁展开）；
	// 默认等比，避免图标按字形宽高比被拉伸。
	bool RenderIcon(EQmIcon Icon, const CUIRect &Rect, const ColorRGBA &Color, bool PreserveAspect = true) const;
	bool RenderIconRotated(EQmIcon Icon, const CUIRect &Rect, const ColorRGBA &Color, float Rotation, bool PreserveAspect = true) const;
	bool RenderIcon(EQmIcon Icon, const CUIRect &Rect, EQmIconState State, const SQmIconStyle &Style = SQmIconStyle(), bool PreserveAspect = true) const;
	// 眼睛 morph：按进度混合相邻两张预烘焙 MSDF 关键帧（抗锯齿、不依赖 FSAA）。
	// 图集未提供关键帧时返回 false，调用方退回几何 morph / 交叉淡化。
	bool RenderMorphFrames(const CUIRect &Rect, const ColorRGBA &Color, float Progress) const;
	bool HasMorphFrames() const { return m_Atlas.m_MorphFrameCount > 0; }

	static const char *IconName(EQmIcon Icon)
	{
		switch(Icon)
		{
#define QM_ICON_ENTRY(Id, Name, Codepoint) \
	case EQmIcon::Id: return Name;
#define QM_ICON_ALIAS QM_ICON_ENTRY
#include "qm_icon_registry.inc"
#undef QM_ICON_ENTRY
#undef QM_ICON_ALIAS
		default: return "";
		}
	}

	// 仅接受一个完整字形，正文与混合文本不进入图标绘制路径。
	static EQmIcon IconFromGlyph(const char *pText, int Length = -1)
	{
		if(pText == nullptr || Length == 0)
			return EQmIcon::COUNT;
		char aGlyph[5];
		if(Length >= 0)
		{
			if(Length > 4)
				return EQmIcon::COUNT;
			mem_copy(aGlyph, pText, Length);
			aGlyph[Length] = '\0';
			pText = aGlyph;
		}
		if(!pText[0])
			return EQmIcon::COUNT;
		const char *pEnd = pText;
		const int Codepoint = str_utf8_decode(&pEnd);
		if((Length < 0 && *pEnd != '\0') || (Length >= 0 && pEnd - pText != Length))
			return EQmIcon::COUNT;
		switch(Codepoint)
		{
#define QM_ICON_ENTRY(Id, Name, Codepoint) \
	case Codepoint: return EQmIcon::Id;
#define QM_ICON_ALIAS(Id, Name, Codepoint)
#include "qm_icon_registry.inc"
#undef QM_ICON_ENTRY
#undef QM_ICON_ALIAS
		default: return EQmIcon::COUNT;
		}
	}

private:
	bool LoadMsdfManifest(CQmIconAtlas &Atlas);
	// 图集任意条目（图标或 morph 关键帧）的 MSDF 绘制，统一等比适配。
	bool RenderAtlasEntry(const CQmIconAtlas::SEntry &Entry, const CUIRect &Rect, const ColorRGBA &Color, bool PreserveAspect, float Rotation = 0.0f) const;
	CUIRect PixelAlignedRect(const CUIRect &Rect) const;
	void ClearAtlas(CQmIconAtlas &Atlas);
	void FinishMsdfManagerCallRun() const;

	IGraphics *m_pGraphics = nullptr;
	IStorage *m_pStorage = nullptr;
	IConsole *m_pConsole = nullptr;
	CQmIconAtlas m_Atlas;
	int m_AtlasWeight = -1;
	int64_t m_NextReloadAttemptTime = 0;
	int m_FailedReloadWeight = -1;
	bool m_FailedReloadMsdfSupported = false;
	bool m_HasFailedReloadTarget = false;
	mutable SQmIconDiagnostics m_Diagnostics;
	bool m_DiagnosticsEnabled = false;
	mutable uint64_t m_CurrentMsdfManagerCallRun = 0;
};

#endif
