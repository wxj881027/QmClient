/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "sounds.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/sound.h>
#include <engine/storage.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/components/camera.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

CSoundLoading::CSoundLoading(CGameClient *pGameClient, bool Render) :
	m_pGameClient(pGameClient),
	m_Render(Render)
{
	Abortable(true);
}

static bool IsSoundMuted(int SoundId)
{
	switch(SoundId)
	{
	case SOUND_GUN_FIRE:
	case SOUND_SHOTGUN_FIRE:
	case SOUND_GRENADE_FIRE:
	case SOUND_HAMMER_FIRE:
	case SOUND_HAMMER_HIT:
	case SOUND_NINJA_FIRE:
	case SOUND_NINJA_HIT:
	case SOUND_GRENADE_EXPLODE:
	case SOUND_LASER_FIRE:
	case SOUND_LASER_BOUNCE:
	case SOUND_HIT:
		return g_Config.m_ClSndMuteWeapon != 0;

	case SOUND_WEAPON_SWITCH:
		return g_Config.m_ClSndMuteWeaponSwitch != 0;

	case SOUND_WEAPON_NOAMMO:
		return g_Config.m_ClSndMuteWeaponNoAmmo != 0;

	case SOUND_HOOK_LOOP:
	case SOUND_HOOK_ATTACH_GROUND:
	case SOUND_HOOK_ATTACH_PLAYER:
	case SOUND_HOOK_NOATTACH:
		return g_Config.m_ClSndMuteHook != 0;

	case SOUND_PLAYER_JUMP:
	case SOUND_PLAYER_AIRJUMP:
	case SOUND_BODY_LAND:
	case SOUND_PLAYER_SKID:
		return g_Config.m_ClSndMuteMovement != 0;

	case SOUND_PLAYER_PAIN_SHORT:
	case SOUND_PLAYER_PAIN_LONG:
	case SOUND_PLAYER_DIE:
	case SOUND_PLAYER_SPAWN:
	case SOUND_TEE_CRY:
		return g_Config.m_ClSndMutePlayerState != 0;

	case SOUND_PICKUP_HEALTH:
	case SOUND_PICKUP_ARMOR:
	case SOUND_PICKUP_GRENADE:
	case SOUND_PICKUP_SHOTGUN:
	case SOUND_PICKUP_NINJA:
	case SOUND_WEAPON_SPAWN:
		return g_Config.m_ClSndMutePickup != 0;

	case SOUND_CTF_DROP:
	case SOUND_CTF_RETURN:
	case SOUND_CTF_GRAB_PL:
	case SOUND_CTF_GRAB_EN:
	case SOUND_CTF_CAPTURE:
		return g_Config.m_ClSndMuteFlag != 0;

	default:
		return false;
	}
}

static const char *GetSoundFilename(const char *pFilename, IStorage *pStorage, char *pBuffer, size_t BufferSize)
{
	if(!pStorage || pFilename == nullptr || pFilename[0] == '\0')
		return pFilename;

	const char *pPack = g_Config.m_SndPack;
	if(pPack[0] == '\0')
		return pFilename;

	const char *pRelative = pFilename;
	if(str_startswith(pFilename, "audio/"))
		pRelative = pFilename + str_length("audio/");

	str_format(pBuffer, BufferSize, "audio/%s/%s", pPack, pRelative);
	if(pStorage->FileExists(pBuffer, IStorage::TYPE_ALL))
		return pBuffer;

	str_format(pBuffer, BufferSize, "audio/%s/%s", pPack, pFilename);
	if(pStorage->FileExists(pBuffer, IStorage::TYPE_ALL))
		return pBuffer;

	return pFilename;
}

void CSoundLoading::Run()
{
	for(int s = 0; s < g_pData->m_NumSounds; s++)
	{
		const char *pLoadingCaption = Localize("Loading DDNet Client");
		const char *pLoadingContent = Localize("Loading sound files");

		for(int i = 0; i < g_pData->m_aSounds[s].m_NumSounds; i++)
		{
			if(State() == IJob::STATE_ABORTED)
				return;

			char aPath[IO_MAX_PATH_LENGTH];
			const char *pFilename = g_pData->m_aSounds[s].m_aSounds[i].m_pFilename;
			const char *pLoadFilename = GetSoundFilename(pFilename, m_pGameClient->Storage(), aPath, sizeof(aPath));
			int Id = m_pGameClient->Sound()->LoadWV(pLoadFilename);
			g_pData->m_aSounds[s].m_aSounds[i].m_Id = Id;
			// try to render a frame
			if(m_Render)
				m_pGameClient->m_Menus.RenderLoading(pLoadingCaption, pLoadingContent, 0);
		}

		if(m_Render)
			m_pGameClient->m_Menus.RenderLoading(pLoadingCaption, pLoadingContent, 1);
	}
}

void CSounds::UpdateChannels()
{
	const float NewGuiSoundVolume = g_Config.m_SndChatVolume / 100.0f;
	if(NewGuiSoundVolume != m_GuiSoundVolume)
	{
		m_GuiSoundVolume = NewGuiSoundVolume;
		Sound()->SetChannel(CSounds::CHN_GUI, m_GuiSoundVolume, 0.0f);
	}

	const float NewGameSoundVolume = g_Config.m_SndGameVolume / 100.0f;
	if(NewGameSoundVolume != m_GameSoundVolume)
	{
		m_GameSoundVolume = NewGameSoundVolume;
		Sound()->SetChannel(CSounds::CHN_WORLD, 0.9f * m_GameSoundVolume, 1.0f);
		Sound()->SetChannel(CSounds::CHN_GLOBAL, m_GameSoundVolume, 0.0f);
	}

	const float NewMapSoundVolume = g_Config.m_SndMapVolume / 100.0f;
	if(NewMapSoundVolume != m_MapSoundVolume)
	{
		m_MapSoundVolume = NewMapSoundVolume;
		Sound()->SetChannel(CSounds::CHN_MAPSOUND, m_MapSoundVolume, 1.0f);
	}

	const float NewBackgroundMusicVolume = g_Config.m_SndBackgroundMusicVolume / 100.0f;
	if(NewBackgroundMusicVolume != m_BackgroundMusicVolume)
	{
		m_BackgroundMusicVolume = NewBackgroundMusicVolume;
		Sound()->SetChannel(CSounds::CHN_MUSIC, m_BackgroundMusicVolume, 1.0f);
	}
}

int CSounds::GetSampleId(int SetId)
{
	if(!g_Config.m_SndEnable || !Sound()->IsSoundEnabled() || m_WaitForSoundJob || SetId < 0 || SetId >= g_pData->m_NumSounds)
		return -1;

	CDataSoundset *pSet = &g_pData->m_aSounds[SetId];
	if(!pSet->m_NumSounds)
		return -1;

	if(pSet->m_NumSounds == 1)
		return pSet->m_aSounds[0].m_Id;

	// return random one
	int Id;
	do
	{
		Id = rand() % pSet->m_NumSounds;
	} while(Id == pSet->m_Last);
	pSet->m_Last = Id;
	return pSet->m_aSounds[Id].m_Id;
}

void CSounds::OnInit()
{
	UpdateChannels();
	ClearQueue();

	// load sounds
	if(g_Config.m_ClThreadsoundloading)
	{
		m_pSoundJob = std::make_shared<CSoundLoading>(GameClient(), false);
		GameClient()->Engine()->AddJob(m_pSoundJob);
		m_WaitForSoundJob = true;
		GameClient()->m_Menus.RenderLoading(Localize("Loading DDNet Client"), Localize("Loading sound files"), 0);
	}
	else
	{
		CSoundLoading(GameClient(), true).Run();
		m_WaitForSoundJob = false;
	}
}

bool CSounds::Reload()
{
	if(m_WaitForSoundJob)
	{
		if(m_pSoundJob && (m_pSoundJob->State() == IJob::STATE_DONE || m_pSoundJob->State() == IJob::STATE_ABORTED))
		{
			m_WaitForSoundJob = false;
		}
		else
		{
			return false;
		}
	}

	Sound()->StopAll();
	ClearQueue();

	for(int s = 0; s < g_pData->m_NumSounds; s++)
	{
		for(int i = 0; i < g_pData->m_aSounds[s].m_NumSounds; i++)
		{
			int &Id = g_pData->m_aSounds[s].m_aSounds[i].m_Id;
			if(Id != -1)
			{
				Sound()->UnloadSample(Id);
				Id = -1;
			}
		}
	}

	for(int s = 0; s < g_pData->m_NumSounds; s++)
	{
		for(int i = 0; i < g_pData->m_aSounds[s].m_NumSounds; i++)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			const char *pFilename = g_pData->m_aSounds[s].m_aSounds[i].m_pFilename;
			const char *pLoadFilename = GetSoundFilename(pFilename, Storage(), aPath, sizeof(aPath));
			int Id = Sound()->LoadWV(pLoadFilename);
			g_pData->m_aSounds[s].m_aSounds[i].m_Id = Id;
		}
	}

	UpdateChannels();
	return true;
}

void CSounds::OnReset()
{
	if(Client()->State() >= IClient::STATE_ONLINE)
	{
		Sound()->StopAll();
		ClearQueue();
	}
}

void CSounds::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_ONLINE || NewState == IClient::STATE_DEMOPLAYBACK)
		OnReset();
}

void CSounds::OnRender()
{
	// check for sound initialisation
	if(m_WaitForSoundJob)
	{
		if(m_pSoundJob->State() == IJob::STATE_DONE)
			m_WaitForSoundJob = false;
		else
			return;
	}

	Sound()->SetListenerPosition(GameClient()->m_Camera.m_Center);
	UpdateChannels();

	// play sound from queue
	if(m_QueuePos > 0)
	{
		int64_t Now = time();
		if(m_QueueWaitTime <= Now)
		{
			Play(m_aQueue[0].m_Channel, m_aQueue[0].m_SetId, 1.0f);
			m_QueueWaitTime = Now + time_freq() * 3 / 10; // wait 300ms before playing the next one
			if(--m_QueuePos > 0)
				mem_move(m_aQueue, m_aQueue + 1, m_QueuePos * sizeof(CQueueEntry));
		}
	}
}

void CSounds::ClearQueue()
{
	mem_zero(m_aQueue, sizeof(m_aQueue));
	m_QueuePos = 0;
	m_QueueWaitTime = time();
}

void CSounds::Enqueue(int Channel, int SetId)
{
	if(GameClient()->m_SuppressEvents)
		return;
	if(m_QueuePos >= QUEUE_SIZE)
		return;
	if(Channel != CHN_MUSIC && g_Config.m_ClEditor)
		return;
	if(IsSoundMuted(SetId))
		return;

	m_aQueue[m_QueuePos].m_Channel = Channel;
	m_aQueue[m_QueuePos++].m_SetId = SetId;
}

void CSounds::PlayAndRecord(int Channel, int SetId, float Volume, vec2 Position)
{
	// TODO: Volume and position are currently not recorded for sounds played with this function
	// TODO: This also causes desync sounds during demo playback of demos recorded on high ping servers:
	//       https://github.com/ddnet/ddnet/issues/1282
	CNetMsg_Sv_SoundGlobal Msg;
	Msg.m_SoundId = SetId;
	Client()->SendPackMsgActive(&Msg, MSGFLAG_NOSEND | MSGFLAG_RECORD);

	PlayAt(Channel, SetId, Volume, Position);
}

void CSounds::Play(int Channel, int SetId, float Volume)
{
	if(IsSoundMuted(SetId))
		return;
	PlaySample(Channel, GetSampleId(SetId), 0, Volume);
}

void CSounds::PlayAt(int Channel, int SetId, float Volume, vec2 Position)
{
	if(IsSoundMuted(SetId))
		return;
	PlaySampleAt(Channel, GetSampleId(SetId), 0, Volume, Position);
}

void CSounds::Stop(int SetId)
{
	if(m_WaitForSoundJob || SetId < 0 || SetId >= g_pData->m_NumSounds)
		return;

	const CDataSoundset *pSet = &g_pData->m_aSounds[SetId];
	for(int i = 0; i < pSet->m_NumSounds; i++)
		if(pSet->m_aSounds[i].m_Id != -1)
			Sound()->Stop(pSet->m_aSounds[i].m_Id);
}

bool CSounds::IsPlaying(int SetId)
{
	if(m_WaitForSoundJob || SetId < 0 || SetId >= g_pData->m_NumSounds)
		return false;

	const CDataSoundset *pSet = &g_pData->m_aSounds[SetId];
	for(int i = 0; i < pSet->m_NumSounds; i++)
		if(pSet->m_aSounds[i].m_Id != -1 && Sound()->IsPlaying(pSet->m_aSounds[i].m_Id))
			return true;
	return false;
}

ISound::CVoiceHandle CSounds::PlaySample(int Channel, int SampleId, int Flags, float Volume)
{
	if(GameClient()->m_SuppressEvents || (Channel == CHN_MUSIC && !g_Config.m_SndMusic) || SampleId == -1)
		return ISound::CVoiceHandle();

	if(Channel == CHN_MUSIC)
		Flags |= ISound::FLAG_LOOP;

	return Sound()->Play(Channel, SampleId, Flags, Volume);
}

ISound::CVoiceHandle CSounds::PlaySampleAt(int Channel, int SampleId, int Flags, float Volume, vec2 Position)
{
	if(GameClient()->m_SuppressEvents || (Channel == CHN_MUSIC && !g_Config.m_SndMusic) || SampleId == -1)
		return ISound::CVoiceHandle();

	if(Channel == CHN_MUSIC)
		Flags |= ISound::FLAG_LOOP;

	return Sound()->PlayAt(Channel, SampleId, Flags, Volume, Position);
}
