#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_POSE_FILE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_POSE_FILE_H

// MomentCap 合影服导出的 .poses 文件解析。
//
// 格式事实（来自 MomentCap 的 posinghelper.cpp SavePoses 与其自带 pose_merger/index.js，
// 并用真实样例核对过）：文件是 C++ 结构体的裸 dump，没有魔数、没有版本号。
//   +0     int64 条数（小端）
//   之后   每条固定 248 字节：ClientInfo / CharacterCore / emote / weapon / timeout / addr
// 因为没有版本号，解析只做长度与字段合法性校验：任何不一致都按解析失败处理，
// 而不是猜一个布局继续读。样例 latest.poses 是 182 条 × 248 + 8 = 45144 字节，已核对。

#include <engine/shared/protocol.h>

#include <cstddef>
#include <cstdint>
#include <vector>

struct SPoseTee
{
	char m_aName[MAX_NAME_LENGTH];
	char m_aClan[MAX_CLAN_LENGTH];
	char m_aSkin[24];
	bool m_UseCustomColor;
	int m_ColorBody;
	int m_ColorFeet;
	// 位置、角度、钩子状态；坐标是 DDNet 世界坐标（int），角度是 0-255 刻度。
	int m_X;
	int m_Y;
	int m_Angle;
	int m_Direction;
	int m_HookState;
	int m_HookX;
	int m_HookY;
	int m_Emote;
	int m_Weapon;
};

namespace qm_pose
{
	// 单条记录的字节数，用于长度校验。
	constexpr size_t RECORD_SIZE = 248;
	// 坐标合法性上限：DDNet 地图坐标是 int，但实际世界远小于这个量级。
	// 超过它说明布局对不上（例如对方改了结构体或换了 32 位构建）。
	constexpr int MAX_COORDINATE = 1000000;
	constexpr size_t MAX_POSES = 4096;

	enum class ELoadResult
	{
		OK,
		EMPTY, // 文件或条数为 0
		SIZE_MISMATCH, // 长度与条数不符（布局对不上）
		TOO_MANY, // 条数超过上限
		BAD_COORDINATE, // 坐标越界
	};

	const char *LoadResultName(ELoadResult Result);

	// 解析整份文件。失败时不修改 vOut（调用方不会拿到半份数据）。
	ELoadResult Load(const void *pData, size_t Size, std::vector<SPoseTee> &vOut);

	// 从 4×int32 打包的名字里取出字符串（-128 偏移，遇到 0 截断）。
	// 供测试与诊断使用；解析路径内部同源。
	void ReadPackedString(const int32_t *pInts, size_t NumInts, char *pOut, size_t OutSize);
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_POSE_FILE_H
