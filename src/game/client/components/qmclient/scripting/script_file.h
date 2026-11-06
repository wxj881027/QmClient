#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCRIPTING_SCRIPT_FILE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCRIPTING_SCRIPT_FILE_H

#include <string>

// 读取方返回拥有文本的对象，求值正常结束或抛异常后都释放文本。
template<typename TRead, typename TEvaluate>
auto QmEvaluateScriptFile(const char *pFilename, TRead &&Read, TEvaluate &&Evaluate)
{
	auto pScript = Read(pFilename);
	if(!pScript || !*pScript)
		throw std::string("Failed to open script '") + pFilename + "'";
	return Evaluate(pScript.get());
}

#endif
