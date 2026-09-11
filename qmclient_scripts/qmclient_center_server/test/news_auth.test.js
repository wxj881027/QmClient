"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");
const { CreateNewsService, NormalizeMarkdown, DEFAULT_MAX_MARKDOWN_BYTES } = require("../news_auth");

const CREDENTIAL = { developer_id: "qimeng", key_id: "windows-desktop-1" };

function Fixture(t, Options = {})
{
	const Directory = fs.mkdtempSync(path.join(os.tmpdir(), "qm-news-"));
	t.after(() => fs.rmSync(Directory, { recursive: true, force: true }));
	let Now = 1000;
	const ServiceOptions = {
		Directory,
		Authenticate: (Authorization) => (Authorization === "Bearer good" ? CREDENTIAL : null),
		CanPublish: (Credential) => Credential.developer_id === "qimeng",
		NowSec: () => Now,
		...Options
	};
	return {
		Directory,
		ServiceOptions,
		Service: CreateNewsService(ServiceOptions),
		Auth: "Bearer good",
		Advance: () => { Now += 5; }
	};
}

test("未发布时返回空内容且版本为 0", (t) => {
	const f = Fixture(t);
	const Reply = f.Service.Current();
	assert.equal(Reply.statusCode, 200);
	assert.deepEqual(Reply.response, { ok: true, version: 0, updated_at: 0, markdown: "" });
});

test("发布后版本自增、可持久化重载，并公开可读", (t) => {
	const f = Fixture(t);
	const Body = { markdown: "# 新功能\n\n- 第一条\n" };
	const First = f.Service.Publish(f.Auth, Body);
	assert.equal(First.statusCode, 200);
	assert.equal(First.response.version, 1);
	assert.equal(First.response.updated_at, 1000);

	f.Advance();
	const Second = f.Service.Publish(f.Auth, { markdown: "## 第二条\n" });
	assert.equal(Second.response.version, 2);
	assert.equal(Second.response.updated_at, 1005);

	const Restarted = CreateNewsService(f.ServiceOptions);
	const Current = Restarted.Current().response;
	assert.equal(Current.version, 2);
	assert.equal(Current.markdown, "## 第二条\n");
});

test("无凭据、无发布权限、非法负载都按契约拒绝", (t) => {
	const f = Fixture(t);
	assert.equal(f.Service.Publish("Bearer bad", { markdown: "x" }).statusCode, 401);
	assert.equal(f.Service.Publish(undefined, { markdown: "x" }).statusCode, 401);
	assert.equal(f.Service.Publish(f.Auth, null).statusCode, 400);
	assert.equal(f.Service.Publish(f.Auth, { markdown: "" }).statusCode, 400);
	assert.equal(f.Service.Publish(f.Auth, { markdown: "   \n  " }).statusCode, 400);
	assert.equal(f.Service.Publish(f.Auth, { markdown: 42 }).statusCode, 400);
	assert.equal(f.Service.Publish(f.Auth, { markdown: "a\u0000b" }).statusCode, 400);
	assert.equal(f.Service.Publish(f.Auth, { markdown: ["# x"] }).statusCode, 400);
	assert.equal(f.Service.Current().response.version, 0);

	const Denied = CreateNewsService({ ...f.ServiceOptions, CanPublish: () => false });
	assert.equal(Denied.Publish(f.Auth, { markdown: "# x" }).statusCode, 403);
	assert.equal(Denied.Current().response.version, 0);
});

test("超长内容被拒绝且不落盘", (t) => {
	const f = Fixture(t, { MaxMarkdownBytes: 16 });
	assert.equal(f.Service.Publish(f.Auth, { markdown: "a".repeat(16) }).statusCode, 200);
	assert.equal(f.Service.Publish(f.Auth, { markdown: "b".repeat(17) }).statusCode, 413);
	assert.equal(f.Service.Current().response.version, 1);
	assert.equal(f.Service.Current().response.markdown, "a".repeat(16));
});

test("CRLF 归一化，且默认上限不低于 16 KiB", () => {
	assert.equal(NormalizeMarkdown("a\r\nb\rc"), "a\nb\nc");
	assert.equal(NormalizeMarkdown("a\tb"), "a\tb");
	assert.equal(NormalizeMarkdown("a\u0007b"), null);
	assert.equal(NormalizeMarkdown(1), null);
	assert.ok(DEFAULT_MAX_MARKDOWN_BYTES >= 16 * 1024);
});

test("数据库损坏时构造失败，而不是带病运行", (t) => {
	const Directory = fs.mkdtempSync(path.join(os.tmpdir(), "qm-news-bad-"));
	t.after(() => fs.rmSync(Directory, { recursive: true, force: true }));
	fs.writeFileSync(path.join(Directory, "news.json"), JSON.stringify({ version: "1", markdown: 2 }));
	assert.throws(() => CreateNewsService({ Directory }));
});
