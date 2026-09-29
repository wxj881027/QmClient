#ifndef QM_MUSIC_QM_KUGOU_PROTOCOL_H
#define QM_MUSIC_QM_KUGOU_PROTOCOL_H

#include "qm_music_source.h"

#include <functional>
#include <string_view>
#include <vector>

namespace QmMusicHook
{
	bool ParseKugouPlayback(std::string_view Json, SPlayback *pState);
	bool SelectKugouLyricCandidate(std::string_view Json, std::string *pId, std::string *pAccessKey);
	bool DecodeKugouLyricResponse(std::string_view Json, bool Krc, std::string *pText);

	struct SKugouPatch
	{
		uint64_t m_Offset;
		std::string m_Original;
		std::string m_Patched;
	};
	// 指纹来自酷狗 20.1.22.27795 附带的 CEF 89.20.0，未知版本绝不写入。
	const std::vector<SKugouPatch> &KugouPatches();
	enum class EKugouPatchState
	{
		UNSUPPORTED,
		ORIGINAL,
		PATCHED,
	};
	using TKugouReadBytes = std::function<bool(uint64_t, void *, size_t)>;
	EKugouPatchState ClassifyKugouPatch(const TKugouReadBytes &Read);
	// 分块验证当前 DLL 相对备份只有九处已知改动，避免恢复覆盖软件升级。
	bool IsKugouRestoreChunk(uint64_t Offset, std::string_view Original, std::string_view Current, bool AllowPartial = false);
}

#endif
