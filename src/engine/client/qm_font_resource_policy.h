#ifndef ENGINE_CLIENT_QM_FONT_RESOURCE_POLICY_H
#define ENGINE_CLIENT_QM_FONT_RESOURCE_POLICY_H

// 图标样式缺失时保持可用：优先使用配置指定的 face，其次使用当前 face，
// 最后回退到 regular icon face。模板只处理指针选择，不触碰 face 生命周期。
template<typename TFace>
constexpr TFace ResolveFontFaceWithFallback(TFace Candidate, TFace Current, TFace Regular)
{
	return Candidate != nullptr ? Candidate : (Current != nullptr ? Current : Regular);
}

#endif
