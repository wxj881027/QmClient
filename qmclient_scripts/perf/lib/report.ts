// 请抬头享受阳光｜日子很好 我很我---------致咩子
// report.ts — 生成自包含 ECharts HTML 报表（R-style 论文式排版，纸面图表主题）

import { basename } from 'node:path';
import { configurationSummary } from './configuration.ts';
import { configurationReport } from './configuration_report.ts';

import type { PerfEntry } from './parse.ts';
import { FPS_BASELINES, fpsBaselineVerdict, nonCardMenuBudgetSummary, reportQuality, type ParseDiagnostics } from './quality.ts';
import {
  calcPercentiles, toTimeSeries, detectSpikes, histogram, pageBreakdown,
  complianceRate, computeVerdict, generateNarrative, isSamplingBiased, BUDGET,
  kde, qqNorm, pagePerformanceAttribution, selectFrameTimeEntries, entryDurationMs,
  inferSamplingThreshold, sectionPerformanceTop, fpsSummaries, targetSettingsSnapshot, PERF_SYSTEM,
  settingsTextAnalysis, assetsPreviewAdmissionSummary, assetsVisibleReadySummary, demoBrowserPhaseSummary, adaptiveBudgetSummary, settingsUiBudgetSummary,
  previewBudgetSummary, textRuntimeBudgetSummary, budgetCorrelationSummary, coldTabSwitchFpsSummaries, warmTabSwitchFpsSummaries,
  stutterDiagnosticsSummary, hasUnifiedFrameSamples,
  type BudgetCorrelationWindow, type Percentiles, type SpikeInfo, type PageStats, type ComparisonResult,
} from './stats.ts';

function escapeHtml(s: string): string {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function truncateMiddle(s: string, max = 72): string {
  if (s.length <= max) return s;
  const head = Math.max(8, Math.floor((max - 3) * 0.58));
  const tail = Math.max(8, max - 3 - head);
  return `${s.slice(0, head)}...${s.slice(-tail)}`;
}

function sampleField(sample: string, name: string): string {
  const match = sample.match(new RegExp(`(?:^| )${name}=([^ ]+)`));
  return match?.[1] ?? '';
}

function numericField(entry: PerfEntry, name: string, fallback = 0): number {
  const value = entry.fields[name];
  if (value === undefined || value === null || value === '') return fallback;
  const parsed = typeof value === 'number' ? value : Number.parseFloat(String(value));
  return Number.isFinite(parsed) ? parsed : fallback;
}

function formatMs(value: number, digits = 3): string {
  return `${value.toFixed(digits)}ms`;
}

function formatFps(value: number): string {
  return `${value.toFixed(1)} FPS`;
}

function renderMapPreparation(entries: PerfEntry[]): string {
  const samples = entries.filter(e => e.system === 'perf/connect');
  const images = samples.filter(e => e.stage === 'map_image_prepare')
    .sort((a, b) => b.durationMs - a.durationMs).slice(0, 5);
  const layers = samples.filter(e => e.stage === 'map_layer_prepare')
    .sort((a, b) => numericField(b, 'peak_ms') - numericField(a, 'peak_ms')).slice(0, 5);
  if (!images.length && !layers.length) return '';
  return `<section class="section">
    <h2>进服和换图</h2>
    ${images.length ? `<h3>贴图准备</h3><p>按样本中每张贴图的工作耗时排序，最多五条。后台解码可与主线程重叠。</p>
      <table class="data-table compact"><thead><tr><th>帧</th><th>贴图</th><th>读取</th><th>解码</th><th>主线程提交</th></tr></thead>
      <tbody>${images.map(e => `<tr><td>${numericField(e, 'frame')}</td><td>${numericField(e, 'image')}</td>
        <td>${formatMs(numericField(e, 'read_ms'))}</td><td>${numericField(e, 'read_includes_decode') ? '已计入读取' : formatMs(numericField(e, 'decode_ms'))}</td>
        <td>${formatMs(numericField(e, 'submit_ms'))}</td></tr>`).join('')}</tbody></table>` : ''}
    ${layers.length ? `<h3>图层准备</h3><p>按样本中单批主线程耗时排序，最多五条。</p>
      <table class="data-table compact"><thead><tr><th>最慢批次所在帧</th><th>绘制阶段</th><th>组 / 层</th><th>批次</th><th>累计</th><th>最慢一批</th></tr></thead>
      <tbody>${layers.map(e => `<tr><td>${numericField(e, 'peak_frame')}</td><td>${escapeHtml(String(e.fields.pass ?? ''))}</td>
        <td>${numericField(e, 'group')} / ${numericField(e, 'layer')}</td><td>${numericField(e, 'slices')}</td>
        <td>${formatMs(e.durationMs)}</td><td>${formatMs(numericField(e, 'peak_ms'))}</td></tr>`).join('')}</tbody></table>` : ''}
  </section>`;
}

function statCell(label: string, value: string, tone = ''): string {
  return `<div class="stat-cell"><span>${escapeHtml(label)}</span><strong class="${tone}">${escapeHtml(value)}</strong></div>`;
}

function metricTone(value: number, ok: number, warn: number): string {
  return value <= ok ? 'ok' : value <= warn ? 'warn' : 'bad';
}

function statisticalSummary(values: number[]): Percentiles {
  return calcPercentiles(values);
}

function sampleArrayEvenly<T>(items: T[], maxItems: number): T[] {
  if (items.length <= maxItems) return items;
  if (maxItems <= 0) return [];
  if (maxItems === 1) return [items[0]];
  const result: T[] = [];
  const lastIndex = items.length - 1;
  for (let i = 0; i < maxItems; i++) {
    result.push(items[Math.round((i / (maxItems - 1)) * lastIndex)]);
  }
  return result;
}

function renderBudgetWindowCards(windows: BudgetCorrelationWindow[]): string {
  if (windows.length === 0) {
    return '<p class="small-note">缺少 fps_summary，无法生成低帧窗口统计。</p>';
  }
  return `<div class="budget-window-grid">
    ${windows.slice(0, 8).map((row, index) => {
      const culprit = row.culpritRank[0];
      const topDetails = culprit?.details.replace(/ /g, ' · ') ?? 'none';
      const culpritList = row.culpritRank
        .filter(item => item.score > 0)
        .slice(0, 5)
        .map(item => `${item.kind}=${item.score.toFixed(2)}`)
        .join(' · ') || 'none';
      const fpsTone = row.fpsOnePctLowAvailable && row.fpsOnePctLow >= 240 ? 'ok' : row.fpsOnePctLowAvailable && row.fpsOnePctLow >= 120 ? 'warn' : 'bad';
      const p99Tone = metricTone(row.frameMsP99, BUDGET.h240, BUDGET.h120);
      return `<article class="budget-window-card ${fpsTone}">
        <div class="budget-card-head">
          <span class="rank">#${index + 1}</span>
          <div>
            <h3>${escapeHtml(row.operation || 'unknown')}</h3>
            <p>${escapeHtml(row.page || 'unknown')} · ${escapeHtml(row.tab || 'none')} · frame ${row.windowStartFrame}-${row.windowEndFrame}</p>
          </div>
          <span class="badge ${fpsTone}">${row.fpsOnePctLowAvailable ? escapeHtml(row.fpsOnePctLowSource) : 'P99-derived'}</span>
        </div>
        <div class="stat-grid dense">
          ${statCell('1% Low', formatFps(row.fpsOnePctLow), fpsTone)}
          ${statCell('p99', formatMs(row.frameMsP99), p99Tone)}
          ${statCell('Top Culprit', culprit?.kind ?? 'none', culprit && culprit.score > 0 ? 'warn' : '')}
          ${statCell('Score', culprit ? culprit.score.toFixed(2) : '0.00', culprit && culprit.score > 0 ? 'warn' : '')}
          ${statCell('Top UI Section', row.topUiSectionStage || 'none', row.topUiSectionMs > 0 ? 'warn' : '')}
          ${statCell('Section Time', formatMs(row.topUiSectionMs), metricTone(row.topUiSectionMs, 4, 8))}
          ${statCell('Card Draw', formatMs(row.maxCardDrawMs), metricTone(row.maxCardDrawMs, 4, 8))}
          ${statCell('Preview Draw', formatMs(row.maxPreviewDrawMs), metricTone(row.maxPreviewDrawMs, 1, 4))}
          ${statCell('Metadata', formatMs(row.maxMetadataLayoutMs), metricTone(row.maxMetadataLayoutMs, 1, 4))}
          ${statCell('Texture Upload', formatMs(row.maxTextureUploadMs), metricTone(row.maxTextureUploadMs, 1, 4))}
          ${statCell('Container Create', formatMs(row.maxTextContainerCreateMs), metricTone(row.maxTextContainerCreateMs, 1, 4))}
          ${statCell('Glyph Rasterize', formatMs(row.maxGlyphRasterizeMs), metricTone(row.maxGlyphRasterizeMs, 1, 4))}
          ${statCell('Paragraph', formatMs(row.maxParagraphLayoutMs), metricTone(row.maxParagraphLayoutMs, 1, 4))}
          ${statCell('Paragraph Blocked', String(row.paragraphBudgetBlocked), row.paragraphBudgetBlocked > 0 ? 'warn' : 'ok')}
          ${statCell('Telemetry', formatMs(row.maxTelemetryOverheadMs), metricTone(row.maxTelemetryOverheadMs, 1, 4))}
          ${statCell('Telemetry Flush', formatMs(row.maxTelemetryFlushMs), metricTone(row.maxTelemetryFlushMs, 1, 4))}
        </div>
        <p class="culprit-note">${escapeHtml(row.topUiSectionStage ? `ui_section:${row.topUiSectionStage} section_ms=${row.topUiSectionMs.toFixed(3)} page=${row.topUiSectionPage} frame=${row.topUiSectionFrame}` : 'ui_section:none')}</p>
        <p class="culprit-note">${escapeHtml(topDetails)}</p>
        <p class="culprit-note">${escapeHtml(culpritList)}</p>
      </article>`;
    }).join('')}
  </div>`;
}

function percentilesToChartData(p: Percentiles) {
  return [
    { name: 'p50', value: p.p50 },
    { name: 'p90', value: p.p90 },
    { name: 'p95', value: p.p95 },
    { name: 'p99', value: p.p99 },
    { name: 'max', value: p.max },
  ];
}

export function generateReport(
  entries: PerfEntry[],
  sourceFile: string,
  comparison?: ComparisonResult | null,
  diagnostics: ParseDiagnostics = { totalLines: entries.length, invalidLines: 0 },
  generationDurationMs?: number,
): string {
  const interactionEntries = entries.filter(e => e.system === PERF_SYSTEM.INTERACTION);
  const frameTimeEntries = selectFrameTimeEntries(entries);
  const menuEntries = entries.filter(e => e.system === PERF_SYSTEM.MENU && (e.stage.includes('render_total') || e.stage.includes('page_content')));
  const deviceEntries = entries.filter(e => e.system === PERF_SYSTEM.DEVICE);
  const skinUxEntries = entries.filter(e => e.system === PERF_SYSTEM.SKIN_UX);
  const allEntries = frameTimeEntries;
  const frameDurations = frameTimeEntries.map(e => entryDurationMs(e) ?? e.durationMs);
  const menuDurations = menuEntries.map(e => entryDurationMs(e) ?? e.durationMs);
  const attribution = pagePerformanceAttribution(entries);
  const sectionTop = sectionPerformanceTop(entries, 10);
  const fps = fpsSummaries(entries);
  const nonCardMenu = nonCardMenuBudgetSummary(entries);
  const coldTabSwitchFps = coldTabSwitchFpsSummaries(entries);
  const warmTabSwitchFps = warmTabSwitchFpsSummaries(entries);
  const targetSettings = targetSettingsSnapshot(entries);
  const textAnalysis = settingsTextAnalysis(entries);
  const assetsPreviewAdmission = assetsPreviewAdmissionSummary(entries);
  const assetsVisibleReady = assetsVisibleReadySummary(entries);
  const previewBudget = previewBudgetSummary(entries);
  const demoBrowser = demoBrowserPhaseSummary(entries);
  const adaptiveBudget = adaptiveBudgetSummary(entries);
  const settingsUiBudget = settingsUiBudgetSummary(entries);
  const textRuntimeBudget = textRuntimeBudgetSummary(entries);
  const budgetCorrelation = budgetCorrelationSummary(entries);
  const stutterDiagnostics = stutterDiagnosticsSummary(entries);
  const textAnalysisForData = {
    ...textAnalysis,
    prebuildSeries: sampleArrayEvenly(textAnalysis.prebuildSeries, 240),
    eventTimeline: sampleArrayEvenly(textAnalysis.eventTimeline, 240),
  };
  const assetsCardDrawEvents = entries.filter(entry => entry.system === 'perf/assets' && entry.fields.stage === 'assets_preview_draw_workshop_cards');
  const assetsCardDrawStats = statisticalSummary(assetsCardDrawEvents.map(entry => entryDurationMs(entry) ?? entry.durationMs));
  const assetsLayoutTextStats = statisticalSummary(assetsCardDrawEvents.map(entry => numericField(entry, 'layout_text_ms')));
  const assetsPreviewDrawStats = statisticalSummary(assetsCardDrawEvents.map(entry => numericField(entry, 'preview_draw_ms')));
  const assetsThumbSchedulingStats = statisticalSummary(assetsCardDrawEvents.map(entry => numericField(entry, 'thumb_scheduling_ms')));
  const textContainerCreateStats = statisticalSummary(entries
    .filter(entry => entry.system === 'perf/text' && entry.fields.event === 'text_runtime_budget')
    .map(entry => numericField(entry, 'text_container_create_ms')));
  const glyphRasterizeStats = statisticalSummary(entries
    .filter(entry => entry.system === 'perf/text' && entry.fields.event === 'text_runtime_budget')
    .map(entry => numericField(entry, 'glyph_rasterize_ms')));

  const p = calcPercentiles(frameDurations);
  const rawTimeSeries = toTimeSeries(allEntries);
  const sampledTimelinePairs = sampleArrayEvenly(
    rawTimeSeries.times.map((time, index) => ({ time, duration: rawTimeSeries.durations[index] })),
    600,
  );
  const ts = {
    times: sampledTimelinePairs.map(point => point.time),
    durations: sampledTimelinePairs.map(point => point.duration),
  };
  const spikes = detectSpikes(allEntries, BUDGET.h60);
  const histData = histogram(frameDurations, [0, 2, 4, 8, 16, 33, 100, 500]);
  const pages = pageBreakdown(menuEntries, BUDGET.h60);
  const compliance240 = complianceRate(frameDurations, BUDGET.h240);
  const compliance120 = complianceRate(frameDurations, BUDGET.h120);
  const compliance60 = complianceRate(frameDurations, BUDGET.h60);
  const biased = !hasUnifiedFrameSamples(entries) && isSamplingBiased(frameDurations);
  const samplingThresholdMs = inferSamplingThreshold(frameDurations);
  const quality = reportQuality(entries, diagnostics);
  const verdict = quality.failed ? 'FAIL' : frameTimeEntries.length === 0 || (quality.frameSamplesEstimated && computeVerdict(p, spikes.length) === 'PASS') ? 'WARN' : computeVerdict(p, spikes.length);
  const narrative = generateNarrative(p, spikes, compliance240, compliance120, compliance60, biased, samplingThresholdMs);
  const targetSettingsAcceptanceBlocked = targetSettings.stableTextCoverage.acceptanceBlocked ||
    !targetSettings.verdictAvailable;
  const targetSettingsVerdictLabel = targetSettingsAcceptanceBlocked ? '不足以验收' : targetSettings.verdict;
  const targetSettingsVerdictClass = targetSettingsAcceptanceBlocked ? 'bad' :
    targetSettings.verdict === 'PASS' ? 'ok' : targetSettings.verdict === 'WARN' ? 'warn' : 'bad';
  const stableTextNarrative = targetSettings.stableTextCoverage.acceptanceBlocked
    ? `static stable text coverage 未达标：visible candidate=${targetSettings.stableTextCoverage.visibleCandidateCount}，planned=${targetSettings.stableTextCoverage.planCandidateCount}，unplanned=${targetSettings.stableTextCoverage.unplannedVisibleCount}，key_mismatch=${targetSettings.stableTextCoverage.keyMismatchCount}，pool hit=${targetSettings.stableTextCoverage.poolHitCount}，render-ready hit=${targetSettings.stableTextCoverage.renderReadyHitCount}，build queued=${targetSettings.stableTextCoverage.buildQueued}，fallback immediate=${targetSettings.stableTextCoverage.fallbackImmediate}，reuse=${targetSettings.stableTextCoverage.reuseCount}，miss=${targetSettings.stableTextCoverage.missCount}，stale=${targetSettings.stableTextCoverage.staleCount}，render-ready hit rate=${targetSettings.stableTextCoverage.staticHitRate.toFixed(1)}%，reuse rate=${targetSettings.stableTextCoverage.staticReuseRate.toFixed(1)}%，text_new=${targetSettings.stableTextCoverage.textNew}，text_reused=${targetSettings.stableTextCoverage.textReused}，plan collection remaining=${targetSettings.stableTextCoverage.planCollectionRemainingBeforeTarget}，container prebuild remaining=${targetSettings.stableTextCoverage.prebuildRemainingBeforeTarget}，usage=${targetSettings.stableTextCoverage.utilizationAvailable ? 'available' : 'missing'}，plan coverage=${targetSettings.stableTextCoverage.planCoverageAvailable ? 'available' : 'missing'}，plan collection=${targetSettings.stableTextCoverage.planCollectionAvailable ? (targetSettings.stableTextCoverage.planCollectionComplete ? 'complete' : 'incomplete') : 'missing'}。pool hit 只说明 key 命中，render-ready hit 才说明本帧无需构建且可直接绘制；collection remaining=0 只表示计划收集完成，container remaining=0 只表示已知计划的容器构建完成。`
    : `static stable text coverage 已覆盖 target settings / ingame Esc 验收窗口：visible candidate=${targetSettings.stableTextCoverage.visibleCandidateCount}，planned=${targetSettings.stableTextCoverage.planCandidateCount}，pool hit=${targetSettings.stableTextCoverage.poolHitCount}，render-ready hit=${targetSettings.stableTextCoverage.renderReadyHitCount}，build queued=${targetSettings.stableTextCoverage.buildQueued}，fallback immediate=${targetSettings.stableTextCoverage.fallbackImmediate}，render-ready hit rate=${targetSettings.stableTextCoverage.staticHitRate.toFixed(1)}%，reuse rate=${targetSettings.stableTextCoverage.staticReuseRate.toFixed(1)}%，text_new=${targetSettings.stableTextCoverage.textNew}，text_reused=${targetSettings.stableTextCoverage.textReused}。dynamic snapshot hit rate=${targetSettings.stableTextCoverage.dynamicHitRate.toFixed(1)}%，paragraph cache hit rate=${textRuntimeBudget.paragraphCacheHitRate.toFixed(1)}%。`;
  const hottestLocation = textAnalysis.topByLocation[0];
  const hottestReason = textAnalysis.topByReason[0];
  const hottestOperation = textAnalysis.topByOperation[0];
  const textAnalysisNarrative = textAnalysis.eventTimeline.length === 0
    ? '本次日志未包含 settings_text_prebuild / miss / stale 事件。'
    : targetSettings.stableTextCoverage.acceptanceBlocked
      ? `主要 miss 热点位于 ${hottestLocation?.location ?? '-'}，主要原因为 ${hottestReason?.reason ?? '-'}，主要操作窗口为 ${hottestOperation?.operation ?? '-'}。`
      : textAnalysis.summary.missCount > 0 || textAnalysis.summary.staleCount > 0
        ? `target window 已通过，但普通 scope 仍有 miss=${textAnalysis.summary.missCount} / stale=${textAnalysis.summary.staleCount}，不阻塞本次验收。`
        : '当前日志未发现 settings text miss/stale 热点。';
  const stableTextSamples = targetSettings.stableTextCoverage.samples.slice(0, 12);
  const stableTextSamplesHtml = stableTextSamples.length === 0 ? '' : `<details class="sample-details">
    <summary>查看 static stable text miss 样本（前 ${stableTextSamples.length} 条）</summary>
    <div class="table-scroll">
      <table class="data-table compact">
        <thead><tr><th>Event</th><th>Page</th><th>Tab</th><th>Subtab</th><th>Reason</th><th>Plan Status</th><th>Operation</th><th>Key</th></tr></thead>
        <tbody>
          ${stableTextSamples.map(sample => `<tr>
            <td class="mono">${escapeHtml(sampleField(sample, 'event'))}</td>
            <td class="mono">${escapeHtml(sampleField(sample, 'page'))}</td>
            <td class="num">${escapeHtml(sampleField(sample, 'tab'))}</td>
            <td class="num">${escapeHtml(sampleField(sample, 'subtab'))}</td>
            <td>${escapeHtml(sampleField(sample, 'reason'))}</td>
            <td class="mono">${escapeHtml(sampleField(sample, 'plan_status'))}</td>
            <td class="mono">${escapeHtml(sampleField(sample, 'operation'))}</td>
            <td class="mono sample-key-cell" title="${escapeHtml(sampleField(sample, 'key'))}">${escapeHtml(truncateMiddle(sampleField(sample, 'key'), 92))}</td>
          </tr>`).join('')}
        </tbody>
      </table>
    </div>
  </details>`;
  const stableTextCoverage = targetSettings.stableTextCoverage;
  const stableTextRateClass = (value: number) =>
    !stableTextCoverage.utilizationAvailable ? 'bad' : value >= 99 ? 'ok' : value >= 95 ? 'warn' : 'bad';
  const stableTextCountClass = (value: number) => value === 0 ? 'ok' : 'bad';
  const stableTextCoverageHtml = `<div class="coverage-panel">
    <div class="coverage-title">
      <span>Static Stable Text Coverage</span>
      <span class="badge ${stableTextCoverage.acceptanceBlocked ? 'bad' : 'ok'}">${stableTextCoverage.acceptanceBlocked ? '不足以验收' : 'OK'}</span>
    </div>
    <div class="coverage-grid">
      <div class="coverage-card"><div class="coverage-label">Static Candidates</div><div class="coverage-value">${stableTextCoverage.staticCandidateTotal}</div></div>
      <div class="coverage-card"><div class="coverage-label">Render-Ready Hit Rate</div><div class="coverage-value ${stableTextRateClass(stableTextCoverage.staticHitRate)}">${stableTextCoverage.staticHitRate.toFixed(1)}%</div></div>
      <div class="coverage-card"><div class="coverage-label">Static Reuse Rate</div><div class="coverage-value ${stableTextRateClass(stableTextCoverage.staticReuseRate)}">${stableTextCoverage.staticReuseRate.toFixed(1)}%</div></div>
      <div class="coverage-card"><div class="coverage-label">Dynamic Snapshot Text Coverage</div><div class="coverage-value">${stableTextCoverage.dynamicCandidateTotal}</div></div>
      <div class="coverage-card"><div class="coverage-label">Snapshot Hit Rate</div><div class="coverage-value">${stableTextCoverage.dynamicHitRate.toFixed(1)}%</div></div>
      <div class="coverage-card"><div class="coverage-label">Plan Collection</div><div class="coverage-value ${stableTextCoverage.planCollectionAvailable && stableTextCoverage.planCollectionComplete ? 'ok' : 'bad'}">${stableTextCoverage.planCollectionAvailable ? (stableTextCoverage.planCollectionComplete ? 'Complete' : 'Incomplete') : 'Missing'}</div></div>
      <div class="coverage-card"><div class="coverage-label">Collection Units</div><div class="coverage-value">${stableTextCoverage.planCollectionUnitsDone}/${stableTextCoverage.planCollectionUnitsTotal}</div></div>
      <div class="coverage-card"><div class="coverage-label">Collection Remaining</div><div class="coverage-value ${stableTextCountClass(stableTextCoverage.planCollectionRemainingBeforeTarget)}">${stableTextCoverage.planCollectionRemainingBeforeTarget}</div></div>
      <div class="coverage-card"><div class="coverage-label">Collection Budget</div><div class="coverage-value">${stableTextCoverage.planCollectionBudget}</div></div>
      <div class="coverage-card"><div class="coverage-label">Plan Coverage</div><div class="coverage-value ${stableTextCoverage.planCoverageAvailable ? 'ok' : 'bad'}">${stableTextCoverage.planCoverageAvailable ? 'Available' : 'Missing'}</div></div>
      <div class="coverage-card"><div class="coverage-label">Planned</div><div class="coverage-value">${stableTextCoverage.planCandidateCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Visible</div><div class="coverage-value">${stableTextCoverage.visibleCandidateCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Unplanned</div><div class="coverage-value ${stableTextCountClass(stableTextCoverage.unplannedVisibleCount)}">${stableTextCoverage.unplannedVisibleCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Key Mismatch</div><div class="coverage-value ${stableTextCountClass(stableTextCoverage.keyMismatchCount)}">${stableTextCoverage.keyMismatchCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Pool Hit</div><div class="coverage-value">${stableTextCoverage.poolHitCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Queued Builds</div><div class="coverage-value ${stableTextCountClass(stableTextCoverage.buildQueued)}">${stableTextCoverage.buildQueued}</div></div>
      <div class="coverage-card"><div class="coverage-label">Immediate Fallback</div><div class="coverage-value ${stableTextCountClass(stableTextCoverage.fallbackImmediate)}">${stableTextCoverage.fallbackImmediate}</div></div>
      <div class="coverage-card"><div class="coverage-label">Render-Ready Hit Rate</div><div class="coverage-value ${stableTextRateClass(stableTextCoverage.hitRate)}">${stableTextCoverage.hitRate.toFixed(1)}%</div></div>
      <div class="coverage-card"><div class="coverage-label">Reuse Rate</div><div class="coverage-value ${stableTextRateClass(stableTextCoverage.reuseRate)}">${stableTextCoverage.reuseRate.toFixed(1)}%</div></div>
      <div class="coverage-card"><div class="coverage-label">Render-Ready Hits</div><div class="coverage-value">${stableTextCoverage.renderReadyHitCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Reused</div><div class="coverage-value">${stableTextCoverage.reuseCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Miss</div><div class="coverage-value ${stableTextCountClass(stableTextCoverage.missCount)}">${stableTextCoverage.missCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Stale</div><div class="coverage-value ${stableTextCountClass(stableTextCoverage.staleCount)}">${stableTextCoverage.staleCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Text New</div><div class="coverage-value ${stableTextCountClass(stableTextCoverage.textNew)}">${stableTextCoverage.textNew}</div></div>
      <div class="coverage-card"><div class="coverage-label">Text Reused</div><div class="coverage-value">${stableTextCoverage.textReused}</div></div>
      <div class="coverage-card"><div class="coverage-label">Container Remaining</div><div class="coverage-value ${stableTextCountClass(stableTextCoverage.prebuildRemainingBeforeTarget)}">${stableTextCoverage.prebuildRemainingBeforeTarget}</div></div>
      <div class="coverage-card"><div class="coverage-label">Visible Coverage</div><div class="coverage-value ${stableTextCoverage.planCoverageAvailable && stableTextCoverage.unplannedVisibleCount === 0 && stableTextCoverage.keyMismatchCount === 0 ? 'ok' : 'bad'}">${stableTextCoverage.planCoverageAvailable ? 'Tracked' : 'Missing'}</div></div>
      <div class="coverage-card"><div class="coverage-label">Usage</div><div class="coverage-value ${stableTextCoverage.utilizationAvailable ? 'ok' : 'bad'}">${stableTextCoverage.utilizationAvailable ? 'Available' : 'Missing'}</div></div>
    </div>
  </div>`;
  const assetsPreviewAdmissionHtml = `<div class="coverage-panel">
    <div class="coverage-title">
      <span>Assets Visible-First Admission</span>
      <span class="badge ${assetsPreviewAdmission.visibleFirstAvailable ? 'ok' : assetsPreviewAdmission.available ? 'warn' : 'bad'}">${assetsPreviewAdmission.visibleFirstAvailable ? 'VISIBLE-FIRST' : assetsPreviewAdmission.available ? 'MISSING' : 'NO DATA'}</span>
    </div>
    <div class="coverage-grid">
      <div class="coverage-card"><div class="coverage-label">Events</div><div class="coverage-value">${assetsPreviewAdmission.eventCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Max Draw</div><div class="coverage-value">${assetsPreviewAdmission.maxDurationMs.toFixed(1)}ms</div></div>
      <div class="coverage-card"><div class="coverage-label">Layout/Text</div><div class="coverage-value">${assetsPreviewAdmission.maxLayoutTextMs.toFixed(1)}ms</div></div>
      <div class="coverage-card"><div class="coverage-label">Preview Draw</div><div class="coverage-value">${assetsPreviewAdmission.maxPreviewDrawMs.toFixed(1)}ms</div></div>
      <div class="coverage-card"><div class="coverage-label">Thumb Scheduling</div><div class="coverage-value">${assetsPreviewAdmission.maxThumbSchedulingMs.toFixed(1)}ms</div></div>
      <div class="coverage-card"><div class="coverage-label">Max Rendered</div><div class="coverage-value">${assetsPreviewAdmission.maxRendered}</div></div>
      <div class="coverage-card"><div class="coverage-label">Max Thumb Starts</div><div class="coverage-value">${assetsPreviewAdmission.maxThumbStarts}</div></div>
      <div class="coverage-card"><div class="coverage-label">Visible Starts</div><div class="coverage-value">${assetsPreviewAdmission.visibleStarts}</div></div>
      <div class="coverage-card"><div class="coverage-label">Prefetch Starts</div><div class="coverage-value">${assetsPreviewAdmission.prefetchStarts}</div></div>
      <div class="coverage-card"><div class="coverage-label">Background Starts</div><div class="coverage-value">${assetsPreviewAdmission.backgroundStarts}</div></div>
    </div>
    <p class="small-note">首屏资源 admission 应优先 combined visible window；remaining thumb/preview 工作只能进入 prefetch/background。</p>
  </div>`;
  const settingsUiBudgetHtml = `<div class="coverage-panel">
    <div class="coverage-title">
      <span>Settings UI Budget</span>
      <span class="badge ${settingsUiBudget.available ? 'ok' : 'bad'}">${settingsUiBudget.available ? 'AVAILABLE' : 'NO DATA'}</span>
    </div>
    <div class="coverage-grid">
      <div class="coverage-card"><div class="coverage-label">Events</div><div class="coverage-value">${settingsUiBudget.eventCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Layout/Frame</div><div class="coverage-value">${settingsUiBudget.maxLayoutMs.toFixed(1)}ms</div></div>
      <div class="coverage-card"><div class="coverage-label">Text</div><div class="coverage-value">${settingsUiBudget.maxTextMs.toFixed(1)}ms</div></div>
      <div class="coverage-card"><div class="coverage-label">Text New</div><div class="coverage-value">${settingsUiBudget.maxTextNew}</div></div>
      <div class="coverage-card"><div class="coverage-label">Text Reused</div><div class="coverage-value">${settingsUiBudget.maxTextReused}</div></div>
      <div class="coverage-card"><div class="coverage-label">Draw Calls</div><div class="coverage-value">${settingsUiBudget.maxDrawCalls}</div></div>
      <div class="coverage-card"><div class="coverage-label">Vertices</div><div class="coverage-value">${settingsUiBudget.maxVertices}</div></div>
      <div class="coverage-card"><div class="coverage-label">Indices</div><div class="coverage-value">${settingsUiBudget.maxIndices}</div></div>
      <div class="coverage-card"><div class="coverage-label">Heap Allocs</div><div class="coverage-value">${settingsUiBudget.maxHeapAllocs}</div></div>
      <div class="coverage-card"><div class="coverage-label">Visible Widgets</div><div class="coverage-value">${settingsUiBudget.maxVisibleWidgets}</div></div>
    </div>
    <p class="small-note">这些字段来自 settings UI frame budget 事件，用于和 fps window、text/runtime、preview/upload 事件一起做窗口归因。</p>
  </div>`;
  const nonCardMenuHtml = `<div class="coverage-panel">
    <div class="coverage-title">
      <span>非卡片菜单预算</span>
      <span class="badge ${nonCardMenu.operations.some(row => row.verdict === 'FAIL') ? 'bad' : nonCardMenu.operations.some(row => row.verdict === 'WARN') ? 'warn' : 'ok'}">FIXED SCENES</span>
    </div>
    <div class="table-scroll"><table class="data-table compact">
      <thead><tr><th>Operation</th><th>Verdict</th><th>P95</th><th>P99</th><th>Max</th><th>Menu Max</th><th>1% Low</th><th>Processed</th><th>Skipped</th><th>Cache Hit</th><th>Cache Miss</th><th>Eviction</th></tr></thead>
      <tbody>${nonCardMenu.operations.map(row => `<tr>
        <td class="mono">${escapeHtml(row.operation)}</td><td><span class="badge ${row.verdict === 'PASS' ? 'ok' : row.verdict === 'WARN' ? 'warn' : 'bad'}" title="${escapeHtml(row.reason)}">${row.verdict}</span></td>
        <td class="num">${row.frameMsP95.toFixed(3)}ms</td><td class="num">${row.frameMsP99.toFixed(3)}ms</td><td class="num">${row.frameMsMax.toFixed(3)}ms</td><td class="num">${row.uiFieldsAvailable ? `${row.menuMsMax.toFixed(3)}ms` : 'N/A'}</td><td class="num">${row.fpsOnePctLow.toFixed(1)}</td>
        <td class="num">${row.itemsProcessed}</td><td class="num">${row.itemsSkipped}</td><td class="num">${row.cacheFieldsAvailable ? row.cacheHits : 'N/A'}</td><td class="num">${row.cacheFieldsAvailable ? row.cacheMisses : 'N/A'}</td><td class="num">${row.cacheFieldsAvailable ? row.cacheEvictions : 'N/A'}</td>
      </tr>`).join('')}</tbody>
    </table></div>
    <p class="small-note">八个固定 operation 必须同时包含真实采样 1% low 与 menu_ui_frame 可见工作；缺缓存字段只降级为 WARN。</p>
  </div>`;
  const textRuntimeBudgetHtml = `<div class="coverage-panel">
    <div class="coverage-title">
      <span>Text Pipeline</span>
      <span class="badge ${textRuntimeBudget.available ? 'ok' : 'bad'}">${textRuntimeBudget.available ? 'AVAILABLE' : 'NO DATA'}</span>
    </div>
    <div class="coverage-grid">
      <div class="coverage-card"><div class="coverage-label">Events</div><div class="coverage-value">${textRuntimeBudget.eventCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Glyph New</div><div class="coverage-value">${textRuntimeBudget.glyphNew}</div></div>
      <div class="coverage-card"><div class="coverage-label">Glyph Uploads</div><div class="coverage-value">${textRuntimeBudget.glyphUploads}</div></div>
      <div class="coverage-card"><div class="coverage-label">Glyph Rasterize Max</div><div class="coverage-value">${textRuntimeBudget.maxGlyphRasterizeMs.toFixed(3)}ms</div></div>
      <div class="coverage-card"><div class="coverage-label">Glyph Upload Max</div><div class="coverage-value">${textRuntimeBudget.maxGlyphUploadMs.toFixed(3)}ms</div></div>
      <div class="coverage-card"><div class="coverage-label">Container New</div><div class="coverage-value">${textRuntimeBudget.textContainerNew}</div></div>
      <div class="coverage-card"><div class="coverage-label">Container Uploads</div><div class="coverage-value">${textRuntimeBudget.textContainerUploads}</div></div>
      <div class="coverage-card"><div class="coverage-label">Container Create Max</div><div class="coverage-value">${textRuntimeBudget.maxTextContainerCreateMs.toFixed(3)}ms</div></div>
      <div class="coverage-card"><div class="coverage-label">Container Upload Max</div><div class="coverage-value">${textRuntimeBudget.maxTextContainerUploadMs.toFixed(3)}ms</div></div>
      <div class="coverage-card"><div class="coverage-label">Static Stable Text</div><div class="coverage-value">${textRuntimeBudget.staticStableHitCount}/${textRuntimeBudget.staticStableHitCount + textRuntimeBudget.staticStableMissCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Static Stable Hit Rate</div><div class="coverage-value">${textRuntimeBudget.staticStableHitRate.toFixed(1)}%</div></div>
      <div class="coverage-card"><div class="coverage-label">Snapshot Cache</div><div class="coverage-value">${textRuntimeBudget.snapshotCacheHit}/${textRuntimeBudget.snapshotCacheHit + textRuntimeBudget.snapshotCacheMiss}</div></div>
      <div class="coverage-card"><div class="coverage-label">Snapshot Cache Hit Rate</div><div class="coverage-value">${textRuntimeBudget.snapshotCacheHitRate.toFixed(1)}%</div></div>
      <div class="coverage-card"><div class="coverage-label">Paragraph Layout</div><div class="coverage-value">${textRuntimeBudget.maxParagraphLayoutMs.toFixed(3)}ms</div></div>
      <div class="coverage-card"><div class="coverage-label">Paragraph Budget Blocked</div><div class="coverage-value">${textRuntimeBudget.paragraphBudgetBlocked}</div></div>
      <div class="coverage-card"><div class="coverage-label">Paragraph Cache</div><div class="coverage-value">${textRuntimeBudget.paragraphCacheHit}/${textRuntimeBudget.paragraphCacheHit + textRuntimeBudget.paragraphCacheMiss}</div></div>
      <div class="coverage-card"><div class="coverage-label">Paragraph Cache Hit Rate</div><div class="coverage-value">${textRuntimeBudget.paragraphCacheHitRate.toFixed(1)}%</div></div>
      <div class="coverage-card"><div class="coverage-label">Paragraph Cache Hit</div><div class="coverage-value">${textRuntimeBudget.paragraphCacheHit}</div></div>
      <div class="coverage-card"><div class="coverage-label">Paragraph Cache Miss</div><div class="coverage-value">${textRuntimeBudget.paragraphCacheMiss}</div></div>
    </div>
    <p class="small-note">该面板展示 glyph/container/snapshot/paragraph runtime 成本，并参与低帧窗口归因。</p>
  </div>`;
  const assetsVisibleReadyHtml = `<div class="coverage-panel">
    <div class="coverage-title">
      <span>Assets Visible Ready</span>
      <span class="badge ${assetsVisibleReady.available && assetsVisibleReady.visibleReadyAvailable && assetsVisibleReady.geometryStable && assetsVisibleReady.thumbStartsDuringDraw === 0 ? 'ok' : 'bad'}">${assetsVisibleReady.available ? (assetsVisibleReady.visibleReadyAvailable ? 'VISIBLE READY' : 'NOT READY') : 'NO DATA'}</span>
    </div>
    <div class="coverage-grid">
      <div class="coverage-card"><div class="coverage-label">Available</div><div class="coverage-value ${assetsVisibleReady.available ? 'ok' : 'bad'}">${assetsVisibleReady.available ? 'Yes' : 'No'}</div></div>
      <div class="coverage-card"><div class="coverage-label">Visible Ready</div><div class="coverage-value ${assetsVisibleReady.visibleReadyAvailable ? 'ok' : 'bad'}">${assetsVisibleReady.visibleReadyAvailable ? 'Yes' : 'No'}</div></div>
      <div class="coverage-card"><div class="coverage-label">Geometry Stable</div><div class="coverage-value ${assetsVisibleReady.geometryStable ? 'ok' : 'bad'}">${assetsVisibleReady.geometryStable ? 'Yes' : 'No'}</div></div>
      <div class="coverage-card"><div class="coverage-label">Thumb Starts Before Visible</div><div class="coverage-value">${assetsVisibleReady.thumbStartsBeforeVisible}</div></div>
      <div class="coverage-card"><div class="coverage-label">Thumb Starts During Draw</div><div class="coverage-value ${assetsVisibleReady.thumbStartsDuringDraw === 0 ? 'ok' : 'bad'}">${assetsVisibleReady.thumbStartsDuringDraw}</div></div>
      <div class="coverage-card"><div class="coverage-label">Not Ready</div><div class="coverage-value ${assetsVisibleReady.notReadyCount === 0 ? 'ok' : 'warn'}">${assetsVisibleReady.notReadyCount}</div></div>
    </div>
    <p class="small-note">visible_first 只表示请求优先；visible_ready 才表示首屏可见卡片在展示前已 ready 或使用稳定尺寸 skeleton。</p>
  </div>`;
  const tabSwitchFpsRows = [...coldTabSwitchFps.map(summary => ({ kind: 'Cold', summary })), ...warmTabSwitchFps.map(summary => ({ kind: 'Warm', summary }))];
  const fpsBaselineRows = fps
    .map(summary => ({ summary, verdict: fpsBaselineVerdict(summary) }))
    .filter((row): row is { summary: typeof row.summary; verdict: NonNullable<ReturnType<typeof fpsBaselineVerdict>> } => row.verdict !== null);
  const onePctLowTargetFps = 240;
  const tabSwitchFpsHasRealOnePctLow = tabSwitchFpsRows.length > 0 && tabSwitchFpsRows.every(row => row.summary.fpsOnePctLowAvailable);
  const tabSwitchFpsHasFallbackOnePctLow = tabSwitchFpsRows.some(row => !row.summary.fpsOnePctLowAvailable);
  const tabSwitchFpsMeetsTarget = tabSwitchFpsHasRealOnePctLow && tabSwitchFpsRows.every(row => row.summary.fpsOnePctLow >= onePctLowTargetFps);
  const coldWarmTabSwitchHtml = `<div class="coverage-panel">
    <div class="coverage-title">
      <span>Cold/Warm Tab Switch</span>
      <span class="badge ${tabSwitchFpsRows.length === 0 ? 'bad' : tabSwitchFpsMeetsTarget ? 'ok' : 'warn'}">1% Low Target ${onePctLowTargetFps} FPS</span>
    </div>
    ${tabSwitchFpsRows.length === 0 ? '<p class="small-note">缺少 settings_assets_tab_switch / settings_tee_tab_switch 的 fps_summary。</p>' : `<div class="table-scroll">
      <table class="data-table compact">
        <thead><tr><th>Kind</th><th>Operation</th><th>Context</th><th>Page</th><th>Tab</th><th>1% Low</th><th>1% Source</th><th>P99 Frame</th><th>Max Frame</th><th>Frames</th></tr></thead>
        <tbody>
          ${tabSwitchFpsRows.map(row => `<tr>
            <td>${row.kind}</td>
            <td class="mono">${escapeHtml(row.summary.operation)}</td>
            <td class="mono">${escapeHtml(row.summary.context)}</td>
            <td class="mono">${escapeHtml(row.summary.page)}</td>
            <td class="mono">${escapeHtml(row.summary.tab)}</td>
            <td class="num ${row.summary.fpsOnePctLowAvailable && row.summary.fpsOnePctLow >= onePctLowTargetFps ? 'ok' : 'bad'}">${row.summary.fpsOnePctLow.toFixed(1)}</td>
            <td class="mono">${escapeHtml(row.summary.fpsOnePctLowSource)}</td>
            <td class="num ${row.summary.frameMsP99 <= BUDGET.h240 ? 'ok' : 'bad'}">${row.summary.frameMsP99.toFixed(3)}ms</td>
            <td class="num">${row.summary.frameMsMax.toFixed(3)}ms</td>
            <td class="num">${row.summary.sampleFrames}</td>
          </tr>`).join('')}
        </tbody>
      </table>
    </div>`}
    <p class="small-note">1% Low Target 以真实 fps_1pct_low 为准；${tabSwitchFpsHasFallbackOnePctLow ? '旧日志缺字段时只显示 P99-derived 参考值，不计入通过。' : '当前窗口使用 real_sampled。'}P99 frame 应小于 ${BUDGET.h240.toFixed(3)}ms。</p>
  </div>`;
  const fpsBaselineHtml = `<div class="coverage-panel">
    <div class="coverage-title">
      <span>FPS Baseline Targets</span>
      <span class="badge ${fpsBaselineRows.length === 0 ? 'warn' : fpsBaselineRows.every(row => row.verdict.passed) ? 'ok' : 'bad'}">${fpsBaselineRows.length === 0 ? 'NO TARGET WINDOWS' : fpsBaselineRows.every(row => row.verdict.passed) ? 'PASS' : 'FAIL'}</span>
    </div>
    ${fpsBaselineRows.length === 0 ? '<p class="small-note">缺少 ingame_esc_open / settings_assets_tab_switch / settings_tab_switch 的 fps_summary，无法做窗口级 FPS baseline 判定。</p>' : `<div class="table-scroll">
      <table class="data-table compact">
        <thead><tr><th>Operation</th><th>Context</th><th>Page</th><th>Tab</th><th>p99 Target</th><th>max Target</th><th>menu Target</th><th>1% Low Target</th><th>Current</th><th>Verdict</th></tr></thead>
        <tbody>
          ${fpsBaselineRows.map(row => {
            const baseline = FPS_BASELINES[row.verdict.operation];
            const current = `p99=${row.summary.frameMsP99.toFixed(3)}ms max=${row.summary.frameMsMax.toFixed(3)}ms menu=${row.summary.menuMsMax.toFixed(3)}ms 1pct=${row.summary.fpsOnePctLow.toFixed(1)} ${row.summary.fpsOnePctLowSource}`;
            return `<tr>
              <td class="mono">${escapeHtml(row.summary.operation)}</td>
              <td class="mono">${escapeHtml(row.summary.context)}</td>
              <td class="mono">${escapeHtml(row.summary.page)}</td>
              <td class="mono">${escapeHtml(row.summary.tab)}</td>
              <td class="num">${baseline.p99.toFixed(1)}ms</td>
              <td class="num">${baseline.max.toFixed(1)}ms</td>
              <td class="num">${baseline.menuMax.toFixed(1)}ms</td>
              <td class="num">${baseline.onePctLow.toFixed(1)} FPS</td>
              <td class="mono">${escapeHtml(current)}</td>
              <td><span class="badge ${row.verdict.passed ? 'ok' : 'bad'}">${row.verdict.passed ? 'PASS' : escapeHtml(row.verdict.reason)}</span></td>
            </tr>`;
          }).join('')}
        </tbody>
      </table>
    </div>`}
    <p class="small-note">这些 baseline 是目标窗口验收阈值；240Hz 低帧 gate 仍单独保留，不会被此表放宽。</p>
  </div>`;
  const previewBudgetHtml = `<div class="coverage-panel">
    <div class="coverage-title">
      <span>Preview Budget</span>
      <span class="badge ${previewBudget.available ? 'ok' : 'bad'}">${previewBudget.available ? 'AVAILABLE' : 'NO DATA'}</span>
    </div>
    <div class="coverage-grid">
      <div class="coverage-card"><div class="coverage-label">Events</div><div class="coverage-value">${previewBudget.eventCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Jobs Started</div><div class="coverage-value">${previewBudget.previewJobsStarted}</div></div>
      <div class="coverage-card"><div class="coverage-label">Jobs Done</div><div class="coverage-value">${previewBudget.previewJobsDone}</div></div>
      <div class="coverage-card"><div class="coverage-label">Uploads</div><div class="coverage-value">${previewBudget.previewUploads}</div></div>
      <div class="coverage-card"><div class="coverage-label">Admissions</div><div class="coverage-value">${previewBudget.previewAdmissions}</div></div>
      <div class="coverage-card"><div class="coverage-label">Artifact Max</div><div class="coverage-value">${previewBudget.maxPreviewArtifactMs.toFixed(3)}ms</div></div>
      <div class="coverage-card"><div class="coverage-label">Metadata Max</div><div class="coverage-value">${previewBudget.maxMetadataHydrateMs.toFixed(3)}ms</div></div>
      <div class="coverage-card"><div class="coverage-label">Placeholders</div><div class="coverage-value">${previewBudget.maxPlaceholderCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Ready Textures</div><div class="coverage-value">${previewBudget.maxReadyTextureCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Visible Ready Ratio</div><div class="coverage-value">${(previewBudget.minVisibleReadyRatio * 100).toFixed(1)}%</div></div>
    </div>
    <p class="small-note">Preview pipeline 预算区分真实 jobs/uploads 与 admission。当前 Assets card draw loop 只记录 admission、placeholder 和 ready texture；真实 artifact/upload 应由后台 job 和上传 drain 路径上报。</p>
  </div>`;
  const adaptiveBudgetHtml = `<div class="coverage-panel">
    <div class="coverage-title">
      <span>UI Frame Scheduler</span>
      <span class="badge ${adaptiveBudget.available ? adaptiveBudget.framePressureCount > 0 ? 'warn' : 'ok' : 'bad'}">${adaptiveBudget.available ? 'AVAILABLE' : 'NO DATA'}</span>
    </div>
    <div class="coverage-grid">
      <div class="coverage-card"><div class="coverage-label">Events</div><div class="coverage-value">${adaptiveBudget.eventCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Frame Pressure</div><div class="coverage-value ${adaptiveBudget.framePressureCount > 0 ? 'warn' : 'ok'}">${adaptiveBudget.framePressureCount}</div></div>
      <div class="coverage-card"><div class="coverage-label">Visible Tokens</div><div class="coverage-value">${adaptiveBudget.maxVisibleTokens}</div></div>
      <div class="coverage-card"><div class="coverage-label">Prefetch Tokens</div><div class="coverage-value">${adaptiveBudget.maxPrefetchTokens}</div></div>
      <div class="coverage-card"><div class="coverage-label">Background Tokens</div><div class="coverage-value">${adaptiveBudget.maxBackgroundTokens}</div></div>
      <div class="coverage-card"><div class="coverage-label">GPU Upload Tokens</div><div class="coverage-value">${adaptiveBudget.maxGpuUploadTokens}</div></div>
      <div class="coverage-card"><div class="coverage-label">Resource Upload Tokens</div><div class="coverage-value">${adaptiveBudget.maxResourceUploadTokens}</div></div>
      <div class="coverage-card"><div class="coverage-label">Text Tokens</div><div class="coverage-value">${adaptiveBudget.maxTextTokens}</div></div>
      <div class="coverage-card"><div class="coverage-label">Text Container Tokens</div><div class="coverage-value">${adaptiveBudget.maxTextContainerTokens}</div></div>
      <div class="coverage-card"><div class="coverage-label">Glyph Upload Tokens</div><div class="coverage-value">${adaptiveBudget.maxGlyphUploadTokens}</div></div>
      <div class="coverage-card"><div class="coverage-label">Paragraph Layout Tokens</div><div class="coverage-value">${adaptiveBudget.maxParagraphLayoutTokens}</div></div>
      <div class="coverage-card"><div class="coverage-label">Demo Tokens</div><div class="coverage-value">${adaptiveBudget.maxDemoTokens}</div></div>
    </div>
    <p class="small-note">预算根据 frame pacing、滚动状态、backlog 和窗口激活状态 AIMD 调整；frame_pressure 会快速削减 background，visible 保留最小 token。</p>
  </div>`;
  const assetsDrawDistributionHtml = `<div class="coverage-panel">
    <div class="coverage-title">
      <span>Assets Draw Distribution</span>
      <span class="badge ${assetsCardDrawStats.count === 0 ? 'bad' : assetsCardDrawStats.p99 <= 4 ? 'ok' : assetsCardDrawStats.p99 <= 8 ? 'warn' : 'bad'}">${assetsCardDrawStats.count === 0 ? 'NO DATA' : `${assetsCardDrawStats.count} SAMPLES`}</span>
    </div>
    <div class="distribution-grid">
      <div>
        <h3 class="mini-heading">Card Draw</h3>
        <div class="stat-grid dense">
          ${statCell('p50', formatMs(assetsCardDrawStats.p50), metricTone(assetsCardDrawStats.p50, 4, 8))}
          ${statCell('p95', formatMs(assetsCardDrawStats.p95), metricTone(assetsCardDrawStats.p95, 4, 8))}
          ${statCell('p99', formatMs(assetsCardDrawStats.p99), metricTone(assetsCardDrawStats.p99, 4, 8))}
          ${statCell('max', formatMs(assetsCardDrawStats.max), metricTone(assetsCardDrawStats.max, 4, 8))}
        </div>
      </div>
      <div>
        <h3 class="mini-heading">Substage p99 / max</h3>
        <div class="stat-grid dense">
          ${statCell('Layout p99', formatMs(assetsLayoutTextStats.p99), metricTone(assetsLayoutTextStats.p99, 1, 4))}
          ${statCell('Layout max', formatMs(assetsLayoutTextStats.max), metricTone(assetsLayoutTextStats.max, 1, 4))}
          ${statCell('Preview p99', formatMs(assetsPreviewDrawStats.p99), metricTone(assetsPreviewDrawStats.p99, 1, 4))}
          ${statCell('Preview max', formatMs(assetsPreviewDrawStats.max), metricTone(assetsPreviewDrawStats.max, 1, 4))}
          ${statCell('Thumb p99', formatMs(assetsThumbSchedulingStats.p99), metricTone(assetsThumbSchedulingStats.p99, 1, 4))}
          ${statCell('Thumb max', formatMs(assetsThumbSchedulingStats.max), metricTone(assetsThumbSchedulingStats.max, 1, 4))}
        </div>
      </div>
    </div>
    <p class="small-note">目标口径：Assets card draw p99/max 越接近 4ms 越好；layout_text 和 preview_draw 应接近 1ms 级别。</p>
  </div>`;
  const textRuntimeDistributionHtml = `<div class="coverage-panel">
    <div class="coverage-title">
      <span>Text Runtime Distribution</span>
      <span class="badge ${textContainerCreateStats.count === 0 && glyphRasterizeStats.count === 0 ? 'bad' : textContainerCreateStats.p99 <= 1 && glyphRasterizeStats.p99 <= 1 ? 'ok' : 'warn'}">${textContainerCreateStats.count + glyphRasterizeStats.count === 0 ? 'NO DATA' : 'SAMPLED'}</span>
    </div>
    <div class="stat-grid dense">
      ${statCell('Container Create p95', formatMs(textContainerCreateStats.p95), metricTone(textContainerCreateStats.p95, 1, 4))}
      ${statCell('Container Create p99', formatMs(textContainerCreateStats.p99), metricTone(textContainerCreateStats.p99, 1, 4))}
      ${statCell('Container Create max', formatMs(textContainerCreateStats.max), metricTone(textContainerCreateStats.max, 1, 4))}
      ${statCell('Glyph Rasterize p95', formatMs(glyphRasterizeStats.p95), metricTone(glyphRasterizeStats.p95, 1, 4))}
      ${statCell('Glyph Rasterize p99', formatMs(glyphRasterizeStats.p99), metricTone(glyphRasterizeStats.p99, 1, 4))}
      ${statCell('Glyph Rasterize max', formatMs(glyphRasterizeStats.max), metricTone(glyphRasterizeStats.max, 1, 4))}
    </div>
    <p class="small-note">这里显示文本 runtime 的尾部成本，避免用 raw text_runtime 行判断是否优化有效。</p>
  </div>`;
  const budgetCorrelationHtml = `<div class="coverage-panel">
    <div class="coverage-title">
      <span>Budget Attribution by Window</span>
      <span class="badge ${budgetCorrelation.available ? budgetCorrelation.unattributedFailingWindowCount > 0 ? 'bad' : budgetCorrelation.failingWindowCount > 0 ? 'warn' : 'ok' : 'bad'}">${budgetCorrelation.available ? `${budgetCorrelation.failingWindowCount} BELOW 240${budgetCorrelation.unattributedFailingWindowCount > 0 ? ` / ${budgetCorrelation.unattributedFailingWindowCount} unattributed_spike` : ''}` : 'NO FPS WINDOWS'}</span>
    </div>
    <div class="stat-grid dense">
      ${statCell('Windows', String(budgetCorrelation.windowCount))}
      ${statCell('Below 240 FPS', String(budgetCorrelation.failingWindowCount), budgetCorrelation.failingWindowCount > 0 ? 'warn' : 'ok')}
      ${statCell('Unattributed', String(budgetCorrelation.unattributedFailingWindowCount), budgetCorrelation.unattributedFailingWindowCount > 0 ? 'bad' : 'ok')}
      ${statCell('Worst 1% Low', budgetCorrelation.windows.length === 0 ? 'N/A' : formatFps(budgetCorrelation.windows[0].fpsOnePctLow), budgetCorrelation.windows[0]?.fpsOnePctLow >= 240 ? 'ok' : 'bad')}
    </div>
    <h3 class="mini-heading">Budget Window Statistics</h3>
    ${renderBudgetWindowCards(budgetCorrelation.windows)}
    <p class="small-note">Graphics backend optimization is not implicated unless backend upload/render buckets dominate the target window. This report keeps backend, glyph/text, and UI/layout buckets separate.</p>
    <p class="small-note">窗口归因按 window_start_frame/window_end_frame 关联 fps、resource preview、adaptive budget 和 text runtime。正文只展示统计结论和 top culprit；原始行保留在 summary JSON/日志中。</p>
  </div>`;

  const worstStutterWindow = stutterDiagnostics.windows[0];
  const worstStutterComponents = worstStutterWindow
    ? stutterDiagnostics.components.filter(component => component.stutterId === worstStutterWindow.stutterId && component.segment === worstStutterWindow.segment).slice(0, 20)
    : [];
  const worstStutterStages = worstStutterWindow
    ? stutterDiagnostics.stages.filter(stage => stage.stutterId === worstStutterWindow.stutterId && stage.segment === worstStutterWindow.segment).slice(0, 12)
    : [];
  const worstStutterFeatures = worstStutterWindow
    ? stutterDiagnostics.featureStates.filter(feature => feature.stutterId === worstStutterWindow.stutterId && feature.segment === worstStutterWindow.segment).slice(0, 80)
    : [];
  const stutterDiagnosticsHtml = !stutterDiagnostics.available
    ? '<p class="body-text" style="color:rgba(var(--ink-rgb),0.4);font-style:italic">本次日志未包含卡顿诊断数据。请开启 <code>qm_perf_debug 1</code> 并采集实际游戏帧。</p>'
    : `<div class="coverage-panel">
      <div class="coverage-title">
        <span>300 FPS / 3.333ms Diagnosis</span>
        <span class="badge ${stutterDiagnostics.status === 'cap_limited' ? 'warn' : 'bad'}">${stutterDiagnostics.status === 'cap_limited' ? 'CAP LIMITED' : 'LOW FPS'}</span>
      </div>
      <div class="stat-grid dense">
        ${statCell('Windows', String(stutterDiagnostics.windows.length))}
        ${statCell('Worst FPS Avg', worstStutterWindow ? formatFps(worstStutterWindow.fpsAvg) : 'N/A', worstStutterWindow && worstStutterWindow.fpsAvg >= 300 ? 'ok' : 'bad')}
        ${statCell('Worst 1% Low', worstStutterWindow ? formatFps(worstStutterWindow.fpsOnePctLow) : 'N/A', 'bad')}
        ${statCell('Worst Frame', worstStutterWindow ? formatMs(worstStutterWindow.frameMsMax) : 'N/A', 'bad')}
        ${statCell('Cap Reason', worstStutterWindow?.capReason ?? 'none', worstStutterWindow?.capLimited ? 'warn' : '')}
        ${statCell('Frame Range', worstStutterWindow ? `${worstStutterWindow.windowStartFrame}-${worstStutterWindow.windowEndFrame}` : 'N/A')}
      </div>
      ${worstStutterWindow?.detailMode === 'summary'
        ? '<p class="body-text">此窗口只记录限帧摘要，完整渲染间隔仍计入帧统计。</p>'
        : worstStutterWindow?.detailMode === 'slow_frames'
        ? '<p class="body-text">组件统计仅计入超出当前帧预算的慢帧。限帧时，记录达到限帧周期 1.5 倍的帧。</p>'
        : worstStutterWindow?.capLimited
        ? '<p class="body-text">检测到人为限帧、VSync、后台限帧或菜单 idle throttle。下面的 CPU 数据仅作为优化候选，不判定任何模块为卡顿原因。</p>'
        : '<p class="body-text">以下排名来自低于 300 FPS 帧对应的组件 CPU 回调实测耗时；total 反映持续负担，P95/Max 反映尾部尖峰。</p>'}
      <h3 class="mini-heading">主线程与 Graphics Swap</h3>
      ${worstStutterStages.length === 0 ? '<p class="small-note">该窗口没有超过采样阈值的 main_thread 阶段。</p>' : `<div class="table-scroll"><table class="data-table compact">
        <thead><tr><th>Stage</th><th>Samples</th><th>Total</th><th>Average</th><th>P95</th><th>Max</th></tr></thead>
        <tbody>${worstStutterStages.map(stage => `<tr><td class="mono">${escapeHtml(stage.stage)}</td><td class="num">${stage.samples}</td><td class="num">${formatMs(stage.totalMs)}</td><td class="num">${formatMs(stage.avgMs)}</td><td class="num">${formatMs(stage.p95Ms)}</td><td class="num">${formatMs(stage.maxMs)}</td></tr>`).join('')}</tbody>
      </table></div>`}
      ${worstStutterWindow?.detailMode === 'summary' ? '' : '<h3 class="mini-heading">CPU 模块耗时排名</h3>'}
      ${worstStutterWindow?.detailMode === 'summary' ? '' : worstStutterComponents.length === 0 ? '<p class="small-note">该窗口缺少 component_sample，无法进行模块归因。</p>' : `<div class="table-scroll"><table class="data-table compact">
        <thead><tr><th>#</th><th>Module</th><th>Phase</th><th>Samples</th><th>Total</th><th>Average</th><th>P95</th><th>Max</th><th>Max Frame</th></tr></thead>
        <tbody>${worstStutterComponents.map((component, index) => `<tr>
          <td class="num">${index + 1}</td><td class="mono">${escapeHtml(component.component)}</td><td>${escapeHtml(component.phase)}</td>
          <td class="num">${component.samples}</td><td class="num">${formatMs(component.totalMs)}</td><td class="num">${formatMs(component.avgMs)}</td>
          <td class="num">${formatMs(component.p95Ms)}</td><td class="num">${formatMs(component.maxMs)}</td><td class="num">${component.maxFrame}</td>
        </tr>`).join('')}</tbody>
      </table></div>`}
      ${worstStutterWindow?.detailMode === 'summary' ? '' : '<h3 class="mini-heading">采样慢帧的功能状态</h3>'}
      ${worstStutterWindow?.detailMode === 'summary' ? '' : worstStutterFeatures.length === 0 ? '<p class="small-note">该窗口没有启用的 qm_/tc_/cl_ 整数功能开关。</p>' : `<div class="table-scroll"><table class="data-table compact">
        <thead><tr><th>Owner</th><th>Feature</th><th>Value</th><th>HUD</th><th>Settings</th><th>Executed</th></tr></thead>
        <tbody>${worstStutterFeatures.map(feature => `<tr><td>${escapeHtml(feature.owner)}</td><td class="mono">${escapeHtml(feature.name)}</td><td class="num">${feature.value}</td><td>${feature.hudVisible < 0 ? 'unknown' : feature.hudVisible}</td><td>${feature.settingsVisible < 0 ? 'unknown' : feature.settingsVisible}</td><td>${feature.executedInFrame < 0 ? 'unknown' : feature.executedInFrame}</td></tr>`).join('')}</tbody>
      </table></div>`}
      <p class="small-note">组件墙钟时间衡量 CPU 提交成本，不是逐模块 GPU 时间。graphics_swap 偏高只能指向 GPU、驱动或 VSync 压力；未覆盖的输入、消息、snapshot 和后台线程成本属于未归因部分。功能配置、当前页面和组件耗时来自同一最慢帧窗口；通用组件没有可靠可见性接口时，HUD/Settings/Executed 显示 unknown，不根据耗时猜测。</p>
    </div>`;

  const configuration = configurationSummary(entries);
  configuration.incomplete ||= diagnostics.configurationIncomplete ?? false;
  const dataJson = JSON.stringify({
    configuration,
    percentiles: percentilesToChartData(p),
    timeline: ts,
    spikes: spikes.slice(0, 20),
    histogram: histData,
    kde: kde(frameDurations),
    qq: qqNorm(frameDurations),
    qqLine: { x1: 0, y1: 0, x2: p.max, y2: p.max },
    pages: pages.map(pg => ({ page: pg.page, count: pg.count, avg: pg.avg, max: pg.max, p95: pg.p95, boxPlot: pg.boxPlot, outliers: pg.outliers.slice(0, 50) })),
    interactions: interactionEntries.map(e => ({ timestamp: e.timestamp, event: e.fields.event ?? '', page: e.fields.page ?? '', frame: e.fields.frame ?? '', visibleRows: e.fields.visible_rows ?? '', firstVisibleSkin: e.fields.first_visible_skin ?? '' })),
    fpsSummaries: fps,
    coldTabSwitchFps,
    warmTabSwitchFps,
    targetSettings,
    textAnalysis: textAnalysisForData,
    assetsPreviewAdmission,
    assetsVisibleReady,
    previewBudget,
    adaptiveBudget,
    settingsUiBudget,
    textRuntimeBudget,
    budgetCorrelation,
    stutterDiagnostics: {
      ...stutterDiagnostics,
      windows: stutterDiagnostics.windows.slice(0, 50),
      components: stutterDiagnostics.components.slice(0, 1000),
      stages: stutterDiagnostics.stages.slice(0, 500),
      featureStates: stutterDiagnostics.featureStates.slice(0, 1000),
    },
    attribution,
    sectionTop,
    skinUx: skinUxEntries.map(e => ({ timestamp: e.timestamp, event: e.fields.event ?? '', durMs: e.fields.dur_ms ?? e.fields.duration_ms ?? '', total: e.fields.total ?? '', visibleRows: e.fields.visible_rows ?? '' })),
    devices: deviceEntries.map(e => ({ timestamp: e.timestamp, frame: e.fields.frame ?? '', gpuUtil: e.fields.gpu_util_percent ?? '', gpuDedicated: e.fields.gpu_dedicated_vram_mb ?? '', gpuShared: e.fields.gpu_shared_vram_mb ?? '', cpuProcess: e.fields.cpu_process_percent ?? '', cpuTotal: e.fields.cpu_total_percent ?? '', mem: e.fields.memory_process_mb ?? '', disk: e.fields.disk_read_mb_s ?? '' })),
  }).replace(/</g, '\\u003c');

  const kpiClass = (v: number, okThresh: number, warnThresh: number) =>
    v <= okThresh ? 'ok' : v <= warnThresh ? 'warn' : 'bad';
  const metricClass = (value: number, ok: number, warn: number) => p.count === 0 ? 'warn' : kpiClass(value, ok, warn);
  const metricValue = (value: number, digits = 1) => p.count === 0 ? 'N/A' : value.toFixed(digits);
  const complianceValue = (value: number) => p.count === 0 ? 'N/A' : value.toFixed(1);
  const complianceClass = (value: number, ok: number, warn: number) => p.count === 0 ? 'warn' : value >= ok ? 'ok' : value >= warn ? 'warn' : 'bad';

  const verdictClass = verdict === 'PASS' ? 'ok' : verdict === 'WARN' ? 'warn' : 'bad';
  const genDate = new Date().toISOString().slice(0, 19).replace('T', ' ');
  const generationDurationLabel = generationDurationMs === undefined ? 'N/A' : `${generationDurationMs.toFixed(1)}ms`;

  return `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>QmClient 性能分析报告 — ${escapeHtml(sourceFile)}</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Noto+Serif+SC:wght@400;600;700&family=Noto+Sans+SC:wght@300;400;500&family=IBM+Plex+Mono:wght@400;500&display=swap" rel="stylesheet">
<script src="https://cdn.jsdelivr.net/npm/echarts@5.5.0/dist/echarts.min.js"></script>
<style>
:root {
  --ink: #0a1f3d;
  --ink-rgb: 10,31,61;
  --paper: #faf9f7;
  --paper-rgb: 250,249,247;
  --paper-tint: #eae8e4;
  --accent: #2563eb;
  --accent-rgb: 37,99,235;
  --ok: #8FA89A;
  --ok-bg: #EFF4F1;
  --warn: #C4A77D;
  --warn-bg: #F7F2EB;
  --bad: #B5838D;
  --bad-bg: #F5EFF0;
  --hairline: rgba(10,31,61,0.09);
  --hairline-strong: rgba(10,31,61,0.16);
  --serif: 'Noto Serif SC', Georgia, 'Source Han Serif SC', 'SimSun', serif;
  --sans: 'Noto Sans SC', -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Microsoft YaHei UI', sans-serif;
  --mono: 'IBM Plex Mono', 'JetBrains Mono', Consolas, monospace;
  --max-w: 960px;
}
*{margin:0;padding:0;box-sizing:border-box}
html{font-size:15px;scroll-behavior:smooth}
body{background:var(--paper);color:var(--ink);font-family:var(--sans);font-weight:400;line-height:1.75;-webkit-font-smoothing:antialiased}

/* ── Title Page ── */
.title-page{max-width:var(--max-w);margin:0 auto;padding:6rem 2rem 3rem;border-bottom:1px solid var(--hairline-strong)}
.title-page h1{font-family:var(--serif);font-weight:700;font-size:2.4rem;line-height:1.3;letter-spacing:-0.02em}
.title-page .subtitle{font-family:var(--serif);font-size:1.15rem;color:rgba(var(--ink-rgb),0.5);margin-top:0.5rem}
.title-page .meta-grid{display:grid;grid-template-columns:auto 1fr;gap:0.25rem 1.2rem;margin-top:2rem;font-family:var(--mono);font-size:0.8rem;color:rgba(var(--ink-rgb),0.5)}
.title-page .meta-grid .label{text-transform:uppercase;letter-spacing:0.05em;font-weight:500}
.title-page .meta-grid .value{color:rgba(var(--ink-rgb),0.75)}

/* ── Abstract / Summary ── */
.abstract{max-width:var(--max-w);margin:0 auto;padding:2.5rem 2rem;border-bottom:1px solid var(--hairline)}
.abstract .kicker{font-family:var(--mono);font-size:0.7rem;text-transform:uppercase;letter-spacing:0.1em;color:rgba(var(--ink-rgb),0.4);margin-bottom:0.6rem}
.abstract p{font-size:0.95rem;line-height:1.85;color:rgba(var(--ink-rgb),0.72)}
.verdict-banner{display:inline-block;padding:0.15rem 0.7rem;border-radius:2px;font-family:var(--mono);font-size:0.7rem;font-weight:500;letter-spacing:0.04em;margin-left:0.5rem;vertical-align:middle}
.verdict-banner.ok{background:var(--ok-bg);color:var(--ok)}
.verdict-banner.warn{background:var(--warn-bg);color:var(--warn)}
.verdict-banner.bad{background:var(--bad-bg);color:var(--bad)}

/* ── Section ── */
.section{max-width:var(--max-w);margin:0 auto;padding:2.5rem 2rem;border-bottom:1px solid var(--hairline)}
.section-head{display:flex;align-items:baseline;gap:0.8rem;margin-bottom:1.5rem}
.section-num{font-family:var(--mono);font-size:0.7rem;font-weight:500;text-transform:uppercase;letter-spacing:0.08em;color:var(--accent);opacity:0.7}
.section-head h2{font-family:var(--serif);font-weight:700;font-size:1.35rem;letter-spacing:-0.01em}
.section p.body-text{font-size:0.92rem;line-height:1.85;color:rgba(var(--ink-rgb),0.68);margin-bottom:1rem}

/* ── KPI Cards ── */
.kpi-row{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:1rem}
.kpi-card{border:1px solid var(--hairline-strong);border-radius:2px;padding:1rem 1.2rem;text-align:center;background:white}
.kpi-card .kpi-label{font-family:var(--mono);font-size:0.65rem;text-transform:uppercase;letter-spacing:0.08em;color:rgba(var(--ink-rgb),0.4);margin-bottom:0.3rem}
.kpi-card .kpi-value{font-family:var(--serif);font-size:1.8rem;font-weight:700;letter-spacing:-0.02em;line-height:1.2}
.kpi-card .kpi-value.ok{color:var(--ok)}
.kpi-card .kpi-value.warn{color:var(--warn)}
.kpi-card .kpi-value.bad{color:var(--bad)}
.kpi-card .kpi-unit{font-family:var(--sans);font-size:0.7rem;color:rgba(var(--ink-rgb),0.35);margin-top:0.15rem}

/* ── Chart Figure ── */
.figure{margin-top:1rem}
.figure .chart-wrap{background:white;border:1px solid var(--hairline-strong);border-radius:2px;overflow:visible}
.figure .chart-inner{width:100%;height:320px}
.figure .chart-inner.tall{height:400px}
.figure .chart-inner.short{height:240px}
.figcaption{font-family:var(--mono);font-size:0.68rem;color:rgba(var(--ink-rgb),0.38);margin-top:0.6rem;padding-left:0.2rem}
.figcaption em{font-style:italic;color:rgba(var(--ink-rgb),0.52)}

/* ── Grid ── */
.chart-grid{display:grid;grid-template-columns:1fr 1fr;gap:1.5rem}

/* ── Data Table ── */
.table-scroll{width:100%;overflow-x:auto;overscroll-behavior-x:contain;border:1px solid var(--hairline-strong);background:white}
.table-scroll .data-table{margin-top:0;border:0}
.data-table{width:100%;border-collapse:collapse;font-size:0.85rem;margin-top:0.5rem;table-layout:auto}
.data-table.compact{table-layout:fixed}
.data-table thead th{font-family:var(--mono);font-size:0.63rem;font-weight:500;text-transform:uppercase;letter-spacing:0.06em;color:rgba(var(--ink-rgb),0.42);border-bottom:2px solid var(--hairline-strong);padding:0.5rem 0.65rem;text-align:left;white-space:nowrap}
.data-table tbody td{padding:0.5rem 0.65rem;border-bottom:1px solid var(--hairline);font-variant-numeric:tabular-nums;vertical-align:middle}
.data-table tbody tr:hover{background:rgba(var(--ink-rgb),0.02)}
.data-table .mono{font-family:var(--mono);font-size:0.78rem}
.data-table.compact{font-size:0.78rem;min-width:960px;table-layout:auto}
.data-table.fps-table{min-width:1080px}
.data-table .num{text-align:right;font-family:var(--mono);white-space:nowrap}
.key-cell{max-width:34rem;overflow-wrap:anywhere}
.sample-key-cell{max-width:30rem;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.sample-details{margin:1rem 0 0;border:1px solid var(--hairline-strong);background:rgba(255,255,255,0.62)}
.sample-details summary{cursor:pointer;padding:0.7rem 0.85rem;font-family:var(--mono);font-size:0.72rem;color:rgba(var(--ink-rgb),0.62);user-select:none}
.sample-details .table-scroll{border-left:0;border-right:0;border-bottom:0}
.diagnostic-note{overflow-wrap:anywhere}
.coverage-panel{margin:0.9rem 0 1.2rem;border:1px solid var(--hairline-strong);background:white;border-radius:2px;padding:0.85rem}
.coverage-title{display:flex;align-items:center;justify-content:space-between;gap:0.8rem;margin-bottom:0.65rem;font-family:var(--mono);font-size:0.72rem;text-transform:uppercase;letter-spacing:0.06em;color:rgba(var(--ink-rgb),0.55)}
.coverage-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(118px,1fr));gap:0.5rem}
.coverage-card{border:1px solid var(--hairline);background:rgba(var(--paper-rgb),0.48);padding:0.55rem 0.65rem;min-width:0}
.coverage-label{font-family:var(--mono);font-size:0.62rem;text-transform:uppercase;letter-spacing:0.05em;color:rgba(var(--ink-rgb),0.42);white-space:nowrap}
.coverage-value{font-family:var(--mono);font-size:0.92rem;font-weight:500;color:rgba(var(--ink-rgb),0.82);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.coverage-value.ok{color:var(--ok)}
.coverage-value.warn{color:var(--warn)}
.coverage-value.bad{color:var(--bad)}
.badge{display:inline-block;padding:0.1rem 0.5rem;border-radius:1px;font-family:var(--mono);font-size:0.63rem;font-weight:500}
.badge.ok{background:var(--ok-bg);color:var(--ok)}
.badge.warn{background:var(--warn-bg);color:var(--warn)}
.badge.bad{background:var(--bad-bg);color:var(--bad)}
.mini-heading{font-family:var(--mono);font-size:0.72rem;text-transform:uppercase;letter-spacing:0.06em;color:rgba(var(--ink-rgb),0.54);margin:0.9rem 0 0.45rem}
.stat-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:0.5rem}
.stat-grid.dense{grid-template-columns:repeat(auto-fit,minmax(132px,1fr))}
.stat-cell{border:1px solid var(--hairline);background:rgba(var(--paper-rgb),0.5);padding:0.52rem 0.62rem;min-width:0}
.stat-cell span{display:block;font-family:var(--mono);font-size:0.6rem;text-transform:uppercase;letter-spacing:0.05em;color:rgba(var(--ink-rgb),0.42);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.stat-cell strong{display:block;margin-top:0.18rem;font-family:var(--mono);font-size:0.9rem;font-weight:600;line-height:1.25;color:rgba(var(--ink-rgb),0.84);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.stat-cell strong.ok{color:var(--ok)}
.stat-cell strong.warn{color:var(--warn)}
.stat-cell strong.bad{color:var(--bad)}
.budget-window-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(330px,1fr));gap:0.8rem;margin-top:0.5rem}
.budget-window-card{border:1px solid var(--hairline-strong);background:rgba(var(--paper-rgb),0.32);padding:0.85rem;min-width:0}
.budget-window-card.bad{border-color:rgba(181,87,87,0.35);background:rgba(181,87,87,0.035)}
.budget-window-card.warn{border-color:rgba(176,124,62,0.35);background:rgba(176,124,62,0.035)}
.budget-card-head{display:grid;grid-template-columns:auto 1fr auto;gap:0.65rem;align-items:start;margin-bottom:0.65rem}
.budget-card-head .rank{font-family:var(--mono);font-size:0.7rem;color:rgba(var(--ink-rgb),0.42);padding-top:0.15rem}
.budget-card-head h3{font-family:var(--mono);font-size:0.85rem;font-weight:600;line-height:1.35;margin:0;overflow-wrap:anywhere}
.budget-card-head p{font-family:var(--mono);font-size:0.66rem;line-height:1.45;color:rgba(var(--ink-rgb),0.48);margin:0.1rem 0 0;overflow-wrap:anywhere}
.culprit-note{font-family:var(--mono);font-size:0.66rem;line-height:1.55;color:rgba(var(--ink-rgb),0.55);margin:0.65rem 0 0;overflow-wrap:anywhere}
.distribution-grid{display:grid;grid-template-columns:1fr 1fr;gap:1rem}

/* ── Methodology ── */
.methodology p{font-size:0.88rem;line-height:1.8;color:rgba(var(--ink-rgb),0.62);margin-bottom:0.8rem}
.methodology code{font-family:var(--mono);font-size:0.82rem;background:rgba(var(--ink-rgb),0.05);padding:0.1em 0.4em;border-radius:2px}

/* ── Footer ── */
.report-footer{max-width:var(--max-w);margin:0 auto;padding:1.5rem 2rem 3rem;font-family:var(--mono);font-size:0.63rem;color:rgba(var(--ink-rgb),0.28);text-transform:uppercase;letter-spacing:0.05em}

/* ── Comparison ── */
.compare-section{max-width:var(--max-w);margin:0 auto;padding:2rem 2rem;border-bottom:1px solid var(--hairline)}
.compare-grid{display:grid;grid-template-columns:1fr 1fr;gap:0.8rem;margin-top:0.8rem}
.delta-card{border:1px solid var(--hairline-strong);border-radius:2px;padding:0.6rem 1rem;display:flex;justify-content:space-between;align-items:center;background:white}
.delta-card .delta-name{font-family:var(--mono);font-size:0.72rem;color:rgba(var(--ink-rgb),0.55)}
.delta-card .delta-values{display:flex;align-items:baseline;gap:0.5rem}
.delta-card .delta-before{font-size:0.82rem;color:rgba(var(--ink-rgb),0.4)}
.delta-card .delta-arrow{font-size:0.75rem}
.delta-card .delta-after{font-family:var(--serif);font-size:1rem;font-weight:600}
.delta-card .delta-after.better{color:var(--ok)}
.delta-card .delta-after.worse{color:var(--bad)}
.delta-card .delta-after.neutral{color:var(--ink)}
.delta-card .delta-pct{font-family:var(--mono);font-size:0.65rem;opacity:0.6}
.delta-narrative{font-size:0.9rem;line-height:1.7;color:rgba(var(--ink-rgb),0.68);margin-top:0.8rem;padding:0.6rem 0.8rem;background:rgba(var(--ink-rgb),0.02);border-radius:2px}

@media print{html{font-size:11pt}.chart-wrap{break-inside:avoid}.section{break-inside:avoid}}
@media(max-width:700px){
  .title-page{padding:3rem 1.2rem 2rem}
  .title-page h1{font-size:1.6rem}
  .section{padding:1.5rem 1.2rem}
  .chart-grid{grid-template-columns:1fr}
  .distribution-grid{grid-template-columns:1fr}
  .budget-window-grid{grid-template-columns:1fr}
  .kpi-row{grid-template-columns:repeat(2,1fr)}
}
</style>
</head>
<body>

<header class="title-page">
  <h1>QmClient 设置页性能分析报告</h1>
  <div class="subtitle">Client Performance Diagnostics & Configuration</div>
  <div class="meta-grid">
    <span class="label">Date</span><span class="value">${genDate}</span>
    <span class="label">Source</span><span class="value">${escapeHtml(sourceFile.replace(/^.*[\\/]/, ''))}</span>
    <span class="label">Verdict</span><span class="value"><span class="verdict-banner ${verdictClass}">${verdict}</span></span>
    <span class="label">Total Frames</span><span class="value">${allEntries.length}</span>
    <span class="label">Menu Frames</span><span class="value">${menuEntries.length}</span>
    <span class="label">Quality</span><span class="value">${quality.warnings.length === 0 ? 'OK' : `${quality.warnings.length} warning(s)`}</span>
    <span class="label">Spikes</span><span class="value">${spikes.length} (&gt;16.67ms)</span>
    <span class="label">Chart Data</span><span class="value chart-sampled-note">sampled ${ts.times.length}/${allEntries.length} frames</span>
    <span class="label">Report Generation</span><span class="value">${generationDurationLabel}</span>
  </div>
  ${biased ? `<div style="max-width:var(--max-w);margin:0 auto;padding:1rem 2rem;border-bottom:1px solid var(--hairline);font-family:var(--mono);font-size:0.75rem;color:var(--warn);background:var(--warn-bg)">
    ⚠ Sampling Bias Detected — 当前采样阈值估计 p5=${samplingThresholdMs.toFixed(1)}ms（旧版过滤阈值约 4ms），日志可能仅包含超过阈值的帧，合规率和百分位统计不能反映实际帧分布。建议设置 <code style="background:rgba(0,0,0,0.06);padding:0.1em 0.3em;border-radius:2px">qm_perf_debug 1</code> 后重新采集。
  </div>` : ''}
</header>

<div class="abstract">
  <div class="kicker">Executive Summary</div>
  <p>${escapeHtml(narrative)}</p>
</div>

<section class="section">
  <div class="section-head">
    <span class="section-num">quality</span>
    <h2>样本可信度</h2>
  </div>
  <table class="data-table">
    <tbody>
      <tr><td class="mono">Frame samples</td><td>${quality.sampleCount}</td></tr>
      <tr><td class="mono">Parsed entries</td><td>${quality.totalEntries}</td></tr>
      <tr><td class="mono">Invalid lines</td><td>${quality.invalidLines}</td></tr>
      <tr><td class="mono">Frame samples omitted</td><td>${quality.sampledFrameEntries ?? 'N/A'}</td></tr>
      <tr><td class="mono">Detail samples omitted</td><td>${quality.sampledDetailEntries ?? 'N/A'}</td></tr>
      <tr><td class="mono">Window events omitted</td><td>${quality.droppedEventEntries ?? 'N/A'}</td></tr>
      <tr><td class="mono">Sampling p5 estimate</td><td>${quality.samplingThresholdMs.toFixed(1)}ms</td></tr>
      <tr><td class="mono">Pages</td><td>${escapeHtml(quality.operation.pages.join(', ') || 'N/A')}</td></tr>
      <tr><td class="mono">Systems</td><td>${escapeHtml(quality.operation.systems.join(', ') || 'N/A')}</td></tr>
    </tbody>
  </table>
  ${quality.warnings.length === 0 ? '<p class="body-text">当前样本未发现明显采样或解析风险。</p>' : `<p class="body-text">${escapeHtml(quality.warnings.join('；'))}</p>`}
</section>

<section class="section">
  <div class="section-head">
    <span class="section-num">text</span>
    <h2>文本池 Miss 分析</h2>
  </div>
  <p class="body-text">${escapeHtml(textAnalysisNarrative)}</p>
  ${stableTextCoverageHtml}
  ${textAnalysis.eventTimeline.length === 0 ? '<p class="body-text" style="color:rgba(var(--ink-rgb),0.4);font-style:italic">本次日志未包含 settings_text_* 事件。</p>' : `
  <div class="chart-grid">
    <div class="figure">
      <div class="chart-wrap"><div id="chart-text-location-top" class="chart-inner short"></div></div>
      <div class="figcaption"><em>Figure T1.</em> page/tab/subtab 维度的 miss / stale Top-N。</div>
    </div>
    <div class="figure">
      <div class="chart-wrap"><div id="chart-text-prebuild" class="chart-inner short"></div></div>
      <div class="figcaption"><em>Figure T2.</em> prebuild built / reused / remaining。</div>
    </div>
  </div>
  <div class="chart-grid">
    <div class="figure">
      <div class="chart-wrap"><div id="chart-text-reason-top" class="chart-inner short"></div></div>
      <div class="figcaption"><em>Figure T3.</em> reason 热点排行。</div>
    </div>
    <div class="figure">
      <div class="chart-wrap"><div id="chart-text-operation-top" class="chart-inner short"></div></div>
      <div class="figcaption"><em>Figure T4.</em> operation 热点排行。</div>
    </div>
  </div>
  <div class="figure">
    <div class="chart-wrap"><div id="chart-text-timeline" class="chart-inner short"></div></div>
    <div class="figcaption"><em>Figure T5.</em> miss / stale / prebuild 事件时间线。</div>
  </div>
  <table class="data-table">
    <thead><tr><th>Timestamp</th><th>Event</th><th>Page</th><th>Tab</th><th>Subtab</th><th>Reason</th><th>Plan Status</th><th>Operation</th><th>Built</th><th>Reused</th><th>Remaining</th><th>Key</th></tr></thead>
    <tbody>
      ${textAnalysis.eventTimeline.slice(-20).map(event => `<tr>
        <td class="mono">${escapeHtml(event.timestamp.slice(11, 19))}</td>
        <td class="mono">${escapeHtml(event.event)}</td>
        <td>${escapeHtml(event.page)}</td>
        <td class="mono">${escapeHtml(event.tab)}</td>
        <td class="mono">${escapeHtml(event.subtab)}</td>
        <td>${escapeHtml(event.reason)}</td>
        <td class="mono">${escapeHtml(event.planStatus)}</td>
        <td class="mono">${escapeHtml(event.operation)}</td>
        <td class="num">${event.built}</td>
        <td class="num">${event.reused}</td>
        <td class="num">${event.remaining}</td>
        <td class="mono key-cell" title="${escapeHtml(event.key)}">${escapeHtml(truncateMiddle(event.key, 72))}</td>
      </tr>`).join('')}
    </tbody>
  </table>`}
</section>

<section class="section">
  <div class="section-head">
    <span class="section-num">fps</span>
    <h2>FPS 摘要</h2>
  </div>
  ${fpsBaselineHtml}
  ${fps.length === 0 ? '<p class="body-text" style="color:var(--bad)">缺少 fps_summary；目标操作窗口样本不足以验收。请重新采集设置页进入、设置页切 tab、子 tab、Tee 滚动、游戏中 Esc 打开菜单。</p>' : `<div class="table-scroll"><table class="data-table fps-table">
    <thead><tr><th>Operation</th><th>Context</th><th>Page</th><th>Tab</th><th>Frames</th><th>FPS Avg</th><th>FPS Min</th><th>1% Low</th><th>FPS Max</th><th>Frame Avg</th><th>Frame P95</th><th>Frame P99</th><th>Frame Max</th><th>Menu Max</th><th>Cap</th></tr></thead>
    <tbody>
      ${fps.map(s => `<tr>
        <td class="mono">${escapeHtml(s.operation)}</td>
        <td>${escapeHtml(s.context)}</td>
        <td>${escapeHtml(s.page)}</td>
        <td class="mono">${escapeHtml(s.tab)}</td>
        <td class="num">${s.sampleFrames}</td>
        <td class="num">${s.fpsAvg.toFixed(1)}</td>
        <td class="num">${s.fpsMin.toFixed(1)}</td>
        <td class="num ${s.fpsOnePctLowAvailable && s.fpsOnePctLow >= 240 ? 'ok' : s.fpsOnePctLowAvailable && s.fpsOnePctLow >= 120 ? 'warn' : 'bad'}">${s.fpsOnePctLow.toFixed(1)}${s.fpsOnePctLowSource === 'real_sampled' ? '' : ` (${escapeHtml(s.fpsOnePctLowSource)})`}</td>
        <td class="num">${s.fpsMax.toFixed(1)}</td>
        <td class="num">${s.frameMsAvg.toFixed(1)}ms</td>
        <td class="num">${s.frameMsP95.toFixed(1)}ms</td>
        <td class="num">${s.frameMsP99.toFixed(1)}ms</td>
        <td class="num">${s.frameMsMax.toFixed(1)}ms</td>
        <td class="num">${s.menuMsMax.toFixed(1)}ms</td>
        <td>${s.capLimited ? '<span class="badge warn">cap/vsync</span>' : '<span class="badge ok">free</span>'}</td>
      </tr>`).join('')}
    </tbody>
  </table></div>`}
  <p class="body-text">目标设置页判定只使用 settings / ingame Esc 操作窗口相关样本；internet/offline、demo browser、server browser 和 work_drain 高耗时只作为背景热点，不参与清除 stable-text blocker。当前目标判定：<span class="badge ${targetSettingsVerdictClass}">${targetSettingsVerdictLabel}</span>，spikes=${targetSettings.spikeCount}，p99=${targetSettings.verdictAvailable ? targetSettings.percentiles.p99.toFixed(1) + 'ms' : 'N/A'}。</p>
  <p class="body-text diagnostic-note">${escapeHtml(stableTextNarrative)}</p>
  ${stableTextSamplesHtml}
</section>

<section class="section">
  <div class="section-head">
    <span class="section-num">stutter</span>
    <h2>客户端卡顿诊断</h2>
  </div>
  ${stutterDiagnosticsHtml}
  ${configurationReport(configuration)}
</section>

${renderMapPreparation(entries)}

<section class="section">
  <div class="section-head">
    <span class="section-num">assets</span>
    <h2>资源页首屏 Admission</h2>
  </div>
  ${budgetCorrelationHtml}
  ${assetsDrawDistributionHtml}
  ${textRuntimeDistributionHtml}
  ${settingsUiBudgetHtml}
  ${nonCardMenuHtml}
  ${textRuntimeBudgetHtml}
  ${adaptiveBudgetHtml}
  ${coldWarmTabSwitchHtml}
  ${previewBudgetHtml}
  ${assetsPreviewAdmissionHtml}
  ${assetsVisibleReadyHtml}
</section>

<section class="section">
  <div class="section-head">
    <span class="section-num">demo</span>
    <h2>Demo Browser</h2>
  </div>
  ${!demoBrowser.available ? '<p class="body-text" style="color:rgba(var(--ink-rgb),0.4);font-style:italic">本次日志未包含 demo browser 启动/header/date 分阶段事件。</p>' : `<div class="coverage-panel">
    <div class="coverage-title">
      <span>Demo Browser Phases</span>
      <span class="badge ${demoBrowser.maxRemaining > 0 || demoBrowser.maxMetadataRemaining > 0 ? 'warn' : 'ok'}">${demoBrowser.maxRemaining > 0 || demoBrowser.maxMetadataRemaining > 0 ? 'INCREMENTAL' : 'COMPLETE'}</span>
    </div>
    <div class="coverage-grid">
      <div><strong>${demoBrowser.startupCount}</strong><span>startup</span></div>
      <div><strong>${demoBrowser.headerFetchCount}</strong><span>header fetch</span></div>
      <div><strong>${demoBrowser.dateFetchCount}</strong><span>date fetch</span></div>
      <div><strong>${demoBrowser.visibleScanned}</strong><span>visible scanned</span></div>
      <div><strong>${demoBrowser.visibleDone}</strong><span>visible done</span></div>
      <div><strong>${demoBrowser.backgroundScanned}</strong><span>background scanned</span></div>
      <div><strong>${demoBrowser.backgroundDone}</strong><span>background done</span></div>
      <div><strong>${demoBrowser.maxRemaining}</strong><span>max remaining</span></div>
      <div><strong>${demoBrowser.maxMetadataRemaining}</strong><span>metadata remaining</span></div>
      <div><strong>${demoBrowser.maxDurationMs.toFixed(3)}ms</strong><span>max duration</span></div>
    </div>
    ${demoBrowser.samples.length === 0 ? '' : `<details class="sample-details"><summary>查看 demo browser 样本</summary><pre>${escapeHtml(demoBrowser.samples.join('\n'))}</pre></details>`}
  </div>`}
</section>

${comparison ? `<section class="compare-section">
  <div class="section-head">
    <span class="section-num">vs</span>
    <h2>Session 对比分析</h2>
  </div>
  <p style="font-family:var(--mono);font-size:0.75rem;color:rgba(var(--ink-rgb),0.4)">
    基线: ${escapeHtml(basename(comparison.previous.file))} (${comparison.previous.totalFrames} 帧)
    &nbsp;|&nbsp; 判定: <span class="badge ${comparison.previous.verdict.toLowerCase() === 'pass' ? 'ok' : comparison.previous.verdict.toLowerCase() === 'warn' ? 'warn' : 'bad'}">${comparison.previous.verdict}</span>
    → <span class="badge ${comparison.current.verdict.toLowerCase() === 'pass' ? 'ok' : comparison.current.verdict.toLowerCase() === 'warn' ? 'warn' : 'bad'}">${comparison.current.verdict}</span>
    ${comparison.verdictChanged ? '<span style="color:var(--bad);font-weight:600;margin-left:0.5rem">判定变化!</span>' : ''}
  </p>
  <p class="body-text">此对比基线为自动选择的上一份日志，可能不是同一操作路径或同一采样配置；用于快速观察趋势，不作为严格回归判定。</p>
  <p class="body-text">对比可信度：${comparison.operation.comparable ? 'same operation signature' : `advisory only - ${escapeHtml(comparison.operation.reason)}`}</p>
  <div class="compare-grid">
    ${comparison.metrics.map(m => `<div class="delta-card">
      <span class="delta-name">${m.name}</span>
      <span class="delta-values">
        <span class="delta-before">${m.before.toFixed(1)}</span>
        <span class="delta-arrow">${m.direction === 'better' ? '→' : m.direction === 'worse' ? '→' : '→'}</span>
        <span class="delta-after ${m.direction}">${m.after.toFixed(1)}</span>
        <span class="delta-pct">${m.direction === 'better' ? '↓' : m.direction === 'worse' ? '↑' : '—'}${Math.abs(m.changePercent).toFixed(0)}%</span>
      </span>
    </div>`).join('')}
    ${comparison.compliance.map(c => `<div class="delta-card">
      <span class="delta-name">${c.name}</span>
      <span class="delta-values">
        <span class="delta-before">${c.before.toFixed(1)}%</span>
        <span class="delta-arrow">→</span>
        <span class="delta-after ${c.direction}">${c.after.toFixed(1)}%</span>
        <span class="delta-pct">${c.direction === 'better' ? '↑' : c.direction === 'worse' ? '↓' : '—'}${Math.abs(c.changePercent).toFixed(0)}%</span>
      </span>
    </div>`).join('')}
  </div>
  <div class="delta-narrative">${escapeHtml(comparison.narrative)}</div>
</section>` : ''}

<section class="section">
  <div class="section-head">
    <span class="section-num">§1</span>
    <h2>关键性能指标</h2>
  </div>
  <div class="kpi-row">
    <div class="kpi-card"><div class="kpi-label">p50</div><div class="kpi-value ${metricClass(p.p50, 4, 8)}">${metricValue(p.p50)}</div><div class="kpi-unit">ms · median</div></div>
    <div class="kpi-card"><div class="kpi-label">p95</div><div class="kpi-value ${metricClass(p.p95, 8, 16)}">${metricValue(p.p95)}</div><div class="kpi-unit">ms</div></div>
    <div class="kpi-card"><div class="kpi-label">p99</div><div class="kpi-value ${metricClass(p.p99, BUDGET.h60, BUDGET.h60Double)}">${metricValue(p.p99)}</div><div class="kpi-unit">ms</div></div>
    <div class="kpi-card"><div class="kpi-label">Max</div><div class="kpi-value ${metricClass(p.max, BUDGET.h60, BUDGET.h60Double)}">${metricValue(p.max)}</div><div class="kpi-unit">ms · worst</div></div>
    <div class="kpi-card"><div class="kpi-label">240Hz 合规</div><div class="kpi-value ${complianceClass(compliance240, 95, 80)}">${complianceValue(compliance240)}</div><div class="kpi-unit">% ≤4.17ms</div></div>
    <div class="kpi-card"><div class="kpi-label">120Hz 合规</div><div class="kpi-value ${complianceClass(compliance120, 95, 80)}">${complianceValue(compliance120)}</div><div class="kpi-unit">% ≤8.33ms</div></div>
    <div class="kpi-card"><div class="kpi-label">60Hz 合规</div><div class="kpi-value ${complianceClass(compliance60, 99, 95)}">${complianceValue(compliance60)}</div><div class="kpi-unit">% ≤16.67ms</div></div>
  </div>
</section>

<section class="section">
  <div class="section-head">
    <span class="section-num">§2</span>
    <h2>描述统计</h2>
  </div>
  <p class="body-text">
    样本量 N=${p.count}，均值 ${metricValue(p.avg, 2)}ms，标准差 ${metricValue(p.std, 2)}ms。
    IQR (Q3−Q1) = ${metricValue(p.iqr, 2)}ms，反映中间 50% 数据的离散程度。
  </p>
  <table class="data-table">
    <thead><tr><th>Statistic</th><th>Value</th><th>Statistic</th><th>Value</th></tr></thead>
    <tbody>
      <tr><td class="mono">Min</td><td>${metricValue(p.min, 2)} ms</td><td class="mono">Max</td><td>${metricValue(p.max, 2)} ms</td></tr>
      <tr><td class="mono">Q1 (p25)</td><td>${metricValue(p.p25, 2)} ms</td><td class="mono">Q3 (p75)</td><td>${metricValue(p.p75, 2)} ms</td></tr>
      <tr><td class="mono">Median (p50)</td><td>${metricValue(p.p50, 2)} ms</td><td class="mono">IQR</td><td>${metricValue(p.iqr, 2)} ms</td></tr>
      <tr><td class="mono">Mean</td><td>${metricValue(p.avg, 2)} ms</td><td class="mono">Std Dev</td><td>${metricValue(p.std, 2)} ms</td></tr>
      <tr><td class="mono">p90</td><td>${metricValue(p.p90, 2)} ms</td><td class="mono">p95</td><td>${metricValue(p.p95, 2)} ms</td></tr>
      <tr><td class="mono">p99</td><td>${metricValue(p.p99, 2)} ms</td><td class="mono">Spikes</td><td>${spikes.length}</td></tr>
    </tbody>
  </table>
  <div class="figcaption"><em>Table 1.</em> 描述统计摘要。Spikes 定义为超过 16.67ms (60Hz 帧预算) 的帧数。</div>
</section>

<section class="section">
  <div class="section-head">
    <span class="section-num">§3</span>
    <h2>帧时间趋势</h2>
  </div>
  <div class="figure">
    <div class="chart-wrap"><div id="chart-timeline" class="chart-inner tall"></div></div>
    <div class="figcaption"><em>Figure 1.</em> 帧耗时时间序列（Y 轴对数刻度，兼顾正常帧与尖峰帧）。绿色虚线 = 8.33ms (120Hz)，红色虚线 = 16.67ms (60Hz)。红色标注 = 尖峰帧。可拖拽缩放查看细节。</div>
  </div>
</section>

<section class="section">
  <div class="section-head">
    <span class="section-num">§4</span>
    <h2>分布与百分位</h2>
  </div>
  <div class="chart-grid">
    <div class="figure">
      <div class="chart-wrap"><div id="chart-histogram" class="chart-inner short"></div></div>
      <div class="figcaption"><em>Figure 2.</em> 帧耗时分布直方图 + 核密度估计曲线 (KDE)。</div>
    </div>
    <div class="figure">
      <div class="chart-wrap"><div id="chart-qq" class="chart-inner short"></div></div>
      <div class="figcaption"><em>Figure 3.</em> QQ 图（分位数-分位数图）。偏离参考线 = 偏离正态分布。</div>
    </div>
  </div>
</section>

${pages.length > 1 ? `<section class="section">
  <div class="section-head">
    <span class="section-num">§5</span>
    <h2>页面级耗时分解</h2>
  </div>
  <div class="chart-grid">
    <div class="figure">
      <div class="chart-wrap"><div id="chart-boxplot" class="chart-inner short"></div></div>
      <div class="figcaption"><em>Figure 4.</em> 各页面帧耗时箱线图 (Box Plot)。箱体 = Q1→Q3，中线 = 中位数，须 = 1.5×IQR，圆点 = 离群值。</div>
    </div>
    <div class="figure">
      <div class="chart-wrap"><div id="chart-percentiles" class="chart-inner short"></div></div>
      <div class="figcaption"><em>Figure 5.</em> 关键百分位 (p50 / p90 / p95 / p99 / max)。</div>
    </div>
  </div>
</section>` : `<section class="section">
  <div class="section-head">
    <span class="section-num">§5</span>
    <h2>百分位分析</h2>
  </div>
  <div class="figure">
    <div class="chart-wrap"><div id="chart-percentiles" class="chart-inner short"></div></div>
    <div class="figcaption"><em>Figure 4.</em> 关键百分位 (p50 / p90 / p95 / p99 / max)。</div>
  </div>
</section>`}

<section class="section">
  <div class="section-head">
    <span class="section-num">§${pages.length > 1 ? '6' : '5'}</span>
    <h2>尖峰帧详细分析</h2>
  </div>
  ${spikes.length === 0 ? '<p class="body-text" style="color:rgba(var(--ink-rgb),0.4);font-style:italic">本次 session 未检测到超过 16.67ms 阈值的尖峰帧。</p>' : ''}
  ${spikes.length > 0 ? `<table class="data-table">
    <thead><tr><th>Timestamp</th><th>Stage</th><th>Page</th><th>Duration</th><th>Overrun</th><th>Severity</th></tr></thead>
    <tbody>
      ${spikes.slice(0, 20).map(s => {
        const sev = s.durationMs > 100 ? 'bad' : s.durationMs > 33 ? 'warn' : 'ok';
        const sevLabel = s.durationMs > 100 ? 'Critical' : s.durationMs > 33 ? 'Warning' : 'Minor';
        return `<tr>
          <td class="mono">${s.timestamp.slice(11,19)}</td>
          <td>${escapeHtml(s.stage)}</td>
          <td>${escapeHtml(s.page)}</td>
          <td style="font-weight:600;color:${s.durationMs>100?'var(--bad)':s.durationMs>33?'var(--warn)':'var(--ink)'}">${s.durationMs.toFixed(1)}ms</td>
          <td class="mono">${(s.durationMs/16.67).toFixed(1)}x</td>
          <td><span class="badge ${sev}">${sevLabel}</span></td>
        </tr>`;
      }).join('')}
    </tbody>
  </table>
  <div class="figcaption"><em>Table 2.</em> 超过 16.67ms 阈值的尖峰帧，按耗时降序，最多 20 条。</div>` : ''}
</section>

<section class="section">
  <div class="section-head">
    <span class="section-num">§${pages.length > 1 ? '7' : '6'}</span>
    <h2>页面性能归因</h2>
  </div>
  <p class="body-text">按页面切换、列表处理、UI rebuild 和 work drain 四类事件汇总长帧来源。这里不统计 FBO 命中率，也不把 FBO 作为页面性能判断入口。</p>
  ${attribution.length === 0 ? '<p class="body-text" style="color:rgba(var(--ink-rgb),0.4);font-style:italic">本次日志未包含 list_frame、section、ui_runtime 或 work_drain 归因事件。</p>' : `<table class="data-table">
    <thead><tr><th>Timestamp</th><th>Kind</th><th>Page</th><th>Duration</th><th>Summary</th><th>Details</th></tr></thead>
    <tbody>
      ${attribution.slice(0, 30).map(e => `<tr><td class="mono">${escapeHtml(e.timestamp.slice(11, 19))}</td><td>${escapeHtml(e.kind)}</td><td>${escapeHtml(e.page)}</td><td class="mono">${e.durationMs.toFixed(3)}ms</td><td class="mono">${escapeHtml(e.summary)}</td><td class="mono">${escapeHtml(e.details)}</td></tr>`).join('')}
    </tbody>
  </table>`}
</section>

<section class="section">
  <div class="section-head">
    <span class="section-num">§${pages.length > 1 ? '8' : '7'}</span>
    <h2>Section Top-10</h2>
  </div>
  <p class="body-text">按 <code>perf/section</code> 事件聚合局部 section 耗时，当前数据仍来自设置页局部包装采样，尚不是 <code>CSectionLoader::Process()</code> 的完整生命周期视图。</p>
  ${sectionTop.length === 0 ? '<p class="body-text" style="color:rgba(var(--ink-rgb),0.4);font-style:italic">本次日志未包含 perf/section 样本。</p>' : `<div class="figure">
    <div class="chart-wrap"><div id="chart-section-top" class="chart-inner short"></div></div>
    <div class="figcaption"><em>Figure ${pages.length > 1 ? '6' : '5'}.</em> Section Top-10，按 p95 降序。</div>
  </div>
  <table class="data-table">
    <thead><tr><th>Page</th><th>Section</th><th>Samples</th><th>Avg</th><th>p95</th><th>Max</th></tr></thead>
    <tbody>
      ${sectionTop.map(s => `<tr><td>${escapeHtml(s.page)}</td><td>${escapeHtml(s.section)}</td><td class="mono">${s.count}</td><td class="mono">${s.avg.toFixed(3)}ms</td><td class="mono">${s.p95.toFixed(3)}ms</td><td class="mono">${s.max.toFixed(3)}ms</td></tr>`).join('')}
    </tbody>
  </table>`}
</section>

<section class="section">
  <div class="section-head">
    <span class="section-num">§${pages.length > 1 ? '9' : '8'}</span>
    <h2>交互窗口</h2>
  </div>
  <p class="body-text">记录 Tee 页进入、滚动、点击、刷新等交互边界，供主线程帧时间与 UX 收敛事件做窗口切片。</p>
  ${interactionEntries.length === 0 ? '<p class="body-text" style="color:rgba(var(--ink-rgb),0.4);font-style:italic">本次日志未包含 perf/interaction 事件。</p>' : `<table class="data-table">
    <thead><tr><th>Timestamp</th><th>Event</th><th>Page</th><th>Frame</th><th>Visible</th><th>First Skin</th></tr></thead>
    <tbody>
      ${interactionEntries.slice(0, 20).map(e => `<tr><td class="mono">${escapeHtml(e.timestamp.slice(11, 19))}</td><td>${escapeHtml(e.fields.event ?? '')}</td><td>${escapeHtml(e.fields.page ?? '')}</td><td class="mono">${escapeHtml(String(e.fields.frame ?? ''))}</td><td class="mono">${escapeHtml(String(e.fields.visible_rows ?? ''))}</td><td>${escapeHtml(e.fields.first_visible_skin ?? '')}</td></tr>`).join('')}
    </tbody>
  </table>`}
</section>

<section class="section">
  <div class="section-head">
    <span class="section-num">§${pages.length > 1 ? '10' : '9'}</span>
    <h2>Tee 收敛</h2>
  </div>
  <p class="body-text">关注首个可见预览、全部可见预览、全列表完成的 UX 耗时，不把 source/load 队列状态误当成用户已经可见。</p>
  ${skinUxEntries.length === 0 ? '<p class="body-text" style="color:rgba(var(--ink-rgb),0.4);font-style:italic">本次日志未包含 perf/skin-ux 事件。</p>' : `<table class="data-table">
    <thead><tr><th>Timestamp</th><th>Event</th><th>Duration</th><th>Visible</th><th>Total</th></tr></thead>
    <tbody>
      ${skinUxEntries.slice(0, 20).map(e => `<tr><td class="mono">${escapeHtml(e.timestamp.slice(11, 19))}</td><td>${escapeHtml(e.fields.event ?? '')}</td><td class="mono">${escapeHtml(String(e.fields.dur_ms ?? e.fields.duration_ms ?? ''))}</td><td class="mono">${escapeHtml(String(e.fields.visible_rows ?? ''))}</td><td class="mono">${escapeHtml(String(e.fields.total ?? ''))}</td></tr>`).join('')}
    </tbody>
  </table>`}
</section>

<section class="section">
  <div class="section-head">
    <span class="section-num">§${pages.length > 1 ? '11' : '10'}</span>
    <h2>设备资源</h2>
  </div>
  <p class="body-text">汇总 GPU、VRAM、CPU、内存、磁盘读速率样本，用于判断加载慢时是否真的把设备资源吃满。</p>
  ${deviceEntries.length === 0 ? '<p class="body-text" style="color:rgba(var(--ink-rgb),0.4);font-style:italic">本次日志未包含 perf/device 事件。</p>' : `<table class="data-table">
    <thead><tr><th>Timestamp</th><th>Frame</th><th>GPU%</th><th>VRAM(Ded.)</th><th>VRAM(Shared)</th><th>CPU Proc.</th><th>CPU Total</th><th>Disk MB/s</th></tr></thead>
    <tbody>
      ${deviceEntries.slice(0, 20).map(e => `<tr><td class="mono">${escapeHtml(e.timestamp.slice(11, 19))}</td><td class="mono">${escapeHtml(String(e.fields.frame ?? ''))}</td><td class="mono">${escapeHtml(String(e.fields.gpu_util_percent ?? ''))}</td><td class="mono">${escapeHtml(String(e.fields.gpu_dedicated_vram_mb ?? ''))}</td><td class="mono">${escapeHtml(String(e.fields.gpu_shared_vram_mb ?? ''))}</td><td class="mono">${escapeHtml(String(e.fields.cpu_process_percent ?? ''))}</td><td class="mono">${escapeHtml(String(e.fields.cpu_total_percent ?? ''))}</td><td class="mono">${escapeHtml(String(e.fields.disk_read_mb_s ?? ''))}</td></tr>`).join('')}
    </tbody>
  </table>`}
</section>

<section class="section methodology">
  <div class="section-head">
    <span class="section-num">§${pages.length > 1 ? '12' : '11'}</span>
    <h2>数据采集方法</h2>
  </div>
  <p>性能数据通过 QmClient 内置的统一性能诊断系统；新日志的帧统计使用 <code>perf/frame</code> 完整渲染间隔，菜单和组件阶段仅用于归因采集，只需启用 <code>qm_perf_debug 1</code>。日志输出至 <code>%APPDATA%/DDNet/dumps/QmClient_Perf/</code>。</p>
  <p>帧预算基准：240Hz → 4.17ms，120Hz → 8.33ms，60Hz → 16.67ms。百分位采用最近秩法 (nearest-rank)。直方图分桶 [0, 2, 4, 8, 16, 33, 100, 500] ms。</p>
  ${biased ? `<p style="color:var(--warn)">当前采样阈值估计 p5=${samplingThresholdMs.toFixed(1)}ms（旧版日志的过滤行为）。日志可能仅包含超过阈值的帧，因此本报告中的合规率和百分位仅反映被采样帧的分布，不能代表实际渲染性能。统一诊断会批量记录完整渲染间隔；大日志分析阶段若再次抽样，会单独标记。</p>` : ''}
  <p>判定标准：p99 &lt; 16.67ms 且尖峰 &lt; 5 → <span class="badge ok">PASS</span>；p99 &lt; 33ms 或尖峰 &ge; 1 → <span class="badge warn">WARN</span>；p99 &ge; 33ms 或尖峰 &ge; 5 → <span class="badge bad">FAIL</span>。</p>
</section>

<footer class="report-footer">Generated by QmClient Perf Analyzer &mdash; ${genDate}</footer>

<script>
const DATA = ${dataJson};
document.getElementById('configuration-filter')?.addEventListener('input', event => {
  const query = event.target.value.toLocaleLowerCase();
  document.querySelectorAll('[data-config-row]').forEach(row => {
    row.hidden = !row.textContent.toLocaleLowerCase().includes(query);
  });
});

(function(){
  // ── Morandi Chart Theme ──
  const F = "'Noto Sans SC', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif";
  const ink = '#0a1f3d';
  const axisLbl = { color: '#8a8a8a', fontFamily: F, fontSize: 11 };
  const axisName = { color: '#a3a3a3', fontFamily: F, fontSize: 11 };
  const gridLine = { lineStyle: { color: '#e5e3df', type: 'dashed' } };
  const axisLine = { lineStyle: { color: '#d1cec8' } };
  const tooltipPosition = (point, params, dom, rect, size) => {
    const gap = 18;
    const margin = 12;
    const viewW = size.viewSize[0];
    const viewH = size.viewSize[1];
    const boxW = size.contentSize[0];
    const boxH = size.contentSize[1];
    let x = point[0] + gap;
    let y = point[1] - boxH - gap;
    if (x + boxW + margin > viewW) x = point[0] - boxW - gap;
    if (y < margin) y = point[1] + gap;
    if (y + boxH + margin > viewH) y = viewH - boxH - margin;
    return [Math.max(margin, x), Math.max(margin, y)];
  };
  const tooltipStyle = {
    backgroundColor: '#fdfcfa',
    borderColor: '#d1cec8',
    borderWidth: 1,
    confine: true,
    position: tooltipPosition,
    textStyle: { color: ink, fontFamily: F, fontSize: 12 },
    extraCssText: 'max-width:260px;white-space:normal;box-shadow:0 8px 22px rgba(10,31,61,0.14);pointer-events:none;z-index:30;',
  };
  const primaryColor = '#8B9DAF';
  const areaFill = { type: 'linear', x: 0, y: 0, x2: 0, y2: 1, colorStops: [{ offset: 0, color: 'rgba(139,157,175,0.10)' }, { offset: 1, color: 'rgba(139,157,175,0)' }] };
  const m = {
    // Morandi palette
    green: '#8FA89A',     // sage
    yellow: '#C4A77D',    // sand
    red: '#B5838D',       // dusty rose
    purple: '#9B8BB4',    // lavender
    blue: '#8B9DAF',      // steel blue
    blueLt: '#A5B5C7',    // light steel
  };

  // ── §3 Timeline ──
  const tl = echarts.init(document.getElementById('chart-timeline'));
  tl.setOption({
    textStyle: { fontFamily: F },
    backgroundColor: 'transparent',
    tooltip: { ...tooltipStyle, trigger: 'axis', valueFormatter: v => v.toFixed(2) + ' ms' },
    xAxis: { type: 'category', data: DATA.timeline.times, axisLabel: { show: false }, axisLine, axisTick: { show: false } },
    yAxis: { type: 'log', name: 'ms', nameTextStyle: axisName, axisLabel: { ...axisLbl, formatter: '{value}' }, splitLine: gridLine, minorSplitLine: { show: true, lineStyle: { color: '#edeae6', type: 'dashed' } } },
    dataZoom: [
      { type: 'inside', brushSelect: false },
      { type: 'slider', height: 18, bottom: 6, borderColor: '#d1d5db', fillerColor: 'rgba(59,89,152,0.1)', handleStyle: { color: primaryColor }, textStyle: { color: '#9ca3af', fontFamily: F, fontSize: 10 }, dataBackground: { lineStyle: { color: '#d1d5db' }, areaStyle: { color: '#e5e7eb' } } },
    ],
    series: [{
      type: 'line', data: DATA.timeline.durations, symbol: 'none',
      lineStyle: { color: primaryColor, width: 1.2 },
      areaStyle: { color: areaFill },
      markLine: {
        silent: true, symbol: 'none',
        label: { fontFamily: F, fontSize: 10, color: '#6b7280' },
        data: [
          { yAxis: 4.17, lineStyle: { color: m.purple, type: 'dotted', width: 1 }, label: { formatter: '4.17ms / 240Hz', position: 'insideStart', color: m.purple } },
          { yAxis: 8.33, lineStyle: { color: m.green, type: 'dashed', width: 1 }, label: { formatter: '8.33ms / 120Hz', position: 'insideStart', color: m.green } },
          { yAxis: 16.67, lineStyle: { color: m.red, type: 'dashed', width: 1 }, label: { formatter: '16.67ms / 60Hz', position: 'insideStart', color: m.red } },
        ],
      },
      markPoint: {
        data: DATA.spikes.slice(0, 5).map(s => ({
          coord: [s.index, s.durationMs],
          value: s.durationMs.toFixed(0) + 'ms',
          symbol: 'diamond', symbolSize: 10,
          itemStyle: { color: m.red },
          label: { show: true, color: m.red, fontFamily: F, fontSize: 10, fontWeight: 600, position: 'top' },
        })),
      },
    }],
    grid: { left: 50, right: 14, top: 14, bottom: 48 },
  });

  // ── §4 Histogram + KDE overlay ──
  const hist = echarts.init(document.getElementById('chart-histogram'));
  const histColors = [m.green, m.green, m.yellow, m.yellow, m.red, m.red, m.red];
  hist.setOption({
    textStyle: { fontFamily: F },
    backgroundColor: 'transparent',
    tooltip: { ...tooltipStyle, trigger: 'axis' },
    xAxis: { type: 'category', data: DATA.histogram.map(h => h.label), axisLabel: { ...axisLbl, fontSize: 10 }, axisLine },
    yAxis: [
      { type: 'value', name: 'frames', nameTextStyle: axisName, axisLabel: { ...axisLbl, fontSize: 10 }, splitLine: gridLine },
      { type: 'value', name: 'density', nameTextStyle: axisName, axisLabel: { show: false }, splitLine: { show: false } },
    ],
    series: [
      {
        type: 'bar', yAxisIndex: 0,
        data: DATA.histogram.map((h, i) => ({ value: h.count, itemStyle: { color: histColors[i] ?? m.red } })),
        barWidth: '55%',
      },
      {
        type: 'line', yAxisIndex: 1, smooth: true, symbol: 'none',
        data: DATA.kde ? DATA.kde.map(d => [String(d.x), d.y]) : [],
        lineStyle: { color: m.blue, width: 2 },
        areaStyle: { color: 'rgba(139,157,175,0.10)' },
      },
    ],
    grid: { left: 42, right: 10, top: 12, bottom: 26 },
  });

  // ── §4 QQ Plot ──
  const qqEl = document.getElementById('chart-qq');
  if (qqEl) {
    const qq = echarts.init(qqEl);
    const qqData = DATA.qq || [];
    const maxVal = qqData.length > 0 ? Math.max(qqData[qqData.length-1]?.theoretical||0, qqData[qqData.length-1]?.sample||0) : 1;
    qq.setOption({
      textStyle: { fontFamily: F },
      backgroundColor: 'transparent',
      tooltip: { ...tooltipStyle, formatter: p => \`Theoretical: \${p.data[0].toFixed(2)} ms<br>Sample: \${p.data[1].toFixed(2)} ms\` },
      xAxis: { type: 'value', name: 'Theoretical (ms)', nameTextStyle: axisName, axisLabel: { ...axisLbl, fontSize: 10 }, axisLine, splitLine: gridLine },
      yAxis: { type: 'value', name: 'Sample (ms)', nameTextStyle: axisName, axisLabel: { ...axisLbl, fontSize: 10 }, axisLine, splitLine: gridLine },
      series: [
        { type: 'scatter', data: qqData.map(d => [d.theoretical, d.sample]), symbolSize: 3, itemStyle: { color: m.blue, opacity: 0.6 } },
        { type: 'line', data: [[0, 0], [maxVal, maxVal]], lineStyle: { color: '#c4c4c4', type: 'dashed', width: 1 }, symbol: 'none' },
      ],
      grid: { left: 48, right: 10, top: 12, bottom: 30 },
    });
    window.addEventListener('resize', () => qq.resize());
  }

  // ── §5 Percentiles ──
  const pc = echarts.init(document.getElementById('chart-percentiles'));
  const pColors = [m.green, m.green, m.yellow, m.yellow, m.red];
  pc.setOption({
    textStyle: { fontFamily: F },
    backgroundColor: 'transparent',
    tooltip: { ...tooltipStyle, trigger: 'axis', valueFormatter: v => v.toFixed(2) + ' ms' },
    xAxis: { type: 'category', data: DATA.percentiles.map(p => p.name), axisLabel: axisLbl, axisLine },
    yAxis: { type: 'value', name: 'ms', nameTextStyle: axisName, axisLabel: { ...axisLbl, fontSize: 10 }, splitLine: gridLine },
    series: [{
      type: 'bar',
      data: DATA.percentiles.map((p, i) => ({ value: p.value, itemStyle: { color: pColors[i] ?? m.red } })),
      barWidth: '48%',
      markLine: {
        silent: true, symbol: 'none',
        label: { fontFamily: F, fontSize: 10 },
        data: [
          { yAxis: 4.17, lineStyle: { color: m.purple, type: 'dotted', width: 1 } },
          { yAxis: 8.33, lineStyle: { color: m.green, type: 'dashed', width: 1 } },
          { yAxis: 16.67, lineStyle: { color: m.red, type: 'dashed', width: 1 } },
        ],
      },
    }],
    grid: { left: 42, right: 10, top: 12, bottom: 26 },
  });

  // ── §5 Box Plot per Page ──
  const boxEl = document.getElementById('chart-boxplot');
  if (boxEl && DATA.pages && DATA.pages.length > 0) {
    const bx = echarts.init(boxEl);
    const pageNames = DATA.pages.map(p => p.page);
    const boxData = DATA.pages.map(p => p.boxPlot);
    const outlierData = [];
    DATA.pages.forEach((pg, i) => {
      (pg.outliers || []).forEach(o => outlierData.push({ value: [i, o] }));
    });
    bx.setOption({
      textStyle: { fontFamily: F },
      backgroundColor: 'transparent',
      tooltip: { ...tooltipStyle, trigger: 'item' },
      xAxis: { type: 'category', data: pageNames, axisLabel: { ...axisLbl, fontSize: 10 }, axisLine },
      yAxis: { type: 'log', name: 'ms', nameTextStyle: axisName, axisLabel: { ...axisLbl, fontSize: 10 }, splitLine: gridLine, minorSplitLine: { show: true, lineStyle: { color: '#edeae6', type: 'dashed' } } },
      series: [
        {
          name: 'boxplot', type: 'boxplot', data: boxData,
          itemStyle: { color: 'rgba(139,157,175,0.25)', borderColor: m.blue, borderWidth: 1 },
        },
        {
          name: 'outliers', type: 'scatter', data: outlierData,
          symbolSize: 4, itemStyle: { color: m.red, opacity: 0.7 },
        },
      ],
      grid: { left: 48, right: 10, top: 12, bottom: 26 },
    });
    window.addEventListener('resize', () => bx.resize());
  }

  const sectionEl = document.getElementById('chart-section-top');
  if (sectionEl && DATA.sectionTop && DATA.sectionTop.length > 0) {
    const sec = echarts.init(sectionEl);
    const sectionNames = DATA.sectionTop.map(s => s.page + ' / ' + s.section).reverse();
    const sectionValues = DATA.sectionTop.map(s => s.p95).reverse();
    sec.setOption({
      textStyle: { fontFamily: F },
      backgroundColor: 'transparent',
      tooltip: { ...tooltipStyle, trigger: 'axis', valueFormatter: v => v.toFixed(3) + ' ms' },
      xAxis: { type: 'value', name: 'p95 ms', nameTextStyle: axisName, axisLabel: { ...axisLbl, fontSize: 10 }, axisLine, splitLine: gridLine },
      yAxis: { type: 'category', data: sectionNames, axisLabel: { ...axisLbl, fontSize: 10 }, axisLine },
      series: [{
        type: 'bar',
        data: sectionValues,
        itemStyle: { color: m.yellow },
        barWidth: '55%',
      }],
      grid: { left: 130, right: 12, top: 12, bottom: 28 },
    });
    window.addEventListener('resize', () => sec.resize());
  }

  const textLocationEl = document.getElementById('chart-text-location-top');
  if (textLocationEl && DATA.textAnalysis && DATA.textAnalysis.topByLocation.length > 0) {
    const chart = echarts.init(textLocationEl);
    const rows = DATA.textAnalysis.topByLocation.slice(0, 12).reverse();
    chart.setOption({
      textStyle: { fontFamily: F },
      backgroundColor: 'transparent',
      tooltip: { ...tooltipStyle, trigger: 'axis' },
      xAxis: { type: 'value', name: 'count', nameTextStyle: axisName, axisLabel: { ...axisLbl, fontSize: 10 }, axisLine, splitLine: gridLine },
      yAxis: { type: 'category', data: rows.map(r => r.location), axisLabel: { ...axisLbl, fontSize: 10 }, axisLine },
      series: [
        { type: 'bar', name: 'miss', data: rows.map(r => r.missCount), itemStyle: { color: m.red }, barWidth: '35%' },
        { type: 'bar', name: 'stale', data: rows.map(r => r.staleCount), itemStyle: { color: m.yellow }, barWidth: '35%' },
      ],
      legend: { top: 2, textStyle: { fontFamily: F, fontSize: 10, color: '#6b7280' } },
      grid: { left: 170, right: 14, top: 26, bottom: 20 },
    });
    window.addEventListener('resize', () => chart.resize());
  }

  const textReasonEl = document.getElementById('chart-text-reason-top');
  if (textReasonEl && DATA.textAnalysis && DATA.textAnalysis.topByReason.length > 0) {
    const chart = echarts.init(textReasonEl);
    const rows = DATA.textAnalysis.topByReason.slice(0, 12).reverse();
    chart.setOption({
      textStyle: { fontFamily: F },
      backgroundColor: 'transparent',
      tooltip: { ...tooltipStyle, trigger: 'axis' },
      xAxis: { type: 'value', name: 'count', nameTextStyle: axisName, axisLabel: { ...axisLbl, fontSize: 10 }, axisLine, splitLine: gridLine },
      yAxis: { type: 'category', data: rows.map(r => r.reason), axisLabel: { ...axisLbl, fontSize: 10 }, axisLine },
      series: [
        { type: 'bar', name: 'miss', data: rows.map(r => r.missCount), itemStyle: { color: m.red }, barWidth: '35%' },
        { type: 'bar', name: 'stale', data: rows.map(r => r.staleCount), itemStyle: { color: m.yellow }, barWidth: '35%' },
      ],
      legend: { top: 2, textStyle: { fontFamily: F, fontSize: 10, color: '#6b7280' } },
      grid: { left: 120, right: 14, top: 26, bottom: 20 },
    });
    window.addEventListener('resize', () => chart.resize());
  }

  const textOperationEl = document.getElementById('chart-text-operation-top');
  if (textOperationEl && DATA.textAnalysis && DATA.textAnalysis.topByOperation.length > 0) {
    const chart = echarts.init(textOperationEl);
    const rows = DATA.textAnalysis.topByOperation.slice(0, 12).reverse();
    chart.setOption({
      textStyle: { fontFamily: F },
      backgroundColor: 'transparent',
      tooltip: { ...tooltipStyle, trigger: 'axis' },
      xAxis: { type: 'value', name: 'count', nameTextStyle: axisName, axisLabel: { ...axisLbl, fontSize: 10 }, axisLine, splitLine: gridLine },
      yAxis: { type: 'category', data: rows.map(r => r.operation), axisLabel: { ...axisLbl, fontSize: 10 }, axisLine },
      series: [
        { type: 'bar', name: 'miss', data: rows.map(r => r.missCount), itemStyle: { color: m.red }, barWidth: '35%' },
        { type: 'bar', name: 'stale', data: rows.map(r => r.staleCount), itemStyle: { color: m.yellow }, barWidth: '35%' },
      ],
      legend: { top: 2, textStyle: { fontFamily: F, fontSize: 10, color: '#6b7280' } },
      grid: { left: 130, right: 14, top: 26, bottom: 20 },
    });
    window.addEventListener('resize', () => chart.resize());
  }

  const textPrebuildEl = document.getElementById('chart-text-prebuild');
  if (textPrebuildEl && DATA.textAnalysis && DATA.textAnalysis.prebuildSeries.length > 0) {
    const chart = echarts.init(textPrebuildEl);
    const rows = DATA.textAnalysis.prebuildSeries.slice(-12);
    chart.setOption({
      textStyle: { fontFamily: F },
      backgroundColor: 'transparent',
      tooltip: { ...tooltipStyle, trigger: 'axis' },
      xAxis: { type: 'category', data: rows.map((r, i) => r.timestamp.slice(11, 19) || String(i + 1)), axisLabel: { ...axisLbl, fontSize: 10 }, axisLine },
      yAxis: { type: 'value', name: 'count', nameTextStyle: axisName, axisLabel: { ...axisLbl, fontSize: 10 }, axisLine, splitLine: gridLine },
      series: [
        { type: 'bar', name: 'built', data: rows.map(r => r.built), itemStyle: { color: m.green } },
        { type: 'bar', name: 'reused', data: rows.map(r => r.reused), itemStyle: { color: m.blue } },
        { type: 'bar', name: 'remaining', data: rows.map(r => r.remaining), itemStyle: { color: m.red } },
      ],
      legend: { top: 2, textStyle: { fontFamily: F, fontSize: 10, color: '#6b7280' } },
      grid: { left: 50, right: 14, top: 26, bottom: 20 },
    });
    window.addEventListener('resize', () => chart.resize());
  }

  const textTimelineEl = document.getElementById('chart-text-timeline');
  if (textTimelineEl && DATA.textAnalysis && DATA.textAnalysis.eventTimeline.length > 0) {
    const chart = echarts.init(textTimelineEl);
    const categoryIndex = { settings_text_usage: 3, settings_text_prebuild: 2, settings_text_stale: 1, settings_text_miss: 0 };
    const scatterData = DATA.textAnalysis.eventTimeline.map(e => ({
      value: [e.timestamp, categoryIndex[e.event] ?? 0, e.remaining],
      event: e.event,
      page: e.page,
      tab: e.tab,
      subtab: e.subtab,
      reason: e.reason,
      operation: e.operation,
      built: e.built,
      reused: e.reused,
      remaining: e.remaining,
    }));
    chart.setOption({
      textStyle: { fontFamily: F },
      backgroundColor: 'transparent',
      tooltip: {
        ...tooltipStyle,
        formatter: p => {
          const d = p.data;
          const extra = d.event === 'settings_text_prebuild'
            ? '<br>built=' + d.built + ' reused=' + d.reused + ' remaining=' + d.remaining
            : '';
          return d.event + '<br>' + d.page + ' / ' + d.tab + ' / ' + d.subtab +
            '<br>reason=' + d.reason + '<br>operation=' + d.operation + extra;
        },
      },
      xAxis: { type: 'category', data: DATA.textAnalysis.eventTimeline.map(e => e.timestamp.slice(11, 19)), axisLabel: { ...axisLbl, fontSize: 10 }, axisLine },
      yAxis: { type: 'category', data: ['miss', 'stale', 'prebuild', 'usage'], axisLabel: { ...axisLbl, fontSize: 10 }, axisLine, splitLine: gridLine },
      series: [{
        type: 'scatter',
        data: scatterData,
        symbolSize: p => p[2] > 0 ? 10 : 7,
        itemStyle: {
          color: params => {
            const event = params.data.event;
            if (event === 'settings_text_usage') return m.green;
            if (event === 'settings_text_prebuild') return m.blue;
            if (event === 'settings_text_stale') return m.yellow;
            return m.red;
          },
        },
      }],
      grid: { left: 60, right: 14, top: 14, bottom: 26 },
    });
    window.addEventListener('resize', () => chart.resize());
  }

  window.addEventListener('resize', () => { tl.resize(); hist.resize(); pc.resize(); });
})();
</script>
</body>
</html>`;
}
