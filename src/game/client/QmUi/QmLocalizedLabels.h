#ifndef GAME_CLIENT_QMUI_QMLOCALIZEDLABELS_H
#define GAME_CLIENT_QMUI_QMLOCALIZEDLABELS_H

#include <base/str.h>
#include <base/system.h>

#include <game/localization.h>

#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

// 固定本地化标签（页签名称等）的缓存。
// Localize 返回语言文件内存堆中的指针，每次加载语言文件都会重建该堆；即使再次加载的是同一
// 语言文件，旧指针也已失效。因此这里保存译文副本，而不是跨帧保留 Localize 的返回值。
class CQmLocalizedLabelCache
{
public:
	// 返回是否按当前语言文件重新取词。标签内容变化时由调用方决定是否同步失效其他缓存。
	bool Refresh(const char *pLanguageFile, const std::initializer_list<const char *> &Keys, bool Force = false)
	{
		return RefreshWith([](const char *pKey) { return Localize(pKey); }, pLanguageFile, Keys, Force);
	}

	// 可注入取词函数的版本，用于不依赖全局语言数据库验证缓存所有权。
	template<typename TLocalize>
	bool RefreshWith(TLocalize &&LocalizeText, const char *pLanguageFile, const std::initializer_list<const char *> &Keys, bool Force = false)
	{
		pLanguageFile = pLanguageFile != nullptr ? pLanguageFile : "";
		if(!Force && m_Initialized && str_comp(m_aLanguageFile, pLanguageFile) == 0)
			return false;

		m_Initialized = true;
		str_copy(m_aLanguageFile, pLanguageFile, sizeof(m_aLanguageFile));
		m_vLabels.clear();
		m_vLabels.reserve(Keys.size());
		for(const char *pKey : Keys)
		{
			const char *pLabel = LocalizeText(pKey);
			m_vLabels.emplace_back(pLabel != nullptr ? pLabel : (pKey != nullptr ? pKey : ""));
		}
		return true;
	}

	const char *Get(const size_t Index) const
	{
		return Index < m_vLabels.size() ? m_vLabels[Index].c_str() : "";
	}

	size_t Size() const { return m_vLabels.size(); }

private:
	std::vector<std::string> m_vLabels;
	char m_aLanguageFile[IO_MAX_PATH_LENGTH] = {};
	bool m_Initialized = false;
};

#endif
