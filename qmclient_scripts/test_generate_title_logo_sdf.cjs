const assert = require('node:assert/strict');
const test = require('node:test');
const { signedDistances } = require('./generate_title_logo_sdf.cjs');

test('直线轮廓落在相邻像素中心之间，内部为正', () => {
    const mask = Uint8Array.from({ length: 24 }, (_, i) => i % 8 >= 3 ? 1 : 0);
    const distances = signedDistances(mask, 8, 3);
    assert.deepEqual(Array.from(distances.slice(8, 16)), [-2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5, 4.5]);
});

test('封闭轮廓内的孔洞保留负号', () => {
    const mask = Uint8Array.from([
        0, 0, 0, 0, 0,
        0, 1, 1, 1, 0,
        0, 1, 0, 1, 0,
        0, 1, 1, 1, 0,
        0, 0, 0, 0, 0,
    ]);
    const distances = signedDistances(mask, 5, 5);
    assert.equal(distances[12], -0.5);
    assert.equal(distances[11], 0.5);
    assert.ok(distances[0] < 0);
});

test('空轮廓和无效尺寸明确失败', () => {
    assert.throws(() => signedDistances(new Uint8Array(9), 3, 3));
    assert.throws(() => signedDistances(new Uint8Array(9).fill(1), 3, 3));
    assert.throws(() => signedDistances(new Uint8Array(9), 2, 3));
});
