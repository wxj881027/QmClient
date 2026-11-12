// 从开发者徽章的原始字形路径离线生成 Dev 与代码符号距离场，正常构建不依赖 sharp。
const fs = require('node:fs/promises');
const path = require('node:path');
const { generateDistanceField } = require('./generate_title_logo_sdf.cjs');

const LAYERS = [
    { id: 'developer-wordmark', name: 'developer_wordmark_sdf.png', x: 34, y: -4, size: 32 },
    { id: 'developer-code', name: 'developer_code_sdf.png', x: 65, y: 2, size: 20 },
];

function layerSvg(source, layer) {
    const paths = source.match(/<path\b[^>]*\/>/g) ?? [];
    const matches = paths.filter((element) => element.match(/\bid="([^"]+)"/)?.[1] === layer.id);
    if (matches.length !== 1)
        throw new Error(`开发者徽章需要唯一的 ${layer.id} 路径`);
    // 原 SVG 为 364 × 96，游戏设计坐标为 91 × 24。颜色和透明度在绘制时单独应用。
    return `<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="${layer.x} ${layer.y} ${layer.size} ${layer.size}" fill="none"><g transform="scale(0.25)">${matches[0]}</g></svg>`;
}

async function main() {
    const directory = path.resolve(__dirname, '../data/qmclient');
    const source = await fs.readFile(path.join(directory, 'developer_badge.svg'), 'utf8');
    for (const layer of LAYERS) {
        const destination = path.join(directory, 'title_badges', layer.name);
        await fs.writeFile(destination, await generateDistanceField(layerSvg(source, layer), layer.size));
        process.stdout.write(`Generated ${destination}\n`);
    }
}

module.exports = { layerSvg, LAYERS };
if (require.main === module) {
    main().catch((error) => {
        process.stderr.write(`${error.message}\n`);
        process.exitCode = 1;
    });
}
