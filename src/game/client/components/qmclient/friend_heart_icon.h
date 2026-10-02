#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_HEART_ICON_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_HEART_ICON_H

// 好友爱心图标：U+2665 BLACK HEART SUIT（UTF-8 字节 E2 99 A5）。
// 图标字体 Phosphor 只有中空心形——U+E2A8 在 Phosphor-Regular/Bold 里都是 2 轮廓（中空），
// 相邻的心形码位（U+E2AA / U+E2AC / U+EBE8）同样中空，字体里不存在实心爱心码位；
// 因此好友爱心改用默认字体 DejaVu Sans 的实体心形（同样是 1 轮廓实心）。
// 该码位在图标字体中缺失，渲染时由 ITextRender::GetCharGlyph 在选中字体之后回退到默认字体，
// 所以调用点需要先把字体预设切回 EFontPreset::DEFAULT_FONT，不要继续用 ICON_FONT。
constexpr const char *QM_FRIEND_HEART_ICON = "♥";
constexpr const char *QM_FRIEND_HEART_ICON_WITH_SPACE = "♥ ";

#endif
