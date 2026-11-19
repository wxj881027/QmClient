#ifndef GAME_EDITOR_COLLAB_JOIN_H
#define GAME_EDITOR_COLLAB_JOIN_H

namespace EditorCollab
{
	// 首次加入以房间为基准；重连只加载较新的版本，以保留同版本上的本地编辑。
	template<typename FLoad>
	bool AcceptJoinSnapshot(const char *pSnapshot, int RoomRevision, bool Rejoining, int &LocalRevision, bool &JoinedTransport, const FLoad &Load)
	{
		JoinedTransport = false;
		if(!Rejoining || RoomRevision > LocalRevision)
		{
			if(pSnapshot[0] != '\0' && !Load(pSnapshot, RoomRevision))
				return false;
			LocalRevision = RoomRevision;
		}
		JoinedTransport = true;
		return true;
	}
}

#endif
