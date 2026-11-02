// 从原 SVG 离线生成头衔 Logo 的距离场；客户端和正常构建不依赖 sharp。
// 用法：在可解析 sharp 模块的 Node.js 环境运行 node qmclient_scripts/generate_title_logo_sdf.cjs。
const fs = require('node:fs/promises');
const path = require('node:path');

const SOURCE_SIZE = 16;
const PADDING = 2;
const DISTANCE_RANGE = 4;
const RASTER_SIZE = 1024;
const FIELD_SIZE = 256;

// 对抛物线下包络分别做列、行变换，得到到指定像素集合的平方欧氏距离。
function distancesTo(mask, width, height, target) {
    const data = new Float64Array(width * height);
    const input = new Float64Array(Math.max(width, height));
    const output = new Float64Array(input.length);
    const sites = new Int32Array(input.length);
    const boundaries = new Float64Array(input.length + 1);
    const transform = (length) => {
        let last = 0;
        sites[0] = 0;
        boundaries[0] = -Infinity;
        boundaries[1] = Infinity;
        for (let q = 1; q < length; ++q) {
            let crossing;
            do {
                const v = sites[last];
                crossing = (input[q] + q * q - input[v] - v * v) / (2 * (q - v));
                if (crossing > boundaries[last]) break;
                --last;
            } while (last >= 0);
            ++last;
            sites[last] = q;
            boundaries[last] = crossing;
            boundaries[last + 1] = Infinity;
        }
        last = 0;
        for (let q = 0; q < length; ++q) {
            while (boundaries[last + 1] < q) ++last;
            output[q] = (q - sites[last]) ** 2 + input[sites[last]];
        }
    };
    for (let x = 0; x < width; ++x) {
        for (let y = 0; y < height; ++y)
            input[y] = Boolean(mask[y * width + x]) === target ? 0 : 1e12;
        transform(height);
        for (let y = 0; y < height; ++y) data[y * width + x] = output[y];
    }
    for (let y = 0; y < height; ++y) {
        for (let x = 0; x < width; ++x) input[x] = data[y * width + x];
        transform(width);
        for (let x = 0; x < width; ++x) data[y * width + x] = output[x];
    }
    return data;
}

function signedDistances(mask, width, height) {
    if (!Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0 || mask.length !== width * height)
        throw new Error('距离场输入尺寸无效');
    if (!mask.some(Boolean) || !mask.some((value) => !value))
        throw new Error('距离场需要同时包含轮廓内部和外部');
    const inside = distancesTo(mask, width, height, true);
    const outside = distancesTo(mask, width, height, false);
    // 轮廓落在两像素中心之间，消除二值采样固有的半像素膨胀。
    return Float32Array.from(mask, (value, i) => value ? Math.sqrt(outside[i]) - 0.5 : 0.5 - Math.sqrt(inside[i]));
}

// 所有徽章距离场共用编码方式；SVG 的 viewBox 必须是 extent × extent 的正方形。
async function generateDistanceField(svg, extent) {
    const sharp = require('sharp');
    const { data, info } = await sharp(Buffer.from(svg)).ensureAlpha().raw().toBuffer({ resolveWithObject: true });
    if (info.width !== RASTER_SIZE || info.height !== RASTER_SIZE || info.channels !== 4)
        throw new Error('SVG 光栅化结果尺寸或格式错误');
    const mask = Uint8Array.from({ length: RASTER_SIZE * RASTER_SIZE }, (_, i) => data[i * 4 + 3] >= 128 ? 1 : 0);
    const distances = signedDistances(mask, RASTER_SIZE, RASTER_SIZE);
    const pixels = Buffer.alloc(FIELD_SIZE * FIELD_SIZE * 4);
    for (let y = 0; y < FIELD_SIZE; ++y) {
        for (let x = 0; x < FIELD_SIZE; ++x) {
            const sx = (x + 0.5) * RASTER_SIZE / FIELD_SIZE - 0.5;
            const sy = (y + 0.5) * RASTER_SIZE / FIELD_SIZE - 0.5;
            const ix = Math.floor(sx), iy = Math.floor(sy);
            const fx = sx - ix, fy = sy - iy;
            const top = distances[iy * RASTER_SIZE + ix] * (1 - fx) + distances[iy * RASTER_SIZE + ix + 1] * fx;
            const bottom = distances[(iy + 1) * RASTER_SIZE + ix] * (1 - fx) + distances[(iy + 1) * RASTER_SIZE + ix + 1] * fx;
            const distance = (top * (1 - fy) + bottom * fy) * extent / RASTER_SIZE;
            const encoded = Math.round(Math.max(0, Math.min(1, 0.5 + distance / DISTANCE_RANGE)) * 255);
            const offset = (y * FIELD_SIZE + x) * 4;
            pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = encoded;
            // 距离值保存在 RGB，alpha 恒为 1，避免纹理处理预乘轮廓数据。
            pixels[offset + 3] = 255;
        }
    }
    return sharp(pixels, { raw: { width: FIELD_SIZE, height: FIELD_SIZE, channels: 4 } }).png().toBuffer();
}

async function main() {
    const directory = path.resolve(__dirname, '../data/qmclient/title_badges');
    const source = await fs.readFile(path.join(directory, 'logo.svg'), 'utf8');
    const extent = SOURCE_SIZE + 2 * PADDING;
    const svg = source.replace(/<svg\b[^>]*>/, `<svg xmlns="http://www.w3.org/2000/svg" width="${RASTER_SIZE}" height="${RASTER_SIZE}" viewBox="${-PADDING} ${-PADDING} ${extent} ${extent}" fill="none">`);
    const destination = path.join(directory, 'logo_sdf.png');
    await fs.writeFile(destination, await generateDistanceField(svg, extent));
    process.stdout.write(`Generated ${destination}\n`);
}

module.exports = { signedDistances, generateDistanceField };
if (require.main === module) {
    main().catch((error) => {
        process.stderr.write(`${error.message}\n`);
        process.exitCode = 1;
    });
}
