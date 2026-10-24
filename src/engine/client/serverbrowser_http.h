#ifndef ENGINE_CLIENT_SERVERBROWSER_HTTP_H
#define ENGINE_CLIENT_SERVERBROWSER_HTTP_H
#include <base/types.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class CServerInfo;
class IEngine;
class IStorage;
class IHttp;
class IHttpRequest;
typedef struct _json_value json_value;

using FServerListParser = std::function<bool(json_value *, std::vector<CServerInfo> *)>;

class IServerBrowserHttp
{
public:
	virtual ~IServerBrowserHttp() = default;

	virtual void Shutdown() = 0;
	virtual void Update() = 0;

	virtual uint64_t Revision() const = 0;
	virtual bool IsRefreshing() const = 0;
	virtual bool IsError() const = 0;
	virtual bool IsStale() const = 0;
	virtual void Refresh() = 0;

	virtual bool GetBestUrl(const char **pBestUrl) const = 0;

	virtual int NumServers() const = 0;
	virtual const CServerInfo &Server(int Index) const = 0;
};

IServerBrowserHttp *CreateServerBrowserHttp(IEngine *pEngine, IStorage *pStorage, IHttp *pHttp, const char *pPreviousBestUrl);
// 注入 HTTP、时钟、解析与写入边界，生产调度和缓存仍共用同一个实现。
IServerBrowserHttp *CreateServerBrowserHttpWithSources(IEngine *pEngine, IStorage *pStorage, IHttp *pHttp,
	std::vector<std::string> vUrls, const char *pPreviousBestUrl,
	std::function<std::shared_ptr<IHttpRequest>(const char *)> Factory,
	std::function<int64_t()> Clock, bool UseFallbackTiers = false,
	std::function<int(int)> RetryDelay = {},
	std::function<bool(const std::string &, const std::string &)> SaveWriter = {}, FServerListParser ParseList = {});
#endif // ENGINE_CLIENT_SERVERBROWSER_HTTP_H
