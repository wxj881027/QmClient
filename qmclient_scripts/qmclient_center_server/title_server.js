"use strict";

const path = require("node:path");
const express = require("express");
const { CreateTitleService, RegisterTitleRoutes } = require("./title_auth");

const App = express();
// 仅监听回环地址，来源 IP 由本机 Nginx 覆盖传入。
App.set("trust proxy", "loopback");
App.use(express.json({ limit: "16kb" }));
const Rates = new Map();
const Service = CreateTitleService({ Directory: process.env.TITLE_DATA_DIR || path.join(__dirname, "title_data") });
RegisterTitleRoutes(App, Service, {
	ClientIp: (Req) => Req.ip,
	CheckRateLimit(Ip)
	{
		const Now = Date.now();
		for(const [Key, Value] of Rates)
			if(Value.until <= Now)
				Rates.delete(Key);
		const Entry = Rates.get(Ip) || { until: Now + 60000, count: 0 };
		Rates.set(Ip, Entry);
		return ++Entry.count <= 120;
	}
});
App.get("/healthz", (_Req, Res) => Res.json({ ok: true }));
App.listen(Number(process.env.PORT || 19092), "127.0.0.1");
