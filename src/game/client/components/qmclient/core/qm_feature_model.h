/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_FEATURE_MODEL_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_FEATURE_MODEL_H

#include <string>
#include <utility>

// feature 状态模型属于 Qm 核心层，UI 只借用其注册引用。
struct SFeatureModel
{
	std::string m_Id;
	std::string m_TitleKey;
	bool m_Enabled = false;
	bool m_Available = true;

	SFeatureModel() = default;
	SFeatureModel(const char *pId, const char *pTitleKey, bool Enabled, bool Available) :
		m_Id(pId ? pId : ""),
		m_TitleKey(pTitleKey ? pTitleKey : ""),
		m_Enabled(Enabled),
		m_Available(Available)
	{
	}
	SFeatureModel(std::string Id, std::string TitleKey, bool Enabled, bool Available) :
		m_Id(std::move(Id)),
		m_TitleKey(std::move(TitleKey)),
		m_Enabled(Enabled),
		m_Available(Available)
	{
	}
};

using SQmFeatureModel = SFeatureModel;

#endif
