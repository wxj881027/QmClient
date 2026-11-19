// Deck 集成使用真实卡片壳与 CUi，不构造完整菜单；误入文字池或菜单表单立即失败。
#include <game/client/QmUi/UiContext.h>
#include <game/client/components/menus.h>

#include <stdexcept>

namespace
{
	[[noreturn]] void UnexpectedMenuBoundary()
	{
		throw std::logic_error("full menu settings are outside the real Deck integration fixture");
	}
}

void CMenus::DoSettingsMenuLabel(int, int, int, const char *, const CUIRect *, const char *, float, int, const SLabelProperties &, int, bool)
{
	UnexpectedMenuBoundary();
}

int CMenus::DoSettingsButton_CheckBox(int, int, int, const void *, const char *, const char *, int, const CUIRect *)
{
	UnexpectedMenuBoundary();
}

const char *CMenus::TemporaryOverrideTooltip(const int *) const
{
	UnexpectedMenuBoundary();
}

IUiContext CMenus::SettingsUiContext(const char *, float)
{
	UnexpectedMenuBoundary();
}
