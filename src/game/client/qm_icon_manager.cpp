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
	constexpr const char *QM_ICON_MANIFEST_PATTERN = "qmclient/icons/qm_icons_%s_%dx.json";
	constexpr const char *QM_ICON_MSDF_MANIFEST_PATTERN = "qmclient/icons/qm_icons_%s_msdf.json";
	constexpr int QM_ICON_RELOAD_RETRY_DELAY_SECONDS = 2;

	bool IconDiagnosticsEnabled()
	{
		return g_Config.m_QmPerfDebug != 0 || g_Config.m_QmPerfLogfile != 0 || g_Config.m_QmPerfStutterDiagnostics != 0;
	}

	const char *IconAtlasWeightName(const int Weight)
	{
		switch(NormalizeQmIconWeight(Weight))
		{
		case 0: return "regular";
		case 1: return "bold";
		case 2: return "thin";
		case 3: return "fill";
		}
		return "bold";
	}

	EQmIcon IconFromName(const char *pName)
	{
		if(str_comp(pName, "star") == 0)
			return EQmIcon::STAR;
		if(str_comp(pName, "bookmark") == 0)
			return EQmIcon::BOOKMARK;
		if(str_comp(pName, "magnifying-glass") == 0 || str_comp(pName, "search") == 0)
			return EQmIcon::SEARCH;
		if(str_comp(pName, "close") == 0)
			return EQmIcon::CLOSE;
		if(str_comp(pName, "eye") == 0)
			return EQmIcon::EYE;
		if(str_comp(pName, "eye-off") == 0)
			return EQmIcon::EYE_OFF;
		if(str_comp(pName, "chevron-down") == 0)
			return EQmIcon::CHEVRON_DOWN;
		if(str_comp(pName, "plus") == 0)
			return EQmIcon::PLUS;
		if(str_comp(pName, "trash") == 0)
			return EQmIcon::TRASH;
		if(str_comp(pName, "satellite-swap-incoming") == 0)
			return EQmIcon::SATELLITE_SWAP_INCOMING;
		if(str_comp(pName, "satellite-swap-outgoing") == 0)
			return EQmIcon::SATELLITE_SWAP_OUTGOING;
		if(str_comp(pName, "satellite-switch") == 0)
			return EQmIcon::SATELLITE_SWITCH;
		if(str_comp(pName, "satellite-mute") == 0)
			return EQmIcon::SATELLITE_MUTE;
		if(str_comp(pName, "satellite-check") == 0)
			return EQmIcon::SATELLITE_CHECK;
		if(str_comp(pName, "satellite-spectator-eye") == 0)
			return EQmIcon::SATELLITE_SPECTATOR_EYE;
		if(str_comp(pName, "satellite-spectator-eye-closed") == 0)
			return EQmIcon::SATELLITE_SPECTATOR_EYE_CLOSED;
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
}

ColorRGBA ConfiguredQmUiIconColor(const ColorRGBA &Color)
{
	if(g_Config.m_QmUiIconColor != 4)
		return QmUiIconColor(Color, g_Config.m_QmUiIconColor, g_Config.m_QmUiIconCustomColor);

	const float Time = static_cast<float>(time_get()) / static_cast<float>(time_freq());
	return QmUiIconColor(Color, g_Config.m_QmUiIconColor, g_Config.m_QmUiIconCustomColor, Time);
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
	m_LoadedIconCount = 0;
	m_AtlasScale = 0;
	m_Width = 0;
	m_Height = 0;
	m_Padding = 0;
	m_PxRange = 0.0f;
	m_Type = EType::ALPHA;
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
	m_NextMsdfProbeTime = 0;
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
	m_PreferredScale = 0;
	m_AtlasWeight = -1;
	m_MsdfManifestAvailable = false;
	m_NextReloadAttemptTime = 0;
	m_NextMsdfProbeTime = 0;
	m_FailedReloadWeight = -1;
	m_FailedReloadScale = 0;
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

	const int PreferredScale = PreferredAtlasScale();
	const bool MsdfSupported = m_pGraphics->HasTexturedMsdf();
	const int Weight = NormalizeQmIconWeight(g_Config.m_QmUiIconWeight);
	CQmIconAtlas Candidate;
	bool Success = false;
	bool LoadedMsdf = false;
	if(MsdfSupported)
	{
		if(m_DiagnosticsEnabled)
			m_Diagnostics.m_MsdfProbes++;
		if(LoadMsdfManifest(Candidate))
		{
			Success = true;
			LoadedMsdf = true;
			if(m_DiagnosticsEnabled)
				m_Diagnostics.m_MsdfProbeSuccesses++;
		}
	}
	if(!Success)
	{
		for(const int Scale : QmIconAtlasScaleFallbackOrder(PreferredScale))
		{
			if(LoadManifestForScale(Candidate, Scale))
			{
				Success = true;
				break;
			}
		}
	}

	const bool MsdfProbeFailed = MsdfSupported && !LoadedMsdf;
	if(!Success)
	{
		m_NextReloadAttemptTime = time_get() + time_freq() * QM_ICON_RELOAD_RETRY_DELAY_SECONDS;
		m_FailedReloadWeight = Weight;
		m_FailedReloadScale = PreferredScale;
		m_FailedReloadMsdfSupported = MsdfSupported;
		m_HasFailedReloadTarget = true;
		const bool RetainedResidentAtlas = QmIconAtlasCanRetainOnReloadFailure(IsReady(), m_Atlas.Type(), MsdfSupported);
		if(!RetainedResidentAtlas)
		{
			ClearAtlas(m_Atlas);
			m_MsdfManifestAvailable = false;
			m_PreferredScale = PreferredScale;
			m_AtlasWeight = Weight;
		}
		return false;
	}

	m_Atlas.Swap(Candidate);
	ClearAtlas(Candidate);
	if(m_DiagnosticsEnabled)
	{
		m_Diagnostics.m_ReloadSuccesses++;
		m_Diagnostics.m_AtlasSwaps++;
	}
	m_MsdfManifestAvailable = LoadedMsdf;
	m_PreferredScale = LoadedMsdf ? 0 : PreferredScale;
	m_AtlasWeight = Weight;
	m_NextReloadAttemptTime = 0;
	m_HasFailedReloadTarget = false;
	m_NextMsdfProbeTime = MsdfProbeFailed ? time_get() + time_freq() * QM_ICON_RELOAD_RETRY_DELAY_SECONDS : 0;
	return true;
}

bool CQmIconManager::RetryMsdfAtlas()
{
	if(m_DiagnosticsEnabled)
		m_Diagnostics.m_MsdfProbes++;
	CQmIconAtlas Candidate;
	if(!LoadMsdfManifest(Candidate))
	{
		m_MsdfManifestAvailable = false;
		m_NextMsdfProbeTime = time_get() + time_freq() * QM_ICON_RELOAD_RETRY_DELAY_SECONDS;
		return false;
	}

	m_Atlas.Swap(Candidate);
	ClearAtlas(Candidate);
	if(m_DiagnosticsEnabled)
	{
		m_Diagnostics.m_MsdfProbeSuccesses++;
		m_Diagnostics.m_AtlasSwaps++;
	}
	m_MsdfManifestAvailable = true;
	m_PreferredScale = 0;
	m_AtlasWeight = NormalizeQmIconWeight(g_Config.m_QmUiIconWeight);
	m_NextMsdfProbeTime = 0;
	return true;
}

bool CQmIconManager::LoadManifestForScale(CQmIconAtlas &Atlas, const int Scale)
{
	char aManifestPath[IO_MAX_PATH_LENGTH];
	str_format(aManifestPath, sizeof(aManifestPath), QM_ICON_MANIFEST_PATTERN, IconAtlasWeightName(NormalizeQmIconWeight(g_Config.m_QmUiIconWeight)), Scale);
	return LoadManifest(Atlas, aManifestPath, Scale, false);
}

bool CQmIconManager::LoadMsdfManifest(CQmIconAtlas &Atlas)
{
	char aManifestPath[IO_MAX_PATH_LENGTH];
	str_format(aManifestPath, sizeof(aManifestPath), QM_ICON_MSDF_MANIFEST_PATTERN, IconAtlasWeightName(NormalizeQmIconWeight(g_Config.m_QmUiIconWeight)));
	return LoadManifest(Atlas, aManifestPath, 0, true);
}

bool CQmIconManager::LoadManifest(CQmIconAtlas &Atlas, const char *pManifestPath, const int Scale, const bool Msdf)
{
	ClearAtlas(Atlas);
	if(!m_pStorage->FileExists(pManifestPath, IStorage::TYPE_ALL))
		return false;

	void *pFileData = nullptr;
	unsigned FileSize = 0;
	if(!m_pStorage->ReadFile(pManifestPath, IStorage::TYPE_ALL, &pFileData, &FileSize))
		return false;

	char aError[256] = "";
	json_settings Settings{};
	json_value *pRoot = JsonParseEx(&Settings, static_cast<json_char *>(pFileData), FileSize, aError);
	free(pFileData);
	if(pRoot == nullptr)
	{
		char aBuf[320];
		str_format(aBuf, sizeof(aBuf), "Failed to parse %s: %s", pManifestPath, aError);
		LogIconAtlas(m_pConsole, aBuf);
		return false;
	}

	bool Success = false;
	do
	{
		int PxRange = 0;
		if(Msdf)
		{
			const char *pKind = JsonStringField(pRoot, "kind");
			if(str_comp(pKind, "msdf") != 0 || !JsonIntField(pRoot, "px_range", PxRange) || PxRange <= 0)
				break;
		}

		const json_value *pAtlas = json_object_get(pRoot, "atlas");
		const json_value *pIcons = json_object_get(pRoot, "icons");
		if(pAtlas == &json_value_none || pAtlas->type != json_object || pIcons == &json_value_none || pIcons->type != json_object)
			break;

		int AtlasWidth = 0;
		int AtlasHeight = 0;
		if(!JsonIntField(pAtlas, "width", AtlasWidth) || !JsonIntField(pAtlas, "height", AtlasHeight) || AtlasWidth <= 0 || AtlasHeight <= 0)
			break;
		int AtlasPadding = 0;
		JsonIntField(pAtlas, "padding", AtlasPadding);

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
			Entry.m_Valid = true;
			Entry.m_U0 = (X + 0.5f) / static_cast<float>(AtlasWidth);
			Entry.m_V0 = (Y + 0.5f) / static_cast<float>(AtlasHeight);
			Entry.m_U1 = (X + W - 0.5f) / static_cast<float>(AtlasWidth);
			Entry.m_V1 = (Y + H - 0.5f) / static_cast<float>(AtlasHeight);
			++LoadedIconCount;
		}

		if(InvalidKnownEntry || LoadedIconCount != static_cast<int>(EQmIcon::COUNT))
			break;

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
		Atlas.m_LoadedIconCount = LoadedIconCount;
		Atlas.m_AtlasScale = Scale;
		Atlas.m_Width = AtlasWidth;
		Atlas.m_Height = AtlasHeight;
		Atlas.m_Padding = AtlasPadding;
		Atlas.m_PxRange = static_cast<float>(PxRange);
		Atlas.m_Type = Msdf ? CQmIconAtlas::EType::MSDF : CQmIconAtlas::EType::ALPHA;
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

	const int PreferredScale = PreferredAtlasScale();
	const bool MsdfSupported = m_pGraphics->HasTexturedMsdf();
	const int Weight = NormalizeQmIconWeight(g_Config.m_QmUiIconWeight);
	if(QmIconAtlasMustDropMsdf(MsdfSupported, m_Atlas.Type()))
	{
		ClearAtlas(m_Atlas);
		m_MsdfManifestAvailable = false;
		m_PreferredScale = PreferredScale;
		m_AtlasWeight = Weight;
		m_NextReloadAttemptTime = 0;
		m_HasFailedReloadTarget = false;
	}
	const EQmIconAtlasType DesiredType = SelectQmIconAtlasType(MsdfSupported, m_MsdfManifestAvailable);
	const bool MsdfProbeNeedsRetry = QmIconAtlasNeedsMsdfProbe(MsdfSupported, m_MsdfManifestAvailable);
	const bool NeedsReload = QmIconAtlasNeedsReload(IsReady(), m_Atlas.Type(), DesiredType, m_AtlasWeight, Weight, m_PreferredScale, PreferredScale);
	const int64_t Now = time_get();
	const bool ReloadCooldownActive = QmIconReloadCooldownActive(Now, m_NextReloadAttemptTime, m_HasFailedReloadTarget, m_FailedReloadWeight, m_FailedReloadScale, m_FailedReloadMsdfSupported, Weight, PreferredScale, MsdfSupported);
	const bool MsdfProbeCooldownActive = QmIconAtlasRetryCooldownActive(Now, m_NextMsdfProbeTime);
	const SQmIconRefreshState RefreshState{NeedsReload, ReloadCooldownActive, MsdfProbeNeedsRetry, MsdfProbeCooldownActive};
	const EQmIconRefreshAction RefreshAction = QmIconRefreshAction(RefreshState);
	if(RefreshAction == EQmIconRefreshAction::RELOAD)
		Reload();
	else if(RefreshAction == EQmIconRefreshAction::RETRY_MSDF)
		RetryMsdfAtlas();
}

int CQmIconManager::PreferredAtlasScale() const
{
	if(m_pGraphics == nullptr)
		return 1;

	return QmIconPreferredAtlasScale(m_pGraphics->ScreenHiDPIScale());
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

bool CQmIconManager::RenderIcon(EQmIcon Icon, const CUIRect &Rect, const ColorRGBA &Color) const
{
	const size_t IconIndex = static_cast<size_t>(Icon);
	if(!IsReady() || IconIndex >= m_Atlas.m_aEntries.size() || !m_Atlas.m_aEntries[IconIndex].m_Valid || Color.a <= 0.0f)
		return false;

	const CQmIconAtlas::SEntry &Entry = m_Atlas.m_aEntries[IconIndex];
	const CUIRect Aligned = PixelAlignedRect(Rect);
	if(m_Atlas.IsMsdf())
	{
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
		Params.m_PxRange = m_Atlas.m_PxRange;
		Params.m_AtlasWidth = static_cast<float>(m_Atlas.m_Width);
		Params.m_AtlasHeight = static_cast<float>(m_Atlas.m_Height);
		m_pGraphics->RenderTexturedMsdf(Params);
		return true;
	}

	if(m_DiagnosticsEnabled)
	{
		m_Diagnostics.m_AlphaIconDraws++;
		FinishMsdfManagerCallRun();
	}
	m_pGraphics->WrapClamp();
	m_pGraphics->TextureSet(m_Atlas.m_Texture);
	m_pGraphics->QuadsBegin();
	m_pGraphics->SetColor(Color.r, Color.g, Color.b, Color.a);
	m_pGraphics->QuadsSetSubset(Entry.m_U0, Entry.m_V0, Entry.m_U1, Entry.m_V1);
	IGraphics::CQuadItem Quad(Aligned.x, Aligned.y, Aligned.w, Aligned.h);
	m_pGraphics->QuadsDrawTL(&Quad, 1);
	m_pGraphics->QuadsEnd();
	m_pGraphics->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	m_pGraphics->WrapNormal();
	return true;
}

bool CQmIconManager::RenderIconRotated(EQmIcon Icon, const CUIRect &Rect, const ColorRGBA &Color, float Rotation) const
{
	const size_t IconIndex = static_cast<size_t>(Icon);
	if(!IsReady() || IconIndex >= m_Atlas.m_aEntries.size() || !m_Atlas.m_aEntries[IconIndex].m_Valid || Color.a <= 0.0f)
		return false;

	const CQmIconAtlas::SEntry &Entry = m_Atlas.m_aEntries[IconIndex];
	const CUIRect Aligned = PixelAlignedRect(Rect);
	if(m_Atlas.IsMsdf())
	{
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
		Params.m_PxRange = m_Atlas.m_PxRange;
		Params.m_AtlasWidth = static_cast<float>(m_Atlas.m_Width);
		Params.m_AtlasHeight = static_cast<float>(m_Atlas.m_Height);
		Params.m_Rotation = Rotation;
		m_pGraphics->RenderTexturedMsdf(Params);
		return true;
	}

	if(m_DiagnosticsEnabled)
	{
		m_Diagnostics.m_AlphaIconDraws++;
		FinishMsdfManagerCallRun();
	}
	m_pGraphics->WrapClamp();
	m_pGraphics->TextureSet(m_Atlas.m_Texture);
	m_pGraphics->QuadsBegin();
	m_pGraphics->SetColor(Color.r, Color.g, Color.b, Color.a);
	m_pGraphics->QuadsSetSubset(Entry.m_U0, Entry.m_V0, Entry.m_U1, Entry.m_V1);
	m_pGraphics->QuadsSetRotation(Rotation);
	IGraphics::CQuadItem Quad(Aligned.x, Aligned.y, Aligned.w, Aligned.h);
	m_pGraphics->QuadsDrawTL(&Quad, 1);
	m_pGraphics->QuadsSetRotation(0.0f);
	m_pGraphics->QuadsEnd();
	m_pGraphics->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	m_pGraphics->WrapNormal();
	return true;
}

bool CQmIconManager::RenderIcon(EQmIcon Icon, const CUIRect &Rect, EQmIconState State, const SQmIconStyle &Style) const
{
	return RenderIcon(Icon, Rect, Style.Color(State));
}
