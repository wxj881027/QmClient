# Q1menG 客户端中心识别服务器

这个服务端与当前客户端代码对齐，提供 4 个接口：

- `GET /client/version`
- `GET /token`
- `POST /report`
- `GET /users.json`

## 1. 启动

```bash
cd qmclient_scripts/qmclient_center_server
npm install
AUTH_SECRET="replace-with-random-long-secret" PORT=8080 npm start
```

## 2. 客户端配置

当前客户端已写死中心服务器地址为：

- `http://42.194.185.210:8080/client/version`
- `http://42.194.185.210:8080/token`
- `http://42.194.185.210:8080/report`
- `http://42.194.185.210:8080/users.json`

客户端只需要一个开关：

```cfg
qm_client_mark_trail 1
```

如果后续要改 IP 或端口，修改 `src/game/client/components/qmclient/qmclient.cpp` 里的常量：

- `QMCLIENT_TOKEN_URL`
- `QMCLIENT_REPORT_URL`
- `QMCLIENT_USERS_URL`

## 3. 接口契约

### `GET /client/version`

请求示例：

```text
GET /client/version?current=2.35.2
```

返回：

```json
{
  "ok": true,
  "version": "2.35.2",
  "latest_version": "2.35.2",
  "latest_tag": "v2.35.2",
  "release_url": "https://github.com/wxj881027/QmClient/releases/tag/v2.35.2",
  "current_version": "2.35.2",
  "up_to_date": true,
  "cache_source": "github",
  "cache_expires_at": 1777321482,
  "last_error": "",
  "update_message": "当前版本不是最新版，请前往 QQ 群更新最新版"
}
```

服务端会请求 GitHub Releases API：

```text
https://api.github.com/repos/wxj881027/QmClient/releases/latest
```

默认缓存 5 分钟；GitHub 请求失败时会使用上一次成功结果，若服务刚启动且尚未成功拉取，则回退到 `CLIENT_LATEST_VERSION`。

### `GET /token`

返回：

```json
{
  "auth_token": "....",
  "expires_in": 300
}
```

### `POST /report`

请求体（客户端已按这个格式发送）：

```json
{
  "server_address": "1.2.3.4:8303",
  "auth_token": "....",
  "timestamp": 1739436900,
  "players": [
    { "player_id": 3, "dummy": false },
    { "player_id": 8, "dummy": true }
  ]
}
```

返回：

```json
{
  "ok": true,
  "accepted": 2
}
```

### `GET /users.json`

返回：

```json
{
  "users": [
    {
      "server_address": "1.2.3.4:8303",
      "player_id": 3,
      "updated_at": 1739436900
    }
  ]
}
```

## 4. 运行参数（环境变量）

- `PORT` 默认 `8080`
- `AUTH_SECRET` 默认随机（建议固定设置）
- `TOKEN_TTL_SEC` 默认 `300`
- `REPORT_TTL_SEC` 默认 `90`
- `TIME_SKEW_SEC` 默认 `600`
- `RATE_LIMIT_PER_MIN` 默认 `120`
- `REQUIRE_IP_BIND` 默认 `1`（token 绑定请求 IP）
- `TRUST_PROXY` 默认 `0`（反代场景可设为 `1`）
- `CLIENT_LATEST_VERSION` 默认 `2.35.2`
- `CLIENT_RELEASE_OWNER` 默认 `wxj881027`
- `CLIENT_RELEASE_REPO` 默认 `QmClient`
- `CLIENT_RELEASES_API_URL` 默认 GitHub latest release API 地址
- `CLIENT_VERSION_CACHE_TTL_SEC` 默认 `300`
- `CLIENT_VERSION_RETRY_DELAY_SEC` 默认 `60`
- `CLIENT_VERSION_FETCH_TIMEOUT_MS` 默认 `5000`
- `CLIENT_LATEST_TAG` 默认 `v${CLIENT_LATEST_VERSION}`，仅作为 GitHub 拉取失败时的回退
- `CLIENT_RELEASE_URL` 默认 GitHub release tag 地址，仅作为 GitHub 拉取失败时的回退

## 5. 生产建议

- 建议放到 HTTPS 反向代理后面（Nginx/Caddy）。
- `AUTH_SECRET` 用高强度随机串并持久化。
- 多实例部署时把内存存储改成 Redis（token 与在线用户都放 Redis）。
