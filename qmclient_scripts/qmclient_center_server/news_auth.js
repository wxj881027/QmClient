// 请抬头享受阳光｜日子很好 我很我---------致咩子
"use strict";

// 「新功能」广播：公开读取 + 开发者凭据发布。
// 内容只存一份 Markdown（当前只发中文），客户端自行渲染受限子集。

const fs = require("node:fs");
const path = require("node:path");

const DEFAULT_MAX_MARKDOWN_BYTES = 16 * 1024;
const Result = (statusCode, response) => ({ statusCode, response });
const Fail = (Code, Error) => Result(Code, { ok: false, error: Error });

function NormalizeMarkdown(Value)
{
	if(typeof Value !== "string")
		return null;
	const Normalized = Value.replace(/\r\n?/g, "\n");
	// 只允许换行与制表符参与排版，其余控制字符一律拒绝，避免渲染层拿到异常输入。
	if(/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/.test(Normalized))
		return null;
	return Normalized;
}

function CreateNewsService({
	Directory,
	Authenticate = () => null,
	CanPublish = () => false,
	MaxMarkdownBytes = DEFAULT_MAX_MARKDOWN_BYTES,
	NowSec = () => Math.floor(Date.now() / 1000)
})
{
	if(typeof Directory !== "string" || Directory.length === 0)
		throw new Error("news data directory is required");
	fs.mkdirSync(Directory, { recursive: true, mode: 0o700 });

	const File = path.join(Directory, "news.json");
	let Data = fs.existsSync(File) ? JSON.parse(fs.readFileSync(File, "utf8")) : { version: 0, updated_at: 0, markdown: "" };
	if(!Number.isInteger(Data.version) || Data.version < 0 || typeof Data.markdown !== "string" || !Number.isInteger(Data.updated_at))
		throw new Error("invalid news database");

	function Save(Next)
	{
		// 先落盘再发布内存状态：写入失败时旧版本仍然可读。
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

	return {
		Current()
		{
			return Result(200, {
				ok: true,
				version: Data.version,
				updated_at: Data.updated_at,
				markdown: Data.markdown
			});
		},
		Publish(Authorization, Body)
		{
			const Credential = Authenticate(Authorization);
			if(!Credential)
				return Fail(401, "invalid_developer_credential");
			if(!CanPublish(Credential))
				return Fail(403, "publishing_not_allowed");
			if(!Body || typeof Body !== "object" || Array.isArray(Body))
				return Fail(400, "invalid_body");

			const Markdown = NormalizeMarkdown(Body.markdown);
			if(Markdown === null || Markdown.trim() === "")
				return Fail(400, "invalid_markdown");
			if(Buffer.byteLength(Markdown, "utf8") > MaxMarkdownBytes)
				return Fail(413, "markdown_too_large");

			const Next = { version: Data.version + 1, updated_at: NowSec(), markdown: Markdown };
			Save(Next);
			return Result(200, { ok: true, version: Next.version, updated_at: Next.updated_at });
		}
	};
}

function RegisterNewsRoutes(App, Service, { CheckRateLimit, ClientIp })
{
	for(const [Method, Route, Handle] of [
		["get", "current", () => Service.Current()],
		["post", "publish", (Req) => Service.Publish(Req.get("authorization"), Req.body)]
	])
	{
		App[Method]("/api/v1/news/" + Route, (Req, Res) => {
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

module.exports = { CreateNewsService, RegisterNewsRoutes, NormalizeMarkdown, DEFAULT_MAX_MARKDOWN_BYTES };
