// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCRIPTING_IMPL_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCRIPTING_IMPL_H

#include "execution_scope.h"

#include <base/log.h>

#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <variant>

#define SCRIPTING_IMPL "chai"

class IStorage;

class CScriptingCtx
{
private:
	class CScriptingCtxData;
	std::unique_ptr<CScriptingCtxData> m_pData;

	// If you are getting link errors, define more in the .cpp file
	template<typename T, typename... Args>
	void AddFunctionInternal(const char *pName, const std::function<T(Args...)> &Function);
	CScriptingCtx();
	~CScriptingCtx();
	void Run(IStorage *pStorage, const char *pFilename, const char *pArgs);

public:
	using Any = std::variant<std::nullptr_t, std::string, bool, int, float>;

	static const char *Implementation();

	template<typename T>
	void AddGlobal(const char *pName, const T &Object);
	template<typename T>
	void AddFunction(const char *pName, const T &Function)
	{
		AddFunctionInternal(pName, std::function(Function));
	}
	template<typename TConfigure>
	static void Execute(IStorage *pStorage, const char *pFilename, const char *pArgs, TConfigure &&Configure)
	{
		CQmScriptExecutionScope Scope;
		if(!Scope.Entered())
		{
			log_error(SCRIPTING_IMPL, "Failed to run '%s': %s", pFilename, CQmScriptExecutionScope::ERROR);
			return;
		}
		try
		{
			CScriptingCtx Context;
			Configure(Context);
			Context.Run(pStorage, pFilename, pArgs);
		}
		catch(const std::exception &Error)
		{
			log_error(SCRIPTING_IMPL, "Failed to run '%s': %s", pFilename, Error.what());
		}
		catch(...)
		{
			log_error(SCRIPTING_IMPL, "Failed to run '%s': unknown exception", pFilename);
		}
	}
};

#endif
