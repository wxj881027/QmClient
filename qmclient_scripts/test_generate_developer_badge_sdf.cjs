const assert = require('node:assert/strict');
const test = require('node:test');
const { layerSvg, LAYERS } = require('./generate_developer_badge_sdf.cjs');

test('文字距离场只使用指定字形路径，排除胶囊、兔子和其他文字', () => {
    const source = '<svg><path id="developer-logo" d="M0 0H16V16Z"/><path id="developer-wordmark" d="M145 24H170V70Z" fill="#D8FFF5"/><g opacity="0.55"><path id="developer-code" d="M290 36L280 48L290 60" stroke="#8BDFCF"/></g></svg>';
    const wordmark = layerSvg(source, LAYERS[0]);
    const code = layerSvg(source, LAYERS[1]);
    assert.ok(wordmark.includes('d="M145 24H170V70Z"'));
    assert.ok(!wordmark.includes('id="developer-code"'));
    assert.ok(!wordmark.includes('id="developer-logo"'));
    assert.ok(code.includes('d="M290 36L280 48L290 60"'));
    assert.ok(!code.includes('id="developer-wordmark"'));
    // 半透明由 GPU 应用一次，不能提前削弱距离场的轮廓掩码。
    assert.ok(!code.includes('opacity='));
    assert.ok(code.includes('transform="scale(0.25)"'));
});

test('缺少或重复的字形路径明确失败', () => {
    assert.throws(() => layerSvg('<svg/>', LAYERS[0]));
    const duplicate = '<path id="developer-wordmark" d="M0 0H1V1Z"/>';
    assert.throws(() => layerSvg(`<svg>${duplicate}${duplicate}</svg>`, LAYERS[0]));
});
