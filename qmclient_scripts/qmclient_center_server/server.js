"use strict";

const crypto = require("node:crypto");
const https = require("node:https");
const express = require("express");

const app = express();
app.use(express.json({ limit: "32kb" }));

const PORT = Number(process.env.PORT || 8080);
const TOKEN_TTL_SEC = Number(process.env.TOKEN_TTL_SEC || 300);
const REPORT_TTL_SEC = Number(process.env.REPORT_TTL_SEC || 90);
const MAX_PLAYERS_PER_REPORT = Number(process.env.MAX_PLAYERS_PER_REPORT || 8);
const MAX_SERVER_ADDRESS_LEN = Number(process.env.MAX_SERVER_ADDRESS_LEN || 128);
const TIME_SKEW_SEC = Number(process.env.TIME_SKEW_SEC || 600);
const REQUIRE_IP_BIND = process.env.REQUIRE_IP_BIND !== "0";
const TRUST_PROXY = process.env.TRUST_PROXY === "1";
const RATE_LIMIT_PER_MIN = Number(process.env.RATE_LIMIT_PER_MIN || 120);
const AUTH_SECRET = process.env.AUTH_SECRET || crypto.randomBytes(32).toString("hex");
const CLIENT_RELEASE_OWNER = process.env.CLIENT_RELEASE_OWNER || "wxj881027";
const CLIENT_RELEASE_REPO = process.env.CLIENT_RELEASE_REPO || "QmClient";
const CLIENT_VERSION_FALLBACK = NormalizeClientVersion(process.env.CLIENT_LATEST_VERSION || "2.35.2");
const CLIENT_VERSION_CACHE_TTL_SEC = Number(process.env.CLIENT_VERSION_CACHE_TTL_SEC || 300);
const CLIENT_VERSION_RETRY_DELAY_SEC = Number(process.env.CLIENT_VERSION_RETRY_DELAY_SEC || 60);
const CLIENT_VERSION_FETCH_TIMEOUT_MS = Number(process.env.CLIENT_VERSION_FETCH_TIMEOUT_MS || 5000);
const CLIENT_RELEASES_API_URL = process.env.CLIENT_RELEASES_API_URL || `https://api.github.com/repos/${CLIENT_RELEASE_OWNER}/${CLIENT_RELEASE_REPO}/releases/latest`;
const CLIENT_FALLBACK_TAG = process.env.CLIENT_LATEST_TAG || `v${CLIENT_VERSION_FALLBACK}`;
const CLIENT_FALLBACK_RELEASE_URL = process.env.CLIENT_RELEASE_URL || BuildClientReleaseUrl(CLIENT_FALLBACK_TAG);

if(TRUST_PROXY)
{
	app.set("trust proxy", true);
}

const g_Tokens = new Map();
const g_Users = new Map();
const g_Rate = new Map();
const g_ClientVersionCache = {
	version: CLIENT_VERSION_FALLBACK,
	tag: CLIENT_FALLBACK_TAG,
	releaseUrl: CLIENT_FALLBACK_RELEASE_URL,
	source: "fallback",
	fetchedAt: 0,
	expiresAt: 0,
	lastError: "",
	pending: null
};

function NowSec()
{
	return Math.floor(Date.now() / 1000);
}

function ClientIp(req)
{
	return req.ip || req.socket.remoteAddress || "unknown";
}

function NormalizeClientVersion(Value)
{
	if(typeof Value !== "string")
		return "";

	const Trimmed = Value.trim();
	if(Trimmed.startsWith("v") || Trimmed.startsWith("V"))
		return Trimmed.slice(1).trim();
	return Trimmed;
}

function BuildClientReleaseUrl(Tag)
{
	return `https://github.com/${CLIENT_RELEASE_OWNER}/${CLIENT_RELEASE_REPO}/releases/tag/${Tag}`;
}

function IsClientVersionCacheFresh()
{
	return g_ClientVersionCache.version !== "" && g_ClientVersionCache.expiresAt > NowSec();
}

function ReadJsonUrl(Url, TimeoutMs)
{
	return new Promise((resolve, reject) => {
		const Request = https.get(Url, {
			headers: {
				"Accept": "application/vnd.github+json",
				"User-Agent": "QmClient-Center-Server"
			},
			timeout: TimeoutMs
		}, (Response) => {
			let Body = "";
			Response.setEncoding("utf8");
			Response.on("data", (Chunk) => {
				Body += Chunk;
				if(Body.length > 128 * 1024)
				{
					Request.destroy(new Error("response_too_large"));
				}
			});
			Response.on("end", () => {
				if(Response.statusCode < 200 || Response.statusCode >= 300)
				{
					reject(new Error(`http_${Response.statusCode}`));
					return;
				}

				try
				{
					resolve(JSON.parse(Body));
				}
				catch(Error)
				{
					reject(Error);
				}
			});
		});

		Request.on("timeout", () => {
			Request.destroy(new Error("timeout"));
		});
		Request.on("error", reject);
	});
}

async function RefreshClientVersionCache()
{
	if(IsClientVersionCacheFresh())
		return g_ClientVersionCache;
	if(g_ClientVersionCache.pending)
		return g_ClientVersionCache.pending;

	g_ClientVersionCache.pending = (async () => {
		const Now = NowSec();
		try
		{
			const Release = await ReadJsonUrl(CLIENT_RELEASES_API_URL, CLIENT_VERSION_FETCH_TIMEOUT_MS);
			const Tag = typeof Release.tag_name === "string" ? Release.tag_name.trim() : "";
			const Version = NormalizeClientVersion(Tag);
			if(Version === "")
				throw new Error("missing_tag_name");

			g_ClientVersionCache.version = Version;
			g_ClientVersionCache.tag = Tag;
			g_ClientVersionCache.releaseUrl = typeof Release.html_url === "string" && Release.html_url !== "" ? Release.html_url : BuildClientReleaseUrl(Tag);
			g_ClientVersionCache.source = "github";
			g_ClientVersionCache.fetchedAt = Now;
			g_ClientVersionCache.expiresAt = Now + CLIENT_VERSION_CACHE_TTL_SEC;
			g_ClientVersionCache.lastError = "";
		}
		catch(Error)
		{
			g_ClientVersionCache.lastError = Error && Error.message ? Error.message : String(Error);
			g_ClientVersionCache.expiresAt = Now + CLIENT_VERSION_RETRY_DELAY_SEC;
		}
		finally
		{
			g_ClientVersionCache.pending = null;
		}
		return g_ClientVersionCache;
	})();

	return g_ClientVersionCache.pending;
}

function Cleanup()
{
	const Now = NowSec();

	for(const [Token, TokenEntry] of g_Tokens.entries())
	{
		if(TokenEntry.expiresAt <= Now)
		{
			g_Tokens.delete(Token);
		}
	}

	for(const [Key, User] of g_Users.entries())
	{
		if(User.expiresAt <= Now)
		{
			g_Users.delete(Key);
		}
	}

	for(const [Ip, Rate] of g_Rate.entries())
	{
		if(Rate.windowStart + 60 <= Now)
		{
			g_Rate.delete(Ip);
		}
	}
}

function CheckRateLimit(Ip)
{
	const Now = NowSec();
	const Entry = g_Rate.get(Ip);
	if(!Entry || Entry.windowStart + 60 <= Now)
	{
		g_Rate.set(Ip, { windowStart: Now, count: 1 });
		return true;
	}
	if(Entry.count >= RATE_LIMIT_PER_MIN)
	{
		return false;
	}
	Entry.count += 1;
	return true;
}

function NewToken(Ip)
{
	const Nonce = crypto.randomBytes(12).toString("base64url");
	const ExpiresAt = NowSec() + TOKEN_TTL_SEC;
	const Payload = `${Nonce}.${ExpiresAt}`;
	const Sig = crypto.createHmac("sha256", AUTH_SECRET).update(Payload).digest("base64url");
	const Token = `${Payload}.${Sig}`;
	g_Tokens.set(Token, { ip: Ip, expiresAt: ExpiresAt });
	return { token: Token, expiresAt: ExpiresAt };
}

function VerifyToken(Token, Ip)
{
	if(typeof Token !== "string" || Token.length < 24)
	{
		return false;
	}

	const Entry = g_Tokens.get(Token);
	if(!Entry)
	{
		return false;
	}

	const Now = NowSec();
	if(Entry.expiresAt <= Now)
	{
		g_Tokens.delete(Token);
		return false;
	}

	if(REQUIRE_IP_BIND && Entry.ip !== Ip)
	{
		return false;
	}

	return true;
}

function IsValidServerAddress(ServerAddress)
{
	if(typeof ServerAddress !== "string")
	{
		return false;
	}
	if(ServerAddress.length === 0 || ServerAddress.length > MAX_SERVER_ADDRESS_LEN)
	{
		return false;
	}
	return true;
}

function IsValidPlayerId(PlayerId)
{
	return Number.isInteger(PlayerId) && PlayerId >= 0 && PlayerId < 64;
}

app.get("/healthz", (_req, res) => {
	res.json({ ok: true, ts: NowSec() });
});

app.get("/client/version", async (req, res) => {
	const CurrentVersion = NormalizeClientVersion(req.query.current || "");
	const VersionInfo = await RefreshClientVersionCache();
	res.json({
		ok: true,
		version: VersionInfo.version,
		latest_version: VersionInfo.version,
		latest_tag: VersionInfo.tag,
		release_url: VersionInfo.releaseUrl,
		current_version: CurrentVersion,
		up_to_date: CurrentVersion !== "" && CurrentVersion === VersionInfo.version,
		cache_source: VersionInfo.source,
		cache_expires_at: VersionInfo.expiresAt,
		last_error: VersionInfo.lastError,
		update_message: "当前版本不是最新版，请前往 QQ 群更新最新版"
	});
});

app.get("/token", (req, res) => {
	Cleanup();
	const Ip = ClientIp(req);
	if(!CheckRateLimit(Ip))
	{
		res.status(429).json({ ok: false, error: "rate_limited" });
		return;
	}

	const TokenInfo = NewToken(Ip);
	res.json({
		auth_token: TokenInfo.token,
		expires_in: TOKEN_TTL_SEC
	});
});

app.post("/report", (req, res) => {
	Cleanup();
	const Ip = ClientIp(req);
	if(!CheckRateLimit(Ip))
	{
		res.status(429).json({ ok: false, error: "rate_limited" });
		return;
	}

	const Body = req.body || {};
	const ServerAddress = Body.server_address;
	const Token = Body.auth_token;
	const Timestamp = Number(Body.timestamp);
	const Players = Array.isArray(Body.players) ? Body.players : [];

	if(!IsValidServerAddress(ServerAddress))
	{
		res.status(400).json({ ok: false, error: "invalid_server_address" });
		return;
	}

	if(!VerifyToken(Token, Ip))
	{
		res.status(401).json({ ok: false, error: "invalid_auth_token" });
		return;
	}

	const Now = NowSec();
	if(!Number.isFinite(Timestamp) || Math.abs(Now - Math.floor(Timestamp)) > TIME_SKEW_SEC)
	{
		res.status(400).json({ ok: false, error: "invalid_timestamp" });
		return;
	}

	if(Players.length > MAX_PLAYERS_PER_REPORT)
	{
		res.status(400).json({ ok: false, error: "too_many_players" });
		return;
	}

	let Accepted = 0;
	for(const Player of Players)
	{
		if(!Player || typeof Player !== "object")
			continue;
		const PlayerId = Number(Player.player_id);
		if(!IsValidPlayerId(PlayerId))
			continue;

		const Key = `${ServerAddress}|${PlayerId}`;
		g_Users.set(Key, {
			server_address: ServerAddress,
			player_id: PlayerId,
			updated_at: Now,
			expiresAt: Now + REPORT_TTL_SEC
		});
		Accepted += 1;
	}

	res.json({ ok: true, accepted: Accepted });
});

app.get("/users.json", (_req, res) => {
	Cleanup();
	const Users = Array.from(g_Users.values()).map((User) => ({
		server_address: User.server_address,
		player_id: User.player_id,
		updated_at: User.updated_at
	}));
	res.json({ users: Users });
});

setInterval(Cleanup, 30 * 1000).unref();

app.listen(PORT, "0.0.0.0", () => {
	console.log(`[qmclient-center-server] listening on :${PORT}`);
});
