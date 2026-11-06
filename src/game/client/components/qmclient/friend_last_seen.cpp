#include "friend_last_seen.h"

#include <base/system.h>

#include <engine/http.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <new>
#include <stdexcept>

CQmFriendLastSeen::CQmFriendLastSeen() :
	m_Service(HttpGet)
{
}

void CQmFriendLastSeen::OnInit()
{
	(void)m_Service.Load(*Storage(), time_timestamp());
}

const SQmFriendLastSeenEntry *CQmFriendLastSeen::Get(const char *pName)
{
	return g_Config.m_QmFriendLastSeen ? m_Service.Get(pName, time_timestamp()) : nullptr;
}

void CQmFriendLastSeen::OnUpdate()
{
	m_Service.Update(*GameClient()->Friends(), *Engine(), *Http(), g_Config.m_QmFriendLastSeen != 0, time_timestamp());
}

void CQmFriendLastSeen::OnShutdown()
{
	m_Service.CancelRequests();
	try
	{
		(void)m_Service.Save(*Storage());
	}
	catch(const std::bad_alloc &)
	{
		// 退出边界不重放未知的存储提交，也不让路径转换分配失败阻断关闭。
	}
	catch(const std::length_error &)
	{
	}
}
