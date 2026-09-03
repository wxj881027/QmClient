/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_I18N_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_I18N_H

#include <base/str.h>

class CQmI18n final
{
public:
	using FLookup = const char *(*)(const char *pKey, const char *pContext);

	void SetLookup(FLookup pLookup)
	{
		m_pLookup = pLookup;
	}

	const char *Text(const char *pKey, const char *pFallback, const char *pContext = "") const
	{
		if(pKey && m_pLookup)
		{
			const char *pText = m_pLookup(pKey, pContext ? pContext : "");
			// 官方 Localize 在缺失时返回原始 key，不能把内部 key 当成文案。
			if(pText && pText[0] && str_comp(pText, pKey) != 0)
				return pText;
		}
		return pFallback;
	}

private:
	FLookup m_pLookup = nullptr;
};

#endif
