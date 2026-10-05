#ifndef ENGINE_CLIENT_QM_FONT_RESOURCE_POLICY_H
#define ENGINE_CLIENT_QM_FONT_RESOURCE_POLICY_H

#include <base/str.h>

// QmClient：旧版本曾把随包 Phosphor 复制到用户存档目录。
// 该路径只属于历史残留，不能作为用户字体加载，也不能参与同族优先级解析。
inline bool IsLegacyBundledIconFontPath(const char *pPath)
{
	return pPath != nullptr && str_startswith_nocase(pPath, "qmclient/fonts/Phosphor/") != nullptr;
}

// 图标样式缺失时保持可用：优先使用配置指定的 face，其次使用当前 face，
// 最后回退到 regular icon face。模板只处理指针选择，不触碰 face 生命周期。
template<typename TFace>
constexpr TFace ResolveFontFaceWithFallback(TFace Candidate, TFace Current, TFace Regular)
{
	return Candidate != nullptr ? Candidate : (Current != nullptr ? Current : Regular);
}

#endif
