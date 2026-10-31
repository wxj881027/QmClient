#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CAMERA_NETWORK_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CAMERA_NETWORK_H

#include <generated/protocol.h>

#include <game/client/components/camera.h>

namespace QmCameraNetwork
{
	// 仅量化消息副本，不限制本地镜头目标或更改消息格式。
	inline CNetMsg_Cl_CameraInfo CameraInfo(float Zoom, int Deadzone, int FollowFactor)
	{
		CNetMsg_Cl_CameraInfo Message{};
		Message.m_Zoom = QmCameraEffects::EncodeZoom(Zoom);
		Message.m_Deadzone = Deadzone;
		Message.m_FollowFactor = FollowFactor;
		return Message;
	}

	inline CNetMsg_Cl_ShowDistance ShowDistance(float Width, float Height)
	{
		CNetMsg_Cl_ShowDistance Message{};
		Message.m_X = QmCameraEffects::EncodeNonNegativeInt(Width);
		Message.m_Y = QmCameraEffects::EncodeNonNegativeInt(Height);
		return Message;
	}
}

#endif
