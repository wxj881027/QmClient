#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_LOCAL_TIME_FORMAT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_LOCAL_TIME_FORMAT_H

#include <base/time.h>

// 本地时间统一格式化入口：所有 HUD 渲染点共用同一条“是否显示秒”的判断，
// 避免媒体岛、灵动岛与常驻时钟各自硬编码格式导致开关只对部分位置生效。
inline void QmFormatLocalTime(char *pBuffer, int BufferSize, bool IncludeSeconds)
{
	str_timestamp_format(pBuffer, BufferSize, IncludeSeconds ? "%H:%M:%S" : "%H:%M");
}

#endif
