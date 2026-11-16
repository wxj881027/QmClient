// 静态成员必须无条件定义：IVideo 的声明不在 CONF_VIDEORECORDER 之内，
// 关闭录像的构建（如 Android）里 HUD/聊天/后端仍会读 Current()。
#include "video.h"

IVideo *IVideo::ms_pCurrentVideo = nullptr;
bool IVideo::ms_LastStopFailed = false;

int64_t IVideo::ms_Time = 0;
float IVideo::ms_LocalTime = 0;
int64_t IVideo::ms_LocalStartTime = 0;
int64_t IVideo::ms_TickTime = 0;
