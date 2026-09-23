/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef BASE_SYSTEM_H
#define BASE_SYSTEM_H

// QmClient 兼容聚合头。
//
// 上游自 19.8 起把 base/system.h 拆成了 aio/bytes/io/net/os/process 等独立头文件。
// 本仓库仍有 270+ 处 include <base/system.h>，为了不在同一切片里改动这些调用方，
// 这里保留 system.h 作为聚合入口。新代码请直接 include 具体头文件；
// 调用方的 include 清理留到后续切片，不阻塞上游同步。
#include "aio.h"
#include "bytes.h"
#include "dbg.h"
#include "detect.h"
#include "fs.h"
#include "io.h"
#include "log.h"
#include "mem.h"
#include "net.h"
#include "os.h"
#include "process.h"
#include "secure.h"
#include "sphore.h"
#include "str.h"
#include "thread.h"
#include "time.h"
#include "types.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

// QmClient 自有：Windows qWave 网络优先级（QoS）接口。
// 实现位于 src/base/net.cpp 末尾（需要 NETSOCKET 内部结构），调用方见 engine/shared/network_client.cpp。
// 尽力为指定 UDP 目标创建客户端 QoS flow，失败时返回 nullptr。
NETQOS net_qos_add_socket(NETSOCKET sock, const NETADDR *addr, ENetQosStatus *pStatus);
void net_qos_remove_socket(NETQOS qos);

#endif
