// 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env bun
// analyze.ts — QmClient 性能日志分析入口
// 用法: bun analyze.ts [log文件路径]
//       如果不传路径，自动读取 %APPDATA%/DDNet/dumps/QmClient_Perf/ 下最新日志
//       自动检测上一次报告对应的日志，生成对比分析；--no-compare 可跳过历史日志

import { createReadStream, writeFileSync, readdirSync, statSync, mkdirSync, existsSync } from 'node:fs';
import { createInterface } from 'node:readline';
import { homedir } from 'node:os';
import { PerfLogCollector, expandFrameBatch } from './lib/stream.ts';
import { join, basename, dirname, extname, resolve } from 'node:path';

const PERF_DIR = () => {
  if (process.platform === 'win32') return join(process.env.APPDATA ?? join(homedir(), 'AppData', 'Roaming'), 'DDNet', 'dumps', 'QmClient_Perf');
  if (process.platform === 'darwin') return join(homedir(), 'Library', 'Application Support', 'DDNet', 'dumps', 'QmClient_Perf');
  return join(process.env.XDG_DATA_HOME ?? join(homedir(), '.local', 'share'), 'ddnet', 'dumps', 'QmClient_Perf');
};
const REPORT_DIR = () => join(PERF_DIR(), 'Perf_Report');

function listLogFiles(): { name: string; path: string; mtime: number }[] {
  const dir = PERF_DIR();
  try {
    return readdirSync(dir)
      .filter(f => f.startsWith('qm_perf_') && f.endsWith('.log'))
      .map(f => ({ name: f, path: join(dir, f), mtime: statSync(join(dir, f)).mtimeMs }))
      .sort((a, b) => b.mtime - a.mtime);
  } catch {
    return [];
  }
}

function findLatestLog(): string {
  const files = listLogFiles();
  if (files.length === 0) {
    console.error('无法找到性能日志文件。请确保:');
    console.error('  1. qm_perf 1 已启用');
    console.error('  2. 至少运行过一次游戏客户端');
    process.exit(1);
  }
  return files[0].path;
}

/** 找到上一次报告对应的日志文件（当前日志的前一个） */
function findPreviousLog(currentLogPath: string): string | null {
  const files = listLogFiles();
  const currentName = basename(currentLogPath);
  const idx = files.findIndex(f => f.name === currentName);
  if (idx < 0 || idx >= files.length - 1) return null;
  return files[idx + 1].path; // 下一个 = 时间更早的
}

async function main() {
  const startedAt = performance.now();
  const { parseLine } = await import('./lib/parse.ts');
  const { generateReport } = await import('./lib/report.ts');
  const { snapshot, compareSessions } = await import('./lib/stats.ts');
  const { summarizeForBundle } = await import('./lib/quality.ts');

  async function parseLogFileWithDiagnostics(path: string) {
    const collector = new PerfLogCollector();
    let totalLines = 0;
    let invalidLines = 0;
    const lines = createInterface({
      input: createReadStream(path, { encoding: 'utf-8' }),
      crlfDelay: Infinity,
    });
    for await (const rawLine of lines) {
      const line = rawLine.trim();
      if (line.length === 0) {
        continue;
      }
      totalLines++;
      const entry = parseLine(line);
      if (entry === null) {
        invalidLines++;
        continue;
      }
      try {
        for (const expanded of expandFrameBatch(entry)) collector.add(expanded);
      } catch {
        invalidLines++;
      }
    }
    return collector.finish({ totalLines, invalidLines });
  }

  const args = process.argv.slice(2);
  const outputIndex = args.indexOf('--output');
  const outputPath = outputIndex >= 0 ? args[outputIndex + 1] : undefined;
  if (outputIndex >= 0 && !outputPath) {
    throw new Error('--output requires an HTML path');
  }
  const logPath = args.find((arg, index) => arg !== '--output' && arg !== '--no-compare' && (outputIndex < 0 || index !== outputIndex + 1)) ?? findLatestLog();
  console.log(`读取: ${logPath}`);

  console.log('解析中...（流式读取、限制驻留样本数量）');
  const parsed = await parseLogFileWithDiagnostics(logPath);
  const entries = parsed.entries;
  console.log(`解析行数: ${parsed.diagnostics.totalLines}`);
  console.log(`保留条目: ${entries.length} / ${parsed.diagnostics.totalEntries}`);
  if (parsed.diagnostics.sampledEntries) console.log('日志较大：报告使用有界样本，配置保留初始值和最新值。');
  if (parsed.diagnostics.invalidLines > 0) {
    console.log(`忽略无效行: ${parsed.diagnostics.invalidLines}`);
  }

  // 当前会话快照
  const currentSnapshot = snapshot(entries, logPath);

  // 自动查找上一次日志并生成对比
  let comparison = null;
  const prevLogPath = args.includes('--no-compare') ? null : findPreviousLog(logPath);
  if (prevLogPath) {
    try {
      const prevParsed = await parseLogFileWithDiagnostics(prevLogPath);
      const prevEntries = prevParsed.entries;
      if (prevEntries.length > 0 && !prevParsed.diagnostics.sampledEntries && !parsed.diagnostics.sampledEntries) {
        const prevSnapshot = snapshot(prevEntries, prevLogPath);
        comparison = compareSessions(prevSnapshot, currentSnapshot);
        console.log(`对比基线: ${basename(prevLogPath)} (${prevEntries.length} 条)`);
        if (comparison.verdictChanged) {
          console.log(`  判定变化: ${comparison.previous.verdict} → ${comparison.current.verdict}`);
        }
        const reg = comparison.metrics.filter(m => m.direction === 'worse').length;
        const imp = comparison.metrics.filter(m => m.direction === 'better').length;
        console.log(`  ${imp} 项改善, ${reg} 项退化`);
      }
    } catch (e) {
      console.log(`跳过对比: ${(e as Error).message}`);
    }
  } else {
    console.log('无历史日志可对比（首次分析）');
  }

  const reportHtml = generateReport(entries, logPath, comparison, parsed.diagnostics, performance.now() - startedAt);
  const summaryJson = summarizeForBundle(entries, logPath, parsed.diagnostics);

  // 输出到 Perf_Report/ 子目录
  const outPath = outputPath ? resolve(outputPath) : join(REPORT_DIR(), `${basename(logPath).replace('.log', '')}_report.html`);
  const reportDir = dirname(outPath);
  if (!existsSync(reportDir)) {
    mkdirSync(reportDir, { recursive: true });
  }

  writeFileSync(outPath, reportHtml, 'utf-8');
  const summaryText = JSON.stringify(summaryJson, null, 2);
  const logName = basename(logPath).replace('.log', '');
  const outputBase = basename(outPath, extname(outPath));
  const archiveSummaryPath = outputPath ? join(reportDir, `${outputBase}_summary.json`) : join(reportDir, `${logName}_summary.json`);
  const bundleSummaryPath = join(reportDir, 'perf_summary.json');
  writeFileSync(archiveSummaryPath, summaryText, 'utf-8');
  if (!outputPath) writeFileSync(bundleSummaryPath, summaryText, 'utf-8');
  console.log(`报表已生成: ${outPath}`);
  console.log(`摘要已生成: ${archiveSummaryPath}`);
  if (!outputPath) console.log(`Debug bundle 摘要已生成: ${bundleSummaryPath}`);
}

main();
