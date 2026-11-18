#ifndef GAME_CLIENT_QMUI_QMVOTEMAPPREVIEW_H
#define GAME_CLIENT_QMUI_QMVOTEMAPPREVIEW_H

#include <base/str.h>

#include <game/localization.h>

#include <string>

// 投票页预览目标的元数据与标题说明。面板本体由 CQmMapPreviewPanel 提供，
// 与服务器列表共用同一套地图缓存、拖拽/缩放和实体层开关。
namespace qm_vote_map_preview
{
	// 打开面板时拷贝的一帧地图信息；切换目标后旧内容不会残留在标题里。
	struct SInfo
	{
		std::string m_Mapper;
		int m_Stars = -1;
		int m_Points = -1;
	};

	// 预览目标：地图库页取用户选中的那张图，其余子页取当前正在玩的地图。
	// 两者都为空时按钮置灰（没有可预览的对象），不打开空面板。
	inline const char *TargetName(const char *pSelectedName, const char *pCurrentMap)
	{
		if(pSelectedName != nullptr && pSelectedName[0] != '\0')
			return pSelectedName;
		return pCurrentMap != nullptr ? pCurrentMap : "";
	}

	// 面板标题右侧的一行说明：作者 / 难度 / 分数。缺项用破折号占位，
	// 不改变行数，切换目标时标题宽度不会跳动。
	inline void FormatCaption(const SInfo &Info, char *pBuf, const int BufSize)
	{
		if(pBuf == nullptr || BufSize <= 0)
			return;
		char aDifficulty[32];
		char aPoints[32];
		if(Info.m_Stars >= 0)
			str_format(aDifficulty, sizeof(aDifficulty), "%d / 5", Info.m_Stars);
		else
			str_copy(aDifficulty, "—");
		if(Info.m_Points >= 0)
			str_format(aPoints, sizeof(aPoints), "%d", Info.m_Points);
		else
			str_copy(aPoints, "—");
		str_format(pBuf, BufSize, "%s %s · %s %s · %s %s",
			Localize("Mapper"), Info.m_Mapper.empty() ? "—" : Info.m_Mapper.c_str(),
			Localize("Difficulty"), aDifficulty,
			Localize("Points"), aPoints);
	}
} // namespace qm_vote_map_preview

#endif
