"use strict";

const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");

const TTL = 15;
const Hash = (Value) => crypto.createHash("sha256").update(Value).digest("hex");
const Result = (statusCode, response) => ({ statusCode, response });
const Fail = (Code, Error) => Result(Code, { ok: false, error: Error });
const IsToken = (Value) => typeof Value === "string" && /^[a-f0-9]{64}$/.test(Value);
const IsText = (Value, Limit) => typeof Value === "string" && Value.length > 0 && Buffer.byteLength(Value, "utf8") <= Limit && !/[\p{C}\p{Zl}\p{Zp}]/u.test(Value);

function ValidateTitle(Value)
{
	if(!IsText(Value, 48) || Value !== Value.trim() || /[\[\]]/.test(Value))
		return false;
	return Array.from(Value).reduce((Count, Char) => Count + (Char.codePointAt(0) < 128 ? 1 : 2), 0) <= 12;
}

function IssueTitleCode(Directory, Label)
{
	fs.mkdirSync(path.join(Directory, "codes"), { recursive: true, mode: 0o700 });
	const Code = crypto.randomBytes(24).toString("hex");
	fs.writeFileSync(path.join(Directory, "codes", Hash(Code) + ".json"), JSON.stringify({ label: Label }), { flag: "wx", mode: 0o600 });
	return Code;
}

function CreateTitleService({ Directory, NowSec = () => Math.floor(Date.now() / 1000) })
{
	fs.mkdirSync(Directory, { recursive: true, mode: 0o700 });
	const File = path.join(Directory, "titles.json");
	let Data = fs.existsSync(File) ? JSON.parse(fs.readFileSync(File, "utf8")) : { users: {}, redeemed: {} };
	if(!Data.users || !Data.redeemed)
		throw new Error("invalid title database");
	const Sessions = new Map();
	function Save(Next)
	{
		// 先落盘再发布内存状态，失败时不会消耗认证码。
		const Temporary = File + ".tmp";
		const Fd = fs.openSync(Temporary, "w", 0o600);
		try
		{
			fs.writeFileSync(Fd, JSON.stringify(Next));
			fs.fsyncSync(Fd);
		}
		finally
		{
			fs.closeSync(Fd);
		}
		fs.renameSync(Temporary, File);
		Data = Next;
	}
	function Authenticate(Auth)
	{
		const Match = typeof Auth === "string" && /^Bearer ([a-f0-9]{64})$/.exec(Auth);
		const Key = Match && Hash(Match[1]);
		return Key && Data.users[Key] && !Data.users[Key].revoked ? Key : null;
	}
	function ProfileFor(Key)
	{
		const User = Data.users[Key];
		return Result(200, { ok: true, title: User.title, bound_name: User.bound_name });
	}
	function Cleanup()
	{
		for(const [Key, Session] of Sessions)
			if(Session.expires_at <= NowSec())
				Sessions.delete(Key);
	}
	return {
		Redeem(Body)
		{
			if(!Body || typeof Body.code !== "string" || !/^[a-f0-9]{48}$/.test(Body.code) || !IsToken(Body.token))
				return Fail(400, "invalid_code");
			const CodeHash = Hash(Body.code);
			const TokenHash = Hash(Body.token);
			if(Data.redeemed[CodeHash])
				return Data.redeemed[CodeHash] === TokenHash && !Data.users[TokenHash].revoked ? ProfileFor(TokenHash) : Fail(409, "code_used");
			const CodeFile = path.join(Directory, "codes", CodeHash + ".json");
			if(!fs.existsSync(CodeFile))
				return Fail(400, "invalid_code");
			if(Data.users[TokenHash])
				return Fail(409, "already_registered");
			const Code = JSON.parse(fs.readFileSync(CodeFile, "utf8"));
			const Next = JSON.parse(JSON.stringify(Data));
			Next.users[TokenHash] = { label: Code.label, title: "赞助者", bound_name: "", revoked: false };
			Next.redeemed[CodeHash] = TokenHash;
			Save(Next);
			return ProfileFor(TokenHash);
		},
		Profile(Auth)
		{
			const Key = Authenticate(Auth);
			return Key ? ProfileFor(Key) : Fail(401, "invalid_credential");
		},
		Update(Auth, Body)
		{
			const Key = Authenticate(Auth);
			if(!Key)
				return Fail(401, "invalid_credential");
			if(!Body || !ValidateTitle(Body.title))
				return Fail(400, "invalid_title");
			if(Body.bound_name !== "" && !IsText(Body.bound_name, 63))
				return Fail(400, "invalid_name");
			const Next = JSON.parse(JSON.stringify(Data));
			Next.users[Key].title = Body.title;
			Next.users[Key].bound_name = Body.bound_name;
			Save(Next);
			return ProfileFor(Key);
		},
		Report(Auth, Body, Ip)
		{
			const Key = Authenticate(Auth);
			if(!Key)
				return Fail(401, "invalid_credential");
			if(!Body || !IsText(Body.server_address, 128) || !IsText(Body.session_id, 128) ||
				!Array.isArray(Body.players) || Body.players.length > 2 || Body.players.length === 0 || typeof Ip !== "string" || !Ip)
				return Fail(400, "invalid_presence");
			const Ids = new Set();
			for(const Player of Body.players)
			{
				if(!Player || !Number.isInteger(Player.player_id) || Player.player_id < 0 || Player.player_id >= 128 ||
					!IsText(Player.player_name, 63) || typeof Player.dummy !== "boolean" || Ids.has(Player.player_id))
					return Fail(400, "invalid_presence");
				Ids.add(Player.player_id);
			}
			Cleanup();
			const ActiveIps = new Set(Array.from(Sessions.values()).filter((Session) => Session.key === Key).map((Session) => Session.ip));
			if(!ActiveIps.has(Ip) && ActiveIps.size >= 4)
				return Fail(409, "ip_limit");
			const Now = NowSec();
			const Players = Body.players.map((Player) => ({ player_id: Player.player_id, player_name: Player.player_name, dummy: Player.dummy }));
			Sessions.set(Key + ":" + Body.session_id, { key: Key, ip: Ip, server_address: Body.server_address, players: Players, issued_at: Now, expires_at: Now + TTL });
			return Result(200, { ok: true, accepted: Players.filter((Player) => !Data.users[Key].bound_name || Player.player_name === Data.users[Key].bound_name).length });
		},
		List(Server)
		{
			if(!IsText(Server, 128))
				return Fail(400, "invalid_server_address");
			Cleanup();
			const Presences = new Map();
			for(const Session of Sessions.values())
			{
				const User = Data.users[Session.key];
				if(User.revoked || Session.server_address !== Server)
					continue;
				for(const Player of Session.players)
				{
					if(User.bound_name && Player.player_name !== User.bound_name)
						continue;
					Presences.set(Player.player_id + ":" + Player.player_name, { ...Player, title: User.title, server_address: Server, issued_at: Session.issued_at, expires_at: Session.expires_at });
				}
			}
			return Result(200, { server_time: NowSec(), presences: Array.from(Presences.values()) });
		}
	};
}

function RegisterTitleRoutes(App, Service, { CheckRateLimit, ClientIp })
{
	for(const [Method, Route, Handle] of [
		["post", "redeem", (Req) => Service.Redeem(Req.body)],
		["get", "profile", (Req) => Service.Profile(Req.get("authorization"))],
		["post", "profile", (Req) => Service.Update(Req.get("authorization"), Req.body)],
		["post", "presence", (Req) => Service.Report(Req.get("authorization"), Req.body, ClientIp(Req))],
		["get", "presences", (Req) => Service.List(Req.query.server_address)]
	])
	{
		App[Method]("/api/v1/titles/" + Route, (Req, Res) => {
			Res.set("Cache-Control", "no-store");
			if(!CheckRateLimit(ClientIp(Req)))
				return Res.status(429).json({ ok: false, error: "rate_limited" });
			try
			{
				const Reply = Handle(Req);
				Res.status(Reply.statusCode).json(Reply.response);
			}
			catch
			{
				Res.status(503).json({ ok: false, error: "storage_unavailable" });
			}
		});
	}
}

module.exports = { CreateTitleService, IssueTitleCode, RegisterTitleRoutes, ValidateTitle };
