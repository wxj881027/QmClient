/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "countryflags.h"

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <game/client/components/menus.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/settings_runtime_cache.h>
#include <game/client/gameclient.h>

namespace
{
	bool CountryFlagsPerfDebugEnabled()
	{
		return g_Config.m_QmPerfDebug != 0;
	}

	void LogCountryFlagsPerfStage(const char *pStage, const char *pExtra)
	{
		if(!CountryFlagsPerfDebugEnabled())
			return;
		QmPerfLogStage("perf/countryflags", pStage, 0.0, true, nullptr, nullptr, nullptr, pExtra);
	}

	void LogCountryFlagSettingsResourcePerf(const char *pJob, int Count, int Budget, int Remaining, ESettingsWarmupMissReason Reason, double DurationMs)
	{
		LogSettingsResourcePerf(CMenus::SETTINGS_LANGUAGE, pJob, Count, Budget, Remaining, Reason, DurationMs);
		LogSettingsResourcePerf(CMenus::SETTINGS_PLAYER, pJob, Count, Budget, Remaining, Reason, DurationMs);
	}
}

CCountryFlags::CCountryFlagLoadJob::CCountryFlagLoadJob(const char *pPath, int CountryCode, IStorage *pStorage) :
	m_Path(pPath),
	m_pStorage(pStorage)
{
	m_Result.m_CountryCode = CountryCode;
}

CCountryFlags::CCountryFlagLoadJob::~CCountryFlagLoadJob()
{
	m_Result.m_Image.Free();
}

bool CCountryFlags::ValidateCountryCodeString(const char *pString)
{
	const size_t Length = str_length(pString);
	if(Length == 0 || Length >= sizeof(CCountryFlag{}.m_aCountryCodeString))
	{
		log_error("countryflags", "Country name '%s' is invalid or too long", pString);
		return false;
	}

	for(size_t Index = 0; Index < Length; ++Index)
	{
		const char Character = pString[Index];
		if(Character != '-' &&
			!(Character >= 'a' && Character <= 'z') &&
			!(Character >= 'A' && Character <= 'Z'))
		{
			log_error("countryflags", "Country name '%s' must only contain letters and dash characters", pString);
			return false;
		}
	}

	return true;
}

bool CCountryFlags::ValidateCountryCodeIntegerString(const char *pString)
{
	if(pString[0] == '\0')
		return false;

	size_t Index = pString[0] == '-' ? 1 : 0;
	if(pString[Index] == '\0')
		return false;

	for(; pString[Index] != '\0'; ++Index)
	{
		if(pString[Index] < '0' || pString[Index] > '9')
			return false;
	}

	return true;
}

void CCountryFlags::CCountryFlagLoadJob::Run()
{
	void *pFileData = nullptr;
	unsigned FileSize = 0;
	if(!m_pStorage->ReadFile(m_Path.c_str(), IStorage::TYPE_ALL, &pFileData, &FileSize))
	{
		log_error("countryflags", "Failed to read flag file '%s'", m_Path.c_str());
		CLockScope Lock(m_Mutex);
		m_Completed = true;
		return;
	}

	CImageInfo Image;
	if(!CImageLoader::LoadPng(static_cast<uint8_t *>(pFileData), FileSize, m_Path.c_str(), Image))
	{
		free(pFileData);
		log_error("countryflags", "Failed to decode flag PNG '%s'", m_Path.c_str());
		CLockScope Lock(m_Mutex);
		m_Completed = true;
		return;
	}
	free(pFileData);

	{
		CLockScope Lock(m_Mutex);
		m_Result.m_Image = std::move(Image);
		m_Result.m_Success = true;
		m_Completed = true;
	}
}

void CCountryFlags::LoadCountryflagsIndexfile()
{
	m_vCountryFlags.clear();
	m_vLoadTriggered.clear();

	const char *pFilename = "countryflags/index.txt";
	CLineReader LineReader;
	if(!LineReader.OpenFile(Storage()->OpenFile(pFilename, IOFLAG_READ, IStorage::TYPE_ALL)))
	{
		log_error("countryflags", "couldn't open index file '%s'", pFilename);
	}
	else
	{
		while(const char *pLine = LineReader.Get())
		{
			if(pLine[0] == '\0' || pLine[0] == '#')
				continue;

			if(!ValidateCountryCodeString(pLine))
				continue;

			CCountryFlag CountryFlag;
			str_copy(CountryFlag.m_aCountryCodeString, pLine);
			const char *pReplacement = LineReader.Get();
			if(!pReplacement)
			{
				log_error("countryflags", "unexpected end of index file after country '%s'", CountryFlag.m_aCountryCodeString);
				break;
			}

			if(pReplacement[0] != '=' || pReplacement[1] != '=' || pReplacement[2] != ' ')
			{
				log_error("countryflags", "malformed replacement for index '%s'", CountryFlag.m_aCountryCodeString);
				continue;
			}

			const char *pCountryCode = pReplacement + 3;
			if(!ValidateCountryCodeIntegerString(pCountryCode))
			{
				log_error("countryflags", "country code '%s' for country '%s' is not a number", pCountryCode, CountryFlag.m_aCountryCodeString);
				continue;
			}

			CountryFlag.m_CountryCode = str_toint(pCountryCode);
			if(CountryFlag.m_CountryCode < CODE_LB || CountryFlag.m_CountryCode > CODE_UB)
			{
				log_error("countryflags", "country code '%i' not within valid code range [%i..%i]", CountryFlag.m_CountryCode, CODE_LB, CODE_UB);
				continue;
			}
			if(CountryFlag.m_CountryCode == -1 && str_comp(CountryFlag.m_aCountryCodeString, "default") != 0)
			{
				log_error("countryflags", "country code '-1' is only allowed for the default country");
				continue;
			}

			CountryFlag.m_Loaded = false;
			m_vCountryFlags.push_back(CountryFlag);
		}
	}

	auto ExistingDefaultFlag = std::find_if(m_vCountryFlags.begin(), m_vCountryFlags.end(), [](const CCountryFlag &Flag) {
		return Flag.m_CountryCode == -1;
	});
	if(ExistingDefaultFlag == m_vCountryFlags.end())
	{
		CCountryFlag DefaultFlag;
		DefaultFlag.m_CountryCode = -1;
		str_copy(DefaultFlag.m_aCountryCodeString, "default");
		DefaultFlag.m_Loaded = false;
		m_vCountryFlags.push_back(DefaultFlag);
	}

	std::sort(m_vCountryFlags.begin(), m_vCountryFlags.end());

	m_vLoadTriggered.resize(m_vCountryFlags.size(), false);

	size_t DefaultIndex = 0;
	for(size_t Index = 0; Index < m_vCountryFlags.size(); ++Index)
		if(m_vCountryFlags[Index].m_CountryCode == -1)
		{
			DefaultIndex = Index;
			break;
		}

	std::fill(std::begin(m_aCodeIndexLUT), std::end(m_aCodeIndexLUT), DefaultIndex);
	for(size_t i = 0; i < m_vCountryFlags.size(); ++i)
		m_aCodeIndexLUT[maximum(0, (m_vCountryFlags[i].m_CountryCode - CODE_LB) % CODE_RANGE)] = i;

	log_debug("countryflags", "Loaded %" PRIzu " country flags", m_vCountryFlags.size());
}

void CCountryFlags::StartFlagLoadJob(int Index)
{
	if(Index < 0 || Index >= (int)m_vCountryFlags.size())
		return;

	if(m_vLoadTriggered[Index])
		return;

	m_vLoadTriggered[Index] = true;

	char aPath[128];
	str_format(aPath, sizeof(aPath), "countryflags/%s.png", m_vCountryFlags[Index].m_aCountryCodeString);

	auto pJob = std::make_shared<CCountryFlagLoadJob>(aPath, m_vCountryFlags[Index].m_CountryCode, Storage());
	Engine()->AddJob(pJob);
	m_PendingJobs.push_back(pJob);
	LogCountryFlagSettingsResourcePerf("queued", 1, 1, (int)m_PendingJobs.size(), ESettingsWarmupMissReason::RESOURCE_PLAN_PENDING, 0.0);
}

void CCountryFlags::ProcessCompletedJobs()
{
	auto Iter = m_PendingJobs.begin();
	while(Iter != m_PendingJobs.end())
	{
		auto &pJob = *Iter;
		if(!pJob->IsCompleted())
		{
			++Iter;
			continue;
		}

		if(!GameClient()->GpuUploadLimiter()->CanUpload())
		{
			LogCountryFlagsPerfStage("countryflags_gpu_upload_budget", SettingsWarmupMissReasonName(ESettingsWarmupMissReason::GPU_UPLOAD_BUDGET));
			LogCountryFlagSettingsResourcePerf("upload", 0, 1, (int)m_PendingJobs.size(), ESettingsWarmupMissReason::GPU_UPLOAD_BUDGET, 0.0);
			break;
		}

		CCountryFlagLoadJob::SResult Result = pJob->GetResult();
		LogCountryFlagSettingsResourcePerf("complete", Result.m_Success ? 1 : 0, 1, (int)m_PendingJobs.size() - 1, Result.m_Success ? ESettingsWarmupMissReason::NONE : ESettingsWarmupMissReason::JOB_RESULT_PENDING, 0.0);
		if(Result.m_Success)
		{
			for(auto &Flag : m_vCountryFlags)
			{
				if(Flag.m_CountryCode == Result.m_CountryCode)
				{
					Flag.m_Texture = Graphics()->LoadTextureRawMove(Result.m_Image, 0, Flag.m_aCountryCodeString);
					Flag.m_Loaded = true;
					GameClient()->GpuUploadLimiter()->OnUploaded();
					LogCountryFlagSettingsResourcePerf("upload", 1, 1, (int)m_PendingJobs.size() - 1, ESettingsWarmupMissReason::NONE, 0.0);

					if(g_Config.m_Debug)
					{
						log_debug("countryflags", "loaded country flag '%s'", Flag.m_aCountryCodeString);
					}
					break;
				}
			}
		}

		Iter = m_PendingJobs.erase(Iter);
	}
}

void CCountryFlags::OnInit()
{
	LoadCountryflagsIndexfile();

	m_FlagsQuadContainerIndex = Graphics()->CreateQuadContainer(false);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	Graphics()->QuadContainerAddSprite(m_FlagsQuadContainerIndex, 0.0f, 0.0f, 1.0f, 1.0f);
	Graphics()->QuadContainerUpload(m_FlagsQuadContainerIndex);
}

void CCountryFlags::OnReset()
{
	ProcessCompletedJobs();
}

size_t CCountryFlags::Num() const
{
	return m_vCountryFlags.size();
}

const CCountryFlags::CCountryFlag &CCountryFlags::GetByCountryCode(int CountryCode) const
{
	CountryCode = QmNormalizeCountryCode(CountryCode);
	size_t Index = m_aCodeIndexLUT[maximum(0, (CountryCode - CODE_LB) % CODE_RANGE)];
	const_cast<CCountryFlags *>(this)->StartFlagLoadJob(Index);
	return m_vCountryFlags[Index];
}

const CCountryFlags::CCountryFlag &CCountryFlags::GetByIndex(size_t Index) const
{
	dbg_assert(Index < m_vCountryFlags.size(), "Invalid Index: %" PRIzu, Index);
	const_cast<CCountryFlags *>(this)->StartFlagLoadJob(Index);
	return m_vCountryFlags[Index];
}

void CCountryFlags::PrewarmByCountryCodes(const std::vector<int> &vCountryCodes)
{
	for(int CountryCode : vCountryCodes)
	{
		CountryCode = QmNormalizeCountryCode(CountryCode);
		size_t Index = m_aCodeIndexLUT[maximum(0, (CountryCode - CODE_LB) % CODE_RANGE)];
		StartFlagLoadJob((int)Index);
	}
}

void CCountryFlags::PrewarmByIndices(const std::vector<int> &vIndices)
{
	for(int Index : vIndices)
		StartFlagLoadJob(Index);
}

bool CCountryFlags::PrewarmByCountryCodesReady(const std::vector<int> &vCountryCodes)
{
	constexpr int MaxStartsPerCall = 16;
	int StartsThisCall = 0;
	for(int CountryCode : vCountryCodes)
	{
		CountryCode = QmNormalizeCountryCode(CountryCode);
		size_t Index = m_aCodeIndexLUT[maximum(0, (CountryCode - CODE_LB) % CODE_RANGE)];
		if(Index >= m_vCountryFlags.size() || m_vLoadTriggered[Index])
			continue;
		StartFlagLoadJob((int)Index);
		if(++StartsThisCall >= MaxStartsPerCall)
			break;
	}
	ProcessCompletedJobs();
	for(int CountryCode : vCountryCodes)
	{
		CountryCode = QmNormalizeCountryCode(CountryCode);
		size_t Index = m_aCodeIndexLUT[maximum(0, (CountryCode - CODE_LB) % CODE_RANGE)];
		if(Index >= m_vCountryFlags.size() || !m_vCountryFlags[Index].m_Loaded)
			return false;
	}
	return true;
}

bool CCountryFlags::PrewarmByIndicesReady(const std::vector<int> &vIndices)
{
	constexpr int MaxStartsPerCall = 16;
	int StartsThisCall = 0;
	for(int Index : vIndices)
	{
		if(Index < 0 || Index >= (int)m_vCountryFlags.size() || m_vLoadTriggered[Index])
			continue;
		StartFlagLoadJob(Index);
		if(++StartsThisCall >= MaxStartsPerCall)
			break;
	}
	ProcessCompletedJobs();
	for(int Index : vIndices)
	{
		if(Index < 0 || Index >= (int)m_vCountryFlags.size() || !m_vCountryFlags[Index].m_Loaded)
			return false;
	}
	return true;
}

void CCountryFlags::Render(const CCountryFlag &Flag, ColorRGBA Color, float x, float y, float w, float h)
{
	ProcessCompletedJobs();
	if(Flag.m_Texture.IsValid())
	{
		Graphics()->TextureSet(Flag.m_Texture);
		Graphics()->SetColor(Color);
		Graphics()->QuadsSetRotation(0.0f);
		Graphics()->RenderQuadContainerEx(m_FlagsQuadContainerIndex, 0, -1, x, y, w, h);
	}
}

void CCountryFlags::Render(int CountryCode, ColorRGBA Color, float x, float y, float w, float h)
{
	Render(GetByCountryCode(CountryCode), Color, x, y, w, h);
}
