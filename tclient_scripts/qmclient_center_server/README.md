# Q1menG 客户端中心识别服务器

这个服务端与当前客户端代码对齐，提供 3 个接口：

- `GET /token`
- `POST /report`
- `GET /users.json`

## 1. 启动

```bash
cd tclient_scripts/qmclient_center_server
npm install
AUTH_SECRET="replace-with-random-long-secret" PORT=8080 npm start
```

## 2. 客户端配置

当前客户端已写死中心服务器地址为：

- `http://42.194.185.210:8080/token`
- `http://42.194.185.210:8080/report`
- `http://42.194.185.210:8080/users.json`

客户端只需要一个开关：

```cfg
qm_client_mark_trail 1
```

如果后续要改 IP 或端口，修改 `src/game/client/components/tclient/tclient.cpp` 里的常量：

- `QMCLIENT_TOKEN_URL`
- `QMCLIENT_REPORT_URL`
- `QMCLIENT_USERS_URL`

## 3. 接口契约

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

## 5. 生产建议

- 建议放到 HTTPS 反向代理后面（Nginx/Caddy）。
- `AUTH_SECRET` 用高强度随机串并持久化。
- 多实例部署时把内存存储改成 Redis（token 与在线用户都放 Redis）。
