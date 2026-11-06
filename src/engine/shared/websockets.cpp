#if defined(CONF_WEBSOCKETS)

#include "websockets.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/shared/network.h>
#include <engine/shared/protocol.h>
#include <engine/shared/websocket_log.h>
#include <engine/shared/websocket_native_state.h>

#if defined(CONF_FAMILY_UNIX)
#include <arpa/inet.h>
#elif defined(CONF_FAMILY_WINDOWS)
#include <ws2tcpip.h>
#endif
#include <libwebsockets.h>

#include <algorithm>
#include <iterator>
#include <limits>

// NOLINTBEGIN(readability-identifier-naming)
struct context_data
{
	char bindaddr_str[NETADDR_MAXSTRSIZE];
	lws_context_creation_info creation_info;
	lws_context *context = nullptr;
	bool creating = false;
	CWebsocketNativeState state;
};

// Client has main, dummy and contact connections with IPv4 and IPv6
static context_data contexts[3 * 2];

static lws_context *websocket_context(int socket)
{
	dbg_assert(socket >= 0 && socket < (int)std::size(contexts), "socket index invalid: %d", socket);
	lws_context *context = contexts[socket].context;
	dbg_assert(context != nullptr, "socket context not initialized: %d", socket);
	return context;
}

static bool sockaddr_to_netaddr_websocket(const sockaddr *src, socklen_t src_len, NETADDR *dst)
{
	*dst = NETADDR_ZEROED;
	if(src->sa_family == AF_INET && src_len >= (socklen_t)sizeof(sockaddr_in))
	{
		const sockaddr_in *src_in = (const sockaddr_in *)src;
		dst->type = NETTYPE_WEBSOCKET_IPV4;
		dst->port = htons(src_in->sin_port);
		static_assert(sizeof(dst->ip) >= sizeof(src_in->sin_addr.s_addr));
		mem_copy(dst->ip, &src_in->sin_addr.s_addr, sizeof(src_in->sin_addr.s_addr));
		return true;
	}
	if(src->sa_family == AF_INET6 && src_len >= (socklen_t)sizeof(sockaddr_in6))
	{
		const sockaddr_in6 *src_in6 = (const sockaddr_in6 *)src;
		dst->type = NETTYPE_WEBSOCKET_IPV6;
		dst->port = htons(src_in6->sin6_port);
		static_assert(sizeof(dst->ip) >= sizeof(src_in6->sin6_addr.s6_addr));
		mem_copy(dst->ip, &src_in6->sin6_addr.s6_addr, sizeof(src_in6->sin6_addr.s6_addr));
		return true;
	}
	return false;
}

static int websocket_protocol_callback_impl(context_data *ctx_data, lws *wsi, enum lws_callback_reasons reason, void *in, size_t len)
{
	auto &state = ctx_data->state;
	switch(reason)
	{
	case LWS_CALLBACK_WSI_CREATE:
#if defined(CONF_FAMILY_UNIX)
		// 尚未分配fd的创建通知不建立会话；高fd仍在进入select前拒绝。
		if(lws_get_socket_fd(wsi) >= FD_SETSIZE)
			return -1;
#endif
		return 0;

	case LWS_CALLBACK_ESTABLISHED:
	case LWS_CALLBACK_CLIENT_ESTABLISHED:
	{
#if defined(CONF_FAMILY_UNIX)
		if(lws_get_socket_fd(wsi) >= FD_SETSIZE)
			return -1;
#endif
		sockaddr_storage peersockaddr{};
		socklen_t peersockaddr_size = sizeof(peersockaddr);
		if(getpeername(lws_get_socket_fd(wsi), (sockaddr *)&peersockaddr, &peersockaddr_size) != 0)
			return -1;
		NETADDR addr;
		if(!sockaddr_to_netaddr_websocket((sockaddr *)&peersockaddr, peersockaddr_size, &addr) || !state.TryRegister(wsi, addr))
			return -1;
		char addr_str[NETADDR_MAXSTRSIZE];
		net_addr_str(&addr, addr_str, sizeof(addr_str), true);
		log_trace("websockets", "Connection established with '%s'", addr_str);
		return 0;
	}

	case LWS_CALLBACK_CLIENT_CLOSED:
	case LWS_CALLBACK_CLOSED:
	{
		auto *pss = state.Find(wsi);
		if(!pss)
			return 0;
		const NETADDR addr = pss->m_Addr;
		state.Remove(wsi);
		static const unsigned char CLOSE_PACKET[] = {0x10, 0x0e, 0x00, 0x04};
		state.Receive(addr, CLOSE_PACKET, sizeof(CLOSE_PACKET));
		char addr_str[NETADDR_MAXSTRSIZE];
		net_addr_str(&addr, addr_str, sizeof(addr_str), true);
		log_trace("websockets", "Connection closed with '%s'", addr_str);
		return 0;
	}

	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
	case LWS_CALLBACK_WSI_DESTROY:
		state.Remove(wsi);
		return 0;

	case LWS_CALLBACK_CLIENT_WRITEABLE:
	case LWS_CALLBACK_SERVER_WRITEABLE:
	{
		auto *pss = state.Find(wsi);
		if(!pss)
			return -1;
		auto *chunk = pss->m_SendBuffer.First();
		if(!chunk)
			return 0;
		const int chunk_len = static_cast<int>(chunk->m_Size - chunk->m_Read);
		const int n = lws_write(wsi, &chunk->m_aData[LWS_SEND_BUFFER_PRE_PADDING + chunk->m_Read], chunk_len, LWS_WRITE_BINARY);
		if(n < 0)
			return -1;
		if(n < chunk_len)
		{
			chunk->m_Read += n;
			lws_callback_on_writable(wsi);
			return 0;
		}
		pss->m_SendBuffer.PopFirst();
		lws_callback_on_writable(wsi);
		return 0;
	}

	case LWS_CALLBACK_CLIENT_RECEIVE:
	case LWS_CALLBACK_RECEIVE:
	{
		auto *pss = state.Find(wsi);
		return pss && state.Receive(pss->m_Addr, in, len) ? 0 : -1;
	}
	default:
		return 0;
	}
}

static int websocket_protocol_callback(lws *wsi, enum lws_callback_reasons reason, void *, void *in, size_t len) noexcept
{
	if(!wsi)
		return 0;
	auto *context = lws_get_context(wsi);
	auto *ctx_data = context ? static_cast<context_data *>(lws_context_user(context)) : nullptr;
	if(!ctx_data)
		return 0;
	return ctx_data->state.Dispatch(wsi, [&] { return websocket_protocol_callback_impl(ctx_data, wsi, reason, in, len); });
}

static const lws_protocols protocols[] = {
	{"binary", websocket_protocol_callback, 0},
	{"base64", websocket_protocol_callback, 0},
	{nullptr, nullptr, 0}};

static LEVEL websocket_level_to_loglevel(int level)
{
	if(level & LLL_ERR)
		return LEVEL_ERROR;
	if(level & LLL_WARN)
		return LEVEL_WARN;
	if(level & (LLL_NOTICE | LLL_INFO))
		return LEVEL_DEBUG;
	return LEVEL_TRACE;
}

static void websocket_log_callback(int level, const char *line) noexcept
{
	try
	{
		char line_truncated[4096];
		CopyWebsocketLogLine(line_truncated, line);
		log_log(websocket_level_to_loglevel(level), "websockets", "%s", line_truncated);
	}
	catch(...)
	{
	}
}

void websocket_init()
{
	lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO, websocket_log_callback);
}

int websocket_create(const NETADDR *bindaddr)
{
	// find free context
	int first_free = -1;
	for(int i = 0; i < (int)std::size(contexts); i++)
	{
		if(contexts[i].context == nullptr && !contexts[i].creating)
		{
			first_free = i;
			break;
		}
	}
	if(first_free == -1)
	{
		log_error("websockets", "Failed to create websocket: no free contexts available");
		return -1;
	}

	context_data *ctx_data = &contexts[first_free];
	struct creation_guard
	{
		bool &creating;
		~creation_guard() { creating = false; }
	} guard{ctx_data->creating};
	ctx_data->creating = true;
	ctx_data->state.Reset();
	mem_zero(&ctx_data->creation_info, sizeof(ctx_data->creation_info));
	ctx_data->creation_info.options = LWS_SERVER_OPTION_FAIL_UPON_UNABLE_TO_BIND;
	if(bindaddr->type == NETTYPE_WEBSOCKET_IPV6)
	{
		// Set IPv6-only mode and socket option for IPv6 Websockets.
		ctx_data->creation_info.options |= LWS_SERVER_OPTION_IPV6_V6ONLY_VALUE | LWS_SERVER_OPTION_IPV6_V6ONLY_MODIFY;
	}
	net_addr_str(bindaddr, ctx_data->bindaddr_str, sizeof(ctx_data->bindaddr_str), false);
	if(ctx_data->bindaddr_str[0] == '[' && ctx_data->bindaddr_str[str_length(ctx_data->bindaddr_str) - 1] == ']')
	{
		// Bindaddr must not be enclosed in brackets for IPv6 Websockets.
		ctx_data->bindaddr_str[str_length(ctx_data->bindaddr_str) - 1] = '\0';
		mem_move(&ctx_data->bindaddr_str[0], &ctx_data->bindaddr_str[1], str_length(ctx_data->bindaddr_str) + 1);
	}
	ctx_data->creation_info.iface = ctx_data->bindaddr_str;
	ctx_data->creation_info.port = bindaddr->port;
	ctx_data->creation_info.protocols = protocols;
	ctx_data->creation_info.gid = -1;
	ctx_data->creation_info.uid = -1;
	ctx_data->creation_info.user = ctx_data;

	ctx_data->context = lws_create_context(&ctx_data->creation_info);
	if(ctx_data->context == nullptr)
	{
		ctx_data->state.Reset();
		return -1;
	}
	return first_free;
}

void websocket_destroy(int socket)
{
	lws_context *context = websocket_context(socket);
	lws_context_destroy(context);
	contexts[socket].context = nullptr;
	contexts[socket].state.Reset();
}

int websocket_recv(int socket, unsigned char *data, size_t maxsize, NETADDR *addr)
{
	lws_context *context = websocket_context(socket);
	const int service_result = lws_service(context, -1);
	if(service_result < 0)
	{
		return service_result;
	}

	context_data *ctx_data = &contexts[socket];
	auto *chunk = ctx_data->state.RecvBuffer().First();
	if(chunk == nullptr)
	{
		return 0;
	}

	if(maxsize >= chunk->m_Size - chunk->m_Read)
	{
		const int len = static_cast<int>(chunk->m_Size - chunk->m_Read);
		mem_copy(data, &chunk->m_aData[chunk->m_Read], len);
		*addr = chunk->m_Addr;
		ctx_data->state.RecvBuffer().PopFirst();
		return len;
	}
	else
	{
		mem_copy(data, &chunk->m_aData[chunk->m_Read], maxsize);
		*addr = chunk->m_Addr;
		chunk->m_Read += maxsize;
		return static_cast<int>(maxsize);
	}
}

int websocket_send(int socket, const unsigned char *data, size_t size, const NETADDR *addr)
{
	if(size > static_cast<size_t>(std::numeric_limits<int>::max()))
		return -1;
	lws_context *context = websocket_context(socket);
	context_data *ctx_data = &contexts[socket];
	auto *pss = ctx_data->state.Find(*addr);
	if(pss == nullptr)
	{
		char addr_str[NETADDR_MAXSTRSIZE];
		net_addr_str(addr, addr_str, sizeof(addr_str), false);
		lws_client_connect_info ccinfo = {};
		ccinfo.context = context;
		ccinfo.address = addr_str;
		ccinfo.port = addr->port;
		ccinfo.protocol = protocols[0].name;
		lws *wsi = lws_client_connect_via_info(&ccinfo);
		if(wsi == nullptr)
		{
			return -1;
		}
		lws_service(context, -1);
		pss = ctx_data->state.Find(*addr);
		if(pss == nullptr)
		{
			return -1;
		}
	}

	if(!ctx_data->state.QueueSend(*pss, data, size, LWS_SEND_BUFFER_PRE_PADDING, LWS_SEND_BUFFER_POST_PADDING))
		return -1;
	lws_callback_on_writable(pss->m_pWsi);
	lws_service(context, -1);
	return static_cast<int>(size);
}

int websocket_fd_set(int socket, fd_set *set)
{
	lws_context *context = websocket_context(socket);
	lws_service(context, -1);

	context_data *ctx_data = &contexts[socket];
	int max = 0;
	for(const auto &[_, pss] : ctx_data->state.Ports())
	{
		if(pss == nullptr)
		{
			continue;
		}
		int fd = lws_get_socket_fd(pss->m_pWsi);
#if defined(CONF_FAMILY_UNIX)
		if(!net_socket_is_selectable(fd))
			continue;
#endif
		max = std::max(fd, max);
		FD_SET(fd, set);
	}
	return max;
}

int websocket_fd_get(int socket, fd_set *set)
{
	lws_context *context = websocket_context(socket);
	lws_service(context, -1);

	context_data *ctx_data = &contexts[socket];
	for(const auto &[_, pss] : ctx_data->state.Ports())
	{
		if(pss == nullptr)
		{
			continue;
		}
		const auto fd = lws_get_socket_fd(pss->m_pWsi);
#if defined(CONF_FAMILY_UNIX)
		if(!net_socket_is_selectable(fd))
			continue;
#endif
		if(FD_ISSET(fd, set))
		{
			return 1;
		}
	}
	return 0;
}
// NOLINTEND(readability-identifier-naming)

#endif
