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
#include <game/client/components/qmclient/skin7_info_job.h>
#include <game/client/components/qmclient/skin_load_budget.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

const char *const CSkins7::ms_apSkinPartNames[protocol7::NUM_SKINPARTS] = {"body", "marking", "decoration", "hands", "feet", "eyes"};
const char *const CSkins7::ms_apSkinPartNamesLocalized[protocol7::NUM_SKINPARTS] = {Localizable("Body", "skins"), Localizable("Marking", "skins"), Localizable("Decoration", "skins"), Localizable("Hands", "skins"), Localizable("Feet", "skins"), Localizable("Eyes", "skins")};
const char *const CSkins7::ms_apColorComponents[NUM_COLOR_COMPONENTS] = {"hue", "sat", "lgt", "alp"};

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

static std::shared_ptr<CQmSkinOutline> CreateSkinPartOutline(const CImageInfo &Image, int Part);
static std::shared_ptr<const QmChatAvatar::SSource> CreateChatAvatarPartSource(const CImageInfo &Image, int Part);

CSkins7::CSkinPartLoadJob::CSkinPartLoadJob(const char *pPath, const char *pPartName, IStorage *pStorage, int StorageType, int PartType, int Flags) :
	m_Path(pPath),
	m_PartName(pPartName),
	m_pStorage(pStorage),
	m_StorageType(StorageType),
	m_PartType(PartType),
	m_Flags(Flags)
{
	str_copy(m_Result.m_aName, pPartName, sizeof(m_Result.m_aName));
	m_Result.m_PartType = PartType;
	m_Result.m_Flags = Flags;
	Abortable(true);
}

CSkins7::CSkinPartLoadJob::~CSkinPartLoadJob()
{
	m_Result.m_OriginalImage.Free();
	m_Result.m_GrayscaleImage.Free();
}

void CSkins7::CSkinPartLoadJob::Run()
{
	if(State() == STATE_ABORTED)
		return;
	void *pFileData = nullptr;
	unsigned FileSize = 0;
	if(!m_pStorage->ReadFile(m_Path.c_str(), m_StorageType, &pFileData, &FileSize))
	{
		log_error("skins7", "Failed to read skin part file '%s'", m_Path.c_str());
		CLockScope Lock(m_Mutex);
		m_Completed = true;
		return;
	}
	if(State() == STATE_ABORTED)
	{
		free(pFileData);
		return;
	}

	CImageInfo OriginalImage;
	if(!CImageLoader::LoadPng(static_cast<uint8_t *>(pFileData), FileSize, m_Path.c_str(), OriginalImage))
	{
		free(pFileData);
		log_error("skins7", "Failed to decode skin part PNG '%s'", m_Path.c_str());
		CLockScope Lock(m_Mutex);
		m_Completed = true;
		return;
	}
	free(pFileData);
	if(State() == STATE_ABORTED)
	{
		OriginalImage.Free();
		return;
	}

	if(OriginalImage.m_Format != CImageInfo::FORMAT_RGBA)
	{
		log_error("skins7", "Skin part '%s' must be RGBA format", m_Path.c_str());
		OriginalImage.Free();
		CLockScope Lock(m_Mutex);
		m_Completed = true;
		return;
	}

	CImageInfo GrayscaleImage = OriginalImage.DeepCopy();
	if(GrayscaleImage.m_pData == nullptr)
	{
		log_error("skins7", "Failed to copy skin part '%s'", m_Path.c_str());
		OriginalImage.Free();
		CLockScope Lock(m_Mutex);
		m_Completed = true;
		return;
	}
	ConvertToGrayscale(GrayscaleImage);

	ColorRGBA BloodColor = DetermineBloodColorFromInfo(OriginalImage);
	auto pOutline = CreateSkinPartOutline(OriginalImage, m_PartType);
	auto pAvatarOriginal = CreateChatAvatarPartSource(OriginalImage, m_PartType);
	auto pAvatarColorable = CreateChatAvatarPartSource(GrayscaleImage, m_PartType);

	{
		CLockScope Lock(m_Mutex);
		m_Result.m_OriginalImage = std::move(OriginalImage);
		m_Result.m_GrayscaleImage = std::move(GrayscaleImage);
		m_Result.m_BloodColor = BloodColor;
		m_Result.m_QmSkinOutline = std::move(pOutline);
		m_Result.m_pChatAvatarOriginal = std::move(pAvatarOriginal);
		m_Result.m_pChatAvatarColorable = std::move(pAvatarColorable);
		m_Result.m_Success = true;
		m_Completed = true;
	}
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

bool CSkins7::CSkin::operator<(const CSkin &Other) const
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

void CSkins7::StartSkinPartLoadJob(int PartType, const char *pName, int DirType)
{
	size_t PartNameSize, PartNameCount;
	str_utf8_stats(pName, str_length(pName) - str_length(".png") + 1, IO_MAX_PATH_LENGTH, &PartNameSize, &PartNameCount);
	if(PartNameSize >= protocol7::MAX_SKIN_ARRAY_SIZE || PartNameCount > protocol7::MAX_SKIN_LENGTH)
	{
		log_error("skins7", "Failed to load skin part '%s/%s': name too long", CSkins7::ms_apSkinPartNames[PartType], pName);
		return;
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

	auto pJob = std::make_shared<CSkinPartLoadJob>(aFilename, aPartName, Storage(), DirType, PartType, Flags);
	Engine()->AddJob(pJob);
	m_PendingSkinPartJobs.push_back(pJob);
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
		const auto &Part = Parts[m_NextPart++];
		StartSkinPartLoadJob(Part.m_Part, Part.m_Name.c_str(), Part.m_StorageType);
		++Scheduled;
	}
	// GPU 句柄只能在主线程创建，读盘和解码已由描述任务完成。
	// 1ms 是批处理软预算，单次驱动上传无法抢占，上传后不再开始超预算的下一项。
	while(m_NextAccessory < 2 && GameClient()->GpuUploadLimiter()->CanUpload(1) && time_get_nanoseconds() - Begin < std::chrono::milliseconds(1))
	{
		const bool Hat = m_NextAccessory++ == 0;
		auto Image = Hat ? m_pSkinInfoJob->TakeXmasHat() : m_pSkinInfoJob->TakeBot();
		auto &Texture = Hat ? m_XmasHatTexture : m_BotTexture;
		Graphics()->UnloadTexture(&Texture);
		if(Image.m_pData)
		{
			Texture = Graphics()->LoadTextureRawMove(Image, 0, Hat ? "skins7/xmas_hat.png" : "skins7/bot.png");
			GameClient()->GpuUploadLimiter()->OnUploaded();
		}
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

		if(!GameClient()->GpuUploadLimiter()->CanUpload(2))
		{
			break;
		}

		CSkinPartLoadJob::SResult Result = pJob->GetResult();
		if(Result.m_Success)
		{
			CSkinPart Part;
			Part.m_Type = Result.m_PartType;
			Part.m_Flags = Result.m_Flags;
			str_copy(Part.m_aName, Result.m_aName, sizeof(Part.m_aName));
			Part.m_OriginalTexture = Graphics()->LoadTextureRaw(Result.m_OriginalImage, 0, Result.m_aName);
			GameClient()->GpuUploadLimiter()->OnUploaded();
			Part.m_BloodColor = Result.m_BloodColor;
			// CPU 素材已随任务结果发布；主线程只接管并上传纹理。
			Part.m_QmSkinOutline = std::move(Result.m_QmSkinOutline);
			Part.m_pChatAvatarOriginal = std::move(Result.m_pChatAvatarOriginal);
			Part.m_pChatAvatarColorable = std::move(Result.m_pChatAvatarColorable);
			Part.m_ColorableTexture = Graphics()->LoadTextureRawMove(Result.m_GrayscaleImage, 0, Result.m_aName);
			GameClient()->GpuUploadLimiter()->OnUploaded();
			Result.m_OriginalImage.Free();

			if(Config()->m_Debug)
			{
				log_trace("skins7", "Loaded skin part '%s/%s'", CSkins7::ms_apSkinPartNames[Result.m_PartType], Part.m_aName);
			}
			m_avSkinParts[Result.m_PartType].emplace_back(Part);
		}

		Iter = m_PendingSkinPartJobs.erase(Iter);
	}

	if(m_NextPart == Parts.size() && m_NextAccessory == 2 && m_PendingSkinPartJobs.empty() && m_Loading && time_get_nanoseconds() - Begin < std::chrono::milliseconds(1))
	{
		if(!RebuildSkins())
			return;
		m_Loading = false;
		m_ResourceLifecycle.CompleteLoad();
		if(QmPerfEnabled())
		{
			char aPayload[96];
			str_format(aPayload, sizeof(aPayload), "event=resource_load_complete skins=%zu", m_vSkins.size());
			QmPerfLogPayloadForce("perf/skins7", aPayload, Client());
		}
		m_LastRefreshTime = time_get_nanoseconds();
		if(m_SkinLoadedCallback)
			m_SkinLoadedCallback();
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
		const auto &Entry = Entries[m_NextSkin++];
		LoadSkin(Entry.m_Name.c_str(), Entry.m_StorageType, *Entry.m_pInfo);
		// 每项绑定后即释放 JSON，避免末帧集中销毁全部解析树。
		m_pSkinInfoJob->ReleaseEntry(m_NextSkin - 1);
		++Processed;
	}
	if(m_NextSkin != Entries.size())
		return false;
	m_pSkinInfoJob.reset();
	return true;
}

bool CSkins7::LoadSkin(const char *pName, int DirType, const json_value &Info)
{
	char aFilename[IO_MAX_PATH_LENGTH];
	str_format(aFilename, sizeof(aFilename), SKINS_DIR "/%s.json", pName);

	CSkin Skin;
	str_copy(Skin.m_aName, pName);
	const bool SpecialSkin = IsSpecialSkin(Skin.m_aName);
	Skin.m_Flags = 0;
	if(SpecialSkin)
	{
		Skin.m_Flags |= SKINFLAG_SPECIAL;
	}
	if(DirType != IStorage::TYPE_SAVE)
	{
		Skin.m_Flags |= SKINFLAG_STANDARD;
	}

	const json_value &Start = Info["skin"];

	for(int PartIndex = 0; PartIndex < protocol7::NUM_SKINPARTS; ++PartIndex)
	{
		Skin.m_aUseCustomColors[PartIndex] = 0;
		Skin.m_aPartColors[PartIndex] = (PartIndex == protocol7::SKINPART_MARKING ? 0xFF000000u : 0u) + 0x00FF80u;

		const json_value &Part = Start[(const char *)ms_apSkinPartNames[PartIndex]];
		if(Part.type == json_none)
		{
			Skin.m_apParts[PartIndex] = FindDefaultSkinPart(PartIndex);
			continue;
		}
		if(Part.type != json_object)
		{
			log_error("skins7", "Failed to parse skin json file '%s': attribute '%s' must specify an object", aFilename, ms_apSkinPartNames[PartIndex]);
			return false;
		}

		const json_value &Filename = Part["filename"];
		if(Filename.type == json_string)
		{
			Skin.m_apParts[PartIndex] = FindSkinPart(PartIndex, (const char *)Filename, SpecialSkin);
		}
		else
		{
			log_error("skins7", "Failed to parse skin json file '%s': part '%s' attribute 'filename' must specify a string", aFilename, ms_apSkinPartNames[PartIndex]);
			return false;
		}

		bool UseCustomColors = false;
		const json_value &Color = Part["custom_colors"];
		if(Color.type == json_string)
			UseCustomColors = str_comp((const char *)Color, "true") == 0;
		else if(Color.type == json_boolean)
			UseCustomColors = Color.u.boolean;
		Skin.m_aUseCustomColors[PartIndex] = UseCustomColors;

		if(!UseCustomColors)
			continue;

		for(int i = 0; i < NUM_COLOR_COMPONENTS; i++)
		{
			if(PartIndex != protocol7::SKINPART_MARKING && i == 3)
				continue;

			const json_value &Component = Part[(const char *)ms_apColorComponents[i]];
			if(Component.type == json_integer)
			{
				switch(i)
				{
				case 0: Skin.m_aPartColors[PartIndex] = (Skin.m_aPartColors[PartIndex] & 0xFF00FFFFu) | (Component.u.integer << 16); break;
				case 1: Skin.m_aPartColors[PartIndex] = (Skin.m_aPartColors[PartIndex] & 0xFFFF00FFu) | (Component.u.integer << 8); break;
				case 2: Skin.m_aPartColors[PartIndex] = (Skin.m_aPartColors[PartIndex] & 0xFFFFFF00u) | Component.u.integer; break;
				case 3: Skin.m_aPartColors[PartIndex] = (Skin.m_aPartColors[PartIndex] & 0x00FFFFFFu) | (Component.u.integer << 24); break;
				}
			}
		}
	}

	if(Config()->m_Debug)
	{
		log_trace("skins7", "Loaded skin '%s'", Skin.m_aName);
	}
	m_vSkins.insert(std::lower_bound(m_vSkins.begin(), m_vSkins.end(), Skin), Skin);
	return true;
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
	for(auto &pJob : m_PendingSkinPartJobs)
		pJob->Abort();
	m_PendingSkinPartJobs.clear();
	if(m_pSkinInfoJob)
		m_pSkinInfoJob->Abort();
	m_pSkinInfoJob.reset();
	m_Loading = false;
	m_SkinLoadedCallback = {};
}

void CSkins7::OnUpdate()
{
	const auto Action = m_ResourceLifecycle.Update(Client()->State(), Client()->IsSixup());
	if(Action == CQmSkin7ResourceLifecycle::EAction::LOAD)
		GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SEVEN);
	else if(Action == CQmSkin7ResourceLifecycle::EAction::CANCEL)
	{
		if(QmPerfEnabled())
			QmPerfLogPayloadForce("perf/skins7", "event=resource_load_cancel", Client());
		CancelPendingLoads();
	}
	if(QmNeedsSkin7Resources(Client()->State(), Client()->IsSixup()))
		ProcessCompletedJobs();
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
	// 通用皮肤刷新、设置页预热等入口也受同一规则约束，不能绕过按需启用。
	if(!m_ResourceLifecycle.BeginRefresh(Client()->State(), Client()->IsSixup()))
		return;
	if(QmPerfEnabled())
		QmPerfLogPayloadForce("perf/skins7", "event=resource_load_begin", Client());
	m_vSkins.clear();
	m_NextPart = 0;
	m_NextSkin = 0;
	m_NextAccessory = 0;
	if(m_pSkinInfoJob)
		m_pSkinInfoJob->Abort();
	m_pSkinInfoJob = std::make_shared<CQmSkin7InfoJob>(Storage());
	Engine()->AddJob(m_pSkinInfoJob);

	for(auto &pJob : m_PendingSkinPartJobs)
	{
		pJob->Abort();
	}
	m_PendingSkinPartJobs.clear();

	for(int Part = 0; Part < protocol7::NUM_SKINPARTS; Part++)
	{
		for(CSkinPart &SkinPart : m_avSkinParts[Part])
		{
			Graphics()->UnloadTexture(&SkinPart.m_OriginalTexture);
			Graphics()->UnloadTexture(&SkinPart.m_ColorableTexture);
			if(SkinPart.m_QmSkinOutline)
				SkinPart.m_QmSkinOutline->Unload(Graphics());
		}
		m_avSkinParts[Part].clear();

		if(Part == protocol7::SKINPART_MARKING || Part == protocol7::SKINPART_DECORATION)
		{
			CSkinPart NoneSkinPart;
			NoneSkinPart.m_Type = Part;
			NoneSkinPart.m_Flags = SKINFLAG_STANDARD;
			NoneSkinPart.m_aName[0] = '\0';
			NoneSkinPart.m_BloodColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
			m_avSkinParts[Part].emplace_back(NoneSkinPart);
		}
	}

	m_SkinLoadedCallback = std::move(SkinLoadedCallback);
	m_Loading = true;

	ProcessCompletedJobs();
}

void CSkins7::AddSkinFromConfigVariables(const char *pName, int Dummy)
{
	auto OldSkin = std::find_if(m_vSkins.begin(), m_vSkins.end(), [pName](const CSkin &Skin) {
		return str_comp(Skin.m_aName, pName) == 0;
	});
	if(OldSkin != m_vSkins.end())
	{
		m_vSkins.erase(OldSkin);
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
	m_vSkins.insert(std::lower_bound(m_vSkins.begin(), m_vSkins.end(), NewSkin), NewSkin);
	m_LastRefreshTime = time_get_nanoseconds();
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

static void SaveSkinfilePart(CJsonFileWriter &Writer, const char *pPartName, const char *pFilename)
{
	if(pFilename == nullptr || pFilename[0] == '\0')
		return;

	Writer.WriteAttribute(pPartName);
	Writer.BeginObject();
	Writer.WriteAttribute("filename");
	Writer.WriteStrValue(pFilename);
	Writer.WriteAttribute("custom_colors");
	Writer.WriteBoolValue(false);
	Writer.EndObject();
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
bool SaveSkinfileFromParts(IStorage *pStorage, const char *pName, const char *pBodyPartName, const char *pMarkingPartName, const char *pDecorationPartName, const char *pHandsPartName, const char *pFeetPartName, const char *pEyesPartName)
{
	if(pStorage == nullptr || pName == nullptr || pName[0] == '\0')
		return false;

	char aBuf[IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), SKINS_DIR "/%s.json", pName);
	IOHANDLE File = pStorage->OpenFile(aBuf, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;

	CJsonFileWriter Writer(File);
	Writer.BeginObject();
	Writer.WriteAttribute("skin");
	Writer.BeginObject();
	SaveSkinfilePart(Writer, CSkins7::ms_apSkinPartNames[protocol7::SKINPART_BODY], pBodyPartName);
	SaveSkinfilePart(Writer, CSkins7::ms_apSkinPartNames[protocol7::SKINPART_MARKING], pMarkingPartName);
	SaveSkinfilePart(Writer, CSkins7::ms_apSkinPartNames[protocol7::SKINPART_DECORATION], pDecorationPartName);
	SaveSkinfilePart(Writer, CSkins7::ms_apSkinPartNames[protocol7::SKINPART_HANDS], pHandsPartName);
	SaveSkinfilePart(Writer, CSkins7::ms_apSkinPartNames[protocol7::SKINPART_FEET], pFeetPartName);
	SaveSkinfilePart(Writer, CSkins7::ms_apSkinPartNames[protocol7::SKINPART_EYES], pEyesPartName);
	Writer.EndObject();
	Writer.EndObject();
	return true;
}

bool CSkins7::SaveSkinfile(const char *pName, int Dummy)
{
	dbg_assert(!IsSpecialSkin(pName), "Cannot save special skins");

	char aBuf[IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), SKINS_DIR "/%s.json", pName);
	IOHANDLE File = Storage()->OpenFile(aBuf, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;

	CJsonFileWriter Writer(File);

	Writer.BeginObject();
	Writer.WriteAttribute("skin");
	Writer.BeginObject();
	for(int PartIndex = 0; PartIndex < protocol7::NUM_SKINPARTS; PartIndex++)
	{
		if(!ms_apSkinVariables[Dummy][PartIndex][0])
			continue;

		// part start
		Writer.WriteAttribute(ms_apSkinPartNames[PartIndex]);
		Writer.BeginObject();
		{
			Writer.WriteAttribute("filename");
			Writer.WriteStrValue(ms_apSkinVariables[Dummy][PartIndex]);

			const bool CustomColors = *ms_apUCCVariables[Dummy][PartIndex];
			Writer.WriteAttribute("custom_colors");
			Writer.WriteBoolValue(CustomColors);

			if(CustomColors)
			{
				for(int ColorComponent = 0; ColorComponent < NUM_COLOR_COMPONENTS - 1; ColorComponent++)
				{
					int Val = (*ms_apColorVariables[Dummy][PartIndex] >> (2 - ColorComponent) * 8) & 0xff;
					Writer.WriteAttribute(ms_apColorComponents[ColorComponent]);
					Writer.WriteIntValue(Val);
				}
				if(PartIndex == protocol7::SKINPART_MARKING)
				{
					int Val = (*ms_apColorVariables[Dummy][PartIndex] >> 24) & 0xff;
					Writer.WriteAttribute(ms_apColorComponents[3]);
					Writer.WriteIntValue(Val);
				}
			}
		}
		Writer.EndObject();
	}
	Writer.EndObject();
	Writer.EndObject();

	AddSkinFromConfigVariables(pName, Dummy);
	return true;
}
