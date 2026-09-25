// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "qm_icon_manager.h"

#include <base/system.h>

#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <algorithm>
#include <cmath>

namespace
{
	constexpr const char *QM_ICON_MSDF_MANIFEST_PATTERN = "qmclient/icons/qm_icons_%s_msdf.json";
	constexpr int QM_ICON_RELOAD_RETRY_DELAY_SECONDS = 2;

	bool IconDiagnosticsEnabled()
	{
		return g_Config.m_QmPerfDebug != 0;
	}

	const char *IconAtlasWeightName(const int Weight)
	{
		switch(NormalizeQmIconWeight(Weight))
		{
		case 0: return "regular";
		case 1: return "bold";
		// Thin 字体未随包：weight 2 复用 Light 图集（配置值保留兼容）。
		case 2: return "light";
		case 3: return "fill";
		case 4: return "light";
		case 5: return "duotone";
		}
		return "bold";
	}

	EQmIcon IconFromName(const char *pName)
	{
		for(int IconIndex = 0; IconIndex < static_cast<int>(EQmIcon::COUNT); ++IconIndex)
		{
			const EQmIcon Icon = static_cast<EQmIcon>(IconIndex);
			const char *pIconName = CQmIconManager::IconName(Icon);
			if(pIconName[0] != '\0' && str_comp(pName, pIconName) == 0)
				return Icon;
		}
		// 兼容历史 manifest 名
		if(str_comp(pName, "search") == 0)
			return EQmIcon::SEARCH;
		if(str_comp(pName, "close") == 0)
			return EQmIcon::CLOSE;
		if(str_comp(pName, "eye-off") == 0)
			return EQmIcon::EYE_OFF;
		return EQmIcon::COUNT;
	}

	bool JsonIntField(const json_value *pObject, const char *pName, int &Out)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		if(pValue == &json_value_none || pValue->type != json_integer)
			return false;
		Out = static_cast<int>(pValue->u.integer);
		return true;
	}

	const char *JsonStringField(const json_value *pObject, const char *pName)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		if(pValue == &json_value_none || pValue->type != json_string)
			return "";
		return pValue->u.string.ptr;
	}

	void LogIconAtlas(IConsole *pConsole, const char *pText)
	{
		if(pConsole != nullptr)
			pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "qm_icons", pText);
	}

	void FillEntryUv(CQmIconAtlas::SEntry &Entry, const int X, const int Y, const int W, const int H, const int AtlasWidth, const int AtlasHeight)
	{
		Entry.m_Valid = true;
		Entry.m_U0 = (X + 0.5f) / static_cast<float>(AtlasWidth);
		Entry.m_V0 = (Y + 0.5f) / static_cast<float>(AtlasHeight);
		Entry.m_U1 = (X + W - 0.5f) / static_cast<float>(AtlasWidth);
		Entry.m_V1 = (Y + H - 0.5f) / static_cast<float>(AtlasHeight);
		Entry.m_BoxW = W;
		Entry.m_BoxH = H;
	}

	// 解析 morph 关键帧段。缺省或任一帧非法都返回 0（全有或全无），
	// 调用方据此回退到几何 morph / 交叉淡化，不会出现半套帧的错乱动画。
	int ParseMorphFrames(const json_value *pRoot, const int AtlasWidth, const int AtlasHeight, std::array<CQmIconAtlas::SEntry, static_cast<size_t>(CQmIconAtlas::MORPH_FRAME_CAPACITY)> &aOut)
	{
		const json_value *pFrames = json_object_get(pRoot, "morph_frames");
		if(pFrames == &json_value_none || pFrames->type != json_array)
			return 0;

		int Count = 0;
		for(unsigned int Index = 0; Index < pFrames->u.array.length; ++Index)
		{
			const json_value *pFrame = pFrames->u.array.values[Index];
			if(pFrame == nullptr || pFrame->type != json_object || Count >= CQmIconAtlas::MORPH_FRAME_CAPACITY)
				return 0;
			int X = 0;
			int Y = 0;
			int W = 0;
			int H = 0;
			if(!JsonIntField(pFrame, "x", X) || !JsonIntField(pFrame, "y", Y) || !JsonIntField(pFrame, "w", W) || !JsonIntField(pFrame, "h", H) ||
				X < 0 || Y < 0 || W <= 1 || H <= 1 || X + W > AtlasWidth || Y + H > AtlasHeight)
				return 0;
			FillEntryUv(aOut[Count], X, Y, W, H, AtlasWidth, AtlasHeight);
			++Count;
		}
		return Count;
	}
}

ColorRGBA SQmIconStyle::Color(EQmIconState State) const
{
	switch(State)
	{
	case EQmIconState::NORMAL: return m_Normal;
	case EQmIconState::HOVER: return m_Hover;
	case EQmIconState::ACTIVE: return m_Active;
	case EQmIconState::DISABLED: return m_Disabled;
	}
	return m_Normal;
}

void CQmIconAtlas::Clear(IGraphics *pGraphics)
{
	if(m_Texture.IsValid() && pGraphics != nullptr)
		pGraphics->UnloadTexture(&m_Texture);
	m_Texture = IGraphics::CTextureHandle();
	ResetForDeviceRecreate();
}

void CQmIconAtlas::ResetForDeviceRecreate()
{
	// 纹理对象已经随设备消失，旧句柄也已因设备纪元自增而失效，
	// 因此这里只丢弃本地状态，不做任何 GPU 侧操作。
	m_Texture.Invalidate();
	for(SEntry &Entry : m_aEntries)
		Entry = {};
	for(SEntry &Entry : m_aMorphFrames)
		Entry = {};
	m_MorphFrameCount = 0;
	m_LoadedIconCount = 0;
	m_Width = 0;
	m_Height = 0;
	m_PxRange = 0.0f;
	m_UseTrueSdf = false;
	m_SecondaryMask = false;
}

void CQmIconManager::Init(IGraphics *pGraphics, IStorage *pStorage, IConsole *pConsole)
{
	if(m_pGraphics != nullptr)
		Shutdown();
	m_pGraphics = pGraphics;
	m_pStorage = pStorage;
	m_pConsole = pConsole;
	m_DiagnosticsEnabled = IconDiagnosticsEnabled();
	Reload();
}

void CQmIconManager::OnGraphicsResourcesReset()
{
	if(m_pGraphics == nullptr)
		return;

	// 旧的图集纹理已经随设备消失，句柄也已失效：只清本地状态，不再对它发删除命令。
	m_Atlas.ResetForDeviceRecreate();
	m_AtlasWeight = -1;
	m_NextReloadAttemptTime = 0;
	m_HasFailedReloadTarget = false;
	Reload();
}

void CQmIconManager::Shutdown()
{
	if(m_pGraphics != nullptr)
		ClearAtlas(m_Atlas);
	m_pGraphics = nullptr;
	m_pStorage = nullptr;
	m_pConsole = nullptr;
	m_AtlasWeight = -1;
	m_NextReloadAttemptTime = 0;
	m_FailedReloadWeight = -1;
	m_FailedReloadMsdfSupported = false;
	m_HasFailedReloadTarget = false;
	m_Diagnostics = {};
	m_DiagnosticsEnabled = false;
	m_CurrentMsdfManagerCallRun = 0;
}

void CQmIconManager::ClearAtlas(CQmIconAtlas &Atlas)
{
	if(m_DiagnosticsEnabled)
		FinishMsdfManagerCallRun();
	if(m_DiagnosticsEnabled && Atlas.m_Texture.IsValid() && !Atlas.m_Texture.IsNullTexture())
		m_Diagnostics.m_TextureUnloads++;
	Atlas.Clear(m_pGraphics);
}

void CQmIconManager::FinishMsdfManagerCallRun() const
{
	if(m_CurrentMsdfManagerCallRun == 0)
		return;
	m_Diagnostics.m_MsdfManagerCallRunBuckets[QmIconMsdfRunBucket(m_CurrentMsdfManagerCallRun)]++;
	m_CurrentMsdfManagerCallRun = 0;
}

SQmIconDiagnostics CQmIconManager::TakeDiagnostics() const
{
	if(!m_DiagnosticsEnabled)
	{
		m_Diagnostics = {};
		m_CurrentMsdfManagerCallRun = 0;
		return {};
	}
	FinishMsdfManagerCallRun();
	const SQmIconDiagnostics Diagnostics = m_Diagnostics;
	m_Diagnostics = {};
	return Diagnostics;
}

bool CQmIconManager::Reload()
{
	if(m_pGraphics == nullptr || m_pStorage == nullptr)
	{
		return false;
	}
	if(m_DiagnosticsEnabled)
		m_Diagnostics.m_ReloadAttempts++;

	const bool MsdfSupported = m_pGraphics->HasTexturedMsdf();
	const int Weight = NormalizeQmIconWeight(g_Config.m_QmUiIconWeight);
	CQmIconAtlas Candidate;
	const bool Success = MsdfSupported && LoadMsdfManifest(Candidate);
	if(!Success)
	{
		// 图集不可用（后端无 MSDF 或 manifest/贴图缺失）：没有位图中间层，
		// 直接清空图集交给调用方走 TTF 字形兜底；冷却期内不重试 IO。
		m_NextReloadAttemptTime = time_get() + time_freq() * QM_ICON_RELOAD_RETRY_DELAY_SECONDS;
		m_FailedReloadWeight = Weight;
		m_FailedReloadMsdfSupported = MsdfSupported;
		m_HasFailedReloadTarget = true;
		if(IsReady())
			ClearAtlas(m_Atlas);
		return false;
	}

	m_Atlas.Swap(Candidate);
	ClearAtlas(Candidate);
	if(m_DiagnosticsEnabled)
	{
		m_Diagnostics.m_ReloadSuccesses++;
		m_Diagnostics.m_AtlasSwaps++;
	}
	m_AtlasWeight = Weight;
	m_NextReloadAttemptTime = 0;
	m_HasFailedReloadTarget = false;
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "MTSDF icon atlas ready: weight=%s icons=%d", IconAtlasWeightName(Weight), m_Atlas.m_LoadedIconCount);
	LogIconAtlas(m_pConsole, aBuf);
	return true;
}

bool CQmIconManager::LoadMsdfManifest(CQmIconAtlas &Atlas)
{
	char aManifestPath[IO_MAX_PATH_LENGTH];
	str_format(aManifestPath, sizeof(aManifestPath), QM_ICON_MSDF_MANIFEST_PATTERN, IconAtlasWeightName(NormalizeQmIconWeight(g_Config.m_QmUiIconWeight)));

	ClearAtlas(Atlas);
	if(!m_pStorage->FileExists(aManifestPath, IStorage::TYPE_ALL))
		return false;

	void *pFileData = nullptr;
	unsigned FileSize = 0;
	if(!m_pStorage->ReadFile(aManifestPath, IStorage::TYPE_ALL, &pFileData, &FileSize))
		return false;

	char aError[256] = "";
	json_settings Settings{};
	json_value *pRoot = JsonParseEx(&Settings, static_cast<json_char *>(pFileData), FileSize, aError);
	free(pFileData);
	if(pRoot == nullptr)
	{
		char aBuf[320];
		str_format(aBuf, sizeof(aBuf), "Failed to parse %s: %s", aManifestPath, aError);
		LogIconAtlas(m_pConsole, aBuf);
		return false;
	}

	bool Success = false;
	do
	{
		int PxRange = 0;
		const char *pKind = JsonStringField(pRoot, "kind");
		if(str_comp(pKind, "mtsdf") != 0 || json_object_get(pRoot, "alpha_sdf") == &json_value_none || !JsonIntField(pRoot, "px_range", PxRange) || PxRange <= 0)
			break;

		const json_value *pAtlas = json_object_get(pRoot, "atlas");
		const json_value *pIcons = json_object_get(pRoot, "icons");
		if(pAtlas == &json_value_none || pAtlas->type != json_object || pIcons == &json_value_none || pIcons->type != json_object)
			break;

		int AtlasWidth = 0;
		int AtlasHeight = 0;
		if(!JsonIntField(pAtlas, "width", AtlasWidth) || !JsonIntField(pAtlas, "height", AtlasHeight) || AtlasWidth <= 0 || AtlasHeight <= 0)
			break;

		const char *pImagePath = JsonStringField(pAtlas, "image");
		if(pImagePath[0] == '\0')
			break;

		std::array<CQmIconAtlas::SEntry, static_cast<size_t>(EQmIcon::COUNT)> aEntries{};
		int LoadedIconCount = 0;
		bool InvalidKnownEntry = false;
		for(unsigned int IconIndex = 0; IconIndex < pIcons->u.object.length; ++IconIndex)
		{
			const auto &JsonIcon = pIcons->u.object.values[IconIndex];
			const EQmIcon Icon = IconFromName(JsonIcon.name);
			if(Icon == EQmIcon::COUNT || JsonIcon.value == nullptr || JsonIcon.value->type != json_object)
			{
				if(Icon != EQmIcon::COUNT)
					InvalidKnownEntry = true;
				continue;
			}

			int X = 0;
			int Y = 0;
			int W = 0;
			int H = 0;
			if(!JsonIntField(JsonIcon.value, "x", X) || !JsonIntField(JsonIcon.value, "y", Y) ||
				!JsonIntField(JsonIcon.value, "w", W) || !JsonIntField(JsonIcon.value, "h", H) ||
				X < 0 || Y < 0 || W <= 1 || H <= 1 || X + W > AtlasWidth || Y + H > AtlasHeight)
			{
				InvalidKnownEntry = true;
				continue;
			}

			CQmIconAtlas::SEntry &Entry = aEntries[static_cast<size_t>(Icon)];
			if(Entry.m_Valid)
			{
				InvalidKnownEntry = true;
				continue;
			}
			FillEntryUv(Entry, X, Y, W, H, AtlasWidth, AtlasHeight);
			++LoadedIconCount;
		}

		if(InvalidKnownEntry || LoadedIconCount != static_cast<int>(EQmIcon::COUNT))
			break;

		std::array<CQmIconAtlas::SEntry, static_cast<size_t>(CQmIconAtlas::MORPH_FRAME_CAPACITY)> aMorphFrames{};
		const int MorphFrameCount = ParseMorphFrames(pRoot, AtlasWidth, AtlasHeight, aMorphFrames);

		IGraphics::CTextureHandle Texture = m_pGraphics->LoadTexture(pImagePath, IStorage::TYPE_ALL, IGraphics::TEXLOAD_NO_MIPMAPS);
		if(!QmIconTextureCanCommit(Texture.IsValid(), Texture.IsNullTexture()))
		{
			if(m_DiagnosticsEnabled)
				m_Diagnostics.m_TextureLoadFailures++;
			if(Texture.IsValid())
			{
				if(m_DiagnosticsEnabled && !Texture.IsNullTexture())
					m_Diagnostics.m_TextureUnloads++;
				m_pGraphics->UnloadTexture(&Texture);
			}
			break;
		}

		Atlas.m_Texture = Texture;
		Atlas.m_aEntries = aEntries;
		Atlas.m_aMorphFrames = aMorphFrames;
		Atlas.m_MorphFrameCount = MorphFrameCount;
		Atlas.m_LoadedIconCount = LoadedIconCount;
		Atlas.m_Width = AtlasWidth;
		Atlas.m_Height = AtlasHeight;
		Atlas.m_PxRange = static_cast<float>(PxRange);
		Atlas.m_UseTrueSdf = json_object_get(pRoot, "alpha_sdf") != &json_value_none;
		Atlas.m_SecondaryMask = json_object_get(pRoot, "secondary_mask") != &json_value_none;
		if(m_DiagnosticsEnabled)
			m_Diagnostics.m_TextureLoads++;
		Success = true;
	} while(false);

	json_value_free(pRoot);
	if(!Success)
		ClearAtlas(Atlas);
	return Success;
}

void CQmIconManager::RefreshForCurrentDpi()
{
	if(m_pGraphics == nullptr || m_pStorage == nullptr)
		return;
	const bool DiagnosticsEnabled = IconDiagnosticsEnabled();
	if(DiagnosticsEnabled != m_DiagnosticsEnabled)
	{
		m_Diagnostics = {};
		m_CurrentMsdfManagerCallRun = 0;
		m_DiagnosticsEnabled = DiagnosticsEnabled;
	}

	const bool MsdfSupported = m_pGraphics->HasTexturedMsdf();
	const int Weight = NormalizeQmIconWeight(g_Config.m_QmUiIconWeight);
	// 能力丢失（如切到无 MSDF 的后端）时立即清空图集，交给字体兜底。
	if(!MsdfSupported && IsReady())
	{
		ClearAtlas(m_Atlas);
		m_AtlasWeight = Weight;
		m_HasFailedReloadTarget = false;
		m_NextReloadAttemptTime = 0;
		return;
	}

	const int64_t Now = time_get();
	const bool ReloadCooldownActive = QmIconReloadCooldownActive(Now, m_NextReloadAttemptTime, m_HasFailedReloadTarget, m_FailedReloadWeight, m_FailedReloadMsdfSupported, Weight, MsdfSupported);
	if(QmIconAtlasNeedsReload(IsReady(), m_AtlasWeight, Weight) && !ReloadCooldownActive)
		Reload();
}

CUIRect CQmIconManager::PixelAlignedRect(const CUIRect &Rect) const
{
	if(m_pGraphics == nullptr)
		return Rect;

	float ScreenX0 = 0.0f;
	float ScreenY0 = 0.0f;
	float ScreenX1 = 0.0f;
	float ScreenY1 = 0.0f;
	m_pGraphics->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float ScaleX = QmIconPixelScale(m_pGraphics->ScreenWidth(), ScreenX1 - ScreenX0);
	const float ScaleY = QmIconPixelScale(m_pGraphics->ScreenHeight(), ScreenY1 - ScreenY0);
	if(ScaleX <= 0.0f || ScaleY <= 0.0f)
		return Rect;

	CUIRect Out = Rect;
	const float X0 = std::round(Rect.x * ScaleX) / ScaleX;
	const float Y0 = std::round(Rect.y * ScaleY) / ScaleY;
	const float X1 = std::round((Rect.x + Rect.w) * ScaleX) / ScaleX;
	const float Y1 = std::round((Rect.y + Rect.h) * ScaleY) / ScaleY;
	Out.x = X0;
	Out.y = Y0;
	Out.w = std::max(1.0f / ScaleX, X1 - X0);
	Out.h = std::max(1.0f / ScaleY, Y1 - Y0);
	return Out;
}

bool CQmIconManager::RenderAtlasEntry(const CQmIconAtlas::SEntry &Entry, const CUIRect &Rect, const ColorRGBA &Color, const bool PreserveAspect, const float Rotation) const
{
	if(!Entry.m_Valid || Color.a <= 0.0f)
		return false;

	const CUIRect Aligned = PreserveAspect ? QmIconAspectFittedRect(PixelAlignedRect(Rect), Entry.m_BoxW, Entry.m_BoxH) : PixelAlignedRect(Rect);
	if(m_DiagnosticsEnabled)
	{
		m_Diagnostics.m_MsdfIconDraws++;
		m_CurrentMsdfManagerCallRun++;
		m_Diagnostics.m_MaxMsdfManagerCallRun = maximum(m_Diagnostics.m_MaxMsdfManagerCallRun, m_CurrentMsdfManagerCallRun);
	}
	IGraphics::STexturedMsdfParams Params;
	Params.m_Texture = m_Atlas.m_Texture;
	Params.m_Rect = vec4(Aligned.x, Aligned.y, Aligned.w, Aligned.h);
	Params.m_UvRect = vec4(Entry.m_U0, Entry.m_V0, Entry.m_U1, Entry.m_V1);
	Params.m_Color = Color;
	Params.m_SecondaryColor = ConfiguredQmUiIconSecondaryColor(Color);
	Params.m_PxRange = m_Atlas.m_PxRange;
	Params.m_AtlasWidth = static_cast<float>(m_Atlas.m_Width);
	Params.m_AtlasHeight = static_cast<float>(m_Atlas.m_Height);
	Params.m_UseTrueSdf = m_Atlas.m_UseTrueSdf && !m_Atlas.HasSecondaryMask();
	Params.m_UseSecondarySdf = m_Atlas.HasSecondaryMask();
	Params.m_Rotation = Rotation;
	m_pGraphics->RenderTexturedMsdf(Params);
	return true;
}

bool CQmIconManager::RenderIcon(EQmIcon Icon, const CUIRect &Rect, const ColorRGBA &Color, const bool PreserveAspect) const
{
	const size_t IconIndex = static_cast<size_t>(Icon);
	if(!IsReady() || IconIndex >= m_Atlas.m_aEntries.size())
		return false;
	return RenderAtlasEntry(m_Atlas.m_aEntries[IconIndex], Rect, Color, PreserveAspect);
}

bool CQmIconManager::RenderIconRotated(EQmIcon Icon, const CUIRect &Rect, const ColorRGBA &Color, float Rotation, const bool PreserveAspect) const
{
	const size_t IconIndex = static_cast<size_t>(Icon);
	if(!IsReady() || IconIndex >= m_Atlas.m_aEntries.size())
		return false;
	return RenderAtlasEntry(m_Atlas.m_aEntries[IconIndex], Rect, Color, PreserveAspect, Rotation);
}

bool CQmIconManager::RenderMorphFrames(const CUIRect &Rect, const ColorRGBA &Color, const float Progress) const
{
	const int FrameCount = m_Atlas.m_MorphFrameCount;
	if(FrameCount <= 0 || !IsReady() || Color.a <= 0.0f)
		return false;

	// 相邻两帧做 alpha 混合：预烘焙帧本身是 MSDF，形变与图标走同一条抗锯齿路径。
	const SQmIconMorphFrameBlend Blend = QmIconMorphFrameBlend(Progress, FrameCount);
	bool Drawn = false;
	if(Blend.m_Alpha0 > 0.001f)
		Drawn = RenderAtlasEntry(m_Atlas.m_aMorphFrames[Blend.m_Index0], Rect, ColorRGBA(Color.r, Color.g, Color.b, Color.a * Blend.m_Alpha0), true);
	if(Blend.m_Alpha1 > 0.001f)
		Drawn = RenderAtlasEntry(m_Atlas.m_aMorphFrames[Blend.m_Index1], Rect, ColorRGBA(Color.r, Color.g, Color.b, Color.a * Blend.m_Alpha1), true) || Drawn;
	return Drawn;
}

bool CQmIconManager::RenderIcon(EQmIcon Icon, const CUIRect &Rect, EQmIconState State, const SQmIconStyle &Style, const bool PreserveAspect) const
{
	return RenderIcon(Icon, Rect, Style.Color(State), PreserveAspect);
}
