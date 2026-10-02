#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PLAYER_VOLUME_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PLAYER_VOLUME_H

#include <cstdint>
#include <memory>
#include <string>

// 只在媒体后台线程使用，COM 对象随该线程的 apartment 一起释放。
class CQmPlayerVolume
{
	struct SImpl;
	std::unique_ptr<SImpl> m_pImpl;

public:
	struct SSnapshot
	{
		bool m_Available = false;
		bool m_Muted = false;
		float m_Level = 0.0f;
		uint64_t m_Generation = 0;
	};
	CQmPlayerVolume();
	~CQmPlayerVolume();
	SSnapshot Refresh(const std::string &SourceAppId);
	bool SetVolume(uint64_t Generation, float Level);
	void Reset();
};

#endif
