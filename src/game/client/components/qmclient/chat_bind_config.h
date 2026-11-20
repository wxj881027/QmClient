#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_BIND_CONFIG_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_BIND_CONFIG_H

#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/components/tclient/bindchat.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

// 全部行准备完成后才交给保存回调，排序只影响引用，不能改变运行中的命令优先顺序。
inline std::vector<std::string> QmPrepareChatBindConfig(const std::vector<CBindChat::CBind> &vBinds, std::vector<std::reference_wrapper<const CBindChat::CBind>> vDefaultBinds)
{
	const auto Compare = [](const CBindChat::CBind &A, const CBindChat::CBind &B) {
		const int Res = str_utf8_comp_nocase(A.m_aName, B.m_aName);
		return Res < 0 || (Res == 0 && str_comp(A.m_aName, B.m_aName) < 0);
	};
	std::sort(vDefaultBinds.begin(), vDefaultBinds.end(), Compare);
	std::vector<std::reference_wrapper<const CBindChat::CBind>> vSortedBinds;
	vSortedBinds.reserve(vBinds.size());
	for(const auto &Bind : vBinds)
		vSortedBinds.emplace_back(Bind);
	std::sort(vSortedBinds.begin(), vSortedBinds.end(), Compare);

	std::vector<std::string> vLines;
	vLines.reserve(vBinds.size() + vDefaultBinds.size());
	for(const CBindChat::CBind &Bind : vSortedBinds)
	{
		const auto It = std::lower_bound(vDefaultBinds.begin(), vDefaultBinds.end(), Bind, Compare);
		if(It != vDefaultBinds.end() && str_utf8_comp_nocase(It->get().m_aName, Bind.m_aName) == 0)
		{
			const bool IsDefault = Bind.CompContent(*It);
			vDefaultBinds.erase(It);
			if(IsDefault)
				continue;
		}
		vLines.push_back("bindchat " + qm_bind_editor::QuoteArgument(Bind.m_aName) + " " + qm_bind_editor::QuoteArgument(Bind.m_aCommand));
	}
	for(const CBindChat::CBind &Bind : vDefaultBinds)
		vLines.push_back("unbindchat " + qm_bind_editor::QuoteArgument(Bind.m_aName));
	return vLines;
}

#endif
