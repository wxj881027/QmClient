#include "pose_scene.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/shared/snapshot.h>

#include <generated/protocol.h>

#include <algorithm>

namespace qm_pose
{
	namespace
	{
		// 名字/皮肤沿用协议里的 StrToInts 打包（每字节 +128，末字节恒 0）。
		// 与 gamecore 的实现同义，但解析层要独立可测，这里自带一份最小实现，
		// 不引入 game 模块依赖。
		void PackString(int *pOut, size_t NumInts, const char *pStr)
		{
			for(size_t i = 0; i < NumInts; ++i)
			{
				const size_t Base = i * 4;
				unsigned int Packed = 0;
				for(int k = 0; k < 4; ++k)
				{
					const char Ch = pStr[Base + (size_t)k];
					const unsigned int Value = (unsigned int)((unsigned char)Ch + 128);
					Packed |= (Value & 0xff) << (24 - k * 8);
					if(Ch == 0)
					{
						// 之后的字节保持 0（也就是编码后的「结束符」）
						for(int Rest = k + 1; Rest < 4; ++Rest)
							Packed = (Packed & ~(0xffu << (24 - Rest * 8))) | (128u << (24 - Rest * 8));
						break;
					}
				}
				pOut[i] = (int)Packed;
			}
			// 最后一个 int 的低字节恒为 0，与 StrToInts 一致
			pOut[NumInts - 1] &= (int)0xffffff00;
		}

		// 队伍衰减：合影里所有人同队最容易看清楚，皮肤不被染成红/蓝半透明。
		int ClampTeam(int Team)
		{
			if(Team < TEAM_SPECTATORS)
				return TEAM_SPECTATORS;
			if(Team > TEAM_BLUE)
				return TEAM_BLUE;
			return Team;
		}
	} // namespace

	int VideoFrameCount(float Seconds, int Fps)
	{
		if(Seconds <= 0.0f || Fps <= 0)
			return 0;
		// 向上取整：录 0.5 秒 @ 60fps 应该是 30 帧而不是 0 帧。
		const float Frames = Seconds * (float)Fps;
		const int Whole = (int)Frames;
		return Frames > (float)Whole ? Whole + 1 : Whole;
	}

	void ComputeCenter(const std::vector<SPoseTee> &vPoses, int &CenterX, int &CenterY)
	{
		if(vPoses.empty())
		{
			CenterX = 0;
			CenterY = 0;
			return;
		}
		int MinX = vPoses[0].m_X, MaxX = vPoses[0].m_X;
		int MinY = vPoses[0].m_Y, MaxY = vPoses[0].m_Y;
		const size_t VisibleCount = std::min<size_t>(vPoses.size(), MAX_SCENE_TEES);
		for(size_t i = 0; i < VisibleCount; ++i)
		{
			const SPoseTee &Pose = vPoses[i];
			MinX = std::min(MinX, Pose.m_X);
			MaxX = std::max(MaxX, Pose.m_X);
			MinY = std::min(MinY, Pose.m_Y);
			MaxY = std::max(MaxY, Pose.m_Y);
		}
		// 向零取整会让中心偏移半格，这里用整数平均 + 四舍五入。
		CenterX = (MinX + MaxX) / 2 + ((MinX + MaxX) % 2 != 0 ? ((MinX + MaxX) > 0 ? 1 : -1) : 0);
		CenterY = (MinY + MaxY) / 2 + ((MinY + MaxY) % 2 != 0 ? ((MinY + MaxY) > 0 ? 1 : -1) : 0);
	}

	float ComputeFitZoom(const std::vector<SPoseTee> &vPoses, float BaseWidth, float BaseHeight, float Padding)
	{
		if(vPoses.empty() || BaseWidth <= 0.0f || BaseHeight <= 0.0f)
			return 1.0f;
		int MinX = vPoses[0].m_X, MaxX = MinX;
		int MinY = vPoses[0].m_Y, MaxY = MinY;
		for(const SPoseTee &Pose : vPoses)
		{
			MinX = std::min(MinX, Pose.m_X);
			MaxX = std::max(MaxX, Pose.m_X);
			MinY = std::min(MinY, Pose.m_Y);
			MaxY = std::max(MaxY, Pose.m_Y);
		}
		const float SafePadding = std::max(Padding, 0.0f);
		const float NeedWidth = std::max((float)(MaxX - MinX) + SafePadding, 1.0f);
		const float NeedHeight = std::max((float)(MaxY - MinY) + SafePadding, 1.0f);
		return std::max(NeedWidth / BaseWidth, NeedHeight / BaseHeight);
	}

	int BuildSnapshot(const std::vector<SPoseTee> &vPoses, const SSceneOptions &Options,
		unsigned char *pOutBuffer, size_t OutSize, SSceneStats *pStats)
	{
		if(!pOutBuffer || OutSize < sizeof(CSnapshotBuffer))
			return 0;

		const int NumTees = (int)std::min<size_t>(vPoses.size(), (size_t)MAX_SCENE_TEES);
		if(NumTees <= 0)
			return 0;
		log_info("poses", "building snapshot for %d tees", NumTees);

		// 合成快照：每名 Tee 4 个对象 + 场景 2 个对象。
		rust::Box<CSnapshotBuilder> pBuilder = CSnapshotBuilder::New();
		pBuilder->Init(false);

		const int Team = ClampTeam(Options.m_Team);
		const int Now = 0;

		for(int i = 0; i < NumTees; ++i)
		{
			const SPoseTee &Pose = vPoses[(size_t)i];

			CNetObj_ClientInfo ClientInfo{};
			PackString(ClientInfo.m_aName, std::size(ClientInfo.m_aName), Pose.m_aName);
			PackString(ClientInfo.m_aClan, std::size(ClientInfo.m_aClan), Pose.m_aClan);
			PackString(ClientInfo.m_aSkin, std::size(ClientInfo.m_aSkin), Pose.m_aSkin[0] != 0 ? Pose.m_aSkin : "default");
			ClientInfo.m_Country = 0;
			ClientInfo.m_UseCustomColor = Pose.m_UseCustomColor ? 1 : 0;
			ClientInfo.m_ColorBody = Pose.m_ColorBody;
			ClientInfo.m_ColorFeet = Pose.m_ColorFeet;
			if(!pBuilder->NewItem(NETOBJTYPE_CLIENTINFO, i, ClientInfo.AsSlice()))
				return 0;

			CNetObj_PlayerInfo PlayerInfo{};
			// 0 号当作本地玩家：demo 回放态下客户端没有本地玩家时会把相机切成自由视角，
			// 而自由视角在 demo 状态下只认 SPECTATORINFO 的坐标——但那段代码要求
			// 存在本地玩家才会把 m_SpecInfo 置为激活。没有本地玩家时相机不会对准合影。
			PlayerInfo.m_Local = i == 0 ? 1 : 0;
			PlayerInfo.m_ClientId = i;
			PlayerInfo.m_Team = Team;
			PlayerInfo.m_Score = 0;
			PlayerInfo.m_Latency = 0;
			if(!pBuilder->NewItem(NETOBJTYPE_PLAYERINFO, i, PlayerInfo.AsSlice()))
				return 0;

			CNetObj_DDNetPlayer DDNetPlayer{};
			DDNetPlayer.m_Flags = 0;
			DDNetPlayer.m_AuthLevel = 0;
			DDNetPlayer.m_FinishTimeSeconds = 0;
			DDNetPlayer.m_FinishTimeMillis = 0;
			if(!pBuilder->NewItem(NETOBJTYPE_DDNETPLAYER, i, DDNetPlayer.AsSlice()))
				return 0;

			CNetObj_Character Character{};
			Character.m_Tick = Now;
			Character.m_X = Pose.m_X;
			Character.m_Y = Pose.m_Y;
			// 速度为 0：合影是静止造型，有速度会让渲染器画朝向插值。
			Character.m_VelX = 0;
			Character.m_VelY = 0;
			Character.m_Angle = Pose.m_Angle;
			Character.m_Direction = Pose.m_Direction != 0 ? Pose.m_Direction : 1;
			Character.m_Jumped = 0;
			Character.m_HookedPlayer = -1;
			Character.m_HookState = Pose.m_HookState;
			Character.m_HookTick = 0;
			// 没有钩子时把钩子端点收敛到自身，否则会从 (0,0) 拉一条线到场内。
			Character.m_HookX = Pose.m_HookState > 0 ? Pose.m_HookX : Pose.m_X;
			Character.m_HookY = Pose.m_HookState > 0 ? Pose.m_HookY : Pose.m_Y;
			Character.m_HookDx = 0;
			Character.m_HookDy = 0;
			Character.m_PlayerFlags = 0;
			Character.m_Health = 10;
			Character.m_Armor = 10;
			Character.m_AmmoCount = 10;
			Character.m_Weapon = std::clamp(Pose.m_Weapon, -1, NUM_WEAPONS - 1);
			Character.m_Emote = std::max(Pose.m_Emote, 0);
			Character.m_AttackTick = 0;
			if(!pBuilder->NewItem(NETOBJTYPE_CHARACTER, i, Character.AsSlice()))
				return 0;
		}

		// 场景：没有 GAMEINFO 时客户端很多逻辑会跳过（m_pGameInfoObj 为空）。
		CNetObj_GameInfo GameInfo{};
		GameInfo.m_GameFlags = 0;
		GameInfo.m_GameStateFlags = 0;
		GameInfo.m_RoundStartTick = 0;
		GameInfo.m_WarmupTimer = 0;
		GameInfo.m_ScoreLimit = 0;
		GameInfo.m_TimeLimit = 0;
		GameInfo.m_RoundNum = 0;
		GameInfo.m_RoundCurrent = 0;
		if(!pBuilder->NewItem(NETOBJTYPE_GAMEINFO, 0, GameInfo.AsSlice()))
			return 0;

		int CenterX = Options.m_CenterX;
		int CenterY = Options.m_CenterY;
		if(Options.m_AutoCenter)
		{
			// 与快照实际保留的前 MAX_SCENE_TEES 名保持一致，避免超限文件中
			// 被截掉的远端 Tee 把相机拉得过远。
			std::vector<SPoseTee> vVisiblePoses(vPoses.begin(), vPoses.begin() + NumTees);
			ComputeCenter(vVisiblePoses, CenterX, CenterY);
		}

		// SPECTATORINFO 把自由视角相机放到合影中心；SpectatorId = -1 表示自由视角。
		CNetObj_SpectatorInfo SpecInfo{};
		SpecInfo.m_SpectatorId = -1;
		SpecInfo.m_X = CenterX;
		SpecInfo.m_Y = CenterY;
		if(!pBuilder->NewItem(NETOBJTYPE_SPECTATORINFO, 0, SpecInfo.AsSlice()))
			return 0;

		CSnapshotBuffer Buffer;
		const int Size = pBuilder->Finish(Buffer);
		if(Size <= 0 || (size_t)Size > OutSize)
			return 0;
		mem_copy(pOutBuffer, Buffer.m_aData, (size_t)Size);

		if(pStats)
		{
			pStats->m_Tees = NumTees;
			pStats->m_CenterX = CenterX;
			pStats->m_CenterY = CenterY;
		}
		return Size;
	}
} // namespace qm_pose
