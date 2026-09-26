#ifndef ENGINE_CLIENT_QM_FESTIVE_MESSAGE_BOX_H
#define ENGINE_CLIENT_QM_FESTIVE_MESSAGE_BOX_H

#include <engine/graphics.h>

// 显示不依赖游戏图形后端的 QmClient 喜庆崩溃窗口。
// 返回空值时调用方应回退到 SDL 原生消息框。
std::optional<int> ShowQmFestiveMessageBox(const IGraphics::CMessageBox &MessageBox);

#endif
