#include "mapsounds.h"

#include <base/log.h>

#include <engine/demo.h>
#include <engine/shared/config.h>
#include <engine/sound.h>

#include <game/client/components/camera.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/layers.h>
#include <game/localization.h>
#include <game/map/map_render_items.h>
#include <game/mapitems.h>

#include <cstdint>

static float s_MapSoundTime = 0.0f;

CMapSounds::CMapSounds()
{
	m_Count = 0;
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

	// load new samples
	bool ShowWarning = false;
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
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), "mapres/%s.opus", pName);
				m_aSounds[i] = Sound()->LoadOpus(aBuf);
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
			m_aSounds[i] = Sound()->LoadOpusFromMem(pData, SoundDataSize, false, pName);
			ReleaseSoundData(SoundItem->m_SoundData, pData);
		}
		ShowWarning = ShowWarning || m_aSounds[i] == -1;
	}
	if(!PrepareMapSoundSourceQueue(*Layers(), std::span<const int>(m_aSounds, m_Count), m_vSourceQueue))
	{
		ShowWarning = true;
		// 分配失败后的诊断也可能分配，不能再次中断地图加载。
		try
		{
			log_error("mapsounds", "Failed to prepare map sound sources.");
		}
		catch(...)
		{
		}
	}
	if(ShowWarning)
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
	// unload all samples
	m_vSourceQueue.clear();
	for(int i = 0; i < m_Count; i++)
	{
		Sound()->UnloadSample(m_aSounds[i]);
		m_aSounds[i] = -1;
	}
	m_Count = 0;
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
