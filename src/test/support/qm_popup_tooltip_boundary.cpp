#include <game/client/QmUi/UiContext.h>
#include <game/client/components/menus.h>
#include <game/client/components/tooltips.h>

#include <gtest/gtest.h>

// 独立目标只使用提示组件的标签缓存，不启动完整提示生命周期。
CTooltips::CTooltips()
{
	CTooltips::OnReset();
}

// 胶囊表面使用真实公共绘制；完整菜单按钮不属于独立目标，误入即失败。
int CMenus::DoButton_MenuTab(CButtonContainer *, const char *, int, const CUIRect *, int, SUIAnimator *, const ColorRGBA *, const ColorRGBA *, const ColorRGBA *, float, const CCommunityIcon *, CUIElement *, float, bool)
{
	ADD_FAILURE() << "Full menu tab buttons are outside the isolated UI target";
	return 0;
}

// 公共卡片连接真实 Deck 与绘制；完整菜单的文字池、表单编排属于外部边界。
// 测试上下文不提供 m_pMenus，误入这些入口立即失败，不能替代被测行为。
void CMenus::DoSettingsMenuLabel(int, int, int, const char *, const CUIRect *, const char *, float, int, const SLabelProperties &, int)
{
	ADD_FAILURE() << "The menu text pool is outside the isolated UI target";
}

IUiContext CMenus::SettingsUiContext(const char *, float)
{
	ADD_FAILURE() << "Menu-owned forms are outside the isolated UI target";
	return {};
}

const char *CMenus::TemporaryOverrideTooltip(const int *) const
{
	ADD_FAILURE() << "Menu-owned config overrides are outside the isolated UI target";
	return nullptr;
}

// 独立输入／下拉集成目标没有完整菜单与配置管理器；提示作为外部边界，
// 不参与这些测试的断言。按钮和输入仍链接真实生产实现。
void CTooltips::DoConfigToolTip(const void *, const CUIRect *, const void *, const void *)
{
}

void CTooltips::DoSettingsToolTipForConfig(const void *, const CUIRect *, const void *, const CUIRect *, const void *)
{
}

// 独立 UI 目标不创建提示组件；左右按钮仍通过真实公共入口独立登记。
void CTooltips::DoToolTipForRect(const void *, const CUIRect *, const char *, float)
{
}

void CTooltips::DoToolTip(const void *, const CUIRect *, const char *, float)
{
}

void CTooltips::DoSmallToolTip(const void *, const CUIRect *, const char *, float, float)
{
}

void CTooltips::DoInfoToolTipForRect(const void *, const CUIRect *, const char *, float, float)
{
}

// 只隔离气泡注册与绘制；标签解析仍使用生产缓存与策略，供原始名称作用域验证。
const char *CTooltips::PrepareCardLabel(const CUIRect *, const char *pText, bool)
{
	return pText != nullptr ? m_CardLabelTexts.Get(pText).m_Label.c_str() : pText;
}

// 为 Itanium ABI 的 key function 提供定义，sanitizer 的 vptr 检查需要完整 RTTI。
// 本目标不构造或测试完整 Tooltip 组件；真实文字布局由独立协作测试覆盖。
void CTooltips::OnReset()
{
}

void CTooltips::OnRender()
{
}

// vtable 的窗口/退出入口会引用清理函数；本目标不构造完整提示组件。
void CTooltips::ClearActiveTooltip()
{
}

// 独立目标没有客户端 owner；仅提供未使用的基类 key function 以生成 RTTI。
void CComponentInterfaces::OnInterfacesInit(CGameClient *)
{
}
