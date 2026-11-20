#ifndef ENGINE_CLIENT_SERVER_INFO_UPDATE_H
#define ENGINE_CLIENT_SERVER_INFO_UPDATE_H

#include <engine/serverbrowser.h>

#include <type_traits>
#include <utility>

namespace server_info
{
	// 复制和后处理都只改候选；准备异常沿调用方原恢复入口传播。
	template<typename FPrepare>
	void Update(CServerInfo &Target, const CServerInfo &Source, FPrepare &&Prepare)
	{
		static_assert(std::is_nothrow_move_assignable_v<CServerInfo>);
		CServerInfo Candidate(Source);
		Prepare(Candidate);
		Target = std::move(Candidate);
	}
}

#endif
