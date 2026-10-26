/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "skins7.h"

#include "menus.h"

#include <base/color.h>
#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/engine.h>
#include <engine/external/json-parser/json.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/localization.h>
#include <engine/shared/protocol7.h>
#include <engine/storage.h>

#include <generated/client_data7.h>

#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/qm_skin_outline.h>
#include <game/client/components/qmclient/skin7_descriptor_publication.h>
#include <game/client/components/qmclient/skin7_info_job.h>
#include <game/client/components/qmclient/skin7_save.h>
#include <game/client/components/qmclient/skin_load_budget.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

const char *const CSkins7::ms_apSkinPartNamesLocalized[protocol7::NUM_SKINPARTS] = {Localizable("Body", "skins"), Localizable("Marking", "skins"), Localizable("Decoration", "skins"), Localizable("Hands", "skins"), Localizable("Feet", "skins"), Localizable("Eyes", "skins")};

char *CSkins7::ms_apSkinNameVariables[NUM_DUMMIES] = {nullptr};
char *CSkins7::ms_apSkinVariables[NUM_DUMMIES][protocol7::NUM_SKINPARTS] = {{nullptr}};
int *CSkins7::ms_apUCCVariables[NUM_DUMMIES][protocol7::NUM_SKINPARTS] = {{nullptr}};
int unsigned *CSkins7::ms_apColorVariables[NUM_DUMMIES][protocol7::NUM_SKINPARTS] = {{nullptr}};

#define SKINS_DIR "skins7"

static ColorRGBA DetermineBloodColorFromInfo(const CImageInfo &Info)
{
	const size_t Step = Info.PixelSize();
	const size_t Pitch = Info.m_Width * Step;
	const size_t PartX = Info.m_Width / 2;
	const size_t PartY = 0;
	const size_t PartWidth = Info.m_Width / 2;
	const size_t PartHeight = Info.m_Height / 2;

	int64_t aColors[3] = {0};
	for(size_t y = PartY; y < PartY + PartHeight; y++)
	{
		for(size_t x = PartX; x < PartX + PartWidth; x++)
		{
			const size_t Offset = y * Pitch + x * Step;
			if(Info.m_pData[Offset + 3] > 128)
			{
				for(size_t c = 0; c < 3; c++)
				{
					aColors[c] += Info.m_pData[Offset + c];
				}
			}
		}
	}

	const vec3 NormalizedColor = normalize(vec3(aColors[0], aColors[1], aColors[2]));
	return ColorRGBA(NormalizedColor.x, NormalizedColor.y, NormalizedColor.z);
}

// TODO: uncomment
// const float MIN_EYE_BODY_COLOR_DIST = 80.f; // between body and eyes (LAB color space)

// 六个部件各自的描边：从原图取「填充 + 描边」两个素材生成遮罩，供 sixup 渲染按部件画描边。
static std::shared_ptr<CQmSkinOutline> CreateSkinPartOutline(const CImageInfo &Image, int Part)
{
	if(Part == protocol7::SKINPART_BODY)
		return QmCreateSkinOutline(Image, client_data7::g_pData->m_aSprites[client_data7::SPRITE_TEE_BODY], client_data7::g_pData->m_aSprites[client_data7::SPRITE_TEE_BODY_OUTLINE], vec2(64, 64));
	if(Part == protocol7::SKINPART_FEET)
		return QmCreateSkinOutline(Image, client_data7::g_pData->m_aSprites[client_data7::SPRITE_TEE_FOOT], client_data7::g_pData->m_aSprites[client_data7::SPRITE_TEE_FOOT_OUTLINE], vec2(64.0f / 2.1f, 64.0f / 2.1f));
	if(Part == protocol7::SKINPART_DECORATION)
		return QmCreateSkinOutline(Image, client_data7::g_pData->m_aSprites[client_data7::SPRITE_TEE_DECORATION], client_data7::g_pData->m_aSprites[client_data7::SPRITE_TEE_DECORATION_OUTLINE], vec2(64, 64));
	return nullptr;
}
// 六人皮肤按部件拆成独立图片：这里把每个部件用到的素材缩成 CPU 副本供聊天导出复用。
static std::shared_ptr<const QmChatAvatar::SSource> CreateChatAvatarPartSource(const CImageInfo &Image, int Part)
{
	using namespace QmChatAvatar;
	auto pSource = std::make_shared<SSource>();
	const auto Copy = [&](ESprite Target, int Sprite) {
		pSource->m_aSprites[Target] = CopySprite(Image, client_data7::g_pData->m_aSprites[Sprite]);
	};
	switch(Part)
	{
	case protocol7::SKINPART_BODY:
		Copy(BODY, client_data7::SPRITE_TEE_BODY);
		Copy(BODY_OUTLINE, client_data7::SPRITE_TEE_BODY_OUTLINE);
		Copy(SHADOW, client_data7::SPRITE_TEE_BODY_SHADOW);
		Copy(UPPER_OUTLINE, client_data7::SPRITE_TEE_BODY_UPPER_OUTLINE);
		break;
	case protocol7::SKINPART_FEET:
		Copy(FEET, client_data7::SPRITE_TEE_FOOT);
		Copy(FEET_OUTLINE, client_data7::SPRITE_TEE_FOOT_OUTLINE);
		break;
	case protocol7::SKINPART_EYES:
		Copy(EYES, client_data7::SPRITE_TEE_EYES_NORMAL);
		break;
	case protocol7::SKINPART_MARKING:
		Copy(MARKING, client_data7::SPRITE_TEE_MARKING);
		break;
	case protocol7::SKINPART_DECORATION:
		Copy(DECORATION, client_data7::SPRITE_TEE_DECORATION);
		Copy(DECORATION_OUTLINE, client_data7::SPRITE_TEE_DECORATION_OUTLINE);
		break;
	default:
		return nullptr;
	}
	return pSource;
}

static void PrepareSkinPartVisuals(CSkins7::CSkinPartLoadJob::SResult &Result)
{
	Result.m_BloodColor = DetermineBloodColorFromInfo(Result.m_OriginalImage);
	Result.m_QmSkinOutline = CreateSkinPartOutline(Result.m_OriginalImage, Result.m_PartType);
	Result.m_pChatAvatarOriginal = CreateChatAvatarPartSource(Result.m_OriginalImage, Result.m_PartType);
	Result.m_pChatAvatarColorable = CreateChatAvatarPartSource(Result.m_GrayscaleImage, Result.m_PartType);
}

void CSkins7::CSkinPart::ApplyTo(CTeeRenderInfo::CSixup &SixupRenderInfo) const
{
	SixupRenderInfo.m_aOriginalTextures[m_Type] = m_OriginalTexture;
	SixupRenderInfo.m_aColorableTextures[m_Type] = m_ColorableTexture;
	SixupRenderInfo.m_apQmSkinOutlines[m_Type] = m_QmSkinOutline;
	SixupRenderInfo.m_apChatAvatarOriginal[m_Type] = m_pChatAvatarOriginal;
	SixupRenderInfo.m_apChatAvatarColorable[m_Type] = m_pChatAvatarColorable;
	if(m_Type == protocol7::SKINPART_BODY)
	{
		SixupRenderInfo.m_BloodColor = m_BloodColor;
	}
}

bool CSkins7::CSkinPart::operator<(const CSkinPart &Other) const
{
	return str_comp_nocase(m_aName, Other.m_aName) < 0;
}

bool CSkins7::CSkin::operator==(const CSkin &Other) const
{
	return str_comp(m_aName, Other.m_aName) == 0;
}

bool CSkins7::IsSpecialSkin(const char *pName)
{
	return str_startswith(pName, "x_") != nullptr;
}

std::shared_ptr<CSkins7::CSkinPartLoadJob> CSkins7::CreateSkinPartLoadJob(int PartType, const char *pName, int DirType)
{
	size_t PartNameSize, PartNameCount;
	str_utf8_stats(pName, str_length(pName) - str_length(".png") + 1, IO_MAX_PATH_LENGTH, &PartNameSize, &PartNameCount);
	if(PartNameSize >= protocol7::MAX_SKIN_ARRAY_SIZE || PartNameCount > protocol7::MAX_SKIN_LENGTH)
	{
		log_error("skins7", "Failed to load skin part '%s/%s': name too long", CSkins7::ms_apSkinPartNames[PartType], pName);
		return nullptr;
	}

	char aFilename[IO_MAX_PATH_LENGTH];
	str_format(aFilename, sizeof(aFilename), SKINS_DIR "/%s/%s", CSkins7::ms_apSkinPartNames[PartType], pName);

	char aPartName[24];
	str_copy(aPartName, pName, minimum<int>(PartNameSize + 1, sizeof(aPartName)));

	int Flags = 0;
	if(IsSpecialSkin(pName))
	{
		Flags |= SKINFLAG_SPECIAL;
	}
	if(DirType != IStorage::TYPE_SAVE)
	{
		Flags |= SKINFLAG_STANDARD;
	}

	return std::make_shared<CSkinPartLoadJob>(aFilename, aPartName, Storage(), DirType, PartType, Flags, PrepareSkinPartVisuals);
}

void CSkins7::ProcessCompletedJobs()
{
	const auto Begin = time_get_nanoseconds();
	if(!m_pSkinInfoJob || m_pSkinInfoJob->State() != IJob::STATE_DONE)
		return;
	// 后台扫描的结果分帧入队；文件多时不能一次创建全部任务阻塞输入。
	const auto &Parts = m_pSkinInfoJob->Parts();
	int Scheduled = 0;
	while(m_NextPart < Parts.size() && QmSkinCanFinalize(Scheduled, time_get_nanoseconds() - Begin, std::chrono::milliseconds(1)))
	{
		if(!QmTryQueueNextSkin7Part(Parts, m_NextPart, m_PendingSkinPartJobs, *Engine(), [this](const auto &Part) {
			   return CreateSkinPartLoadJob(Part.m_Part, Part.m_Name.c_str(), Part.m_StorageType);
		   }))
			break;
		++Scheduled;
	}
	// GPU 句柄只能在主线程创建，读盘和解码已由描述任务完成。
	// 1ms 是批处理软预算，单次驱动上传无法抢占，上传后不再开始超预算的下一项。
	while(m_NextAccessory < 2 && time_get_nanoseconds() - Begin < std::chrono::milliseconds(1))
	{
		if(!m_pSkinInfoJob->PublishNextAccessory(m_NextAccessory, [&](const CImageInfo &Image, bool Hat) {
			   return m_PartPublisher.TryPublishAccessory(Image, Hat ? m_XmasHatTexture : m_BotTexture,
				   *Graphics(), *GameClient()->GpuUploadLimiter(), Hat ? "skins7/xmas_hat.png" : "skins7/bot.png");
		   }))
			break;
	}

	auto Iter = m_PendingSkinPartJobs.begin();
	while(Iter != m_PendingSkinPartJobs.end() && time_get_nanoseconds() - Begin < std::chrono::milliseconds(1))
	{
		auto &pJob = *Iter;
		if(!pJob->IsCompleted())
		{
			++Iter;
			continue;
		}

		char aLoadedName[24] = "";
		int LoadedPart = -1;
		if(!pJob->PublishResult([&](const CSkinPartLoadJob::SResult &Result) {
			   if(!m_PartPublisher.TryPublish(Result, m_avSkinParts[Result.m_PartType], *Graphics(), *GameClient()->GpuUploadLimiter()))
				   return false;
			   str_copy(aLoadedName, Result.m_aName);
			   LoadedPart = Result.m_PartType;
			   return true;
		   }))
			break;

		// 诊断失败不能回滚已提交部件，也不能让同一任务再次上传。
		if(LoadedPart >= 0 && Config()->m_Debug)
		{
			try
			{
				log_trace("skins7", "Loaded skin part '%s/%s'", CSkins7::ms_apSkinPartNames[LoadedPart], aLoadedName);
			}
			catch(...)
			{
			}
		}

		Iter = m_PendingSkinPartJobs.erase(Iter);
	}

	if(m_NextPart == Parts.size() && m_NextAccessory == 2 && m_PendingSkinPartJobs.empty() && m_Loading && time_get_nanoseconds() - Begin < std::chrono::milliseconds(1))
	{
		if(!RebuildSkins())
			return;
		m_LoadCompletion.Complete(m_Loading, m_ResourceLifecycle, m_LastRefreshTime, time_get_nanoseconds(), [this] {
			if(QmPerfEnabled())
			{
				char aPayload[96];
				str_format(aPayload, sizeof(aPayload), "event=resource_load_complete skins=%zu", m_vSkins.size());
				QmPerfLogPayloadForce("perf/skins7", aPayload, Client());
			}
		});
	}
}

bool CSkins7::RebuildSkins()
{
	// 部件向量全部稳定之后才能绑定指针，描述绑定也按时间预算分帧推进。
	const auto Begin = time_get_nanoseconds();
	const auto &Entries = m_pSkinInfoJob->Entries();
	int Processed = 0;
	while(m_NextSkin < Entries.size() && QmSkinCanFinalize(Processed, time_get_nanoseconds() - Begin, std::chrono::milliseconds(1)))
	{
		if(!QmTryFinalizeNextSkin7(*m_pSkinInfoJob, m_NextSkin, [this](const auto &Entry) {
			   return LoadSkin(Entry.m_Name.c_str(), Entry.m_StorageType, *Entry.m_pInfo);
		   }))
			return false;
		++Processed;
	}
	if(m_NextSkin != Entries.size())
		return false;
	m_pSkinInfoJob.reset();
	return true;
}

bool CSkins7::LoadSkin(const char *pName, int DirType, const json_value &Info)
{
	const bool Special = IsSpecialSkin(pName);
	const int Flags = (Special ? SKINFLAG_SPECIAL : 0) | (DirType != IStorage::TYPE_SAVE ? SKINFLAG_STANDARD : 0);
	const auto Lookup = [this](int Part, const char *pFilename, bool AllowSpecial) {
		return pFilename ? FindSkinPart(Part, pFilename, AllowSpecial) : FindDefaultSkinPart(Part);
	};
	const auto Diagnose = [this, pName](qm_skin7_descriptor::SStatus Status) {
		using EError = qm_skin7_descriptor::EError;
		if(Status.m_Error == EError::PART_OBJECT)
			log_error("skins7", "Failed to parse skin json file '" SKINS_DIR "/%s.json': attribute '%s' must specify an object", pName, ms_apSkinPartNames[Status.m_Part]);
		else if(Status.m_Error == EError::FILENAME_STRING)
			log_error("skins7", "Failed to parse skin json file '" SKINS_DIR "/%s.json': part '%s' attribute 'filename' must specify a string", pName, ms_apSkinPartNames[Status.m_Part]);
		else if(Config()->m_Debug)
			log_trace("skins7", "Loaded skin '%s'", pName);
	};
	return qm_skin7_descriptor::Publish(m_vSkins, pName, Flags, Special, Info, ms_apSkinPartNames, ms_apColorComponents, Lookup, Diagnose);
}

void CSkins7::OnInit()
{
	int Dummy = 0;
	ms_apSkinNameVariables[Dummy] = Config()->m_ClPlayer7Skin;
	ms_apSkinVariables[Dummy][protocol7::SKINPART_BODY] = Config()->m_ClPlayer7SkinBody;
	ms_apSkinVariables[Dummy][protocol7::SKINPART_MARKING] = Config()->m_ClPlayer7SkinMarking;
	ms_apSkinVariables[Dummy][protocol7::SKINPART_DECORATION] = Config()->m_ClPlayer7SkinDecoration;
	ms_apSkinVariables[Dummy][protocol7::SKINPART_HANDS] = Config()->m_ClPlayer7SkinHands;
	ms_apSkinVariables[Dummy][protocol7::SKINPART_FEET] = Config()->m_ClPlayer7SkinFeet;
	ms_apSkinVariables[Dummy][protocol7::SKINPART_EYES] = Config()->m_ClPlayer7SkinEyes;
	ms_apUCCVariables[Dummy][protocol7::SKINPART_BODY] = &Config()->m_ClPlayer7UseCustomColorBody;
	ms_apUCCVariables[Dummy][protocol7::SKINPART_MARKING] = &Config()->m_ClPlayer7UseCustomColorMarking;
	ms_apUCCVariables[Dummy][protocol7::SKINPART_DECORATION] = &Config()->m_ClPlayer7UseCustomColorDecoration;
	ms_apUCCVariables[Dummy][protocol7::SKINPART_HANDS] = &Config()->m_ClPlayer7UseCustomColorHands;
	ms_apUCCVariables[Dummy][protocol7::SKINPART_FEET] = &Config()->m_ClPlayer7UseCustomColorFeet;
	ms_apUCCVariables[Dummy][protocol7::SKINPART_EYES] = &Config()->m_ClPlayer7UseCustomColorEyes;
	ms_apColorVariables[Dummy][protocol7::SKINPART_BODY] = &Config()->m_ClPlayer7ColorBody;
	ms_apColorVariables[Dummy][protocol7::SKINPART_MARKING] = &Config()->m_ClPlayer7ColorMarking;
	ms_apColorVariables[Dummy][protocol7::SKINPART_DECORATION] = &Config()->m_ClPlayer7ColorDecoration;
	ms_apColorVariables[Dummy][protocol7::SKINPART_HANDS] = &Config()->m_ClPlayer7ColorHands;
	ms_apColorVariables[Dummy][protocol7::SKINPART_FEET] = &Config()->m_ClPlayer7ColorFeet;
	ms_apColorVariables[Dummy][protocol7::SKINPART_EYES] = &Config()->m_ClPlayer7ColorEyes;

	Dummy = 1;
	ms_apSkinNameVariables[Dummy] = Config()->m_ClDummy7Skin;
	ms_apSkinVariables[Dummy][protocol7::SKINPART_BODY] = Config()->m_ClDummy7SkinBody;
	ms_apSkinVariables[Dummy][protocol7::SKINPART_MARKING] = Config()->m_ClDummy7SkinMarking;
	ms_apSkinVariables[Dummy][protocol7::SKINPART_DECORATION] = Config()->m_ClDummy7SkinDecoration;
	ms_apSkinVariables[Dummy][protocol7::SKINPART_HANDS] = Config()->m_ClDummy7SkinHands;
	ms_apSkinVariables[Dummy][protocol7::SKINPART_FEET] = Config()->m_ClDummy7SkinFeet;
	ms_apSkinVariables[Dummy][protocol7::SKINPART_EYES] = Config()->m_ClDummy7SkinEyes;
	ms_apUCCVariables[Dummy][protocol7::SKINPART_BODY] = &Config()->m_ClDummy7UseCustomColorBody;
	ms_apUCCVariables[Dummy][protocol7::SKINPART_MARKING] = &Config()->m_ClDummy7UseCustomColorMarking;
	ms_apUCCVariables[Dummy][protocol7::SKINPART_DECORATION] = &Config()->m_ClDummy7UseCustomColorDecoration;
	ms_apUCCVariables[Dummy][protocol7::SKINPART_HANDS] = &Config()->m_ClDummy7UseCustomColorHands;
	ms_apUCCVariables[Dummy][protocol7::SKINPART_FEET] = &Config()->m_ClDummy7UseCustomColorFeet;
	ms_apUCCVariables[Dummy][protocol7::SKINPART_EYES] = &Config()->m_ClDummy7UseCustomColorEyes;
	ms_apColorVariables[Dummy][protocol7::SKINPART_BODY] = &Config()->m_ClDummy7ColorBody;
	ms_apColorVariables[Dummy][protocol7::SKINPART_MARKING] = &Config()->m_ClDummy7ColorMarking;
	ms_apColorVariables[Dummy][protocol7::SKINPART_DECORATION] = &Config()->m_ClDummy7ColorDecoration;
	ms_apColorVariables[Dummy][protocol7::SKINPART_HANDS] = &Config()->m_ClDummy7ColorHands;
	ms_apColorVariables[Dummy][protocol7::SKINPART_FEET] = &Config()->m_ClDummy7ColorFeet;
	ms_apColorVariables[Dummy][protocol7::SKINPART_EYES] = &Config()->m_ClDummy7ColorEyes;

	InitPlaceholderSkinParts();
	// 启动只准备安全占位，不能在普通服务器列表中扫描/加载 0.7 皮肤。
	m_ResourceLifecycle.Reset();
}

void CSkins7::OnReset()
{
	OnUpdate();
}

void CSkins7::OnShutdown()
{
	CancelPendingLoads();
	m_ResourceLifecycle.Reset();
}

void CSkins7::CancelPendingLoads()
{
	if(m_StartingRefresh)
		m_StartingRefresh->m_pJob->Abort();
	m_StartingRefresh.reset();
	m_RefreshRetirement.Drain(*Graphics(), 32);
	m_RefreshRequest.Cancel();
	m_PartPublisher.TryRelease(*Graphics());
	for(auto &pJob : m_PendingSkinPartJobs)
		pJob->Abort();
	m_PendingSkinPartJobs.clear();
	if(m_pSkinInfoJob)
		m_pSkinInfoJob->Abort();
	m_pSkinInfoJob.reset();
	m_Loading = false;
	m_LoadCompletion.Cancel();
}

void CSkins7::OnUpdate()
{
	m_PartPublisher.TryRelease(*Graphics());
	if(!QmNeedsSkin7Resources(Client()->State(), Client()->IsSixup()))
	{
		m_RefreshRequest.Cancel();
		m_LoadCompletion.Cancel();
	}
	const auto Action = m_ResourceLifecycle.Update(Client()->State(), Client()->IsSixup());
	if(Action == CQmSkin7ResourceLifecycle::EAction::LOAD)
		GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SEVEN);
	else if(Action == CQmSkin7ResourceLifecycle::EAction::CANCEL)
	{
		try
		{
			if(QmPerfEnabled())
				QmPerfLogPayloadForce("perf/skins7", "event=resource_load_cancel", Client());
		}
		catch(...)
		{
		}
		CancelPendingLoads();
	}
	else
		TryStartRefresh();
	if(QmNeedsSkin7Resources(Client()->State(), Client()->IsSixup()))
	{
		ProcessCompletedJobs();
		m_LoadCompletion.Dispatch(m_Loading);
	}
}

void CSkins7::InitPlaceholderSkinParts()
{
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		CSkinPart &SkinPart = m_aPlaceholderSkinParts[Part];
		SkinPart.m_Type = Part;
		SkinPart.m_Flags = SKINFLAG_STANDARD;
		str_copy(SkinPart.m_aName, "dummy");
		SkinPart.m_OriginalTexture.Invalidate();
		SkinPart.m_ColorableTexture.Invalidate();
		SkinPart.m_BloodColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

void CSkins7::Refresh(TSkinLoadedCallback &&SkinLoadedCallback)
{
	// 先登记可重试请求，已有缓存/任务在新请求被接收前保持可用。
	if(!m_ResourceLifecycle.RequestRefresh(Client()->State(), Client()->IsSixup()))
		return;
	if(m_StartingRefresh)
		m_StartingRefresh->m_pJob->Abort();
	m_StartingRefresh.reset();
	m_RefreshRequest.Request(std::move(SkinLoadedCallback));
	TryStartRefresh();
}

void CSkins7::TryStartRefresh()
{
	const auto Begin = time_get_nanoseconds();
	const auto DrainRetired = [&] {
		return m_RefreshRetirement.Drain(*Graphics(), 32, [&] {
			return time_get_nanoseconds() - Begin < std::chrono::milliseconds(1);
		});
	};
	// 离线也只继续释放既有资源，不创建新任务或上传新图。
	if(!DrainRetired() || !QmNeedsSkin7Resources(Client()->State(), Client()->IsSixup()))
		return;
	if(!m_StartingRefresh)
	{
		auto Plan = m_RefreshRequest.TryStart(*Engine(), Storage(), SKINFLAG_STANDARD);
		if(!Plan)
			return;
		m_StartingRefresh.emplace(std::move(*Plan));
		m_ResourceLifecycle.BeginRefresh(Client()->State(), Client()->IsSixup());
		m_PartPublisher.TryRelease(*Graphics());
		m_vSkins.clear();
		m_NextPart = 0;
		m_NextSkin = 0;
		m_NextAccessory = 0;
		if(m_pSkinInfoJob)
			m_pSkinInfoJob->Abort();
		m_pSkinInfoJob.reset();
		for(auto &pJob : m_PendingSkinPartJobs)
			pJob->Abort();
		m_PendingSkinPartJobs.clear();
		m_RefreshRetirement.Adopt(m_avSkinParts, m_StartingRefresh->m_aParts);
		m_LoadCompletion.Cancel();
		m_Loading = true;
		try
		{
			if(QmPerfEnabled())
				QmPerfLogPayloadForce("perf/skins7", "event=resource_load_begin", Client());
		}
		catch(...)
		{
		}
		if(!DrainRetired())
			return;
	}
	// 旧资源全部释放后才移交新扫描结果，计划和完成回调可跨帧保留。
	m_pSkinInfoJob = std::move(m_StartingRefresh->m_pJob);
	m_LoadCompletion.Set(std::move(m_StartingRefresh->m_Callback));
	m_StartingRefresh.reset();
	ProcessCompletedJobs();
}

void CSkins7::PrepareSkinFromConfigVariables(std::vector<CSkin> &Skins, const char *pName, int Dummy) const
{
	auto OldSkin = std::find_if(Skins.begin(), Skins.end(), [pName](const CSkin &Skin) {
		return str_comp(Skin.m_aName, pName) == 0;
	});
	if(OldSkin != Skins.end())
	{
		Skins.erase(OldSkin);
	}

	CSkin NewSkin;
	NewSkin.m_Flags = 0;
	str_copy(NewSkin.m_aName, pName);
	for(int PartIndex = 0; PartIndex < protocol7::NUM_SKINPARTS; ++PartIndex)
	{
		NewSkin.m_apParts[PartIndex] = FindSkinPart(PartIndex, ms_apSkinVariables[Dummy][PartIndex], false);
		NewSkin.m_aUseCustomColors[PartIndex] = *ms_apUCCVariables[Dummy][PartIndex];
		NewSkin.m_aPartColors[PartIndex] = *ms_apColorVariables[Dummy][PartIndex];
	}
	Skins.insert(std::lower_bound(Skins.begin(), Skins.end(), NewSkin), NewSkin);
}

bool CSkins7::RemoveSkin(const char *pName)
{
	const CSkin *pSkin = FindSkin(pName, true);
	if(pSkin == nullptr)
		return false;
	char aBuf[IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), SKINS_DIR "/%s.json", pSkin->m_aName);
	if(!Storage()->RemoveFile(aBuf, IStorage::TYPE_SAVE))
	{
		return false;
	}

	auto FoundSkin = std::find(m_vSkins.begin(), m_vSkins.end(), *pSkin);
	dbg_assert(FoundSkin != m_vSkins.end(), "Skin not found");
	m_vSkins.erase(FoundSkin);
	m_LastRefreshTime = time_get_nanoseconds();
	return true;
}

const std::vector<CSkins7::CSkin> &CSkins7::GetSkins() const
{
	return m_vSkins;
}

const std::vector<CSkins7::CSkinPart> &CSkins7::GetSkinParts(int Part) const
{
	return m_avSkinParts[Part];
}

const CSkins7::CSkin *CSkins7::FindSkin(const char *pName, bool AllowSpecialSkin) const
{
	if(pName == nullptr)
		return nullptr;
	for(const CSkin &Skin : m_vSkins)
	{
		if((AllowSpecialSkin || (Skin.m_Flags & SKINFLAG_SPECIAL) == 0) && str_comp(Skin.m_aName, pName) == 0)
			return &Skin;
	}
	return nullptr;
}

const CSkins7::CSkinPart *CSkins7::FindSkinPartOrNullptr(int Part, const char *pName, bool AllowSpecialPart) const
{
	auto FoundPart = std::find_if(m_avSkinParts[Part].begin(), m_avSkinParts[Part].end(), [pName](const CSkinPart &SkinPart) {
		return str_comp(SkinPart.m_aName, pName) == 0;
	});
	if(FoundPart == m_avSkinParts[Part].end())
	{
		return nullptr;
	}
	if((FoundPart->m_Flags & SKINFLAG_SPECIAL) != 0 && !AllowSpecialPart)
	{
		return nullptr;
	}
	return &*FoundPart;
}

const CSkins7::CSkinPart *CSkins7::FindDefaultSkinPart(int Part) const
{
	const char *pDefaultPartName = Part == protocol7::SKINPART_MARKING || Part == protocol7::SKINPART_DECORATION ? "" : "standard";
	const CSkinPart *pDefault = FindSkinPartOrNullptr(Part, pDefaultPartName, false);
	if(pDefault != nullptr)
	{
		return pDefault;
	}
	return &m_aPlaceholderSkinParts[Part];
}

const CSkins7::CSkinPart *CSkins7::FindSkinPart(int Part, const char *pName, bool AllowSpecialPart) const
{
	const CSkinPart *pSkinPart = FindSkinPartOrNullptr(Part, pName, AllowSpecialPart);
	if(pSkinPart != nullptr)
	{
		return pSkinPart;
	}
	return FindDefaultSkinPart(Part);
}

void CSkins7::RandomizeSkin(int Dummy) const
{
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		int Hue = rand() % 255;
		int Sat = rand() % 255;
		int Lgt = rand() % 255;
		int Alp = 0;
		if(Part == protocol7::SKINPART_MARKING)
			Alp = rand() % 255;
		int ColorVariable = (Alp << 24) | (Hue << 16) | (Sat << 8) | Lgt;
		*ms_apUCCVariables[Dummy][Part] = true;
		*ms_apColorVariables[Dummy][Part] = ColorVariable;
	}

	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		std::vector<const CSkins7::CSkinPart *> vpConsideredSkinParts;
		for(const CSkinPart &SkinPart : GetSkinParts(Part))
		{
			if((SkinPart.m_Flags & CSkins7::SKINFLAG_SPECIAL) != 0)
				continue;
			vpConsideredSkinParts.push_back(&SkinPart);
		}
		const CSkins7::CSkinPart *pRandomPart;
		if(vpConsideredSkinParts.empty())
		{
			pRandomPart = FindDefaultSkinPart(Part);
		}
		else
		{
			pRandomPart = vpConsideredSkinParts[rand() % vpConsideredSkinParts.size()];
		}
		str_copy(CSkins7::ms_apSkinVariables[Dummy][Part], pRandomPart->m_aName, protocol7::MAX_SKIN_ARRAY_SIZE);
	}

	ms_apSkinNameVariables[Dummy][0] = '\0';
}

ColorRGBA CSkins7::GetColor(int Value, bool UseAlpha) const
{
	return color_cast<ColorRGBA>(ColorHSLA(Value, UseAlpha).UnclampLighting(ColorHSLA::DARKEST_LGT7));
}

void CSkins7::ApplyColorTo(CTeeRenderInfo::CSixup &SixupRenderInfo, bool UseCustomColors, int Value, int Part) const
{
	SixupRenderInfo.m_aUseCustomColors[Part] = UseCustomColors;
	if(UseCustomColors)
	{
		SixupRenderInfo.m_aColors[Part] = GetColor(Value, Part == protocol7::SKINPART_MARKING);
	}
	else
	{
		SixupRenderInfo.m_aColors[Part] = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

ColorRGBA CSkins7::GetTeamColor(int UseCustomColors, int PartColor, int Team, int Part) const
{
	static const int s_aTeamColors[3] = {0xC4C34E, 0x00FF6B, 0x9BFF6B};

	int TeamHue = (s_aTeamColors[Team + 1] >> 16) & 0xff;
	int TeamSat = (s_aTeamColors[Team + 1] >> 8) & 0xff;
	int TeamLgt = s_aTeamColors[Team + 1] & 0xff;
	int PartSat = (PartColor >> 8) & 0xff;
	int PartLgt = PartColor & 0xff;

	if(!UseCustomColors)
	{
		PartSat = 255;
		PartLgt = 255;
	}

	int MinSat = 160;
	int MaxSat = 255;

	int h = TeamHue;
	int s = std::clamp(mix(TeamSat, PartSat, 0.2), MinSat, MaxSat);
	int l = std::clamp(mix(TeamLgt, PartLgt, 0.2), (int)ColorHSLA::DARKEST_LGT7, 200);

	int ColorVal = (h << 16) + (s << 8) + l;

	return GetColor(ColorVal, Part == protocol7::SKINPART_MARKING);
}

bool CSkins7::ValidateSkinParts(char *apPartNames[protocol7::NUM_SKINPARTS], int *pUseCustomColors, int *pPartColors, int GameFlags) const
{
	// force standard (black) eyes on team skins
	if(GameFlags & GAMEFLAG_TEAMS)
	{
		// TODO: adjust eye color here as well?
		if(str_comp(apPartNames[protocol7::SKINPART_EYES], "colorable") == 0 || str_comp(apPartNames[protocol7::SKINPART_EYES], "negative") == 0)
		{
			str_copy(apPartNames[protocol7::SKINPART_EYES], "standard", protocol7::MAX_SKIN_ARRAY_SIZE);
			return false;
		}
	}
	return true;
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
bool SaveSkinfileFromParts(IStorage *pStorage, const char *pName, const char *pBodyPartName, const char *pMarkingPartName, const char *pDecorationPartName, const char *pHandsPartName, const char *pFeetPartName, const char *pEyesPartName)
{
	const qm_skin7_save::TParts Parts{{{pBodyPartName}, {pMarkingPartName}, {pDecorationPartName}, {pHandsPartName}, {pFeetPartName}, {pEyesPartName}}};
	return qm_skin7_save::Save(pStorage, pName, Parts);
}

bool CSkins7::SaveSkinfile(const char *pName, int Dummy)
{
	dbg_assert(!IsSpecialSkin(pName), "Cannot save special skins");
	qm_skin7_save::TParts Parts;
	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; ++Part)
	{
		Parts[Part] = {ms_apSkinVariables[Dummy][Part], *ms_apUCCVariables[Dummy][Part] != 0, *ms_apColorVariables[Dummy][Part]};
	}
	if(!qm_skin7_save::SaveAndPublish(Storage(), pName, Parts, m_vSkins, [this, pName, Dummy](auto &Candidate) {
		   PrepareSkinFromConfigVariables(Candidate, pName, Dummy);
	   }))
		return false;
	m_LastRefreshTime = time_get_nanoseconds();
	return true;
}
