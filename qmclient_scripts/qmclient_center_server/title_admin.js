"use strict";

const path = require("node:path");
const { IssueTitleCode } = require("./title_auth");

if(process.argv[2] !== "issue" || !process.argv[3])
{
	console.error("用法：TITLE_DATA_DIR=/持久化目录 node title_admin.js issue 赞助者备注");
	process.exitCode = 1;
}
else
{
	const Directory = process.env.TITLE_DATA_DIR || path.join(__dirname, "title_data");
	console.log(IssueTitleCode(Directory, process.argv[3]));
}
