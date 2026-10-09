#include <game/client/components/tooltips.h>

// 独立输入／下拉集成目标没有完整菜单与配置管理器；提示作为外部边界，
// 不参与这些测试的断言。按钮和输入仍链接真实生产实现。
void CTooltips::DoConfigToolTip(const void *, const CUIRect *, const void *, const void *)
{
}

void CTooltips::DoSettingsToolTipForConfig(const void *, const CUIRect *, const void *, const CUIRect *, const void *)
{
}

// 为 Itanium ABI 的 key function 提供定义，sanitizer 的 vptr 检查需要完整 RTTI。
// 本目标不构造或测试完整 Tooltip 组件；真实文字布局由独立协作测试覆盖。
void CTooltips::OnReset()
{
}

void CTooltips::OnRender()
{
}

// 独立目标没有客户端 owner；仅提供未使用的基类 key function 以生成 RTTI。
void CComponentInterfaces::OnInterfacesInit(CGameClient *)
{
}
