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

#include <game/client/gameclient.h>

CCountryFlags::CCountryFlagLoadJob::CCountryFlagLoadJob(const char *pPath, int CountryCode, IStorage *pStorage) :
	m_Path(pPath),
	m_CountryCode(CountryCode),
	m_pStorage(pStorage)
{
	m_Result.m_CountryCode = CountryCode;
}

CCountryFlags::CCountryFlagLoadJob::~CCountryFlagLoadJob()
{
	m_Result.m_Image.Free();
}

void CCountryFlags::CCountryFlagLoadJob::Run()
{
	void *pFileData = nullptr;
	unsigned FileSize = 0;
	if(!m_pStorage->ReadFile(m_Path.c_str(), IStorage::TYPE_ALL, &pFileData, &FileSize))
	{
		log_error("countryflags", "Failed to read flag file '%s'", m_Path.c_str());
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_Completed = true;
		return;
	}

	CImageInfo Image;
	if(!CImageLoader::LoadPng(static_cast<uint8_t *>(pFileData), FileSize, m_Path.c_str(), Image))
	{
		free(pFileData);
		log_error("countryflags", "Failed to decode flag PNG '%s'", m_Path.c_str());
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_Completed = true;
		return;
	}
	free(pFileData);

	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_Result.m_Image = std::move(Image);
		m_Result.m_Success = true;
		m_Completed = true;
	}
}

void CCountryFlags::LoadCountryflagsIndexfile()
{
	const char *pFilename = "countryflags/index.txt";
	CLineReader LineReader;
	if(!LineReader.OpenFile(Storage()->OpenFile(pFilename, IOFLAG_READ, IStorage::TYPE_ALL)))
	{
		log_error("countryflags", "couldn't open index file '%s'", pFilename);
		return;
	}

	char aOrigin[128];
	while(const char *pLine = LineReader.Get())
	{
		if(!str_length(pLine) || pLine[0] == '#')
			continue;

		str_copy(aOrigin, pLine);
		const char *pReplacement = LineReader.Get();
		if(!pReplacement)
		{
			log_error("countryflags", "unexpected end of index file");
			break;
		}

		if(pReplacement[0] != '=' || pReplacement[1] != '=' || pReplacement[2] != ' ')
		{
			log_error("countryflags", "malformed replacement for index '%s'", aOrigin);
			continue;
		}

		int CountryCode = str_toint(pReplacement + 3);
		if(CountryCode < CODE_LB || CountryCode > CODE_UB)
		{
			log_error("countryflags", "country code '%i' not within valid code range [%i..%i]", CountryCode, CODE_LB, CODE_UB);
			continue;
		}

		CCountryFlag CountryFlag;
		CountryFlag.m_CountryCode = CountryCode;
		str_copy(CountryFlag.m_aCountryCodeString, aOrigin);
		CountryFlag.m_Loaded = false;
		m_vCountryFlags.push_back(CountryFlag);
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
}

void CCountryFlags::ProcessCompletedJobs()
{
	auto it = m_PendingJobs.begin();
	while(it != m_PendingJobs.end())
	{
		auto &pJob = *it;
		if(!pJob->IsCompleted())
		{
			++it;
			continue;
		}

		if(!GameClient()->GpuUploadLimiter()->CanUpload())
		{
			break;
		}

		CCountryFlagLoadJob::SResult Result = pJob->GetResult();
		if(Result.m_Success)
		{
			for(auto &Flag : m_vCountryFlags)
			{
				if(Flag.m_CountryCode == Result.m_CountryCode)
				{
					Flag.m_Texture = Graphics()->LoadTextureRawMove(Result.m_Image, 0, Flag.m_aCountryCodeString);
					Flag.m_Loaded = true;
					GameClient()->GpuUploadLimiter()->OnUploaded();

					if(g_Config.m_Debug)
					{
						log_debug("countryflags", "loaded country flag '%s'", Flag.m_aCountryCodeString);
					}
					break;
				}
			}
		}

		it = m_PendingJobs.erase(it);
	}
}

void CCountryFlags::OnInit()
{
	m_vCountryFlags.clear();
	LoadCountryflagsIndexfile();
	if(m_vCountryFlags.empty())
	{
		log_error("countryflags", "failed to load country flags. folder='countryflags/'");
		CCountryFlag DummyEntry;
		DummyEntry.m_CountryCode = -1;
		DummyEntry.m_aCountryCodeString[0] = '\0';
		DummyEntry.m_Loaded = false;
		m_vCountryFlags.push_back(DummyEntry);
		m_vLoadTriggered.push_back(false);
	}

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
	size_t Index = m_aCodeIndexLUT[maximum(0, (CountryCode - CODE_LB) % CODE_RANGE)];
	const_cast<CCountryFlags *>(this)->StartFlagLoadJob(Index);
	return m_vCountryFlags[Index];
}

const CCountryFlags::CCountryFlag &CCountryFlags::GetByIndex(size_t Index) const
{
	const_cast<CCountryFlags *>(this)->StartFlagLoadJob(Index);
	return m_vCountryFlags[Index % m_vCountryFlags.size()];
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
