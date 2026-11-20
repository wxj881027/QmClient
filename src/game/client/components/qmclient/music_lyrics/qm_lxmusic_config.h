#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_LXMUSIC_CONFIG_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_LXMUSIC_CONFIG_H

#include <string>
#include <string_view>

// 从 LX Music 自己的配置文件里读出开放 API 的真实端口。
// 用户可以在 LX 设置里改端口,固定 23330 会失效;这里直接读它的配置,
// 让「改了端口」不需要用户再到 QmClient 手动同步。
// 文件:%APPDATA%/lx-music-desktop/LxDatas/config_v2.json 的 setting 段。
namespace QmLxMusic
{
	// 上游开放 API 的默认端口:配置里没写端口时 LX 会回落到该值。
	constexpr int DEFAULT_OPEN_API_PORT = 23330;

	struct SClientConfig
	{
		bool m_Found = false;
		bool m_OpenApiEnabled = false;
		int m_OpenApiPort = 0;
		bool m_SyncServerEnabled = false;
		int m_SyncServerPort = 0;

		int EffectiveOpenApiPort() const { return m_OpenApiPort > 0 ? m_OpenApiPort : DEFAULT_OPEN_API_PORT; }

		// 两个服务占同一端口时只有先启动的那个能绑定,另一个静默失效:
		// 这正是用户机器上「/status 返回 401」的成因,需要提示用户错开。
		// 端口字段缺失时按上游默认值比较,否则会漏判「配置里没写端口」的冲突。
		bool HasPortConflict() const
		{
			return m_SyncServerEnabled && m_SyncServerPort > 0 && EffectiveOpenApiPort() == m_SyncServerPort;
		}
	};

	// 解析 config_v2.json;缺少 setting 段时返回 false(保持输出不变)。
	bool ParseClientConfig(std::string_view Json, SClientConfig *pOut);
	// 读取本机 LX Music 配置;文件不存在或非 Windows 时返回 false。
	bool LoadClientConfig(SClientConfig *pOut);

	// 纯文本改写,只动 setting 段内的目标字段,其余字节保持不变。
	// 独立出来是为了能在不触碰文件系统的前提下单测改写逻辑:
	// 这段代码会直接改用户的 LX 配置文件,写坏一次代价很高。
	bool ConfigTextSetBool(std::string &Text, const char *pKey, bool Value);
	bool ConfigTextSetPort(std::string &Text, const char *pKey, int Port);

	enum class EEnableResult
	{
		Ok,
		AlreadyEnabled,
		ClientRunning,
		NotInstalled,
		WriteFailed,
	};

	// 替用户打开开放 API:LX 必须未运行(运行中改会被它退出时覆盖)。
	// 只替换 openAPI.enable 一处字节并处理端口冲突,其余配置保持原样;
	// 写前备份为 config_v2.json.qmclient-bak。
	// pMessage 返回面向用户的一行结果说明。
	EEnableResult EnableOpenApi(std::string *pMessage);
}

#endif
