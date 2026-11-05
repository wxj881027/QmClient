# 服务仓库索引

`qmclient_scripts/` 下维护中心服、语音服与称号服务源码。中心服采用直接维护与上线流程；其他服务保留各自独立 Git 仓库。
主仓库通过 `.gitignore` 排除这些目录，不会把它们并入 QmClient 主仓库历史。

## 仓库一览

| 目录 | 仓库 | 语言 | 运行形态 | 用途 |
| --- | --- | --- | --- | --- |
| `qmclient_center_server/` | 直接维护（[历史源码仓库](https://github.com/wxj881027/qmclient-center-server)） | Node.js | HTTP `:8080` + WebSocket | 中心服务：识别同步、实时通道、编辑器协作、称号、新闻、游玩时长、开发者名牌 |
| `qmclient_voice_server/` | [wxj881027/qmclient-voicesrv](https://github.com/wxj881027/qmclient-voicesrv) | Rust | HTTP/WebSocket `:9987` | 语音中继（`RV01` 协议）与身份/在线同步 |
| `qmclient_titles/` | [wxj881027/qmclient-titles](https://github.com/wxj881027/qmclient-titles) | Node.js | HTTP `:19092` | 赞助者称号：兑换码、资料绑定、在场样式分配 |

## 部署约定

各服务的 `deploy/` 下提供 systemd 单元与 nginx 片段，路径与账号按通用约定编写（`/opt/qmclient-*` + 专用系统账号），
部署到具体主机时按实际情况调整。密钥通过 `EnvironmentFile` 或受保护的配置文件注入，仓库内只保留占位示例。

## 中心服更新规则

- `qmclient_center_server/` 直接维护源码，不采用 Git 提交、推送或 PR 流程。目录内若有历史 Git 元数据，不把它作为交付入口。
- 修改完成后立即上线。以当前线上代码为基线，只部署本次修改，不覆盖本地或线上其他任务的改动。
- 上线前在独立中心服备份目录保存源码、相关代理配置和受保护的运行配置；该目录只保留最近两次完整备份。
- 先核对语法和代理配置，再重启受影响服务并核对健康状态；失败时恢复本次备份，不把未启动的候选版本当作已上线。
- 密码、会话、玩家数据和密钥不进入主仓库或文档；凭据只部署到受保护的配置文件。

## 其他服务更新

语音服与称号服务仍在各自源码目录通过 Git 更新，遵循各仓库的授权与交付规则。
