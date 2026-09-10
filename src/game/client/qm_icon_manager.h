// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QM_ICON_MANAGER_H
#define GAME_CLIENT_QM_ICON_MANAGER_H

#include <base/color.h>

#include <engine/graphics.h>

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
	STAR = 0,
	BOOKMARK,
	SEARCH,
	CLOSE,
	EYE,
	EYE_OFF,
	CHEVRON_DOWN,
	PLUS,
	TRASH,
	SATELLITE_SWAP_INCOMING,
	SATELLITE_SWAP_OUTGOING,
	SATELLITE_SWITCH,
	SATELLITE_MUTE,
	SATELLITE_CHECK,
	SATELLITE_SPECTATOR_EYE,
	SATELLITE_SPECTATOR_EYE_CLOSED,
	COUNT,
};

enum class EQmIconState
{
	NORMAL = 0,
	HOVER,
	ACTIVE,
	DISABLED,
};

enum class EQmIconAtlasType
{
	ALPHA,
	MSDF,
};

enum class EQmIconRefreshAction
{
	NONE,
	RELOAD,
	RETRY_MSDF,
};

struct SQmIconRefreshState
{
	bool m_NeedsReload = false;
	bool m_ReloadCooldownActive = false;
	bool m_NeedsMsdfProbe = false;
	bool m_MsdfProbeCooldownActive = false;
};

inline EQmIconAtlasType SelectQmIconAtlasType(const bool MsdfSupported, const bool MsdfAvailable)
{
	return MsdfSupported && MsdfAvailable ? EQmIconAtlasType::MSDF : EQmIconAtlasType::ALPHA;
}

inline bool QmIconAtlasNeedsReload(const bool IsReady, const EQmIconAtlasType LoadedType, const EQmIconAtlasType DesiredType, const int LoadedWeight, const int DesiredWeight, const int LoadedScale, const int DesiredScale)
{
	return !IsReady || LoadedType != DesiredType || LoadedWeight != DesiredWeight || (DesiredType == EQmIconAtlasType::ALPHA && LoadedScale != DesiredScale);
}

inline bool QmIconAtlasRetryCooldownActive(const int64_t Now, const int64_t RetryDeadline)
{
	return Now < RetryDeadline;
}

inline bool QmIconReloadCooldownActive(const int64_t Now, const int64_t RetryDeadline, const bool HasFailedTarget, const int FailedWeight, const int FailedScale, const bool FailedMsdfSupported, const int DesiredWeight, const int DesiredScale, const bool MsdfSupported)
{
	return HasFailedTarget && Now < RetryDeadline && FailedWeight == DesiredWeight && FailedScale == DesiredScale && FailedMsdfSupported == MsdfSupported;
}

inline EQmIconRefreshAction QmIconRefreshAction(const SQmIconRefreshState &State)
{
	if(State.m_NeedsReload)
		return State.m_ReloadCooldownActive ? EQmIconRefreshAction::NONE : EQmIconRefreshAction::RELOAD;
	if(State.m_NeedsMsdfProbe && !State.m_MsdfProbeCooldownActive)
		return EQmIconRefreshAction::RETRY_MSDF;
	return EQmIconRefreshAction::NONE;
}

inline EQmIconRefreshAction QmIconRefreshAction(const bool NeedsReload, const bool ReloadCooldownActive, const bool NeedsMsdfProbe, const bool MsdfProbeCooldownActive)
{
	return QmIconRefreshAction({NeedsReload, ReloadCooldownActive, NeedsMsdfProbe, MsdfProbeCooldownActive});
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
	uint64_t m_AlphaIconDraws = 0;
	uint64_t m_MsdfIconDraws = 0;
	uint64_t m_MaxMsdfManagerCallRun = 0;
	std::array<uint64_t, MSDF_RUN_BUCKET_COUNT> m_MsdfManagerCallRunBuckets{};
	uint64_t m_ReloadAttempts = 0;
	uint64_t m_ReloadSuccesses = 0;
	uint64_t m_AtlasSwaps = 0;
	uint64_t m_MsdfProbes = 0;
	uint64_t m_MsdfProbeSuccesses = 0;
	uint64_t m_TextureLoads = 0;
	uint64_t m_TextureLoadFailures = 0;
	uint64_t m_TextureUnloads = 0;
};

inline bool QmIconTextureCanCommit(const bool IsValid, const bool IsNullTexture)
{
	return IsValid && !IsNullTexture;
}

inline bool QmIconAtlasCanRetainOnReloadFailure(const bool IsReady, const EQmIconAtlasType LoadedType, const bool MsdfSupported)
{
	return IsReady && (LoadedType != EQmIconAtlasType::MSDF || MsdfSupported);
}

inline bool QmIconAtlasMustDropMsdf(const bool MsdfSupported, const EQmIconAtlasType LoadedType)
{
	return !MsdfSupported && LoadedType == EQmIconAtlasType::MSDF;
}

inline bool QmIconAtlasNeedsMsdfProbe(const bool MsdfSupported, const bool MsdfManifestAvailable)
{
	return MsdfSupported && !MsdfManifestAvailable;
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

inline float QmIconPixelScale(const int DrawableExtent, const float LogicalExtent)
{
	return DrawableExtent > 0 && LogicalExtent > 0.0f ? DrawableExtent / LogicalExtent : 0.0f;
}

inline int NormalizeQmIconWeight(const int Weight)
{
	return Weight >= 0 && Weight <= 3 ? Weight : 1;
}

inline bool QmIconWeightUsesBoldFontFallback(const int Weight)
{
	return NormalizeQmIconWeight(Weight) == 1;
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

ColorRGBA ConfiguredQmUiIconColor(const ColorRGBA &Color);

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
	enum class EType
	{
		ALPHA,
		MSDF,
	};

	struct SEntry
	{
		bool m_Valid = false;
		float m_U0 = 0.0f;
		float m_V0 = 0.0f;
		float m_U1 = 1.0f;
		float m_V1 = 1.0f;
	};

	void Clear(IGraphics *pGraphics);
	// 图形设备重建后使用：纹理已随设备消失，只清本地状态，绝不对旧句柄发删除命令。
	void ResetForDeviceRecreate();
	void Swap(CQmIconAtlas &Other)
	{
		std::swap(m_Texture, Other.m_Texture);
		std::swap(m_aEntries, Other.m_aEntries);
		std::swap(m_LoadedIconCount, Other.m_LoadedIconCount);
		std::swap(m_AtlasScale, Other.m_AtlasScale);
		std::swap(m_Width, Other.m_Width);
		std::swap(m_Height, Other.m_Height);
		std::swap(m_Padding, Other.m_Padding);
		std::swap(m_PxRange, Other.m_PxRange);
		std::swap(m_Type, Other.m_Type);
	}
	bool IsReady() const { return m_Texture.IsValid() && m_LoadedIconCount == static_cast<int>(EQmIcon::COUNT); }
	int LoadedIconCount() const { return m_LoadedIconCount; }
	int AtlasScale() const { return m_AtlasScale; }
	int Width() const { return m_Width; }
	int Height() const { return m_Height; }
	int Padding() const { return m_Padding; }
	bool IsMsdf() const { return m_Type == EType::MSDF; }
	EQmIconAtlasType Type() const { return IsMsdf() ? EQmIconAtlasType::MSDF : EQmIconAtlasType::ALPHA; }

private:
	friend class CQmIconManager;

	IGraphics::CTextureHandle m_Texture;
	std::array<SEntry, static_cast<size_t>(EQmIcon::COUNT)> m_aEntries{};
	int m_LoadedIconCount = 0;
	int m_AtlasScale = 0;
	int m_Width = 0;
	int m_Height = 0;
	int m_Padding = 0;
	float m_PxRange = 0.0f;
	EType m_Type = EType::ALPHA;
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
	int LoadedIconCount() const { return m_Atlas.LoadedIconCount(); }
	int AtlasScale() const { return m_Atlas.AtlasScale(); }
	SQmIconDiagnostics TakeDiagnostics() const;

	bool RenderIcon(EQmIcon Icon, const CUIRect &Rect, const ColorRGBA &Color) const;
	bool RenderIconRotated(EQmIcon Icon, const CUIRect &Rect, const ColorRGBA &Color, float Rotation) const;
	bool RenderIcon(EQmIcon Icon, const CUIRect &Rect, EQmIconState State, const SQmIconStyle &Style = SQmIconStyle()) const;

	static const char *IconName(EQmIcon Icon)
	{
		switch(Icon)
		{
		case EQmIcon::STAR: return "star";
		case EQmIcon::BOOKMARK: return "bookmark";
		case EQmIcon::SEARCH: return "magnifying-glass";
		case EQmIcon::CLOSE: return "close";
		case EQmIcon::EYE: return "eye";
		case EQmIcon::EYE_OFF: return "eye-off";
		case EQmIcon::CHEVRON_DOWN: return "chevron-down";
		case EQmIcon::PLUS: return "plus";
		case EQmIcon::TRASH: return "trash";
		case EQmIcon::SATELLITE_SWAP_INCOMING: return "satellite-swap-incoming";
		case EQmIcon::SATELLITE_SWAP_OUTGOING: return "satellite-swap-outgoing";
		case EQmIcon::SATELLITE_SWITCH: return "satellite-switch";
		case EQmIcon::SATELLITE_MUTE: return "satellite-mute";
		case EQmIcon::SATELLITE_CHECK: return "satellite-check";
		case EQmIcon::SATELLITE_SPECTATOR_EYE: return "satellite-spectator-eye";
		case EQmIcon::SATELLITE_SPECTATOR_EYE_CLOSED: return "satellite-spectator-eye-closed";
		case EQmIcon::COUNT: break;
		}
		return "";
	}

private:
	bool RetryMsdfAtlas();
	bool LoadManifest(CQmIconAtlas &Atlas, const char *pManifestPath, int Scale, bool Msdf);
	bool LoadMsdfManifest(CQmIconAtlas &Atlas);
	bool LoadManifestForScale(CQmIconAtlas &Atlas, int Scale);
	int PreferredAtlasScale() const;
	CUIRect PixelAlignedRect(const CUIRect &Rect) const;
	void ClearAtlas(CQmIconAtlas &Atlas);
	void FinishMsdfManagerCallRun() const;

	IGraphics *m_pGraphics = nullptr;
	IStorage *m_pStorage = nullptr;
	IConsole *m_pConsole = nullptr;
	CQmIconAtlas m_Atlas;
	int m_PreferredScale = 0;
	int m_AtlasWeight = -1;
	bool m_MsdfManifestAvailable = false;
	int64_t m_NextReloadAttemptTime = 0;
	int64_t m_NextMsdfProbeTime = 0;
	int m_FailedReloadWeight = -1;
	int m_FailedReloadScale = 0;
	bool m_FailedReloadMsdfSupported = false;
	bool m_HasFailedReloadTarget = false;
	mutable SQmIconDiagnostics m_Diagnostics;
	bool m_DiagnosticsEnabled = false;
	mutable uint64_t m_CurrentMsdfManagerCallRun = 0;
};

#endif
