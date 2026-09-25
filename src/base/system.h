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

/**
 * Sends a packet over an UDP socket.
 *
 * @ingroup Network-UDP
 *
 * @param sock Socket to use.
 * @param addr Where to send the packet.
 * @param data Pointer to the packet data to send.
 * @param size Size of the packet.
 *
 * @return On success it returns the number of bytes sent. Returns `-1` on error.
 */
int net_udp_send(NETSOCKET sock, const NETADDR *addr, const void *data, int size);

/**
 * Receives a packet over an UDP socket.
 *
 * @ingroup Network-UDP
 *
 * @param sock Socket to use.
 * @param addr Pointer to an NETADDR that will receive the address.
 * @param data Received data. Will be invalidated when this function is called again.
 *
 * @return On success it returns the number of bytes received. Returns `-1` on error.
 */
int net_udp_recv(NETSOCKET sock, NETADDR *addr, unsigned char **data);

/**
 * 检查 UDP socket 是否已被系统关闭。
 *
 * @ingroup Network-UDP
 *
 * @param sock 要检查的 socket。
 *
 * @return 当 socket 已不可继续使用、必须重建时返回 `true`。
 */
bool net_udp_is_broken(NETSOCKET sock);

/**
 * Closes an UDP socket.
 *
 * @ingroup Network-UDP
 *
 * @param sock Socket to close.
 */
void net_udp_close(NETSOCKET sock);

/**
 * @defgroup Network-TCP TCP Networking
 *
 * @ingroup Network
 */

/**
 * Creates a TCP socket.
 *
 * @ingroup Network-TCP
 *
 * @param bindaddr Address to bind the socket to.
 *
 * @return On success it returns an handle to the socket. On failure it returns `nullptr`.
 */
NETSOCKET net_tcp_create(NETADDR bindaddr);

/**
 * Makes the socket start listening for new connections.
 *
 * @ingroup Network-TCP
 *
 * @param sock Socket to start listen to.
 * @param backlog Size of the queue of incoming connections to keep.
 *
 * @return `0` on success.
 */
int net_tcp_listen(NETSOCKET sock, int backlog);

/**
 * Polls a listening socket for a new connection.
 *
 * @ingroup Network-TCP
 *
 * @param sock Listening socket to poll.
 * @param new_sock Pointer to a socket to fill in with the new socket.
 * @param addr Pointer to an address that will be filled in the remote address, can be `nullptr`.
 *
 * @return A non-negative integer on success. Negative integer on failure.
 */
int net_tcp_accept(NETSOCKET sock, NETSOCKET *new_sock, NETADDR *addr);

/**
 * Connects one socket to another.
 *
 * @ingroup Network-TCP
 *
 * @param sock Socket to connect.
 * @param addr Address to connect to.
 *
 * @return `0` on success.
 *
 */
int net_tcp_connect(NETSOCKET sock, const NETADDR *addr);

/**
 * Connect a socket to a TCP address without blocking.
 *
 * @ingroup Network-TCP
 *
 * @param sock The socket to connect with.
 * @param bindaddr The address to connect to.
 *
 * @returns `0` on success.
 */
int net_tcp_connect_non_blocking(NETSOCKET sock, NETADDR bindaddr);

/**
 * Sends data to a TCP stream.
 *
 * @ingroup Network-TCP
 *
 * @param sock Socket to send data to.
 * @param data Pointer to the data to send.
 * @param size Size of the data to send.
 *
 * @return Number of bytes sent. Negative value on failure.
 */
int net_tcp_send(NETSOCKET sock, const void *data, int size);

/**
 * Receives data from a TCP stream.
 *
 * @ingroup Network-TCP
 *
 * @param sock Socket to recvive data from.
 * @param data Pointer to a buffer to write the data to.
 * @param maxsize Maximum of data to write to the buffer.
 *
 * @return Number of bytes recvived. Negative value on failure. When in
 * non-blocking mode, it returns 0 when there is no more data to be fetched.
 */
int net_tcp_recv(NETSOCKET sock, void *data, int maxsize);

/**
 * Closes a TCP socket.
 *
 * @ingroup Network-TCP
 *
 * @param sock Socket to close.
 */
void net_tcp_close(NETSOCKET sock);

#if defined(CONF_FAMILY_UNIX)
/**
 * @defgroup Network-Unix-Sockets UNIX Socket Networking
 *
 * @ingroup Network
 */

/**
 * Creates an unnamed unix datagram socket.
 *
 * @ingroup Network-Unix-Sockets
 *
 * @return On success it returns a handle to the socket. On failure it returns `-1`.
 */
UNIXSOCKET net_unix_create_unnamed();

/**
 * Sends data to a Unix socket.
 *
 * @ingroup Network-Unix-Sockets
 *
 * @param sock Socket to use.
 * @param addr Where to send the packet.
 * @param data Pointer to the packet data to send.
 * @param size Size of the packet.
 *
 * @return Number of bytes sent. Negative value on failure.
 */
int net_unix_send(UNIXSOCKET sock, UNIXSOCKETADDR *addr, void *data, int size);

/**
 * Sets the unixsocketaddress for a path to a socket file.
 *
 * @ingroup Network-Unix-Sockets
 *
 * @param addr Pointer to the `UNIXSOCKETADDR` to fill.
 * @param path Path to the (named) unix socket.
 */
void net_unix_set_addr(UNIXSOCKETADDR *addr, const char *path);

/**
 * Closes a Unix socket.
 *
 * @ingroup Network-Unix-Sockets
 *
 * @param sock Socket to close.
 */
void net_unix_close(UNIXSOCKET sock);

#endif

/**
 * Swaps the endianness of data. Each element is swapped individually by reversing its bytes.
 *
 * @param data Pointer to data to be swapped.
 * @param elem_size Size in bytes of each element.
 * @param num Number of elements.
 *
 * @remark The caller must ensure that the data is at least `elem_size * num` bytes in size.
 */
void swap_endian(void *data, unsigned elem_size, unsigned num);

void net_stats(NETSTATS *stats);

/**
 * Packs 4 big endian bytes into an unsigned.
 *
 * @param bytes Pointer to an array of bytes that will be packed.
 *
 * @return The packed unsigned.
 *
 * @remark Assumes the passed array is least 4 bytes in size.
 * @remark Assumes unsigned is 4 bytes in size.
 *
 * @see uint_to_bytes_be
 */
unsigned bytes_be_to_uint(const unsigned char *bytes);

/**
 * Packs an unsigned into 4 big endian bytes.
 *
 * @param bytes Pointer to an array where the bytes will be stored.
 * @param value The values that will be packed into the array.
 *
 * @remark Assumes the passed array is least 4 bytes in size.
 * @remark Assumes unsigned is 4 bytes in size.
 *
 * @see bytes_be_to_uint
 */
void uint_to_bytes_be(unsigned char *bytes, unsigned value);

/**
 * Shell, process management, OS specific functionality.
 *
 * @defgroup Shell Shell
 */

/**
 * Returns the ID of the current process.
 *
 * @ingroup Shell
 *
 * @return PID of the current process.
 */
int pid();

/**
 * Fixes the command line arguments to be encoded in UTF-8 on all systems.
 *
 * @ingroup Shell
 *
 * @param argc A pointer to the argc parameter that was passed to the main function.
 * @param argv A pointer to the argv parameter that was passed to the main function.
 *
 * @remark You need to call @link cmdline_free @endlink once you're no longer using the results.
 */
void cmdline_fix(int *argc, const char ***argv);

/**
 * Frees memory that was allocated by @link cmdline_fix @endlink.
 *
 * @ingroup Shell
 *
 * @param argc The argc obtained from `cmdline_fix`.
 * @param argv The argv obtained from `cmdline_fix`.
 */
void cmdline_free(int argc, const char **argv);

#if !defined(CONF_PLATFORM_ANDROID)
/**
 * Determines the initial window state when using @link shell_execute @endlink
 * to execute a process.
 *
 * @ingroup Shell
 *
 * @remark Currently only supported on Windows.
 */
/**
 * Executes a given file.
 *
 * @ingroup Shell
 *
 * @param file The file to execute.
 * @param window_state The window state how the process window should be shown.
 * @param arguments Optional array of arguments to pass to the process.
 * @param num_arguments The number of arguments.
 *
 * @return Handle of the new process, or @link INVALID_PROCESS @endlink on error.
 */
PROCESS shell_execute(const char *file, EShellExecuteWindowState window_state, const char **arguments = nullptr, size_t num_arguments = 0);

/**
 * Sends kill signal to a process.
 *
 * @ingroup Shell
 *
 * @param process Handle of the process to kill.
 *
 * @return `1` on success, `0` on error.
 */
int kill_process(PROCESS process);

/**
 * Checks if a process is alive.
 *
 * @ingroup Shell
 *
 * @param process Handle/PID of the process.
 *
 * @return `true` if the process is currently running,
 * @return `false` if the process is not running (dead).
 */
bool is_process_alive(PROCESS process);

/**
 * Opens a link in the browser.
 *
 * @ingroup Shell
 *
 * @param link The link to open in a browser.
 *
 * @return `1` on success, `0` on failure.
 *
 * @remark The strings are treated as null-terminated strings.
 * @remark This may not be called with untrusted input or it'll result in arbitrary code execution, especially on Windows.
 */
int open_link(const char *link);

/**
 * Opens a file or directory with the default program.
 *
 * @ingroup Shell
 *
 * @param path The file or folder to open with the default program.
 *
 * @return `1` on success, `0` on failure.
 *
 * @remark The strings are treated as null-terminated strings.
 * @remark This may not be called with untrusted input or it'll result in arbitrary code execution, especially on Windows.
 */
int open_file(const char *path);
#endif // !defined(CONF_PLATFORM_ANDROID)

/**
 * Returns a human-readable version string of the operating system.
 *
 * @ingroup Shell
 *
 * @param version Buffer to use for the output.
 * @param length Length of the output buffer.
 *
 * @return `true` on success, `false` on failure.
 */
bool os_version_str(char *version, size_t length);

/**
 * Returns a string of the preferred locale of the user / operating system.
 * The string conforms to [RFC 3066](https://www.ietf.org/rfc/rfc3066.txt)
 * and only contains the characters `a`-`z`, `A`-`Z`, `0`-`9` and `-`.
 * If the preferred locale could not be determined this function
 * falls back to the locale `"en-US"`.
 *
 * @ingroup Shell
 *
 * @param locale Buffer to use for the output.
 * @param length Length of the output buffer.
 *
 * @remark The strings are treated as null-terminated strings.
 */
void os_locale_str(char *locale, size_t length);

/**
 * Fixes the command line arguments to be encoded in UTF-8 on all systems.
 * This is a RAII wrapper for @link cmdline_fix @endlink and @link cmdline_free @endlink.
 *
 * @ingroup Shell
 */
#endif
