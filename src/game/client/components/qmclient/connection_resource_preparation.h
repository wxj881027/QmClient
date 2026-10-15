#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CONNECTION_RESOURCE_PREPARATION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CONNECTION_RESOURCE_PREPARATION_H

// 连接的图形资源准备完成后只放行一次；断开或重新开始时撤销旧连接的放行资格。
class CConnectionResourcePreparation
{
	bool m_Pending = false;

public:
	void Begin() { m_Pending = true; }
	void Cancel() { m_Pending = false; }
	bool Pending() const { return m_Pending; }
	bool CompleteWhenReady(bool BackgroundReady, bool ForegroundReady)
	{
		if(!m_Pending || !BackgroundReady || !ForegroundReady)
			return false;
		m_Pending = false;
		return true;
	}
};

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_CONNECTION_RESOURCE_PREPARATION_H
