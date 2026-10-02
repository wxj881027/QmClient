#include <game/client/ui.h>

// testrunner 编译 CUIRect/UiSurface 的逻辑，但不启动完整客户端 UI。
// 这些最小实现避免把完整 ui.cpp 及其运行时依赖拖进测试链接。
float CUi::PixelSize()
{
	return 1.0f;
}

void CUi::RenderGaussianBlur(const CUIRect &, float, int, float)
{
}
