#include "mapsounds.h"

#include <base/log.h>

#include <engine/demo.h>
#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/sound.h>

#include <game/client/components/camera.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/layers.h>
#include <game/localization.h>
#include <game/map/map_render_items.h>
#include <game/mapitems.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>

class CQmMapSoundDecodeJob final : public IJob
{
public:
	struct SInput
	{
		int m_SoundIndex = -1;
		std::vector<unsigned char> m_vData;
		std::string m_Path;
		std::string m_Name;
	};

private:
	ISound *m_pSound;
	std::vector<SInput> m_vInputs;
	std::vector<int> m_vSampleIds;
	std::atomic<bool> m_InlineComplete = false;
	int64_t m_StartNs = time_get_nanoseconds().count();
	int64_t m_EndNs = 0;

protected:
	void Run() override { Decode(); }

public:
	CQmMapSoundDecodeJob(ISound *pSound, std::vector<SInput> &&vInputs) :
		m_pSound(pSound),
		m_vInputs(std::move(vInputs)),
		m_vSampleIds(m_vInputs.size(), -1)
	{
	}

	void Decode()
	{
		for(size_t Index = 0; Index < m_vInputs.size(); ++Index)
		{
			const SInput &Input = m_vInputs[Index];
			try
			{
				if(!Input.m_Path.empty())
					m_vSampleIds[Index] = m_pSound->LoadOpus(Input.m_Path.c_str());
				else if(!Input.m_vData.empty())
					m_vSampleIds[Index] = m_pSound->LoadOpusFromMem(Input.m_vData.data(), Input.m_vData.size(), false, Input.m_Name.c_str());
			}
			catch(...)
			{
				m_vSampleIds[Index] = -1;
			}
		}
		m_EndNs = time_get_nanoseconds().count();
		m_InlineComplete.store(true, std::memory_order_release);
	}

	bool Ready() const { return m_InlineComplete.load(std::memory_order_acquire) || Finished(); }
	bool InlineComplete() const { return m_InlineComplete.load(std::memory_order_acquire); }
	const std::vector<SInput> &Inputs() const { return m_vInputs; }
	const std::vector<int> &SampleIds() const { return m_vSampleIds; }
	double ElapsedMs() const { return (m_EndNs - m_StartNs) / 1000000.0; }
};

static float s_MapSoundTime = 0.0f;

CMapSounds::CMapSounds()
{
	m_Count = 0;
	std::fill(std::begin(m_aSounds), std::end(m_aSounds), -1);
}

CMapSounds::~CMapSounds()
{
	while((m_pDecodeJob && !m_pDecodeJob->Ready()) || std::any_of(m_vRetiredDecodeJobs.begin(), m_vRetiredDecodeJobs.end(), [](const auto &pJob) { return pJob && !pJob->Ready(); }))
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	if(Sound())
	{
		for(int Index = 0; Index < m_Count; ++Index)
			Sound()->UnloadSample(m_aSounds[Index]);
		if(m_pDecodeJob)
			for(int SampleId : m_pDecodeJob->SampleIds())
				Sound()->UnloadSample(SampleId);
		for(const auto &pJob : m_vRetiredDecodeJobs)
			if(pJob)
				for(int SampleId : pJob->SampleIds())
					Sound()->UnloadSample(SampleId);
	}
}

void CMapSounds::Play(int Channel, int SoundId)
{
	if(!SoundEnabled() || !Layers()->Map())
		return;
	if(SoundId < 0 || SoundId >= m_Count)
		return;

	GameClient()->m_Sounds.PlaySample(Channel, m_aSounds[SoundId], 0, 1.0f);
}

void CMapSounds::PlayAt(int Channel, int SoundId, vec2 Position)
{
	if(!SoundEnabled() || !Layers()->Map())
		return;
	if(SoundId < 0 || SoundId >= m_Count)
		return;

	GameClient()->m_Sounds.PlaySampleAt(Channel, m_aSounds[SoundId], 0, 1.0f, Position);
}

void CMapSounds::StopAll()
{
	for(auto &Source : m_vSourceQueue)
	{
		if(Source.m_Voice.IsValid())
		{
			Sound()->StopVoice(Source.m_Voice);
			Source.m_Voice = ISound::CVoiceHandle();
		}
	}
}

void CMapSounds::OnMapLoad()
{
	IMap *pMap = Kernel()->RequestInterface<IMap>();

	Clear();

	if(!Sound()->IsSoundEnabled())
		return;

	// load samples
	int Start;
	pMap->GetType(MAPITEMTYPE_SOUND, &Start, &m_Count);

	m_Count = std::clamp<int>(m_Count, 0, MAX_MAPSOUNDS);
	const CCollision &MapCollision = *Collision();
	const auto ReleaseSoundData = [&](int DataIndex, const void *pData) {
		// 声音条目可与碰撞层共用数据索引，生命周期由地图管理。
		if(pData == MapCollision.GameLayer() || pData == MapCollision.TeleLayer() ||
			pData == MapCollision.SpeedupLayer() || pData == MapCollision.FrontLayer() ||
			pData == MapCollision.SwitchLayer() || pData == MapCollision.TuneLayer())
			return;
		pMap->UnloadData(DataIndex);
	};

	std::vector<CQmMapSoundDecodeJob::SInput> vInputs;
	bool ShowWarning = false;
	try
	{
		vInputs.reserve(m_Count);
	}
	catch(const std::bad_alloc &)
	{
		ShowWarning = true;
	}
	for(int i = 0; i < m_Count; i++)
	{
		const auto SoundItem = MapRenderItems::ReadSound(*pMap, Start + i);
		if(!SoundItem)
		{
			log_error("mapsounds", "Failed to load map sound %d: invalid header.", i);
			ShowWarning = true;
			m_aSounds[i] = -1;
			continue;
		}
		const char *pName = pMap->GetDataString(SoundItem->m_SoundName);
		if(pName == nullptr || pName[0] == '\0')
		{
			if(SoundItem->m_External)
			{
				log_error("mapsounds", "Failed to load map sound %d: failed to load name.", i);
				ShowWarning = true;
				m_aSounds[i] = -1;
				continue;
			}
			pName = "(error)";
		}

		if(SoundItem->m_External)
		{
			char aBuf[IO_MAX_PATH_LENGTH];
			constexpr int MaxNameLength = sizeof(aBuf) - sizeof("mapres/.opus");
			if(str_length(pName) > MaxNameLength)
			{
				log_error("mapsounds", "Failed to load map sound %d: external path is too long.", i);
				m_aSounds[i] = -1;
				ShowWarning = true;
			}
			else
			{
				try
				{
					str_format(aBuf, sizeof(aBuf), "mapres/%s.opus", pName);
					CQmMapSoundDecodeJob::SInput Input;
					Input.m_SoundIndex = i;
					Input.m_Path = aBuf;
					Input.m_Name = pName;
					vInputs.push_back(std::move(Input));
				}
				catch(const std::bad_alloc &)
				{
					ShowWarning = true;
				}
			}
			ReleaseSoundData(SoundItem->m_SoundName, pName);
		}
		else
		{
			const void *pData = pMap->GetData(SoundItem->m_SoundData);
			if(pData == nullptr)
			{
				log_error("mapsounds", "Failed to load map sound %d: failed to load data.", i);
				ShowWarning = true;
				m_aSounds[i] = -1;
				continue;
			}
			const int SoundDataSize = pMap->GetDataSize(SoundItem->m_SoundData);
			if(SoundDataSize <= 0)
			{
				log_error("mapsounds", "Failed to load map sound %d: invalid data size %d.", i, SoundDataSize);
				ShowWarning = true;
				ReleaseSoundData(SoundItem->m_SoundData, pData);
				continue;
			}
			CQmMapSoundDecodeJob::SInput Input;
			Input.m_SoundIndex = i;
			const auto *pBytes = static_cast<const unsigned char *>(pData);
			try
			{
				Input.m_Name = pName;
				Input.m_vData.assign(pBytes, pBytes + SoundDataSize);
			}
			catch(const std::bad_alloc &)
			{
				ShowWarning = true;
				ReleaseSoundData(SoundItem->m_SoundData, pData);
				continue;
			}
			ReleaseSoundData(SoundItem->m_SoundData, pData);
			try
			{
				vInputs.push_back(std::move(Input));
			}
			catch(const std::bad_alloc &)
			{
				ShowWarning = true;
			}
		}
	}
	m_DecodeWarning = ShowWarning;
	if(vInputs.empty())
	{
		m_DecodeReady = true;
		if(!PrepareMapSoundSourceQueue(*Layers(), std::span<const int>(m_aSounds, m_Count), m_vSourceQueue))
			m_DecodeWarning = true;
		if(m_DecodeWarning)
		{
			try
			{
				Client()->AddWarning(SWarning(Localize("Some map sounds could not be loaded. Check the local console for details.")));
			}
			catch(...)
			{
			}
		}
		return;
	}
	m_DecodeReady = false;
	try
	{
		m_pDecodeJob = std::make_shared<CQmMapSoundDecodeJob>(Sound(), std::move(vInputs));
	}
	catch(const std::bad_alloc &)
	{
		m_DecodeWarning = true;
		m_DecodeReady = true;
	}
	if(m_pDecodeJob)
	{
		if(Client()->State() != IClient::STATE_LOADING || !Engine()->TryAddJob(m_pDecodeJob))
			m_pDecodeJob->Decode();
		if(QmPerfEnabled())
		{
			char aPayload[192];
			str_format(aPayload, sizeof(aPayload), "event=map_sounds_decode_start count=%zu", m_pDecodeJob->Inputs().size());
			QmPerfLogPayloadForce("perf/connect", aPayload, Client());
		}
	}
	if(m_DecodeReady)
	{
		if(!PrepareMapSoundSourceQueue(*Layers(), std::span<const int>(m_aSounds, m_Count), m_vSourceQueue))
			m_DecodeWarning = true;
		if(m_DecodeWarning)
		{
			try
			{
				Client()->AddWarning(SWarning(Localize("Some map sounds could not be loaded. Check the local console for details.")));
			}
			catch(...)
			{
			}
		}
	}
	PollDecode();
}

bool CMapSounds::PollDecode()
{
	for(auto It = m_vRetiredDecodeJobs.begin(); It != m_vRetiredDecodeJobs.end();)
	{
		if(!(*It)->Ready())
		{
			++It;
			continue;
		}
		for(int SampleId : (*It)->SampleIds())
			Sound()->UnloadSample(SampleId);
		if(QmPerfEnabled())
		{
			char aPayload[192];
			str_format(aPayload, sizeof(aPayload), "event=map_sounds_decode_canceled count=%zu duration_ms=%.3f", (*It)->Inputs().size(), (*It)->ElapsedMs());
			QmPerfLogPayloadForce("perf/connect", aPayload, Client());
		}
		It = m_vRetiredDecodeJobs.erase(It);
	}
	if(m_DecodeReady || !m_pDecodeJob || !m_pDecodeJob->Ready())
		return m_DecodeReady;
	if(!m_pDecodeJob->InlineComplete() && m_pDecodeJob->State() != IJob::STATE_DONE)
		m_DecodeWarning = true;
	const auto &vInputs = m_pDecodeJob->Inputs();
	const auto &vSampleIds = m_pDecodeJob->SampleIds();
	for(size_t Index = 0; Index < vInputs.size() && Index < vSampleIds.size(); ++Index)
	{
		const int SoundIndex = vInputs[Index].m_SoundIndex;
		if(SoundIndex < 0 || SoundIndex >= m_Count)
		{
			Sound()->UnloadSample(vSampleIds[Index]);
			m_DecodeWarning = true;
			continue;
		}
		m_aSounds[SoundIndex] = vSampleIds[Index];
		m_DecodeWarning |= m_aSounds[SoundIndex] < 0;
	}
	m_DecodeReady = true;
	if(!PrepareMapSoundSourceQueue(*Layers(), std::span<const int>(m_aSounds, m_Count), m_vSourceQueue))
	{
		m_DecodeWarning = true;
		log_error("mapsounds", "Failed to prepare map sound sources.");
	}
	if(m_DecodeWarning)
	{
		try
		{
			Client()->AddWarning(SWarning(Localize("Some map sounds could not be loaded. Check the local console for details.")));
		}
		catch(...)
		{
		}
	}
	if(QmPerfEnabled())
	{
		char aPayload[192];
		str_format(aPayload, sizeof(aPayload), "event=map_sounds_decode_complete count=%zu duration_ms=%.3f", vInputs.size(), m_pDecodeJob->ElapsedMs());
		QmPerfLogPayloadForce("perf/connect", aPayload, Client());
	}
	m_pDecodeJob.reset();
	return true;
}

void CMapSounds::OnRender()
{
	// 加载中换图时可能没有状态切换回调，但旧图层已经被清除。
	if(m_Count > 0 && !Layers()->Map())
	{
		Clear();
		return;
	}

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	bool DemoPlayerPaused = Client()->State() == IClient::STATE_DEMOPLAYBACK && DemoPlayer()->BaseInfo()->m_Paused;
	if(!SoundEnabled())
	{
		for(auto &Source : m_vSourceQueue)
		{
			if(Source.m_Voice.IsValid())
			{
				Sound()->StopVoice(Source.m_Voice);
				Source.m_Voice = ISound::CVoiceHandle();
			}
		}
		return;
	}

	if(const auto *pGameInfo = GameClient()->m_Snap.m_pGameInfoObj; !m_vSourceQueue.empty() && pGameInfo)
	{
		const std::int64_t RoundStartTick = pGameInfo->m_RoundStartTick;
		const float TickSpeed = static_cast<float>(Client()->GameTickSpeed());
		s_MapSoundTime = mix(static_cast<float>(Client()->PrevGameTick(g_Config.m_ClDummy) - RoundStartTick) / TickSpeed,
			static_cast<float>(Client()->GameTick(g_Config.m_ClDummy) - RoundStartTick) / TickSpeed,
			Client()->IntraGameTick(g_Config.m_ClDummy));
	}

	// enqueue sounds
	for(auto &Source : m_vSourceQueue)
	{
		const CSoundSource &SoundSource = Source.m_Source;
		const float Offset = s_MapSoundTime - SoundSource.m_TimeDelay;
		if(!DemoPlayerPaused && Offset >= 0.0f && SoundEnabled() && (g_Config.m_GfxHighDetail || !Source.m_HighDetail))
		{
			if(Source.m_Voice.IsValid())
			{
				// currently playing, set offset
				Sound()->SetVoiceTimeOffset(Source.m_Voice, Offset);
			}
			else
			{
				// need to enqueue
				int Flags = 0;
				if(SoundSource.m_Loop)
					Flags |= ISound::FLAG_LOOP;
				if(!SoundSource.m_Pan)
					Flags |= ISound::FLAG_NO_PANNING;

				Source.m_Voice = GameClient()->m_Sounds.PlaySampleAt(CSounds::CHN_MAPSOUND, m_aSounds[Source.m_Sound], Flags, 1.0f, vec2(fx2f(SoundSource.m_Position.x), fx2f(SoundSource.m_Position.y)));
				Sound()->SetVoiceTimeOffset(Source.m_Voice, Offset);
				Sound()->SetVoiceFalloff(Source.m_Voice, SoundSource.m_Falloff / 255.0f);
				switch(SoundSource.m_Shape.m_Type)
				{
				case CSoundShape::SHAPE_CIRCLE:
				{
					Sound()->SetVoiceCircle(Source.m_Voice, SoundSource.m_Shape.m_Circle.m_Radius);
					break;
				}

				case CSoundShape::SHAPE_RECTANGLE:
				{
					Sound()->SetVoiceRectangle(Source.m_Voice, fx2f(SoundSource.m_Shape.m_Rectangle.m_Width), fx2f(SoundSource.m_Shape.m_Rectangle.m_Height));
					break;
				}
				};
			}
		}
		else
		{
			// stop voice
			Sound()->StopVoice(Source.m_Voice);
			Source.m_Voice = ISound::CVoiceHandle();
		}
	}

	const vec2 Center = GameClient()->m_Camera.m_Center;
	for(const auto &Source : m_vSourceQueue)
	{
		if(!Source.m_Voice.IsValid())
			continue;

		const CSoundSource &SoundSource = Source.m_Source;
		ColorRGBA Position = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
		CEnvelopeState &EnvEvaluator = GameClient()->m_MapLayersBackground.EnvEvaluator();
		EnvEvaluator.EnvelopeEval(SoundSource.m_PosEnvOffset, SoundSource.m_PosEnv, Position, 2);

		float x = fx2f(SoundSource.m_Position.x) + Position.r;
		float y = fx2f(SoundSource.m_Position.y) + Position.g;

		x += Center.x * (1.0f - Source.m_pGroup->m_ParallaxX / 100.0f);
		y += Center.y * (1.0f - Source.m_pGroup->m_ParallaxY / 100.0f);

		x -= Source.m_pGroup->m_OffsetX;
		y -= Source.m_pGroup->m_OffsetY;

		Sound()->SetVoicePosition(Source.m_Voice, vec2(x, y));

		ColorRGBA Volume = ColorRGBA(1.0f, 0.0f, 0.0f, 0.0f);
		EnvEvaluator.EnvelopeEval(SoundSource.m_SoundEnvOffset, SoundSource.m_SoundEnv, Volume, 1);

		Sound()->SetVoiceVolume(Source.m_Voice, std::clamp(Volume.r, 0.0f, 1.0f));
	}
}

void CMapSounds::Clear()
{
	s_MapSoundTime = 0.0f;
	if(m_pDecodeJob)
	{
		if(m_pDecodeJob->Ready())
		{
			for(int SampleId : m_pDecodeJob->SampleIds())
				Sound()->UnloadSample(SampleId);
		}
		else
		{
			try
			{
				m_vRetiredDecodeJobs.push_back(std::move(m_pDecodeJob));
			}
			catch(const std::bad_alloc &)
			{
				while(!m_pDecodeJob->Ready())
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				for(int SampleId : m_pDecodeJob->SampleIds())
					Sound()->UnloadSample(SampleId);
			}
		}
		m_pDecodeJob.reset();
	}
	// unload all samples
	m_vSourceQueue.clear();
	for(int i = 0; i < m_Count; i++)
	{
		Sound()->UnloadSample(m_aSounds[i]);
		m_aSounds[i] = -1;
	}
	m_Count = 0;
	m_DecodeReady = true;
	m_DecodeWarning = false;
}

bool CMapSounds::SoundEnabled() const
{
	return g_Config.m_SndEnable && g_Config.m_SndGame && !g_Config.m_ClSndMuteMapSound;
}

void CMapSounds::OnStateChange(int NewState, int OldState)
{
	if(NewState < IClient::STATE_ONLINE)
		Clear();
}
