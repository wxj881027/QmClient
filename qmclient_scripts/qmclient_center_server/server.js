// 请抬头享受阳光｜日子很好 我很我---------致咩子
"use strict";

const crypto = require("node:crypto");
const https = require("node:https");
const fs = require("node:fs");
const path = require("node:path");
const express = require("express");

const {
	CreateDeveloperPresenceService,
	RegisterDeveloperPresenceRoutes
} = require("./developer_auth");

const { CreateTitleService, RegisterTitleRoutes } = require("./title_auth");

const app = express();
const DefaultJsonParser = express.json({ limit: "32kb" });
const EditorCollabJsonParser = express.json({ limit: "32mb" });
app.use((req, res, next) => {
	if(req.path.startsWith("/editor/collab/"))
		return EditorCollabJsonParser(req, res, next);
	return DefaultJsonParser(req, res, next);
});

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
const DEVELOPER_CREDENTIALS_FILE = process.env.DEVELOPER_CREDENTIALS_FILE || "";
const PLAYTIME_DB_FILE = process.env.PLAYTIME_DB_FILE || path.join(__dirname, "playtime_db.json");
const CLIENT_RELEASE_OWNER = process.env.CLIENT_RELEASE_OWNER || "wxj881027";
const CLIENT_RELEASE_REPO = process.env.CLIENT_RELEASE_REPO || "QmClient";
const CLIENT_VERSION_FALLBACK = NormalizeClientVersion(process.env.CLIENT_LATEST_VERSION || "2.36.0");
const CLIENT_VERSION_CACHE_TTL_SEC = Number(process.env.CLIENT_VERSION_CACHE_TTL_SEC || 300);
const CLIENT_VERSION_RETRY_DELAY_SEC = Number(process.env.CLIENT_VERSION_RETRY_DELAY_SEC || 60);
const CLIENT_VERSION_FETCH_TIMEOUT_MS = Number(process.env.CLIENT_VERSION_FETCH_TIMEOUT_MS || 5000);
const CLIENT_RELEASES_API_URL = process.env.CLIENT_RELEASES_API_URL || `https://api.github.com/repos/${CLIENT_RELEASE_OWNER}/${CLIENT_RELEASE_REPO}/releases/latest`;
const CLIENT_FALLBACK_TAG = process.env.CLIENT_LATEST_TAG || `v${CLIENT_VERSION_FALLBACK}`;
const CLIENT_FALLBACK_RELEASE_URL = process.env.CLIENT_RELEASE_URL || BuildClientReleaseUrl(CLIENT_FALLBACK_TAG);
const MAX_CLIENT_ID_LEN = Number(process.env.MAX_CLIENT_ID_LEN || 64);
const MAX_PLAYER_NAME_LEN = Number(process.env.MAX_PLAYER_NAME_LEN || 32);
const EDITOR_COLLAB_MAX_MEMBERS = 4;
const EDITOR_COLLAB_MEMBER_TTL_SEC = Number(process.env.EDITOR_COLLAB_MEMBER_TTL_SEC || 45);
const EDITOR_COLLAB_ROOM_TTL_SEC = Number(process.env.EDITOR_COLLAB_ROOM_TTL_SEC || 300);
const EDITOR_COLLAB_MAX_MAP_BASE64_LEN = Number(process.env.EDITOR_COLLAB_MAX_MAP_BASE64_LEN || 24 * 1024 * 1024);
const SECONDS_PER_MINUTE = 60;
const SECONDS_PER_HOUR = 60 * SECONDS_PER_MINUTE;
const SECONDS_PER_DAY = 24 * SECONDS_PER_HOUR;
const SECONDS_PER_MONTH = 30 * SECONDS_PER_DAY;
const SECONDS_PER_YEAR = 365 * SECONDS_PER_DAY;

if(TRUST_PROXY)
{
	app.set("trust proxy", "loopback");
}

const g_Tokens = new Map();
const g_Users = new Map();
const g_Rate = new Map();
const g_EditorCollabRooms = new Map();
const g_Playtime = new Map();
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

function SafeInt(Value, DefaultValue = 0)
{
	const NumberValue = Number(Value);
	if(!Number.isFinite(NumberValue))
	{
		return DefaultValue;
	}
	return Math.max(0, Math.floor(NumberValue));
}

function SavePlaytimeStore()
{
	const Clients = {};
	for(const [ClientId, Record] of g_Playtime.entries())
	{
		Clients[ClientId] = {
			client_id: ClientId,
			player_name: Record.player_name,
			total_seconds: SafeInt(Record.total_seconds),
			active_since: SafeInt(Record.active_since),
			created_at: SafeInt(Record.created_at),
			updated_at: SafeInt(Record.updated_at),
			last_start_at: SafeInt(Record.last_start_at),
			last_stop_at: SafeInt(Record.last_stop_at),
			last_seen_at: SafeInt(Record.last_seen_at)
		};
	}

	const Dir = path.dirname(PLAYTIME_DB_FILE);
	fs.mkdirSync(Dir, { recursive: true });

	const TmpFile = `${PLAYTIME_DB_FILE}.tmp`;
	const Payload = JSON.stringify({
		version: 1,
		updated_at: NowSec(),
		clients: Clients
	}, null, 2);
	fs.writeFileSync(TmpFile, Payload);
	fs.renameSync(TmpFile, PLAYTIME_DB_FILE);
}

function LoadPlaytimeStore()
{
	try
	{
		if(!fs.existsSync(PLAYTIME_DB_FILE))
		{
			return;
		}

		const Parsed = JSON.parse(fs.readFileSync(PLAYTIME_DB_FILE, "utf8"));
		const Clients = Parsed && typeof Parsed === "object" ? Parsed.clients : null;
		if(!Clients || typeof Clients !== "object")
		{
			return;
		}

		for(const [ClientId, Record] of Object.entries(Clients))
		{
			if(!IsValidClientId(ClientId) || !Record || typeof Record !== "object")
			{
				continue;
			}

			g_Playtime.set(ClientId, {
				client_id: ClientId,
				player_name: typeof Record.player_name === "string" ? Record.player_name : "",
				total_seconds: SafeInt(Record.total_seconds),
				active_since: SafeInt(Record.active_since),
				created_at: SafeInt(Record.created_at),
				updated_at: SafeInt(Record.updated_at),
				last_start_at: SafeInt(Record.last_start_at),
				last_stop_at: SafeInt(Record.last_stop_at),
				last_seen_at: SafeInt(Record.last_seen_at)
			});
		}
	}
	catch(Error)
	{
		console.error(`[qmclient-center-server] failed to load playtime db: ${Error.message}`);
	}
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

	for(const [RoomCode, Room] of g_EditorCollabRooms.entries())
	{
		for(const [ClientId, Member] of Room.members.entries())
		{
			if(Member.expiresAt <= Now)
			{
				Room.members.delete(ClientId);
			}
		}

		if(Room.members.size === 0 && Room.updatedAt + EDITOR_COLLAB_ROOM_TTL_SEC <= Now)
		{
			g_EditorCollabRooms.delete(RoomCode);
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

const g_DeveloperPresenceService = CreateDeveloperPresenceService({
	CredentialsFilePath: DEVELOPER_CREDENTIALS_FILE
});
RegisterDeveloperPresenceRoutes(app, g_DeveloperPresenceService, {
	CheckRateLimit: (req) => CheckRateLimit(ClientIp(req))
});

const g_TitleService = CreateTitleService({ Directory: process.env.TITLE_DATA_DIR || path.join(__dirname, "title_data") });
RegisterTitleRoutes(app, g_TitleService, { CheckRateLimit, ClientIp });

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

function IsValidClientId(ClientId)
{
	return typeof ClientId === "string" &&
		ClientId.length >= 8 &&
		ClientId.length <= MAX_CLIENT_ID_LEN &&
		/^[A-Za-z0-9_-]+$/.test(ClientId);
}

function NormalizePlayerName(PlayerName)
{
	if(typeof PlayerName !== "string")
	{
		return "";
	}
	return PlayerName.trim().slice(0, MAX_PLAYER_NAME_LEN);
}

function NormalizeClientType(ClientType, DefaultType = "qm")
{
	if(typeof ClientType !== "string")
		return DefaultType;

	const Normalized = ClientType.trim().toLowerCase();
	if(Normalized === "arg" || Normalized === "arghena")
		return "arg";
	if(Normalized === "qm" || Normalized === "qmclient" || Normalized === "q1meng")
		return "qm";
	return DefaultType;
}

function NormalizeOptionalClientId(ClientId)
{
	return IsValidClientId(ClientId) ? ClientId : "";
}

function IsValidPlayerId(PlayerId)
{
	return Number.isInteger(PlayerId) && PlayerId >= 0 && PlayerId < 64;
}

function NormalizeOptionalPlayerId(PlayerId)
{
	if(PlayerId === undefined || PlayerId === null || PlayerId === "")
		return null;
	const Value = Number(PlayerId);
	return IsValidPlayerId(Value) ? Value : null;
}

function GetOrCreatePlaytimeRecord(ClientId, Now)
{
	const Existing = g_Playtime.get(ClientId);
	if(Existing)
	{
		return Existing;
	}

	const Record = {
		client_id: ClientId,
		player_name: "",
		total_seconds: 0,
		active_since: 0,
		created_at: Now,
		updated_at: Now,
		last_start_at: 0,
		last_stop_at: 0,
		last_seen_at: Now
	};
	g_Playtime.set(ClientId, Record);
	return Record;
}

function GetPlaytimeSeconds(Record, Now)
{
	let TotalSeconds = SafeInt(Record.total_seconds);
	const ActiveSince = SafeInt(Record.active_since);
	if(ActiveSince > 0 && ActiveSince <= Now)
	{
		TotalSeconds += Now - ActiveSince;
	}
	return TotalSeconds;
}

function FormatDurationParts(TotalSeconds)
{
	let Remaining = SafeInt(TotalSeconds);

	const Years = Math.floor(Remaining / SECONDS_PER_YEAR);
	Remaining -= Years * SECONDS_PER_YEAR;

	const Months = Math.floor(Remaining / SECONDS_PER_MONTH);
	Remaining -= Months * SECONDS_PER_MONTH;

	const Days = Math.floor(Remaining / SECONDS_PER_DAY);
	Remaining -= Days * SECONDS_PER_DAY;

	const Hours = Math.floor(Remaining / SECONDS_PER_HOUR);
	Remaining -= Hours * SECONDS_PER_HOUR;

	const Minutes = Math.floor(Remaining / SECONDS_PER_MINUTE);
	Remaining -= Minutes * SECONDS_PER_MINUTE;

	const Seconds = Remaining;

	return {
		years: Years,
		months: Months,
		days: Days,
		hours: Hours,
		minutes: Minutes,
		seconds: Seconds
	};
}

function FormatDurationText(Parts)
{
	const Segments = [];
	if(Parts.years > 0)
		Segments.push(`${Parts.years}年`);
	if(Parts.months > 0)
		Segments.push(`${Parts.months}月`);
	if(Parts.days > 0)
		Segments.push(`${Parts.days}天`);
	if(Parts.hours > 0)
		Segments.push(`${Parts.hours}小时`);
	if(Parts.minutes > 0)
		Segments.push(`${Parts.minutes}分钟`);
	if(Parts.seconds > 0 || Segments.length === 0)
		Segments.push(`${Parts.seconds}秒`);
	return Segments.join("");
}

function PlaytimeSummary(Record, Now)
{
	const TotalSeconds = Record ? GetPlaytimeSeconds(Record, Now) : 0;
	const Parts = FormatDurationParts(TotalSeconds);
	return {
		client_id: Record ? Record.client_id : "",
		player_name: Record ? Record.player_name : "",
		running: !!(Record && SafeInt(Record.active_since) > 0),
		total_seconds: TotalSeconds,
		total_time: Parts,
		total_time_text: FormatDurationText(Parts),
		last_start_at: Record ? SafeInt(Record.last_start_at) : 0,
		last_stop_at: Record ? SafeInt(Record.last_stop_at) : 0,
		last_seen_at: Record ? SafeInt(Record.last_seen_at) : 0
	};
}

function NewEditorCollabRoomCode()
{
	const Alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
	for(let Attempts = 0; Attempts < 32; ++Attempts)
	{
		let Code = "";
		const Bytes = crypto.randomBytes(6);
		for(let i = 0; i < 6; ++i)
			Code += Alphabet[Bytes[i] % Alphabet.length];
		if(!g_EditorCollabRooms.has(Code))
			return Code;
	}
	return crypto.randomBytes(4).toString("hex").toUpperCase();
}

function NormalizeEditorCollabRoomCode(RoomCode)
{
	if(typeof RoomCode !== "string")
		return "";
	const Normalized = RoomCode.trim().toUpperCase();
	return /^[A-Z0-9]{4,12}$/.test(Normalized) ? Normalized : "";
}

function NormalizeEditorCollabClientId(ClientId)
{
	if(typeof ClientId !== "string")
		return "";
	const Normalized = ClientId.trim();
	return /^[A-Za-z0-9_-]{8,64}$/.test(Normalized) ? Normalized : "";
}

function NormalizeEditorCollabMapBase64(MapBase64)
{
	if(typeof MapBase64 !== "string")
		return "";
	const Normalized = MapBase64.trim();
	if(Normalized.length === 0 || Normalized.length > EDITOR_COLLAB_MAX_MAP_BASE64_LEN)
		return "";
	return /^[A-Za-z0-9+/=]+$/.test(Normalized) ? Normalized : "";
}

function TouchEditorCollabMember(Room, ClientId, PlayerName)
{
	const Now = NowSec();
	Room.members.set(ClientId, {
		client_id: ClientId,
		player_name: NormalizePlayerName(PlayerName),
		updated_at: Now,
		expiresAt: Now + EDITOR_COLLAB_MEMBER_TTL_SEC
	});
	Room.updatedAt = Now;
}

function EditorCollabMembersJson(Room)
{
	return Array.from(Room.members.values()).map((Member) => ({
		client_id: Member.client_id,
		player_name: Member.player_name,
		updated_at: Member.updated_at
	}));
}

function SendEditorCollabRoom(res, Room, Extra = {})
{
	res.json({
		ok: true,
		room_code: Room.code,
		revision: Room.revision,
		member_count: Room.members.size,
		max_members: EDITOR_COLLAB_MAX_MEMBERS,
		members: EditorCollabMembersJson(Room),
		...Extra
	});
}

app.get("/healthz", (_req, res) => {
	res.json({ ok: true, ts: NowSec() });
});

app.post("/editor/collab/create", (req, res) => {
	Cleanup();
	const Body = req.body || {};
	const ClientId = NormalizeEditorCollabClientId(Body.client_id);
	if(ClientId === "")
	{
		res.status(400).json({ ok: false, error: "invalid_client_id", message: "客户端标识无效" });
		return;
	}

	const RoomCode = NewEditorCollabRoomCode();
	const Now = NowSec();
	const Room = {
		code: RoomCode,
		revision: 0,
		map_base64: "",
		createdAt: Now,
		updatedAt: Now,
		members: new Map()
	};
	TouchEditorCollabMember(Room, ClientId, Body.player_name);
	g_EditorCollabRooms.set(RoomCode, Room);
	SendEditorCollabRoom(res, Room);
});

app.post("/editor/collab/join", (req, res) => {
	Cleanup();
	const Body = req.body || {};
	const RoomCode = NormalizeEditorCollabRoomCode(Body.room_code);
	const ClientId = NormalizeEditorCollabClientId(Body.client_id);
	const Room = g_EditorCollabRooms.get(RoomCode);
	if(RoomCode === "" || !Room)
	{
		res.status(404).json({ ok: false, error: "invalid_room", message: "房间码无效" });
		return;
	}
	if(ClientId === "")
	{
		res.status(400).json({ ok: false, error: "invalid_client_id", message: "客户端标识无效" });
		return;
	}
	if(!Room.members.has(ClientId) && Room.members.size >= EDITOR_COLLAB_MAX_MEMBERS)
	{
		res.status(409).json({ ok: false, error: "room_full", message: "房间已满，最多支持 4 人协作" });
		return;
	}

	TouchEditorCollabMember(Room, ClientId, Body.player_name);
	SendEditorCollabRoom(res, Room, Room.map_base64 ? { map_base64: Room.map_base64 } : {});
});

app.post("/editor/collab/leave", (req, res) => {
	Cleanup();
	const Body = req.body || {};
	const RoomCode = NormalizeEditorCollabRoomCode(Body.room_code);
	const ClientId = NormalizeEditorCollabClientId(Body.client_id);
	const Room = g_EditorCollabRooms.get(RoomCode);
	if(!Room)
	{
		res.status(404).json({ ok: false, error: "invalid_room", message: "房间码无效" });
		return;
	}
	if(ClientId !== "")
		Room.members.delete(ClientId);
	Room.updatedAt = NowSec();
	res.json({ ok: true, room_code: RoomCode, member_count: Room.members.size });
});

app.post("/editor/collab/push", (req, res) => {
	Cleanup();
	const Body = req.body || {};
	const RoomCode = NormalizeEditorCollabRoomCode(Body.room_code);
	const ClientId = NormalizeEditorCollabClientId(Body.client_id);
	const Room = g_EditorCollabRooms.get(RoomCode);
	if(!Room)
	{
		res.status(404).json({ ok: false, error: "invalid_room", message: "房间码无效" });
		return;
	}
	if(ClientId === "" || !Room.members.has(ClientId))
	{
		res.status(403).json({ ok: false, error: "not_in_room", message: "尚未加入该协作房间" });
		return;
	}
	const MapBase64 = NormalizeEditorCollabMapBase64(Body.map_base64);
	if(MapBase64 === "")
	{
		res.status(400).json({ ok: false, error: "invalid_map", message: "地图同步数据无效或过大" });
		return;
	}

	TouchEditorCollabMember(Room, ClientId, Body.player_name);
	Room.map_base64 = MapBase64;
	Room.revision += 1;
	Room.updatedAt = NowSec();
	SendEditorCollabRoom(res, Room);
});

app.get("/editor/collab/pull", (req, res) => {
	Cleanup();
	const RoomCode = NormalizeEditorCollabRoomCode(req.query.room_code);
	const ClientId = NormalizeEditorCollabClientId(req.query.client_id);
	const Since = Number(req.query.since || 0);
	const Room = g_EditorCollabRooms.get(RoomCode);
	if(!Room)
	{
		res.status(404).json({ ok: false, error: "invalid_room", message: "房间码无效" });
		return;
	}
	if(ClientId === "" || !Room.members.has(ClientId))
	{
		res.status(403).json({ ok: false, error: "not_in_room", message: "尚未加入该协作房间" });
		return;
	}

	TouchEditorCollabMember(Room, ClientId, req.query.player_name || "");
	SendEditorCollabRoom(res, Room, Room.map_base64 && Room.revision > Since ? { map_base64: Room.map_base64 } : {});
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
	const ReportClientType = NormalizeClientType(Body.client_type || Body.type);
	const ReportClientId = NormalizeOptionalClientId(Body.client_id || Body.machine_hash);

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
		const PlayerName = NormalizePlayerName(typeof Player.player_name === "string" ? Player.player_name : Player.name);
		const PlayerId = NormalizeOptionalPlayerId(Player.player_id);
		if(PlayerName === "" && PlayerId === null)
			continue;
		const Dummy = !!Player.dummy;
		const FootParticlesEnabled = !!Player.foot_particles_enabled;
		const RemoteParticlesEnabled = !!Player.remote_particles_enabled;
		const VoiceSupported = Player.voice_supported !== false;
		const ClientType = NormalizeClientType(Player.client_type || Player.type, ReportClientType);
		const ClientId = NormalizeOptionalClientId(Player.client_id || Player.machine_hash) || ReportClientId;
		const IdentityKey = PlayerName !== "" ? `name:${PlayerName}` : `id:${PlayerId}`;

		const Key = `${ServerAddress}|${IdentityKey}`;
		g_Users.set(Key, {
			server_address: ServerAddress,
			player_name: PlayerName,
			player_id: PlayerId,
			dummy: Dummy,
			client_type: ClientType,
			client_id: ClientId,
			foot_particles_enabled: FootParticlesEnabled,
			remote_particles_enabled: RemoteParticlesEnabled,
			voice_supported: VoiceSupported,
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
	...(User.player_name ? { player_name: User.player_name } : {}),
	...(IsValidPlayerId(User.player_id) ? { player_id: User.player_id } : {}),
	dummy: !!User.dummy,
	client_type: User.client_type,
		type: User.client_type,
		client_id: User.client_id || "",
		qid: User.client_id || "",
		foot_particles_enabled: !!User.foot_particles_enabled,
		remote_particles_enabled: !!User.remote_particles_enabled,
		voice_supported: User.voice_supported !== false,
		updated_at: User.updated_at
	}));
	res.json({ users: Users });
});

app.post("/playtime/start", (req, res) => {
	Cleanup();
	const Ip = ClientIp(req);
	if(!CheckRateLimit(Ip))
	{
		res.status(429).json({ ok: false, error: "rate_limited" });
		return;
	}

	const Body = req.body || {};
	const ClientId = Body.client_id;
	const PlayerName = NormalizePlayerName(Body.player_name);

	if(!IsValidClientId(ClientId))
	{
		res.status(400).json({ ok: false, error: "invalid_client_id" });
		return;
	}

	const Now = NowSec();
	const Record = GetOrCreatePlaytimeRecord(ClientId, Now);
	const WasRunning = SafeInt(Record.active_since) > 0;

	if(PlayerName !== "")
	{
		Record.player_name = PlayerName;
	}
	Record.updated_at = Now;
	Record.last_seen_at = Now;

	if(!WasRunning)
	{
		Record.active_since = Now;
		Record.last_start_at = Now;
	}

	SavePlaytimeStore();
	res.status(200).json({
		ok: true,
		action: "start",
		already_running: WasRunning,
		...PlaytimeSummary(Record, Now)
	});
});

app.post("/playtime/stop", (req, res) => {
	Cleanup();
	const Ip = ClientIp(req);
	if(!CheckRateLimit(Ip))
	{
		res.status(429).json({ ok: false, error: "rate_limited" });
		return;
	}

	const Body = req.body || {};
	const ClientId = Body.client_id;
	const PlayerName = NormalizePlayerName(Body.player_name);
	const RequestedStopAt = Number(Body.stop_at);

	if(!IsValidClientId(ClientId))
	{
		res.status(400).json({ ok: false, error: "invalid_client_id" });
		return;
	}

	const Now = NowSec();
	const Record = GetOrCreatePlaytimeRecord(ClientId, Now);
	const WasRunning = SafeInt(Record.active_since) > 0;
	let EffectiveStopAt = Now;
	if(Number.isFinite(RequestedStopAt))
	{
		EffectiveStopAt = Math.floor(RequestedStopAt);
		if(EffectiveStopAt < 0)
			EffectiveStopAt = 0;
		if(EffectiveStopAt > Now)
			EffectiveStopAt = Now;
	}

	if(PlayerName !== "")
	{
		Record.player_name = PlayerName;
	}
	if(WasRunning)
	{
		const ActiveSince = SafeInt(Record.active_since);
		if(EffectiveStopAt < ActiveSince)
			EffectiveStopAt = ActiveSince;
		Record.total_seconds = SafeInt(Record.total_seconds) + (EffectiveStopAt - ActiveSince);
		Record.active_since = 0;
		Record.last_stop_at = EffectiveStopAt;
	}
	Record.updated_at = Now;
	Record.last_seen_at = Now;

	SavePlaytimeStore();
	res.status(200).json({
		ok: true,
		action: "stop",
		was_running: WasRunning,
		...PlaytimeSummary(Record, Now)
	});
});

app.post("/playtime/query", (req, res) => {
	Cleanup();
	const Ip = ClientIp(req);
	if(!CheckRateLimit(Ip))
	{
		res.status(429).json({ ok: false, error: "rate_limited" });
		return;
	}

	const Body = req.body || {};
	const ClientId = Body.client_id;
	const PlayerName = NormalizePlayerName(Body.player_name);

	if(!IsValidClientId(ClientId))
	{
		res.status(400).json({ ok: false, error: "invalid_client_id" });
		return;
	}

	const Now = NowSec();
	const Record = g_Playtime.get(ClientId) || null;
	if(Record)
	{
		if(PlayerName !== "")
		{
			Record.player_name = PlayerName;
		}
		Record.updated_at = Now;
		Record.last_seen_at = Now;
	}

	res.status(200).json({
		ok: true,
		action: "query",
		...PlaytimeSummary(Record, Now)
	});
});

setInterval(Cleanup, 30 * 1000).unref();
LoadPlaytimeStore();

app.listen(PORT, "0.0.0.0", () => {
	console.log(`[qmclient-center-server] listening on :${PORT}`);
});
