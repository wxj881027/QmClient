// 请抬头享受阳光｜日子很好 我很我---------致咩子
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { parseLine, parseLog, parseLogWithDiagnostics } from './lib/parse.ts';
import { PerfLogCollector, expandFrameBatch } from './lib/stream.ts';
import { configurationSummary } from './lib/configuration.ts';
import { selectFrameTimeEntries } from './lib/stats.ts';
import {
  compareOperationSignatures,
  operationSignature,
} from './lib/quality_core.ts';
import {
  NON_CARD_MENU_BASELINES,
  nonCardMenuBudgetSummary,
  reportQuality,
  summarizeForBundle,
} from './lib/quality.ts';
import { generateReport } from './lib/report.ts';
import {
  compareSessions,
  computeVerdict,
  fpsSummaries,
  coldTabSwitchFpsSummaries,
  previewBudgetSummary,
  warmTabSwitchFpsSummaries,
  isSamplingBiased,
  isFrameTimeEntry,
  isListFrameEvent,
  isPageSwitchEvent,
  isUiRebuildEvent,
  isWorkDrainEvent,
  pagePerformanceAttribution,
  adaptiveBudgetSummary,
  budgetCorrelationSummary,
  settingsTextAnalysis,
  settingsUiBudgetSummary,
  stutterDiagnosticsSummary,
  textRuntimeBudgetSummary,
  snapshot,
} from './lib/stats.ts';

const FIXTURE_DIR = join(dirname(fileURLToPath(import.meta.url)), 'test');

function readFixture(name: string): string {
  return readFileSync(join(FIXTURE_DIR, name), 'utf-8');
}

function testParseKeepsEventOnlyPerfLines() {
  const line = '2026-06-04 12:00:00 I perf/interaction: event=scroll_begin frame=42 page=settings:tee visible_rows=8';
  const entry = parseLine(line);
  assert.ok(entry);
  assert.equal(entry.system, 'perf/interaction');
  assert.equal(entry.fields.event, 'scroll_begin');

  const entries = parseLog([
    line,
    '2026-06-04 12:00:01 I perf/skin-ux: event=first_visible_ready dur_ms=123.500 frame=45 page=settings:tee',
  ].join('\n'));
  assert.equal(entries.length, 2);
}

function testParseSupportsJsonLinesEvents() {
  const entry = parseLine('{"timestamp":"2026-06-04T12:00:02","system":"perf/device","event":"sample","frame":77,"gpu_util_percent":61.5}');
  assert.ok(entry);
  assert.equal(entry?.system, 'perf/device');
  assert.equal(entry?.fields.event, 'sample');
  assert.equal(entry?.fields.frame, 77);

  const prefixed = parseLine('2026-06-04 12:00:03 I perf/settings-invalidate: {"system":"perf/settings-invalidate","frame":88,"session":9,"reason":"config_hash_changed","text":1}');
  assert.ok(prefixed);
  assert.equal(prefixed?.system, 'perf/settings-invalidate');
  assert.equal(prefixed?.fields.reason, 'config_hash_changed');
  assert.equal(prefixed?.fields.frame, 88);
}

function testParseLogWithDiagnosticsCountsInvalidLines() {
  const result = parseLogWithDiagnostics([
    '2026-06-04 12:00:00 I perf/menu: stage=settings_page_content duration_ms=6.000 frame=10 page=settings:tee',
    'not a perf line',
    '',
    '{"timestamp":"2026-06-04T12:00:02","system":"perf/device","event":"sample","frame":77}',
    '{broken json',
  ].join('\n'));

  assert.equal(result.entries.length, 2);
  assert.equal(result.diagnostics.totalLines, 4);
  assert.equal(result.diagnostics.invalidLines, 2);
}

function testReportIncludesInteractionAndDeviceSections() {
  const entries = parseLog(readFixture('sample.log'));
  const html = generateReport(entries, 'sample.log', null);
  assert.match(html, /交互窗口/);
  assert.match(html, /Tee 收敛/);
  assert.match(html, /设备资源/);
  assert.match(html, /页面性能归因/);
  assert.match(html, /Section Top-10/);
}

function testReportShowsGenerationDuration() {
  const entries = parseLog(readFixture('sample.log'));
  const html = generateReport(entries, 'sample.log', null, undefined, 123.456);

  assert.match(html, /Report Generation/);
  assert.match(html, /123\.5ms/);
}

function testReportAttributesPagePerformanceEvents() {
  const entries = parseLog([
    '2026-06-04 12:00:00 I perf/interaction: event=page_switch page=settings from=general to=tee dur_ms=12.500 frame=10',
    '2026-06-04 12:00:01 I perf/interaction: event=list_frame page=server_browser items_total=1200 rows_visible=18 rows_processed=18 rows_skipped=1182 dur_ms=5.250 frame=11',
    '2026-06-04 12:00:02 I perf/section: event=section page=settings:tee section=identity dur_ms=4.750 visible=1 dirty=config text_new=2 text_reused=8 frame=12',
    '2026-06-04 12:00:03 I perf/settings-resource: event=work_drain page=settings:tee kind=upload count=4 bytes=8192 dur_ms=9.000 stop=budget frame=13',
    '2026-06-04 12:00:04 I perf/skin-ux: event=list_drain_summary page=settings:tee dur_ms=18.000 requested=5 pending=2 loading=1 loaded=10 frame=14',
  ].join('\n'));
  const html = generateReport(entries, 'qm_perf_attribution.log', null);
  assert.match(html, /页面性能归因/);
  assert.doesNotMatch(html, /Page Switch/);
  assert.match(html, /page_switch/);
  assert.match(html, /List Interaction/);
  assert.match(html, /UI Rebuild/);
  assert.match(html, /Work Drain/);
  assert.match(html, /server_browser/);
  assert.match(html, /rows_processed/);
  assert.match(html, /stop=budget/);
  assert.match(html, /kind=merge/);
  assert.match(html, /requested=5 pending=2 loading=1 loaded=10/);
}

function testServerBrowserListFrameAttributionUsesRowCounts() {
  const entries = parseLog([
    '2026-06-04 12:00:00 I perf/interaction: event=list_frame page=server_browser items_total=1200 rows_visible=18 rows_rendered=18 rows_iterated=1200 rows_skipped=1182 dur_ms=5.250 frame=11 source=server_browser',
  ].join('\n'));

  const attribution = pagePerformanceAttribution(entries);

  assert.equal(attribution.length, 1);
  assert.equal(attribution[0].kind, 'List Interaction');
  assert.match(attribution[0].summary, /rows_visible=18/);
  assert.match(attribution[0].details, /rows_rendered=18/);
  assert.match(attribution[0].details, /rows_iterated=1200/);
  assert.match(attribution[0].details, /rows_skipped=1182/);
}

function testQmUiRuntimeAttributionUsesDedicatedKind() {
  const entries = parseLog([
    '2026-06-10 12:00:00 I perf/ui_runtime: {"system":"perf/ui_runtime","frame":"1","session":"7","page":"settings:qmclient","event":"ui_runtime","operation":"settings_qmclient","nodes":"120","anim_ms":"0.200","active_anims":"3","queued_anims":"1","render_bridge_ms":"0.050","duration_ms":"0.400"}',
  ].join('\n'));

  const attribution = pagePerformanceAttribution(entries);

  assert.equal(attribution.length, 1);
  assert.equal(attribution[0].kind, 'UI Runtime');
  assert.equal(attribution[0].page, 'settings:qmclient');
  assert.match(attribution[0].summary, /operation=settings_qmclient/);
}

function testFpsSummaryTelemetryFeedsReportAndBundleSummary() {
  const entries = parseLog([
    '2026-06-11 02:00:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_open","context":"online","page":"settings:tee","tab":"none","sample_frames":30,"sample_seconds":0.500,"fps_avg":60.000,"fps_min":45.000,"fps_max":120.000,"fps_1pct_low":40.000,"fps_1pct_source":"real_sampled","window_start_frame":100,"window_end_frame":129,"frame_ms_avg":16.667,"frame_ms_p95":22.000,"frame_ms_p99":25.000,"frame_ms_max":25.000,"cap_limited":0}',
    '2026-06-11 02:00:00 I perf/menu: {"system":"perf/menu","frame":10,"stage":"settings_page_content","duration_ms":7.000,"page":"tee"}',
  ].join('\n'));

  const fps = fpsSummaries(entries);
  assert.equal(fps.length, 1);
  assert.equal(fps[0].operation, 'settings_open');
  assert.equal(fps[0].context, 'online');
  assert.equal(fps[0].fpsMin, 45);
  assert.equal(fps[0].fpsOnePctLow, 40);
  assert.equal(fps[0].fpsOnePctLowSource, 'real_sampled');
  assert.equal(fps[0].windowStartFrame, 100);
  assert.equal(fps[0].windowEndFrame, 129);
  assert.equal(fps[0].frameMsP99, 25);

  const summary = summarizeForBundle(entries, 'qm_perf_fps.log', { invalidLines: 0, totalLines: 2 });
  assert.equal(summary.fps.available, true);
  assert.equal(summary.fps.summaries[0].page, 'settings:tee');
  assert.equal(summary.fps.summaries[0].fpsOnePctLow, 40);
  assert.equal(summary.quality.warnings.some(w => /ingame\/online/i.test(w)), false);

  const html = generateReport(entries, 'qm_perf_fps.log', null);
  assert.match(html, /FPS 摘要/);
  assert.match(html, /FPS Avg/);
  assert.match(html, /FPS Min/);
  assert.match(html, /1% Low/);
  assert.match(html, /real_sampled/);
  assert.match(html, /FPS Max/);
  assert.match(html, /Frame Avg/);
  assert.match(html, /Frame Max/);
  assert.match(html, /settings_open/);
  assert.match(html, /60\.0/);
  assert.match(html, /45\.0/);
  assert.match(html, /40\.0/);
  assert.match(html, /120\.0/);
}

function testPerfAnalyzerReportsRealSampledOnePctLow() {
  const entries = parseLog([
    '2026-06-16 05:00:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_tab_switch","context":"cold","page":"settings:assets","tab":"none","sample_frames":100,"sample_seconds":0.200,"fps_avg":500.000,"fps_min":10.000,"fps_max":1000.000,"fps_1pct_low":10.000,"fps_1pct_source":"real_sampled","window_start_frame":200,"window_end_frame":299,"frame_ms_avg":2.000,"frame_ms_p95":1.000,"frame_ms_p99":1.000,"frame_ms_max":100.000,"cap_limited":0}',
  ].join('\n'));

  const fps = fpsSummaries(entries);
  assert.equal(fps.length, 1);
  assert.equal(fps[0].fpsOnePctLow, 10);
  assert.equal(fps[0].fpsOnePctLowAvailable, true);
  assert.equal(fps[0].fpsOnePctLowSource, 'real_sampled');
  assert.equal(fps[0].windowStartFrame, 200);
  assert.equal(fps[0].windowEndFrame, 299);

  const html = generateReport(entries, 'qm_perf_real_sampled_fps.log', null);
  assert.match(html, /real_sampled/);
  assert.doesNotMatch(html, /P99-derived/);
}

function testPreviewBudgetSummaryAndColdWarmTabSwitches() {
  const entries = parseLog([
    '2026-06-16 02:00:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_assets_tab_switch","context":"cold","page":"settings:assets","tab":"entity_bg","sample_frames":60,"sample_seconds":0.250,"fps_avg":300.000,"fps_min":120.000,"fps_max":900.000,"fps_1pct_low":220.000,"frame_ms_avg":3.333,"frame_ms_p95":4.000,"frame_ms_p99":4.545,"frame_ms_max":8.000,"cap_limited":0}',
    '2026-06-16 02:00:01 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_assets_tab_switch","context":"warm","page":"settings:assets","tab":"entity_bg","sample_frames":60,"sample_seconds":0.250,"fps_avg":600.000,"fps_min":400.000,"fps_max":1200.000,"fps_1pct_low":320.000,"frame_ms_avg":1.667,"frame_ms_p95":2.500,"frame_ms_p99":3.125,"frame_ms_max":4.000,"cap_limited":0}',
    '2026-06-16 02:00:02 I perf/assets: stage=assets_preview_draw_workshop_cards duration_ms=3.000 frame=10 tab=8 preview_jobs_started=0 preview_jobs_done=0 preview_uploads=0 preview_admissions=2 preview_artifact_ms=0.750 metadata_hydrate_ms=0.250 placeholder_count=4 ready_texture_count=6 visible_ready_ratio=0.600',
  ].join('\n'));

  const previewBudget = previewBudgetSummary(entries);
  assert.equal(previewBudget.available, true);
  assert.equal(previewBudget.previewJobsStarted, 0);
  assert.equal(previewBudget.previewJobsDone, 0);
  assert.equal(previewBudget.previewUploads, 0);
  assert.equal(previewBudget.previewAdmissions, 2);
  assert.equal(previewBudget.maxPreviewArtifactMs, 0.75);
  assert.equal(previewBudget.maxMetadataHydrateMs, 0.25);
  assert.equal(previewBudget.maxPlaceholderCount, 4);
  assert.equal(previewBudget.maxReadyTextureCount, 6);
  assert.equal(previewBudget.minVisibleReadyRatio, 0.6);

  assert.equal(coldTabSwitchFpsSummaries(entries).length, 1);
  assert.equal(warmTabSwitchFpsSummaries(entries).length, 1);

  const summary = summarizeForBundle(entries, 'qm_perf_preview_budget.log', { invalidLines: 0, totalLines: 3 });
  assert.equal(summary.previewBudget.available, true);
  assert.equal(summary.previewBudget.previewUploads, 0);
  assert.equal(summary.previewBudget.previewAdmissions, 2);

  const html = generateReport(entries, 'qm_perf_preview_budget.log', null);
  assert.match(html, /Cold\/Warm Tab Switch/);
  assert.match(html, /Preview Budget/);
  assert.match(html, /1% Low Target/);
  assert.match(html, /settings_assets_tab_switch/);
  assert.match(html, /visible_ready_ratio/);
}

function testTextRuntimeBudgetSummary() {
  const entries = parseLog([
    '2026-06-16 02:00:03 I perf/text: {"system":"perf/text","event":"text_runtime_budget","glyph_new":3,"glyph_uploads":6,"glyph_upload_ms":1.250,"text_container_new":4,"text_container_uploads":4,"text_container_upload_ms":0.750}',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_text_runtime.log', { invalidLines: 0, totalLines: 1 });
  assert.equal(summary.textRuntimeBudget.available, true);
  assert.equal(summary.textRuntimeBudget.glyphNew, 3);
  assert.equal(summary.textRuntimeBudget.glyphUploads, 6);
  assert.equal(summary.textRuntimeBudget.maxGlyphUploadMs, 1.25);
  assert.equal(summary.textRuntimeBudget.textContainerNew, 4);
  assert.equal(summary.textRuntimeBudget.textContainerUploads, 4);
  assert.equal(summary.textRuntimeBudget.maxTextContainerUploadMs, 0.75);

  const html = generateReport(entries, 'qm_perf_text_runtime.log', null);
  assert.match(html, /Text Pipeline/);
  assert.match(html, /Glyph New/);
  assert.match(html, /Container Uploads/);
}

function testStableTextCoverageExcludesDynamicTextFromStaticHitRate() {
  const entries = parseLog([
    '2026-06-16 01:59:59 I perf/settings-text: event=settings_text_plan_collection scope=target_settings operation=settings_open phase=before_target units_done=8 units_total=8 remaining=0 budget=1 complete=1 dirty=0',
    '2026-06-16 01:59:59 I perf/settings-text: event=settings_text_prebuild scope=target_settings operation=settings_open phase=before_target built=40 reused=12 remaining=0 budget=64',
    '2026-06-16 02:00:00 I perf/settings-text: event=settings_text_usage scope=target_settings page=settings:tee tab=appearance subtab=assets operation=settings_open frame=40 text_class=static_stable candidates=10 hits=10 reused=10 miss=0 stale=0 text_new=0 text_reused=10 planned=10 unplanned=0',
    '2026-06-16 02:00:01 I perf/settings-text: event=settings_text_usage scope=target_settings page=settings:assets tab=1 subtab=preview operation=settings_open frame=41 text_class=dynamic_snapshot candidates=9 hits=2 reused=2 miss=7 stale=0 text_new=0 text_reused=2 planned=0 unplanned=0',
    '2026-06-16 02:00:02 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_open","context":"online","page":"settings:tee","tab":"none","sample_frames":30,"sample_seconds":0.500,"fps_avg":500.000,"fps_min":210.000,"fps_max":1000.000,"fps_1pct_low":300.000,"fps_1pct_source":"real_sampled","window_start_frame":40,"window_end_frame":60,"frame_ms_avg":2.000,"frame_ms_p95":3.500,"frame_ms_p99":4.348,"frame_ms_max":8.000,"cap_limited":0}',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_text_classification.log', { invalidLines: 0, totalLines: 3 });
  const coverage = summary.targetSettings.stableTextCoverage as typeof summary.targetSettings.stableTextCoverage & {
    staticCandidateTotal: number;
    staticHitCount: number;
    staticHitRate: number;
    staticReuseRate: number;
    staticKeyMismatchCount: number;
    staticUnplannedVisibleCount: number;
    dynamicCandidateTotal: number;
    dynamicHitCount: number;
    dynamicHitRate: number;
    dynamicReuseCount: number;
    dynamicReuseRate: number;
  };

  assert.equal(coverage.acceptanceBlocked, false);
  assert.equal(coverage.candidateTotal, 10);
  assert.equal(coverage.hitCount, 10);
  assert.equal(coverage.hitRate, 100);
  assert.equal(coverage.staticCandidateTotal, 10);
  assert.equal(coverage.staticHitCount, 10);
  assert.equal(coverage.staticHitRate, 100);
  assert.equal(coverage.staticReuseRate, 100);
  assert.equal(coverage.staticKeyMismatchCount, 0);
  assert.equal(coverage.staticUnplannedVisibleCount, 0);
  assert.equal(coverage.dynamicCandidateTotal, 9);
  assert.equal(coverage.dynamicHitCount, 2);
  assert.equal(coverage.dynamicHitRate, 22.22222222222222);
  assert.equal(coverage.dynamicReuseCount, 2);
  assert.equal(coverage.dynamicReuseRate, 22.22222222222222);

  const html = generateReport(entries, 'qm_perf_text_classification.log', null);
  assert.match(html, /Static Stable Text Coverage/);
  assert.match(html, /Dynamic Snapshot Text Coverage/);
  assert.match(html, /Render-Ready Hit Rate/);
  assert.match(html, /Snapshot Hit Rate/);
}

function testUnifiedFrameSchedulerAndTextPipelineBudgetSummary() {
  const entries = parseLog([
    '2026-06-16 02:00:04 I perf/settings-resource: event=settings_adaptive_budget mode=idle reason=progress frame_ms_avg=2.000 frame_ms_p95=3.000 target_ms=4.167 visible_tokens=4 prefetch_tokens=2 background_tokens=1 gpu_upload_tokens=2 resource_upload_tokens=2 text_tokens=3 text_container_tokens=3 glyph_upload_tokens=2 paragraph_layout_tokens=1 demo_tokens=1 backlog=7 scroll=0 jump_scroll=0',
    '2026-06-16 02:00:05 I perf/text: {"system":"perf/text","event":"text_runtime_budget","glyph_new":3,"glyph_uploads":6,"glyph_rasterize_ms":0.500,"glyph_upload_ms":1.250,"text_container_new":4,"text_container_uploads":4,"text_container_create_ms":0.625,"text_container_upload_ms":0.750,"paragraph_layout_ms":1.500,"paragraph_cache_hit":2,"paragraph_cache_miss":1,"page":"game","operation":"ingame_server_info","frame":42}',
    '2026-06-16 02:00:06 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"ingame_server_info","context":"online","page":"game","tab":"server_info","sample_frames":60,"sample_seconds":0.250,"fps_avg":500.000,"fps_min":210.000,"fps_max":1000.000,"fps_1pct_low":230.000,"frame_ms_avg":2.000,"frame_ms_p95":3.500,"frame_ms_p99":4.348,"frame_ms_max":8.000,"cap_limited":0}',
  ].join('\n'));

  const adaptive = adaptiveBudgetSummary(entries);
  assert.equal(adaptive.available, true);
  assert.equal(adaptive.maxResourceUploadTokens, 2);
  assert.equal(adaptive.maxTextContainerTokens, 3);
  assert.equal(adaptive.maxGlyphUploadTokens, 2);
  assert.equal(adaptive.maxParagraphLayoutTokens, 1);

  const textRuntime = textRuntimeBudgetSummary(entries);
  assert.equal(textRuntime.available, true);
  assert.equal(textRuntime.maxGlyphRasterizeMs, 0.5);
  assert.equal(textRuntime.maxTextContainerCreateMs, 0.625);
  assert.equal(textRuntime.maxParagraphLayoutMs, 1.5);
  assert.equal(textRuntime.paragraphCacheHit, 2);
  assert.equal(textRuntime.paragraphCacheMiss, 1);

  const summary = summarizeForBundle(entries, 'qm_perf_unified_ui_budget.log', { invalidLines: 0, totalLines: 3 });
  assert.equal(summary.adaptiveBudget.maxResourceUploadTokens, 2);
  assert.equal(summary.textRuntimeBudget.maxParagraphLayoutMs, 1.5);

  const html = generateReport(entries, 'qm_perf_unified_ui_budget.log', null);
  assert.match(html, /UI Frame Scheduler/);
  assert.match(html, /Text Pipeline/);
  assert.match(html, /Paragraph Layout/);
  assert.match(html, /ingame_server_info/);
}

function testPerfAnalyzerReportsStaticSnapshotParagraphHitRates() {
  const entries = parseLog([
    '2026-06-16 04:00:10 I perf/text: {"system":"perf/text","event":"text_runtime_budget","operation":"settings_open","page":"settings:tee","tab":"appearance","frame":77,"glyph_new":4,"glyph_uploads":2,"glyph_rasterize_ms":0.300,"glyph_upload_ms":0.200,"text_container_new":1,"text_container_uploads":1,"text_container_create_ms":0.400,"text_container_upload_ms":0.250,"paragraph_layout_ms":0.600,"paragraph_budget_blocked":1,"snapshot_cache_hit":18,"snapshot_cache_miss":2,"static_stable_hit":120,"static_stable_miss":0,"paragraph_cache_hit":6,"paragraph_cache_miss":1}',
    '2026-06-16 04:00:11 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_open","context":"online","page":"settings:tee","tab":"appearance","sample_frames":120,"sample_seconds":0.500,"fps_avg":500.000,"fps_min":210.000,"fps_max":1000.000,"fps_1pct_low":300.000,"fps_1pct_source":"real_sampled","window_start_frame":70,"window_end_frame":90,"frame_ms_avg":2.000,"frame_ms_p95":3.500,"frame_ms_p99":4.348,"frame_ms_max":8.000,"cap_limited":0}',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_text_pipeline_rates.log', { invalidLines: 0, totalLines: 2 });
  const runtime = summary.textRuntimeBudget as typeof summary.textRuntimeBudget & {
    staticStableHitRate: number;
    snapshotCacheHitRate: number;
    paragraphCacheHitRate: number;
    staticStableHitCount: number;
    staticStableMissCount: number;
    snapshotCacheHit: number;
    snapshotCacheMiss: number;
  };

  assert.equal(runtime.available, true);
  assert.equal(runtime.staticStableHitCount, 120);
  assert.equal(runtime.staticStableMissCount, 0);
  assert.equal(runtime.staticStableHitRate, 100);
  assert.equal(runtime.snapshotCacheHit, 18);
  assert.equal(runtime.snapshotCacheMiss, 2);
  assert.equal(runtime.snapshotCacheHitRate, 90);
  assert.equal(runtime.paragraphCacheHitRate, 85.71428571428571);

  const html = generateReport(entries, 'qm_perf_text_pipeline_rates.log', null);
  assert.match(html, /Static Stable Text/);
  assert.match(html, /Snapshot Cache/);
  assert.match(html, /Paragraph Cache/);
  assert.match(html, /Static Stable Hit Rate/);
  assert.match(html, /Snapshot Cache Hit Rate/);
}

function testPerfAnalyzerCorrelatesOnePctLowWithTextAndResourceBudgets() {
  const entries = parseLog([
    '2026-06-16 03:00:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_assets_tab_switch","context":"cold","page":"settings:assets","tab":"entity_bg","sample_frames":120,"sample_seconds":0.500,"fps_avg":420.000,"fps_min":120.000,"fps_max":1000.000,"fps_1pct_low":180.000,"fps_1pct_source":"real_sampled","window_start_frame":70,"window_end_frame":90,"frame_ms_avg":2.381,"frame_ms_p95":3.900,"frame_ms_p99":5.556,"frame_ms_max":11.000,"cap_limited":0}',
    '2026-06-16 03:00:01 I perf/settings-resource: event=settings_adaptive_budget mode=frame_pressure reason=frame_pressure frame_ms_avg=5.556 frame_ms_p95=8.000 target_ms=4.167 visible_tokens=2 prefetch_tokens=0 background_tokens=0 gpu_upload_tokens=1 resource_upload_tokens=1 text_tokens=1 text_container_tokens=1 glyph_upload_tokens=1 paragraph_layout_tokens=0 demo_tokens=1 backlog=16 scroll=0 jump_scroll=0',
    '2026-06-16 03:00:02 I perf/assets: stage=assets_preview_draw_workshop_cards page=settings:assets operation=settings_assets_tab_switch duration_ms=2.500 frame=77 tab=8 preview_jobs_started=2 preview_jobs_done=1 preview_uploads=1 preview_admissions=3 preview_artifact_ms=2.000 metadata_hydrate_ms=0.500 placeholder_count=6 ready_texture_count=4 visible_ready_ratio=0.500',
    '2026-06-16 03:00:03 I perf/text: {"system":"perf/text","event":"text_runtime_budget","page":"game","operation":"ingame_server_info","frame":78,"glyph_new":4,"glyph_uploads":2,"glyph_rasterize_ms":0.300,"glyph_upload_ms":0.200,"text_container_new":1,"text_container_uploads":1,"text_container_create_ms":0.400,"text_container_upload_ms":0.250,"paragraph_layout_ms":0.000,"paragraph_budget_blocked":1,"paragraph_cache_hit":0,"paragraph_cache_miss":1}',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_budget_correlation.log', { invalidLines: 0, totalLines: 4 });
  assert.equal(summary.fps.summaries[0].fpsOnePctLow, 180);
  assert.equal(summary.adaptiveBudget.maxResourceUploadTokens, 1);
  assert.equal(summary.previewBudget.previewUploads, 1);
  assert.equal(summary.textRuntimeBudget.paragraphBudgetBlocked, 1);

  const html = generateReport(entries, 'qm_perf_budget_correlation.log', null);
  assert.match(html, /1% Low Target 240 FPS/);
  assert.match(html, /UI Frame Scheduler/);
  assert.match(html, /Preview Budget/);
  assert.match(html, /Text Pipeline/);
  assert.match(html, /Paragraph Budget Blocked/);
  assert.match(html, /180\.0/);
}

function testReportUsesStatisticalBudgetReportInsteadOfRawBudgetDump() {
  const entries = parseLog([
    '2026-06-16 03:00:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_assets_tab_switch","context":"cold","page":"settings:assets","tab":"entity_bg","sample_frames":120,"sample_seconds":0.500,"fps_avg":420.000,"fps_min":120.000,"fps_max":1000.000,"fps_1pct_low":180.000,"fps_1pct_source":"real_sampled","window_start_frame":70,"window_end_frame":90,"frame_ms_avg":2.381,"frame_ms_p95":3.900,"frame_ms_p99":5.556,"frame_ms_max":11.000,"cap_limited":0}',
    '2026-06-16 03:00:01 I perf/settings-resource: event=settings_adaptive_budget operation=settings_assets_tab_switch context=cold page=settings:assets tab=entity_bg frame=71 mode=frame_pressure resource_upload_tokens=1 text_container_tokens=1 glyph_upload_tokens=1 paragraph_layout_tokens=0',
    '2026-06-16 03:00:02 I perf/assets: stage=assets_preview_draw_workshop_cards page=settings:assets operation=settings_assets_tab_switch context=cold duration_ms=35.600 frame=77 tab=entity_bg preview_jobs_started=2 preview_jobs_done=1 preview_uploads=1 preview_admissions=3 preview_artifact_ms=2.000 metadata_hydrate_ms=0.500 placeholder_count=6 ready_texture_count=4 visible_ready_ratio=0.500 layout_text_ms=1.300 preview_draw_ms=26.100 thumb_scheduling_ms=8.600',
    '2026-06-16 03:00:03 I perf/text: {"system":"perf/text","event":"text_runtime_budget","page":"settings:assets","operation":"settings_assets_tab_switch","context":"cold","tab":"entity_bg","frame":78,"glyph_new":4,"glyph_uploads":2,"glyph_rasterize_ms":0.300,"glyph_upload_ms":0.200,"text_container_new":1,"text_container_uploads":1,"text_container_create_ms":0.400,"text_container_upload_ms":0.250,"paragraph_layout_ms":0.000,"paragraph_budget_blocked":1}',
  ].join('\n'));

  const html = generateReport(entries, 'qm_perf_report_layout.log', null);

  // Regression guard for the visual report: budget attribution must read like a
  // statistical report, not a raw log dump. The broken version rendered wide raw
  // rows that overlapped columns and wrapped long tokens vertically in browser.
  assert.match(html, /Budget Window Statistics/);
  assert.match(html, /Assets Draw Distribution/);
  assert.match(html, /budget-window-grid/);
  assert.match(html, /budget-window-card/);
  assert.match(html, /Top Culprit/);
  assert.match(html, /p95/);
  assert.doesNotMatch(html, /<summary>查看 budget correlation 样本<\/summary><pre>/);
  assert.doesNotMatch(html, /<summary>查看 assets admission 样本<\/summary><pre>/);
  assert.doesNotMatch(html, /查看 visible-ready 样本/);
}

function testBudgetCorrelationSummaryByFpsWindow() {
  const entries = parseLog([
    '2026-06-16 04:00:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"ingame_server_info","context":"cold","page":"game","tab":"server_info","sample_frames":120,"sample_seconds":0.500,"fps_avg":360.000,"fps_min":120.000,"fps_max":900.000,"fps_1pct_low":180.000,"fps_1pct_source":"real_sampled","window_start_frame":50,"window_end_frame":170,"frame_ms_avg":2.778,"frame_ms_p95":3.900,"frame_ms_p99":5.556,"frame_ms_max":12.000,"cap_limited":0}',
    '2026-06-16 04:00:01 I perf/settings-resource: event=settings_adaptive_budget operation=other_window page=other tab=other context=warm frame=49 mode=frame_pressure reason=frame_pressure resource_upload_tokens=99',
    '2026-06-16 04:00:01 I perf/settings-resource: event=settings_adaptive_budget operation=ingame_server_info page=game tab=server_info context=cold frame=51 mode=frame_pressure reason=frame_pressure frame_ms_avg=5.556 frame_ms_p95=8.000 target_ms=4.167 visible_tokens=1 prefetch_tokens=0 background_tokens=0 gpu_upload_tokens=1 resource_upload_tokens=1 text_tokens=1 text_container_tokens=1 glyph_rasterize_tokens=1 glyph_upload_tokens=1 paragraph_layout_tokens=0 metadata_layout_tokens=0 preview_artifact_tokens=0 texture_upload_tokens=1 card_draw_tokens=1 demo_tokens=0 backlog=8 scroll=0 jump_scroll=0',
    '2026-06-16 04:00:02 I perf/text: {"system":"perf/text","event":"text_runtime_budget","operation":"ingame_server_info","context":"cold","page":"game","tab":"server_info","frame":51,"glyph_new":9,"glyph_uploads":3,"glyph_rasterize_ms":0.600,"glyph_upload_ms":0.400,"text_container_new":2,"text_container_uploads":2,"text_container_create_ms":1.100,"text_container_upload_ms":0.700,"paragraph_layout_ms":2.400,"paragraph_budget_blocked":1,"paragraph_cache_hit":0,"paragraph_cache_miss":1}',
    '2026-06-16 04:00:03 I perf/assets: stage=assets_preview_draw_workshop_cards operation=ingame_server_info context=cold page=game tab=server_info duration_ms=1.500 frame=52 preview_jobs_started=1 preview_jobs_done=1 preview_uploads=2 preview_admissions=2 preview_artifact_ms=0.900 metadata_hydrate_ms=0.200 placeholder_count=1 ready_texture_count=2 visible_ready_ratio=0.750',
    '2026-06-16 04:00:04 I perf/text: {"system":"perf/text","event":"text_runtime_budget","operation":"ingame_server_info","context":"cold","page":"game","tab":"server_info","frame":171,"glyph_new":90,"glyph_uploads":30,"glyph_rasterize_ms":90.000,"glyph_upload_ms":40.000,"text_container_new":20,"text_container_uploads":20,"text_container_create_ms":80.000,"text_container_upload_ms":70.000,"paragraph_layout_ms":60.000,"paragraph_budget_blocked":1,"paragraph_cache_hit":0,"paragraph_cache_miss":1}',
  ].join('\n'));

  const correlation = budgetCorrelationSummary(entries);
  assert.equal(correlation.available, true);
  assert.equal(correlation.windows.length, 1);
  assert.equal(correlation.windows[0].operation, 'ingame_server_info');
  assert.equal(correlation.windows[0].fpsOnePctLow, 180);
  assert.equal(correlation.windows[0].fpsOnePctLowAvailable, true);
  assert.equal(correlation.windows[0].fpsOnePctLowSource, 'real_sampled');
  assert.equal(correlation.windows[0].windowStartFrame, 50);
  assert.equal(correlation.windows[0].windowEndFrame, 170);
  assert.equal(correlation.windows[0].resourceUploadTokens, 1);
  assert.equal(correlation.windows[0].previewUploads, 2);
  assert.equal(correlation.windows[0].paragraphBudgetBlocked, 1);
  assert.equal(correlation.windows[0].topUiSectionStage, 'assets_preview_draw_workshop_cards');
  assert.equal(correlation.windows[0].dominantAttribution, 'paragraph_layout');
  assert.equal(correlation.windows[0].culpritRank[0].kind, 'paragraph_layout');
  assert.ok(correlation.windows[0].culpritRank.some(culprit => culprit.kind === 'ui_section:assets_preview_draw_workshop_cards'));

  const summary = summarizeForBundle(entries, 'qm_perf_budget_correlation_window.log', { invalidLines: 0, totalLines: 4 });
  assert.equal(summary.budgetCorrelation.available, true);
  assert.equal(summary.budgetCorrelation.windows[0].dominantAttribution, 'paragraph_layout');
  assert.equal(summary.budgetCorrelation.windows[0].culpritRank[0].kind, 'paragraph_layout');
  assert.equal(summary.budgetCorrelation.windows[0].topUiSectionStage, 'assets_preview_draw_workshop_cards');

  const html = generateReport(entries, 'qm_perf_budget_correlation_window.log', null);
  assert.match(html, /Budget Attribution by Window/);
  assert.match(html, /ingame_server_info/);
  assert.match(html, /text/);
  assert.match(html, /180\.0/);
}

function testPerfAnalyzerFailsUnattributedLowFpsWindow() {
  const entries = parseLog([
    '2026-06-16 05:10:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_tab_switch","context":"cold","page":"settings:general","tab":"none","sample_frames":30,"sample_seconds":0.250,"fps_avg":120.000,"fps_min":8.000,"fps_max":900.000,"fps_1pct_low":8.000,"fps_1pct_source":"real_sampled","window_start_frame":300,"window_end_frame":329,"frame_ms_avg":8.333,"frame_ms_p95":30.000,"frame_ms_p99":125.000,"frame_ms_max":125.000,"cap_limited":0}',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_unattributed_low.log', { invalidLines: 0, totalLines: 1 });
  assert.equal(summary.budgetCorrelation.failingWindowCount, 1);
  assert.equal(summary.budgetCorrelation.unattributedFailingWindowCount, 1);
  assert.equal(summary.verdict, 'FAIL');
  assert.equal(summary.quality.failed, true);
  assert.match(summary.quality.warnings.join('\n'), /unattributed_spike/);

  const html = generateReport(entries, 'qm_perf_unattributed_low.log', null);
  assert.match(html, /unattributed_spike/);
  assert.match(html, /FAIL/);
}

function testBudgetCorrelationAttributesRenderStageWhenBudgetCountersAreMissing() {
  const entries = parseLog([
    '2026-06-16 05:15:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_tab_switch","context":"cold","page":"settings:general","tab":"none","sample_frames":30,"sample_seconds":0.250,"fps_avg":120.000,"fps_min":8.000,"fps_max":900.000,"fps_1pct_low":8.000,"fps_1pct_source":"real_sampled","window_start_frame":300,"window_end_frame":329,"frame_ms_avg":8.333,"frame_ms_p95":30.000,"frame_ms_p99":125.000,"frame_ms_max":125.000,"cap_limited":0}',
    '2026-06-16 05:15:01 I perf/menu: stage=settings_page_content page=settings:general operation=settings_tab_switch context=cold duration_ms=124.000 frame=301',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_render_stage_low.log', { invalidLines: 0, totalLines: 2 });
  assert.equal(summary.budgetCorrelation.failingWindowCount, 1);
  assert.equal(summary.budgetCorrelation.unattributedFailingWindowCount, 0);
  assert.equal(summary.budgetCorrelation.windows[0].dominantAttribution, 'ui_layout_or_render_total');
  assert.equal(summary.budgetCorrelation.windows[0].culpritRank[0].kind, 'ui_layout_or_render_total');
  assert.equal(summary.budgetCorrelation.windows[0].culpritRank[0].score, 124);
}

function testTargetFpsFailureRequiresConcreteUiSectionAttribution() {
  const entries = parseLog([
    '2026-06-18 10:00:00 I perf/fps: event=fps_summary operation=ingame_esc_open context=online page=game tab=none sample_frames=30 sample_seconds=0.500 fps_avg=60 fps_min=2 fps_1pct_low=2 fps_1pct_source=real_sampled fps_max=1200 frame_ms_avg=16.0 frame_ms_p95=20.0 frame_ms_p99=120.0 frame_ms_max=120.0 menu_ms_max=110.0 window_start_frame=100 window_end_frame=130 cap_limited=0',
    '2026-06-18 10:00:00 I perf/menu: page=game operation=ingame_esc_open frame=112 stage=ingame_esc_menu_shell duration_ms=9.5',
    '2026-06-18 10:00:00 I perf/menu: page=game operation=ingame_esc_open frame=112 stage=ingame_server_info_layout duration_ms=77.0',
    '2026-06-18 10:00:00 I perf/menu: page=game operation=ingame_esc_open frame=112 stage=ingame_tabbar duration_ms=3.0',
  ].join('\n'));
  const summary = summarizeForBundle(entries, 'test.log', { totalLines: entries.length, invalidLines: 0 });
  const sample = JSON.stringify(summary);
  assert.match(sample, /ingame_server_info_layout/);
  assert.doesNotMatch(sample, /top_culprit=ui_layout_or_render_total/);
  assert.equal(summary.budgetCorrelation.windows[0].topUiSectionStage, 'ingame_server_info_layout');
  assert.equal(summary.budgetCorrelation.windows[0].topUiSectionMs, 77);
  assert.equal(summary.budgetCorrelation.windows[0].culpritRank[0].kind, 'ui_section:ingame_server_info_layout');
  assert.equal(summary.budgetCorrelation.windows[0].dominantAttribution, 'ui_section:ingame_server_info_layout');
}

function testBudgetCorrelationDoesNotUseStringFallbackWithoutFrameWindow() {
  const entries = parseLog([
    '2026-06-16 05:20:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_tab_switch","context":"cold","page":"settings:assets","tab":"entity_bg","sample_frames":30,"sample_seconds":0.250,"fps_avg":120.000,"fps_min":8.000,"fps_max":900.000,"fps_1pct_low":8.000,"frame_ms_avg":8.333,"frame_ms_p95":30.000,"frame_ms_p99":125.000,"frame_ms_max":125.000,"cap_limited":0}',
    '2026-06-16 05:20:01 I perf/text: {"system":"perf/text","event":"text_runtime_budget","operation":"settings_tab_switch","context":"cold","page":"settings:assets","tab":"entity_bg","frame":999,"text_container_new":20,"text_container_create_ms":80.000}',
  ].join('\n'));

  const correlation = budgetCorrelationSummary(entries);
  assert.equal(correlation.windows.length, 1);
  assert.equal(correlation.windows[0].fpsOnePctLowAvailable, false);
  assert.equal(correlation.windows[0].dominantAttribution, 'none');
  assert.equal(correlation.unattributedFailingWindowCount, 1);
}

function testBudgetCorrelationUsesMetadataHydrateForMetadataLayoutCulprit() {
  const entries = parseLog([
    '2026-06-16 05:30:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_assets_tab_switch","context":"cold","page":"settings:assets","tab":"entity_bg","sample_frames":60,"sample_seconds":0.250,"fps_avg":180.000,"fps_min":90.000,"fps_max":900.000,"fps_1pct_low":150.000,"fps_1pct_source":"real_sampled","window_start_frame":10,"window_end_frame":69,"frame_ms_avg":5.556,"frame_ms_p95":6.000,"frame_ms_p99":6.667,"frame_ms_max":11.111,"cap_limited":0}',
    '2026-06-16 05:30:01 I perf/assets: stage=assets_preview_draw_workshop_cards operation=settings_assets_tab_switch context=cold page=settings:assets tab=entity_bg duration_ms=1.000 frame=20 preview_uploads=0 preview_artifact_ms=0.000 metadata_hydrate_ms=12.000 layout_text_ms=0.000 preview_draw_ms=0.000',
  ].join('\n'));

  const correlation = budgetCorrelationSummary(entries);
  assert.equal(correlation.windows.length, 1);
  assert.equal(correlation.windows[0].maxMetadataLayoutMs, 12);
  assert.equal(correlation.windows[0].dominantAttribution, 'metadata_layout');
  assert.equal(correlation.windows[0].culpritRank[0].kind, 'metadata_layout');
  assert.ok(correlation.windows[0].culpritRank.some(culprit => culprit.kind === 'ui_section:assets_preview_draw_workshop_cards'));
}

function testPerfOverheadIsReportedAsCulprit() {
  const entries = parseLog([
    '2026-06-16 06:00:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_tab_switch","context":"cold","page":"settings:general","tab":"none","sample_frames":30,"sample_seconds":0.250,"fps_avg":120.000,"fps_min":8.000,"fps_max":900.000,"fps_1pct_low":8.000,"fps_1pct_source":"real_sampled","window_start_frame":400,"window_end_frame":429,"frame_ms_avg":8.333,"frame_ms_p95":30.000,"frame_ms_p99":125.000,"frame_ms_max":125.000,"cap_limited":0}',
    '2026-06-16 06:00:01 I perf/menu: stage=frame_render page=settings:general operation=settings_tab_switch context=cold duration_ms=126.000 frame=401',
    '2026-06-16 06:00:02 I perf/ui_budget: event=settings_ui_budget page=settings:general operation=settings_tab_switch frame=401 tab=general subtab=none layout_ms=4.000 text_ms=2.000 draw_calls=12 vertices=144 indices=216 heap_allocs=18 visible_widgets=31',
    '2026-06-16 06:00:03 I perf/qmclient: stage=telemetry_flush operation=settings_tab_switch page=settings:general context=cold frame=401 duration_ms=9.000',
  ].join('\n'));

  const correlation = budgetCorrelationSummary(entries);
  assert.equal(correlation.windows.length, 1);
  assert.equal(correlation.windows[0].dominantAttribution, 'ui_layout_or_render_total');
  assert.equal(correlation.windows[0].culpritRank[0].kind, 'ui_layout_or_render_total');
  assert.ok(correlation.windows[0].culpritRank.some(culprit => culprit.kind === 'telemetry_overhead'));
  assert.ok(correlation.windows[0].culpritRank.some(culprit => culprit.kind === 'telemetry_flush'));

  const html = generateReport(entries, 'qm_perf_telemetry_overhead.log', null);
  assert.match(html, /telemetry_overhead/);
  assert.match(html, /telemetry_flush/);
}

function testMainFpsTableDoesNotMarkP99DerivedAsPassing() {
  const entries = parseLog([
    '2026-06-16 05:40:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_tab_switch","context":"cold","page":"settings:assets","tab":"entity_bg","sample_frames":60,"sample_seconds":0.250,"fps_avg":300.000,"fps_min":120.000,"fps_max":900.000,"fps_1pct_low":250.000,"frame_ms_avg":3.333,"frame_ms_p95":3.800,"frame_ms_p99":4.000,"frame_ms_max":8.333,"cap_limited":0}',
  ].join('\n'));

  const html = generateReport(entries, 'qm_perf_p99_derived.log', null);
  assert.match(html, /P99-derived/);
  assert.doesNotMatch(html, /class="num ok">250\.0 \(P99-derived\)<\/td>/);
  assert.match(html, /class="num bad">250\.0 \(P99-derived\)<\/td>/);
}

function testBudgetCorrelationRanksCulprits() {
  const entries = parseLog([
    '2026-06-16 04:10:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_assets_tab_switch","context":"cold","page":"settings:assets","tab":"entity_bg","sample_frames":120,"sample_seconds":0.500,"fps_avg":320.000,"fps_min":110.000,"fps_max":900.000,"fps_1pct_low":170.000,"fps_1pct_source":"real_sampled","window_start_frame":60,"window_end_frame":80,"frame_ms_avg":3.125,"frame_ms_p95":4.200,"frame_ms_p99":5.882,"frame_ms_max":13.000,"cap_limited":0}',
    '2026-06-16 04:10:01 I perf/settings-resource: event=settings_adaptive_budget operation=settings_assets_tab_switch page=settings:assets tab=entity_bg context=cold frame=61 mode=frame_pressure reason=frame_pressure frame_ms_avg=5.882 frame_ms_p95=8.000 target_ms=4.167 visible_tokens=1 prefetch_tokens=0 background_tokens=0 gpu_upload_tokens=2 resource_upload_tokens=2 text_tokens=1 text_container_tokens=1 glyph_upload_tokens=1 paragraph_layout_tokens=0 demo_tokens=0 backlog=12 scroll=0 jump_scroll=0',
    '2026-06-16 04:10:02 I perf/assets: stage=assets_preview_draw_workshop_cards operation=settings_assets_tab_switch context=cold page=settings:assets tab=entity_bg duration_ms=4.000 frame=62 preview_jobs_started=3 preview_jobs_done=2 preview_uploads=6 preview_admissions=8 preview_artifact_ms=3.500 metadata_hydrate_ms=0.400 placeholder_count=3 ready_texture_count=4 visible_ready_ratio=0.600',
    '2026-06-16 04:10:03 I perf/text: {"system":"perf/text","event":"text_runtime_budget","operation":"settings_assets_tab_switch","context":"cold","page":"settings:assets","tab":"entity_bg","frame":63,"glyph_new":1,"glyph_uploads":1,"glyph_rasterize_ms":0.100,"glyph_upload_ms":0.100,"text_container_new":1,"text_container_uploads":1,"text_container_create_ms":0.100,"text_container_upload_ms":0.100,"paragraph_layout_ms":0.000,"paragraph_budget_blocked":0,"paragraph_cache_hit":1,"paragraph_cache_miss":0}',
  ].join('\n'));

  const correlation = budgetCorrelationSummary(entries);
  assert.equal(correlation.windows.length, 1);
  assert.equal(correlation.windows[0].dominantAttribution, 'card_draw');
  assert.equal(correlation.windows[0].culpritRank[0].kind, 'card_draw');
  assert.ok(correlation.windows[0].culpritRank.some(culprit => culprit.kind === 'ui_section:assets_preview_draw_workshop_cards'));

  const html = generateReport(entries, 'qm_perf_budget_culprit_rank.log', null);
  assert.match(html, /Top Culprit/);
  assert.match(html, /ui_section:assets_preview_draw_workshop_cards/);
  assert.match(html, /card_draw/);
}

function testBudgetCorrelationKeepsOverlappingWindowsOperationScoped() {
  const entries = parseLog([
    '2026-06-18 11:00:00 I perf/fps: event=fps_summary operation=settings_open context=online page=settings:tee tab=none sample_frames=30 sample_seconds=0.125 fps_avg=220 fps_min=180 fps_1pct_low=180 fps_1pct_source=real_sampled fps_max=400 frame_ms_avg=4.500 frame_ms_p95=5.000 frame_ms_p99=5.500 frame_ms_max=6.000 menu_ms_max=5.000 window_start_frame=100 window_end_frame=130 cap_limited=0',
    '2026-06-18 11:00:00 I perf/fps: event=fps_summary operation=server_browser_open context=online page=server_browser tab=internet sample_frames=30 sample_seconds=0.125 fps_avg=210 fps_min=170 fps_1pct_low=170 fps_1pct_source=real_sampled fps_max=400 frame_ms_avg=4.800 frame_ms_p95=5.300 frame_ms_p99=5.800 frame_ms_max=6.500 menu_ms_max=5.000 window_start_frame=110 window_end_frame=140 cap_limited=0',
    '2026-06-18 11:00:00 I perf/menu: page=server_browser operation=server_browser_open context=online tab=internet frame=115 stage=server_browser_open_list duration_ms=5.000',
  ].join('\n'));

  const correlation = budgetCorrelationSummary(entries);
  const settingsWindow = correlation.windows.find(window => window.operation === 'settings_open');
  const serverBrowserWindow = correlation.windows.find(window => window.operation === 'server_browser_open');

  assert.ok(settingsWindow);
  assert.ok(serverBrowserWindow);
  assert.notEqual(settingsWindow.topUiSectionStage, 'server_browser_open_list');
  assert.equal(serverBrowserWindow.topUiSectionStage, 'server_browser_open_list');
}

function testBudgetCorrelationKeepsHigherCostCulpritAboveConcreteUiSection() {
  const entries = parseLog([
    '2026-06-18 11:10:00 I perf/fps: event=fps_summary operation=settings_open context=online page=settings:tee tab=appearance sample_frames=30 sample_seconds=0.125 fps_avg=120 fps_min=80 fps_1pct_low=80 fps_1pct_source=real_sampled fps_max=400 frame_ms_avg=8.000 frame_ms_p95=10.000 frame_ms_p99=12.500 frame_ms_max=14.000 menu_ms_max=5.000 window_start_frame=200 window_end_frame=230 cap_limited=0',
    '2026-06-18 11:10:00 I perf/menu: page=settings:tee operation=settings_open context=online tab=appearance frame=210 stage=settings_identity_panel duration_ms=5.000',
    '2026-06-18 11:10:00 I perf/text: event=text_runtime_budget page=settings:tee operation=settings_open context=online tab=appearance frame=211 glyph_new=0 glyph_uploads=0 glyph_rasterize_ms=0.000 glyph_upload_ms=0.000 text_container_new=1 text_container_create_ms=3.000 paragraph_layout_ms=22.000 paragraph_budget_blocked=1',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_budget_rank_concrete_section.log', { invalidLines: 0, totalLines: 3 });
  const window = summary.budgetCorrelation.windows[0];

  assert.equal(window.culpritRank[0].kind, 'paragraph_layout');
  assert.ok(window.culpritRank[0].score > window.topUiSectionMs);
  assert.ok(window.culpritRank.some(culprit => culprit.kind === 'ui_section:settings_identity_panel'));
  assert.equal(window.topUiSectionStage, 'settings_identity_panel');
  assert.equal(window.topUiSectionMs, 5);

  const html = generateReport(entries, 'qm_perf_budget_rank_concrete_section.log', null);
  assert.match(html, /Top UI Section/);
  assert.match(html, /ui_section:settings_identity_panel/);
  assert.match(html, /paragraph_layout/);
}

function testBudgetCorrelationTreatsUiLayoutTotalAsAggregateWhenRankingConcreteSections() {
  const entries = parseLog([
    '2026-06-18 11:20:00 I perf/fps: event=fps_summary operation=settings_open context=online page=settings:tee tab=appearance sample_frames=30 sample_seconds=0.125 fps_avg=60 fps_min=40 fps_1pct_low=40 fps_1pct_source=real_sampled fps_max=400 frame_ms_avg=16.000 frame_ms_p95=20.000 frame_ms_p99=25.000 frame_ms_max=28.000 menu_ms_max=100.000 window_start_frame=300 window_end_frame=330 cap_limited=0',
    '2026-06-18 11:20:00 I perf/menu: page=settings:tee operation=settings_open context=online tab=appearance frame=310 stage=menus_render_total duration_ms=100.000',
    '2026-06-18 11:20:00 I perf/menu: page=settings:tee operation=settings_open context=online tab=appearance frame=311 stage=settings_identity_panel duration_ms=5.000',
    '2026-06-18 11:20:00 I perf/text: event=text_runtime_budget page=settings:tee operation=settings_open context=online tab=appearance frame=312 glyph_new=0 glyph_uploads=0 glyph_rasterize_ms=0.000 glyph_upload_ms=0.000 text_container_new=0 text_container_create_ms=0.000 paragraph_layout_ms=80.000 paragraph_budget_blocked=0',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_budget_rank_aggregate_total.log', { invalidLines: 0, totalLines: 4 });
  const window = summary.budgetCorrelation.windows[0];

  assert.equal(window.culpritRank[0].kind, 'paragraph_layout');
  assert.equal(window.dominantAttribution, 'paragraph_layout');
  assert.ok(window.culpritRank.some(culprit => culprit.kind === 'ui_section:settings_identity_panel'));
  assert.ok(window.culpritRank.some(culprit => culprit.kind === 'ui_layout_or_render_total'));
}

function testFpsBaselineFailsIngameEscAndAssetsTabSwitchWindowsIndependently() {
  const entries = parseLog([
    '2026-06-18 10:00:00 I perf/fps: event=fps_summary operation=ingame_esc_open context=online page=game tab=none sample_frames=30 sample_seconds=0.5 fps_avg=55 fps_min=2 fps_1pct_low=2 fps_1pct_source=real_sampled fps_max=1200 frame_ms_avg=18 frame_ms_p95=15 frame_ms_p99=481 frame_ms_max=481 menu_ms_max=324 window_start_frame=10 window_end_frame=40 cap_limited=0',
    '2026-06-18 10:00:01 I perf/fps: event=fps_summary operation=settings_assets_tab_switch context=offline page=settings:assets tab=1 sample_frames=30 sample_seconds=0.1 fps_avg=320 fps_min=35 fps_1pct_low=35 fps_1pct_source=real_sampled fps_max=1300 frame_ms_avg=3 frame_ms_p95=17 frame_ms_p99=28 frame_ms_max=28 menu_ms_max=25 window_start_frame=50 window_end_frame=80 cap_limited=0',
    '2026-06-18 10:00:01 I perf/menu: page=game operation=ingame_esc_open context=online tab=none frame=20 stage=ingame_esc_menu_shell duration_ms=12.000',
    '2026-06-18 10:00:01 I perf/assets: page=settings:assets operation=settings_assets_tab_switch context=offline tab=1 frame=60 stage=assets_tab_switch_shell_first duration_ms=1.000',
  ].join('\n'));
  const summary = summarizeForBundle(entries, 'test.log', { totalLines: entries.length, invalidLines: 0 });
  const warnings = summary.quality.warnings.join('\n');
  assert.equal(summary.quality.failed, true);
  assert.match(warnings, /fps_baseline_failed: ingame_esc_open/);
  assert.match(warnings, /fps_baseline_failed: settings_assets_tab_switch/);
  assert.match(warnings, /menu_max=324\.000>12/);
  assert.match(warnings, /menu_max=25\.000>8/);
}

function testBackendRemainsSeparateWhenUiLayoutDominatesAndUploadsAreZero() {
  const entries = parseLog([
    '2026-06-18 10:00:00 I perf/fps: event=fps_summary operation=ingame_esc_open context=online page=game tab=none sample_frames=30 sample_seconds=0.5 fps_avg=55 fps_min=2 fps_1pct_low=2 fps_1pct_source=real_sampled fps_max=1200 frame_ms_avg=18 frame_ms_p95=15 frame_ms_p99=481 frame_ms_max=481 menu_ms_max=324 window_start_frame=10 window_end_frame=40 cap_limited=0',
    '2026-06-18 10:00:00 I perf/menu: page=game operation=ingame_esc_open context=online tab=none frame=20 stage=ingame_server_info_layout duration_ms=300',
    '2026-06-18 10:00:00 I perf/assets: stage=assets_preview_gpu_upload_batch operation=ingame_esc_open context=online page=game tab=none frame=20 duration_ms=0 uploads_this_frame=0 bytes=0',
  ].join('\n'));
  const summary = summarizeForBundle(entries, 'test.log', { totalLines: entries.length, invalidLines: 0 });
  const window = summary.budgetCorrelation.windows[0];
  const text = JSON.stringify(summary);
  assert.equal(window.maxTextureUploadMs, 0);
  assert.equal(window.maxGlyphUploadMs, 0);
  assert.equal(window.maxUiLayoutMs, 0);
  assert.equal(window.topUiSectionStage, 'ingame_server_info_layout');
  assert.equal(window.dominantAttribution, 'ui_section:ingame_server_info_layout');
  assert.match(text, /ui_section:ingame_server_info_layout/);
  assert.doesNotMatch(text, /dominantAttribution":"texture_upload/);

  const html = generateReport(entries, 'test.log', null);
  assert.match(html, /Graphics backend optimization is not implicated unless backend upload\/render buckets dominate/);
}

function testMissingOnePctLowIsMarkedP99DerivedAndNotTargetPass() {
  const entries = parseLog([
    '2026-06-16 02:00:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_assets_tab_switch","context":"cold","page":"settings:assets","tab":"entity_bg","sample_frames":60,"sample_seconds":0.250,"fps_avg":500.000,"fps_min":80.000,"fps_max":900.000,"frame_ms_avg":2.000,"frame_ms_p95":3.000,"frame_ms_p99":4.000,"frame_ms_max":12.000,"cap_limited":0}',
  ].join('\n'));

  const fps = coldTabSwitchFpsSummaries(entries);
  assert.equal(fps.length, 1);
  assert.equal(fps[0].fpsOnePctLowAvailable, false);
  assert.equal(fps[0].fpsOnePctLow, 250);

  const html = generateReport(entries, 'qm_perf_legacy_fps.log', null);
  assert.match(html, /P99-derived/);
  assert.doesNotMatch(html, /<span class="badge ok">1% Low Target 240 FPS<\/span>/);
}

function testMissingFpsSummaryWarnsThatSettingsAcceptanceIsIncomplete() {
  const entries = parseLog([
    '2026-06-11 02:00:00 I perf/menu: {"system":"perf/menu","frame":10,"stage":"settings_page_content","duration_ms":7.000,"page":"tee"}',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_missing_fps.log', { invalidLines: 0, totalLines: 1 });

  assert.equal(summary.fps.available, false);
  assert.match(summary.quality.warnings.join('\n'), /missing fps_summary/i);
  assert.match(summary.quality.warnings.join('\n'), /ingame\/online/i);

  const html = generateReport(entries, 'qm_perf_missing_fps.log', null);
  assert.match(html, /缺少 fps_summary/);
  assert.match(html, /不足以验收/);
}

function testTargetSettingsVerdictIgnoresServerBrowserFrames() {
  const entries = parseLog([
    '2026-06-11 02:00:00 I perf/menu: {"system":"perf/menu","frame":1,"stage":"offline_page_content","duration_ms":200.000,"page":"internet"}',
    '2026-06-11 02:00:01 I perf/menu: {"system":"perf/menu","frame":2,"stage":"settings_page_content","duration_ms":7.000,"page":"tee"}',
    '2026-06-11 02:00:01 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_tab_switch","context":"offline","page":"settings:tee","tab":"none","sample_frames":30,"sample_seconds":0.250,"fps_avg":120.000,"fps_min":100.000,"fps_max":180.000,"frame_ms_avg":8.000,"frame_ms_p95":9.000,"frame_ms_p99":10.000,"frame_ms_max":10.000,"cap_limited":0}',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_mixed.log', { invalidLines: 0, totalLines: 3 });

  assert.equal(summary.targetSettings.verdict, 'PASS');
  assert.equal(summary.targetSettings.spikeCount, 0);
  assert.equal(summary.verdict, 'FAIL');
}

function testTargetSettingsVerdictUsesIngameEscFpsSummaryWindow() {
  const entries = parseLog([
    '2026-06-11 02:00:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"ingame_esc_open","context":"online","page":"game","tab":"none","sample_frames":30,"sample_seconds":0.500,"fps_avg":60.000,"fps_min":30.000,"fps_max":120.000,"frame_ms_avg":16.667,"frame_ms_p95":20.000,"frame_ms_p99":45.000,"frame_ms_max":45.000,"cap_limited":0}',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_ingame_esc.log', { invalidLines: 0, totalLines: 1 });

  assert.equal(summary.targetSettings.verdict, 'FAIL');
  assert.equal(summary.targetSettings.verdictAvailable, true);
  assert.equal(summary.targetSettings.spikeCount, 1);
  assert.doesNotMatch(summary.quality.warnings.join('\n'), /missing ingame\/online/i);
}

function testStableTextCoverageBlocksSettingsAcceptanceEvenWithBackgroundHotspots() {
	const entries = parseLog([
		'2026-06-11 01:59:58 I perf/settings-text: event=settings_text_prebuild built=120 reused=24 remaining=3 budget=160 phase=before_target scope=target_settings operation=settings_open',
    '2026-06-11 01:59:59 I perf/settings: event=settings_text_miss scope=target_settings page=settings:tee tab=appearance subtab=assets key=qm.ui.preview reason=cache_miss',
    '2026-06-11 02:00:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_open","context":"online","page":"settings:tee","tab":"none","sample_frames":30,"sample_seconds":0.500,"fps_avg":144.000,"fps_min":120.000,"fps_max":180.000,"frame_ms_avg":6.944,"frame_ms_p95":8.000,"frame_ms_p99":9.000,"frame_ms_max":9.000,"cap_limited":0}',
    '2026-06-11 02:00:03 I perf/interaction: event=list_frame page=server_browser items_total=3200 rows_visible=24 rows_rendered=24 rows_iterated=3200 rows_skipped=3176 dur_ms=48.500 frame=22 source=server_browser',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_stable_text_blocker.log', { invalidLines: 0, totalLines: 4 });

  assert.equal(summary.targetSettings.verdict, 'PASS');
  assert.equal(summary.targetSettings.stableTextCoverage.acceptanceBlocked, true);
  assert.equal(summary.targetSettings.stableTextCoverage.missCount, 1);
  assert.equal(summary.targetSettings.stableTextCoverage.staleCount, 0);
  assert.equal(summary.targetSettings.stableTextCoverage.prebuildRemainingBeforeTarget, 3);
  assert.match(summary.targetSettings.stableTextCoverage.samples.join('\n'), /settings_text_miss/);

  const html = generateReport(entries, 'qm_perf_stable_text_blocker.log', null);
  assert.match(html, /不足以验收/);
  assert.match(html, /stable text/i);
  assert.match(html, /hit rate=0\.0%/);
  assert.match(html, /reuse rate=0\.0%/);
  assert.match(html, /text_new=0/);
  assert.match(html, /text_reused=0/);
	assert.doesNotMatch(html, /server browser.*PASS/i);
}

function testStableTextHitRateUsesRenderReadyHitInsteadOfPoolHit() {
	const entries = parseLog([
		'2026-06-18 10:00:00 I perf/settings-text: event=settings_text_plan_collection units_done=1 units_total=1 remaining=0 budget=1 complete=1 dirty=0 phase=before_target scope=target_settings operation=settings_open',
		'2026-06-18 10:00:00 I perf/settings-text: event=settings_text_usage scope=target_settings page=settings tab=0 subtab=-1 operation=settings_open frame=10 text_class=static_stable candidates=10 hits=10 reused=10 miss=0 stale=0 text_new=0 text_reused=10 planned=10 unplanned=0 pool_hit=10 render_ready_hit=6 build_queued=4 fallback_immediate=0',
		'2026-06-18 10:00:00 I perf/fps: event=fps_summary operation=settings_open context=online page=settings tab=none sample_frames=30 sample_seconds=0.125 fps_avg=240 fps_min=240 fps_1pct_low=240 fps_1pct_source=real_sampled fps_max=240 frame_ms_avg=4.1 frame_ms_p95=4.2 frame_ms_p99=4.3 frame_ms_max=4.4 menu_ms_max=1.0 window_start_frame=1 window_end_frame=30 cap_limited=0',
	].join('\n'));

	const summary = summarizeForBundle(entries, 'qm_perf_render_ready_hit.log', { invalidLines: 0, totalLines: 2 });
	assert.equal(summary.targetSettings.stableTextCoverage.visibleCandidateCount, 10);
	assert.equal(summary.targetSettings.stableTextCoverage.poolHitCount, 10);
	assert.equal(summary.targetSettings.stableTextCoverage.renderReadyHitCount, 6);
	assert.equal(summary.targetSettings.stableTextCoverage.staticHitRate, 60);
	assert.equal(summary.targetSettings.stableTextCoverage.acceptanceBlocked, true);
	assert.match(summary.quality.warnings.join('\n'), /queued visible builds/);
}

function testStableTextFallbackImmediateBlocksAcceptanceEvenWhenPoolHitIsPerfect() {
	const entries = parseLog([
		'2026-06-18 10:00:00 I perf/settings-text: event=settings_text_plan_collection units_done=1 units_total=1 remaining=0 budget=1 complete=1 dirty=0 phase=before_target scope=target_settings operation=settings_open',
		'2026-06-18 10:00:00 I perf/settings-text: event=settings_text_usage scope=target_settings page=settings tab=0 subtab=-1 operation=settings_open frame=10 text_class=static_stable scheduler_coverage=uncovered candidates=3 hits=3 reused=3 miss=0 stale=0 text_new=0 text_reused=3 planned=3 unplanned=0 pool_hit=3 render_ready_hit=3 build_queued=0 fallback_immediate=1',
		'2026-06-18 10:00:00 I perf/fps: event=fps_summary operation=settings_open context=online page=settings tab=none sample_frames=30 sample_seconds=0.125 fps_avg=240 fps_min=240 fps_1pct_low=240 fps_1pct_source=real_sampled fps_max=240 frame_ms_avg=4.1 frame_ms_p95=4.2 frame_ms_p99=4.3 frame_ms_max=4.4 menu_ms_max=1.0 window_start_frame=1 window_end_frame=30 cap_limited=0',
	].join('\n'));

	const summary = summarizeForBundle(entries, 'qm_perf_immediate_fallback.log', { invalidLines: 0, totalLines: 2 });
	assert.equal(summary.targetSettings.stableTextCoverage.fallbackImmediate, 1);
	assert.equal(summary.targetSettings.stableTextCoverage.schedulerCoverage, 'uncovered');
	assert.equal(summary.targetSettings.stableTextCoverage.acceptanceBlocked, true);
	assert.match(summary.quality.warnings.join('\n'), /scheduler coverage uncovered immediate fallback/);
}

function testReportDistinguishesPoolHitRateFromRenderReadyHitRate() {
	const entries = parseLog([
		'2026-06-18 10:00:00 I perf/settings-text: event=settings_text_plan_collection units_done=1 units_total=1 remaining=0 budget=1 complete=1 dirty=0 phase=before_target scope=target_settings operation=settings_open',
		'2026-06-18 10:00:00 I perf/settings-text: event=settings_text_usage scope=target_settings page=settings tab=0 subtab=-1 operation=settings_open frame=10 text_class=static_stable candidates=10 hits=10 reused=6 miss=0 stale=0 text_new=0 text_reused=6 planned=10 unplanned=0 pool_hit=10 render_ready_hit=6 build_queued=4 fallback_immediate=0',
		'2026-06-18 10:00:00 I perf/fps: event=fps_summary operation=settings_open context=online page=settings tab=none sample_frames=30 sample_seconds=0.125 fps_avg=240 fps_min=240 fps_1pct_low=240 fps_1pct_source=real_sampled fps_max=240 frame_ms_avg=4.1 frame_ms_p95=4.2 frame_ms_p99=4.3 frame_ms_max=4.4 menu_ms_max=1.0 window_start_frame=1 window_end_frame=30 cap_limited=0',
	].join('\n'));

	const html = generateReport(entries, 'qm_perf_render_ready_report.log', null);
	assert.match(html, /Render-Ready Hit Rate/);
	assert.match(html, /Pool Hit/);
	assert.match(html, /Queued Builds/);
	assert.match(html, /Immediate Fallback/);
	assert.match(html, /pool hit 只说明 key 命中/);
	assert.match(html, /render-ready hit 才说明本帧无需构建且可直接绘制/);
	assert.doesNotMatch(html, /static stable text coverage 已覆盖/);
}

function testStableTextCoverageIgnoresOrdinarySettingsPagesOutsideTargetWindow() {
	const entries = parseLog([
		'2026-06-11 02:00:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_open","context":"online","page":"settings:tee","tab":"none","sample_frames":30,"sample_seconds":0.500,"fps_avg":144.000,"fps_min":120.000,"fps_max":180.000,"frame_ms_avg":6.944,"frame_ms_p95":8.000,"frame_ms_p99":9.000,"frame_ms_max":9.000,"cap_limited":0}',
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_plan_collection units_done=10 units_total=10 remaining=0 budget=1 complete=1 dirty=0 phase=before_target scope=target_settings operation=settings_open',
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_usage scope=target_settings page=settings:tee tab=appearance subtab=assets operation=settings_open frame=40 candidates=12 hits=12 reused=12 miss=0 stale=0 text_new=0 text_reused=12 planned=12 unplanned=0',
    '2026-06-11 02:00:01 I perf/settings-text: event=settings_text_prebuild built=40 reused=10 remaining=7 budget=80 phase=before_target scope=settings operation=settings_open',
    '2026-06-11 02:00:01 I perf/settings-text: event=settings_text_miss scope=settings page=settings:qmclient tab=0 subtab=-1 key=settings:12:0:-1:qmclient-theme-title:fs140:al8:mw2600:us100:cm1:ch1 reason=missing operation=settings_open frame=42',
    '2026-06-11 02:00:02 I perf/settings-text: event=settings_text_stale scope=settings page=settings:tclient tab=0 subtab=-1 key=settings:11:0:-1:tclient-theme-title:fs140:al8:mw2600:us100:cm1:ch1 reason=style operation=settings_tab_switch frame=43',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_ordinary_settings_text.log', { invalidLines: 0, totalLines: 4 });

  assert.equal(summary.targetSettings.verdict, 'PASS');
  assert.equal(summary.targetSettings.stableTextCoverage.acceptanceBlocked, false);
  assert.equal(summary.targetSettings.stableTextCoverage.missCount, 0);
  assert.equal(summary.targetSettings.stableTextCoverage.staleCount, 0);
  assert.equal(summary.targetSettings.stableTextCoverage.prebuildRemainingBeforeTarget, 0);
  assert.equal(summary.targetSettings.stableTextCoverage.utilizationAvailable, true);
  assert.equal(summary.targetSettings.stableTextCoverage.candidateTotal, 12);
  assert.equal(summary.targetSettings.stableTextCoverage.hitCount, 12);
  assert.equal(summary.targetSettings.stableTextCoverage.reuseCount, 12);
  assert.equal(summary.targetSettings.stableTextCoverage.hitRate, 100);
  assert.equal(summary.targetSettings.stableTextCoverage.reuseRate, 100);
  assert.equal(summary.targetSettings.stableTextCoverage.samples.join('\n'), '');
}

function testStableTextCoverageIncludesLaterTargetWindows() {
  const entries = parseLog([
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_prebuild built=32 reused=8 remaining=0 budget=64 phase=before_target scope=target_settings operation=settings_open',
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_usage scope=target_settings page=settings:tee tab=appearance subtab=assets operation=settings_open frame=40 candidates=8 hits=8 reused=8 miss=0 stale=0 text_new=0 text_reused=8 planned=8 unplanned=0',
    '2026-06-11 02:00:01 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_open","context":"online","page":"settings:tee","tab":"none","sample_frames":30,"sample_seconds":0.500,"fps_avg":144.000,"fps_min":120.000,"fps_max":180.000,"frame_ms_avg":6.944,"frame_ms_p95":8.000,"frame_ms_p99":9.000,"frame_ms_max":9.000,"cap_limited":0}',
    '2026-06-11 02:00:03 I perf/settings-text: event=settings_text_usage scope=target_settings page=settings:qmclient tab=0 subtab=0 operation=settings_subtab_switch frame=77 candidates=4 hits=3 reused=3 miss=1 stale=0 text_new=0 text_reused=3 planned=3 unplanned=1',
    '2026-06-11 02:00:03 I perf/settings: event=settings_text_miss scope=target_settings page=settings:qmclient tab=0 subtab=0 key=late_key reason=missing operation=settings_subtab_switch frame=77',
    '2026-06-11 02:00:04 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_subtab_switch","context":"online","page":"settings:qmclient","tab":"0","sample_frames":30,"sample_seconds":0.500,"fps_avg":144.000,"fps_min":120.000,"fps_max":180.000,"frame_ms_avg":6.944,"frame_ms_p95":8.000,"frame_ms_p99":9.000,"frame_ms_max":9.000,"cap_limited":0}',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_later_target_window_text.log', { invalidLines: 0, totalLines: 6 });

  assert.equal(summary.targetSettings.verdict, 'PASS');
  assert.equal(summary.targetSettings.stableTextCoverage.acceptanceBlocked, true);
  assert.equal(summary.targetSettings.stableTextCoverage.missCount, 1);
  assert.equal(summary.targetSettings.stableTextCoverage.candidateTotal, 12);
  assert.match(summary.targetSettings.stableTextCoverage.samples.join('\n'), /late_key/);
}

function testStableTextCoverageSeparatesPlanCoverageFromUtilization() {
  const entries = parseLog([
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_prebuild built=32 reused=8 remaining=0 budget=64 phase=before_target scope=target_settings operation=settings_open',
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_usage scope=target_settings page=settings:tclient tab=4 subtab=4 operation=settings_open frame=40 candidates=10 hits=7 reused=7 miss=3 stale=0 text_new=0 text_reused=7 planned=7 unplanned=3',
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_miss scope=target_settings page=settings:tclient tab=4 subtab=4 key=missing_a reason=missing plan_status=missing_descriptor operation=settings_open frame=40',
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_miss scope=target_settings page=settings:tclient tab=4 subtab=4 key=missing_b reason=missing plan_status=missing_descriptor operation=settings_open frame=40',
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_stale scope=target_settings page=settings:tclient tab=4 subtab=4 key=stale_c reason=style plan_status=key_mismatch operation=settings_open frame=40',
    '2026-06-11 02:00:01 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_open","context":"online","page":"settings:tclient","tab":"4","sample_frames":30,"sample_seconds":0.500,"fps_avg":144.000,"fps_min":120.000,"fps_max":180.000,"frame_ms_avg":6.944,"frame_ms_p95":8.000,"frame_ms_p99":9.000,"frame_ms_max":9.000,"cap_limited":0}',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_plan_coverage.log', { invalidLines: 0, totalLines: 6 });

  assert.equal(summary.targetSettings.stableTextCoverage.acceptanceBlocked, true);
  assert.equal(summary.targetSettings.stableTextCoverage.planCoverageAvailable, true);
  assert.equal(summary.targetSettings.stableTextCoverage.planCandidateCount, 7);
  assert.equal(summary.targetSettings.stableTextCoverage.visibleCandidateCount, 10);
  assert.equal(summary.targetSettings.stableTextCoverage.unplannedVisibleCount, 3);
  assert.equal(summary.targetSettings.stableTextCoverage.keyMismatchCount, 1);
  assert.match(summary.targetSettings.stableTextCoverage.consistencyWarnings.join('\n'), /unplanned visible stable text/);
  assert.match(summary.targetSettings.stableTextCoverage.samples.join('\n'), /plan_status=missing_descriptor/);

  const html = generateReport(entries, 'qm_perf_plan_coverage.log', null);
  assert.match(html, /Plan Coverage/);
  assert.match(html, /Unplanned/);
  assert.match(html, /Key Mismatch/);
  assert.match(html, /missing_descriptor/);
  assert.match(html, /key_mismatch/);
}

function testStableTextCoverageUsesPrebuildRemainingBeforeTargetOnly() {
  const entries = parseLog([
    '2026-06-11 01:59:59 I perf/settings-text: event=settings_text_prebuild built=32 reused=8 remaining=5 budget=64 phase=before_target scope=target_settings operation=settings_open',
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_usage scope=target_settings page=settings:tee tab=appearance subtab=assets operation=settings_open frame=40 candidates=8 hits=8 reused=8 miss=0 stale=0 text_new=0 text_reused=8 planned=8 unplanned=0',
    '2026-06-11 02:00:01 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_open","context":"online","page":"settings:tee","tab":"none","sample_frames":30,"sample_seconds":0.500,"fps_avg":144.000,"fps_min":120.000,"fps_max":180.000,"frame_ms_avg":6.944,"frame_ms_p95":8.000,"frame_ms_p99":9.000,"frame_ms_max":9.000,"cap_limited":0}',
    '2026-06-11 02:00:02 I perf/settings-text: event=settings_text_prebuild built=5 reused=40 remaining=0 budget=64 phase=before_target scope=target_settings operation=settings_open',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_prebuild_after_target.log', { invalidLines: 0, totalLines: 4 });

  assert.equal(summary.targetSettings.stableTextCoverage.acceptanceBlocked, true);
  assert.equal(summary.targetSettings.stableTextCoverage.prebuildRemainingBeforeTarget, 5);
}

function testStableTextCoverageBlocksWhenPlanCollectionIncompleteBeforeTarget() {
  const entries = parseLog([
    '2026-06-11 01:59:59 I perf/settings-text: event=settings_text_plan_collection units_done=2 units_total=10 remaining=8 budget=1 complete=0 dirty=0 phase=before_target scope=target_settings operation=settings_open',
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_prebuild built=32 reused=8 remaining=0 budget=64 phase=before_target scope=target_settings operation=settings_open',
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_usage scope=target_settings page=settings:tee tab=appearance subtab=assets operation=settings_open frame=40 candidates=8 hits=8 reused=8 miss=0 stale=0 text_new=0 text_reused=8 planned=8 unplanned=0',
    '2026-06-11 02:00:01 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_open","context":"online","page":"settings:tee","tab":"none","sample_frames":30,"sample_seconds":0.500,"fps_avg":144.000,"fps_min":120.000,"fps_max":180.000,"frame_ms_avg":6.944,"frame_ms_p95":8.000,"frame_ms_p99":9.000,"frame_ms_max":9.000,"cap_limited":0}',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_plan_collection_incomplete.log', { invalidLines: 0, totalLines: 4 });

  assert.equal(summary.targetSettings.stableTextCoverage.acceptanceBlocked, true);
  assert.equal(summary.targetSettings.stableTextCoverage.planCollectionAvailable, true);
  assert.equal(summary.targetSettings.stableTextCoverage.planCollectionComplete, false);
  assert.equal(summary.targetSettings.stableTextCoverage.planCollectionRemainingBeforeTarget, 8);
  assert.equal(summary.targetSettings.stableTextCoverage.prebuildRemainingBeforeTarget, 0);
  assert.match(summary.targetSettings.stableTextCoverage.consistencyWarnings.join('\n'), /stable text plan collection incomplete/);

  const html = generateReport(entries, 'qm_perf_plan_collection_incomplete.log', null);
  assert.match(html, /Plan Collection/);
  assert.match(html, /Collection Remaining/);
  assert.match(html, /Container Remaining/);
  assert.match(html, /Visible Coverage/);
}

function testStableTextCoverageBlocksWhenTargetUsageIsMissing() {
  const entries = parseLog([
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_prebuild built=32 reused=8 remaining=0 budget=64 phase=before_target scope=target_settings operation=settings_open',
    '2026-06-11 02:00:01 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_open","context":"online","page":"settings:tee","tab":"none","sample_frames":30,"sample_seconds":0.500,"fps_avg":144.000,"fps_min":120.000,"fps_max":180.000,"frame_ms_avg":6.944,"frame_ms_p95":8.000,"frame_ms_p99":9.000,"frame_ms_max":9.000,"cap_limited":0}',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_missing_usage.log', { invalidLines: 0, totalLines: 2 });

  assert.equal(summary.targetSettings.stableTextCoverage.acceptanceBlocked, true);
  assert.equal(summary.targetSettings.stableTextCoverage.utilizationAvailable, false);
  assert.match(summary.targetSettings.stableTextCoverage.consistencyWarnings.join('\n'), /missing settings_text_usage/);
}

function testAssetsVisibleFirstAdmissionAppearsInSummaryAndReport() {
  const entries = parseLog([
    '2026-06-14 02:15:22 I perf/assets: {"system":"perf/assets","frame":40,"page":"assets","stage":"assets_preview_draw_workshop_cards","duration_ms":41.332,"tab":1,"combined":797,"local_total":23,"remote_total":774,"rendered":42,"thumb_starts":12,"visible_first":1,"visible_starts":12,"prefetch_starts":0,"background_starts":0}',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_assets_visible_first.log', { invalidLines: 0, totalLines: 1 });

  assert.equal(summary.assetsPreviewAdmission.available, true);
  assert.equal(summary.assetsPreviewAdmission.visibleFirstAvailable, true);
  assert.equal(summary.assetsPreviewAdmission.maxDurationMs, 41.332);
  assert.equal(summary.assetsPreviewAdmission.maxRendered, 42);
  assert.equal(summary.assetsPreviewAdmission.maxThumbStarts, 12);
  assert.equal(summary.assetsPreviewAdmission.visibleStarts, 12);
  assert.equal(summary.assetsPreviewAdmission.prefetchStarts, 0);
  assert.equal(summary.assetsPreviewAdmission.backgroundStarts, 0);

  const html = generateReport(entries, 'qm_perf_assets_visible_first.log', null);
  assert.match(html, /Assets Visible-First Admission/);
  assert.match(html, /VISIBLE-FIRST/);
  assert.match(html, /Visible Starts/);
  assert.match(html, /Prefetch Starts/);
  assert.match(html, /Background Starts/);
}

function testAssetsVisibleReadyRequiresEveryPreflightReady() {
  const entries = parseLog([
    '2026-06-14 03:33:42 I perf/assets: {"system":"perf/assets","frame":40,"page":"assets","stage":"assets_visible_preflight","duration_ms":1.000,"tab":1,"visible_count":24,"half_visible_count":6,"ready_count":24,"not_ready_count":0,"visible_ready":1,"geometry_stable":1,"thumb_starts_before_visible":0,"thumb_starts_during_draw":0}',
    '2026-06-14 03:33:43 I perf/assets: {"system":"perf/assets","frame":41,"page":"assets","stage":"assets_visible_preflight","duration_ms":1.000,"tab":1,"visible_count":24,"half_visible_count":6,"ready_count":20,"not_ready_count":4,"visible_ready":0,"geometry_stable":1,"thumb_starts_before_visible":4,"thumb_starts_during_draw":0}',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_assets_mixed_visible_ready.log', { invalidLines: 0, totalLines: 2 });

  assert.equal(summary.assetsVisibleReady.available, true);
  assert.equal(summary.assetsVisibleReady.visibleReadyAvailable, false);
  assert.equal(summary.assetsVisibleReady.notReadyCount, 4);
  assert.match(summary.quality.warnings.join('\n'), /not_ready_count > 0/);
}

function testAssetsGeometryIgnoresEmptyOrMissingFieldSamples() {
  const entries = parseLog([
    '2026-06-14 03:33:42 I perf/assets: {"system":"perf/assets","frame":40,"page":"assets","stage":"assets_visible_preflight","duration_ms":1.000,"tab":1,"visible_count":24,"half_visible_count":6,"ready_count":24,"not_ready_count":0,"visible_ready":1,"geometry_stable":1,"thumb_starts_before_visible":0,"thumb_starts_during_draw":0}',
    '2026-06-14 03:33:43 I perf/assets: {"system":"perf/assets","frame":41,"page":"assets","stage":"assets_preview_draw_workshop_cards","duration_ms":1.000,"tab":1,"combined":0,"rendered":0,"thumb_starts":0,"visible_first":1,"visible_starts":0,"prefetch_starts":0,"background_starts":0}',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_assets_empty_draw.log', { invalidLines: 0, totalLines: 2 });

  assert.equal(summary.assetsVisibleReady.available, true);
  assert.equal(summary.assetsVisibleReady.geometryStable, true);
  assert.doesNotMatch(summary.quality.warnings.join('\n'), /geometry unstable/);
}

function testDemoBrowserPhasesAppearInSummaryAndReport() {
  const entries = parseLog([
    '2026-06-14 04:00:00 I perf/interaction: event=demo_browser_startup page=demo_browser items_total=120 items_scanned=120 items_done=120 remaining=0 metadata_remaining=240 budget=0 dur_ms=12.000 trigger=populate source=demos sort=3 fetch_info=1',
    '2026-06-14 04:00:01 I perf/interaction: event=demo_browser_header_fetch page=demo_browser items_total=120 items_scanned=2 items_done=2 visible_first=20 visible_end=40 visible_scanned=2 visible_done=2 background_scanned=0 background_done=0 remaining=118 budget=2 dur_ms=4.500 trigger=list_frame source=demos sort=3 fetch_info=1',
    '2026-06-14 04:00:02 I perf/interaction: event=demo_browser_date_fetch page=demo_browser items_total=120 items_scanned=4 items_done=4 visible_first=20 visible_end=40 visible_scanned=4 visible_done=4 background_scanned=0 background_done=0 remaining=116 budget=4 dur_ms=3.200 trigger=list_frame source=demos sort=3 fetch_info=1',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_demo_browser.log', { invalidLines: 0, totalLines: 3 });
  assert.equal(summary.demoBrowser.available, true);
  assert.equal(summary.demoBrowser.startupCount, 1);
  assert.equal(summary.demoBrowser.headerFetchCount, 1);
  assert.equal(summary.demoBrowser.dateFetchCount, 1);
  assert.equal(summary.demoBrowser.visibleScanned, 6);
  assert.equal(summary.demoBrowser.visibleDone, 6);
  assert.equal(summary.demoBrowser.backgroundScanned, 0);
  assert.equal(summary.demoBrowser.maxRemaining, 118);
  assert.equal(summary.demoBrowser.maxMetadataRemaining, 240);
  const html = generateReport(entries, 'qm_perf_demo_browser.log');
  assert.match(html, /Demo Browser/);
  assert.match(html, /demo_browser_header_fetch/);
  assert.match(html, /visible_scanned=2/);
  assert.match(html, /visible scanned/);
  assert.match(html, /metadata_remaining=240/);
  assert.match(html, /remaining=118/);
}

function testSettingsTextAnalysisAggregatesMissesByLocationReasonAndOperation() {
  const entries = parseLog([
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_prebuild built=10 reused=4 remaining=2 budget=16 phase=before_target scope=target_settings operation=settings_open',
    '2026-06-11 02:00:01 I perf/settings: event=settings_text_miss scope=target_settings page=settings:tee tab=appearance subtab=assets key=alpha reason=missing operation=settings_open frame=42',
    '2026-06-11 02:00:02 I perf/settings: event=settings_text_stale scope=target_settings page=settings:tee tab=appearance subtab=assets key=beta reason=style operation=settings_open frame=43',
    '2026-06-11 02:00:03 I perf/settings: event=settings_text_miss scope=target_settings page=settings:tee tab=appearance subtab=assets key=gamma reason=missing operation=settings_tab_switch frame=44',
    '2026-06-11 02:00:04 I perf/settings: event=settings_text_miss scope=settings page=settings:qmclient tab=0 subtab=-1 key=delta reason=cache_miss operation=settings_open frame=45',
  ].join('\n'));

  const analysis = settingsTextAnalysis(entries);

  assert.equal(analysis.summary.missCount, 3);
  assert.equal(analysis.summary.staleCount, 1);
  assert.equal(analysis.summary.prebuildCount, 1);
  assert.equal(analysis.summary.remainingPositiveCount, 1);
  assert.equal(analysis.topByLocation[0].location, 'settings:tee / appearance / assets');
  assert.equal(analysis.topByLocation[0].missCount, 2);
  assert.equal(analysis.topByLocation[0].staleCount, 1);
  assert.equal(analysis.topByReason[0].reason, 'missing');
  assert.equal(analysis.topByReason[0].missCount, 2);
  assert.equal(analysis.topByOperation[0].operation, 'settings_open');
  assert.equal(analysis.topByOperation[0].missCount, 2);
  assert.equal(analysis.prebuildSeries[0].built, 10);
  assert.equal(analysis.eventTimeline[0].event, 'settings_text_prebuild');
  assert.equal(analysis.eventTimeline.at(-1)?.event, 'settings_text_miss');
}

function testReportIncludesTextPoolMissAnalysisCharts() {
  const longKey = 'settings:1:-1:-1:settings-shell-title:fs160:al10:mw1475:us100:cm1:ch369197882:very-long-stable-text-key-that-must-not-wrap-vertically';
  const entries = parseLog([
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_prebuild built=10 reused=4 remaining=2 budget=16 phase=before_target scope=target_settings operation=settings_open',
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_usage scope=target_settings page=settings:tee tab=appearance subtab=assets operation=settings_open frame=40 candidates=8 hits=6 reused=5 miss=2 stale=0 text_new=1 text_reused=5 planned=6 unplanned=2',
    `2026-06-11 02:00:01 I perf/settings: event=settings_text_miss scope=target_settings page=settings:tee tab=appearance subtab=assets key=${longKey} reason=missing operation=settings_open frame=42`,
  ].join('\n'));

  const html = generateReport(entries, 'qm_perf_text_analysis.log', null);

  assert.match(html, /文本池 Miss 分析/);
  assert.match(html, /chart-text-location-top/);
  assert.match(html, /chart-text-reason-top/);
  assert.match(html, /chart-text-operation-top/);
  assert.match(html, /chart-text-prebuild/);
  assert.match(html, /chart-text-timeline/);
  assert.match(html, /settings_text_prebuild/);
  assert.match(html, /settings_text_miss/);
  assert.match(html, /<h2>文本池 Miss 分析<\/h2>[\s\S]*Stable Text Coverage[\s\S]*Hit Rate[\s\S]*75\.0%[\s\S]*Reuse Rate[\s\S]*62\.5%/);
  assert.match(html, /<h2>文本池 Miss 分析<\/h2>[\s\S]*Text New[\s\S]*1[\s\S]*Text Reused[\s\S]*5/);
  assert.match(html, /table-scroll/);
  assert.match(html, /sample-key-cell/);
  assert.match(html, /white-space:nowrap/);
  assert.match(html, new RegExp(`title="${longKey}`));
  assert.doesNotMatch(html, new RegExp(`>\\s*${longKey}\\s*<`));
}

function testAdaptiveBudgetEventsAppearInSummaryAndReport() {
	const entries = parseLog([
		'2026-06-14 05:00:00 I perf/settings-resource: event=settings_adaptive_budget mode=idle reason=progress frame_ms_avg=5.000 frame_ms_p95=6.000 target_ms=8.333 visible_tokens=8 prefetch_tokens=4 background_tokens=3 gpu_upload_tokens=2 text_tokens=6 demo_tokens=4 backlog=120 scroll=0 jump_scroll=0',
    '2026-06-14 05:00:01 I perf/settings-resource: event=settings_adaptive_budget mode=frame_pressure reason=frame_pressure frame_ms_avg=14.000 frame_ms_p95=20.000 target_ms=8.333 visible_tokens=2 prefetch_tokens=0 background_tokens=0 gpu_upload_tokens=1 text_tokens=1 demo_tokens=1 backlog=120 scroll=0 jump_scroll=0',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_adaptive_budget.log', { invalidLines: 0, totalLines: 2 });
  assert.equal(summary.adaptiveBudget.available, true);
  assert.equal(summary.adaptiveBudget.eventCount, 2);
  assert.equal(summary.adaptiveBudget.framePressureCount, 1);
  assert.equal(summary.adaptiveBudget.maxBackgroundTokens, 3);
  assert.equal(summary.adaptiveBudget.maxVisibleTokens, 8);

  const html = generateReport(entries, 'qm_perf_adaptive_budget.log');
  assert.match(html, /UI Frame Scheduler/);
	assert.match(html, /frame_pressure/);
	assert.match(html, /Visible Tokens/);
}

function testSettingsUiBudgetFieldsAppearInSummaryAndReport() {
	const entries = parseLog([
		'2026-06-15 21:00:00 I perf/ui_budget: event=settings_ui_budget page=settings:tclient operation=settings_open frame=101 tab=0 subtab=0 layout_ms=1.500 text_ms=2.250 text_new=3 text_reused=47 draw_calls=12 vertices=144 indices=216 heap_allocs=0 visible_widgets=31',
		'2026-06-15 21:00:01 I perf/ui_budget: event=settings_ui_budget page=settings:assets operation=settings_tab_switch frame=102 tab=1 subtab=0 layout_ms=0.500 text_ms=0.750 text_new=1 text_reused=12 draw_calls=8 vertices=96 indices=144 heap_allocs=0 visible_widgets=18',
	].join('\n'));

	const budget = settingsUiBudgetSummary(entries);
	const summary = summarizeForBundle(entries, 'qm_perf_ui_budget.log', { invalidLines: 0, totalLines: 2 });
	const html = generateReport(entries, 'qm_perf_ui_budget.log', null);

	assert.equal(budget.available, true);
	assert.equal(budget.eventCount, 2);
	assert.equal(budget.maxLayoutMs, 1.5);
	assert.equal(budget.maxTextMs, 2.25);
	assert.equal(budget.maxTextNew, 3);
	assert.equal(budget.maxDrawCalls, 12);
	assert.equal(budget.maxVertices, 144);
	assert.equal(budget.maxIndices, 216);
	assert.equal(budget.maxHeapAllocs, 0);
	assert.equal(budget.maxVisibleWidgets, 31);
	assert.equal(summary.settingsUiBudget.maxTextReused, 47);
	assert.match(html, /Settings UI Budget/);
	assert.match(html, /Draw Calls/);
	assert.match(html, /Heap Allocs/);
	assert.doesNotMatch(html, /APPROXIMATE/);
	assert.doesNotMatch(html, /REPORT ONLY/);
	assert.doesNotMatch(html, /Draw Calls Est\./);
	assert.doesNotMatch(html, /不能当完整 renderer\/allocator 计数/);
}

function testAssetsWorkshopCardSubstagesAppearInSummaryAndReport() {
	const entries = parseLog([
		'2026-06-15 21:01:00 I perf/assets: {"system":"perf/assets","frame":110,"page":"assets","stage":"assets_preview_draw_workshop_cards","duration_ms":6.000,"tab":1,"combined":30,"rendered":18,"thumb_starts":2,"visible_first":1,"visible_starts":2,"prefetch_starts":0,"background_starts":0}',
		'2026-06-15 21:01:00 I perf/assets: {"system":"perf/assets","frame":110,"page":"assets","stage":"assets_preview_draw_workshop_cards_layout_text","duration_ms":1.000,"tab":1,"rendered":18}',
		'2026-06-15 21:01:00 I perf/assets: {"system":"perf/assets","frame":110,"page":"assets","stage":"assets_preview_draw_workshop_cards_preview_draw","duration_ms":3.000,"tab":1,"rendered":18}',
		'2026-06-15 21:01:00 I perf/assets: {"system":"perf/assets","frame":110,"page":"assets","stage":"assets_preview_draw_workshop_cards_thumb_scheduling","duration_ms":0.500,"tab":1,"thumb_starts":2}',
	].join('\n'));

	const summary = summarizeForBundle(entries, 'qm_perf_assets_card_substages.log', { invalidLines: 0, totalLines: 4 });
	const html = generateReport(entries, 'qm_perf_assets_card_substages.log', null);

	assert.equal(summary.assetsPreviewAdmission.maxLayoutTextMs, 1);
	assert.equal(summary.assetsPreviewAdmission.maxPreviewDrawMs, 3);
	assert.equal(summary.assetsPreviewAdmission.maxThumbSchedulingMs, 0.5);
	assert.match(summary.assetsPreviewAdmission.samples.join('\n'), /layout_text_ms=1\.000/);
	assert.match(html, /Layout\/Text/);
	assert.match(html, /Preview Draw/);
	assert.match(html, /Thumb Scheduling/);
}

function testAdaptiveBudgetMissingWarnsWhenResourceWindowsExist() {
	const entries = parseLog([
		'2026-06-14 05:00:00 I perf/assets: {"system":"perf/assets","stage":"assets_preview_draw_workshop_cards","duration_ms":4.000,"tab":1,"combined":10,"rendered":10,"thumb_starts":1,"visible_first":1,"visible_starts":1,"prefetch_starts":0,"background_starts":0}',
    '2026-06-14 05:00:01 I perf/interaction: event=demo_browser_header_fetch page=demo_browser items_total=10 items_scanned=1 items_done=1 remaining=9 budget=1 dur_ms=1.000 trigger=list_frame source=demos sort=3 fetch_info=1',
    '2026-06-14 05:00:02 I perf/settings-text: event=settings_text_prebuild built=1 reused=0 remaining=1 budget=1 phase=before_target scope=target_settings operation=settings_open',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_missing_adaptive_budget.log', { invalidLines: 0, totalLines: 3 });
  assert.equal(summary.adaptiveBudget.available, false);
  assert.match(summary.quality.warnings.join('\n'), /adaptive budget telemetry missing/);
}

function testReportPreservesFailVerdictWhenStableTextIsNotBlocking() {
  const entries = parseLog([
    '2026-06-11 02:00:00 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"ingame_esc_open","context":"online","page":"game","tab":"none","sample_frames":30,"sample_seconds":0.500,"fps_avg":60.000,"fps_min":30.000,"fps_max":120.000,"frame_ms_avg":16.667,"frame_ms_p95":20.000,"frame_ms_p99":45.000,"frame_ms_max":45.000,"cap_limited":0}',
    '2026-06-11 02:00:00 I perf/settings-text: event=settings_text_plan_collection units_done=1 units_total=1 remaining=0 budget=1 complete=1 dirty=0 phase=before_target scope=target_settings operation=ingame_esc_open',
    '2026-06-11 02:00:01 I perf/settings-text: event=settings_text_prebuild built=80 reused=20 remaining=0 budget=100 phase=before_target scope=target_settings operation=ingame_esc_open',
    '2026-06-11 01:59:59 I perf/settings-text: event=settings_text_usage scope=target_settings page=game tab=none subtab=-1 operation=ingame_esc_open frame=41 candidates=20 hits=20 reused=20 miss=0 stale=0 text_new=0 text_reused=20 planned=20 unplanned=0',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_fail_verdict.log', { invalidLines: 0, totalLines: 2 });
  assert.equal(summary.targetSettings.verdict, 'FAIL');
  assert.equal(summary.targetSettings.verdictAvailable, true);
  assert.equal(summary.targetSettings.stableTextCoverage.acceptanceBlocked, false);

  const html = generateReport(entries, 'qm_perf_fail_verdict.log', null);
  assert.match(html, /当前目标判定：<span class="badge bad">FAIL<\/span>/);
  assert.doesNotMatch(html, /当前目标判定：<span class="badge bad">不足以验收<\/span>/);
}

function testGenericIngamePageContentDoesNotSatisfyOnlineCoverageWarning() {
  const entries = parseLog([
    '2026-06-11 02:00:00 I perf/menu: {"system":"perf/menu","frame":1,"stage":"ingame_page_content","duration_ms":7.000,"page":"game"}',
    '2026-06-11 02:00:01 I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"settings_tab_switch","context":"offline","page":"settings:tee","tab":"none","sample_frames":30,"sample_seconds":0.250,"fps_avg":120.000,"fps_min":100.000,"fps_max":180.000,"frame_ms_avg":8.000,"frame_ms_p95":9.000,"frame_ms_p99":10.000,"frame_ms_max":10.000,"cap_limited":0}',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_generic_ingame.log', { invalidLines: 0, totalLines: 2 });

  assert.match(summary.quality.warnings.join('\n'), /ingame\/online/i);
}

function testPerfEventClassifiersKeepBoundariesTight() {
  const entries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=settings_page_content duration_ms=6.000 frame=10 page=settings:tee',
    '2026-06-04 12:00:01 I perf/interaction: event=page_switch page=settings from=general to=tee dur_ms=12.500 frame=11',
    '2026-06-04 12:00:02 I perf/interaction: event=list_frame page=server_browser dur_ms=5.250 frame=12',
    '2026-06-04 12:00:03 I perf/section: event=section page=settings:tee section=identity dur_ms=4.750 frame=13',
    '2026-06-04 12:00:04 I perf/interaction: event=section page=settings:tee section=not-a-section dur_ms=99.000 frame=14',
    '2026-06-04 12:00:05 I perf/settings-resource: event=work_drain page=settings:tee dur_ms=9.000 frame=15',
    '2026-06-04 12:00:06 I perf/device: event=sample frame=16 gpu_util_percent=61.5',
  ].join('\n'));

  assert.equal(isFrameTimeEntry(entries[0]), true);
  assert.equal(isPageSwitchEvent(entries[1]), true);
  assert.equal(isListFrameEvent(entries[2]), true);
  assert.equal(isUiRebuildEvent(entries[3]), true);
  assert.equal(isUiRebuildEvent(entries[4]), false);
  assert.equal(isWorkDrainEvent(entries[5]), true);
  assert.equal(isFrameTimeEntry(entries[6]), false);
}

function testPageSwitchBoundaryDoesNotEnterDurationAttribution() {
  const entries = parseLog([
    '2026-06-04 12:00:00 I perf/interaction: event=page_switch page=settings from=general to=tee dur_ms=0.000 frame=10',
    '2026-06-04 12:00:01 I perf/section: event=section page=settings:tee section=identity dur_ms=4.750 visible=1 dirty=config text_new=2 text_reused=8 frame=11',
  ].join('\n'));

  const attribution = pagePerformanceAttribution(entries);

  assert.equal(attribution.length, 1);
  assert.equal(attribution[0].kind, 'UI Rebuild');
  assert.equal(attribution[0].durationMs, 4.75);
}

function testSnapshotIgnoresEventOnlyTelemetry() {
  const entries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=settings_page_content duration_ms=6.000 frame=10 page=settings:tee',
    '2026-06-04 12:00:01 I perf/interaction: event=tee_enter frame=10 page=settings:tee visible_rows=8 first_visible_skin=default',
    '2026-06-04 12:00:02 I perf/device: event=sample frame=12 gpu_util_percent=61.5 cpu_process_percent=12 memory_process_mb=1024',
    '2026-06-04 12:00:03 I perf/menu: stage=settings_page_content duration_ms=10.000 frame=13 page=settings:tee',
  ].join('\n'));

  const current = snapshot(entries, 'qm_perf_current.log');

  assert.equal(current.totalFrames, 2);
  assert.equal(current.percentiles.min, 6);
  assert.equal(current.percentiles.p50, 6);
  assert.equal(current.percentiles.p95, 10);
  assert.equal(current.compliance.h240, 0);
}

function testComparisonReportWarnsAboutAutomaticBaseline() {
  const previousEntries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=settings_page_content duration_ms=8.000 frame=10 page=settings:tee',
  ].join('\n'));
  const currentEntries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=settings_page_content duration_ms=6.000 frame=10 page=settings:tee',
  ].join('\n'));
  const comparison = compareSessions(
    snapshot(previousEntries, 'qm_perf_previous.log'),
    snapshot(currentEntries, 'qm_perf_current.log'),
  );

  const html = generateReport(currentEntries, 'qm_perf_current.log', comparison);

  assert.match(html, /自动选择的上一份日志/);
  assert.match(html, /不作为严格回归判定/);
}

function testComparisonReportMarksDifferentOperationsAsAdvisory() {
  const previousEntries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=demo_browser duration_ms=8.000 frame=10 page=demo_browser',
  ].join('\n'));
  const currentEntries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=settings_page_content duration_ms=6.000 frame=10 page=settings:tee',
  ].join('\n'));
  const comparison = compareSessions(
    snapshot(previousEntries, 'qm_perf_previous.log'),
    snapshot(currentEntries, 'qm_perf_current.log'),
  );

  const html = generateReport(currentEntries, 'qm_perf_current.log', comparison);

  assert.equal(comparison.operation.comparable, false);
  assert.match(html, /advisory only/i);
  assert.match(html, /page set differs/i);
}

function testOperationCompatibilityChecksEventsAndStages() {
  const previousEntries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=settings_page_content duration_ms=6.000 frame=10 page=settings:tee',
    '2026-06-04 12:00:01 I perf/interaction: event=page_switch page=settings:tee dur_ms=4.000 frame=11',
  ].join('\n'));
  const currentEntries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=settings_render_total duration_ms=6.000 frame=10 page=settings:tee',
    '2026-06-04 12:00:01 I perf/interaction: event=list_frame page=settings:tee dur_ms=4.000 frame=11',
  ].join('\n'));

  const result = compareOperationSignatures(operationSignature(previousEntries), operationSignature(currentEntries));

  assert.equal(result.comparable, false);
  assert.match(result.reason, /event set differs|stage set differs/i);
}

function testReportQualityExplainsMissingAndBiasedData() {
  const entries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=settings_page_content duration_ms=10.000 frame=10 page=settings:tee',
    '2026-06-04 12:00:01 I perf/menu: stage=settings_page_content duration_ms=11.000 frame=11 page=settings:tee',
    '2026-06-04 12:00:02 I perf/menu: stage=settings_page_content duration_ms=12.000 frame=12 page=settings:tee',
    '2026-06-04 12:00:03 I perf/menu: stage=settings_page_content duration_ms=13.000 frame=13 page=settings:tee',
    '2026-06-04 12:00:04 I perf/menu: stage=settings_page_content duration_ms=14.000 frame=14 page=settings:tee',
    '2026-06-04 12:00:05 I perf/menu: stage=settings_page_content duration_ms=15.000 frame=15 page=settings:tee',
    '2026-06-04 12:00:06 I perf/menu: stage=settings_page_content duration_ms=16.000 frame=16 page=settings:tee',
    '2026-06-04 12:00:07 I perf/menu: stage=settings_page_content duration_ms=17.000 frame=17 page=settings:tee',
    '2026-06-04 12:00:08 I perf/menu: stage=settings_page_content duration_ms=18.000 frame=18 page=settings:tee',
    '2026-06-04 12:00:09 I perf/menu: stage=settings_page_content duration_ms=19.000 frame=19 page=settings:tee',
  ].join('\n'));

  const quality = reportQuality(entries, { invalidLines: 2, totalLines: 12 });

  assert.equal(quality.sampleCount, 10);
  assert.equal(quality.invalidLines, 2);
  assert.equal(quality.biased, true);
  assert.match(quality.warnings.join('\n'), /sampling threshold/i);
  assert.match(quality.warnings.join('\n'), /invalid lines/i);
  assert.deepEqual(quality.operation.pages, ['settings:tee']);
}

function testOperationCompatibilityIsExplicit() {
  const settingsEntries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=settings_page_content duration_ms=6.000 frame=10 page=settings:tee',
  ].join('\n'));
  const demoEntries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=demo_browser duration_ms=9.000 frame=10 page=demo_browser',
  ].join('\n'));

  const result = compareOperationSignatures(operationSignature(settingsEntries), operationSignature(demoEntries));

  assert.equal(result.comparable, false);
  assert.match(result.reason, /page set differs/i);
}

function testBundleSummaryIsStableJsonShape() {
  const entries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=settings_page_content duration_ms=6.000 frame=10 page=settings:tee',
    '2026-06-04 12:00:01 I perf/section: event=section page=settings:tee section=identity dur_ms=4.000 visible=1 dirty=unknown text_new=0 text_reused=0 frame=11',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_current.log', { invalidLines: 0, totalLines: 2 });

  assert.equal(summary.sourceFile, 'qm_perf_current.log');
  assert.equal(summary.quality.sampleCount, 1);
  assert.deepEqual(summary.quality.operation.pages, ['settings:tee']);
  assert.equal(summary.attribution.top.length, 1);
  assert.equal(summary.attribution.top[0].kind, 'UI Rebuild');
  assert.equal(typeof summary.generatedAt, 'string');
}

function testSamplingBiasReportUsesP5Estimate() {
  const entries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=settings_page_content duration_ms=1.000 frame=10 page=settings:tee',
    ...Array.from({ length: 19 }, (_, i) =>
      `2026-06-04 12:00:${String(i + 1).padStart(2, '0')} I perf/menu: stage=settings_page_content duration_ms=10.000 frame=${i + 11} page=settings:tee`),
  ].join('\n'));

  const html = generateReport(entries, 'qm_perf_sampling_bias.log', null);

  assert.match(html, /p5=10\.0ms/);
  assert.doesNotMatch(html, /当前采样阈值为 1\.0ms/);
  assert.doesNotMatch(html, /当前采样阈值 1\.0ms/);
}

function testSamplingBiasUsesDefaultLoggingThreshold() {
  assert.equal(isSamplingBiased(Array(20).fill(4.1)), true);
}

function testKpiThresholdsAlignWithVerdictBudget() {
  const entries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=settings_page_content duration_ms=16.500 frame=10 page=settings:tee',
  ].join('\n'));
  const html = generateReport(entries, 'qm_perf_thresholds.log', null);

  assert.equal(computeVerdict(snapshot(entries, 'qm_perf_thresholds.log').percentiles, 0), 'PASS');
  assert.match(html, /<div class="kpi-label">p99<\/div><div class="kpi-value ok">16\.5<\/div>/);
  assert.match(html, /<div class="kpi-label">Max<\/div><div class="kpi-value ok">16\.5<\/div>/);
}

function testReportIncludesSectionTop10() {
  const entries = parseLog([
    '2026-06-04 12:00:00 I perf/section: event=section page=settings:tee section=identity dur_ms=4.000 visible=1 dirty=unknown text_new=0 text_reused=0 frame=10',
    '2026-06-04 12:00:01 I perf/section: event=section page=settings:tee section=identity dur_ms=12.000 visible=1 dirty=unknown text_new=0 text_reused=0 frame=11',
    '2026-06-04 12:00:02 I perf/section: event=section page=settings:qmclient section=theme dur_ms=7.000 visible=1 dirty=unknown text_new=0 text_reused=0 frame=12',
    '2026-06-04 12:00:03 I perf/interaction: event=section page=settings:tee section=not-a-section dur_ms=99.000 frame=13',
    '2026-06-04 12:00:04 I perf/menu: stage=settings_page_content duration_ms=8.000 frame=14 page=settings:tee',
  ].join('\n'));

  const html = generateReport(entries, 'qm_perf_sections.log', null);

  assert.match(html, /Section Top-10/);
  assert.match(html, /settings:tee/);
  assert.match(html, /identity/);
  assert.match(html, /12\.000ms/);
  assert.doesNotMatch(html, /not-a-section/);
}

function testReportShowsQualityAndUnavailableData() {
  const entries = parseLog([
    '2026-06-04 12:00:00 I perf/interaction: event=tee_enter frame=10 page=settings:tee visible_rows=8',
  ].join('\n'));

  const html = generateReport(entries, 'qm_perf_empty_frame_data.log', null, {
    totalLines: 2,
    invalidLines: 1,
  });

  assert.match(html, /样本可信度/);
  assert.match(html, /N\/A/);
  assert.match(html, /WARN/);
  assert.doesNotMatch(html, /<span class="verdict-banner ok">PASS<\/span>/);
  assert.doesNotMatch(html, /性能表现良好/);
  assert.match(html, /没有可用的 frame-time 样本/);
  assert.match(html, /no frame-time samples/i);
  assert.match(html, /invalid lines/i);
}

function testReportCoreKpisUseFrameTimeSamplesConsistently() {
  const entries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=settings_page_content duration_ms=1.000 frame=10 page=settings:tee',
    '2026-06-04 12:00:01 I perf/gameclient: stage=render_frame duration_ms=20.000 frame=11 page=settings:tee',
  ].join('\n'));

  const html = generateReport(entries, 'qm_perf_global_frame.log', null);

  assert.match(html, /<div class="kpi-label">p99<\/div><div class="kpi-value warn">20\.0<\/div>/);
  assert.match(html, /最大尖峰耗时 20\.0ms/);
}

function testReportTooltipsCanFloatOutsideCharts() {
  const entries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=settings_page_content duration_ms=6.000 frame=10 page=settings:tee',
  ].join('\n'));

  const html = generateReport(entries, 'qm_perf_tooltip.log', null);

  assert.match(html, /\.figure \.chart-wrap\{[^}]*overflow:visible/);
  assert.match(html, /const tooltipPosition = /);
  assert.match(html, /position: tooltipPosition/);
  assert.match(html, /pointer-events:none/);
}

function testReportSamplesLargeEmbeddedChartData() {
  const lines: string[] = [];
  for (let i = 0; i < 1200; i++) {
    const second = String(i % 60).padStart(2, '0');
    lines.push(`2026-06-04 12:00:${second} I perf/menu: stage=settings_page_content duration_ms=${(1 + (i % 40) / 10).toFixed(3)} frame=${i} page=settings:tee`);
    lines.push(`2026-06-04 12:00:${second} I perf/settings-text: event=settings_text_miss scope=target page=settings:general tab=-1 subtab=-1 reason=key_mismatch plan_status=missing operation=settings_open key=very-long-settings-key-${i}`);
  }

  const html = generateReport(parseLog(lines.join('\n')), 'qm_perf_large_report.log', null);
  const match = html.match(/^const DATA = ([^\n]*);$/m);
  assert.ok(match);
  const data = JSON.parse(match[1]);

  // Regression guard for 2GB logs: the HTML report must contain statistical
  // samples for charts, not the full raw event stream.
  assert.ok(data.timeline.times.length <= 600);
  assert.ok(data.textAnalysis.eventTimeline.length <= 240);
  assert.match(html, /chart-sampled-note/);
}

function testSummaryJsonCanBeSerializedForDebugBundle() {
  const entries = parseLog([
    '2026-06-04 12:00:00 I perf/menu: stage=settings_page_content duration_ms=6.000 frame=10 page=settings:tee',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'C:/tmp/qm_perf_current.log', { invalidLines: 0, totalLines: 1 });
  const parsed = JSON.parse(JSON.stringify(summary));

  assert.equal(parsed.sourceFile, 'qm_perf_current.log');
  assert.equal(parsed.quality.operation.pages[0], 'settings:tee');
  assert.equal(parsed.verdict, 'PASS');
}

function testSummaryJsonMarksUnavailableVerdictForEmptyFrameSamples() {
  const entries = parseLog([
    '2026-06-04 12:00:00 I perf/interaction: event=tee_enter frame=10 page=settings:tee visible_rows=8',
  ].join('\n'));

  const summary = summarizeForBundle(entries, 'qm_perf_empty.log', { invalidLines: 0, totalLines: 1 });
  const current = snapshot(entries, 'qm_perf_empty.log');

  assert.equal(summary.verdict, 'WARN');
  assert.equal(summary.verdictAvailable, false);
  assert.equal(current.verdict, 'WARN');
  assert.equal(current.totalFrames, 0);
  assert.match(summary.quality.warnings.join('\n'), /no frame-time samples/i);
}

function testAnalyzeWritesBundleAndArchiveSummaryFiles() {
  const source = readFileSync(new URL('./analyze.ts', import.meta.url), 'utf-8');

  assert.match(source, /createReadStream/);
  assert.match(source, /createInterface/);
  assert.match(source, /parseLine/);
  assert.match(source, /perf_summary\.json/);
  assert.match(source, /\$\{logName\}_summary\.json/);
  assert.match(source, /summarizeForBundle/);
}

function nonCardMenuFixture(options: { missingCache?: boolean; missingRealSample?: boolean; exceed?: boolean } = {}) {
  return Object.entries(NON_CARD_MENU_BASELINES).flatMap(([operation, budget], index) => {
    const p99 = options.exceed && index === 0 ? budget.p99 + 1 : budget.p99 - 1;
    const onePctSource = options.missingRealSample && index === 1 ? 'p99_derived' : 'real_sampled';
    const cacheFields = options.missingCache && index === 2 ? '' : ' cache_hits=9 cache_misses=1 cache_evictions=0';
    return [
      `2026-07-13 12:00:${String(index).padStart(2, '0')} I perf/fps: {"system":"perf/fps","event":"fps_summary","operation":"${operation}","context":"scroll","page":"menu","tab":"none","sample_frames":120,"sample_seconds":2,"fps_avg":120,"fps_min":60,"fps_max":240,"fps_1pct_low":90,"fps_1pct_source":"${onePctSource}","window_start_frame":${index * 200},"window_end_frame":${index * 200 + 119},"frame_ms_avg":4,"frame_ms_p95":${budget.p95 - 1},"frame_ms_p99":${p99},"frame_ms_max":${budget.max - 1},"menu_ms_max":${budget.menuMax - 1},"cap_limited":0}`,
      `2026-07-13 12:01:${String(index).padStart(2, '0')} I perf/menu-ui: event=menu_ui_frame page=menu operation=${operation} frame=${index * 200 + 10} items_total=100 items_visible=10 items_processed=10 items_skipped=90 ui_ms=3.000 layout_ms=-1.000 text_ms=-1.000 heap_allocs=-1${cacheFields} source=qm_ui_perf`,
    ];
  }).join('\n');
}

function testNonCardMenuBudgetsCoverEightOperationsAndRejectIncompleteEvidence() {
  assert.equal(Object.keys(NON_CARD_MENU_BASELINES).length, 8);
  const passing = nonCardMenuBudgetSummary(parseLog(nonCardMenuFixture()));
  assert.equal(passing.operations.length, 8);
  assert.ok(passing.operations.every(operation => operation.verdict === 'PASS'));

  const missingReal = nonCardMenuBudgetSummary(parseLog(nonCardMenuFixture({ missingRealSample: true })));
  assert.equal(missingReal.operations.find(operation => operation.operation === 'friends_scroll')?.verdict, 'FAIL');
  assert.match(missingReal.operations.find(operation => operation.operation === 'friends_scroll')?.reason ?? '', /real_sampled/);

  const missingCache = nonCardMenuBudgetSummary(parseLog(nonCardMenuFixture({ missingCache: true })));
  assert.equal(missingCache.operations.find(operation => operation.operation === 'demo_browser_scroll')?.verdict, 'WARN');
  assert.match(missingCache.operations.find(operation => operation.operation === 'demo_browser_scroll')?.reason ?? '', /cache/i);

  const exceeded = nonCardMenuBudgetSummary(parseLog(nonCardMenuFixture({ exceed: true })));
  const server = exceeded.operations.find(operation => operation.operation === 'server_browser_scroll');
  assert.equal(server?.verdict, 'FAIL');
  assert.match(server?.reason ?? '', /p99.*16\.7/);

  const fpsOnly = parseLog(nonCardMenuFixture().split('\n').filter(line => line.includes('perf/fps')).join('\n'));
  const missingAllMenuWork = nonCardMenuBudgetSummary(fpsOnly);
  assert.equal(missingAllMenuWork.available, true);
  assert.ok(missingAllMenuWork.operations.every(operation => operation.verdict === 'FAIL'));

  const mismatchedFrames = nonCardMenuBudgetSummary(parseLog(nonCardMenuFixture().replace(/frame=(\d+)/g, (_match, frame) => `frame=${Number(frame) + 1000}`)));
  assert.ok(mismatchedFrames.operations.every(operation => operation.verdict === 'FAIL'));
}

function testNonCardMenuReportShowsFixedBudgetAndCacheWorkTable() {
  const html = generateReport(parseLog(nonCardMenuFixture()), 'qm_perf_non_card_menu.log', null);
  assert.match(html, /非卡片菜单预算/);
  assert.match(html, /server_browser_scroll/);
  assert.match(html, /dropdown_first_wheel/);
  assert.match(html, /Cache Hit/);
  assert.match(html, /Cache Miss/);
  assert.match(html, /Eviction/);
  assert.match(html, /Processed/);
  assert.match(html, /Skipped/);
}

function testAnalyzeSupportsExactOutputAndSiblingSummary() {
  const source = readFileSync(join(dirname(fileURLToPath(import.meta.url)), 'analyze.ts'), 'utf-8');
  assert.match(source, /--output/);
  assert.match(source, /resolve\(outputPath\)/);
  assert.match(source, /extname\(outPath\)/);
  assert.match(source, /summary\.json/);
}

testParseKeepsEventOnlyPerfLines();
testParseSupportsJsonLinesEvents();
testParseLogWithDiagnosticsCountsInvalidLines();
testReportIncludesInteractionAndDeviceSections();
testReportShowsGenerationDuration();
testReportAttributesPagePerformanceEvents();
testServerBrowserListFrameAttributionUsesRowCounts();
testQmUiRuntimeAttributionUsesDedicatedKind();
testFpsSummaryTelemetryFeedsReportAndBundleSummary();
testPerfAnalyzerReportsRealSampledOnePctLow();
testPreviewBudgetSummaryAndColdWarmTabSwitches();
testTextRuntimeBudgetSummary();
testStableTextCoverageExcludesDynamicTextFromStaticHitRate();
testUnifiedFrameSchedulerAndTextPipelineBudgetSummary();
testPerfAnalyzerReportsStaticSnapshotParagraphHitRates();
testPerfAnalyzerCorrelatesOnePctLowWithTextAndResourceBudgets();
testReportUsesStatisticalBudgetReportInsteadOfRawBudgetDump();
testBudgetCorrelationSummaryByFpsWindow();
testBudgetCorrelationAttributesRenderStageWhenBudgetCountersAreMissing();
testTargetFpsFailureRequiresConcreteUiSectionAttribution();
testBudgetCorrelationDoesNotUseStringFallbackWithoutFrameWindow();
testBudgetCorrelationUsesMetadataHydrateForMetadataLayoutCulprit();
testPerfOverheadIsReportedAsCulprit();
testMainFpsTableDoesNotMarkP99DerivedAsPassing();
testBudgetCorrelationRanksCulprits();
testBudgetCorrelationKeepsOverlappingWindowsOperationScoped();
testBudgetCorrelationKeepsHigherCostCulpritAboveConcreteUiSection();
testBudgetCorrelationTreatsUiLayoutTotalAsAggregateWhenRankingConcreteSections();
testFpsBaselineFailsIngameEscAndAssetsTabSwitchWindowsIndependently();
testBackendRemainsSeparateWhenUiLayoutDominatesAndUploadsAreZero();
testPerfAnalyzerFailsUnattributedLowFpsWindow();
testMissingOnePctLowIsMarkedP99DerivedAndNotTargetPass();
testMissingFpsSummaryWarnsThatSettingsAcceptanceIsIncomplete();
testTargetSettingsVerdictIgnoresServerBrowserFrames();
testTargetSettingsVerdictUsesIngameEscFpsSummaryWindow();
testStableTextCoverageBlocksSettingsAcceptanceEvenWithBackgroundHotspots();
testStableTextHitRateUsesRenderReadyHitInsteadOfPoolHit();
testStableTextFallbackImmediateBlocksAcceptanceEvenWhenPoolHitIsPerfect();
testReportDistinguishesPoolHitRateFromRenderReadyHitRate();
testStableTextCoverageIgnoresOrdinarySettingsPagesOutsideTargetWindow();
testStableTextCoverageIncludesLaterTargetWindows();
testStableTextCoverageUsesPrebuildRemainingBeforeTargetOnly();
testStableTextCoverageBlocksWhenPlanCollectionIncompleteBeforeTarget();
testStableTextCoverageBlocksWhenTargetUsageIsMissing();
testAssetsVisibleFirstAdmissionAppearsInSummaryAndReport();
testAssetsVisibleReadyRequiresEveryPreflightReady();
testAssetsGeometryIgnoresEmptyOrMissingFieldSamples();
testDemoBrowserPhasesAppearInSummaryAndReport();
testSettingsTextAnalysisAggregatesMissesByLocationReasonAndOperation();
testReportIncludesTextPoolMissAnalysisCharts();
testAdaptiveBudgetEventsAppearInSummaryAndReport();
testSettingsUiBudgetFieldsAppearInSummaryAndReport();
testAssetsWorkshopCardSubstagesAppearInSummaryAndReport();
testAdaptiveBudgetMissingWarnsWhenResourceWindowsExist();
testReportPreservesFailVerdictWhenStableTextIsNotBlocking();
testGenericIngamePageContentDoesNotSatisfyOnlineCoverageWarning();
testPerfEventClassifiersKeepBoundariesTight();
testPageSwitchBoundaryDoesNotEnterDurationAttribution();
testSnapshotIgnoresEventOnlyTelemetry();
testComparisonReportWarnsAboutAutomaticBaseline();
testComparisonReportMarksDifferentOperationsAsAdvisory();
testOperationCompatibilityChecksEventsAndStages();
testReportQualityExplainsMissingAndBiasedData();
testOperationCompatibilityIsExplicit();
testBundleSummaryIsStableJsonShape();
testSamplingBiasReportUsesP5Estimate();
testSamplingBiasUsesDefaultLoggingThreshold();
testKpiThresholdsAlignWithVerdictBudget();
testReportIncludesSectionTop10();
testReportShowsQualityAndUnavailableData();
testReportCoreKpisUseFrameTimeSamplesConsistently();
testReportTooltipsCanFloatOutsideCharts();
testReportSamplesLargeEmbeddedChartData();
testSummaryJsonCanBeSerializedForDebugBundle();
testSummaryJsonMarksUnavailableVerdictForEmptyFrameSamples();
testAnalyzeWritesBundleAndArchiveSummaryFiles();
testNonCardMenuBudgetsCoverEightOperationsAndRejectIncompleteEvidence();
testNonCardMenuReportShowsFixedBudgetAndCacheWorkTable();
testAnalyzeSupportsExactOutputAndSiblingSummary();

function testUnifiedFrameSamplesDoNotMixComponentDurations() {
  const batch = parseLine('{"system":"perf/frame","event":"frame_batch","session":1,"frames":[10,12],"durations_ms":[0.5,20]}')!;
  const frames = expandFrameBatch(batch);
  assert.equal(frames.length, 2);
  assert.equal(frames[0].durationMs, 0.5);
  assert.equal(Number(frames[1].fields.frame), 12);
  const component = parseLine('{"system":"perf/gameclient","stage":"components_total","duration_ms":999}')!;
  assert.deepEqual(selectFrameTimeEntries([...frames, component]), frames);
  assert.throws(() => expandFrameBatch(parseLine('{"system":"perf/frame","event":"frame_batch","frames":[1],"durations_ms":[]}')!));
}

function testUnifiedConfigurationKeepsInitialCurrentAndStringValues() {
  const cfg = (name: string, value: string, revision: number, initial: number, part = 0, parts = 1) => parseLine(JSON.stringify({
    system: 'perf/config', event: 'config_value', session: 1, name, owner: name.startsWith('qm_') ? 'qmclient' : 'ddnet',
    type: 'string', value, revision, initial, part, parts, is_default: 0, redacted: 0,
  }))!;
  const records = [cfg('player_name', '001', 1, 1), cfg('qm_long_text', '中文 "', 1, 1, 0, 2), cfg('qm_long_text', 'tail', 1, 1, 1, 2),
    cfg('player_name', '<script>alert(1)</script>', 2, 0), cfg('qm_voice_token', 'never-export-this', 1, 1)];
  const summary = configurationSummary(records);
  assert.equal(summary.available, true);
  const player = summary.variables.find(v => v.name === 'player_name')!;
  assert.equal(player.initialValue, '001');
  assert.equal(player.value, '<script>alert(1)</script>');
  assert.equal(player.changed, true);
  assert.equal(summary.variables.find(v => v.name === 'qm_long_text')!.value, '中文 "tail');
  assert.equal(summary.variables.find(v => v.name === 'qm_voice_token')!.value, '<redacted>');
  const html = generateReport(records, 'config.log');
  assert.ok(html.includes('当前客户端配置'));
  assert.ok(html.includes('&lt;script&gt;'));
  assert.ok(!html.includes('never-export-this'));
  assert.ok(!html.includes('<script>alert(1)</script>'));
  assert.equal(summarizeForBundle(records, 'config.log', { totalLines: records.length, invalidLines: 0 }).configuration.variables.length, 3);
  assert.equal(configurationSummary([cfg('qm_long_text', 'partial', 2, 0, 0, 2)]).incomplete, true);
  const manifest = parseLine('{"system":"perf/config","event":"config_snapshot","session":1,"expected_variables":4}')!;
  assert.equal(configurationSummary([manifest, ...records]).incomplete, true);
}

function testUnifiedCollectorBoundsMemoryAndKeepsLatestConfig() {
  const collector = new PerfLogCollector(16, 8, 8);
  for(let i = 0; i < 1000; i++) {
    collector.add(parseLine(JSON.stringify({system: 'perf/frame', event: 'frame_sample', duration_ms: i % 20 + 1, frame: i}))!);
    collector.add(parseLine(JSON.stringify({system: 'perf/config', event: 'config_value', session: 1, name: 'tc_enabled', owner: 'tclient', type: 'int', value: String(i), initial: i === 0 ? 1 : 0, revision: i + 1, part: 0, parts: 1}))!);
  }
  const result = collector.finish({ totalLines: 2000, invalidLines: 0 });
  assert.ok(result.entries.length <= 18);
  assert.ok(result.diagnostics.sampledEntries! > 0);
  const config = configurationSummary(result.entries).variables[0];
  assert.equal(config.initialValue, 0);
  assert.equal(config.value, 999);
  assert.ok(reportQuality(result.entries, result.diagnostics).warnings.some(w => w.includes('analysis_sampled')));
}

testUnifiedFrameSamplesDoNotMixComponentDurations();
testUnifiedConfigurationKeepsInitialCurrentAndStringValues();
testUnifiedCollectorBoundsMemoryAndKeepsLatestConfig();

console.log('qmclient perf tests passed');
