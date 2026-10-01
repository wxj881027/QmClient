#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SPECTATOR_FRIEND_PRIORITY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SPECTATOR_FRIEND_PRIORITY_H

namespace qm_spectator_friends
{
	// 好友优先的显示顺序：好友保持原有相对顺序排在最前，其余玩家保持原有相对顺序紧随其后。
	// 这是一个稳定分区，不排序、不改动任何组内相对顺序；pOrder 写出的是原数组下标。
	// 返回好友数量，也就是好友分组在显示序列里的长度（好友分组必然是显示序列的前缀）。
	// 只做两次线性扫描，下标全部由调用方提供，无分配、无加锁，可在渲染路径上逐帧调用。
	inline int BuildFriendFirstOrder(const bool *pIsFriend, int Count, int *pOrder, bool Enabled = true)
	{
		if(Count <= 0)
			return 0;
		if(!Enabled)
		{
			for(int Index = 0; Index < Count; ++Index)
				pOrder[Index] = Index;
			return 0;
		}

		int Friends = 0;
		for(int Index = 0; Index < Count; ++Index)
		{
			if(pIsFriend[Index])
				pOrder[Friends++] = Index;
		}

		int Write = Friends;
		for(int Index = 0; Index < Count; ++Index)
		{
			if(!pIsFriend[Index])
				pOrder[Write++] = Index;
		}

		return Friends;
	}
}

#endif
