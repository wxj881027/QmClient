// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_MARKDOWN_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_MARKDOWN_H

#include <cstddef>
#include <string>
#include <vector>

// 「新功能」广播内容的受限 Markdown 解析。
//
// 支持（够表达新功能说明与用法）：
//   #/##/###   标题
//   - * +      无序列表
//   1.         有序列表
//   >          引用
//   ---        分隔线
//   **粗体**  *斜体*  `行内代码`  [文字](http/https 链接)
//   [[settings:卡片 stableId|按钮文字]]  → 设置跳转按钮（整行或行内均可）
//
// 不支持（按普通文字处理，避免额外攻击面与带宽）：原始 HTML、图片、表格、代码块、嵌套列表。
namespace qm_md
{
	enum class EBlockKind
	{
		HEADING1,
		HEADING2,
		HEADING3,
		PARAGRAPH,
		BULLET,
		NUMBERED,
		QUOTE,
		SEPARATOR,
		SETTINGS_BUTTON,
	};

	struct SSpan
	{
		std::string m_Text;
		bool m_Bold = false;
		bool m_Italic = false;
		bool m_Code = false;
		std::string m_Link; // 非空表示该片段可点击，且一定以 http:// 或 https:// 开头
	};

	struct SBlock
	{
		EBlockKind m_Kind = EBlockKind::PARAGRAPH;
		std::vector<SSpan> m_vSpans;
		std::string m_SettingsCardId; // SETTINGS_BUTTON：设置卡片 stableId
		std::string m_SettingsLabel; // SETTINGS_BUTTON：按钮文字，可能为空（调用方给默认值）
		int m_Number = 0; // NUMBERED：显示序号
	};

	struct SParseLimits
	{
		size_t m_MaxBlocks = 400;
		size_t m_MaxSpans = 4000;
		size_t m_MaxBytes = 64 * 1024;
	};

	// 解析失败不抛异常：越界内容被截断，未知语法按普通文字保留。
	std::vector<SBlock> Parse(const char *pMarkdown, const SParseLimits &Limits = {});

	// 逐个 UTF-8 字符（码点）拆分，供渲染层按字符宽度换行使用。
	std::vector<std::string> SplitUtf8(const std::string &Text);
}

#endif
