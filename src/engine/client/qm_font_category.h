#ifndef ENGINE_CLIENT_QM_FONT_CATEGORY_H
#define ENGINE_CLIENT_QM_FONT_CATEGORY_H

// QmClient: 按脚本/用途给字符分类，供分类字体（中文 / 英文 / 图标符号）
// 在字形解析时选择优先字体面。分类只影响「优先尝试」的顺序：
// 分类字体没有该字形时仍会沿原有回退链解析，不会造成缺字。

inline bool QmIsCjkCodepoint(int Chr)
{
	// 中日韩统一表意文字、假名、谚文、CJK 标点与全角形式。
	return (Chr >= 0x2E80 && Chr <= 0x2EFF) || // CJK 部首补充
	       (Chr >= 0x3000 && Chr <= 0x303F) || // CJK 符号和标点（、。「」等）
	       (Chr >= 0x3040 && Chr <= 0x30FF) || // 平假名/片假名
	       (Chr >= 0x3130 && Chr <= 0x318F) || // 谚文兼容字母
	       (Chr >= 0x3400 && Chr <= 0x4DBF) || // CJK 扩展 A
	       (Chr >= 0x4E00 && Chr <= 0x9FFF) || // CJK 统一表意文字
	       (Chr >= 0xAC00 && Chr <= 0xD7AF) || // 谚文音节
	       (Chr >= 0xF900 && Chr <= 0xFAFF) || // CJK 兼容表意文字
	       (Chr >= 0xFF00 && Chr <= 0xFFEF) || // 全角/半角形式（，！？等）
	       (Chr >= 0x20000 && Chr <= 0x2FA1F); // CJK 扩展 B-F 及兼容补充
}

inline bool QmIsIconSymbolCodepoint(int Chr)
{
	// 文本中的符号字形：星号、心形、对勾、几何形状、表情与私用区（图标字体）。
	// 刻意排除制表符/方块元素（0x2500-0x259F），等宽字体的文本画框仍走主字体。
	return (Chr >= 0x2190 && Chr <= 0x21FF) || // 箭头
	       (Chr >= 0x25A0 && Chr <= 0x25FF) || // 几何形状（●○■□▲）
	       (Chr >= 0x2600 && Chr <= 0x27BF) || // 杂项符号与印刷花饰（★☆♥✔✗）
	       (Chr >= 0x2B00 && Chr <= 0x2BFF) || // 杂项符号和箭头（⭐）
	       (Chr >= 0x1F000 && Chr <= 0x1FAFF) || // 表情符号区
	       (Chr >= 0xE000 && Chr <= 0xF8FF); // 私用区（图标字体）
}

#endif
