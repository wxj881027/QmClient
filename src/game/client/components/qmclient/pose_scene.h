#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_POSE_SCENE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_POSE_SCENE_H

// 把 .poses 合影数据合成为一份快照，交给客户端已有的快照解析与渲染路径。
//
// 为什么是快照而不是新的渲染器：MomentCap 让合影显示出来的方式，是把 pose 当成假客户端
// 塞进快照里；客户端只是照常解析。我们复用同一条路径，皮肤、表情、武器、钩子、队伍颜色
// 全部沿用现有渲染代码，不重写任何绘制逻辑。
//
// 合成产物包含两类对象：
//   每名 Tee：CLIENTINFO / PLAYERINFO / DDNETPLAYER / CHARACTER（各按 client id 编号）
//   一份场景：GAMEINFO / SPECTATORINFO（把相机放到合影中心）
// CHARACTER 只写当前帧时 m_aCharacters[].m_Active 仍为 false（客户端要求 SNAP_PREV 里也有
// 同一个角色才会激活）。因此调用方需要连续提交两次快照，第二次起才会真正画出来——
// ApplyToClient 负责这件事。

#include <game/client/components/qmclient/pose_file.h>

#include <cstddef>
#include <vector>

struct SPoseSceneTee
{
	int m_ClientId;
	int m_X;
	int m_Y;
	int m_Angle;
};

namespace qm_pose
{
	// 合影最多能放多少名 Tee：受快照对象上限与客户端 client id 上限共同约束。
	// 每名 Tee 占 4 个对象，另有 2 个场景对象。
	constexpr int MAX_SCENE_TEES = 64;

	struct SSceneOptions
	{
		// 队伍分配：-1 = 旁观者，0 = 红队，1 = 蓝队。
		// 默认旁观者：客户端只有在玩家处于旁观状态时才会激活旁观相机，并用
		// SPECTATORINFO 的坐标取景；放红队会让相机跟随本地角色而不是合影中心。
		// 旁观者在渲染上不做队伍染色，正是合影想要的效果。
		int m_Team = -1;
		// 相机中心；0,0 表示按所有 Tee 的包围盒中心。
		int m_CenterX = 0;
		int m_CenterY = 0;
		bool m_AutoCenter = true;
	};

	struct SSceneStats
	{
		int m_Tees;
		int m_CenterX;
		int m_CenterY;
	};

	// 把 pose 列表合成为快照数据。pOutBuffer 需提供 CSnapshot::MAX_SIZE 字节。
	// 返回写入的字节数（即 CSnapshot 的总长度）；<= 0 表示失败。
	// 超过 MAX_SCENE_TEES 时只取前 MAX_SCENE_TEES 名，并在 pStats 中如实报告。
	int BuildSnapshot(const std::vector<SPoseTee> &vPoses, const SSceneOptions &Options,
		unsigned char *pOutBuffer, size_t OutSize, SSceneStats *pStats);

	// 计算包围盒中心（四舍五入），用于把相机对准合影。
	void ComputeCenter(const std::vector<SPoseTee> &vPoses, int &CenterX, int &CenterY);

	// 根据合影包围盒和基础视口尺寸计算能容纳全部 Tee 的相机缩放。
	// Zoom 与引擎 CalcScreenParams 同向：数值越大，可见世界范围越大。
	float ComputeFitZoom(const std::vector<SPoseTee> &vPoses, float BaseWidth, float BaseHeight, float Padding = 400.0f);

	// 合影视频需要录多少帧：秒数 × 帧率，向上取整，至少 1 帧。
	// 抽成纯函数便于测试「短时长不会被截成 0 帧」这类边界。
	int VideoFrameCount(float Seconds, int Fps);
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_POSE_SCENE_H
